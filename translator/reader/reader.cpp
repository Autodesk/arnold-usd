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
#include <pxr/usd/usdGeom/camera.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "prim_reader.h"
#include "registry.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// global reader registry, will be used in the default case
static UsdArnoldReaderRegistry *s_readerRegistry = nullptr;

UsdArnoldReader::~UsdArnoldReader()
{
    // What do we want to do at destruction here ?
    // Should we delete the created nodes in case there was no procParent ?

    if (_readerLock)
        AiCritSecClose((void **)&_readerLock);
}

void UsdArnoldReader::Read(const std::string &filename, AtArray *overrides, const std::string &path)
{
    // Nodes were already exported, should we skip here,
    // or should we just append the new nodes ?
    if (!_nodes.empty()) {
        return;
    }

    SdfLayerRefPtr rootLayer = SdfLayer::FindOrOpen(filename);
    _filename = filename;   // Store the filename that is currently being read
    _overrides = overrides; // Store the overrides that are currently being applied

    if (overrides == nullptr || AiArrayGetNumElements(overrides) == 0) {
        // Only open the usd file as a root layer
        if (rootLayer == nullptr) {
            AiMsgError("[usd] Failed to open file (%s)", filename.c_str());
            return;
        }
        UsdStageRefPtr stage = UsdStage::Open(rootLayer, UsdStage::LoadAll);
        ReadStage(stage, path);
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
        // If there is no rootLayer for a usd file, we only pass the overrideLayer to prevent
        // USD from crashing #235
        auto stage = rootLayer ? UsdStage::Open(rootLayer, overrideLayer, UsdStage::LoadAll)
                               : UsdStage::Open(overrideLayer, UsdStage::LoadAll);

        ReadStage(stage, path);
    }

    _filename = "";       // finished reading, let's clear the filename
    _overrides = nullptr; // clear the overrides pointer. Note that we don't own this array
}

struct UsdThreadData {
    UsdThreadData() : threadId(0), threadCount(0), rootPrim(nullptr) {}

    unsigned int threadId;
    unsigned int threadCount;
    UsdPrim *rootPrim;
    UsdArnoldReaderContext context;
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
    UsdPrim *rootPrim = threadData->rootPrim;
    UsdArnoldReader *reader = threadData->context.GetReader();
    TfToken visibility;

    // Traverse the stage, either the full one, or starting from a root primitive
    // (in case an object_path is set).
    UsdPrimRange range = (rootPrim) ? UsdPrimRange(*rootPrim) : reader->GetStage()->Traverse();
    for (auto iter = range.begin(); iter != range.end(); ++iter) {
        const UsdPrim &prim(*iter);
        // Check if that primitive is set as being invisible.
        // If so, skip it and prune its children to avoid useless conversions
        if (prim.IsA<UsdGeomImageable>()) {
            UsdGeomImageable imageable(prim);
            if (imageable.GetVisibilityAttr().Get(&visibility) && visibility == UsdGeomTokens->invisible) {
                iter.PruneChildren();
                continue;
            }
        }

        // Each thread only considers one primitive for every amount of threads.
        // Note that this must happen after the above visibility test
        if (multithread && ((index++ + threadId) % threadCount))
            continue;

        reader->ReadPrimitive(prim, threadData->context);
        // Note: if the registry didn't find any primReader, we're not prunning
        // its children nodes, but just skipping this one.
    }
    return 0;
}
unsigned int UsdArnoldReader::ProcessConnectionsThread(void *data)
{
    UsdThreadData *threadData = (UsdThreadData *)data;
    if (threadData) {
        threadData->context.ProcessConnections();
    }
    return 0;
}

