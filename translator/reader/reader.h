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

#include <ai.h>

#include <pxr/base/work/dispatcher.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "utils.h"

PXR_NAMESPACE_USING_DIRECTIVE

class UsdArnoldReaderRegistry;

/**
 *  Class handling the translation of USD data to Arnold
 **/

class UsdArnoldReader {
public:
    UsdArnoldReader()
        : _procParent(nullptr),
          _universe(nullptr),
          _registry(nullptr),
          _convert(true),
          _debug(false),
          _threadCount(1),
          _mask(AI_NODE_ALL),
          _defaultShader(nullptr),
          _overrides(nullptr),
          _cacheId(0),
          _hasRootPrim(false),
          _readerLock(nullptr),
          _readStep(READ_NOT_STARTED),
          _purpose(UsdGeomTokens->render),
          _dispatcher(nullptr)
    {
    }
    ~UsdArnoldReader();

    void Read(const std::string &filename, AtArray *overrides,
              const std::string &path = ""); // read a USD file
    void Read(int cacheId, const std::string &path = ""); // read a USdStage from memory
    void ReadStage(UsdStageRefPtr stage,
                   const std::string &path = ""); // read a specific UsdStage
    void ReadPrimitive(const UsdPrim &prim, UsdArnoldReaderContext &context, bool isInstance = false);

    void ClearNodes();

    void SetProceduralParent(const AtNode *node);
    void SetUniverse(AtUniverse *universe);
    void SetRegistry(UsdArnoldReaderRegistry *registry);
    void SetFrame(float frame);
    void SetMotionBlur(bool motionBlur, float motionStart = 0.f, float motionEnd = 0.f);
    void SetDebug(bool b);
    void SetThreadCount(unsigned int t);
    void SetConvertPrimitives(bool b);
    void SetMask(int m) { _mask = m; }
    void SetPurpose(const std::string &p) { _purpose = TfToken(p.c_str()); }
    void SetId(unsigned int id) { _id = id; }
    void SetRenderSettings(const std::string &renderSettings) {_renderSettings = renderSettings;}

    const UsdStageRefPtr &GetStage() const { return _stage; }
    const std::vector<AtNode *> &GetNodes() const { return _nodes; }
    float GetFrame() const { return _time.frame; }
    UsdArnoldReaderRegistry *GetRegistry() { return _registry; }
    AtUniverse *GetUniverse() { return _universe; }
    const AtNode *GetProceduralParent() const { return _procParent; }
    bool GetDebug() const { return _debug; }
    bool GetConvertPrimitives() const { return _convert; }
    const TimeSettings &GetTimeSettings() const { return _time; }
    const std::string &GetFilename() const { return _filename; }
    const AtArray *GetOverrides() const { return _overrides; }
    unsigned int GetThreadCount() const { return _threadCount; }
    int GetMask() const { return _mask; }
    unsigned int GetId() const { return _id;}
    const TfToken &GetPurpose() const {return _purpose;}
    int GetCacheId() const {return _cacheId;}
    const std::string &GetRenderSettings() const {return _renderSettings;}

    static unsigned int ReaderThread(void *data);
    static unsigned int ProcessConnectionsThread(void *data);

    bool GetReferencePath(const std::string &primName, std::string &filename, std::string &objectPath);

    AtNode *GetDefaultShader();
    AtNode *LookupNode(const char *name, bool checkParent = true)
    {
        auto it = _nodeNames.find(std::string(name));
        if (it != _nodeNames.end()) {
            return it->second;
        }

        AtNode *node = AiNodeLookUpByName(_universe, name, _procParent);
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
        if (_threadCount != 1 && _readerLock)
            AiCritSecEnter(&_readerLock);
    }
    void UnlockReader()
    {
        if (_threadCount != 1 && _readerLock)
            AiCritSecLeave(&_readerLock);
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
    
    // Type of connection between 2 nodes
    enum ConnectionType {
        CONNECTION_LINK = 0,
        CONNECTION_PTR = 1,
        CONNECTION_ARRAY
    };

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
    
private:
    const AtNode *_procParent;          // the created nodes are children of a procedural parent
    AtUniverse *_universe;              // only set if a specific universe is being used
    UsdArnoldReaderRegistry *_registry; // custom registry used for this reader. If null, a global
                                        // registry will be used.
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
    // store path to prototypes filenames & object paths
    std::unordered_map<std::string, std::pair<std::string, std::string>> _referencesMap; 

    AtNode *_defaultShader;
    std::string _filename; // usd filename that is currently being read
    AtArray *_overrides;   // usd overrides that are currently being applied on top of the usd file
    int _cacheId;          // usdStage cacheID used with a StageCache
    bool _hasRootPrim;     // are we reading this stage based on a root primitive
    UsdPrim _rootPrim;     // eventual root primitive used to traverse the stage
    AtCritSec _readerLock; // arnold mutex for multi-threaded translator

    ReadStep _readStep;
    TfToken _purpose;
    WorkDispatcher *_dispatcher;

    unsigned int _id = 0; ///< Arnold shape ID for the procedural.
};

class UsdArnoldReaderThreadContext {
public:
    UsdArnoldReaderThreadContext() : _reader(nullptr), _xformCache(nullptr), _dispatcher(nullptr),
        _createNodeLock(nullptr), _addConnectionLock(nullptr), _addNodeNameLock(nullptr){}
    ~UsdArnoldReaderThreadContext();

