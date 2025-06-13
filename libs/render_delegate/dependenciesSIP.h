#pragma once
//
// This scene index declares the dependencies needed in the arnold usd nodes to correctly invalidates the prims
//
#if PXR_VERSION >= 2505 // Hydra 2
#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"

#include "pxr/pxr.h"
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldDependencySceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdArnoldDependencySceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs) override;

};

PXR_NAMESPACE_CLOSE_SCOPE
#endif // PXR_VERSION >= 2505 // Hydra 2