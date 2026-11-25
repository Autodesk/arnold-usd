// Interactive edits of the points of meshes with many points, in the cases where Arnold rewrites the points or where
// several shapes use one USD points buffer: two meshes sharing their points, a subdivided mesh, the prototype of a
// point instancer, and a large mesh. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

// The quadrants of the image: the shared pair at the top, the subdivided grid bottom left and the instancer bottom right
const SdfPath kSharedA("/World/sharedA");
const SdfPath kSharedB("/World/sharedB");
const SdfPath kSubdivided("/World/subdivided");
const SdfPath kInstancer("/World/instancer");
const SdfPath kPrototype("/World/instancer/prototypes/grid");

const GfVec3d kSharedAOffset(-1.5, 1.1, 0);
const GfVec3d kSharedBOffset(1.5, 1.1, 0);
const GfVec3d kSubdividedOffset(-1.5, -1.1, 0);
const GfVec3d kInstancerOffset(1.5, -1.1, 0);

// Points of a grid of quads x quads quads facing the camera, from (xmin, ymin) to (xmax, ymax)
VtVec3fArray GridPoints(int quads, float xmin, float xmax, float ymin, float ymax)
{
    VtVec3fArray points;
    points.reserve((quads + 1) * (quads + 1));
    for (int j = 0; j <= quads; ++j) {
        for (int i = 0; i <= quads; ++i) {
            const float u = static_cast<float>(i) / quads;
            const float v = static_cast<float>(j) / quads;
            points.push_back(GfVec3f(xmin + u * (xmax - xmin), ymin + v * (ymax - ymin), 0.f));
        }
    }
    return points;
}

UsdGeomMesh DefineGrid(const UsdStageRefPtr& stage, const SdfPath& path, int quads, const VtVec3fArray& points,
    const GfVec3d& offset)
{
    VtIntArray counts(quads * quads, 4);
    VtIntArray indices;
    indices.reserve(quads * quads * 4);
    for (int j = 0; j < quads; ++j) {
        for (int i = 0; i < quads; ++i) {
            const int corner = j * (quads + 1) + i;
            indices.push_back(corner);
            indices.push_back(corner + 1);
            indices.push_back(corner + quads + 2);
            indices.push_back(corner + quads + 1);
        }
    }
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, path);
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    mesh.CreatePointsAttr().Set(points);
    mesh.CreateFaceVertexCountsAttr().Set(counts);
    mesh.CreateFaceVertexIndicesAttr().Set(indices);
    if (offset != GfVec3d(0))
        SetTranslate(stage, path, offset);
    return mesh;
}

void SetColor(const UsdGeomMesh& mesh, const GfVec3f& color)
{
    mesh.CreateDisplayColorPrimvar(UsdGeomTokens->constant).Set(VtVec3fArray{color});
}

void SetPoints(const UsdStageRefPtr& stage, const SdfPath& path, const VtVec3fArray& points)
{
    UsdGeomMesh(stage->GetPrimAtPath(path)).GetPointsAttr().Set(points);
}

// Image position of a point given relative to an offset
GfVec2f At(const GfVec3d& offset, double x, double y) { return ScreenPosition(offset + GfVec3d(x, y, 0)); }

