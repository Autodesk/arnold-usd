
#include <pxr/pxr.h>

#ifdef ENABLE_SCENE_INDEX // Hydra2

#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldExtComputationPrimvarPruningSceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdArnoldExtComputationPrimvarPruningSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX
