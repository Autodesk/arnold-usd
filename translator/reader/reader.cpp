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
#include "reader.h"

#include <ai.h>

#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "prim_reader.h"
#include "registry.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// global reader registry, will be used in the default case
static UsdArnoldReaderRegistry *s_readerRegistry = NULL;

UsdArnoldReader::~UsdArnoldReader()
{
    // What do we want to do at destruction here ?
    // Should we delete the created nodes in case there was no procParent ?

    if (_xformCache)
        delete _xformCache;

    for (std::unordered_map<float, UsdGeomXformCache*>::iterator it = _xformCacheMap.begin(); 
        it != _xformCacheMap.end(); ++it)
        delete it->second;

    _xformCacheMap.clear();

    if (_readerLock)
       AiCritSecClose((void**)&_readerLock);

}

void UsdArnoldReader::read(const std::string &filename, AtArray *overrides, const std::string &path)
{
    // Nodes were already exported, should we skip here,
    // or should we just append the new nodes ?
    if (!_nodes.empty()) {
        return;
    }

    SdfLayerRefPtr rootLayer = SdfLayer::FindOrOpen(filename);
    if (rootLayer == nullptr) {
        AiMsgError("[usd] Failed to open file (%s)", filename.c_str());
        return;
    }

    if (overrides == nullptr || AiArrayGetNumElements(overrides) == 0) {
        UsdStageRefPtr stage = UsdStage::Open(rootLayer, UsdStage::LoadAll);
        readStage(stage, path);
    } else {
        auto getLayerName = []() -> std::string {
            static int counter = 0;
            std::stringstream ss;
            ss << "anonymous__override__" << counter++ << ".usda";
            return ss.str();
        };

        auto overrideLayer = SdfLayer::CreateAnonymous(getLayerName());
        const auto overrideCount = AiArrayGetNumElements(overrides);

        std::vector<std::string> layerNames;
        layerNames.reserve(overrideCount);
        // Make sure they kep around after the loop scope ends.
        std::vector<SdfLayerRefPtr> layers;
        layers.reserve(overrideCount);

        for (auto i = decltype(overrideCount){0}; i < overrideCount; ++i) {
            auto layer = SdfLayer::CreateAnonymous(getLayerName());
            if (layer->ImportFromString(AiArrayGetStr(overrides, i).c_str())) {
                layerNames.emplace_back(layer->GetIdentifier());
                layers.push_back(layer);
            }
        }

        overrideLayer->SetSubLayerPaths(layerNames);
        auto stage = UsdStage::Open(rootLayer, overrideLayer, UsdStage::LoadAll);

        readStage(stage, path);
    }
}

struct UsdThreadData {
    UsdThreadData() : threadId(0), threadCount(0), reader(nullptr), rootPrim(nullptr), create(false), convert(false) {}

    unsigned int threadId;
    unsigned int threadCount;
    UsdArnoldReader *reader;
    UsdPrim *rootPrim;
    bool create;
    bool convert;
};

unsigned int UsdArnoldReader::RenderThread(void *data)
{
    UsdThreadData *threadData = (UsdThreadData *)data;
    if (!threadData) {
        return 0;
    }

    size_t index = 0;
    size_t threadId = threadData->threadId;
    size_t threadCount = threadData->threadCount;
    bool multithread = (threadCount > 1);
    bool create = threadData->create;
    bool convert = threadData->convert;
    UsdPrim *rootPrim = threadData->rootPrim;
    UsdArnoldReader *reader = threadData->reader;

    UsdPrimRange range = (rootPrim && false) ? UsdPrimRange(*rootPrim) : reader->getStage()->Traverse();
    for (auto iter = range.begin(); iter != range.end(); ++iter) {
        // Each thread only considers 1 every thread count primitives
        if (multithread && ((index++ + threadId) % threadCount)) {
            continue;
        }
        const UsdPrim &prim(*iter);
        reader->readPrimitive(prim, create, convert);
        // Note: if the registry didn't find any primReader, we're not prunning
        // its children nodes, but just skipping this one.
    }
    return 0;
}

