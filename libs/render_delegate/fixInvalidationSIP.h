#pragma once

#include "pxr/pxr.h"

#if PXR_VERSION >= 2505

#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdArnoldFixInvalidationSceneIndexPlugin
/// 
/// This scene index filter re-enable the materials that are type erased in the PiPrototypePropagatingSceneIndex.
/// This problem should normally be fixed in 25.08 
///

class HdArnoldFixInvalidationSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HdArnoldFixInvalidationSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif