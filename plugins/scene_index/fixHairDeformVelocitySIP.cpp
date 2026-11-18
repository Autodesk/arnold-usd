//
// SPDX-License-Identifier: Apache-2.0
//
#include "fixHairDeformVelocitySIP.h"

#if defined(ENABLE_SCENE_INDEX) && defined(HOUDINI_BUILD)

#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdArnoldFixHairDeformVelocitySceneIndexPlugin"))((hairDeform, "houdini:hairdeform")));

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldFixHairDeformVelocitySceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // Must run after HD_HairDeform has tagged the prims. HD_HairDeform registers for all renderers
    // at phase 0 with InsertionOrderAtStart, so inserting at the end of phase 0 puts this filter
    // after it. If HD_HairDeform is moved to a later phase, this filter will silently stop doing
    // anything and the phase here must be raised accordingly.
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName, nullptr, insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtEnd);
}

namespace {

TF_DECLARE_REF_PTRS(_FixHairDeformVelocitySceneIndex);

/// Blocks the stale velocities and accelerations primvars of deformed hair.
class _FixHairDeformVelocitySceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    static _FixHairDeformVelocitySceneIndexRefPtr New(const HdSceneIndexBaseRefPtr& inputSceneIndex)
    {
        return TfCreateRefPtr(new _FixHairDeformVelocitySceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath& primPath) const override
    {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
        if (!prim.dataSource || !prim.dataSource->Get(_tokens->hairDeform)) {
            return prim;
        }

        // HD_HairDeform deforms the points and tags the prim with "houdini:hairdeform", but leaves
        // the "velocities" primvar of the source prim as zeros. The delegate would extrapolate
        // motion from those zeros instead of using the animated point samples, so the hair would
        // get no motion blur. Block them so the deformed points drive the motion blur.
        const HdDataSourceLocator primvars = HdPrimvarsSchema::GetDefaultLocator();
        prim.dataSource = HdContainerDataSourceEditor(prim.dataSource)
                              .Set(primvars.Append(HdTokens->velocities), HdBlockDataSource::New())
                              .Set(primvars.Append(HdTokens->accelerations), HdBlockDataSource::New())
                              .Finish();
        return prim;
    }

    SdfPathVector GetChildPrimPaths(const SdfPath& primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _FixHairDeformVelocitySceneIndex(const HdSceneIndexBaseRefPtr& inputSceneIndex)
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
#if PXR_VERSION >= 2308
        SetDisplayName("Arnold: fix HD_HairDeform velocity");
#endif
    }

    void _PrimsAdded(const HdSceneIndexBase&, const HdSceneIndexObserver::AddedPrimEntries& entries) override
    {
        if (_IsObserved()) {
            _SendPrimsAdded(entries);
        }
    }

    void _PrimsRemoved(const HdSceneIndexBase&, const HdSceneIndexObserver::RemovedPrimEntries& entries) override
    {
        if (_IsObserved()) {
            _SendPrimsRemoved(entries);
        }
    }

    void _PrimsDirtied(const HdSceneIndexBase&, const HdSceneIndexObserver::DirtiedPrimEntries& entries) override
    {
        if (!_IsObserved()) {
            return;
        }
        // The tag can be added or removed in a live session, which changes whether the velocities
        // are blocked, so dirty them along with it.
        const HdDataSourceLocator hairDeform(_tokens->hairDeform);
        const HdDataSourceLocator primvars = HdPrimvarsSchema::GetDefaultLocator();
        HdSceneIndexObserver::DirtiedPrimEntries newEntries;
        bool modified = false;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].dirtyLocators.Intersects(hairDeform)) {
                if (modified) {
                    newEntries.push_back(entries[i]);
                }
                continue;
            }
            if (!modified) {
                newEntries.assign(entries.begin(), entries.begin() + i);
                modified = true;
            }
            newEntries.push_back(entries[i]);
            newEntries.back().dirtyLocators.insert(primvars.Append(HdTokens->velocities));
            newEntries.back().dirtyLocators.insert(primvars.Append(HdTokens->accelerations));
        }
        _SendPrimsDirtied(modified ? newEntries : entries);
    }
};

} // namespace

HdArnoldFixHairDeformVelocitySceneIndexPlugin::HdArnoldFixHairDeformVelocitySceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdArnoldFixHairDeformVelocitySceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputScene, const HdContainerDataSourceHandle& inputArgs)
{
    TF_UNUSED(inputArgs);
    return _FixHairDeformVelocitySceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX && HOUDINI_BUILD
