//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2022 Autodesk, Inc.
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

#include <ai.h>

#include <pxr/base/work/dispatcher.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/notice.h>
#include <pxr/usd/usdSkel/binding.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/cache.h>

#include <string>
#include <iostream>
#include <unordered_map>
#include <vector>
#include "utils.h"
#include "read_skinning.h"
#include "timesettings.h"
#include "api_adapter.h"
#include "procedural_reader.h"

PXR_NAMESPACE_USING_DIRECTIVE

class UsdArnoldReaderRegistry;

/**
 *  Class handling the translation of USD data to Arnold
 **/

class UsdArnoldReader : public ProceduralReader {
public:
    UsdArnoldReader();
    ~UsdArnoldReader();

    void ReadStage(UsdStageRefPtr stage,
                   const std::string &path) override; // read a specific UsdStage
    void ReadPrimitive(const UsdPrim &prim, UsdArnoldReaderContext &context, bool isInstance = false, AtArray *parentMatrix = nullptr);

    void ClearNodes();
    AtNode *CreateNestedProc(const char *objectPath, UsdArnoldReaderContext &context);
    void InitCacheId();

    void Update() override;

    void SetProceduralParent(AtNode *node) override;
    void SetUniverse(AtUniverse *universe) override;

    void CreateViewportRegistry(AtProcViewportMode mode, const AtParamValueMap* params) override;
    void SetFrame(float frame) override;
    void SetMotionBlur(bool motionBlur, float motionStart = 0.f, float motionEnd = 0.f) override;
    void SetDebug(bool b) override;
    void SetThreadCount(unsigned int t) override;
    void SetConvertPrimitives(bool b) override;
    void SetMask(int m) override { _mask = m; }
    void SetPurpose(const std::string &p) override { _purpose = TfToken(p.c_str()); }
    void SetId(unsigned int id) override { _id = id; }
    void SetRenderSettings(const std::string &renderSettings) override {_renderSettings = renderSettings;}

    const UsdStageRefPtr &GetStage() const { return _stage; }
    const std::vector<AtNode *> &GetNodes() const override { return _nodes; }
    float GetFrame() const { return _time.frame; }
    UsdArnoldReaderRegistry *GetRegistry() { return _readerRegistry; }
    AtUniverse *GetUniverse() { return _universe; }
    const AtNode *GetProceduralParent() const { return _procParent; }
    bool GetDebug() const { return _debug; }
    bool GetConvertPrimitives() const { return _convert; }
    const TimeSettings &GetTimeSettings() const { return _time; }
        
    unsigned int GetThreadCount() const { return _threadCount; }
    int GetMask() const { return _mask; }
    unsigned int GetId() const { return _id;}
    const TfToken &GetPurpose() const {return _purpose;}
    const std::string &GetRenderSettings() const {return _renderSettings;}

    bool IsUpdating() const {return _updating;}

    static unsigned int ReaderThread(void *data);
    static unsigned int ProcessConnectionsThread(void *data);

    void TraverseStage(UsdPrim *rootPrim, UsdArnoldReaderContext &context, 
                                    int threadId, int threadCount,
                                    bool doPointInstancer, bool doSkelData, AtArray *matrix);


    bool HasRootPrim() const {return _hasRootPrim;}
    const UsdPrim &GetRootPrim() const {return _rootPrim;}

    AtNode *GetDefaultShader();
    AtNode *LookupNode(const char *name, bool checkParent = true)
    {
        auto it = _nodeNames.find(std::string(name));
        if (it != _nodeNames.end()) {
            return it->second;
        }

        AtNode *node = AiNodeLookUpByName(_universe, AtString(name), _procParent);
        // We don't want to take into account nodes that were created by a parent procedural
        // (see #172). It happens that calling AiNodeGetParent on a child node that was just
        // created by this procedural returns nullptr. I guess we'll get a correct result only
        // after the procedural initialization is finished. The best test we can do now is to
        // ignore the node returned by AiNodeLookupByName if it has a non-null parent that
        // is different from the current procedural parent
        if (checkParent && node) {
            AtNode *parent = AiNodeGetParent(node);
            if (parent != nullptr && parent != _procParent)
                node = nullptr;
        }
        return node;
    }

