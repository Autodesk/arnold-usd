#include "propagateDirtyPrimsSIP.h"
#ifdef ENABLE_SCENE_INDEX
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include "constant_strings.h"

PXR_NAMESPACE_OPEN_SCOPE

PropagateDirtyPrimsSceneIndexRefPtr PropagateDirtyPrimsSceneIndex::New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
{
    return TfCreateRefPtr(new PropagateDirtyPrimsSceneIndex(inputSceneIndex));
}

HdSceneIndexPrim PropagateDirtyPrimsSceneIndex::GetPrim(const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetPrim(primPath);
}

SdfPathVector PropagateDirtyPrimsSceneIndex::GetChildPrimPaths(const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void PropagateDirtyPrimsSceneIndex::DirtyPrims(const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    _SendPrimsDirtied(entries);
}

PropagateDirtyPrimsSceneIndex::PropagateDirtyPrimsSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
#if PXR_VERSION >= 2308
    SetDisplayName("Arnold: propagate dirty prims");
#endif
}

void PropagateDirtyPrimsSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }
    // Check if prims added are light and have dependencies, keep the dependencies
    _SendPrimsAdded(entries);
}

void PropagateDirtyPrimsSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }
    _SendPrimsRemoved(entries);
}

void PropagateDirtyPrimsSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender, const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }
    _SendPrimsDirtied(entries);
}

void PropagateDirtyPrimsSceneIndex::_SystemMessage(const TfToken &messageType, const HdDataSourceBaseHandle &args)
{
    if (messageType == str::t_ArnoldMarkPrimsDirty) {
        // The type is defined in render_delegate.cpp, ideally we would like a more memory friendly type
        auto handle =
            HdRetainedTypedSampledDataSource<TfHashMap<SdfPath, HdDataSourceLocatorSet, SdfPath::Hash>>::Cast(args);
        if (handle) {
            TfHashMap<SdfPath, HdDataSourceLocatorSet, SdfPath::Hash> passedEntries =
                handle->GetTypedValue(0); // Is this a copy ? Oo
            HdSceneIndexObserver::DirtiedPrimEntries dirtyEntries;
            for (const auto &[id, locators] : passedEntries) {
                dirtyEntries.emplace_back(id, locators);
            }
            _SendPrimsDirtied(dirtyEntries);
        }
    }
}

HdArnoldPropagateDirtyPrimsSceneIndexPlugin::HdArnoldPropagateDirtyPrimsSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdArnoldPropagateDirtyPrimsSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs)
{
    return PropagateDirtyPrimsSceneIndex::New(inputScene);
}

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldPropagateDirtyPrimsSceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", TfToken("HdArnoldPropagateDirtyPrimsSceneIndexPlugin"), nullptr, insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

PXR_NAMESPACE_CLOSE_SCOPE
#endif // ENABLE_SCENE_INDEX