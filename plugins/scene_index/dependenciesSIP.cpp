
#include "dependenciesSIP.h"
#ifdef ENABLE_SCENE_INDEX // Hydra 2
#include <unordered_map>
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/dataSourceLocator.h>
#include <pxr/imaging/hd/dataSourceTypeDefs.h>
#include <pxr/imaging/hd/dependenciesSchema.h>
#include <pxr/imaging/hd/dependencySchema.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/lazyContainerDataSource.h>
#include <pxr/imaging/hd/lightSchema.h>
#include <pxr/imaging/hd/mapContainerDataSource.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/materialSchema.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdArnoldDependencySceneIndexPlugin"))
    (__dependenciesToFilters)
    // Material bindings: when a material is added/removed, geometry with that
    // binding must be invalidated (see HdDependencyForwardingSceneIndex).
    (arnoldAddedToMaterialBindings)
    (arnoldRemovedToMaterialBindings)
    (arnoldMaterialBindingsToAddedDependency)
    (arnoldMaterialBindingsToRemovedDependency)
    ((added, "__added__"))
    ((removed, "__removed__")));

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldDependencySceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // This scene index should be added *before*
    // HdArnoldDependencyForwardingSceneIndexPlugin (which currently uses 1000),
    // but subsequent to any scene indexes that generate data sources which
    // imply dependencies for this scene index to add.
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 900;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName, nullptr, insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}
namespace {

HdContainerDataSourceHandle _BuildLightArnoldShaderDependenciesDs(const std::string &filterPathStr)
{
    if (filterPathStr.empty()) {
        return nullptr;
    }

    TfTokenVector names;
    std::vector<HdDataSourceBaseHandle> deps;

    // Register a dependency on each filter targeted by the light such that
    // the invalidation of *any* locator on the filter invalidates the 'light'
    // locator of the light prim.
    // This matches the legacy dependency declaration in HdPrman_Light using
    // HdChangeTracker::{Add,Remove}SprimSprimDependency.
    // Note that this is conservative in a catch-all sense and we could instead
    // register individual dependency entries for collection, visibility, light
    // and material locators.
    //
    // Additionally, declare that the dependencies depends on the targeted
    // filters.

    static HdLocatorDataSourceHandle filtersLocDs = HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
        HdPrimvarsSchema::GetDefaultLocator().Append(TfToken("arnold:shaders")));

    static HdLocatorDataSourceHandle dependenciesLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(HdDependenciesSchema::GetDefaultLocator());

    names.push_back(_tokens->__dependenciesToFilters);
    deps.push_back(HdDependencySchema::Builder()
                       .SetDependedOnPrimPath(/* self */ nullptr)
                       .SetDependedOnDataSourceLocator(filtersLocDs)
                       .SetAffectedDataSourceLocator(dependenciesLocDs)
                       .Build());

    static HdLocatorDataSourceHandle materialLocatorDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(HdMaterialSchema::GetDefaultLocator());

    static HdLocatorDataSourceHandle emptyLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(HdDataSourceLocator::EmptyLocator());

    static HdLocatorDataSourceHandle outputsLoc = HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
        HdDataSourceLocator(TfToken("outputs"), TfToken("filters"), TfToken("i1")));

    static const HdLocatorDataSourceHandle lightDSL =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(HdLightSchema::GetDefaultLocator());

    static HdLocatorDataSourceHandle affectedLocatorDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(HdPrimvarsSchema::GetDefaultLocator());

    names.push_back(TfToken(filterPathStr));
    deps.push_back(HdDependencySchema::Builder()
                       .SetDependedOnPrimPath(HdRetainedTypedSampledDataSource<SdfPath>::New(SdfPath(filterPathStr)))
                       .SetDependedOnDataSourceLocator(materialLocatorDs)

                       .SetAffectedDataSourceLocator(lightDSL)
                       .Build());

    names.push_back(TfToken("OnDependencies"));
    deps.push_back(HdDependencySchema::Builder()
                       .SetDependedOnDataSourceLocator(lightDSL)
                       .SetAffectedDataSourceLocator(dependenciesLocDs)
                       .Build());

    return HdRetainedContainerDataSource::New(3, names.data(), deps.data());
}

