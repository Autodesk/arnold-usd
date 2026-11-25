// Interactive edits of point instancers whose curves prototypes are deduplicated by the render delegate, with the
// default HDARNOLD_GEOM_DEDUP=1 (identical point instancer prototypes share their arnold geometry). Covers the fix of
// the dedup incremental updates (#2797): the points of a curves canonical re-set with an unchanged hash made Arnold
// reinitialize it while its instances still used its arrays, and a canonical could be freed while instancers
// referenced it. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/base/tf/setenv.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kLeft("/World/left");
const SdfPath kRight("/World/right");
const VtVec3fArray kRed = {GfVec3f(0.8f, 0.05f, 0.05f)};
const VtVec3fArray kGreen = {GfVec3f(0.05f, 0.8f, 0.05f)};

SdfPath Prototype(const SdfPath& instancer) { return instancer.AppendPath(SdfPath("prototypes/curve")); }

UsdGeomBasisCurves Curves(const UsdStageRefPtr& stage, const SdfPath& instancer)
{
    return UsdGeomBasisCurves(stage->GetPrimAtPath(Prototype(instancer)));
}

// A vertical linear curve from y = -0.4 to 0.4, at the given x offset in the prototype
void SetCurve(const UsdStageRefPtr& stage, const SdfPath& instancer, float x)
{
    UsdGeomBasisCurves curves = Curves(stage, instancer);
    curves.GetPointsAttr().Set(VtVec3fArray{GfVec3f(x, -0.4f, 0.f), GfVec3f(x, 0.4f, 0.f)});
    curves.GetCurveVertexCountsAttr().Set(VtIntArray{2});
}

// A point instancer of a red curve of width 0.3, with an instance above and below its origin
void DefineInstancer(const UsdStageRefPtr& stage, const SdfPath& path, double x)
{
    UsdGeomPointInstancer instancer = UsdGeomPointInstancer::Define(stage, path);
    UsdGeomBasisCurves curves = UsdGeomBasisCurves::Define(stage, Prototype(path));
    curves.CreateTypeAttr().Set(UsdGeomTokens->linear);
    curves.CreateWidthsAttr().Set(VtFloatArray{0.3f});
    curves.SetWidthsInterpolation(UsdGeomTokens->constant);
    curves.CreateDisplayColorPrimvar(UsdGeomTokens->constant).Set(kRed);
    curves.CreatePointsAttr();
    curves.CreateCurveVertexCountsAttr();
    SetCurve(stage, path, 0.f);
    instancer.CreatePrototypesRel().SetTargets({Prototype(path)});
    instancer.CreatePositionsAttr().Set(VtVec3fArray{GfVec3f(0.f, 1.f, 0.f), GfVec3f(0.f, -1.f, 0.f)});
    instancer.CreateProtoIndicesAttr().Set(VtIntArray{0, 0});
    SetTranslate(stage, path, GfVec3d(x, 0, 0));
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    // The default, set in case the environment of the test has another one. Read when the render delegate is created.
    TfSetenv("HDARNOLD_GEOM_DEDUP", "1");

    // Centers of the top and bottom instances of each instancer, and where the left ones move
    const GfVec2f leftTop = ScreenPosition(GfVec3d(-1.5, 1, 0));
    const GfVec2f leftBottom = ScreenPosition(GfVec3d(-1.5, -1, 0));
    const GfVec2f leftMovedTop = ScreenPosition(GfVec3d(-0.5, 1, 0));
    const GfVec2f leftMovedBottom = ScreenPosition(GfVec3d(-0.5, -1, 0));
    const GfVec2f rightTop = ScreenPosition(GfVec3d(1.5, 1, 0));
    const GfVec2f rightBottom = ScreenPosition(GfVec3d(1.5, -1, 0));

    const Check allRed = AllOf({ExpectColor(Color::Red, leftTop), ExpectColor(Color::Red, leftBottom),
        ExpectColor(Color::Red, rightTop), ExpectColor(Color::Red, rightBottom)});

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        // Two identical prototypes, sharing a single arnold curves node
        DefineInstancer(stage, kLeft, -1.5);
        DefineInstancer(stage, kRight, 1.5);
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr, allRed},
        {"points of the left prototype set again to the same values, the dedup hash is unchanged",
            [](const UsdStageRefPtr& stage) { SetCurve(stage, kLeft, 0.f); }, allRed},
        {"displayColor change of the left prototype, leaving the dedup group",
            [](const UsdStageRefPtr& stage) { Curves(stage, kLeft).GetDisplayColorPrimvar().Set(kGreen); },
            AllOf({ExpectColor(Color::Green, leftTop), ExpectColor(Color::Green, leftBottom),
                ExpectColor(Color::Red, rightTop), ExpectColor(Color::Red, rightBottom)})},
        {"displayColor change of the left prototype, identical to the right one again",
            [](const UsdStageRefPtr& stage) { Curves(stage, kLeft).GetDisplayColorPrimvar().Set(kRed); }, allRed},
        {"points change of the left prototype, moving its curve to the right",
            [](const UsdStageRefPtr& stage) { SetCurve(stage, kLeft, 1.f); },
            AllOf({ExpectColor(Color::Empty, leftTop), ExpectColor(Color::Empty, leftBottom),
                ExpectColor(Color::Red, leftMovedTop), ExpectColor(Color::Red, leftMovedBottom),
                ExpectColor(Color::Red, rightTop), ExpectColor(Color::Red, rightBottom)})},
        {"points change of the left prototype, identical to the right one again",
            [](const UsdStageRefPtr& stage) { SetCurve(stage, kLeft, 0.f); },
            AllOf({allRed, ExpectColor(Color::Empty, leftMovedTop)})},
        {"right point instancer removal", [](const UsdStageRefPtr& stage) { stage->RemovePrim(kRight); },
            AllOf({ExpectColor(Color::Red, leftTop), ExpectColor(Color::Red, leftBottom),
                ExpectColor(Color::Empty, rightTop), ExpectColor(Color::Empty, rightBottom)})},
        {"points of the left prototype set again to the same values, once alone",
            [](const UsdStageRefPtr& stage) { SetCurve(stage, kLeft, 0.f); },
            AllOf({ExpectColor(Color::Red, leftTop), ExpectColor(Color::Red, leftBottom),
                ExpectColor(Color::Empty, rightTop)})},
    };
    scenario.skippedModes = {{ProceduralLegacyReader, "the legacy reader doesn't deduplicate geometry"}};
    return Run(scenario);
}
