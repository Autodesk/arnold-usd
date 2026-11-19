// Regression test for ARNOLD-19000: the render delegate ignored the arnold:filter of a RenderVar for the color, depth
// and primId outputs of the RenderProducts husk gives it (the delegateRenderProducts render setting), and used the
// RenderProduct filter or the defaults instead (box_filter for color, closest_filter for depth and primId). Only the
// other AOVs honored it.
//
// husk is the only host setting delegateRenderProducts, and its layout (a VtArray of HdAovSettingsMap, see
// https://www.sidefx.com/docs/hdk/_h_d_k__u_s_d_hydra.html#HDK_USDHydraHuskDRP) isn't wrapped in python, so this test
// builds it in C++, gives it to an HdArnoldRenderDelegate, runs one Execute() of a render pass and checks the type of
// the filter of every output of the options.
//
// Like test_2719, no Arnold plugin is loaded: this executable statically links USD, and loading usd_proc, which embeds
// its own copy of USD, could crash the process (see testsuite/common/usd_proc_guard.cpp).
#include <ai.h>

#include <render_buffer.h>
#include <render_delegate.h>
#include <render_param.h>

#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool g_success = true;

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        AiMsgError("[test_19000] %s", message.c_str());
        g_success = false;
    }
    return condition;
}

using DataType = VtArray<HdAovSettingsMap>;

// A RenderVar as husk sends it. An empty filter means no arnold:filter on the RenderVar
HdAovSettingsMap RenderVar(const std::string& sourceName, const std::string& dataType, const std::string& filter)
{
    HdAovSettingsMap aovSettings;
    aovSettings[TfToken("driver:parameters:aov:name")] = VtValue(sourceName);
    if (!filter.empty())
        aovSettings[TfToken("arnold:filter")] = VtValue(filter);

    HdAovSettingsMap renderVar;
    renderVar[TfToken("sourceName")] = VtValue(sourceName);
    renderVar[TfToken("sourceType")] = VtValue(TfToken("raw"));
    renderVar[TfToken("dataType")] = VtValue(TfToken(dataType));
    renderVar[TfToken("aovDescriptor.aovSettings")] = VtValue(aovSettings);
    return renderVar;
}

// An arnold RenderProduct with a single RenderVar, so its driver identifies the output. An empty filter means no
// arnold:filter on the RenderProduct
HdAovSettingsMap Product(const std::string& productName, const HdAovSettingsMap& renderVar, const std::string& filter)
{
    HdAovSettingsMap product;
    product[TfToken("productType")] = VtValue(TfToken("arnold"));
    product[TfToken("productName")] = VtValue(TfToken(productName));
    product[TfToken("arnold:driver")] = VtValue(std::string("driver_exr"));
    if (!filter.empty())
        product[TfToken("arnold:filter")] = VtValue(filter);
    product[TfToken("orderedVars")] = VtValue(DataType{renderVar});
    return product;
}

struct Case {
    std::string productName;
    std::string sourceName;
    std::string dataType;
    std::string renderVarFilter;
    std::string productFilter;
    std::string expectedFilter;
};

HdArnoldRenderParam::Status RunToCompletion(HdArnoldRenderParam* renderParam, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    HdArnoldRenderParam::Status status = HdArnoldRenderParam::Status::Converging;
    while (status == HdArnoldRenderParam::Status::Converging) {
        status = renderParam->UpdateRender();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        if (elapsed.count() > timeoutMs)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return status;
}

} // namespace