void UsdArnoldReader::ReadStage(UsdStageRefPtr stage, const std::string &path)
{
    // set the stage while we're reading
    _stage = stage;
    if (stage == nullptr) {
        AiMsgError("[usd] Unable to create USD stage from %s", _filename.c_str());
        return;
    }

    if (_debug) {
        std::string txt("==== Initializing Usd Reader ");
        if (_procParent) {
            txt += " for procedural ";
            txt += AiNodeGetName(_procParent);
        }
        AiMsgWarning(txt.c_str());
    }

    // eventually use a dedicated registry
    if (_registry == nullptr) {
        // No registry was set (default), let's use the global one
        if (s_readerRegistry == nullptr) {
            s_readerRegistry = new UsdArnoldReaderRegistry(); // initialize the global registry
        }
        _registry = s_readerRegistry;
    }
    // If this is read through a procedural, we don't want to read
    // options, drivers, filters, etc...
    int procMask = (_procParent) ? (AI_NODE_CAMERA | AI_NODE_LIGHT | AI_NODE_SHAPE | AI_NODE_SHADER | AI_NODE_OPERATOR)
                                 : AI_NODE_ALL;

    // Note that the user might have set a mask on a custom registry.
    // We want to consider the intersection of the reader's mask,
    // the existing registry mask, and the eventual procedural mask set above
    _registry->SetMask(_registry->GetMask() & _mask & procMask);

    // Register the prim readers now
    _registry->RegisterPrimitiveReaders();

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

    // If there is not parent procedural, and we need to lookup the options, then we first need to find the
    // render camera and check its shutter, in order to know if we need to read motion data or not (#346)
    if (_procParent == nullptr) {
        UsdPrim options = _stage->GetPrimAtPath(SdfPath("/options"));
        static const TfToken cameraToken("camera");

        if (options && options.HasAttribute(cameraToken)) {
            UsdAttribute cameraAttr = options.GetAttribute(cameraToken);
            if (cameraAttr) {
                std::string cameraName;
                cameraAttr.Get(&cameraName);
                if (!cameraName.empty()) {
                    UsdPrim cameraPrim = _stage->GetPrimAtPath(SdfPath(cameraName.c_str()));
                    if (cameraPrim) {
                        UsdGeomCamera cam(cameraPrim);

                        bool motionBlur = false;
                        float shutterStart = 0.f;
                        float shutterEnd = 0.f;

                        if (cam) {
                            VtValue shutterOpenValue;
                            if (cam.GetShutterOpenAttr().Get(&shutterOpenValue)) {
                                shutterStart = VtValueGetFloat(shutterOpenValue);
                            }
                            VtValue shutterCloseValue;
                            if (cam.GetShutterCloseAttr().Get(&shutterCloseValue)) {
                                shutterEnd = VtValueGetFloat(shutterCloseValue);
                            }
                        }

                        _time.motionBlur = (shutterEnd > shutterStart);
                        _time.motionStart = shutterStart;
                        _time.motionEnd = shutterEnd;
                    }
                }
            }
        }
    }

    size_t threadCount = _threadCount; // do we want to do something
                                       // automatic when threadCount = 0 ?

    // Multi-thread inspection where each thread has its own "context".
    // We'll be looping over the stage primitives,
    // but won't process any connection between nodes, since we need to wait for
    // the target nodes to be created first. We stack the connections, and process them when finished
    std::vector<UsdThreadData> threadData(threadCount);
    std::vector<void *> threads(threadCount, nullptr);

    // First step, we traverse the stage in order to create all nodes
    _readStep = READ_TRAVERSE;
    for (size_t i = 0; i < threadCount; ++i) {
        threadData[i].threadId = i;
        threadData[i].threadCount = threadCount;
        threadData[i].context.SetReader(this);
        threadData[i].rootPrim = rootPrimPtr;
        threads[i] = AiThreadCreate(UsdArnoldReader::RenderThread, &threadData[i], AI_PRIORITY_HIGH);
    }

    // Wait until all threads are finished and merge all the nodes that
    // they have created to our list
    for (size_t i = 0; i < threadCount; ++i) {
        AiThreadWait(threads[i]);
        AiThreadClose(threads[i]);
        _nodes.insert(_nodes.end(), threadData[i].context.GetNodes().begin(), threadData[i].context.GetNodes().end());
        threadData[i].context.GetNodes().clear();
        threads[i] = nullptr;
    }

    // In a second step, each thread goes through the connections it stacked
    // and processes them given that now all the nodes were supposed to be created.
    _readStep = READ_PROCESS_CONNECTIONS;
    for (size_t i = 0; i < threadCount; ++i) {
        // now I just want to append the links from each thread context
        threads[i] = AiThreadCreate(UsdArnoldReader::ProcessConnectionsThread, &threadData[i], AI_PRIORITY_HIGH);
    }
    std::vector<UsdArnoldReaderContext::Connection> danglingConnections;
    // There is an exception though, some connections could be pointing
    // to primitives that were skipped because they weren't visible.
    // In that case the arnold nodes still don't exist yet, and we
    // need to force their export. Here, all the connections pointing
    // to nodes that don't exist yet are kept in each context connections list.
    // We append them in a list of "dangling connections".
    for (size_t i = 0; i < threadCount; ++i) {
        AiThreadWait(threads[i]);
        AiThreadClose(threads[i]);
        threads[i] = nullptr;
        danglingConnections.insert(
            danglingConnections.end(), threadData[i].context.GetConnections().begin(),
            threadData[i].context.GetConnections().end());
        threadData[i].context.GetConnections().clear();
    }

    // 3rd step, in case some links were pointing to nodes that didn't exist.
    // If they were skipped because of their visibility, we need to force
    // their export now. We handle this in a single thread to avoid costly
    // synchronizations between the threads.
    _readStep = READ_DANGLING_CONNECTIONS;
    if (!danglingConnections.empty()) {
        // We only use the first thread context
        UsdArnoldReaderContext &context = threadData[0].context;
        // loop over the dangling connections, ensure the node still doesn't exist
        // (as it might be referenced multiple times in our list),
        // and if not we try to read it
        for (auto &&conn : danglingConnections) {
            const char *name = conn.target.c_str();
            AtNode *target = LookupNode(name, true);
            if (target == nullptr) {
                SdfPath sdfPath(name);
                UsdPrim prim = _stage->GetPrimAtPath(sdfPath);
                if (prim)
                    ReadPrimitive(prim, context);
            }
            // we can now process the connection
            context.ProcessConnection(conn);
        }
        // Some nodes were possibly created in the above loop,
        // we need to append them to our reader
        _nodes.insert(_nodes.end(), context.GetNodes().begin(), context.GetNodes().end());
        context.GetNodes().clear();
    }

    _stage = UsdStageRefPtr(); // clear the shared pointer, delete the stage
    _readStep = READ_FINISHED; // We're done
}

