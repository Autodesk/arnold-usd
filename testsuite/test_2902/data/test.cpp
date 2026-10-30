// Interactive edits of a mesh: points, topology, displayColor values and interpolation, visibility and material
// binding. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kMesh("/World/mesh");

// Quads facing the camera, between the given x coordinates, from y = -1 to 1
void SetQuads(const UsdStageRefPtr& stage, const std::vector<std::pair<float, float>>& quads)
{
    VtVec3fArray points;
    VtIntArray counts;
    VtIntArray indices;
    for (const auto& quad : quads) {
        const int first = static_cast<int>(points.size());
        points.push_back(GfVec3f(quad.first, -1.f, 0.f));
        points.push_back(GfVec3f(quad.second, -1.f, 0.f));
        points.push_back(GfVec3f(quad.second, 1.f, 0.f));
        points.push_back(GfVec3f(quad.first, 1.f, 0.f));
        counts.push_back(4);
        for (int i = 0; i < 4; ++i)
            indices.push_back(first + i);
    }
    UsdGeomMesh mesh(stage->GetPrimAtPath(kMesh));
    mesh.GetPointsAttr().Set(points);
    mesh.GetFaceVertexCountsAttr().Set(counts);
    mesh.GetFaceVertexIndicesAttr().Set(indices);
}

void SetDisplayColor(const UsdStageRefPtr& stage, const VtVec3fArray& colors, const TfToken& interpolation)
{
    UsdGeomPrimvar displayColor = UsdGeomMesh(stage->GetPrimAtPath(kMesh)).GetDisplayColorPrimvar();
    displayColor.SetInterpolation(interpolation);
    displayColor.Set(colors);
}

} // namespace

int main(int, char**)
{
    const std::vector<KnownIssue> interpolationIssue = {{ProceduralLegacyReader,
        "the legacy reader can't update displayColor once its interpolation changed (could not set ARRAY parameter)"}};
    const VtVec3fArray red = {GfVec3f(0.8f, 0.05f, 0.05f)};
    const VtVec3fArray green = {GfVec3f(0.05f, 0.8f, 0.05f)};
    const VtVec3fArray greenAndBlue = {GfVec3f(0.05f, 0.8f, 0.05f), GfVec3f(0.05f, 0.05f, 0.8f)};
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));
    const GfVec2f left = ScreenPosition(GfVec3d(-1.5, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.5, 0, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, kMesh);
        mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
        mesh.CreateDisplayColorPrimvar(UsdGeomTokens->constant);
        SetQuads(stage, {{-1.f, 1.f}});
        SetDisplayColor(stage, red, UsdGeomTokens->constant);
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr, AllOf({ExpectColor(Color::Red, center), ExpectColor(Color::Empty, right)})},
        {"points change, making the quad wider", [](const UsdStageRefPtr& stage) { SetQuads(stage, {{-2.5f, 2.5f}}); },
            AllOf({ExpectColor(Color::Red, center), ExpectColor(Color::Red, right)})},
        {"topology change, splitting the quad in two",
            [](const UsdStageRefPtr& stage) { SetQuads(stage, {{-2.5f, -0.5f}, {0.5f, 2.5f}}); },
            AllOf({ExpectColor(Color::Empty, center), ExpectColor(Color::Red, left), ExpectColor(Color::Red, right)})},
        {"constant displayColor change",
            [&](const UsdStageRefPtr& stage) { SetDisplayColor(stage, green, UsdGeomTokens->constant); },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Green, right)})},
        {"displayColor interpolation change, to one color per face",
            [&](const UsdStageRefPtr& stage) { SetDisplayColor(stage, greenAndBlue, UsdGeomTokens->uniform); },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Blue, right)}), interpolationIssue},
        {"visibility change to invisible",
            [](const UsdStageRefPtr& stage) { UsdGeomImageable(stage->GetPrimAtPath(kMesh)).MakeInvisible(); },
            AllOf({ExpectColor(Color::Empty, left), ExpectColor(Color::Empty, right)})},
        {"visibility change back to visible",
            [](const UsdStageRefPtr& stage) { UsdGeomImageable(stage->GetPrimAtPath(kMesh)).MakeVisible(); },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Blue, right)}), interpolationIssue},
        {"material binding",
            [](const UsdStageRefPtr& stage) {
                BindMaterial(stage, kMesh,
                    DefinePreviewMaterial(stage, SdfPath("/World/materials/red"), GfVec3f(0.8f, 0.05f, 0.05f)));
            },
            AllOf({ExpectColor(Color::Red, left), ExpectColor(Color::Red, right)})},
        {"material unbinding, back to the displayColor",
            [](const UsdStageRefPtr& stage) { UsdShadeMaterialBindingAPI(stage->GetPrimAtPath(kMesh)).UnbindAllBindings(); },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Blue, right)}), interpolationIssue},
    };
    return Run(scenario);
}
