

#include "fixInvalidationSIP.h"

#if PXR_VERSION >= 2505
#include <pxr/base/tf/envSetting.h>
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/lazyContainerDataSource.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usdImaging/usdImaging/usdPrimInfoSchema.h>
#include <iostream>
#include "constant_strings.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((sceneIndexPluginName, "HdArnoldFixInvalidationSceneIndexPlugin")));

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldFixInvalidationSceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName, nullptr, insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

namespace {
TF_DECLARE_REF_PTRS(_FixInvalidationSceneIndex);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/// \class _SceneIndex
///
///
///
class _FixInvalidationSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    static _FixInvalidationSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
    {
        return TfCreateRefPtr(new _FixInvalidationSceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override
    {
        return _GetInputSceneIndex()->GetPrim(primPath);
    }

    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _FixInvalidationSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex)
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
#if PXR_VERSION >= 2308
        SetDisplayName("Arnold: fix invalidation for custom types");
#endif
    }

    void _PrimsAdded(const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries) override
    {
        if (!_IsObserved()) {
            return;
        }
        std::cout << "prims added from " << sender.GetDisplayName() << std::endl;
        // Check if prims added are light and have dependencies, keep the dependencies
        _SendPrimsAdded(entries);
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
        if (!_IsObserved()) {
            return;
        }

        for (const auto &ent:entries) {
            std::cout << "Dirtied " << ent.primPath.GetString() << " " << ent.dirtyLocators << std::endl;
        }

        //  Here we would ideally want to MarkRprimDirty the custom arnold prims when an arnold attribute has been modified
        // Unfortunately there is no way to retrieve the ChangeTracker/RenderIndex, etc here 
        // So as a workaround we remap arnold::attributes to primvars/arnold::attributes which we know will trigger a resync.

        // First check if any of the entry locator starts is "arnold::attribute". 
        // We could also check the prim type belongs to Arnold if this too slow, or keep a cache of our custom prims in the scene (using _PrimsAdded/_PrimsRemoved)
        static HdDataSourceLocator arnoldAttributesLocator(str::t_arnold__attributes);
        if (std::any_of(entries.cbegin(), entries.cend(), [](const auto &entry) {
                return std::count(entry.dirtyLocators.begin(), entry.dirtyLocators.end(), arnoldAttributesLocator);
            })) {
            HdSceneIndexObserver::DirtiedPrimEntries _entries;
            for (const auto &entry : entries) {
                HdDataSourceLocatorSet locators;
                for (const auto &locator : entry.dirtyLocators) {
                    if (locator == arnoldAttributesLocator) {
                        // We remap the arnold::attributes to primvars/arnold::attributes
                        locators.insert(locator.Prepend(HdPrimvarsSchema::GetDefaultLocator()));
                    } else {
                        locators.insert(locator);
                    }
                }
                std::cout << entry.primPath.GetString() << std::endl;
                std::cout << locators << std::endl;
                _entries.emplace_back(entry.primPath, locators);
            }
            _SendPrimsDirtied(_entries);
        } else {
            _SendPrimsDirtied(entries);
        }
    }
};

} // namespace

HdArnoldFixInvalidationSceneIndexPlugin::HdArnoldFixInvalidationSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdArnoldFixInvalidationSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs)
{
    return _FixInvalidationSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_VERSION >= 2505
