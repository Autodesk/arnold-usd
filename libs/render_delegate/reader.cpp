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

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((hydraProcCamera, "/ArnoldHydraProceduralCamera"))
);

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

// Subclass the UsdImagingDelegate that we use in our hydra reader,
// so that we can pass it the desired shutter values 
// (which can come from an arnold render camera that is *not* in USD)
class UsdArnoldProcImagingDelegate final : public UsdImagingDelegate
{
public:
    UsdArnoldProcImagingDelegate(HdRenderIndex *parentIndex,
                                SdfPath const& delegateID) : 
                                UsdImagingDelegate(parentIndex, delegateID)
    {
        // We must force "draw modes" to be disabled
        SetUsdDrawModesEnabled(false);
        // Tell the usdImagingDelegate parent class that there 
        // is a camera for sampling. This camera doesn't actually exist,
        // but this field is only used in GetCurrentTimeSamplingInterval
        // in order to get the camera shutter start / end.
        SdfPath fakeCameraPath(_tokens->hydraProcCamera);
        SetCameraForSampling(fakeCameraPath);
    }

    // Set the shutter values, that can possibly come from an arnold camera
    // that doesn't exist in the UsdStage
    void SetShutter(double start, double end)
    {
        _shutterStart = start;
        _shutterEnd = end;
    }
    virtual VtValue GetCameraParamValue(SdfPath const &id, 
                                     TfToken const &paramName) override
    {
        // Override the function GetCameraParamValue, only for the use case
        // where we ask for the shutter range of the "fake" render camera
        if (id.GetToken() == _tokens->hydraProcCamera) {
            // If the requested value is shutter Open / Close, then
            // return the expected value as a VtValue
            if (paramName == HdCameraTokens->shutterOpen)
                return VtValue(_shutterStart);
            
            if (paramName == HdCameraTokens->shutterClose)
                return VtValue(_shutterEnd);
            
            // If we're asking for any other attribute of this fake camera,
            // let's return an empty value
            return VtValue();
        }
        // Fallback to the original function if this isn't the fake camera
        return UsdImagingDelegate::GetCameraParamValue(id, paramName);
    }
private:
    double _shutterStart = 0.;
    double _shutterEnd = 0.;
};

HydraArnoldReader::~HydraArnoldReader() 
{
    // Warn the render delegate that we're deleting it because the reader is being destroyed.
    // At this stage we don't want any AtNode to be deleted, the nodes ownership is now in the Arnold side
    // and here we're just clearing the usd stage. So we tell the render delegate that nodes
    // destruction should be skipped
    if (_renderDelegate)
        static_cast<HdArnoldRenderDelegate*>(_renderDelegate)->EnableNodesDestruction(false);
    if (_imagingDelegate)
        delete _imagingDelegate;

    if (_renderIndex)
        delete _renderIndex;
    
    if (_renderDelegate)    
        delete _renderDelegate;
}

HydraArnoldReader::HydraArnoldReader(AtUniverse *universe, AtNode *procParent) : 
        ProceduralReader(), 
        _purpose(UsdGeomTokens->render), 
        _universe(universe) 
{
    _renderDelegate = new HdArnoldRenderDelegate(true, TfToken("kick"), _universe, AI_SESSION_INTERACTIVE, procParent);
    TF_VERIFY(_renderDelegate);
    _renderIndex = HdRenderIndex::New(_renderDelegate, HdDriverVector());
    SdfPath _sceneDelegateId = SdfPath::AbsoluteRootPath();
    _imagingDelegate = new UsdArnoldProcImagingDelegate(_renderIndex, _sceneDelegateId);

    const char *debugScene = std::getenv("HDARNOLD_DEBUG_SCENE");
    if (debugScene)
        _debugScene = std::string(debugScene);
}

const std::vector<AtNode *> &HydraArnoldReader::GetNodes() const {
    return _renderDelegate ? static_cast<HdArnoldRenderDelegate*>(_renderDelegate)->_nodes : _nodes; }
    