    // We only lock if we're in multithread, otherwise
    // we want to avoid this cost
    void LockReader()
    {
        // for _threadCount = 0, or > 1 we want to lock
        // for this reader
        if (_threadCount != 1)
            _readerLock.lock();
    }
    void UnlockReader()
    {
        if (_threadCount != 1)
            _readerLock.unlock();
    }

    // Reading a stage in multithread implies to go
    // through different steps, in order to handle the
    // connections between nodes. This enum will tell us
    // at which step we are during the whole process
    enum ReadStep {
        READ_NOT_STARTED = 0,
        READ_TRAVERSE = 1,
        READ_PROCESS_CONNECTIONS,
        READ_DANGLING_CONNECTIONS,
        READ_FINISHED
    };
    ReadStep GetReadStep() const { return _readStep; }
    WorkDispatcher *GetDispatcher() { return _dispatcher; }

    struct StageListener : public TfWeakBase {
        
        void _OnUsdObjectsChanged(UsdNotice::ObjectsChanged const& notice,
                              UsdStageWeakPtr const& sender);
        
        std::unordered_set<SdfPath, TfHash> _dirtyNodes;
    };


    
    const AtString &GetPxrMtlxPath() { return _pxrMtlxPath;}    

    void ReadLightLinks();
    
    // Get the world matrix of a given primitive, using the provided xform cache (each thread has its own)
    void GetWorldMatrix(const UsdPrim &prim, UsdGeomXformCache *xformCache, GfMatrix4d &xform) {
        if (xformCache == nullptr)
            return;

        // If there's no root primitive set ("object_path" in the procedural)
        // then we simply get the local to world matrix for this prim
        if (!_hasRootPrim) {
            xform = xformCache->GetLocalToWorldTransform(prim);
            return;
        }
        // At this point we have a root primitive as we read the stage. We need to ensure that 
        // we don't take into account all transformations from the root's ancestor primitives
        bool resetStack = false; // dummy attribute

        // if the primitive IS the root prim, then we just want its local xform
        if (prim == _rootPrim) {
            xform = xformCache->GetLocalTransformation(prim, &resetStack);
            return;
        }
        UsdPrim parent = _rootPrim.GetParent();
        // Compute the prim's transform relatively to the root prim. However, the function
        // ComputeRelativeTransform specifies that it ignores the "ancestor" trnsform, which
        // is not what we want here. Therefore we must call it with the root's parent prim
        // as the relative "ancestor" prim
        if (parent) {
            xform = xformCache->ComputeRelativeTransform(prim, parent, &resetStack);
        } else {
            // no parent was found for the root prim, let's just compute the world matrix
            xform = xformCache->GetLocalToWorldTransform(prim);
        }
    }
   // void ComputeMotionRange(const UsdPrim &renderSettings);
        
private:
    const AtNode *_procParent;          // the created nodes are children of a procedural parent
    AtUniverse *_universe;              // only set if a specific universe is being used
    TimeSettings _time;
    bool _convert; // do we want to convert the primitives attributes
    bool _debug;
    unsigned int _threadCount;
    int _mask;             // mask based on the arnold flags (AI_NODE_SHADER, etc...) to control
                           // what type of nodes are being read
    std::string _renderSettings; // which RenderSettings prims to consider for the Arnold options
    UsdStageRefPtr _stage; // current stage being read. Will be cleared once
                           // finished reading
    std::vector<AtNode *> _nodes;
    std::unordered_map<std::string, AtNode *> _nodeNames;

    std::unordered_map<std::string, UsdCollectionAPI> _lightLinksMap;
    std::unordered_map<std::string, UsdCollectionAPI> _shadowLinksMap;
    
    AtNode *_defaultShader;
    
    bool _hasRootPrim;     // are we reading this stage based on a root primitive
    UsdPrim _rootPrim;     // eventual root primitive used to traverse the stage
    AtMutex _readerLock; // arnold mutex for multi-threaded translator

    ReadStep _readStep;
    TfToken _purpose;
    WorkDispatcher *_dispatcher;
    AtString _pxrMtlxPath; // environment variable PXR_MTLX_STDLIB_SEARCH_PATHS

