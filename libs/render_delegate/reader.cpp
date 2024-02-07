//
// SPDX-License-Identifier: Apache-2.0
//

#include <iostream>
#include <vector>
#include <ai.h>
#include <constant_strings.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/arch/env.h>
#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/driver.h"
#include "pxr/imaging/hd/engine.h"
#include "pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "pxr/imaging/hd/renderPass.h"
#include "pxr/imaging/hd/rendererPlugin.h"
#include "pxr/imaging/hd/rendererPluginRegistry.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/camera.h"

#include "render_delegate.h"
#include "render_pass.h"

#include "rendersettings_utils.h"
PXR_NAMESPACE_USING_DIRECTIVE
#include "api_adapter.h"
#include "reader.h"

static int s_anonymousOverrideCounter = 0;
static AtMutex s_globalReaderMutex;

// check pxr/imaging/hd/testenv/testHdRenderIndex.cpp
class HdArnoldSyncPass : public HdRenderPass
{
public:
    HdArnoldSyncPass(HdRenderIndex *index,
                              HdRprimCollection const &collection)
        : HdRenderPass(index, collection){}
    virtual ~HdArnoldSyncPass() {}
    void SetCameraPath(const SdfPath &cameraPath) {_cameraPath = cameraPath;} 
    void _Execute(HdRenderPassStateSharedPtr const &renderPassState,
                  TfTokenVector const &renderTags) override {
    }
    SdfPath _cameraPath;
};

class HdArnoldSyncTask final : public HdTask
{
public:
    HdArnoldSyncTask(HdRenderPassSharedPtr const &renderPass)
    : HdTask(SdfPath::EmptyPath())
    , _renderPass(renderPass)
    {
    }

    virtual void Sync(HdSceneDelegate*,
                      HdTaskContext*,
                      HdDirtyBits*) override
    {
        _renderPass->Sync();
    }

    virtual void Prepare(HdTaskContext* ctx,
                         HdRenderIndex* renderIndex) override
    {
    }

    virtual void Execute(HdTaskContext* ctx) override
    {
    }

private:
    HdRenderPassSharedPtr _renderPass;
};

HydraArnoldReader::~HydraArnoldReader() {
    // TODO: If we delete the delegates, the arnold scene is also destroyed and the render fails. Investigate how
    //       to safely delete the delegates without deleting the arnold scene
    // if (_imagingDelegate) {
    //     delete _imagingDelegate;
    //     _imagingDelegate = nullptr;
    // }
    // if (_renderIndex) {
    //     delete _renderIndex;
    //     _renderIndex = nullptr;
    // }
    // if (_renderDelegate) {
    //     delete _renderDelegate;
    // }
}

HydraArnoldReader::HydraArnoldReader(AtUniverse *universe) : _universe(universe), _purpose(UsdGeomTokens->render) {
    _renderDelegate = new HdArnoldRenderDelegate(true, TfToken("kick"), _universe);
    TF_VERIFY(_renderDelegate);
    _renderIndex = HdRenderIndex::New(_renderDelegate, HdDriverVector());
    SdfPath _sceneDelegateId = SdfPath::AbsoluteRootPath();
    _imagingDelegate = new UsdImagingDelegate(_renderIndex, _sceneDelegateId);
}

const std::vector<AtNode *> &HydraArnoldReader::GetNodes() const { return static_cast<HdArnoldRenderDelegate*>(_renderDelegate)->_nodes; }
    
