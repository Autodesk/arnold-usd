// Interactive edits of an arnold primvar of a camera whose arnold primvars are only in the primvars container of its
// Hydra 2 prim, as with MayaHydra: a grey sphere seen by a camera whose primvars:arnold:exposure changes. See
// interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>

#ifdef ENABLE_SCENE_INDEX
#include <pxr/imaging/hd/cameraSchema.h>
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#endif

#include <vector>

using namespace interactive_usd_test;

namespace {

const TfToken exposurePrimvar("primvars:arnold:exposure");

#ifdef ENABLE_SCENE_INDEX
TF_DECLARE_REF_PTRS(MayaHydraLikeCameraSceneIndex);

// Reproduces the camera prims of MayaHydra (with the MtoA scene index): the camera schema only holds its own fields,
// while UsdImaging also answers there for any attribute of the camera prim, and the arnold primvars are in the
// primvars container, as "arnold:exposure"
class MayaHydraLikeCameraSceneIndex final : public HdSingleInputFilteringSceneIndexBase {
public:
    static MayaHydraLikeCameraSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr& inputSceneIndex)
    {
        return TfCreateRefPtr(new MayaHydraLikeCameraSceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath& primPath) const override
    {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
        if (prim.primType != HdPrimTypeTokens->camera || !prim.dataSource)
            return prim;
        const HdContainerDataSourceHandle cameraDs =
            HdContainerDataSource::Cast(prim.dataSource->Get(HdCameraSchemaTokens->camera));
        if (!cameraDs)
            return prim;

        // A copy of the camera schema fields, without the lookup of the other attributes by name
        TfTokenVector names = cameraDs->GetNames();
        std::vector<HdDataSourceBaseHandle> values;
        for (const TfToken& name : names)
            values.push_back(cameraDs->Get(name));
        HdContainerDataSourceEditor editor(prim.dataSource);
        editor.Set(
            HdCameraSchema::GetDefaultLocator(),
            HdRetainedContainerDataSource::New(names.size(), names.data(), values.data()));

        // The arnold primvar, read through that lookup by name
        if (const HdSampledDataSourceHandle exposureDs = HdSampledDataSource::Cast(cameraDs->Get(exposurePrimvar))) {
            editor.Set(
                HdPrimvarsSchema::GetDefaultLocator().Append(TfToken("arnold:exposure")),
                HdPrimvarSchema::Builder()
                    .SetPrimvarValue(exposureDs)
                    .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(HdPrimvarSchemaTokens->constant))
                    .Build());
        }
        return {prim.primType, editor.Finish()};
    }

    SdfPathVector GetChildPrimPaths(const SdfPath& primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    MayaHydraLikeCameraSceneIndex(const HdSceneIndexBaseRefPtr& inputSceneIndex)
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
    }

    void _PrimsAdded(const HdSceneIndexBase&, const HdSceneIndexObserver::AddedPrimEntries& entries) override
    {
        _SendPrimsAdded(entries);
    }

    void _PrimsRemoved(const HdSceneIndexBase&, const HdSceneIndexObserver::RemovedPrimEntries& entries) override
    {
        _SendPrimsRemoved(entries);
    }

    // UsdImaging dirties the camera schema when an attribute of the camera changes. MayaHydra only dirties the
    // primvars when an extension attribute such as aiExposure changes, which HdDirtyBitsTranslator doesn't map to any
    // camera dirty bit: the arnold scene index plugins must dirty the camera schema. As this test only edits the arnold
    // exposure of the camera, its camera schema changes are replaced by the change of that primvar.
    void _PrimsDirtied(const HdSceneIndexBase&, const HdSceneIndexObserver::DirtiedPrimEntries& entries) override
    {
        static const HdDataSourceLocator exposureLocator =
            HdPrimvarsSchema::GetDefaultLocator().Append(TfToken("arnold:exposure"));
        HdSceneIndexObserver::DirtiedPrimEntries dirtied;
        for (const auto& entry : entries) {
            HdDataSourceLocatorSet locators;
            bool cameraDirtied = false;
            for (const HdDataSourceLocator& locator : entry.dirtyLocators) {
                if (locator.HasPrefix(HdCameraSchema::GetDefaultLocator()))
                    cameraDirtied = true;
                else
                    locators.insert(locator);
            }
            if (cameraDirtied)
                locators.insert(exposureLocator);
            dirtied.emplace_back(entry.primPath, locators);
        }
        _SendPrimsDirtied(dirtied);
    }
};
#endif

// The sphere is overexposed, its unclamped color being far above 1
Check ExpectOverexposed()
{
    return [](const Image& image) -> std::string {
        const GfVec4f color = image.Average(0.5f, 0.5f);
        const float luminance = (color[0] + color[1] + color[2]) / 3.f;
        if (luminance > 2.f)
            return {};
        return TfStringPrintf("expected an overexposed sphere, its average color is %g", luminance);
    };
}

void SetExposure(const UsdStageRefPtr& stage, const SdfPath& camera, float exposure)
{
    stage->GetPrimAtPath(camera).CreateAttribute(exposurePrimvar, SdfValueTypeNames->Float).Set(exposure);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    Scenario scenario;
    // The procedural renders through an arnold camera of its own, see Scenario::camera
    scenario.methods = RenderDelegate;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        SetExposure(stage, scenario.camera, 0.f);
        DefineDistantLight(stage, SdfPath("/World/light"));
        const SdfPath sphere("/World/sphere");
        UsdGeomSphere::Define(stage, sphere).CreateRadiusAttr().Set(1.0);
        BindMaterial(
            stage, sphere, DefinePreviewMaterial(stage, SdfPath("/World/materials/grey"), GfVec3f(0.5f, 0.5f, 0.5f)));
        return stage;
    };
#ifdef ENABLE_SCENE_INDEX
    scenario.delegateSceneIndexFilter = [](const HdSceneIndexBaseRefPtr& sceneIndex) -> HdSceneIndexBaseRefPtr {
        return MayaHydraLikeCameraSceneIndex::New(sceneIndex);
    };
#endif

    scenario.steps = {
        {"initial render", nullptr, ExpectColor(Color::Grey)},
        {"camera exposure change to 4",
            [&](const UsdStageRefPtr& stage) { SetExposure(stage, scenario.camera, 4.f); }, ExpectOverexposed()},
        {"camera exposure change back to 0",
            [&](const UsdStageRefPtr& stage) { SetExposure(stage, scenario.camera, 0.f); }, ExpectColor(Color::Grey)},
    };
    // The overexposed render
    scenario.referenceStep = 1;
    return Run(scenario);
}