    unsigned int _id = 0; ///< Arnold shape ID for the procedural.
    // Reader registry, will be used in the default case
    UsdArnoldReaderRegistry *_readerRegistry = nullptr;
    StageListener _listener;
    TfNotice::Key _objectsChangedNoticeKey;
    bool _updating = false;
};

class UsdArnoldReaderThreadContext : public ArnoldAPIAdapter {
public:
    UsdArnoldReaderThreadContext() : _reader(nullptr), _xformCache(nullptr), _dispatcher(nullptr) {}
    ~UsdArnoldReaderThreadContext();

    UsdArnoldReader *GetReader() { return _reader; }
    void SetReader(UsdArnoldReader *r);
    std::vector<AtNode *> &GetNodes() { return _nodes; }
    const TimeSettings &GetTimeSettings() const { return _reader->GetTimeSettings(); }

    const std::vector<UsdGeomPrimvar> &GetPrimvars() const override {return _primvars;}
    
    AtNode *CreateArnoldNode(const char *type, const char *name) override;
    void AddConnection(AtNode *source, const std::string &attr, const std::string &target, 
        ConnectionType type, const std::string &outputElement = std::string()) override;
    void ProcessConnections() override;
    AtNode* LookupTargetNode(const char *targetName, const AtNode* source, ConnectionType c) override;
    const AtString& GetPxrMtlxPath() override {return _reader->GetPxrMtlxPath();}

    UsdGeomXformCache *GetXformCache(float frame);

    void AddNodeName(const std::string &name, AtNode *node) override;
    std::unordered_map<std::string, AtNode *> &GetNodeNames() { return _nodeNames; }

    std::vector<std::vector<UsdGeomPrimvar> > &GetPrimvarsStack() {return _primvarsStack;}
   
    void SetDispatcher(WorkDispatcher *dispatcher);
    WorkDispatcher *GetDispatcher() {return _dispatcher;}

    void RegisterLightLinks(const std::string &lightName, const UsdCollectionAPI &collectionAPI);
    void RegisterShadowLinks(const std::string &lightName, const UsdCollectionAPI &collectionAPI);  
    std::unordered_map<std::string, UsdCollectionAPI> &GetLightLinksMap() {return _lightLinksMap;}
    std::unordered_map<std::string, UsdCollectionAPI> &GetShadowLinksMap() {return _shadowLinksMap;}

    void SetHidden(bool b) {_hide = b;}
    bool IsHidden() const {return _hide;}

    UsdArnoldSkelData *GetSkelData() {
        if (_skelData && !_skelData->IsValid())
            return nullptr;
        return _skelData;
    }
    void CreateSkelData(const UsdPrim &prim) {
        if (_skelData == nullptr)
            _skelData = new UsdArnoldSkelData(prim);
    }
    void ClearSkelData() {
        if (_skelData)
            delete _skelData;
        _skelData = nullptr;
    }

private:
    UsdArnoldReader *_reader;
    std::vector<AtNode *> _nodes;
    std::unordered_map<std::string, AtNode *> _nodeNames;
    UsdGeomXformCache *_xformCache;                                // main xform cache for current frame
    std::unordered_map<float, UsdGeomXformCache *> _xformCacheMap; // map of xform caches for animated keys
    std::vector<std::vector<UsdGeomPrimvar> > _primvarsStack;
    std::vector<UsdGeomPrimvar> _primvars;
    WorkDispatcher *_dispatcher;
    std::unordered_map<std::string, UsdCollectionAPI> _lightLinksMap;
    std::unordered_map<std::string, UsdCollectionAPI> _shadowLinksMap;
    UsdArnoldSkelData *_skelData = nullptr;

    AtMutex _createNodeLock;
    AtMutex _addConnectionLock;
    AtMutex _addNodeNameLock;
    bool _hide = false;

};


class UsdArnoldReaderContext : public ArnoldAPIAdapter {

public:

    UsdArnoldReaderContext(UsdArnoldReaderThreadContext *t) : 
        _threadContext(t),
        _matrix(nullptr) {}

