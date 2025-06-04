
#include "dependenciesSIP.h"
#include "pxr/imaging/hd/containerDataSourceEditor.h"
#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceLocator.h"
#include "pxr/imaging/hd/dataSourceTypeDefs.h"
#include "pxr/imaging/hd/dependenciesSchema.h"
#include "pxr/imaging/hd/dependencySchema.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/lazyContainerDataSource.h"
#include "pxr/imaging/hd/lightSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/mapContainerDataSource.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndex.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/tokens.h"

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens, ((sceneIndexPluginName, "HdArnoldDependencySceneIndexPlugin"))(__dependenciesToFilters));

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

HdContainerDataSourceHandle
_BuildLightArnoldShaderDependenciesDs(const std::string &filterPathStr)
{
    if (filterPathStr.empty()) {
        return nullptr;
    }

    TfTokenVector names;
    std::vector<HdDataSourceBaseHandle> deps;
    const size_t numFilters = 1;

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
    // 
    // const size_t numDeps = 1 /* __dependencies -> filters */ +  numFilters;
    // names.reserve(numDeps);
    // deps.reserve(numDeps);

    static HdLocatorDataSourceHandle filtersLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdPrimvarsSchema::GetDefaultLocator().Append(TfToken("arnold:shaders")));

    static HdLocatorDataSourceHandle dependenciesLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdDependenciesSchema::GetDefaultLocator());

    names.push_back(_tokens->__dependenciesToFilters);
    deps.push_back(
        HdDependencySchema::Builder()
            .SetDependedOnPrimPath(/* self */ nullptr)
            .SetDependedOnDataSourceLocator(filtersLocDs)
            .SetAffectedDataSourceLocator(dependenciesLocDs)
            .Build());

    static HdLocatorDataSourceHandle materialLocatorDs = 
            HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                    HdMaterialSchema::GetDefaultLocator());

    static HdLocatorDataSourceHandle emptyLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdDataSourceLocator::EmptyLocator());

   static HdLocatorDataSourceHandle outputsLoc = 
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdDataSourceLocator(TfToken("outputs"), TfToken("filters"), TfToken("i1")));

    static const HdLocatorDataSourceHandle lightDSL =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdLightSchema::GetDefaultLocator());

    static HdLocatorDataSourceHandle affectedLocatorDs = 
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdPrimvarsSchema::GetDefaultLocator());

    //for (const auto &filterPath : filterPaths) {
        //const std::string filterPathStr = filterPath.GetAsString();
        names.push_back(TfToken(filterPathStr));
        deps.push_back(
            HdDependencySchema::Builder()
            .SetDependedOnPrimPath(
                HdRetainedTypedSampledDataSource<SdfPath>::New(SdfPath(filterPathStr)))
                //.SetDependedOnDataSourceLocator(lightDSL)
                //.SetAffectedDataSourceLocator(lightDSL)
            .SetDependedOnDataSourceLocator(materialLocatorDs)
            //.SetDependedOnDataSourceLocator(outputsLoc)
            //.SetDependedOnDataSourceLocator(filtersLocDs)
            //.SetAffectedDataSourceLocator(dependenciesLocDs) // TODO do we need to specify we want to call DirtyParams ???
            .SetAffectedDataSourceLocator(lightDSL)
            .Build());
   // }
        names.push_back(TfToken("Ondependencies"));
        deps.push_back(
            HdDependencySchema::Builder()
           // .SetDependedOnPrimPath(
           //     HdRetainedTypedSampledDataSource<SdfPath>::New(SdfPath(filterPathStr)))
                //.SetDependedOnDataSourceLocator(lightDSL)
                //.SetAffectedDataSourceLocator(lightDSL)
            .SetDependedOnDataSourceLocator(lightDSL)
            //.SetDependedOnDataSourceLocator(outputsLoc)
            //.SetDependedOnDataSourceLocator(filtersLocDs)
            //.SetAffectedDataSourceLocator(dependenciesLocDs) // TODO do we need to specify we want to call DirtyParams ???
            .SetAffectedDataSourceLocator(dependenciesLocDs)
            .Build());

    return HdRetainedContainerDataSource::New(
        3, names.data(), deps.data());
}


// HdContainerDataSourceHandle
// _BuildLightFilterDependenciesDs(const SdfPathVector &filterPaths)
// {
//     if (filterPaths.empty()) {
//         return nullptr;
//     }

//     TfTokenVector names;
//     std::vector<HdDataSourceBaseHandle> deps;
//     const size_t numFilters = filterPaths.size();

//     // Register a dependency on each filter targeted by the light such that
//     // the invalidation of *any* locator on the filter invalidates the 'light'
//     // locator of the light prim.
//     // This matches the legacy dependency declaration in HdPrman_Light using
//     // HdChangeTracker::{Add,Remove}SprimSprimDependency.
//     // Note that this is conservative in a catch-all sense and we could instead
//     // register individual dependency entries for collection, visibility, light
//     // and material locators.
//     //
//     // Additionally, declare that the dependencies depends on the targeted 
//     // filters.
//     // 
//     const size_t numDeps = 1 /* __dependencies -> filters */ +  numFilters;
//     names.reserve(numDeps);
//     deps.reserve(numDeps);

