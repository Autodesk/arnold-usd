//
// SPDX-License-Identifier: Apache-2.0
//
#pragma once

#include <pxr/pxr.h>
#if defined(ENABLE_SCENE_INDEX) && defined(MTOA_BUILD)

#include <pxr/imaging/hd/sceneIndexPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdArnoldMtoaSceneIndexPlugin
///
/// MtoA-specific scene index fixups applied before the Arnold render delegate
/// consumes the scene. Currently rewrites primvars whose name starts with
/// "ai" followed by an uppercase letter (as emitted by MtoA, e.g.
/// aiSubdivIterations) into the form the render delegate expects: snake_case
/// with an "arnold:" prefix (e.g. arnold:subdiv_iterations). Additional
/// MtoA-compat fixups can be layered into the same filtering scene index as
/// the need arises.
///
class HdArnoldMtoaSceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdArnoldMtoaSceneIndexPlugin();
    ~HdArnoldMtoaSceneIndexPlugin() override;

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr& inputScene, const HdContainerDataSourceHandle& inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX && MTOA_BUILD
