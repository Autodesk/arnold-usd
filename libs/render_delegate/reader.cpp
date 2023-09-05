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

// check pxr/imaging/hd/testenv/testHdRenderIndex.cpp

class HydraArnoldAPI : public ArnoldAPIAdapter {
public:
    HydraArnoldAPI(AtUniverse *universe) : _universe(universe) {}
    AtNode *CreateArnoldNode(const char *type, const char *name) override {
        return AiNode(_universe, type, name);
    }

    void AddConnection(AtNode *source, const std::string &attr, const std::string &target, 
        ConnectionType type, const std::string &outputElement = std::string()) override {
            //TODO
        }

    // Does the caller really need the primvars ? as hydra should have taken care of it
    const std::vector<UsdGeomPrimvar> &GetPrimvars() const override {return _primvars;}

    void AddNodeName(const std::string &name, AtNode *node) override {
        // TODO
    };
    AtUniverse *_universe;
    std::vector<UsdGeomPrimvar> _primvars;
};


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

void HydraArnoldReader::Read(const std::string &filename, AtArray *overrides,
            const std::string &path )
{
    HydraArnoldAPI context(_universe);
    UsdStageRefPtr stage = UsdStage::Open(filename, UsdStage::LoadAll);
    // TODO check that we were able to load the stage


    // if we have a procedural parent, we want to skip certain kind of prims
    HdArnoldRenderDelegate *arnoldRenderDelegate = static_cast<HdArnoldRenderDelegate*>(_renderDelegate);
    int procMask = (arnoldRenderDelegate->GetProceduralParent()) ?
        (AI_NODE_CAMERA | AI_NODE_LIGHT | AI_NODE_SHAPE | AI_NODE_SHADER | AI_NODE_OPERATOR)
        : AI_NODE_ALL;
        
    arnoldRenderDelegate->SetMask(procMask);

    // Populates the rootPrim in the HdRenderIndex.
    // This creates the arnold nodes, but they don't contain any data
    SdfPathVector _excludedPrimPaths; // excluding nothing
    _imagingDelegate->Populate(stage->GetPrimAtPath(SdfPath::AbsoluteRootPath()), _excludedPrimPaths);

    // Not sure about the meaning of collection geometry -- should that be extended ?
    HdRprimCollection collection(HdTokens->geometry, HdReprSelector(HdReprTokens->hull));
    HdRenderPassSharedPtr syncPass(new HdArnoldSyncPass(_renderIndex, collection));

    // TODO:handle the overrides passed to arnold

    // Find the camera as its motion blur values influence how hydra generates the geometry
    if (!static_cast<HdArnoldRenderDelegate*>(_renderDelegate)->GetProceduralParent()) {
        TimeSettings timeSettings;
        std::string renderSettingsPath;
        ChooseRenderSettings(stage, renderSettingsPath, timeSettings);
        if (!renderSettingsPath.empty()) {
            auto renderSettingsPrim = stage->GetPrimAtPath(SdfPath(renderSettingsPath));
            ReadRenderSettings(renderSettingsPrim, context, timeSettings, _universe);
        }
    }


    //std::cout << timeSettings.motionStart << " " << timeSettings.motionEnd << std::endl;
    //std::cout << timeSettings.motionStart << " " << timeSettings.motionEnd << std::endl;
    AtNode *universeCamera = AiUniverseGetCamera(_universe);
    UsdPrim cameraPrim;
    if (universeCamera) {
        cameraPrim = stage->GetPrimAtPath(SdfPath(AiNodeGetName(universeCamera)));
        //UsdPrim cameraPrim = stage->GetPrimAtPath(SdfPath("/cameras/camera1"));
        // TODO: should check for prim existence
        _imagingDelegate->SetCameraForSampling(cameraPrim.GetPath());

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

    if (!universeCamera && cameraPrim) {
        AtNode *camera =  AiNodeLookUpByName(_universe, AtString(cameraPrim.GetPath().GetText()));
        AiNodeSetPtr(AiUniverseGetOptions(_universe), str::camera, camera);
        // At this point in the process, the shutter is not set , so the SyncAll will not sample correctly ?? NO
        // AiNodeSetFlt(universeCamera, str::shutter_start, -0.25f); // TODO HdCOnfig
        // AiNodeSetFlt(universeCamera, str::shutter_end, 0.25f);
    }

    // AtNode *cameraArnold = AiNodeLookUpByName(_universe, AtString("/cameras/camera1"));
    // if (cameraArnold) {
    //     AiNodeSetPtr(AiUniverseGetOptions(_universe), str::camera, cameraArnold);
    // }
    // AiNodeSetBool(_options, str::enable_progressive_render, false);
    // AiNodeSetBool(AiUniverseGetOptions(_universe), str::ignore_motion_blur, false);
    // AiRenderSetHintBool(AiUniverseGetRenderSession(_universe), str::progressive, false);

    // FORCE the first non null camera
    // Ideally the rendersettings selection should be done by the render delegate or by a function here
    // May be shared code with the procedural ?? 
    // if (!static_cast<HdArnoldRenderDelegate*>(_renderDelegate)->GetProceduralParent()) {
    //     AtNodeIterator *iter = AiUniverseGetNodeIterator(_universe, AI_NODE_CAMERA);
    //         while (!AiNodeIteratorFinished(iter)) {
    //             AtNode * cam = AiNodeIteratorGetNext(iter);
    //             if (strlen(AiNodeGetName(cam))>2) {
    //                 std::cout << "Setting camera to " << AiNodeGetName(cam) << std::endl;
    //                 AiNodeSetPtr(AiUniverseGetOptions(_universe), str::camera, cam);
    //                 //AiNodeSetPtr(nullptr, str::camera, cam);
    //                 //AiNodeSetPtr(AiUniverseGetOptions(_universe), str::subdiv_dicing_camera, cam);
    //                 break;
    //             }
    //         }
    //         AiNodeIteratorDestroy (iter);
    // }

    // AtNode *driver = AiNode(_universe, AtString("driver_tiff"), "driver");
    // auto _fallbackOutputs = AiArrayAllocate(1, 1, AI_TYPE_STRING);
    // auto *_defaultFilter = AiNode(_universe, str::box_filter, "box_filter");

    // // Setting up the fallback outputs when no
    // const auto beautyString =
    //     TfStringPrintf("RGBA RGBA %s %s", AiNodeGetName(_defaultFilter), AiNodeGetName(driver));;
    // AiArraySetStr(_fallbackOutputs, 0, beautyString.c_str());

    // AiNodeSetArray(AiUniverseGetOptions(_universe), str::outputs, AiArrayCopy(_fallbackOutputs));

    // AiNodeSetStr(driver, str::filename, AtString("toto.tif"));
    // std::cout << "AI_RENDER_STATUS_NOT_STARTED " << (AiRenderGetStatus(nullptr) == AI_RENDER_STATUS_NOT_STARTED) << std::endl; 
    // std::cout << "AI_RENDER_STATUS_PAUSED " << (AiRenderGetStatus(nullptr) == AI_RENDER_STATUS_PAUSED) << std::endl; 
    // std::cout << "AI_RENDER_STATUS_RESTARTING " << (AiRenderGetStatus(nullptr) == AI_RENDER_STATUS_RESTARTING) << std::endl; 
    // std::cout << "AI_RENDER_STATUS_RENDERING " << (AiRenderGetStatus(nullptr) == AI_RENDER_STATUS_RENDERING)<< std::endl; 
    // std::cout << "AI_RENDER_STATUS_FINISHED " << (AiRenderGetStatus(nullptr) == AI_RENDER_STATUS_FINISHED) << std::endl; 
    // std::cout << "AI_RENDER_STATUS_FAILED " << (AiRenderGetStatus(nullptr) == AI_RENDER_STATUS_FAILED )<< std::endl; 
    // std::cout << "AI_SESSION_BATCH " << (AiGetSessionMode(nullptr) == AI_SESSION_BATCH)<< std::endl;
    // std::cout << "AI_SESSION_INTERACTIVE " << (AiGetSessionMode(nullptr) == AI_SESSION_INTERACTIVE)<< std::endl;
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