void UsdArnoldReader::ReadPrimitive(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    std::string objName = prim.GetPath().GetText();
    std::string objType = prim.GetTypeName().GetText();

    UsdArnoldPrimReader *primReader = _registry->GetPrimReader(objType);
    if (primReader) {
        if (_debug) {
            std::string txt;

            txt += "Object ";
            txt += objName;
            txt += " (type: ";
            txt += objType;
            txt += ")";

            AiMsgWarning(txt.c_str());
        }

        primReader->Read(prim, context); // read this primitive
    }
}
void UsdArnoldReader::SetThreadCount(unsigned int t)
{
    _threadCount = t;

    // if we are in multi-thread, we need to initialize a mutex now
    if (_threadCount > 1 && !_readerLock)
        AiCritSecInit((void **)&_readerLock);
}
void UsdArnoldReader::SetFrame(float frame)
{
    ClearNodes(); // FIXME do we need to clear here ? We should rather re-export
                  // the data
    _time.frame = frame;
}

void UsdArnoldReader::SetMotionBlur(bool motionBlur, float motionStart, float motionEnd)
{
    ClearNodes(); // FIXME do we need to clear here ? We should rather re-export
                  // the data
    _time.motionBlur = motionBlur;
    _time.motionStart = motionStart;
    _time.motionEnd = motionEnd;
}

void UsdArnoldReader::SetDebug(bool b)
{
    // We obviously don't need to clear the data here, but it will make it
    // simpler since the data will be re-generated
    ClearNodes();
    _debug = b;
}
void UsdArnoldReader::SetConvertPrimitives(bool b)
{
    ClearNodes();
    _convert = b;
}
void UsdArnoldReader::ClearNodes()
{
    // FIXME should we also delete the nodes if there is a proc parent ?
    if (_procParent == nullptr) {
        // No parent proc, this means we should delete all nodes ourselves
        for (size_t i = 0; i < _nodes.size(); ++i) {
            AiNodeDestroy(_nodes[i]);
        }
    }
    _nodes.clear();
    _defaultShader = nullptr; // reset defaultShader
}

void UsdArnoldReader::SetProceduralParent(const AtNode *node)
{
    // should we clear the nodes when a new procedural parent is set ?
    ClearNodes();
    _procParent = node;
    _universe = (node) ? AiNodeGetUniverse(node) : nullptr;
}

