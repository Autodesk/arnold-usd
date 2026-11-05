// Interactive edits of lights of different types, lighting a grey wall: spot shaping added to a sphere light and
// removed, light visibility, a shader connected to a rect light color, a light filter, a dome light, a mesh light and
// instanced spot lights. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/meshLightAPI.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdShade/shader.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kWall("/World/wall");
const SdfPath kLight("/World/light");
const SdfPath kLightShaders("/World/light_shaders");
const SdfPath kFlat("/World/light_shaders/flat");
const SdfPath kDecay("/World/light_shaders/decay");
const SdfPath kDome("/World/dome");
const SdfPath kMeshLight("/World/mesh_light");
const SdfPath kInstancer("/World/instancer");

// A quad facing +Z, centered on the origin
void DefineQuad(const UsdStageRefPtr& stage, const SdfPath& path, float halfWidth, float halfHeight)
{
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, path);
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    mesh.CreatePointsAttr().Set(VtVec3fArray{GfVec3f(-halfWidth, -halfHeight, 0.f),
        GfVec3f(halfWidth, -halfHeight, 0.f), GfVec3f(halfWidth, halfHeight, 0.f),
        GfVec3f(-halfWidth, halfHeight, 0.f)});
    mesh.CreateFaceVertexCountsAttr().Set(VtIntArray{4});
    mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray{0, 1, 2, 3});
}

// A light 2 units in front of the wall, pointing at it
template <typename T>
T DefineFrontLight(const UsdStageRefPtr& stage, const SdfPath& path)
{
    T light = T::Define(stage, path);
    SetTranslate(stage, path, GfVec3d(0, 0, 2));
    light.CreateIntensityAttr().Set(30.f);
    light.CreateNormalizeAttr().Set(true);
    return light;
}

UsdShadeShader DefineShader(const UsdStageRefPtr& stage, const SdfPath& path, const char* id)
{
    UsdShadeShader shader = UsdShadeShader::Define(stage, path);
    shader.CreateIdAttr(VtValue(TfToken(id)));
    return shader;
}

UsdShadeOutput LightShadersOutput(const UsdStageRefPtr& stage, const char* name)
{
    return UsdShadeConnectableAPI(stage->GetPrimAtPath(kLightShaders))
        .CreateOutput(TfToken(name), SdfValueTypeNames->Token);
}

} // namespace

