// Interactive edits of point instancers with mesh prototypes and of a mesh identical to them, deduplicated by the
// render delegate with HDARNOLD_GEOM_DEDUP=2 (all identical meshes share their arnold geometry). Covers the fix of
// the dedup incremental updates (#2797): an rprim becoming an instanced duplicate while still holding its own geometry,
// and a canonical freed while the instancers of its group referenced it. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/base/tf/setenv.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kLeft("/World/left");
const SdfPath kRight("/World/right");
const SdfPath kSingle("/World/single");
const VtVec3fArray kRed = {GfVec3f(0.8f, 0.05f, 0.05f)};
const VtVec3fArray kGreen = {GfVec3f(0.05f, 0.8f, 0.05f)};

SdfPath Prototype(const SdfPath& instancer) { return instancer.AppendPath(SdfPath("prototypes/quad")); }

// A square quad facing the camera, of the given half size, centered on (x, 0) in the prim space
void SetQuad(const UsdStageRefPtr& stage, const SdfPath& path, float x, float halfSize = 0.4f)
{
    UsdGeomMesh mesh(stage->GetPrimAtPath(path));
    mesh.GetPointsAttr().Set(VtVec3fArray{GfVec3f(x - halfSize, -halfSize, 0.f), GfVec3f(x + halfSize, -halfSize, 0.f),
        GfVec3f(x + halfSize, halfSize, 0.f), GfVec3f(x - halfSize, halfSize, 0.f)});
}

void DefineQuad(const UsdStageRefPtr& stage, const SdfPath& path)
{
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, path);
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    mesh.CreateDisplayColorPrimvar(UsdGeomTokens->constant).Set(kRed);
    mesh.CreateFaceVertexCountsAttr().Set(VtIntArray{4});
    mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray{0, 1, 2, 3});
    mesh.CreatePointsAttr();
    SetQuad(stage, path, 0.f);
}

// A point instancer of a red quad, with an instance above and below its origin
void DefineInstancer(const UsdStageRefPtr& stage, const SdfPath& path, double x)
{
    UsdGeomPointInstancer instancer = UsdGeomPointInstancer::Define(stage, path);
    DefineQuad(stage, Prototype(path));
    instancer.CreatePrototypesRel().SetTargets({Prototype(path)});
    instancer.CreatePositionsAttr().Set(VtVec3fArray{GfVec3f(0.f, 1.f, 0.f), GfVec3f(0.f, -1.f, 0.f)});
    instancer.CreateProtoIndicesAttr().Set(VtIntArray{0, 0});
    SetTranslate(stage, path, GfVec3d(x, 0, 0));
}

void SetDisplayColor(const UsdStageRefPtr& stage, const SdfPath& path, const VtVec3fArray& color)
{
    UsdGeomMesh(stage->GetPrimAtPath(path)).GetDisplayColorPrimvar().Set(color);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    // Read when the render delegate is created
    TfSetenv("HDARNOLD_GEOM_DEDUP", "2");

    // Centers of the instances and of the single quad, and points inside the 0.8 quads but outside 0.3 ones
    const GfVec2f leftTop = ScreenPosition(GfVec3d(-1.5, 1, 0));
    const GfVec2f leftBottom = ScreenPosition(GfVec3d(-1.5, -1, 0));
    const GfVec2f leftMovedBottom = ScreenPosition(GfVec3d(-0.9, -1, 0));
    const GfVec2f rightTop = ScreenPosition(GfVec3d(1.5, 1, 0));
    const GfVec2f rightBottom = ScreenPosition(GfVec3d(1.5, -1, 0));
    const GfVec2f singleCenter = ScreenPosition(GfVec3d(0, 0, 0));
    const GfVec2f singleEdge = ScreenPosition(GfVec3d(0.3, 0.3, 0));

    const Check allRed = AllOf({ExpectColor(Color::Red, leftTop), ExpectColor(Color::Red, leftBottom),
        ExpectColor(Color::Red, rightTop), ExpectColor(Color::Red, rightBottom), ExpectColor(Color::Red, singleEdge)});

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        // Two identical prototypes and a mesh identical to them, sharing a single arnold polymesh
        DefineInstancer(stage, kLeft, -1.5);
        DefineInstancer(stage, kRight, 1.5);
        DefineQuad(stage, kSingle);
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr, allRed},
        {"points change of the single mesh, making it smaller and leaving the dedup group",
            [](const UsdStageRefPtr& stage) { SetQuad(stage, kSingle, 0.f, 0.15f); },
            AllOf({ExpectColor(Color::Red, leftTop), ExpectColor(Color::Red, rightTop),
                ExpectColor(Color::Red, singleCenter), ExpectColor(Color::Empty, singleEdge)})},
        {"points change of the single mesh, identical to the prototypes again while holding its own geometry",
            [](const UsdStageRefPtr& stage) { SetQuad(stage, kSingle, 0.f); }, allRed},
        {"displayColor change of the left prototype",
            [](const UsdStageRefPtr& stage) { SetDisplayColor(stage, Prototype(kLeft), kGreen); },
            AllOf({ExpectColor(Color::Green, leftTop), ExpectColor(Color::Green, leftBottom),
                ExpectColor(Color::Red, rightTop), ExpectColor(Color::Red, singleEdge)})},
        {"displayColor change of the left prototype, identical to the others again",
            [](const UsdStageRefPtr& stage) { SetDisplayColor(stage, Prototype(kLeft), kRed); }, allRed},
        {"points change of the left prototype, moving its quad to the right",
            [](const UsdStageRefPtr& stage) { SetQuad(stage, Prototype(kLeft), 0.6f); },
            AllOf({ExpectColor(Color::Empty, leftTop), ExpectColor(Color::Red, leftMovedBottom),
                ExpectColor(Color::Red, rightTop), ExpectColor(Color::Red, singleEdge)})},
        {"right point instancer removal", [](const UsdStageRefPtr& stage) { stage->RemovePrim(kRight); },
            AllOf({ExpectColor(Color::Red, leftMovedBottom), ExpectColor(Color::Empty, rightTop),
                ExpectColor(Color::Empty, rightBottom), ExpectColor(Color::Red, singleEdge)})},
        {"points change of the left prototype, identical to the single mesh again",
            [](const UsdStageRefPtr& stage) { SetQuad(stage, Prototype(kLeft), 0.f); },
            AllOf({ExpectColor(Color::Red, leftTop), ExpectColor(Color::Red, leftBottom),
                ExpectColor(Color::Empty, rightTop), ExpectColor(Color::Red, singleEdge)})},
        {"single mesh removal", [](const UsdStageRefPtr& stage) { stage->RemovePrim(kSingle); },
            AllOf({ExpectColor(Color::Red, leftTop), ExpectColor(Color::Red, leftBottom),
                ExpectColor(Color::Empty, singleCenter)})},
    };
    scenario.skippedModes = {{ProceduralLegacyReader, "the legacy reader doesn't deduplicate geometry"}};
    return Run(scenario);
}
