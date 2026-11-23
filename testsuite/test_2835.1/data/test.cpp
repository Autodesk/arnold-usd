// Interactive edits of arnold light groups (primvars:arnold:use_light_group / primvars:arnold:light_group) mixed with
// UsdLux light link collections, with a mesh light in the scene: lights are added to and removed from the arnold light
// groups, the primvars are removed, the collections are edited, and lights and spheres are deleted. See
// interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/lightAPI.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kLeft("/World/left");
const SdfPath kRight("/World/right");
const SdfPath kCenter("/World/center");
const SdfPath kRedLight("/World/red_light");
const SdfPath kGreenLight("/World/green_light");
const SdfPath kBlueLight("/World/blue_light");
const SdfPath kMeshLight("/World/mesh_light");
// The arnold name of the mesh light, given by primvars:arnold:light:name
const char* kMeshLightName = "/World/mesh_light_light";

// Links the light to the given prims only, with its UsdLux light link collection
void SetLinks(const UsdStageRefPtr& stage, const SdfPath& light, const SdfPathVector& prims)
{
    UsdCollectionAPI collection = UsdLuxLightAPI(stage->GetPrimAtPath(light)).GetLightLinkCollectionAPI();
    collection.CreateIncludeRootAttr().Set(false);
    collection.CreateIncludesRel().SetTargets(prims);
}

// Links the shape to the given lights only, with its arnold light group
void SetArnoldLightGroup(const UsdStageRefPtr& stage, const SdfPath& shape, const std::vector<std::string>& lights)
{
    UsdGeomPrimvarsAPI primvars(stage->GetPrimAtPath(shape));
    primvars.CreatePrimvar(TfToken("arnold:use_light_group"), SdfValueTypeNames->Bool, UsdGeomTokens->constant)
        .Set(true);
    primvars.CreatePrimvar(TfToken("arnold:light_group"), SdfValueTypeNames->StringArray, UsdGeomTokens->constant)
        .Set(VtStringArray(lights.begin(), lights.end()));
}

void DefineColoredLight(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3f& color)
{
    DefineDistantLight(stage, path);
    UsdLuxDistantLight::Get(stage, path).CreateColorAttr().Set(color);
}

void DefineSphere(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3d& position)
{
    UsdGeomSphere::Define(stage, path).CreateRadiusAttr().Set(0.8);
    SetTranslate(stage, path, position);
    BindMaterial(stage, path, UsdShadeMaterial::Get(stage, SdfPath("/World/materials/grey")));
}