void HydraArnoldReader::ReadStage(UsdStageRefPtr stage,
                                const std::string &path)
{
    HdArnoldRenderDelegate *arnoldRenderDelegate = static_cast<HdArnoldRenderDelegate*>(_renderDelegate);
    if (arnoldRenderDelegate == 0)
        return;

    if (stage == nullptr) {
        AiMsgError("[usd] Unable to create USD stage from %s", _filename.c_str());
        return;
    }
    
    // if we have a procedural parent, we want to skip certain kind of prims
    int procMask = (arnoldRenderDelegate->GetProceduralParent()) ?
        (AI_NODE_CAMERA | AI_NODE_LIGHT | AI_NODE_SHAPE | AI_NODE_SHADER | AI_NODE_OPERATOR)
        : AI_NODE_ALL;
        
    arnoldRenderDelegate->SetMask(procMask);
    AtNode *universeCamera = AiUniverseGetCamera(_universe);
    SdfPath renderCameraPath;

    if (arnoldRenderDelegate->GetProceduralParent() && universeCamera != nullptr) {
        // When we render this through a procedural, there is no camera prim
        // as it is not in the usd file. We need to create a dummy cam in order to provide it
        // with the right shutter settings. Note that if there is no motion blur, we don't 
        // need to do this.
        double shutter_start = AiNodeGetFlt(universeCamera, str::shutter_start);
        double shutter_end = AiNodeGetFlt(universeCamera, str::shutter_end);
        if (std::abs(shutter_start) > AI_EPSILON || std::abs(shutter_end) > AI_EPSILON) {
            renderCameraPath = SdfPath("/tmp/Arnold/proc_camera");
            UsdGeomCamera cam = UsdGeomCamera::Define(stage, renderCameraPath);
            cam.CreateShutterOpenAttr().Set(shutter_start);
            cam.CreateShutterCloseAttr().Set(shutter_end);
        }
    }

    // Populates the rootPrim in the HdRenderIndex.
    // This creates the arnold nodes, but they don't contain any data
    SdfPathVector _excludedPrimPaths; // excluding nothing
    SdfPath rootPath = (path.empty()) ? SdfPath::AbsoluteRootPath() : SdfPath(path.c_str());
    _imagingDelegate->Populate(stage->GetPrimAtPath(rootPath), _excludedPrimPaths);

    // Not sure about the meaning of collection geometry -- should that be extended ?
    HdRprimCollection collection(HdTokens->geometry, HdReprSelector(HdReprTokens->hull));
    HdRenderPassSharedPtr syncPass(new HdArnoldSyncPass(_renderIndex, collection));

    // TODO:handle the overrides passed to arnold

    // Find the camera as its motion blur values influence how hydra generates the geometry
    if (!arnoldRenderDelegate->GetProceduralParent()) {
        TimeSettings timeSettings;
        std::string renderSettingsPath;
        ChooseRenderSettings(stage, renderSettingsPath, timeSettings);
        if (!renderSettingsPath.empty()) {
            auto renderSettingsPrim = stage->GetPrimAtPath(SdfPath(renderSettingsPath));
            ReadRenderSettings(renderSettingsPrim, arnoldRenderDelegate->GetAPIAdapter(), timeSettings, _universe);
        }
    } 

    UsdPrim cameraPrim;
    if (universeCamera) {
        if (!arnoldRenderDelegate->GetProceduralParent()) {
            cameraPrim = stage->GetPrimAtPath(SdfPath(AiNodeGetName(universeCamera)));
            if (cameraPrim)
                renderCameraPath = SdfPath(cameraPrim.GetPath());
        }
        if (!renderCameraPath.IsEmpty())
            _imagingDelegate->SetCameraForSampling(renderCameraPath);
        
    } else {
        // Use the first camera available
        for (const auto &it: stage->Traverse()) {
            if (it.IsA<UsdGeomCamera>()) {
                cameraPrim = it;
                _imagingDelegate->SetCameraForSampling(cameraPrim.GetPath());
            }
        }
    }

    HdTaskSharedPtrVector tasks;
    HdTaskContext taskContext;
    tasks.push_back(std::make_shared<HdArnoldSyncTask>(syncPass));
    // SdfPathVector root;
    // root.push_back(SdfPath("/"));
    // collection.SetRootPaths(root);
    _renderIndex->SyncAll(&tasks, &taskContext);

    arnoldRenderDelegate->ProcessConnections();

    if (!universeCamera && cameraPrim) {
        AtNode *camera =  arnoldRenderDelegate->LookupNode(cameraPrim.GetPath().GetText());
        AiNodeSetPtr(AiUniverseGetOptions(_universe), str::camera, camera);
        // At this point in the process, the shutter is not set , so the SyncAll will not sample correctly
    }

    // The scene might not be up to date, because of light links, etc, that were generated during the first sync.
    // ShouldSkipIteration updates the dirtybits for a resync, this is how it works in our hydra render pass.
    const GfVec2f shutter(AiNodeGetFlt(AiUniverseGetCamera(_universe), str::shutter_start), AiNodeGetFlt(AiUniverseGetCamera(_universe), str::shutter_end));
    while (arnoldRenderDelegate->ShouldSkipIteration(_renderIndex, shutter)) {
        _renderIndex->SyncAll(&tasks, &taskContext);
    }
}

void HydraArnoldReader::SetProceduralParent(AtNode *node) {
    static_cast<HdArnoldRenderDelegate*>(_renderDelegate)->SetProceduralParent(node);
}
void HydraArnoldReader::SetUniverse(AtUniverse *universe) {_universe = universe; }
void HydraArnoldReader::SetFrame(float frame) {    
    if (_imagingDelegate) {
        _imagingDelegate->SetTime(UsdTimeCode(frame));
    }
}
void HydraArnoldReader::SetMotionBlur(bool motionBlur, float motionStart , float motionEnd ) {}
void HydraArnoldReader::SetDebug(bool b) {}
void HydraArnoldReader::SetThreadCount(unsigned int t) {}
void HydraArnoldReader::SetConvertPrimitives(bool b) {}
void HydraArnoldReader::SetMask(int m) {static_cast<HdArnoldRenderDelegate*>(_renderDelegate)->SetMask(m); }
void HydraArnoldReader::SetPurpose(const std::string &p) { _purpose = TfToken(p.c_str()); }
void HydraArnoldReader::SetId(unsigned int id) { _id = id; }
void HydraArnoldReader::SetRenderSettings(const std::string &renderSettings) {_renderSettings = renderSettings;}


void HydraArnoldReader::WriteDebugScene(const std::string &debugScene) const
{
    AiMsgWarning("Saving debug arnold scene as \"%s\"", debugScene.c_str());
    AtParamValueMap* params = AiParamValueMap();
    AiParamValueMapSetBool(params, str::binary, false);
    AiSceneWrite(_universe, AtString(debugScene.c_str()), params);
    AiParamValueMapDestroy(params);
}
