//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

// This is a modified version of hdprman render pass scene index plugin 

#pragma once

#include <pxr/pxr.h>

#ifdef ENABLE_SCENE_INDEX
#include <optional>
#include <pxr/imaging/hd/collectionExpressionEvaluator.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/sceneIndexPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(HdArnoldRenderPassSceneIndex);

/// HdArnoldRenderPassSceneIndex applies the active render pass
/// specified in the HdSceneGlobalsSchema, modifying the scene
/// contents as needed.
///
class HdArnoldRenderPassSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    static HdArnoldRenderPassSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr &inputSceneIndex);

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    HdArnoldRenderPassSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex);
    ~HdArnoldRenderPassSceneIndex();

    void _PrimsAdded(const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries) override;
    void _PrimsRemoved(
        const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries) override;
    void _PrimsDirtied(
        const HdSceneIndexBase &sender, const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;

private:
    // State specified by a render pass.
    // If renderPassPath is the empty path, no render pass is active.
    // Collection evaluators are set sparsely, corresponding to
    // the presence of the collection in the render pass schema.
    struct _RenderPassState {
        SdfPath renderPassPath;

        // Retain the expressions so we can compare old vs. new state.
        SdfPathExpression matteExpr;
        SdfPathExpression renderVisExpr;
        SdfPathExpression cameraVisExpr;
        SdfPathExpression pruneExpr;

        // Evalulators for each pattern expression.
        std::optional<HdCollectionExpressionEvaluator> matteEval;
        std::optional<HdCollectionExpressionEvaluator> renderVisEval;
        std::optional<HdCollectionExpressionEvaluator> cameraVisEval;
        std::optional<HdCollectionExpressionEvaluator> pruneEval;

        bool DoesOverrideMatte(const SdfPath &primPath, HdSceneIndexPrim const &prim) const;
        bool DoesOverrideVis(const SdfPath &primPath, HdSceneIndexPrim const &prim) const;
        bool DoesOverrideCameraVis(const SdfPath &primPath, HdSceneIndexPrim const &prim) const;
        bool DoesPrune(const SdfPath &primPath) const;
    };

    // Pull on the scene globals schema for the active render pass,
    // computing and caching its state in _activeRenderPass.
    void _UpdateActiveRenderPassState(
        HdSceneIndexObserver::AddedPrimEntries *addedEntries, HdSceneIndexObserver::DirtiedPrimEntries *dirtyEntries,
        HdSceneIndexObserver::RemovedPrimEntries *removedEntries);

    // State for the active render pass.
    _RenderPassState _activeRenderPass;
};


/// \class HdArnoldRenderPassSceneIndexPlugin
///
/// Applies the active scene index in HdSceneGlobalsSchema
/// to the scene contents.
///
class HdArnoldRenderPassSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HdArnoldRenderPassSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX
