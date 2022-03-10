// Copyright 2019 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <ai_nodes.h>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/primvar.h>

#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

class UsdArnoldWriterRegistry;

/**
 *  Class handling the translation of Arnold data to USD.
 *  A registry provides the desired primWriter for a given Arnold node entry
 *name.
 **/

class UsdArnoldWriter {
public:
    UsdArnoldWriter()
        : _universe(nullptr),
          _registry(nullptr),
          _writeBuiltin(true),
          _writeMaterialBindings(true),
          _mask(AI_NODE_ALL),
          _shutterStart(0.f),
          _shutterEnd(0.f),
          _allAttributes(false),
          _time(UsdTimeCode::Default())
    {
    }
    ~UsdArnoldWriter() {}

    void Write(const AtUniverse *universe);  // convert a given arnold universe
    void WritePrimitive(const AtNode *node); // write a primitive (node)

    void SetRegistry(UsdArnoldWriterRegistry *registry);

    void SetUsdStage(UsdStageRefPtr stage) { _stage = stage; }
    const UsdStageRefPtr &GetUsdStage() { return _stage; }

    void SetUniverse(const AtUniverse *universe) { _universe = universe; }
    const AtUniverse *GetUniverse() const { return _universe; }

    void SetWriteBuiltin(bool b) { _writeBuiltin = b; }
    bool GetWriteBuiltin() const { return _writeBuiltin; }

    void SetMask(int m) { _mask = m; }
    int GetMask() const { return _mask; }

    float GetShutterStart() const { return _shutterStart; }
    float GetShutterEnd() const { return _shutterEnd; }

    void SetWriteAllAttributes(bool b) {_allAttributes = b;}
    bool GetWriteAllAttributes() const {return _allAttributes;}

    UsdTimeCode GetTime() const { return _time;}
    UsdTimeCode GetTime(float delta) const { return _time.IsDefault() ? UsdTimeCode(delta) : UsdTimeCode(_time.GetValue() + delta);}
    void SetFrame(float frame) {_time = UsdTimeCode(frame);}
    
    bool IsNodeExported(const AtString &name) { return _exportedNodes.count(name) == 1; }

    const std::string &GetScope() const {return _scope;}
    void SetScope(const std::string &scope) {
        _scope = scope;
        if (!_scope.empty()) { 
            // First character needs to be a slash
            if (_scope[0] != '/')
                _scope = std::string("/") + _scope;
            // Last character should *not* be slash, otherwise we could have 
            // double slashes in the nodes names, which can crash usd
            if (_scope.back() == '/')
                _scope = _scope.substr(0, _scope.length() - 1);            
        }
        
    }

    bool GetWriteMaterialBindings() const {return _writeMaterialBindings;}
    void SetWriteMaterialBindings(bool b) {_writeMaterialBindings = b;}

    void CreateHierarchy(const SdfPath &path, bool leaf = true);

    const std::vector<float> &GetAuthoredFrames() const {return _authoredFrames;}

    /** Set a parameter value on a usd attribute. If we're appending data from varying times, 
     *  this function will take care of creating time samples if needed, or just keeping a 
     *  constant value otherwise. A sub-frame can eventually be provided, in case we need to 
     *  set motion time samples for the current frame
    **/
    template <typename T>
    void SetAttribute(const UsdAttribute &attr, const T& value, float *subFrame = nullptr) const
    {
        if (_time.IsDefault()) {
            // no time was provided, we just want to set a constant value, unless we were
            // provided a subframe for motion blurred data
            attr.Set(value, subFrame ? UsdTimeCode(*subFrame) : UsdTimeCode::Default());            
        } else {
            // A specific time was provided, let's check if there were previously authored frames
            if (!_authoredFrames.empty()) {
                // some frames were previously, authored, we need to check if a time sample is
                // required for this attribute or not
                if (!attr.ValueMightBeTimeVarying()) {
                    // so far it just has a constant value. 
                    // We want to check if it's different from the current one
                    VtValue previousVal;
                    if (!attr.Get(&previousVal))
                    {
                        // couldn't get the previous value, just set the current time
                        attr.Set(value, subFrame ? GetTime(*subFrame) : GetTime());
                    } else if (previousVal != value) {
                        // the attribute value has changed since the previously 
                        // authored frame ! We need to make it time-varying now

                        // First, let's clear the default attribute value
                        attr.ClearDefault();

                        // Set the previous constant value as time samples on the surrounding nearest
                        // frames that were previously authored
                        for (auto nearestFrame : _nearestFrames)
                            attr.Set(previousVal, UsdTimeCode(nearestFrame));

                        // finally, set the desired value as a time sample for the current time.
                        attr.Set(value, subFrame ? GetTime(*subFrame) : GetTime());
                    }
                } else {
                    // this attribute is already time-varying, for now let's just write it as a time sample.
                    // TODO : we could optimize the amount of time samples and avoid writing identical values 
                    // if they're unchanged for multiple frames
                    attr.Set(value, subFrame ? GetTime(*subFrame) : GetTime());
                }

            } else {
                // if a time is provided, but we're not in append mode, we want to just set the plain value.
                // Otherwise, all parameters will always have time samples
                attr.Set(value, subFrame ? GetTime(*subFrame) : UsdTimeCode::Default());
            }            
        }
    }

    template <typename T>
    void SetPrimVar(UsdGeomPrimvar &primvar, const T& value, float *subFrame = nullptr) const
    {
        UsdAttribute attr = primvar.GetAttr();
        SetAttribute(attr, value, subFrame);
    }
    template <typename T>
    void SetPrimVarIndices(UsdGeomPrimvar &primvar, const T& value, float *subFrame = nullptr) const
    {
        UsdAttribute attr = primvar.CreateIndicesAttr();
        SetAttribute(attr, value, subFrame);
    }

private:
    const AtUniverse *_universe;        // Arnold universe to be converted
    UsdArnoldWriterRegistry *_registry; // custom registry used for this writer. If null, a global
                                        // registry will be used.
    UsdStageRefPtr _stage;              // USD stage where the primitives are added
    bool _writeBuiltin;                 // do we want to create usd-builtin primitives, or arnold schemas
    bool _writeMaterialBindings;        // do we want to write usd material bindings (otherwise save arnold shader connections)
    int _mask;                          // Mask based on arnold flags (AI_NODE_SHADER, etc...),
                                        // determining what arnold nodes must be saved out
    float _shutterStart;
    float _shutterEnd;
    std::unordered_set<AtString, AtStringHash> _exportedNodes; // list of arnold attributes that were exported
    std::string _scope;                // scope in which the primitives must be written
    bool _allAttributes;               // write all attributes to usd prims, even if they're left to default
    UsdTimeCode _time;                 // current time required by client code
    std::vector<float> _authoredFrames;// list of frames that were previously authored in this usd stage
    std::vector<float> _nearestFrames; // based on the _authoredFrames list, we store the 1 or 2 nearest frames
    std::string _defaultPrim;          // usd files have a defaultPrim that can be used for file references
};