    UsdArnoldReader *GetReader() { return _reader; }
    void SetReader(UsdArnoldReader *r);
    std::vector<AtNode *> &GetNodes() { return _nodes; }
    const TimeSettings &GetTimeSettings() const { return _reader->GetTimeSettings(); }

    
    struct Connection {
        AtNode *sourceNode;
        std::string sourceAttr;
        std::string target;
        UsdArnoldReader::ConnectionType type;
        std::string outputElement;
    };

    AtNode *CreateArnoldNode(const char *type, const char *name);
    void AddConnection(AtNode *source, const std::string &attr, const std::string &target, 
        UsdArnoldReader::ConnectionType type, const std::string &outputElement = std::string());
    void ProcessConnections();
    bool ProcessConnection(const Connection &connection);

    std::vector<Connection> &GetConnections() { return _connections; }
    UsdGeomXformCache *GetXformCache(float frame);

    void AddNodeName(const std::string &name, AtNode *node);
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

private:
    UsdArnoldReader *_reader;
    std::vector<Connection> _connections;
    std::vector<AtNode *> _nodes;
    std::unordered_map<std::string, AtNode *> _nodeNames;
    UsdGeomXformCache *_xformCache;                                // main xform cache for current frame
    std::unordered_map<float, UsdGeomXformCache *> _xformCacheMap; // map of xform caches for animated keys
    std::vector<std::vector<UsdGeomPrimvar> > _primvarsStack;
    WorkDispatcher *_dispatcher;
    std::unordered_map<std::string, UsdCollectionAPI> _lightLinksMap;
    std::unordered_map<std::string, UsdCollectionAPI> _shadowLinksMap;

    AtCritSec _createNodeLock;
    AtCritSec _addConnectionLock;
    AtCritSec _addNodeNameLock;
    bool _hide = false;

};


class UsdArnoldReaderContext {

public:

    UsdArnoldReaderContext(UsdArnoldReaderThreadContext *t) : 
        _threadContext(t),
        _matrix(nullptr) {}

    UsdArnoldReaderContext() : 
        _threadContext(nullptr),
        _matrix(nullptr) {}

    UsdArnoldReaderContext(const UsdArnoldReaderContext &src, 
        AtArray *matrix, const std::vector<UsdGeomPrimvar> &primvars, bool hide) : 
            _threadContext(src._threadContext),
            _matrix(matrix),
            _primvars(primvars),
            _hide(hide) {}

    ~UsdArnoldReaderContext() {
        if (_matrix) {
            AiArrayDestroy(_matrix);
            _matrix = nullptr;
        }
    }

    UsdArnoldReaderThreadContext *_threadContext;
    AtArray *_matrix;
    std::vector<UsdGeomPrimvar> _primvars;
    bool _hide = false;

    UsdArnoldReader *GetReader() { return _threadContext->GetReader(); }
    void AddNodeName(const std::string &name, AtNode *node) {_threadContext->AddNodeName(name, node);}
    const TimeSettings &GetTimeSettings() const { return _threadContext->GetTimeSettings(); }

    UsdGeomXformCache *GetXformCache(float frame) {
        return _threadContext->GetXformCache(frame);
    }

    AtNode *CreateArnoldNode(const char *type, const char *name) {
        return _threadContext->CreateArnoldNode(type, name);
    }

    void AddConnection(AtNode *source, const std::string &attr, const std::string &target, 
        UsdArnoldReader::ConnectionType type, const std::string &outputElement = std::string()) {
        _threadContext->AddConnection(source, attr, target, type, outputElement);
    }
    void RegisterLightLinks(const std::string &lightName, const UsdCollectionAPI &collectionAPI) {
        _threadContext->RegisterLightLinks(lightName, collectionAPI);
    }
    void RegisterShadowLinks(const std::string &lightName, const UsdCollectionAPI &collectionAPI) {
        _threadContext->RegisterShadowLinks(lightName, collectionAPI);
    }

    const std::vector<UsdGeomPrimvar> &GetPrimvars() const {
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
    UsdArnoldReaderThreadContext *GetThreadContext() {return _threadContext;}
};
