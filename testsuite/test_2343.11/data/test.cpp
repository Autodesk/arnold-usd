// Interactive edits of point instancers whose prototypes are deduplicated by the render delegate, with the default
// HDARNOLD_GEOM_DEDUP=1 (identical point instancer prototypes share their arnold geometry): the prototype of one of
// two point instancers is edited, becoming different from the other one and identical again, and the other point
// instancer is removed. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/base/tf/setenv.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kLeft("/World/left");
const SdfPath kRight("/World/right");

SdfPath Prototype(const SdfPath& instancer) { return instancer.AppendPath(SdfPath("prototypes/cube")); }

// A point instancer of a red cube, with an instance above and below its origin
void DefineInstancer(const UsdStageRefPtr& stage, const SdfPath& path, double x)
{
    UsdGeomPointInstancer instancer = UsdGeomPointInstancer::Define(stage, path);
    UsdGeomCube cube = UsdGeomCube::Define(stage, Prototype(path));
    cube.CreateSizeAttr().Set(0.8);
    cube.CreateDisplayColorPrimvar().Set(VtVec3fArray{GfVec3f(0.8f, 0.05f, 0.05f)});
    instancer.CreatePrototypesRel().SetTargets({Prototype(path)});
    instancer.CreatePositionsAttr().Set(VtVec3fArray{GfVec3f(0.f, 1.f, 0.f), GfVec3f(0.f, -1.f, 0.f)});
    instancer.CreateProtoIndicesAttr().Set(VtIntArray{0, 0});
    SetTranslate(stage, path, GfVec3d(x, 0, 0));
}

void SetSize(const UsdStageRefPtr& stage, const SdfPath& instancer, double size)
{
    UsdGeomCube(stage->GetPrimAtPath(Prototype(instancer))).GetSizeAttr().Set(size);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    // The default, set in case the environment of the test has another one. Read when the render delegate is created.
    TfSetenv("HDARNOLD_GEOM_DEDUP", "1");

    // Centers of the bottom instances, and points inside the 0.8 cubes but outside 0.3 ones
    const GfVec2f leftCenter = ScreenPosition(GfVec3d(-1.5, -1, 0));
    const GfVec2f leftEdge = ScreenPosition(GfVec3d(-1.2, 1.3, 0));
    const GfVec2f rightCenter = ScreenPosition(GfVec3d(1.5, -1, 0));
    const GfVec2f rightEdge = ScreenPosition(GfVec3d(1.8, 1.3, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        DefineInstancer(stage, kLeft, -1.5);
        DefineInstancer(stage, kRight, 1.5);
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr, AllOf({ExpectColor(Color::Red, leftEdge), ExpectColor(Color::Red, rightEdge)})},
        {"size change of the right prototype, making it smaller",
            [](const UsdStageRefPtr& stage) { SetSize(stage, kRight, 0.3); },
            AllOf({ExpectColor(Color::Red, leftEdge), ExpectColor(Color::Red, rightCenter),
                ExpectColor(Color::Empty, rightEdge)})},
        {"size change of the right prototype, identical to the left one again",
            [](const UsdStageRefPtr& stage) { SetSize(stage, kRight, 0.8); },
            AllOf({ExpectColor(Color::Red, leftEdge), ExpectColor(Color::Red, rightEdge)})},
        {"displayColor change of the left prototype",
            [](const UsdStageRefPtr& stage) {
                UsdGeomCube(stage->GetPrimAtPath(Prototype(kLeft)))
                    .GetDisplayColorPrimvar()
                    .Set(VtVec3fArray{GfVec3f(0.05f, 0.8f, 0.05f)});
            },
            AllOf({ExpectColor(Color::Green, leftEdge), ExpectColor(Color::Green, leftCenter),
                ExpectColor(Color::Red, rightEdge)})},
        {"left point instancer removal", [](const UsdStageRefPtr& stage) { stage->RemovePrim(kLeft); },
            AllOf({ExpectColor(Color::Empty, leftEdge), ExpectColor(Color::Empty, leftCenter),
                ExpectColor(Color::Red, rightEdge)})},
        {"left point instancer added back", [](const UsdStageRefPtr& stage) { DefineInstancer(stage, kLeft, -1.5); },
            AllOf({ExpectColor(Color::Red, leftEdge), ExpectColor(Color::Red, rightEdge)})},
        {"size change of the left prototype, making it smaller",
            [](const UsdStageRefPtr& stage) { SetSize(stage, kLeft, 0.3); },
            AllOf({ExpectColor(Color::Empty, leftEdge), ExpectColor(Color::Red, leftCenter),
                ExpectColor(Color::Red, rightEdge)})},
        {"right point instancer removal", [](const UsdStageRefPtr& stage) { stage->RemovePrim(kRight); },
            AllOf({ExpectColor(Color::Red, leftCenter), ExpectColor(Color::Empty, rightEdge),
                ExpectColor(Color::Empty, rightCenter)})},
    };
    scenario.skippedModes = {{ProceduralLegacyReader, "the legacy reader doesn't deduplicate geometry"}};
    return Run(scenario);
}
