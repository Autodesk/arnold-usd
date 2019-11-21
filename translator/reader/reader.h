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
    UsdArnoldReader() : _procParent(NULL), 
                        _universe(NULL), 
                        _registry(NULL), 
                        _debug(false), 
                        _threadCount(1),
                        _xformCache(NULL) {}
    ~UsdArnoldReader();

    void read(const std::string &filename, AtArray *overrides,
              const std::string &path = ""); // read a USD file
    void readStage(UsdStageRefPtr stage,
                   const std::string &path = ""); // read a specific UsdStage
    void readPrimitive(const UsdPrim &prim, bool create = true, bool convert = true);

    // Create an Arnold node of a given type, with a given name. If a node
    // already exists with this name, return the existing one.
    AtNode *createArnoldNode(const char *type, const char *name, bool *existed = NULL);
    void clearNodes();

    void setProceduralParent(const AtNode *node);
    void setUniverse(AtUniverse *universe);
    void setRegistry(UsdArnoldReaderRegistry *registry);
    void setFrame(float frame);
    void setMotionBlur(bool motion_blur, float motion_start = 0.f, float motion_end = 0.f);
    void setDebug(bool b);
    void setThreadCount(unsigned int t);

    const UsdStageRefPtr &getStage() const { return _stage; }
    const std::vector<AtNode *> &getNodes() const { return _nodes; }
    float getFrame() const { return _time.frame; }
    UsdArnoldReaderRegistry *getRegistry() { return _registry; }
    AtUniverse *getUniverse() { return _universe; }
    const AtNode *getProceduralParent() const { return _procParent; }
    bool getDebug() const { return _debug; }
    const TimeSettings &getTimeSettings() const { return _time; }
    UsdGeomXformCache *getXformCache() {return _xformCache;}

    static unsigned int RenderThread(void *data);

private:
    const AtNode *_procParent;          // the created nodes are children of a procedural parent
    AtUniverse *_universe;              // only set if a specific universe is being used
    UsdArnoldReaderRegistry *_registry; // custom registry used for this reader. If null, a global
                                        // registry will be used.
    TimeSettings _time;
    bool _debug;
    unsigned int _threadCount;
    UsdStageRefPtr _stage; // current stage being read. Will be cleared once
                           // finished reading
    std::vector<AtNode *> _nodes;
    UsdGeomXformCache *_xformCache;
};