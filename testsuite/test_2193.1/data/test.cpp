// Interactive edits of the instances of a mesh light authored with primvars:arnold:light. Each instance of the mesh light
// must light the wall from its instance and prototype transforms, the prototype light itself must not emit.
// See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kWall("/World/wall");
const SdfPath kInstancer("/World/instancer");
const SdfPath kLamp("/World/instancer/prototypes/lamp");

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

void SetPositions(const UsdStageRefPtr& stage, const VtVec3fArray& positions)
{
    UsdGeomPointInstancer(stage->GetPrimAtPath(kInstancer)).GetPositionsAttr().Set(positions);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const GfVec2f left = ScreenPosition(GfVec3d(-1.5, 0, 0));
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.5, 0, 0));
    // The prototype is in front of the center of the wall: it lights the center if it emits.
    // Without its own transform, a lamp would be in the plane of the wall and wouldn't light it.
    const Check leftAndRight =
        AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Black, center), ExpectColor(Color::Grey, right)});
    const Check leftAndCenter =
        AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Grey, center), ExpectColor(Color::Black, right)});
    const Check centerAndRight =
        AllOf({ExpectColor(Color::Black, left), ExpectColor(Color::Grey, center), ExpectColor(Color::Grey, right)});
    const Check unlit =
        AllOf({ExpectColor(Color::Black, left), ExpectColor(Color::Black, center), ExpectColor(Color::Black, right)});
    // As for the other instanced lights (e.g. with the MeshLightAPI), the light instances rebuilt by an interactive
    // update of the procedural are not rendered.
    const std::vector<KnownIssue> proceduralUpdateIssue = {
        {ProceduralHydra, "the instanced lights rebuilt by an interactive update of the procedural aren't rendered"}};

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));

        // A grey wall, only lit by the instances of the lamp
        DefineQuad(stage, kWall, 3.f, 0.f, true);
        UsdGeomMesh(stage->GetPrimAtPath(kWall)).CreateDisplayColorPrimvar(UsdGeomTokens->constant)
            .Set(VtVec3fArray{GfVec3f(0.5f, 0.5f, 0.5f)});

        // The lamp, a small mesh light facing the wall, moved in front of it by its own transform. It isn't visible
        // to the camera, its noisy surface would make the reference image unstable.
        UsdGeomPointInstancer instancer = UsdGeomPointInstancer::Define(stage, kInstancer);
        DefineQuad(stage, kLamp, 0.25f, 0.f, false);
        SetTranslate(stage, kLamp, GfVec3d(0, 0, 0.3));
        SetLampPrimvar(stage, TfToken("arnold:light"), SdfValueTypeNames->Bool, VtValue(true));
        SetLampPrimvar(stage, TfToken("arnold:light:intensity"), SdfValueTypeNames->Float, VtValue(0.5f));
        SetLampPrimvar(stage, TfToken("arnold:visibility:camera"), SdfValueTypeNames->Bool, VtValue(false));
        instancer.CreatePrototypesRel().SetTargets({kLamp});
        instancer.CreateProtoIndicesAttr().Set(VtIntArray{0, 0});
        SetPositions(stage, {GfVec3f(-1.5f, 0, 0), GfVec3f(1.5f, 0, 0)});
        return stage;
    };

    scenario.steps = {
        {"initial render, the two instances light the wall", nullptr, leftAndRight},
        // The intensity is set on the prototype light, it must be given to the instances again
        {"mesh light intensity change",
            [](const UsdStageRefPtr& stage) {
                SetLampPrimvar(stage, TfToken("arnold:light:intensity"), SdfValueTypeNames->Float, VtValue(1.f));
            },
            leftAndRight, proceduralUpdateIssue},
        {"instance moved",
            [](const UsdStageRefPtr& stage) { SetPositions(stage, {GfVec3f(-1.5f, 0, 0), GfVec3f(0, 0, 0)}); },
            leftAndCenter, proceduralUpdateIssue},
        {"mesh light disabled",
            [](const UsdStageRefPtr& stage) {
                SetLampPrimvar(stage, TfToken("arnold:light"), SdfValueTypeNames->Bool, VtValue(false));
            },
            unlit},
        {"mesh light enabled again",
            [](const UsdStageRefPtr& stage) {
                SetLampPrimvar(stage, TfToken("arnold:light"), SdfValueTypeNames->Bool, VtValue(true));
            },
            leftAndCenter, proceduralUpdateIssue},
        // The transform of the prototype moves all the instances
        {"prototype moved", [](const UsdStageRefPtr& stage) { SetTranslate(stage, kLamp, GfVec3d(1.5, 0, 0.3)); },
            centerAndRight, proceduralUpdateIssue},
    };
    // The initial render is the same in all the modes
    scenario.referenceStep = 0;
    return Run(scenario);
}
