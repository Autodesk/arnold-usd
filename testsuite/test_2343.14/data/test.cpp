// More interactive edits of meshes: subdivision scheme and corner sharpness, geom subsets with their own material,
// orientation and doubleSided, an arnold visibility primvar, a primvar read by the material, and authored normals
// (vertex, indexed faceVarying, and the normals attribute). The edits of primvars that might not be removed come last,
// not to change the results of the other steps.
// See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

#include <cstdio>

using namespace interactive_usd_test;

namespace {

const SdfPath kCube("/World/cube");
const SdfPath kQuads("/World/quads");
const SdfPath kSubset("/World/quads/subset");
const SdfPath kReaderMaterial("/World/materials/reader");
const TfToken kNormals("primvars:normals");
const TfToken kMyColor("primvars:myColor");
const TfToken kCameraVisibility("primvars:arnold:visibility:camera");

UsdGeomMesh Mesh(const UsdStageRefPtr& stage, const SdfPath& path) { return UsdGeomMesh(stage->GetPrimAtPath(path)); }

// A cube of size 1.6, with a red displayColor. Without primvars:arnold:subdiv_iterations (or a display style refine
// level), arnold doesn't subdivide a mesh, whatever its subdivision scheme (the render delegate sets subdiv_type to
// none when subdiv_iterations is 0, see #679 and #931).
void DefineCube(const UsdStageRefPtr& stage)
{
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, kCube);
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    VtVec3fArray points;
    for (int i = 0; i < 8; ++i)
        points.push_back(GfVec3f(i & 1 ? 0.8f : -0.8f, i & 2 ? 0.8f : -0.8f, i & 4 ? 0.8f : -0.8f));
    mesh.CreatePointsAttr().Set(points);
    mesh.CreateFaceVertexCountsAttr().Set(VtIntArray(6, 4));
    mesh.CreateFaceVertexIndicesAttr().Set(
        VtIntArray{0, 2, 3, 1, 4, 5, 7, 6, 0, 1, 5, 4, 2, 6, 7, 3, 0, 4, 6, 2, 1, 3, 7, 5});
    mesh.CreateDisplayColorPrimvar().Set(VtVec3fArray{GfVec3f(0.8f, 0.05f, 0.05f)});
    UsdGeomPrimvarsAPI(mesh.GetPrim())
        .CreatePrimvar(TfToken("arnold:subdiv_iterations"), SdfValueTypeNames->UChar, UsdGeomTokens->constant)
        .Set(static_cast<unsigned char>(2));
}

// Two quads side by side, facing the camera, from x = -1 to 1
void DefineQuads(const UsdStageRefPtr& stage)
{
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, kQuads);
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    mesh.CreatePointsAttr().Set(VtVec3fArray{GfVec3f(-1.f, -1.f, 0.f), GfVec3f(0.f, -1.f, 0.f),
        GfVec3f(1.f, -1.f, 0.f), GfVec3f(-1.f, 1.f, 0.f), GfVec3f(0.f, 1.f, 0.f), GfVec3f(1.f, 1.f, 0.f)});
    mesh.CreateFaceVertexCountsAttr().Set(VtIntArray{4, 4});
    mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray{0, 1, 4, 3, 1, 2, 5, 4});
}

// Opaque pixels, whatever their color: for the visibility and sidedness checks, that don't depend on the shading
Check ExpectOpaque(const GfVec2f& position)
{
    return [position](const Image& image) -> std::string {
        const float alpha = image.Average(position[0], position[1])[3];
        if (alpha > 0.9f)
            return std::string();
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "expected an opaque pixel at (%.2f, %.2f), got an alpha of %.4f", position[0],
            position[1], alpha);
        return buffer;
    };
}

