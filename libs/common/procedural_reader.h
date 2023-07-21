//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

/// @brief This is the base class for any arnold procedural reader
class ProceduralReader {
public:
    virtual ~ProceduralReader() {};
    virtual void SetProceduralParent(AtNode *node) = 0;
    virtual void SetFrame(float frame) = 0;
    virtual void SetDebug(bool b) = 0;
    virtual void SetThreadCount(unsigned int t) = 0;
    virtual void SetId(unsigned int id) = 0;
    virtual void SetMotionBlur(bool motionBlur, float motionStart = 0.f, float motionEnd = 0.f) = 0;
    virtual void SetConvertPrimitives(bool b) = 0;
    virtual void SetPurpose(const std::string &p) = 0;
    virtual void SetMask(int m) = 0;
    virtual void SetUniverse(AtUniverse *universe) = 0;
    virtual void SetRenderSettings(const std::string &renderSettings) = 0;
    virtual void CreateViewportRegistry(AtProcViewportMode mode, const AtParamValueMap* params) = 0;
    virtual void Read(
        const std::string &filename, AtArray *overrides, const std::string &path = "") = 0; // read a USD file
    virtual bool Read(int cacheId, const std::string &path = "") = 0; // read a USdStage from memory
    virtual const std::vector<AtNode *> &GetNodes() const = 0;
};