int main(int, char**)
{
    const std::vector<KnownIssue> lightShadersIssue = {{DelegateHydra1,
        "with hydra 1, primvars:arnold:shaders added to an existing light isn't applied"}};
    const std::vector<KnownIssue> filterRemovalIssue = {
        {DelegateHydra1, "with hydra 1, primvars:arnold:shaders added to an existing light isn't applied"},
        {ProceduralHydra2 | DelegateHydra2,
            "with hydra 2, the light filter removed from the light shaders node graph stays applied"}};
    // With hydra 1, the render delegate only applies primvars:arnold:shaders when the hydra 1 procedural rendered first
    // in the same process (hydra pass). It then fails the color change and the primvar removal instead.
    const std::vector<KnownIssue> shaderColorIssue = {
        {DelegateHydra1, "with hydra 1, primvars:arnold:shaders added to an existing light isn't applied"},
        {ProceduralHydra1, "with hydra 1, the color change of the shader connected to the light isn't applied"}};
    const std::vector<KnownIssue> shadersRemovalIssue = {{ProceduralHydra | AnyDelegate,
        "the light color stays connected after primvars:arnold:shaders is removed"}};
    const std::vector<KnownIssue> meshLightIssue = {{ProceduralHydra | AnyDelegate,
        "the mesh light API applied to a new mesh doesn't light the scene"}};

    // The wall covers the whole frame, being in the z = 0 plane
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));
    const GfVec2f left = ScreenPosition(GfVec3d(-1.5, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.5, 0, 0));
    const GfVec2f corner = ScreenPosition(GfVec3d(2.9, 2.1, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineQuad(stage, kWall, 4.f, 3.f);
        BindMaterial(stage, kWall, DefinePreviewMaterial(stage, SdfPath("/World/materials/grey"), GfVec3f(0.5f)));
        DefineFrontLight<UsdLuxSphereLight>(stage, kLight).CreateRadiusAttr().Set(0.1f);
        return stage;
    };

    auto setConeAngle = [](const UsdStageRefPtr& stage, float angle) {
        UsdLuxShapingAPI::Apply(stage->GetPrimAtPath(kLight)).CreateShapingConeAngleAttr().Set(angle);
    };
    scenario.steps = {
        {"initial render, with a sphere light", nullptr,
            AllOf({ExpectColor(Color::Grey, center), ExpectColor(Color::Grey, right), ExpectColor(Color::Grey, corner)})},
        {"spot shaping added to the sphere light, with a 20 degrees cone angle",
            [=](const UsdStageRefPtr& stage) { setConeAngle(stage, 20.f); },
            AllOf({ExpectColor(Color::Grey, center), ExpectColor(Color::Black, right)})},
        {"cone angle change to 60 degrees",
            [=](const UsdStageRefPtr& stage) { setConeAngle(stage, 60.f); },
            AllOf({ExpectColor(Color::Grey, right), ExpectColor(Color::Black, corner)})},
        {"spot shaping removed",
            [](const UsdStageRefPtr& stage) {
                UsdPrim light = stage->GetPrimAtPath(kLight);
                light.RemoveProperty(UsdLuxTokens->inputsShapingConeAngle);
                light.RemoveAPI<UsdLuxShapingAPI>();
            },
            AllOf({ExpectColor(Color::Grey, right), ExpectColor(Color::Grey, corner)})},
        {"light visibility change to invisible",
            [](const UsdStageRefPtr& stage) { UsdGeomImageable(stage->GetPrimAtPath(kLight)).MakeInvisible(); },
            AllOf({ExpectColor(Color::Black, center), ExpectColor(Color::Black, corner)})},
        {"light visibility change back to visible",
            [](const UsdStageRefPtr& stage) { UsdGeomImageable(stage->GetPrimAtPath(kLight)).MakeVisible(); },
            AllOf({ExpectColor(Color::Grey, center), ExpectColor(Color::Grey, corner)})},
        {"sphere light replaced by a rect light at the same path",
            [](const UsdStageRefPtr& stage) {
                stage->RemovePrim(kLight);
                UsdLuxRectLight light = DefineFrontLight<UsdLuxRectLight>(stage, kLight);
                light.CreateWidthAttr().Set(0.5f);
                light.CreateHeightAttr().Set(0.5f);
            },
            AllOf({ExpectColor(Color::Grey, center), ExpectColor(Color::Grey, right)})},
        {"rect light color connected to a flat shader, through primvars:arnold:shaders",
            [](const UsdStageRefPtr& stage) {
                stage->DefinePrim(kLightShaders, TfToken("ArnoldNodeGraph"));
                UsdShadeShader flat = DefineShader(stage, kFlat, "arnold:flat");
                flat.CreateInput(TfToken("color"), SdfValueTypeNames->Color3f).Set(GfVec3f(0.f, 1.f, 0.f));
                LightShadersOutput(stage, "color")
                    .ConnectToSource(flat.CreateOutput(TfToken("out"), SdfValueTypeNames->Color3f));
                stage->GetPrimAtPath(kLight)
                    .CreateAttribute(TfToken("primvars:arnold:shaders"), SdfValueTypeNames->String)
                    .Set(kLightShaders.GetString());
            },
            AllOf({ExpectColor(Color::Green, center), ExpectColor(Color::Green, right)}), lightShadersIssue},
        {"color change of the shader connected to the light",
            [](const UsdStageRefPtr& stage) {
                UsdShadeShader::Get(stage, kFlat).GetInput(TfToken("color")).Set(GfVec3f(0.f, 0.f, 1.f));
            },
            AllOf({ExpectColor(Color::Blue, center), ExpectColor(Color::Blue, right)}), shaderColorIssue},
        {"light_decay filter added, attenuating the light before it reaches the wall",
            [](const UsdStageRefPtr& stage) {
                UsdShadeShader decay = DefineShader(stage, kDecay, "arnold:light_decay");
                decay.CreateInput(TfToken("use_far_atten"), SdfValueTypeNames->Bool).Set(true);
                decay.CreateInput(TfToken("far_start"), SdfValueTypeNames->Float).Set(1.f);
                decay.CreateInput(TfToken("far_end"), SdfValueTypeNames->Float).Set(1.5f);
                LightShadersOutput(stage, "filters:i1")
                    .ConnectToSource(decay.CreateOutput(TfToken("out"), SdfValueTypeNames->Token));
            },
            AllOf({ExpectColor(Color::Black, center), ExpectColor(Color::Black, right)}), lightShadersIssue},
        {"light filter removed",
            [](const UsdStageRefPtr& stage) { LightShadersOutput(stage, "filters:i1").DisconnectSource(); },
            AllOf({ExpectColor(Color::Blue, center), ExpectColor(Color::Blue, right)}), filterRemovalIssue},
        {"primvars:arnold:shaders removed, back to the light color",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kLight).RemoveProperty(TfToken("primvars:arnold:shaders"));
            },
            AllOf({ExpectColor(Color::Grey, center), ExpectColor(Color::Grey, right)}), shadersRemovalIssue},
        {"rect light replaced by a red dome light",
            [](const UsdStageRefPtr& stage) {
                stage->RemovePrim(kLight);
                UsdLuxDomeLight dome = UsdLuxDomeLight::Define(stage, kDome);
                dome.CreateColorAttr().Set(GfVec3f(1.f, 0.f, 0.f));
                dome.CreateIntensityAttr().Set(1.f);
            },
            AllOf({ExpectColor(Color::Red, center), ExpectColor(Color::Red, corner)})},
        {"dome light intensity change to 0",
            [](const UsdStageRefPtr& stage) { UsdLuxDomeLight::Get(stage, kDome).GetIntensityAttr().Set(0.f); },
            AllOf({ExpectColor(Color::Black, center), ExpectColor(Color::Black, corner)})},
        {"green mesh light applied to a new quad in front of the wall",
            [](const UsdStageRefPtr& stage) {
                DefineQuad(stage, kMeshLight, 0.5f, 0.5f);
                SetTranslate(stage, kMeshLight, GfVec3d(0, 0, 1));
                UsdPrim prim = stage->GetPrimAtPath(kMeshLight);
                UsdLuxMeshLightAPI::Apply(prim);
                UsdLuxLightAPI light(prim);
                light.CreateIntensityAttr().Set(30.f);
                light.CreateNormalizeAttr().Set(true);
                light.CreateColorAttr().Set(GfVec3f(0.f, 1.f, 0.f));
            },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Green, right)}), meshLightIssue},
        {"mesh light intensity change to 0",
            [](const UsdStageRefPtr& stage) {
                UsdLuxLightAPI(stage->GetPrimAtPath(kMeshLight)).GetIntensityAttr().Set(0.f);
            },
            AllOf({ExpectColor(Color::Black, left), ExpectColor(Color::Black, right)})},
        {"point instancer of a spot light, with an instance on the left",
            [](const UsdStageRefPtr& stage) {
                const SdfPath spot = kInstancer.AppendPath(SdfPath("prototypes/spot"));
                UsdGeomPointInstancer instancer = UsdGeomPointInstancer::Define(stage, kInstancer);
                SetTranslate(stage, kInstancer, GfVec3d(0, 0, 2));
                UsdLuxSphereLight light = UsdLuxSphereLight::Define(stage, spot);
                light.CreateRadiusAttr().Set(0.05f);
                light.CreateIntensityAttr().Set(30.f);
                light.CreateNormalizeAttr().Set(true);
                UsdLuxShapingAPI::Apply(light.GetPrim()).CreateShapingConeAngleAttr().Set(20.f);
                instancer.CreatePrototypesRel().SetTargets({spot});
                instancer.CreatePositionsAttr().Set(VtVec3fArray{GfVec3f(-1.5f, 0.f, 0.f)});
                instancer.CreateProtoIndicesAttr().Set(VtIntArray{0});
            },
            AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Black, right)})},
        {"spot light instance moved to the right",
            [](const UsdStageRefPtr& stage) {
                UsdGeomPointInstancer(stage->GetPrimAtPath(kInstancer))
                    .GetPositionsAttr()
                    .Set(VtVec3fArray{GfVec3f(1.5f, 0.f, 0.f)});
            },
            AllOf({ExpectColor(Color::Black, left), ExpectColor(Color::Grey, right)})},
    };
    scenario.skippedModes = {{ProceduralLegacyReader,
        "this test covers the hydra code paths, the legacy reader hangs when the sphere light is replaced by a rect light"}};
    return Run(scenario);
}
