// Interactive edits of points and curves: positions, widths, curve topology and displayColor.
// See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kPoints("/World/points");
const SdfPath kCurves("/World/curves");

UsdGeomPoints Points(const UsdStageRefPtr& stage) { return UsdGeomPoints(stage->GetPrimAtPath(kPoints)); }
UsdGeomBasisCurves Curves(const UsdStageRefPtr& stage) { return UsdGeomBasisCurves(stage->GetPrimAtPath(kCurves)); }

// Vertical linear curves from y = -1 to 1, at the given x coordinates
void SetCurves(const UsdStageRefPtr& stage, const std::vector<float>& xs)
{
    VtVec3fArray points;
    VtIntArray counts;
    for (float x : xs) {
        points.push_back(GfVec3f(x, -1.f, 0.f));
        points.push_back(GfVec3f(x, 1.f, 0.f));
        counts.push_back(2);
    }
    Curves(stage).GetPointsAttr().Set(points);
    Curves(stage).GetCurveVertexCountsAttr().Set(counts);
}

} // namespace

int main(int, char**)
{
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));
    const GfVec2f left = ScreenPosition(GfVec3d(-1.5, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.5, 0, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));

        // Two red points (spheres of diameter 0.8) on the sides
        UsdGeomPoints points = UsdGeomPoints::Define(stage, kPoints);
        points.CreatePointsAttr().Set(VtVec3fArray{GfVec3f(-1.5f, 0.f, 0.f), GfVec3f(1.5f, 0.f, 0.f)});
        points.CreateWidthsAttr().Set(VtFloatArray{0.8f, 0.8f});
        points.CreateDisplayColorPrimvar().Set(VtVec3fArray{GfVec3f(0.8f, 0.05f, 0.05f)});

        // A green linear curve of width 0.3 in the center
        UsdGeomBasisCurves curves = UsdGeomBasisCurves::Define(stage, kCurves);
        curves.CreateTypeAttr().Set(UsdGeomTokens->linear);
        curves.CreateWidthsAttr().Set(VtFloatArray{0.3f});
        curves.SetWidthsInterpolation(UsdGeomTokens->constant);
        curves.CreateDisplayColorPrimvar().Set(VtVec3fArray{GfVec3f(0.05f, 0.8f, 0.05f)});
        SetCurves(stage, {0.f});
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr,
            AllOf({ExpectColor(Color::Red, left), ExpectColor(Color::Green, center), ExpectColor(Color::Red, right)})},
        {"points positions change, moving them up",
            [](const UsdStageRefPtr& stage) {
                Points(stage).GetPointsAttr().Set(VtVec3fArray{GfVec3f(-1.5f, 1.2f, 0.f), GfVec3f(1.5f, 1.2f, 0.f)});
            },
            AllOf({ExpectColor(Color::Empty, left), ExpectColor(Color::Red, ScreenPosition(GfVec3d(-1.5, 1.2, 0))),
                ExpectColor(Color::Empty, ScreenPosition(GfVec3d(-1.5, 1.9, 0)))})},
        {"points widths change, making them twice bigger",
            [](const UsdStageRefPtr& stage) { Points(stage).GetWidthsAttr().Set(VtFloatArray{1.6f, 1.6f}); },
            ExpectColor(Color::Red, ScreenPosition(GfVec3d(-1.5, 1.9, 0)))},
        {"points displayColor change",
            [](const UsdStageRefPtr& stage) {
                Points(stage).GetDisplayColorPrimvar().Set(VtVec3fArray{GfVec3f(0.05f, 0.05f, 0.8f)});
            },
            ExpectColor(Color::Blue, ScreenPosition(GfVec3d(-1.5, 1.2, 0)))},
        {"curve points change, moving the curve to the right",
            [](const UsdStageRefPtr& stage) { SetCurves(stage, {1.f}); },
            AllOf({ExpectColor(Color::Empty, center), ExpectColor(Color::Green, ScreenPosition(GfVec3d(1, 0, 0)))})},
        {"curve widths change, making it wider",
            [](const UsdStageRefPtr& stage) { Curves(stage).GetWidthsAttr().Set(VtFloatArray{0.8f}); },
            ExpectColor(Color::Green, ScreenPosition(GfVec3d(1.3, -0.5, 0))),
            {{ProceduralHydra | AnyDelegate,
                "HdArnoldBasisCurves ignores DirtyWidths, the widths are only updated with another dirty primvar"}}},
        {"curves topology change, adding a curve on the left",
            [](const UsdStageRefPtr& stage) { SetCurves(stage, {-1.f, 1.f}); },
            AllOf({ExpectColor(Color::Green, ScreenPosition(GfVec3d(-1, 0, 0))),
                ExpectColor(Color::Green, ScreenPosition(GfVec3d(1, 0, 0)))})},
    };
    return Run(scenario);
}
