// Interactive edits of meshes deduplicated by the render delegate, with HDARNOLD_GEOM_DEDUP=2 (all identical meshes
// share their arnold geometry): one of two identical meshes has its points, displayColor, topology and visibility
// edited, becoming different from the other one and identical again, and one of them is removed and added back.
// See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/base/tf/setenv.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kLeft("/World/left");
const SdfPath kRight("/World/right");
const VtVec3fArray kRed = {GfVec3f(0.8f, 0.05f, 0.05f)};
const VtVec3fArray kGreen = {GfVec3f(0.05f, 0.8f, 0.05f)};

// Square quads facing the camera, centered on the prim origin, between the given x coordinates
void SetQuads(const UsdStageRefPtr& stage, const SdfPath& path, const std::vector<std::pair<float, float>>& quads,
    float halfHeight = 1.f)
{
    VtVec3fArray points;
    VtIntArray counts;
    VtIntArray indices;
    for (const auto& quad : quads) {
        const int first = static_cast<int>(points.size());
        points.push_back(GfVec3f(quad.first, -halfHeight, 0.f));
        points.push_back(GfVec3f(quad.second, -halfHeight, 0.f));
        points.push_back(GfVec3f(quad.second, halfHeight, 0.f));
        points.push_back(GfVec3f(quad.first, halfHeight, 0.f));
        counts.push_back(4);
        for (int i = 0; i < 4; ++i)
            indices.push_back(first + i);
    }
    UsdGeomMesh mesh(stage->GetPrimAtPath(path));
    mesh.GetPointsAttr().Set(points);
    mesh.GetFaceVertexCountsAttr().Set(counts);
    mesh.GetFaceVertexIndicesAttr().Set(indices);
}

void DefineQuad(const UsdStageRefPtr& stage, const SdfPath& path, double x)
{
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, path);
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    mesh.CreateDisplayColorPrimvar(UsdGeomTokens->constant).Set(kRed);
    SetQuads(stage, path, {{-1.f, 1.f}});
    SetTranslate(stage, path, GfVec3d(x, 0, 0));
}

void SetDisplayColor(const UsdStageRefPtr& stage, const SdfPath& path, const VtVec3fArray& color)
{
    UsdGeomMesh(stage->GetPrimAtPath(path)).GetDisplayColorPrimvar().Set(color);
}

} // namespace

int main(int, char**)
{
    // Read when the render delegate is created
    TfSetenv("HDARNOLD_GEOM_DEDUP", "2");

    const GfVec2f leftCenter = ScreenPosition(GfVec3d(-1.5, 0, 0));
    const GfVec2f leftEdge = ScreenPosition(GfVec3d(-2.3, 0.7, 0));
    const GfVec2f rightCenter = ScreenPosition(GfVec3d(1.5, 0, 0));
    const GfVec2f rightEdge = ScreenPosition(GfVec3d(2.3, 0.7, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        // Two identical meshes, only their transforms are different
        DefineQuad(stage, kLeft, -1.5);
        DefineQuad(stage, kRight, 1.5);
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr, AllOf({ExpectColor(Color::Red, leftEdge), ExpectColor(Color::Red, rightEdge)})},
        {"points change of the right mesh, making it smaller",
            [](const UsdStageRefPtr& stage) { SetQuads(stage, kRight, {{-0.3f, 0.3f}}, 0.3f); },
            AllOf({ExpectColor(Color::Red, leftEdge), ExpectColor(Color::Red, rightCenter),
                ExpectColor(Color::Empty, rightEdge)})},
        {"points change of the right mesh, identical to the left one again",
            [](const UsdStageRefPtr& stage) { SetQuads(stage, kRight, {{-1.f, 1.f}}); },
            AllOf({ExpectColor(Color::Red, leftEdge), ExpectColor(Color::Red, rightEdge)})},
        {"displayColor change of the left mesh",
            [](const UsdStageRefPtr& stage) { SetDisplayColor(stage, kLeft, kGreen); },
            AllOf({ExpectColor(Color::Green, leftEdge), ExpectColor(Color::Red, rightEdge)})},
        {"displayColor change of the left mesh, identical to the right one again",
            [](const UsdStageRefPtr& stage) { SetDisplayColor(stage, kLeft, kRed); },
            AllOf({ExpectColor(Color::Red, leftEdge), ExpectColor(Color::Red, rightEdge)})},
        {"left mesh removal", [](const UsdStageRefPtr& stage) { stage->RemovePrim(kLeft); },
            AllOf({ExpectColor(Color::Empty, leftEdge), ExpectColor(Color::Red, rightEdge)})},
        {"left mesh added back", [](const UsdStageRefPtr& stage) { DefineQuad(stage, kLeft, -1.5); },
            AllOf({ExpectColor(Color::Red, leftEdge), ExpectColor(Color::Red, rightEdge)})},
        {"topology change of the right mesh, splitting it in two",
            [](const UsdStageRefPtr& stage) { SetQuads(stage, kRight, {{-1.f, -0.3f}, {0.3f, 1.f}}); },
            AllOf({ExpectColor(Color::Red, leftCenter), ExpectColor(Color::Empty, rightCenter),
                ExpectColor(Color::Red, rightEdge)})},
        {"topology change of the right mesh, identical to the left one again",
            [](const UsdStageRefPtr& stage) { SetQuads(stage, kRight, {{-1.f, 1.f}}); },
            AllOf({ExpectColor(Color::Red, leftCenter), ExpectColor(Color::Red, rightCenter)})},
        {"visibility change of the left mesh to invisible",
            [](const UsdStageRefPtr& stage) { UsdGeomImageable(stage->GetPrimAtPath(kLeft)).MakeInvisible(); },
            AllOf({ExpectColor(Color::Empty, leftCenter), ExpectColor(Color::Red, rightCenter)})},
        {"points change of the right mesh while the left one is invisible",
            [](const UsdStageRefPtr& stage) { SetQuads(stage, kRight, {{-0.3f, 0.3f}}, 0.3f); },
            AllOf({ExpectColor(Color::Empty, leftCenter), ExpectColor(Color::Red, rightCenter),
                ExpectColor(Color::Empty, rightEdge)})},
        {"visibility change of the left mesh back to visible",
            [](const UsdStageRefPtr& stage) { UsdGeomImageable(stage->GetPrimAtPath(kLeft)).MakeVisible(); },
            AllOf({ExpectColor(Color::Red, leftEdge), ExpectColor(Color::Empty, rightEdge)})},
    };
    scenario.skippedModes = {{ProceduralLegacyReader, "the legacy reader doesn't deduplicate geometry"}};
    return Run(scenario);
}