void HydraArnoldReader::ReadStage(UsdStageRefPtr stage,
                                const std::string &path)
{
    HdArnoldRenderDelegate *arnoldRenderDelegate = static_cast<HdArnoldRenderDelegate*>(_renderDelegate);
    if (arnoldRenderDelegate == 0)
        return;
    AiProfileBlock("hydra_proc:read_stage"); 
    if (stage == nullptr) {
        AiMsgError("[usd] Unable to create USD stage from %s", _filename.c_str());
        return;
    }
    
    // if we have a procedural parent, we want to skip certain kind of prims
    int procMask = (arnoldRenderDelegate->GetProceduralParent()) ?
        (AI_NODE_CAMERA | AI_NODE_LIGHT | AI_NODE_SHAPE | AI_NODE_SHADER | AI_NODE_OPERATOR)
        : AI_NODE_ALL;
        
    arnoldRenderDelegate->SetMask(procMask);
    if (arnoldRenderDelegate->GetProceduralParent())
        arnoldRenderDelegate->SetNodeId(_id);

    AtNode *universeCamera = AiUniverseGetCamera(_universe);
    SdfPath renderCameraPath;
    
    // Find the camera as its motion blur values influence how hydra generates the geometry
    if (!arnoldRenderDelegate->GetProceduralParent()) {
        if (universeCamera) {
            UsdPrim cameraPrim = stage->GetPrimAtPath(SdfPath(AiNodeGetName(universeCamera)));
            if (cameraPrim)
                renderCameraPath = SdfPath(cameraPrim.GetPath());
        }

        TimeSettings timeSettings;
        ChooseRenderSettings(stage, _renderSettings, timeSettings);
        if (!_renderSettings.empty()) {
            UsdPrim renderSettingsPrim = stage->GetPrimAtPath(SdfPath(_renderSettings));
            ReadRenderSettings(renderSettingsPrim, arnoldRenderDelegate->GetAPIAdapter(), timeSettings, _universe, renderCameraPath);
        }
    } 

    if (arnoldRenderDelegate->GetProceduralParent() && universeCamera != nullptr) {
        // When we render this through a procedural, there is no camera prim
        // as it is not in the usd file. We need to pass the render camera's shutter
        // range to our custom imaging delegate
        double shutter_start = AiNodeGetFlt(universeCamera, str::shutter_start);
        double shutter_end = AiNodeGetFlt(universeCamera, str::shutter_end);
        _imagingDelegate->SetShutter(shutter_start, shutter_end);
    } else {
        if (!renderCameraPath.IsEmpty()) {
        _imagingDelegate->SetCameraForSampling(renderCameraPath);
        } else {
            // Use the first camera available
            for (const auto &it: stage->Traverse()) {
                if (it.IsA<UsdGeomCamera>()) {
                    UsdPrim cameraPrim = it;
                    _imagingDelegate->SetCameraForSampling(cameraPrim.GetPath());
                    break;
                }
            }
        }
    }

    // Populates the rootPrim in the HdRenderIndex.
    // This creates the arnold nodes, but they don't contain any data
    SdfPathVector _excludedPrimPaths; // excluding nothing
    SdfPath rootPath = (path.empty()) ? SdfPath::AbsoluteRootPath() : SdfPath(path.c_str());

    UsdPrim rootPrim = stage->GetPrimAtPath(rootPath);
    _imagingDelegate->Populate(rootPrim, _excludedPrimPaths);
    if (!path.empty()) {
        UsdGeomXformCache xformCache(_imagingDelegate->GetTime());
        _imagingDelegate->SetRootTransform(xformCache.GetLocalToWorldTransform(rootPrim));
    }
    // This will return a "hidden" render tag if a primitive is of a disabled type
    _imagingDelegate->SetDisplayRender(_purpose == UsdGeomTokens->render);
    _imagingDelegate->SetDisplayProxy(_purpose == UsdGeomTokens->proxy);
    _imagingDelegate->SetDisplayGuides(_purpose == UsdGeomTokens->guide);

    // Not sure about the meaning of collection geometry -- should that be extended ?
    _collection = HdRprimCollection (HdTokens->geometry, HdReprSelector(HdReprTokens->hull));
    _syncPass = HdRenderPassSharedPtr(new HdArnoldSyncPass(_renderIndex, _collection));

    GfInterval timeInterval = _imagingDelegate->GetCurrentTimeSamplingInterval();
    UsdTimeCode time = _imagingDelegate->GetTime();
    _shutter = GfVec2f(timeInterval.GetMin(),timeInterval.GetMax());
    if (!time.IsDefault()) {
        _shutter -= GfVec2f(time.GetValue());
    }
    // Update the shutter so that SyncAll translates nodes with the correct shutter #1994
    static_cast<HdArnoldRenderParam*>(arnoldRenderDelegate->GetRenderParam())->UpdateShutter(_shutter);
    if (_tasks.empty())
        _tasks.push_back(std::make_shared<HdArnoldSyncTask>(_syncPass));
    // SdfPathVector root;
    // root.push_back(SdfPath("/"));
    // collection.SetRootPaths(root);
    _renderIndex->SyncAll(&_tasks, &_taskContext);
    arnoldRenderDelegate->ProcessConnections();
    
    // We want to render the purpose that this reader was assigned to.
    // We must also support the purpose "default". Also, when no
    // purpose is set in the usd file, it seems to shows as "geometry", so we need to support that too
    TfTokenVector purpose;
    purpose.push_back(UsdGeomTokens->default_);
    purpose.push_back(_purpose);
    purpose.push_back(HdTokens->geometry);
    arnoldRenderDelegate->SetRenderTags(purpose);

    // The scene might not be up to date, because of light links, etc, that were generated during the first sync.
    // HasPendingChanges updates the dirtybits for a resync, this is how it works in our hydra render pass.
    while (arnoldRenderDelegate->HasPendingChanges(_renderIndex, _shutter)) {
        _renderIndex->SyncAll(&_tasks, &_taskContext);
        arnoldRenderDelegate->ProcessConnections();
    }

#ifndef ENABLE_SHARED_ARRAYS
    // If we're not doing an interactive render, we want to destroy the render delegate in order to release
    // the usd stage.
    // However, if shared arrays are enabled, we shouldn't destroy anything until the render finishes
    if (!_interactive) {
        // At this stage we don't want any AtNode to be deleted, the nodes ownership is now in the Arnold side
        // and here we're just clearing the usd stage. So we tell the render delegate that nodes
        // destruction should be skipped
        arnoldRenderDelegate->EnableNodesDestruction(false);
        delete _imagingDelegate;
        _imagingDelegate = nullptr;
        delete _renderIndex;
        _renderIndex = nullptr;
        // Copy the render delegate list of nodes to the reader
        // so that it can be passed through procedural_get_nodes
        std::swap(_nodes, arnoldRenderDelegate->_nodes);
        
        delete _renderDelegate;
        _renderDelegate = nullptr;
    }
#endif

    if (!_debugScene.empty())
        WriteDebugScene();
}
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

void HydraArnoldReader::Update()
{
    HdArnoldRenderDelegate *arnoldRenderDelegate = static_cast<HdArnoldRenderDelegate*>(_renderDelegate);
    _imagingDelegate->ApplyPendingUpdates();
    arnoldRenderDelegate->HasPendingChanges(_renderIndex, _shutter);
    _renderIndex->SyncAll(&_tasks, &_taskContext);
    // Connections may have been made as part of the sync pass, so we need to process them
    // again to make sure that the nodes are up to date. (#2269)
    arnoldRenderDelegate->ProcessConnections();
}

void HydraArnoldReader::WriteDebugScene() const
{
    if (_debugScene.empty())
        return;
    
    AiMsgWarning("Saving debug arnold scene as \"%s\"", _debugScene.c_str());
    AtParamValueMap* params = AiParamValueMap();
    AiParamValueMapSetBool(params, str::binary, false);
    AiSceneWrite(_universe, AtString(_debugScene.c_str()), params);
    AiParamValueMapDestroy(params);
}
