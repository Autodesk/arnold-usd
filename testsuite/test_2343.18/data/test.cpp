// Interactive edits of the points of meshes with many points: points, points and topology together, removal and
// definition again. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kLeftGrid("/World/left");
const SdfPath kRightGrid("/World/right");

// Number of quads along each side of a grid: (8 + 1)^2 = 81 points
constexpr int kGridQuads = 8;

// Grids of kGridQuads x kGridQuads quads facing the camera, one per x range, from y = ymin to ymax
void SetGrids(const UsdStageRefPtr& stage, const SdfPath& path, const std::vector<std::pair<float, float>>& xRanges,
    float ymin = -1.f, float ymax = 1.f)
{
    VtVec3fArray points;
    VtIntArray counts;
    VtIntArray indices;
    for (const auto& range : xRanges) {
        const int first = static_cast<int>(points.size());
        for (int j = 0; j <= kGridQuads; ++j) {
            for (int i = 0; i <= kGridQuads; ++i) {
                const float u = static_cast<float>(i) / kGridQuads;
                const float v = static_cast<float>(j) / kGridQuads;
                points.push_back(GfVec3f(range.first + u * (range.second - range.first), ymin + v * (ymax - ymin), 0.f));
            }
        }
        for (int j = 0; j < kGridQuads; ++j) {
            for (int i = 0; i < kGridQuads; ++i) {
                const int corner = first + j * (kGridQuads + 1) + i;
                counts.push_back(4);
                indices.push_back(corner);
                indices.push_back(corner + 1);
                indices.push_back(corner + kGridQuads + 2);
                indices.push_back(corner + kGridQuads + 1);
            }
        }
    }
    UsdGeomMesh mesh(stage->GetPrimAtPath(path));
    mesh.GetPointsAttr().Set(points);
    mesh.GetFaceVertexCountsAttr().Set(counts);
    mesh.GetFaceVertexIndicesAttr().Set(indices);
}

// Only the points change, the topology is kept
void MoveGrid(const UsdStageRefPtr& stage, const SdfPath& path, float xmin, float xmax, float ymin, float ymax)
{
    VtVec3fArray points;
    for (int j = 0; j <= kGridQuads; ++j) {
        for (int i = 0; i <= kGridQuads; ++i) {
            const float u = static_cast<float>(i) / kGridQuads;
            const float v = static_cast<float>(j) / kGridQuads;
            points.push_back(GfVec3f(xmin + u * (xmax - xmin), ymin + v * (ymax - ymin), 0.f));
        }
    }
    UsdGeomMesh(stage->GetPrimAtPath(path)).GetPointsAttr().Set(points);
}

void DefineGrid(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3f& color, float xmin, float xmax)
{
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, path);
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    mesh.CreateDisplayColorPrimvar(UsdGeomTokens->constant).Set(VtVec3fArray{color});
    SetGrids(stage, path, {{xmin, xmax}});
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const GfVec3f red(0.8f, 0.05f, 0.05f);
    const GfVec3f blue(0.05f, 0.05f, 0.8f);
    // The left grid spans x in [-2.5, -0.5] and the right one [0.5, 2.5] at first, from y = -1 to 1
    const GfVec2f farLeft = ScreenPosition(GfVec3d(-2, 0, 0));
    const GfVec2f nearLeft = ScreenPosition(GfVec3d(-1, 0, 0));
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));
    const GfVec2f nearRight = ScreenPosition(GfVec3d(1, 0, 0));
    const GfVec2f farRight = ScreenPosition(GfVec3d(2, 0, 0));
    const GfVec2f leftTop = ScreenPosition(GfVec3d(-1.5, 1.5, 0));
    const GfVec2f leftBottom = ScreenPosition(GfVec3d(-1.5, -0.5, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        DefineGrid(stage, kLeftGrid, red, -2.5f, -0.5f);
        DefineGrid(stage, kRightGrid, blue, 0.5f, 2.5f);
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr,
            AllOf({ExpectColor(Color::Red, farLeft), ExpectColor(Color::Empty, center), ExpectColor(Color::Blue, farRight)})},
        // Each points edit replaces the vlist, a shared array pointing at the new USD buffer
        {"left grid points moved up",
            [](const UsdStageRefPtr& stage) { MoveGrid(stage, kLeftGrid, -2.5f, -0.5f, 0.f, 2.f); },
            AllOf({ExpectColor(Color::Red, leftTop), ExpectColor(Color::Empty, leftBottom),
                ExpectColor(Color::Blue, farRight)})},
        {"left grid points moved back down, and the right grid points moved to the center",
            [](const UsdStageRefPtr& stage) {
                MoveGrid(stage, kLeftGrid, -2.5f, -0.5f, -1.f, 1.f);
                MoveGrid(stage, kRightGrid, -0.25f, 0.25f, -1.f, 1.f);
            },
            AllOf({ExpectColor(Color::Red, leftBottom), ExpectColor(Color::Empty, leftTop),
                ExpectColor(Color::Blue, center), ExpectColor(Color::Empty, farRight)})},
        {"right grid points moved back to the right",
            [](const UsdStageRefPtr& stage) { MoveGrid(stage, kRightGrid, 0.5f, 2.5f, -1.f, 1.f); },
            AllOf({ExpectColor(Color::Empty, center), ExpectColor(Color::Blue, farRight)})},
        {"left grid points and topology changed, splitting it in two grids",
            [](const UsdStageRefPtr& stage) { SetGrids(stage, kLeftGrid, {{-2.5f, -1.75f}, {-1.25f, -0.5f}}); },
            AllOf({ExpectColor(Color::Red, ScreenPosition(GfVec3d(-2.1, 0, 0))),
                ExpectColor(Color::Empty, ScreenPosition(GfVec3d(-1.5, 0, 0))),
                ExpectColor(Color::Red, ScreenPosition(GfVec3d(-0.9, 0, 0))), ExpectColor(Color::Blue, farRight)})},
        {"left grid removed", [](const UsdStageRefPtr& stage) { stage->RemovePrim(kLeftGrid); },
            AllOf({ExpectColor(Color::Empty, farLeft), ExpectColor(Color::Empty, nearLeft),
                ExpectColor(Color::Blue, farRight)}),
            {{ProceduralLegacyReader, "the legacy reader doesn't delete removed prims"}}},
        {"left grid defined again, in the center",
            [&](const UsdStageRefPtr& stage) { DefineGrid(stage, kLeftGrid, red, -0.25f, 0.25f); },
            AllOf({ExpectColor(Color::Empty, farLeft), ExpectColor(Color::Red, center), ExpectColor(Color::Blue, farRight)})},
        {"right grid points moved to the left",
            [](const UsdStageRefPtr& stage) { MoveGrid(stage, kRightGrid, -2.5f, -0.5f, -1.f, 1.f); },
            AllOf({ExpectColor(Color::Blue, farLeft), ExpectColor(Color::Red, center), ExpectColor(Color::Empty, nearRight),
                ExpectColor(Color::Empty, farRight)})},
    };
    return Run(scenario);
}