void UsdArnoldReader::readStage(UsdStageRefPtr stage, const std::string &path)
{
    // set the stage while we're reading
    _stage = stage;

    // FIXME : should we flatten the UsdStage or not ?
    // _stage->Flatten();

    if (_debug) {
        std::string txt("==== Initializing Usd Reader ");
        if (_procParent) {
            txt += " for procedural ";
            txt += AiNodeGetName(_procParent);
        }
        AiMsgWarning(txt.c_str());
    }

    // eventually use a dedicated registry
    if (_registry == NULL) {
        // No registry was set (default), let's use the global one
        if (s_readerRegistry == NULL) {
            s_readerRegistry = new UsdArnoldReaderRegistry(); // initialize the global registry
            s_readerRegistry->registerPrimitiveReaders();
        }

        _registry = s_readerRegistry;
    }

    // UsdGeomXformCache will be used to trigger world transformation matrices 
    // by caching the already computed nodes xforms in the hierarchy.
    if (_xformCache == NULL)
        _xformCache = new UsdGeomXformCache(UsdTimeCode(_time.frame));

    UsdPrim rootPrim;
    UsdPrim *rootPrimPtr = nullptr;

    if (!path.empty()) {
        SdfPath sdfPath(path);
        rootPrim = _stage->GetPrimAtPath(sdfPath);
        if ((!rootPrim) || (!rootPrim.IsActive())) {
            AiMsgError(
                "[usd] %s : Object Path %s is not valid", (_procParent) ? AiNodeGetName(_procParent) : "",
                path.c_str());
            return;
        }
        rootPrimPtr = &rootPrim;
    }

    if (_threadCount == 1) {
        // Just for safety, I'm doing a special case for single thread, but this
        // code should be unified with the multi-thread code
        UsdPrimRange range = (rootPrimPtr) ? UsdPrimRange(*rootPrimPtr) : _stage->Traverse();

        for (auto iter = range.begin(); iter != range.end(); ++iter) {
            const UsdPrim &prim(*iter);
            readPrimitive(prim, true, _convert);
        }
    } else {
        size_t threadCount = _threadCount; // do we want to do something
                                           // automatic when threadCount = 0 ?

        // Multi-thread inspection. We need to split the task in two.
        //   - First, we traverse the Usd stage to create all the nodes
        //   - Second, we traverse again the stage in order to do the actual
        //   conversion

        // This is necessary to ensure that all nodes are properly created
        // before setting eventual links between them

        std::vector<UsdThreadData> threadData(threadCount);
        std::vector<void *> threads(threadCount, nullptr);

        // First we want to traverse the stage in order to create all nodes
        for (size_t i = 0; i < threadCount; ++i) {
            threadData[i].threadId = i;
            threadData[i].threadCount = threadCount;
            threadData[i].reader = new UsdArnoldReader(*this);
            threadData[i].rootPrim = rootPrimPtr;
            threadData[i].create = true;
            threadData[i].convert = false;
            threads[i] = AiThreadCreate(UsdArnoldReader::RenderThread, &threadData[i], AI_PRIORITY_HIGH);
        }

        for (size_t i = 0; i < threadCount; ++i) {
            AiThreadWait(threads[i]);
            AiThreadClose(threads[i]);
            _nodes.insert(_nodes.end(), threadData[i].reader->_nodes.begin(), threadData[i].reader->_nodes.end());
            threads[i] = nullptr;
        }
        // At this point all the nodes were created, let's now do the actual
        // conversion
        if (_convert)
        {
            for (size_t i = 0; i < threadCount; ++i) {
                threadData[i].create = false;
                threadData[i].convert = true;
                threads[i] = AiThreadCreate(UsdArnoldReader::RenderThread, &threadData[i], AI_PRIORITY_HIGH);
            }
            for (size_t i = 0; i < threadCount; ++i) {
                AiThreadWait(threads[i]);
                AiThreadClose(threads[i]);
                threads[i] = nullptr;
            }
        }

        for (size_t i = 0; i < threadCount; ++i) {
            delete threadData[i].reader;
        }
    }

    _stage = UsdStageRefPtr(); // clear the shared pointer, delete the stage
}

void UsdArnoldReader::readPrimitive(const UsdPrim &prim, bool create, bool convert)
{
    std::string objName = prim.GetPath().GetText();
    std::string objType = prim.GetTypeName().GetText();

    UsdArnoldPrimReader *primReader = _registry->getPrimReader(objType);
    if (primReader) {
        if (_debug) {
            std::string txt;
            if (create) {
                txt += "Create ";
            }
            if (convert) {
                if (create) {
                    txt += "and ";
                }
                txt += "Convert ";
            }

            txt += "Object ";
            txt += objName;
            txt += " (type: ";
            txt += objType;
            txt += ")";

            AiMsgWarning(txt.c_str());
        }

        primReader->read(prim, *this, create, convert); // read this primitive
    }
}
void UsdArnoldReader::setThreadCount(unsigned int t)
{ 
    _threadCount = t;

    // if we are in multi-thread, we need to initialize a mutex now
    if (_threadCount > 1 && !_readerLock) 
        AiCritSecInit((void**)&_readerLock);
}
void UsdArnoldReader::setFrame(float frame)
{
    clearNodes(); // FIXME do we need to clear here ? We should rather re-export
                  // the data
    _time.frame = frame;

    if (_xformCache)
        delete _xformCache;

    // eventually lock our mutex
    if (_threadCount > 1 && _readerLock)
        AiCritSecEnter(&_readerLock);

    for (std::unordered_map<float, UsdGeomXformCache*>::iterator it = _xformCacheMap.begin(); 
        it != _xformCacheMap.end(); ++it)
        delete it->second;    
    
    // unlock the mutex if needed
    if (_threadCount > 1 && _readerLock)
        AiCritSecLeave(&_readerLock);
}

