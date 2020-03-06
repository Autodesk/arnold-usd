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

#include <pxr/usd/usd/prim.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "utils.h"
#include <pxr/usd/usdGeom/xformCache.h>

PXR_NAMESPACE_USING_DIRECTIVE

class UsdArnoldReaderRegistry;

/**
 *  Class handling the translation of USD data to Arnold
 **/

class UsdArnoldReader {
public:

    UsdArnoldReader() : _procParent(nullptr), 
                        _universe(nullptr), 
                        _registry(nullptr), 
                        _convert(true),
                        _debug(false), 
                        _threadCount(1),
                        _defaultShader(nullptr),
                        _overrides(nullptr),
                        _readerLock(nullptr),
                        _readStep(READ_NOT_STARTED) {}
    ~UsdArnoldReader();

    void read(const std::string &filename, AtArray *overrides,
              const std::string &path = ""); // read a USD file
    void readStage(UsdStageRefPtr stage,
                   const std::string &path = ""); // read a specific UsdStage
    void readPrimitive(const UsdPrim &prim, UsdArnoldReaderContext &context);

    void clearNodes();

    void setProceduralParent(const AtNode *node);
    void setUniverse(AtUniverse *universe);
    void setRegistry(UsdArnoldReaderRegistry *registry);
    void setFrame(float frame);
    void setMotionBlur(bool motion_blur, float motion_start = 0.f, float motion_end = 0.f);
    void setDebug(bool b);
    void setThreadCount(unsigned int t);
    void setConvertPrimitives(bool b);

    const UsdStageRefPtr &getStage() const { return _stage; }
    const std::vector<AtNode *> &getNodes() const { return _nodes; }
    float getFrame() const { return _time.frame; }
    UsdArnoldReaderRegistry *getRegistry() { return _registry; }
    AtUniverse *getUniverse() { return _universe; }
    const AtNode *getProceduralParent() const { return _procParent; }
    bool getDebug() const { return _debug; }
    bool getConvertPrimitives() const {return _convert;}
    const TimeSettings &getTimeSettings() const { return _time; }
    const std::string &getFilename() const {return _filename;}
    const AtArray *getOverrides() const {return _overrides;}
    unsigned int getThreadCount() const {return _threadCount;}

    static unsigned int RenderThread(void *data);
    static unsigned int ProcessConnectionsThread(void *data);

    AtNode *getDefaultShader();
    AtNode *lookupNode(const char *name, bool checkParent = true) {
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
    void lockReader()
    {
        if (_threadCount > 1 && _readerLock)
            AiCritSecEnter(&_readerLock);                
    }
    void unlockReader()
    {
        if (_threadCount > 1 && _readerLock)
            AiCritSecLeave(&_readerLock);                
    }

    // Reading a stage in multithread implies to go
    // through different steps, in order to handle the 
    // connections between nodes. This enum will tell us
    // at which step we are during the whole process
    enum ReadStep
    {
       READ_NOT_STARTED = 0,
       READ_TRAVERSE = 1,
       READ_PROCESS_CONNECTIONS,
       READ_DANGLING_CONNECTIONS,
       READ_FINISHED
    };
    ReadStep getReadStep() const {return _readStep;}
private:

    const AtNode *_procParent;          // the created nodes are children of a procedural parent
    AtUniverse *_universe;              // only set if a specific universe is being used
    UsdArnoldReaderRegistry *_registry; // custom registry used for this reader. If null, a global
                                        // registry will be used.
    TimeSettings _time;
    bool _convert;                      // do we want to convert the primitives attributes
    bool _debug;
    unsigned int _threadCount;
    UsdStageRefPtr _stage; // current stage being read. Will be cleared once
                           // finished reading
    std::vector<AtNode *> _nodes;
    AtNode *_defaultShader;
    std::string _filename; // usd filename that is currently being read
    AtArray *_overrides; // usd overrides that are currently being applied on top of the usd file
    AtCritSec _readerLock; // arnold mutex for multi-threaded translator

    ReadStep _readStep;
};

class UsdArnoldReaderContext {
public:
    UsdArnoldReaderContext() : _reader(nullptr), _xformCache(nullptr) {}
    ~UsdArnoldReaderContext();

    UsdArnoldReader* getReader() {return _reader;}
    void setReader(UsdArnoldReader *r);
    std::vector<AtNode *> &getNodes() {return _nodes;}
    const TimeSettings &getTimeSettings() const { return _reader->getTimeSettings(); }

    enum ConnectionType
    {
       CONNECTION_LINK = 0,
       CONNECTION_PTR = 1,
       CONNECTION_ARRAY,
       CONNECTION_LINK_X,
       CONNECTION_LINK_Y,
       CONNECTION_LINK_Z,
       CONNECTION_LINK_R,
       CONNECTION_LINK_G,
       CONNECTION_LINK_B,
       CONNECTION_LINK_A
    };
    struct Connection {
        AtNode *sourceNode;
        std::string sourceAttr;
        std::string target;
        ConnectionType type;
    };

    AtNode *createArnoldNode(const char *type, const char *name);
    void addConnection(AtNode *source, const std::string &attr, const std::string &target, ConnectionType type );
    void processConnections();
    bool processConnection(const Connection& connection);

    std::vector<Connection> &getConnections() {return _connections;}
    UsdGeomXformCache *getXformCache(float frame);

    /// Checks the visibility of the usdPrim
    ///
    /// @param prim the usdPrim we are check the visibility of
    /// @param frame at what frame we are checking the visibility
    /// @return  whether or not the prim is visible
    bool getPrimVisibility(const UsdPrim& prim, float frame);

private:
    std::vector<Connection> _connections;
    UsdArnoldReader *_reader;
    std::vector<AtNode* > _nodes;
    UsdGeomXformCache *_xformCache; // main xform cache for current frame
    std::unordered_map<float, UsdGeomXformCache*> _xformCacheMap; // map of xform caches for animated keys
};