HdContainerDataSourceHandle _ComputeLightFilterDependencies(const HdContainerDataSourceHandle &lightPrimSource)
{
#if PXR_VERSION >= 2405
    const
#endif
        HdLightSchema ls = HdLightSchema::GetFromParent(lightPrimSource);

    // XXX
    // HdLightSchema is barebones at the moment, so we need to explicitly use
    // the 'filters' token below.
    const HdContainerDataSourceHandle lightDs = ls.GetContainer();
    if (lightDs) {
        HdDataSourceBaseHandle primvarsArnoldShaderDs = lightDs->Get(TfToken("primvars:arnold:shaders"));
        if (HdSampledDataSourceHandle valDs = HdSampledDataSource::Cast(primvarsArnoldShaderDs)) {
            VtValue val = valDs->GetValue(0.0f);
            if (val.IsHolding<std::string>()) {
                return _BuildLightArnoldShaderDependenciesDs(val.UncheckedGet<std::string>());
            }
        }
    }

    return nullptr;
}

// When a material is added or removed, prims that have a material binding to
// that material need to be invalidated. We declare dependencies on nonexistent
// locators __added__ and __removed__ so we only get dirty signals when the
// material prim is added (then we dirty geometry's materialBindings) or when
// the material is removed (HdDependencyForwardingSceneIndex dirties dependants).
HdContainerDataSourceHandle _ComputeMaterialBindingsDependencies(
    const HdContainerDataSourceHandle &inputDs)
{
    const HdMaterialBindingsSchema materialBindings =
        HdMaterialBindingsSchema::GetFromParent(inputDs);
    const HdMaterialBindingSchema binding =
        materialBindings.GetMaterialBinding();
    const HdContainerDataSourceHandle bindingContainer = binding.GetContainer();
    if (!bindingContainer) {
        return nullptr;
    }
    HdDataSourceBaseHandle pathDs = bindingContainer->Get(HdMaterialBindingSchemaTokens->path);
    if (!pathDs) {
        return nullptr;
    }
    HdSampledDataSourceHandle pathSampled = HdSampledDataSource::Cast(pathDs);
    if (!pathSampled) {
        return nullptr;
    }
    const VtValue pathVal = pathSampled->GetValue(0.0f);
    if (!pathVal.IsHolding<SdfPath>()) {
        return nullptr;
    }
    const SdfPath &bindingPath = pathVal.UncheckedGet<SdfPath>();
    if (bindingPath.IsEmpty()) {
        return nullptr;
    }

    static const HdLocatorDataSourceHandle materialBindingsLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdMaterialBindingsSchema::GetDefaultLocator());
    static const HdLocatorDataSourceHandle addedLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdDataSourceLocator(_tokens->added));
    static const HdLocatorDataSourceHandle removedLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdDataSourceLocator(_tokens->removed));
    static const HdLocatorDataSourceHandle atmbDependencyLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdDependenciesSchema::GetDefaultLocator().Append(
                _tokens->arnoldAddedToMaterialBindings));
    static const HdLocatorDataSourceHandle rtmbDependencyLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdDependenciesSchema::GetDefaultLocator().Append(
                _tokens->arnoldRemovedToMaterialBindings));

    TfToken names[4];
    HdDataSourceBaseHandle dataSources[4];
    size_t count = 0;

    // If the bound material's __added__ is dirtied (e.g. material added
    // after geometry), dirty this prim's material bindings.
    names[count] = _tokens->arnoldAddedToMaterialBindings;
    dataSources[count++] =
        HdDependencySchema::Builder()
            .SetDependedOnPrimPath(
                HdRetainedTypedSampledDataSource<SdfPath>::New(bindingPath))
            .SetDependedOnDataSourceLocator(addedLocDs)
            .SetAffectedDataSourceLocator(materialBindingsLocDs)
            .Build();

    // If the bound material's __removed__ is dirtied (material removed or
    // made typeless), dirty this prim's material bindings.
    names[count] = _tokens->arnoldRemovedToMaterialBindings;
    dataSources[count++] =
        HdDependencySchema::Builder()
            .SetDependedOnPrimPath(
                HdRetainedTypedSampledDataSource<SdfPath>::New(bindingPath))
            .SetDependedOnDataSourceLocator(removedLocDs)
            .SetAffectedDataSourceLocator(materialBindingsLocDs)
            .Build();

    // When material bindings change, the above dependencies may need to be
    // reevaluated.
    names[count] = _tokens->arnoldMaterialBindingsToAddedDependency;
    dataSources[count++] =
        HdDependencySchema::Builder()
            .SetDependedOnDataSourceLocator(materialBindingsLocDs)
            .SetAffectedDataSourceLocator(atmbDependencyLocDs)
            .Build();
    names[count] = _tokens->arnoldMaterialBindingsToRemovedDependency;
    dataSources[count++] =
        HdDependencySchema::Builder()
            .SetDependedOnDataSourceLocator(materialBindingsLocDs)
            .SetAffectedDataSourceLocator(rtmbDependencyLocDs)
            .Build();

    return HdRetainedContainerDataSource::New(
        count, names, dataSources);
}