    UsdArnoldReaderContext() : 
        _threadContext(nullptr),
        _matrix(nullptr) {}

    UsdArnoldReaderContext(const UsdArnoldReaderContext &src, 
        AtArray *matrix, const std::vector<UsdGeomPrimvar> &primvars, bool hide, UsdArnoldSkelData *skelData = nullptr) : 
            _threadContext(src._threadContext),
            _matrix(matrix),
            _primvars(primvars),
            _hide(hide),
            _skelData(skelData),
            _prototypeName(src._prototypeName) {}

    ~UsdArnoldReaderContext() {
        if (_matrix) {
            AiArrayDestroy(_matrix);
            _matrix = nullptr;
        }
        if (_skelData)
            delete _skelData;
        _skelData = nullptr;
    }

    UsdArnoldReaderThreadContext *_threadContext;
    AtArray *_matrix;
    std::vector<UsdGeomPrimvar> _primvars;
    bool _hide = false;
    UsdArnoldSkelData *_skelData = nullptr;
    std::string _prototypeName;

    UsdArnoldReader *GetReader() { return _threadContext->GetReader(); }
    void AddNodeName(const std::string &name, AtNode *node) override {_threadContext->AddNodeName(name, node);}
    const TimeSettings &GetTimeSettings() const { return _threadContext->GetTimeSettings(); }
    const AtString& GetPxrMtlxPath() override {return GetReader()->GetPxrMtlxPath();}
    
    UsdGeomXformCache *GetXformCache(float frame) {
        return _threadContext->GetXformCache(frame);
    }

    std::string GetArnoldNodeName(const char *name)
    {
        if (_prototypeName.empty())
            return std::string(name);
        
        std::string primName(name);
        size_t pos = primName.find('/', 1);
        if (pos != std::string::npos)
            primName = primName.substr(pos);

        primName = _prototypeName + primName;
        return primName;
    }
    AtNode *CreateArnoldNode(const char *type, const char *name) override {
        if (_prototypeName.empty())
            return _threadContext->CreateArnoldNode(type, name);

        std::string primName = GetArnoldNodeName(name);
        return _threadContext->CreateArnoldNode(type, primName.c_str());
    }
    AtNode* LookupTargetNode(const char *name, const AtNode* sourceNode, ConnectionType type) override {
        return _threadContext->LookupTargetNode(name, sourceNode, type);
    }

    void AddConnection(AtNode *source, const std::string &attr, const std::string &target, 
        ConnectionType type, const std::string &outputElement = std::string()) override {
        _threadContext->AddConnection(source, attr, target, type, outputElement);
    }
    void RegisterLightLinks(const std::string &lightName, const UsdCollectionAPI &collectionAPI) {
        _threadContext->RegisterLightLinks(lightName, collectionAPI);
    }
    void RegisterShadowLinks(const std::string &lightName, const UsdCollectionAPI &collectionAPI) {
        _threadContext->RegisterShadowLinks(lightName, collectionAPI);
    }
    UsdArnoldSkelData *GetSkelData() {
        if (!_threadContext->GetDispatcher())
            return _threadContext->GetSkelData();
        
        if (_skelData && !_skelData->IsValid())
            return nullptr;
        return _skelData;
    }
    
    const std::vector<UsdGeomPrimvar> &GetPrimvars() const override {
        if (!_threadContext->GetDispatcher())
            return _threadContext->GetPrimvarsStack().back();
        return _primvars;
    }

    bool IsHidden() const
    {
        if (!_threadContext->GetDispatcher())
            return _threadContext->IsHidden();
        return _hide;
    }
    ///
    /// @param prim the usdPrim we are check the visibility of
    /// @param frame at what frame we are checking the visibility
    /// @return  whether or not the prim is visible
    bool GetPrimVisibility(const UsdPrim &prim, float frame);

    AtArray* GetMatrices() {return _matrix;}
    void SetMatrices(AtArray *m) {_matrix = m;}
    UsdArnoldReaderThreadContext *GetThreadContext() {return _threadContext;}

    const std::string &GetPrototypeName() const {return _prototypeName;}
    void SetPrototypeName(const std::string &p) {_prototypeName = p;}
};
