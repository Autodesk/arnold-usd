#pragma once

#include "pxr/pxr.h"

#if PXR_VERSION >= 2208

#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class 
///
///
class HdArnoldImplicitSurfaceSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HdArnoldImplicitSurfaceSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif