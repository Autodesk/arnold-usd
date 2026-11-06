// Interactive edits of a material network: connections added and removed, the arnold surface terminal added, moved to a
// shader already in the network and removed, a binding to a material defined later, and the bound material removed.
// See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kSphere("/World/sphere");
const SdfPath kMaterial("/World/materials/mat");
const SdfPath kPreview("/World/materials/mat/surface");
const SdfPath kReader("/World/materials/mat/reader");
const SdfPath kStandard("/World/materials/mat/standard");
const SdfPath kFlat("/World/materials/mat/flat");
const SdfPath kLaterMaterial("/World/materials/later");

UsdShadeShader Shader(const UsdStageRefPtr& stage, const SdfPath& path) { return UsdShadeShader::Get(stage, path); }

UsdShadeShader DefineShader(const UsdStageRefPtr& stage, const SdfPath& path, const char* id)
{
    UsdShadeShader shader = UsdShadeShader::Define(stage, path);
    shader.CreateIdAttr(VtValue(TfToken(id)));
    return shader;
}

UsdShadeOutput ArnoldSurfaceOutput(const UsdStageRefPtr& stage)
{
    return UsdShadeMaterial::Get(stage, kMaterial).CreateSurfaceOutput(TfToken("arnold"));
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const std::vector<KnownIssue> disconnectIssue = {{ProceduralHydra2 | DelegateHydra2,
        "with hydra 2, a disconnected input doesn't get its authored value back"}};
    const std::vector<KnownIssue> arnoldTerminalIssue = {{ProceduralHydra2,
        "the hydra 2 procedural renders the error shader when an arnold surface terminal is added, then black"}};
    const std::vector<KnownIssue> movedTerminalIssue = {{ProceduralHydra2 | DelegateHydra2,
        "with hydra 2, the arnold surface terminal moved to a shader already in the network, or removed, renders "
        "black"}};
    const std::vector<KnownIssue> laterMaterialIssue = {{ProceduralHydra1 | DelegateHydra1,
        "with hydra 1, a binding to a material that doesn't exist yet isn't updated when the material is defined"}};

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        UsdGeomSphere sphere = UsdGeomSphere::Define(stage, kSphere);
        sphere.CreateRadiusAttr().Set(1.0);
        // Only seen through the primvar reader, and the fallback shader when the bound material doesn't exist
        sphere.CreateDisplayColorPrimvar().Set(VtVec3fArray{GfVec3f(0.05f, 0.8f, 0.05f)});
        BindMaterial(stage, kSphere, DefinePreviewMaterial(stage, kMaterial, GfVec3f(0.8f, 0.05f, 0.05f)));
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr, ExpectColor(Color::Red)},
        {"connection of the diffuseColor to a new primvar reader of the displayColor",
            [](const UsdStageRefPtr& stage) {
                UsdShadeShader reader = DefineShader(stage, kReader, "UsdPrimvarReader_float3");
                reader.CreateInput(TfToken("varname"), SdfValueTypeNames->String).Set(std::string("displayColor"));
                reader.CreateInput(TfToken("fallback"), SdfValueTypeNames->Float3).Set(GfVec3f(0.05f, 0.05f, 0.8f));
                reader.CreateOutput(TfToken("result"), SdfValueTypeNames->Float3);
                Shader(stage, kPreview)
                    .GetInput(TfToken("diffuseColor"))
                    .ConnectToSource(reader.ConnectableAPI(), TfToken("result"));
            },
            ExpectColor(Color::Green)},
        {"disconnection of the diffuseColor, back to its value",
            [](const UsdStageRefPtr& stage) {
                Shader(stage, kPreview).GetInput(TfToken("diffuseColor")).DisconnectSource();
            },
            ExpectColor(Color::Red), disconnectIssue},
        {"new arnold surface terminal, taking precedence over the universal one",
            [](const UsdStageRefPtr& stage) {
                UsdShadeShader standard = DefineShader(stage, kStandard, "arnold:standard_surface");
                standard.CreateInput(TfToken("base_color"), SdfValueTypeNames->Color3f).Set(GfVec3f(0.05f, 0.05f, 0.8f));
                standard.CreateInput(TfToken("specular"), SdfValueTypeNames->Float).Set(0.f);
                ArnoldSurfaceOutput(stage).ConnectToSource(
                    standard.CreateOutput(TfToken("out"), SdfValueTypeNames->Token));
            },
            ExpectColor(Color::Blue), arnoldTerminalIssue},
        {"base_color connected to a new flat shader, with a base weight of 0",
            [](const UsdStageRefPtr& stage) {
                UsdShadeShader flat = DefineShader(stage, kFlat, "arnold:flat");
                flat.CreateInput(TfToken("color"), SdfValueTypeNames->Color3f).Set(GfVec3f(0.05f, 0.8f, 0.05f));
                UsdShadeShader standard = Shader(stage, kStandard);
                standard.CreateInput(TfToken("base"), SdfValueTypeNames->Float).Set(0.f);
                standard.GetInput(TfToken("base_color"))
                    .ConnectToSource(flat.CreateOutput(TfToken("out"), SdfValueTypeNames->Color3f));
            },
            ExpectColor(Color::Black)},
        {"arnold surface terminal moved to the flat shader, already in the network",
            [](const UsdStageRefPtr& stage) {
                ArnoldSurfaceOutput(stage).ConnectToSource(
                    Shader(stage, kFlat).GetOutput(TfToken("out")));
            },
            ExpectColor(Color::Green), movedTerminalIssue},
        {"arnold surface terminal removed, back to the universal one",
            [](const UsdStageRefPtr& stage) { ArnoldSurfaceOutput(stage).DisconnectSource(); },
            ExpectColor(Color::Red), movedTerminalIssue},
        {"binding to a material that doesn't exist, rendered with the fallback shader showing the displayColor",
            [](const UsdStageRefPtr& stage) {
                UsdShadeMaterialBindingAPI(stage->GetPrimAtPath(kSphere))
                    .GetDirectBindingRel()
                    .SetTargets({kLaterMaterial});
            },
            ExpectColor(Color::Green)},
        {"the bound material is defined",
            [](const UsdStageRefPtr& stage) {
                DefinePreviewMaterial(stage, kLaterMaterial, GfVec3f(0.05f, 0.05f, 0.8f));
            },
            ExpectColor(Color::Blue), laterMaterialIssue},
        {"the bound material is removed, back to the fallback shader",
            [](const UsdStageRefPtr& stage) { stage->RemovePrim(kLaterMaterial); },
            ExpectColor(Color::Green)},
    };
    scenario.skippedModes = {{ProceduralLegacyReader, "this test covers the hydra code paths, the legacy reader fails "
                                                      "most of its edits"}};
    return Run(scenario);
}