void UsdArnoldReader::SetRegistry(UsdArnoldReaderRegistry *registry) { _registry = registry; }
void UsdArnoldReader::SetUniverse(AtUniverse *universe)
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
    ClearNodes();
    _universe = universe;
}

AtNode *UsdArnoldReader::GetDefaultShader()
{
    // Eventually lock the mutex
    LockReader();

    if (_defaultShader == nullptr) {
        // The default shader doesn't exist yet, let's create a standard_surface,
        // which base_color is linked to a user_data_rgb that looks up the user data
        // called "displayColor". This way, by default geometries that don't have any
        // shader assigned will appear as in hydra.
        _defaultShader = AiNode(_universe, "standard_surface", "_default_arnold_shader", _procParent);
        AtNode *userData = AiNode(_universe, "user_data_rgb", "_default_arnold_shader_color", _procParent);
        _nodes.push_back(_defaultShader);
        _nodes.push_back(userData);
        AiNodeSetStr(userData, "attribute", "displayColor");
        AiNodeSetRGB(userData, "default", 1.f, 1.f, 1.f); // neutral white shader if no user data is found
        AiNodeLink(userData, "base_color", _defaultShader);
    }

    UnlockReader();

    return _defaultShader;
}

UsdArnoldReaderContext::~UsdArnoldReaderContext()
{
    if (_xformCache)
        delete _xformCache;

    for (std::unordered_map<float, UsdGeomXformCache *>::iterator it = _xformCacheMap.begin();
         it != _xformCacheMap.end(); ++it)
        delete it->second;

    _xformCacheMap.clear();
}
void UsdArnoldReaderContext::SetReader(UsdArnoldReader *r)
{
    if (r == nullptr)
        return; // shouldn't happen
    _reader = r;
    // UsdGeomXformCache will be used to trigger world transformation matrices
    // by caching the already computed nodes xforms in the hierarchy.
    if (_xformCache == nullptr)
        _xformCache = new UsdGeomXformCache(UsdTimeCode(r->GetTimeSettings().frame));
}

AtNode *UsdArnoldReaderContext::CreateArnoldNode(const char *type, const char *name)
{
    AtNode *node = AiNode(_reader->GetUniverse(), type, name, _reader->GetProceduralParent());
    _nodes.push_back(node);
    return node;
}
void UsdArnoldReaderContext::AddConnection(
    AtNode *source, const std::string &attr, const std::string &target, ConnectionType type)
{
    if (_reader->GetReadStep() == UsdArnoldReader::READ_TRAVERSE) {
        // store a link between attributes/nodes to process it later
        _connections.push_back(Connection());
        Connection &conn = _connections.back();
        conn.sourceNode = source;
        conn.sourceAttr = attr;
        conn.target = target;
        conn.type = type;
    } else if (_reader->GetReadStep() == UsdArnoldReader::READ_DANGLING_CONNECTIONS) {
        // we're in the main thread, processing the dangling connections. We want to
        // apply the connection right away
        Connection conn;
        conn.sourceNode = source;
        conn.sourceAttr = attr;
        conn.target = target;
        conn.type = type;
        ProcessConnection(conn);
    }
}
void UsdArnoldReaderContext::ProcessConnections()
{
    std::vector<Connection> danglingConnections;
    for (auto it = _connections.begin(); it != _connections.end(); ++it) {
        // if ProcessConnections returns false, it means that the target
        // wasn't found. We want to stack those dangling connections
        // and keep them in our list
        if (!ProcessConnection(*it))
            danglingConnections.push_back(*it);
    }
    // our connections list is now cleared by contains all the ones
    // that couldn't be resolved
    _connections = danglingConnections;
}

bool UsdArnoldReaderContext::ProcessConnection(const Connection &connection)
{
    UsdArnoldReader::ReadStep step = _reader->GetReadStep();
    if (connection.type == CONNECTION_ARRAY) {
        std::vector<AtNode *> vecNodes;
        std::stringstream ss(connection.target);
        std::string token;
        while (std::getline(ss, token, ' ')) {
            AtNode *target = _reader->LookupNode(token.c_str(), true);
            if (target == nullptr) {
                if (step == UsdArnoldReader::READ_DANGLING_CONNECTIONS) {
                    // generate the missing node right away
                    SdfPath sdfPath(token.c_str());
                    UsdPrim prim = _reader->GetStage()->GetPrimAtPath(sdfPath);
                    if (prim) {
                        _reader->ReadPrimitive(prim, *this);
                        target = _reader->LookupNode(token.c_str(), true);
                    }
                }
                if (target == nullptr)
                    return false; // node is missing, we don't process the connection
            }
            vecNodes.push_back(target);
        }
        AiNodeSetArray(
            connection.sourceNode, connection.sourceAttr.c_str(),
            AiArrayConvert(vecNodes.size(), 1, AI_TYPE_NODE, &vecNodes[0]));

    } else {
        AtNode *target = _reader->LookupNode(connection.target.c_str(), true);
        if (target == nullptr) {
            if (step == UsdArnoldReader::READ_DANGLING_CONNECTIONS) {
                // generate the missing node right away
                SdfPath sdfPath(connection.target.c_str());
                UsdPrim prim = _reader->GetStage()->GetPrimAtPath(sdfPath);
                if (prim) {
                    _reader->ReadPrimitive(prim, *this);
                    target = _reader->LookupNode(connection.target.c_str(), true);
                }
            }
            if (target == nullptr) {
                return false; // node is missing, we don't process the connection
            }
        }
        switch (connection.type) {
            case CONNECTION_PTR:
                AiNodeSetPtr(connection.sourceNode, connection.sourceAttr.c_str(), (void *)target);
                break;
            case CONNECTION_LINK:
                AiNodeLink(target, connection.sourceAttr.c_str(), connection.sourceNode);
                break;
            case CONNECTION_LINK_X:
                AiNodeLinkOutput(target, "x", connection.sourceNode, connection.sourceAttr.c_str());
                break;
            case CONNECTION_LINK_Y:
                AiNodeLinkOutput(target, "y", connection.sourceNode, connection.sourceAttr.c_str());
                break;
            case CONNECTION_LINK_Z:
                AiNodeLinkOutput(target, "z", connection.sourceNode, connection.sourceAttr.c_str());
                break;
            case CONNECTION_LINK_R:
                AiNodeLinkOutput(target, "r", connection.sourceNode, connection.sourceAttr.c_str());
                break;
            case CONNECTION_LINK_G:
                AiNodeLinkOutput(target, "g", connection.sourceNode, connection.sourceAttr.c_str());
                break;
            case CONNECTION_LINK_B:
                AiNodeLinkOutput(target, "b", connection.sourceNode, connection.sourceAttr.c_str());
                break;
            case CONNECTION_LINK_A:
                AiNodeLinkOutput(target, "a", connection.sourceNode, connection.sourceAttr.c_str());
                break;
            default:
                break;
        }
    }
    return true;
}

UsdGeomXformCache *UsdArnoldReaderContext::GetXformCache(float frame)
{
    const TimeSettings &time = _reader->GetTimeSettings();

    if ((time.motionBlur == false || frame == time.frame) && _xformCache)
        return _xformCache; // fastest path : return the main xform cache for the current frame

    UsdGeomXformCache *xformCache = nullptr;

    // Look for a xform cache for the requested frame
    std::unordered_map<float, UsdGeomXformCache *>::iterator it = _xformCacheMap.find(frame);
    if (it == _xformCacheMap.end()) {
        // Need to create a new one.
        // Should we set a hard limit for the amount of xform caches we create ?
        xformCache = new UsdGeomXformCache(UsdTimeCode(frame));
        _xformCacheMap[frame] = xformCache;
    } else {
        xformCache = it->second;
    }

    return xformCache;
}

/// Checks the visibility of the usdPrim
///
/// @param prim The usdPrim we are checking the visibility of
/// @param frame At what frame we are checking the visibility
/// @return  Whether or not the prim is visible
bool UsdArnoldReaderContext::GetPrimVisibility(const UsdPrim &prim, float frame)
{
    // Only compute the visibility when processing the dangling connections,
    // otherwise we return true to avoid costly computation.
    if (_reader->GetReadStep() == UsdArnoldReader::READ_DANGLING_CONNECTIONS) {
        UsdGeomImageable imageable = UsdGeomImageable(prim);
        if (imageable)
            return imageable.ComputeVisibility(frame) != UsdGeomTokens->invisible;
    }

    return true;
}
