//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/usdImaging/usdImaging/delegate.h"
#include "pxr/imaging/hd/engine.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h"
#include "pxr/usdImaging/usdImaging/stageSceneIndex.h"
#include "procedural_reader.h"
#include "render_delegate.h"

#ifdef ENABLE_SCENE_INDEX
#define ARNOLD_SCENE_INDEX

#include "pxr/usdImaging/usdImaging/sceneIndices.h"
#include "pxr/imaging/hdsi/legacyDisplayStyleOverrideSceneIndex.h"
#include "pxr/usdImaging/usdImaging/rootOverridesSceneIndex.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hdsi/primTypePruningSceneIndex.h"
#include "pxr/imaging/hd/materialBindingsSchema.h"

#endif
#include "timesettings.h"

TF_DECLARE_REF_PTRS(UsdImagingStageSceneIndex);
TF_DECLARE_REF_PTRS(UsdImagingRootOverridesSceneIndex);
TF_DECLARE_REF_PTRS(HdsiLegacyDisplayStyleOverrideSceneIndex);
TF_DECLARE_REF_PTRS(HdsiPrimTypePruningSceneIndex);



class UsdArnoldProcImagingDelegate;

// This is the interface we need for the procedural reader

class HydraArnoldReader : public ProceduralReader {
public:
    HydraArnoldReader(AtUniverse *universe, AtNode *procParent);
    ~HydraArnoldReader();
    const std::vector<AtNode *> &GetNodes() const override;

    void ReadStage(UsdStageRefPtr stage,
                   const std::string &path) override; // read a specific UsdStage
    
    void SetFrame(float frame) override;
    void SetMotionBlur(bool motionBlur, float motionStart = 0.f, float motionEnd = 0.f) override;
    void SetConvertPrimitives(bool b) override;
    void SetMask(int m) override;
    void SetPurpose(const std::string &p) override;
    void SetId(unsigned int id) override;
    void SetRenderSettings(const std::string &renderSettings) override;
    void Update() override;
    void CreateViewportRegistry(AtProcViewportMode mode, const AtParamValueMap* params) override {}; // Do we need to create a registry with hydra ???

    void WriteDebugScene() const;

    void SetCameraForSampling(UsdStageRefPtr stage, const SdfPath &cameraPath);

private:
    std::string _renderSettings;
    unsigned int _id;
    HdArnoldRenderDelegate* GetArnoldRenderDelegate()const {return static_cast<HdArnoldRenderDelegate*>(_renderDelegate.Get());}
    TfToken _purpose;
    HdRenderIndex* _renderIndex;
    UsdArnoldProcImagingDelegate* _imagingDelegate = nullptr;
    HdEngine _engine;
    HdPluginRenderDelegateUniqueHandle _renderDelegate;
    SdfPath _sceneDelegateId;
    UsdImagingStageSceneIndexRefPtr _stageSceneIndex;
    //UsdImagingSelectionSceneIndexRefPtr _selectionSceneIndex;
    UsdImagingRootOverridesSceneIndexRefPtr _rootOverridesSceneIndex;
    HdSceneIndexBaseRefPtr _sceneIndex;
    HdsiLegacyDisplayStyleOverrideSceneIndexRefPtr _displayStyleSceneIndex;
    HdsiPrimTypePruningSceneIndexRefPtr _materialPruningSceneIndex;
    HdsiPrimTypePruningSceneIndexRefPtr _lightPruningSceneIndex;

    AtUniverse *_universe = nullptr;
    HdRenderPassSharedPtr _syncPass;
    HdRprimCollection _collection;
    GfVec2f _shutter;
    HdTaskSharedPtrVector _tasks;
    HdTaskContext _taskContext;
    std::vector<AtNode*> _nodes;
    std::string _debugScene;
    bool _useSceneIndex = false; 
    TimeSettings _time;
    SdfPath _renderCameraPath;

#ifdef ARNOLD_SCENE_INDEX
    HdSceneIndexBaseRefPtr
    _AppendOverridesSceneIndices(
        const HdSceneIndexBaseRefPtr &inputScene);
#endif

};