TF_DECLARE_REF_PTRS(_DependenciesSceneIndex);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/// \class _SceneIndex
///
/// The scene index that adds dependencies for volume and light prims.
///
class _DependenciesSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    std::unordered_map<SdfPath, SdfPath, SdfPath::Hash> _lightDep;
    static _DependenciesSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
    {
        return TfCreateRefPtr(new _DependenciesSceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override
    {
        const HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
        const bool hasLightDeps = HdPrimTypeIsLight(prim.primType);
        const bool hasMaterialBindings =
            static_cast<bool>(HdMaterialBindingsSchema::GetFromParent(prim.dataSource));

        if (hasLightDeps || hasMaterialBindings) {
            HdContainerDataSourceHandle depOverlay =
                HdLazyContainerDataSource::New(
                    [prim, hasLightDeps, hasMaterialBindings]()
                    -> HdContainerDataSourceHandle {
                        HdContainerDataSourceHandle light =
                            hasLightDeps ? _ComputeLightFilterDependencies(prim.dataSource) : nullptr;
                        HdContainerDataSourceHandle mat =
                            _ComputeMaterialBindingsDependencies(prim.dataSource);
                        if (!mat) {
                            return light;
                        }
                        if (!light) {
                            return mat;
                        }
                        return HdContainerDataSourceHandle(
                            HdOverlayContainerDataSource::New(light, mat));
                    });
            return {
                prim.primType,
                HdContainerDataSourceEditor(prim.dataSource)
                    .Overlay(
                        HdDependenciesSchema::GetDefaultLocator(),
                        depOverlay)
                    .Finish()};
        }
        return prim;
    }

    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _DependenciesSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex)
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
        SetDisplayName("Arnold: declare prim dependencies");
    }

    void _PrimsAdded(const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries) override
    {
        if (!_IsObserved()) {
            return;
        }

        _SendPrimsAdded(entries);

        // So that HdDependencyForwardingSceneIndex propagates correctly when
        // materials are added/removed: dirty synthetic __removed__ on prims
        // that become typeless, and __added__ on material prims so geometry
        // that was added earlier can refresh its material binding.
        HdSceneIndexObserver::DirtiedPrimEntries dirtied;
        for (const auto &entry : entries) {
            if (entry.primType.IsEmpty()) {
                dirtied.emplace_back(entry.primPath, HdDataSourceLocator(_tokens->removed));
            } else if (entry.primType == HdPrimTypeTokens->material) {
                dirtied.emplace_back(entry.primPath, HdDataSourceLocator(_tokens->added));
            }
        }
        if (!dirtied.empty()) {
            _SendPrimsDirtied(dirtied);
        }
    }

    void _PrimsRemoved(const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries) override
    {
        if (!_IsObserved()) {
            return;
        }
        _SendPrimsRemoved(entries);
    }

    void _PrimsDirtied(const HdSceneIndexBase &sender, const HdSceneIndexObserver::DirtiedPrimEntries &entries) override
    {
        HD_TRACE_FUNCTION();
        if (!_IsObserved()) {
            return;
        }

        _SendPrimsDirtied(entries);
    }
};

} // namespace

HdArnoldDependencySceneIndexPlugin::HdArnoldDependencySceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdArnoldDependencySceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs)
{
    return _DependenciesSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE
#endif // ENABLE_SCENE_INDEX // Hydra 2