void SetNormals(const UsdStageRefPtr& stage, const GfVec3f& normal)
{
    UsdGeomPrimvarsAPI(stage->GetPrimAtPath(kQuads))
        .CreatePrimvar(TfToken("normals"), SdfValueTypeNames->Normal3fArray, UsdGeomTokens->vertex)
        .Set(VtVec3fArray(6, normal));
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    // Reproduced with the arnold API alone on Arnold 7.5.5.0: in an interactive session, creases set on a subdivided
    // polymesh after its first render aren't applied (ARNOLD-18970). It wasn't seen on Arnold 7.5.2.0.
    const std::vector<KnownIssue> creasesIssue = {{ProceduralHydra1 | AnyDelegate,
        "arnold doesn't apply the creases set on a subdivided polymesh that was already rendered"}};
    const std::vector<KnownIssue> subsetRemovalIssue = {{DelegateHydra2,
        "with hydra 2, the faces keep the material of a removed geom subset, Arnold reporting missing shidxs"}};
    const std::vector<KnownIssue> primvarRemovalIssue = {{ProceduralHydra1 | AnyDelegate,
        "the material keeps reading the value of a removed primvar"}};

    // The cube is on the left. With the distant light of DefineDistantLight, a normal pointing to +X is lit, and one
    // pointing to +Y isn't.
    const GfVec2f cubeCenter = ScreenPosition(GfVec3d(-1.5, 0, 0.8));
    const GfVec2f cubeCorner = ScreenPosition(GfVec3d(-1.5 + 0.7, 0.7, 0.8));
    const GfVec2f quadLeft = ScreenPosition(GfVec3d(1, 0, 0));
    const GfVec2f quadRight = ScreenPosition(GfVec3d(2, 0, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        DefineCube(stage);
        SetTranslate(stage, kCube, GfVec3d(-1.5, 0, 0));
        DefineQuads(stage);
        SetTranslate(stage, kQuads, GfVec3d(1.5, 0, 0));
        BindMaterial(stage, kQuads, DefinePreviewMaterial(stage, SdfPath("/World/materials/red"),
            GfVec3f(0.8f, 0.05f, 0.05f)));
        DefinePreviewMaterial(stage, SdfPath("/World/materials/blue"), GfVec3f(0.05f, 0.05f, 0.8f));
        return stage;
    };

    auto bothQuads = [=](Color color) { return AllOf({ExpectColor(color, quadLeft), ExpectColor(color, quadRight)}); };
    const Check bothQuadsOpaque = AllOf({ExpectOpaque(quadLeft), ExpectOpaque(quadRight)});
    scenario.steps = {
        {"initial render", nullptr,
            AllOf({ExpectColor(Color::Red, cubeCenter), ExpectColor(Color::Red, cubeCorner), bothQuads(Color::Red)})},
        {"subdivision scheme change to catmullClark, rounding the cube",
            [](const UsdStageRefPtr& stage) {
                Mesh(stage, kCube).GetSubdivisionSchemeAttr().Set(UsdGeomTokens->catmullClark);
            },
            AllOf({ExpectColor(Color::Red, cubeCenter), ExpectColor(Color::Empty, cubeCorner)})},
        {"sharp corners, the cube corners getting back to their positions",
            [](const UsdStageRefPtr& stage) {
                Mesh(stage, kCube).CreateCornerIndicesAttr().Set(VtIntArray{0, 1, 2, 3, 4, 5, 6, 7});
                Mesh(stage, kCube).CreateCornerSharpnessesAttr().Set(VtFloatArray(8, 10.f));
            },
            AllOf({ExpectColor(Color::Red, cubeCenter), ExpectColor(Color::Red, cubeCorner)}), creasesIssue},
        {"subdivision scheme change back to none, corners removed",
            [](const UsdStageRefPtr& stage) {
                Mesh(stage, kCube).GetSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
                stage->GetPrimAtPath(kCube).RemoveProperty(UsdGeomTokens->cornerIndices);
                stage->GetPrimAtPath(kCube).RemoveProperty(UsdGeomTokens->cornerSharpnesses);
            },
            AllOf({ExpectColor(Color::Red, cubeCenter), ExpectColor(Color::Red, cubeCorner)})},
        {"geom subset of the right face, bound to a blue material",
            [](const UsdStageRefPtr& stage) {
                UsdGeomSubset subset = UsdShadeMaterialBindingAPI(stage->GetPrimAtPath(kQuads))
                                           .CreateMaterialBindSubset(TfToken(kSubset.GetName()), VtIntArray{1});
                BindMaterial(stage, subset.GetPath(), UsdShadeMaterial::Get(stage, SdfPath("/World/materials/blue")));
            },
            AllOf({ExpectColor(Color::Red, quadLeft), ExpectColor(Color::Blue, quadRight)})},
        {"geom subset indices change, to the left face",
            [](const UsdStageRefPtr& stage) {
                UsdGeomSubset(stage->GetPrimAtPath(kSubset)).GetIndicesAttr().Set(VtIntArray{0});
            },
            AllOf({ExpectColor(Color::Blue, quadLeft), ExpectColor(Color::Red, quadRight)})},
        {"geom subset removed", [](const UsdStageRefPtr& stage) { stage->RemovePrim(kSubset); },
            bothQuads(Color::Red), subsetRemovalIssue},
        {"orientation change to leftHanded, the single-sided quads facing away from the camera",
            [](const UsdStageRefPtr& stage) {
                Mesh(stage, kQuads).CreateOrientationAttr().Set(UsdGeomTokens->leftHanded);
            },
            bothQuads(Color::Empty)},
        {"doubleSided change to true",
            [](const UsdStageRefPtr& stage) { Mesh(stage, kQuads).CreateDoubleSidedAttr().Set(true); },
            bothQuadsOpaque},
        {"primvars:arnold:visibility:camera set to false",
            [](const UsdStageRefPtr& stage) {
                UsdGeomPrimvarsAPI(stage->GetPrimAtPath(kQuads))
                    .CreatePrimvar(TfToken("arnold:visibility:camera"), SdfValueTypeNames->Bool,
                        UsdGeomTokens->constant)
                    .Set(false);
            },
            bothQuads(Color::Empty)},
        {"primvars:arnold:visibility:camera set to true",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kQuads).GetAttribute(kCameraVisibility).Set(true);
            },
            bothQuadsOpaque},
        {"binding to a material reading a myColor primvar that doesn't exist, with a red fallback",
            [](const UsdStageRefPtr& stage) {
                UsdShadeMaterial material = DefinePreviewMaterial(stage, kReaderMaterial, GfVec3f(0.5f));
                UsdShadeShader reader = UsdShadeShader::Define(stage, kReaderMaterial.AppendChild(TfToken("reader")));
                reader.CreateIdAttr(VtValue(TfToken("UsdPrimvarReader_float3")));
                reader.CreateInput(TfToken("varname"), SdfValueTypeNames->String).Set(std::string("myColor"));
                reader.CreateInput(TfToken("fallback"), SdfValueTypeNames->Float3).Set(GfVec3f(0.8f, 0.05f, 0.05f));
                UsdShadeShader::Get(stage, kReaderMaterial.AppendChild(TfToken("surface")))
                    .GetInput(TfToken("diffuseColor"))
                    .ConnectToSource(reader.CreateOutput(TfToken("result"), SdfValueTypeNames->Float3));
                BindMaterial(stage, kQuads, material);
            },
            bothQuads(Color::Red)},
        {"myColor primvar added",
            [](const UsdStageRefPtr& stage) {
                UsdGeomPrimvarsAPI(stage->GetPrimAtPath(kQuads))
                    .CreatePrimvar(TfToken("myColor"), SdfValueTypeNames->Color3f, UsdGeomTokens->constant)
                    .Set(GfVec3f(0.05f, 0.8f, 0.05f));
            },
            bothQuads(Color::Green)},
        {"myColor primvar change",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kQuads).GetAttribute(kMyColor).Set(GfVec3f(0.05f, 0.05f, 0.8f));
            },
            bothQuads(Color::Blue)},
        {"myColor primvar removed, back to the fallback",
            [](const UsdStageRefPtr& stage) { stage->GetPrimAtPath(kQuads).RemoveProperty(kMyColor); },
            bothQuads(Color::Red), primvarRemovalIssue},
        {"authored normals pointing to +X, lit, with the red material bound again and the orientation back to "
         "rightHanded",
            [](const UsdStageRefPtr& stage) {
                Mesh(stage, kQuads).GetOrientationAttr().Set(UsdGeomTokens->rightHanded);
                SetNormals(stage, GfVec3f(1, 0, 0));
                BindMaterial(stage, kQuads, UsdShadeMaterial::Get(stage, SdfPath("/World/materials/red")));
            },
            bothQuads(Color::Red)},
        {"authored normals change, pointing to +Y, unlit",
            [](const UsdStageRefPtr& stage) { SetNormals(stage, GfVec3f(0, 1, 0)); }, bothQuads(Color::Black)},
        {"authored normals removed",
            [](const UsdStageRefPtr& stage) { stage->GetPrimAtPath(kQuads).RemoveProperty(kNormals); },
            bothQuads(Color::Red)},
        {"indexed faceVarying normals, all indices pointing to +Y, unlit",
            [](const UsdStageRefPtr& stage) {
                UsdGeomPrimvar normals = UsdGeomPrimvarsAPI(stage->GetPrimAtPath(kQuads))
                    .CreatePrimvar(TfToken("normals"), SdfValueTypeNames->Normal3fArray, UsdGeomTokens->faceVarying);
                normals.Set(VtVec3fArray{GfVec3f(1, 0, 0), GfVec3f(0, 1, 0)});
                normals.SetIndices(VtIntArray(8, 1));
            },
            bothQuads(Color::Black)},
        {"indexed faceVarying normals removed",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kQuads).RemoveProperty(kNormals);
                stage->GetPrimAtPath(kQuads).RemoveProperty(TfToken("primvars:normals:indices"));
            },
            bothQuads(Color::Red)},
        {"normals attribute pointing to +Y, unlit",
            [](const UsdStageRefPtr& stage) {
                Mesh(stage, kQuads).CreateNormalsAttr().Set(VtVec3fArray(6, GfVec3f(0, 1, 0)));
            },
            bothQuads(Color::Black)},
        {"primvars:normals pointing to +X added, overriding the normals attribute",
            [](const UsdStageRefPtr& stage) { SetNormals(stage, GfVec3f(1, 0, 0)); }, bothQuads(Color::Red)},
        {"primvars:normals removed, falling back to the normals attribute",
            [](const UsdStageRefPtr& stage) { stage->GetPrimAtPath(kQuads).RemoveProperty(kNormals); },
            bothQuads(Color::Black)},
        {"normals attribute removed",
            [](const UsdStageRefPtr& stage) { stage->GetPrimAtPath(kQuads).RemoveProperty(UsdGeomTokens->normals); },
            bothQuads(Color::Red)},
    };
    // the following steps have known issues that change the render, the reference image is the render
    // with the geom subset
    scenario.referenceStep = 4;
    // Otherwise arnold renders the quads double-sided, whatever their doubleSided and orientation
    scenario.usdOverrideDoubleSided = false;
    scenario.skippedModes = {{ProceduralLegacyReader, "this test covers the hydra code paths"},
        {ProceduralHydra2, "the hydra 2 procedural renders the geom subsets black, and AiRender fails once the subset is "
                           "removed (missing shidxs)"}};
    return Run(scenario);
}