void UsdArnoldReader::setMotionBlur(bool motion_blur, float motion_start, float motion_end)
{
    clearNodes(); // FIXME do we need to clear here ? We should rather re-export
                  // the data
    _time.motion_blur = motion_blur;
    _time.motion_start = motion_start;
    _time.motion_end = motion_end;
}

void UsdArnoldReader::setDebug(bool b)
{
    // We obviously don't need to clear the data here, but it will make it
    // simpler since the data will be re-generated
    clearNodes();
    _debug = b;
}
void UsdArnoldReader::setConvertPrimitives(bool b)
{
    clearNodes();
    _convert = b;
}
void UsdArnoldReader::clearNodes()
{
    // FIXME should we also delete the nodes if there is a proc parent ?
    if (_procParent == NULL) {
        // No parent proc, this means we should delete all nodes ourselves
        for (size_t i = 0; i < _nodes.size(); ++i) {
            AiNodeDestroy(_nodes[i]);
        }
    }
    _nodes.clear();
    _defaultShader = nullptr; // reset defaultShader
}

void UsdArnoldReader::setProceduralParent(const AtNode *node)
{
    // should we clear the nodes when a new procedural parent is set ?
    clearNodes();
    _procParent = node;
    _universe = (node) ? AiNodeGetUniverse(node) : NULL;
}

void UsdArnoldReader::setRegistry(UsdArnoldReaderRegistry *registry) { _registry = registry; }
void UsdArnoldReader::setUniverse(AtUniverse *universe)
{
    if (_procParent) {
        if (universe != _universe) {
            AiMsgError(
                "UsdArnoldReader: we cannot set a universe that is different "
                "from the procedural parent");
        }
        return;
    }
    // should we clear the nodes when a new universe is set ?
    clearNodes();
    _universe = universe;
}

AtNode *UsdArnoldReader::createArnoldNode(const char *type, const char *name, bool *existed)
{
    AtNode *node = AiNodeLookUpByName(_universe, name, _procParent);
    if (existed) {
        *existed = (node != NULL);
    }

    if (node)
        return node; // FIXME should we test if the nodeType is different ?

    node = AiNode(_universe, type, name, _procParent);
    if (node) {
        _nodes.push_back(node);
    }

    return node;
}
AtNode *UsdArnoldReader::getDefaultShader()
{
    // Eventually lock the mutex
    if (_threadCount > 1 && _readerLock)
        AiCritSecEnter(&_readerLock);

    if (_defaultShader == nullptr) {
        // The default shader doesn't exist yet, let's create a standard_surface, 
        // which base_color is linked to a user_data_rgb that looks up the user data
        // called "displayColor". This way, by default geometries that don't have any 
        // shader assigned will appear as in hydra.
        _defaultShader = createArnoldNode("standard_surface", "_default_arnold_shader");
        AtNode *userData = createArnoldNode("user_data_rgb", "_default_arnold_shader_color");
        AiNodeSetStr(userData, "attribute", "displayColor");
        AiNodeSetRGB(userData, "default", 1.f, 1.f, 1.f); // neutral white shader if no user data is found
        AiNodeLink(userData, "base_color", _defaultShader);
    }

    if (_threadCount > 1 && _readerLock)
        AiCritSecLeave(&_readerLock);

    return _defaultShader;
}

UsdGeomXformCache *UsdArnoldReader::getXformCache(float frame)
{    
    if ((_time.motion_blur == false || frame == _time.frame) && _xformCache)
        return _xformCache; // fastest path : return the main xform cache for the current frame

    // if the reader is multi-threaded, we need to lock our mutex
    if (_threadCount > 1 && _readerLock)
        AiCritSecEnter(&_readerLock);

    UsdGeomXformCache *xformCache = nullptr;

    // Look for a xform cache for the requested frame
    std::unordered_map<float, UsdGeomXformCache*>::iterator it = _xformCacheMap.find(frame);
    if (it == _xformCacheMap.end())
    {
        // Need to create a new one.
        // Should we set a hard limit for the amount of xform caches we create ?
        xformCache = new UsdGeomXformCache(UsdTimeCode(frame));
        _xformCacheMap[frame] = xformCache;
    } else
    {
        xformCache = it->second;
    }

    // if the reader is multi-threaded, we need to unlock our mutex
    if (_threadCount > 1 && _readerLock)
        AiCritSecLeave(&_readerLock);

    return xformCache;
}