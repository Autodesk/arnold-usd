// Interactive edits of a mesh light whose light filter comes from its node graph (primvars:arnold:light:shaders).
// The filter must be kept when another primvar of the mesh light is edited. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/shader.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kWall("/World/wall");
const SdfPath kLamp("/World/lamp");
const SdfPath kLightShaders("/World/lightShaders");
const SdfPath kDecay("/World/lightShaders/decay");

// A quad in the plane z, from (-halfSize, -halfSize) to (halfSize, halfSize), facing +Z or -Z
void DefineQuad(const UsdStageRefPtr& stage, const SdfPath& path, float halfSize, float z, bool facingPositiveZ)
{
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, path);
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    VtVec3fArray points = {GfVec3f(-halfSize, -halfSize, z), GfVec3f(halfSize, -halfSize, z),
        GfVec3f(halfSize, halfSize, z), GfVec3f(-halfSize, halfSize, z)};
    if (!facingPositiveZ)
        std::swap(points[1], points[3]);
    mesh.CreatePointsAttr().Set(points);
    mesh.CreateFaceVertexCountsAttr().Set(VtIntArray{4});
    mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray{0, 1, 2, 3});
}

void SetLampPrimvar(const UsdStageRefPtr& stage, const TfToken& name, const SdfValueTypeName& type, const VtValue& value)
{
    UsdGeomPrimvarsAPI(stage->GetPrimAtPath(kLamp)).CreatePrimvar(name, type, UsdGeomTokens->constant).Set(value);
}

void SetLampShaders(const UsdStageRefPtr& stage, const std::string& nodeGraph)
{
    SetLampPrimvar(stage, TfToken("arnold:light:shaders"), SdfValueTypeNames->String, VtValue(nodeGraph));
}

void SetLampIntensity(const UsdStageRefPtr& stage, float intensity)
{
    SetLampPrimvar(stage, TfToken("arnold:light:intensity"), SdfValueTypeNames->Float, VtValue(intensity));
}

} // namespace

int main(int, char**)
{
    const GfVec2f left = ScreenPosition(GfVec3d(-1.5, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.5, 0, 0));
    // The light_decay filter cuts the light of the lamp before it reaches the wall
    const Check filtered = AllOf({ExpectColor(Color::Black, left), ExpectColor(Color::Black, right)});
    const Check unfiltered = AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Grey, right)});
    const std::vector<KnownIssue> removedFilterIssue = {{ProceduralLegacyReader,
        "the legacy reader keeps the light filters once the light shaders primvar is cleared"}};

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));

        // A grey wall, only lit by the lamp
        DefineQuad(stage, kWall, 3.f, 0.f, true);
        UsdGeomMesh(stage->GetPrimAtPath(kWall)).CreateDisplayColorPrimvar(UsdGeomTokens->constant)
            .Set(VtVec3fArray{GfVec3f(0.5f, 0.5f, 0.5f)});

        // A node graph with a light_decay filter, whose far attenuation ends before the wall
        UsdPrim nodeGraph = stage->DefinePrim(kLightShaders, TfToken("ArnoldNodeGraph"));
        UsdShadeShader decay = UsdShadeShader::Define(stage, kDecay);
        decay.CreateIdAttr().Set(TfToken("arnold:light_decay"));
        decay.CreateInput(TfToken("use_far_atten"), SdfValueTypeNames->Bool).Set(true);
        decay.CreateInput(TfToken("far_start"), SdfValueTypeNames->Float).Set(0.5f);
        decay.CreateInput(TfToken("far_end"), SdfValueTypeNames->Float).Set(1.f);
        UsdShadeOutput decayOutput = decay.CreateOutput(TfToken("shader"), SdfValueTypeNames->Token);
        nodeGraph.CreateAttribute(TfToken("outputs:filters:i1"), SdfValueTypeNames->Token)
            .AddConnection(decayOutput.GetAttr().GetPath());

        // A small mesh light in front of the wall, facing it, not visible to the camera
        DefineQuad(stage, kLamp, 0.3f, 3.f, false);
        SetLampPrimvar(stage, TfToken("arnold:light"), SdfValueTypeNames->Bool, VtValue(true));
        SetLampPrimvar(stage, TfToken("arnold:light:exposure"), SdfValueTypeNames->Float, VtValue(1.f));
        // Only its light is rendered, its noisy surface would make the reference image unstable
        SetLampPrimvar(stage, TfToken("arnold:visibility:camera"), SdfValueTypeNames->Bool, VtValue(false));
        SetLampIntensity(stage, 20.f);
        SetLampShaders(stage, kLightShaders.GetString());
        return stage;
    };

    scenario.steps = {
        {"initial render, the filter cuts the light", nullptr, filtered},
        // The light shaders primvar isn't synced, the filter of its node graph must be kept
        {"mesh light intensity change", [](const UsdStageRefPtr& stage) { SetLampIntensity(stage, 25.f); }, filtered},
        {"light shaders removed", [](const UsdStageRefPtr& stage) { SetLampShaders(stage, ""); }, unfiltered,
            removedFilterIssue},
        {"mesh light intensity change without filter", [](const UsdStageRefPtr& stage) { SetLampIntensity(stage, 20.f); },
            unfiltered, removedFilterIssue},
        {"light shaders set back", [](const UsdStageRefPtr& stage) { SetLampShaders(stage, kLightShaders.GetString()); },
            filtered},
        {"mesh light intensity change back", [](const UsdStageRefPtr& stage) { SetLampIntensity(stage, 25.f); },
            filtered},
    };
    return Run(scenario);
}