//     static HdLocatorDataSourceHandle filtersLocDs =
//         HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
//             HdLightSchema::GetDefaultLocator().Append(HdTokens->filters));

//     static HdLocatorDataSourceHandle dependenciesLocDs =
//         HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
//             HdDependenciesSchema::GetDefaultLocator());

//     names.push_back(_tokens->__dependenciesToFilters);
//     deps.push_back(
//         HdDependencySchema::Builder()
//             .SetDependedOnPrimPath(/* self */ nullptr)
//             .SetDependedOnDataSourceLocator(filtersLocDs)
//             .SetAffectedDataSourceLocator(dependenciesLocDs)
//             .Build());

//     static HdLocatorDataSourceHandle emptyLocDs =
//         HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
//             HdDataSourceLocator::EmptyLocator());

//     static HdLocatorDataSourceHandle affectedLocatorDs = 
//         HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
//             HdLightSchema::GetDefaultLocator());

//     for (const auto &filterPath : filterPaths) {
//         const std::string filterPathStr = filterPath.GetAsString();
//         names.push_back(TfToken(filterPathStr));
//         deps.push_back(
//             HdDependencySchema::Builder()
//             .SetDependedOnPrimPath(
//                 HdRetainedTypedSampledDataSource<SdfPath>::New(filterPath))
//             .SetDependedOnDataSourceLocator(emptyLocDs)
//             .SetAffectedDataSourceLocator(affectedLocatorDs)
//             .Build());
//     }

//     return HdRetainedContainerDataSource::New(
//         numDeps, names.data(), deps.data());
// }

HdContainerDataSourceHandle
_ComputeLightFilterDependencies(
    const HdContainerDataSourceHandle &lightPrimSource)
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
        if (HdSampledDataSourceHandle valDs =
                HdSampledDataSource::Cast(primvarsArnoldShaderDs)) {

            VtValue val = valDs->GetValue(0.0f);
            if (val.IsHolding<std::string>()) {
                return _BuildLightArnoldShaderDependenciesDs(
                    val.UncheckedGet<std::string>());
            }
        }
        // HdDataSourceBaseHandle filtersDs = lightDs->Get(HdTokens->filters);
        // if (HdSampledDataSourceHandle valDs =
        //         HdSampledDataSource::Cast(filtersDs)) {

        //     VtValue val = valDs->GetValue(0.0f);
        //     if (val.IsHolding<SdfPathVector>()) {
        //         return _BuildLightFilterDependenciesDs(
        //             val.UncheckedGet<SdfPathVector>());
        //     }
        // }
    }

    return nullptr;
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
        // if (prim.primType == HdPrimTypeTokens->volume) {
        //     return
        //         { prim.primType,
        //           HdContainerDataSourceEditor(prim.dataSource)
        //               .Overlay(
        //                   HdDependenciesSchema::GetDefaultLocator(),
        //                   HdLazyContainerDataSource::New(
        //                       std::bind(_ComputeVolumeFieldBindingDependencies,
        //                                 primPath, prim.dataSource)))
        //               .Finish() };
        // }

        if (HdPrimTypeIsLight(prim.primType)) {
            return {
                prim.primType,
                HdContainerDataSourceEditor(prim.dataSource)
                    .Overlay(
                        HdDependenciesSchema::GetDefaultLocator(),
                        HdLazyContainerDataSource::New(std::bind(_ComputeLightFilterDependencies, prim.dataSource)))
                    .Finish()};
        }
        return prim;
    }

    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _DependenciesSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex) : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
#if PXR_VERSION >= 2308
        SetDisplayName("Declare arnold dependencies");
#endif
    }

    void _PrimsAdded(const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries) override
    {
        //std::cout << "Prims ADDED" << std::endl;
        if (!_IsObserved()) {
            return;
        }
        // Debug
        size_t i=0;
        // while (i < entries.size()) {
        //     const HdSceneIndexObserver::AddedPrimEntry &entry = entries[i];
        //     std::cout << "Added " << entry.primPath.GetString() << " " << entry.primType.GetString() << std::endl;
        //     i++;
        // }

        // Check if prims added are light and have dependencies, keep the dependencies
        _SendPrimsAdded(entries);
    }

    void _PrimsRemoved(const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries) override
    {
        //std::cout << "Prims Removed" << std::endl;
        if (!_IsObserved()) {
            return;
        }
                size_t i=0;
        // while (i < entries.size()) {
        //     const HdSceneIndexObserver::RemovedPrimEntry &entry = entries[i];
        //     std::cout << "Removed " << entry.primPath.GetString() << std::endl;
        //     i++;
        // }

        _SendPrimsRemoved(entries);
    }

    void _PrimsDirtied(const HdSceneIndexBase &sender, const HdSceneIndexObserver::DirtiedPrimEntries &entries) override
    {
        HD_TRACE_FUNCTION();
        //std::cout << "Prims Dirtied" << std::endl;
        size_t i=0;
        // while (i < entries.size()) {
        //     const HdSceneIndexObserver::DirtiedPrimEntry &entry = entries[i];
        //     std::cout << "Dirtied " << entry.primPath.GetString();
        //     for (const auto &loc:entry.dirtyLocators){
        //         std::cout << " " << loc;
        //     }
            
        //     std::cout << std::endl;
        //     i++;
        // }

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
