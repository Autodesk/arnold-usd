//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once
#include <ai.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE
/// @brief This is the base class for any arnold procedural reader
class ProceduralReader {
public:
    ProceduralReader() {};
    virtual ~ProceduralReader() {};
    virtual void SetFrame(float frame) = 0;
    virtual void SetDebug(bool b) = 0;
    virtual void SetThreadCount(unsigned int t) = 0;
    virtual void SetId(unsigned int id) = 0;
    virtual void SetMotionBlur(bool motionBlur, float motionStart = 0.f, float motionEnd = 0.f) = 0;
    virtual void SetConvertPrimitives(bool b) = 0;
    virtual void SetPurpose(const std::string &p) = 0;
    virtual void SetMask(int m) = 0;
    virtual void SetRenderSettings(const std::string &renderSettings) = 0;
    virtual void CreateViewportRegistry(AtProcViewportMode mode, const AtParamValueMap* params) = 0;
    virtual void ReadStage(UsdStageRefPtr stage,
                   const std::string &path) = 0;
    
    virtual const std::vector<AtNode *> &GetNodes() const = 0;

    const std::string &GetFilename() const { return _filename; }
    const AtArray *GetOverrides() const { return _overrides; }
    long int GetCacheId() const {return _cacheId;}
    void SetInteractive(bool b) {_interactive = b;}
    bool GetInteractive() const {return _interactive;}

    void SetCommandLine(const std::string& cmd) {_commandLine = cmd;}
    const std::string &GetCommandLine() const {return _commandLine;}

    void Read(const std::string &filename, 
        AtArray *overrides, const std::string &path = "");

    bool Read(long int cacheId, const std::string &path = "");
    virtual void Update() {} // Update scene for interactive changes
protected:
    std::string _filename;
    AtArray *_overrides = nullptr;
    long int _cacheId = 0;   // usdStage cacheID used with a StageCache
    bool _interactive = false; // interactive readers can update Arnold when the usdStage changes
    std::string _commandLine; // the eventual command line used to render this file (e.g. kick)
};