// A quad out of the frame converted to a mesh light (as C4DtoA authors them), whose registration re-applies the light
// linking of the shapes (#2835). Its intensity is zero not to change the colors of the spheres.
void DefineMeshLight(const UsdStageRefPtr& stage)
{
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, kMeshLight);
    mesh.CreateFaceVertexCountsAttr().Set(VtIntArray{4});
    mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray{0, 1, 2, 3});
    mesh.CreatePointsAttr().Set(VtVec3fArray{{-1, 5, -1}, {1, 5, -1}, {1, 5, 1}, {-1, 5, 1}});
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    UsdGeomPrimvarsAPI primvars(mesh.GetPrim());
    primvars.CreatePrimvar(TfToken("arnold:light"), SdfValueTypeNames->Bool, UsdGeomTokens->constant).Set(true);
    primvars.CreatePrimvar(TfToken("arnold:light:name"), SdfValueTypeNames->String, UsdGeomTokens->constant)
        .Set(std::string(kMeshLightName));
    primvars.CreatePrimvar(TfToken("arnold:light:intensity"), SdfValueTypeNames->Float, UsdGeomTokens->constant)
        .Set(0.f);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const GfVec2f left = ScreenPosition(GfVec3d(-1.8, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.8, 0, 0));
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));
    const std::string red = kRedLight.GetString();
    const std::string green = kGreenLight.GetString();
    const std::string blue = kBlueLight.GetString();

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefinePreviewMaterial(stage, SdfPath("/World/materials/grey"), GfVec3f(0.5f, 0.5f, 0.5f));
        DefineSphere(stage, kLeft, GfVec3d(-1.8, 0, 0));
        DefineSphere(stage, kCenter, GfVec3d(0, 0, 0));
        DefineSphere(stage, kRight, GfVec3d(1.8, 0, 0));
        DefineColoredLight(stage, kRedLight, GfVec3f(1.f, 0.f, 0.f));
        DefineColoredLight(stage, kGreenLight, GfVec3f(0.f, 1.f, 0.f));
        DefineColoredLight(stage, kBlueLight, GfVec3f(0.f, 0.f, 1.f));
        DefineMeshLight(stage);
        // With UsdLux, the red light lights nothing, the green one the center sphere, and the blue one the right
        // sphere. The arnold light group of the left sphere takes precedence over them, linking it to the red light.
        SetLinks(stage, kRedLight, {});
        SetLinks(stage, kGreenLight, {kCenter});
        SetLinks(stage, kBlueLight, {kRight});
        SetArnoldLightGroup(stage, kLeft, {red, kMeshLightName});
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr,
            AllOf({ExpectColor(Color::Red, left), ExpectColor(Color::Green, center), ExpectColor(Color::Blue, right)})},
        {"the blue light added to the arnold light group of the left sphere, and the red one removed",
            [&](const UsdStageRefPtr& stage) { SetArnoldLightGroup(stage, kLeft, {blue, kMeshLightName}); },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Green, center), ExpectColor(Color::Blue, right)})},
        {"all the lights removed from the arnold light group of the left sphere",
            [&](const UsdStageRefPtr& stage) { SetArnoldLightGroup(stage, kLeft, {}); },
            AllOf({ExpectColor(Color::Black, left), ExpectColor(Color::Green, center), ExpectColor(Color::Blue, right)})},
        {"the green light and the mesh light added to the arnold light group of the left sphere",
            [&](const UsdStageRefPtr& stage) { SetArnoldLightGroup(stage, kLeft, {green, kMeshLightName}); },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Green, center), ExpectColor(Color::Blue, right)})},
        {"an arnold light group on the center sphere, taking precedence over its UsdLux links",
            [&](const UsdStageRefPtr& stage) { SetArnoldLightGroup(stage, kCenter, {red}); },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Red, center), ExpectColor(Color::Blue, right)})},
        {"the arnold light group primvars of the center sphere removed, its UsdLux links apply again",
            [](const UsdStageRefPtr& stage) {
                UsdPrim prim = stage->GetPrimAtPath(kCenter);
                prim.RemoveProperty(TfToken("primvars:arnold:use_light_group"));
                prim.RemoveProperty(TfToken("primvars:arnold:light_group"));
            },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Green, center), ExpectColor(Color::Blue, right)})},
        {"UsdLux collections edited, the red light lighting the right sphere and the blue one nothing",
            [](const UsdStageRefPtr& stage) {
                SetLinks(stage, kRedLight, {kRight});
                SetLinks(stage, kBlueLight, {});
            },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Green, center), ExpectColor(Color::Red, right)})},
        {"the green light, in the arnold light group of the left sphere, is deleted",
            [](const UsdStageRefPtr& stage) { stage->RemovePrim(kGreenLight); },
            AllOf({ExpectColor(Color::Black, left), ExpectColor(Color::Black, center), ExpectColor(Color::Red, right)})},
        // The deleted green light doesn't resolve anymore: it is skipped and the blue light is still applied, where
        // the whole group used to be ignored, keeping the previous array of the left sphere.
        {"the blue light added to the arnold light group of the left sphere, which still names the deleted green light",
            [&](const UsdStageRefPtr& stage) { SetArnoldLightGroup(stage, kLeft, {green, blue, kMeshLightName}); },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Black, center), ExpectColor(Color::Red, right)})},
        {"the blue light removed from the arnold light group of the left sphere",
            [&](const UsdStageRefPtr& stage) { SetArnoldLightGroup(stage, kLeft, {green, kMeshLightName}); },
            AllOf({ExpectColor(Color::Black, left), ExpectColor(Color::Black, center), ExpectColor(Color::Red, right)})},
        {"the green light is created again, its UsdLux collection only including the left sphere",
            [](const UsdStageRefPtr& stage) {
                DefineColoredLight(stage, kGreenLight, GfVec3f(0.f, 1.f, 0.f));
                SetLinks(stage, kGreenLight, {kLeft});
            },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Black, center), ExpectColor(Color::Red, right)})},
        {"the mesh light, in the arnold light group of the left sphere, is deleted",
            [](const UsdStageRefPtr& stage) { stage->RemovePrim(kMeshLight); },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Black, center), ExpectColor(Color::Red, right)})},
        {"the left sphere, with an arnold light group, is deleted",
            [](const UsdStageRefPtr& stage) { stage->RemovePrim(kLeft); },
            AllOf({ExpectColor(Color::Empty, left), ExpectColor(Color::Black, center), ExpectColor(Color::Red, right)})},
        {"the right sphere, in the UsdLux collection of the red light, is deleted",
            [](const UsdStageRefPtr& stage) { stage->RemovePrim(kRight); },
            AllOf({ExpectColor(Color::Empty, left), ExpectColor(Color::Black, center), ExpectColor(Color::Empty, right)})},
    };
    // The reference image is the render of the edited collections, where the three spheres are lit
    scenario.referenceStep = 6;
    // As in test_2343.8, the legacy reader hangs when a light is removed. test_2835.2 covers its arnold light groups
    // mixed with UsdLux light linking.
    scenario.skippedModes = {{ProceduralLegacyReader,
        "this test covers the hydra code paths, the legacy reader hangs when the green light is removed"}};
    return Run(scenario);
}