int main(int, char**)
{
    // See test_2719: only the usd_proc guard is in the plugin path, so no usd_proc gets loaded
    const char* guardPath = std::getenv("USD_PROCEDURAL_GUARD_PATH");
#ifdef _WIN32
    _putenv_s("ARNOLD_PLUGIN_PATH", guardPath ? guardPath : "");
#else
    if (guardPath)
        setenv("ARNOLD_PLUGIN_PATH", guardPath, 1);
    else
        unsetenv("ARNOLD_PLUGIN_PATH");
#endif
    // The expected defaults are box_filter for color and closest_filter for depth and primId
#ifdef _WIN32
    _putenv_s("HDARNOLD_default_filter", "");
#else
    unsetenv("HDARNOLD_default_filter");
#endif

    AiBegin(AI_SESSION_INTERACTIVE);
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);

    // Precedence: RenderVar arnold:filter > RenderProduct arnold:filter > default
    const std::vector<Case> cases = {
        // The RenderVar filter, which was ignored for color, depth and primId
        {"color_var_filter.exr", "color", "color4f", "blackman_harris_filter", "", "blackman_harris_filter"},
        {"depth_var_filter.exr", "depth", "float", "gaussian_filter", "", "gaussian_filter"},
        // primId is an INT AOV, which most filters don't support: farthest_filter does
        {"primid_var_filter.exr", "primId", "int", "farthest_filter", "", "farthest_filter"},
        {"N_var_filter.exr", "N", "normal3f", "gaussian_filter", "", "gaussian_filter"},
        // The RenderVar filter wins over the RenderProduct one
        {"color_both_filters.exr", "color", "color4f", "blackman_harris_filter", "gaussian_filter",
         "blackman_harris_filter"},
        {"depth_both_filters.exr", "depth", "float", "triangle_filter", "gaussian_filter", "triangle_filter"},
        // The RenderProduct filter, for RenderVars without their own
        {"color_product_filter.exr", "color", "color4f", "", "gaussian_filter", "gaussian_filter"},
        {"depth_product_filter.exr", "depth", "float", "", "gaussian_filter", "gaussian_filter"},
        // The defaults
        {"color_default.exr", "color", "color4f", "", "", "box_filter"},
        {"depth_default.exr", "depth", "float", "", "", "closest_filter"},
        {"primid_default.exr", "primId", "int", "", "", "closest_filter"},
        {"N_default.exr", "N", "normal3f", "", "", "box_filter"},
    };

    {
        HdArnoldRenderDelegate delegate(false, TfToken());
        AtUniverse* universe = delegate.GetUniverse();
        delegate.SetRenderSetting(TfToken("arnold:global:AA_samples"), VtValue(1));
        delegate.SetRenderSetting(TfToken("resolution"), VtValue(GfVec2i(16, 16)));

        DataType products;
        for (const auto& c : cases)
            products.push_back(Product(c.productName, RenderVar(c.sourceName, c.dataType, c.renderVarFilter), c.productFilter));
        delegate.SetRenderSetting(TfToken("delegateRenderProducts"), VtValue(products));

        HdRenderIndex* renderIndex = HdRenderIndex::New(&delegate, HdDriverVector());
        Check(renderIndex != nullptr, "Could not create the render index");
        if (renderIndex != nullptr) {
            HdRenderPassSharedPtr renderPass = delegate.CreateRenderPass(
                renderIndex, HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->hull)));
            HdRenderPassStateSharedPtr renderPassState = delegate.CreateRenderPassState();
            // husk binds the AOVs of the render vars to render buffers too, the render pass expects at least one
            HdArnoldRenderBuffer colorBuffer(&delegate, SdfPath("/colorBuffer"));
            colorBuffer.Allocate(GfVec3i(16, 16, 1), HdFormatFloat16Vec4, false);
            HdRenderPassAovBinding colorBinding;
            colorBinding.aovName = HdAovTokens->color;
            colorBinding.renderBuffer = &colorBuffer;
            colorBinding.renderBufferId = colorBuffer.GetId();
            renderPassState->SetAovBindings({colorBinding});
            renderPass->Execute(renderPassState, TfTokenVector{HdRenderTagTokens->geometry});

            // The type of the filter of the output of every product, found from the filename of its driver
            std::map<std::string, std::string> filterTypes;
            const AtArray* outputs = AiNodeGetArray(AiUniverseGetOptions(universe), AtString("outputs"));
            const uint32_t numOutputs = outputs ? AiArrayGetNumElements(outputs) : 0;
            for (uint32_t i = 0; i < numOutputs; ++i) {
                const std::string output = AiArrayGetStr(outputs, i).c_str();
                // "<aov> <type> <filter> <driver> [<layer>] [HALF]"
                std::istringstream stream(output);
                std::vector<std::string> tokens;
                for (std::string token; stream >> token;)
                    tokens.push_back(token);
                if (tokens.size() < 4)
                    continue;
                const AtNode* driver = AiNodeLookUpByName(universe, AtString(tokens[3].c_str()));
                if (driver == nullptr || !AiNodeIs(driver, AtString("driver_exr")))
                    continue;
                const AtNode* filter = AiNodeLookUpByName(universe, AtString(tokens[2].c_str()));
                Check(filter != nullptr, "No filter node " + tokens[2] + " for the output \"" + output + "\"");
                const std::string driverFilename = AiNodeGetStr(driver, AtString("filename")).c_str();
                filterTypes[driverFilename] = filter ? AiNodeEntryGetName(AiNodeGetNodeEntry(filter)) : "";
                AiMsgInfo("[test_19000] %s: \"%s\" (%s)", driverFilename.c_str(), output.c_str(),
                    filterTypes[driverFilename].c_str());
            }

            for (const auto& c : cases) {
                const auto it = filterTypes.find(c.productName);
                if (!Check(it != filterTypes.end(), "No output for the product " + c.productName))
                    continue;
                Check(it->second == c.expectedFilter,
                    "The " + c.sourceName + " output of " + c.productName + " is filtered by " + it->second +
                        " instead of " + c.expectedFilter);
            }

            // Let the render finish so the delegate tears down cleanly
            auto* renderParam = static_cast<HdArnoldRenderParam*>(delegate.GetRenderParam());
            const auto status = RunToCompletion(renderParam, 60000);
            Check(status == HdArnoldRenderParam::Status::Converged, "Render did not converge by the end of the test");

            renderPass.reset();
            delete renderIndex;
        }
    }

    AiEnd();
    return g_success ? 0 : 1;
}
