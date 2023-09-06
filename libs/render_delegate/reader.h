//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/usdImaging/usdImaging/delegate.h"
#include "pxr/imaging/hd/engine.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h"
#include "procedural_reader.h"

// This is the interface we need for the procedural reader

class HydraArnoldReader : public ProceduralReader {
public:
    HydraArnoldReader(AtUniverse *universe);
    ~HydraArnoldReader();
    const std::vector<AtNode *> &GetNodes() const override;

    void Read(const std::string &filename, AtArray *overrides,
              const std::string &path = "") override; // read a USD file

    // TODO: what should the behavior in case we have a cacheId ?
    bool Read(int cacheId, const std::string &path = "") override {return false;};

    void SetProceduralParent(AtNode *node) override;
    void SetUniverse(AtUniverse *universe) override;
  
    void SetFrame(float frame) override;
    void SetMotionBlur(bool motionBlur, float motionStart = 0.f, float motionEnd = 0.f) override;
    void SetDebug(bool b) override;
    void SetThreadCount(unsigned int t) override;
    void SetConvertPrimitives(bool b) override;
    void SetMask(int m) override;
    void SetPurpose(const std::string &p) override;
    void SetId(unsigned int id) override;
    void SetRenderSettings(const std::string &renderSettings) override;

    void CreateViewportRegistry(AtProcViewportMode mode, const AtParamValueMap* params) override {}; // Do we need to create a registry with hydra ???

    void WriteDebugScene(const std::string &debugScene) const;

private:
    std::string _renderSettings;
    unsigned int _id;

    TfToken _purpose;
    HdRenderIndex* _renderIndex;
    UsdImagingDelegate* _imagingDelegate;
    HdEngine _engine;
    HdRenderDelegate *_renderDelegate;
    AtUniverse *_universe = nullptr;
    bool _universeCreated = false;
};