constexpr int kSharedQuads = 8;      // 81 points
constexpr int kSubdividedQuads = 6;  // 49 points, subdivided twice
constexpr int kPrototypeQuads = 8;   // 81 points
constexpr int kLargeQuads = 255;     // 65536 points

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const GfVec3f red(0.8f, 0.05f, 0.05f);
    const GfVec3f green(0.05f, 0.8f, 0.05f);
    const GfVec3f blue(0.05f, 0.05f, 0.8f);
    // Both meshes are given the same VtArray, so they use one USD buffer, and the render delegate makes the second a
    // duplicate of the first (HDARNOLD_GEOM_DEDUP)
    const VtVec3fArray square = GridPoints(kSharedQuads, -0.9f, 0.9f, -0.9f, 0.9f);
    const VtVec3fArray leftHalf = GridPoints(kSharedQuads, -0.9f, 0.f, -0.9f, 0.9f);
    const VtVec3fArray rightHalf = GridPoints(kSharedQuads, 0.f, 0.9f, -0.9f, 0.9f);

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));

        SetColor(DefineGrid(stage, kSharedA, kSharedQuads, square, kSharedAOffset), red);
        SetColor(DefineGrid(stage, kSharedB, kSharedQuads, square, kSharedBOffset), red);

        UsdGeomMesh subdivided = DefineGrid(stage, kSubdivided, kSubdividedQuads,
            GridPoints(kSubdividedQuads, -0.9f, 0.9f, -0.9f, 0.9f), kSubdividedOffset);
        subdivided.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->catmullClark);
        UsdGeomPrimvarsAPI(subdivided)
            .CreatePrimvar(TfToken("arnold:subdiv_iterations"), SdfValueTypeNames->UChar)
            .Set(static_cast<unsigned char>(2));
        SetColor(subdivided, green);

        // The prototype has no displayColor, it's rendered with the fallback shader, in grey
        UsdGeomPointInstancer instancer = UsdGeomPointInstancer::Define(stage, kInstancer);
        DefineGrid(stage, kPrototype, kPrototypeQuads, GridPoints(kPrototypeQuads, -0.4f, 0.4f, -0.8f, 0.8f), GfVec3d(0));
        instancer.CreatePrototypesRel().SetTargets({kPrototype});
        instancer.CreatePositionsAttr().Set(VtVec3fArray{GfVec3f(-0.5f, 0.f, 0.f), GfVec3f(0.5f, 0.f, 0.f)});
        instancer.CreateProtoIndicesAttr().Set(VtIntArray{0, 0});
        SetTranslate(stage, kInstancer, kInstancerOffset);
        return stage;
    };

    const Check sharedBothFull = AllOf({ExpectColor(Color::Red, At(kSharedAOffset, -0.45, 0)),
        ExpectColor(Color::Red, At(kSharedAOffset, 0.45, 0)), ExpectColor(Color::Red, At(kSharedBOffset, -0.45, 0)),
        ExpectColor(Color::Red, At(kSharedBOffset, 0.45, 0))});
    const Check subdividedCentered = AllOf({ExpectColor(Color::Green, At(kSubdividedOffset, 0, -0.5)),
        ExpectColor(Color::Green, At(kSubdividedOffset, 0, 0.5))});
    const Check instancesFull = AllOf({ExpectColor(Color::Grey, At(kInstancerOffset, -0.5, 0)),
        ExpectColor(Color::Grey, At(kInstancerOffset, 0.5, 0)), ExpectColor(Color::Empty, At(kInstancerOffset, 0, 0))});

    scenario.steps = {
        {"initial render", nullptr, AllOf({sharedBothFull, subdividedCentered, instancesFull})},
        {"points of the first shared mesh changed, the second keeps the shared points",
            [&](const UsdStageRefPtr& stage) { SetPoints(stage, kSharedA, leftHalf); },
            AllOf({ExpectColor(Color::Red, At(kSharedAOffset, -0.45, 0)),
                ExpectColor(Color::Empty, At(kSharedAOffset, 0.45, 0)), ExpectColor(Color::Red, At(kSharedBOffset, -0.45, 0)),
                ExpectColor(Color::Red, At(kSharedBOffset, 0.45, 0)), subdividedCentered, instancesFull})},
        {"the second mesh given the points of the first, both sharing them again",
            [&](const UsdStageRefPtr& stage) { SetPoints(stage, kSharedB, leftHalf); },
            AllOf({ExpectColor(Color::Red, At(kSharedAOffset, -0.45, 0)),
                ExpectColor(Color::Empty, At(kSharedAOffset, 0.45, 0)),
                ExpectColor(Color::Red, At(kSharedBOffset, -0.45, 0)),
                ExpectColor(Color::Empty, At(kSharedBOffset, 0.45, 0))})},
        {"the first shared mesh removed, the second still uses the points",
            [](const UsdStageRefPtr& stage) { stage->RemovePrim(kSharedA); },
            AllOf({ExpectColor(Color::Empty, At(kSharedAOffset, -0.45, 0)),
                ExpectColor(Color::Red, At(kSharedBOffset, -0.45, 0)),
                ExpectColor(Color::Empty, At(kSharedBOffset, 0.45, 0))}),
            {{ProceduralLegacyReader, "the legacy reader doesn't delete removed prims"}}},
        {"points of the remaining shared mesh changed",
            [&](const UsdStageRefPtr& stage) { SetPoints(stage, kSharedB, rightHalf); },
            AllOf({ExpectColor(Color::Empty, At(kSharedBOffset, -0.45, 0)),
                ExpectColor(Color::Red, At(kSharedBOffset, 0.45, 0))})},
        {"points of the subdivided mesh moved up",
            [](const UsdStageRefPtr& stage) {
                SetPoints(stage, kSubdivided, GridPoints(kSubdividedQuads, -0.9f, 0.9f, -0.2f, 1.f));
            },
            AllOf({ExpectColor(Color::Empty, At(kSubdividedOffset, 0, -0.6)),
                ExpectColor(Color::Green, At(kSubdividedOffset, 0, 0.5))})},
        {"subdivision turned off and points moved down in the same edit",
            [](const UsdStageRefPtr& stage) {
                UsdGeomMesh mesh(stage->GetPrimAtPath(kSubdivided));
                mesh.GetSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
                mesh.GetPointsAttr().Set(GridPoints(kSubdividedQuads, -0.9f, 0.9f, -1.f, 0.2f));
            },
            AllOf({ExpectColor(Color::Green, At(kSubdividedOffset, 0, -0.6)),
                ExpectColor(Color::Empty, At(kSubdividedOffset, 0, 0.5))})},
        {"points of the instancer prototype narrowed",
            [](const UsdStageRefPtr& stage) {
                SetPoints(stage, kPrototype, GridPoints(kPrototypeQuads, -0.1f, 0.1f, -0.8f, 0.8f));
            },
            AllOf({ExpectColor(Color::Grey, At(kInstancerOffset, -0.5, 0)),
                ExpectColor(Color::Empty, At(kInstancerOffset, -0.75, 0)),
                ExpectColor(Color::Grey, At(kInstancerOffset, 0.5, 0)),
                ExpectColor(Color::Empty, At(kInstancerOffset, 0.75, 0))})},
        {"the subdivided mesh replaced by a large grid of 65536 points, in blue",
            [&](const UsdStageRefPtr& stage) {
                stage->RemovePrim(kSubdivided);
                SetColor(DefineGrid(stage, kSubdivided, kLargeQuads,
                             GridPoints(kLargeQuads, -0.9f, 0.9f, -0.9f, 0.9f), kSubdividedOffset),
                    blue);
            },
            AllOf({ExpectColor(Color::Blue, At(kSubdividedOffset, 0, -0.6)),
                ExpectColor(Color::Blue, At(kSubdividedOffset, 0, 0.5))})},
        {"points of the large grid moved right",
            [](const UsdStageRefPtr& stage) {
                SetPoints(stage, kSubdivided, GridPoints(kLargeQuads, 0.f, 0.9f, -0.9f, 0.9f));
            },
            AllOf({ExpectColor(Color::Empty, At(kSubdividedOffset, -0.45, 0)),
                ExpectColor(Color::Blue, At(kSubdividedOffset, 0.45, 0)),
                ExpectColor(Color::Red, At(kSharedBOffset, 0.45, 0)),
                ExpectColor(Color::Grey, At(kInstancerOffset, 0.5, 0))})},
    };
    return Run(scenario);
}
