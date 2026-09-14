#pragma once

#include "pxr/pxr.h"

#ifdef ENABLE_SCENE_INDEX

#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdArnoldCoordSysSceneIndexPlugin
///
/// Turns coordinate-system bindings into coordSys prims so the Arnold render
/// delegate receives coordSys sprims under any Hydra host (usdview, husk, the
/// arnold-usd procedural, ...).
///
/// UsdImagingCreateSceneIndices does not add this itself, so without a renderer
/// scene-index plugin the coordSys prims never reach the delegate and named
/// coordinate spaces (e.g. "map_proj.camera") fail to resolve. Registering it
/// per-renderer here means every Hydra host picks it up, not just the
/// procedural.
class HdArnoldCoordSysSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HdArnoldCoordSysSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX
