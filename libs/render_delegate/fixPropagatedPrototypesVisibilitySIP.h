#pragma once

#include "pxr/pxr.h"

#if PXR_VERSION >= 2505

#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdArnoldPropagatedPrototypesVisibilitySceneIndexPlugin
/// 
/// This scene index filter re-propagates the visibility of prototypes that are propagated by the PiPrototypePropagatingSceneIndex.
///

class HdArnoldPropagatedPrototypesVisibilitySceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HdArnoldPropagatedPrototypesVisibilitySceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif