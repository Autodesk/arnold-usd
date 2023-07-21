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
#include "reader.h"

#include <ai.h>

#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/pcp/layerStack.h>
#include <pxr/usd/pcp/node.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include "read_skinning.h"
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/blendShapeQuery.h>
#include <pxr/usd/usdUtils/stageCache.h>
#include <pxr/usd/usdRender/settings.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <constant_strings.h>

#include "prim_reader.h"
#include "registry.h"

#include "rendersettings_utils.h"
#include "parameters_utils.h"
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

    // This is the class that is used to run a job from the WorkDispatcher
    struct _UsdArnoldPrimReaderJob {
        UsdPrim prim;
        UsdArnoldPrimReader *reader;
        UsdArnoldReaderContext *context;

        // function that gets executed when calling WorkDispatcher::Run
        void operator() () const {
            // use the primReader to read the input primitive, with the
            // provided context
            reader->Read(prim, *context);
            // delete the context that was created just for this job
            delete context;
        }
    };
    struct UsdThreadData {
        UsdThreadData() : threadId(0), threadCount(0), rootPrim(nullptr), 
                            context(nullptr), dispatcher(nullptr) {}

        unsigned int threadId;
        unsigned int threadCount;
        UsdPrim *rootPrim;
        UsdArnoldReaderThreadContext threadContext;
        UsdArnoldReaderContext *context;
        WorkDispatcher *dispatcher;
    };
};
// global reader registry, will be used in the default case
static UsdArnoldReaderRegistry *s_readerRegistry = nullptr;
static int s_anonymousOverrideCounter = 0;
static int s_mustDeleteRegistry = 0;

static AtMutex s_globalReaderMutex;
static std::unordered_map<int, int> s_cacheRefCount;

UsdArnoldReader::~UsdArnoldReader()
{
    if (s_mustDeleteRegistry && _registry) {
        delete _registry;
        s_mustDeleteRegistry = false;
        _registry = s_readerRegistry;
    }
    // What do we want to do at destruction here ?
    // Should we delete the created nodes in case there was no procParent ?
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
            int counter;
            {
                std::lock_guard<AtMutex> guard(s_globalReaderMutex);
                counter = s_anonymousOverrideCounter++;
            }
            std::stringstream ss;
            ss << "anonymous__override__" << counter << ".usda";
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

bool UsdArnoldReader::Read(int cacheId, const std::string &path)
{
    if (!_nodes.empty()) {
        return true;
    }
    _cacheId = cacheId;
    // Load the USD stage in memory using a cache ID
    UsdStageCache &stageCache = UsdUtilsStageCache::Get();
    UsdStageCache::Id id = UsdStageCache::Id::FromLongInt(cacheId);
   
    UsdStageRefPtr stage = (id.IsValid()) ? stageCache.Find(id) : nullptr;
    if (!stage) {
        AiMsgWarning("[usd] Cache ID not valid %d", cacheId);
        _cacheId = 0;
        return false;
    }
    ReadStage(stage, path);
    return true;
}

void UsdArnoldReader::TraverseStage(UsdPrim *rootPrim, UsdArnoldReaderContext &context, 
                                    int threadId, int threadCount,
                                    bool doPointInstancer, bool doSkelData, AtArray *matrix)
{
    UsdArnoldReaderThreadContext &threadContext = *context.GetThreadContext();
    UsdArnoldReader *reader = threadContext.GetReader();
    std::vector<std::vector<UsdGeomPrimvar> > &primvarsStack = threadContext.GetPrimvarsStack();
    UsdAttribute attr;
    const TimeSettings &time = reader->GetTimeSettings();
    float frame = time.frame;
    TfToken visibility, purpose;
    bool multithread = (threadCount > 1);
    int index = 0;
    
    int pointInstancerCount = 0;
    // Traverse the stage, either the full one, or starting from a root primitive
    // (in case an object_path is set). We need to have "pre" and "post" visits in order
    // to keep track of the primvars list at every point in the hierarchy.
    UsdPrimRange range = UsdPrimRange::PreAndPostVisit((rootPrim) ? 
                    *rootPrim : reader->GetStage()->GetPseudoRoot());
    for (auto iter = range.begin(); iter != range.end(); ++iter) {
        const UsdPrim &prim(*iter);
        bool isInstanceable = prim.IsInstanceable();

        std::string objType = prim.GetTypeName().GetText();
        // skip untyped primitives (unless they're an instance)
        if (objType.empty() && !isInstanceable)
            continue;

        // if this primitive is a point instancer, we want to hide everything below its hierarchy #458
        bool isPointInstancer = doPointInstancer && prim.IsA<UsdGeomPointInstancer>();

        bool isSkelRoot = doSkelData && prim.IsA<UsdSkelRoot>();

        // We traverse every primitive twice : once from root to leaf, 
        // then back from leaf to root. We don't want to anything during "post" visits
        // apart from popping the last element in the primvars stack.
        // This way, the last element in the stack will always match the current 
        // set of primvars
        if (iter.IsPostVisit()) {
            primvarsStack.pop_back();
            if (isPointInstancer)
            {
                if (--pointInstancerCount <= 0)
                {
                    pointInstancerCount = 0; // safety, to ensure we don't have negative values
                    threadContext.SetHidden(false);
                }                
            }
            if (isSkelRoot) {
                // FIXME make it a vector of pointers
                threadContext.ClearSkelData();
            }
            continue; 
        }
  
        if (isSkelRoot) {
            threadContext.CreateSkelData(prim);
        }
        // Get the inheritable primvars for this xform, by giving its parent ones as input
        UsdGeomPrimvarsAPI primvarsAPI(prim);
        std::vector<UsdGeomPrimvar> primvars = 
            primvarsAPI.FindIncrementallyInheritablePrimvars(primvarsStack.back());
        
        // if the returned vector is empty, we want to keep using the same list as our parent
        if (primvars.empty())
            primvarsStack.push_back(primvarsStack.back());
        else
            primvarsStack.push_back(primvars); // primvars were modified for this xform

        
        // Check if that primitive is set as being invisible.
        // If so, skip it and prune its children to avoid useless conversions
        // Special case for arnold schemas, they don't inherit from UsdGeomImageable
        // but we author these attributes nevertheless
        if (prim.IsA<UsdGeomImageable>() || objType.substr(0, 6) == "Arnold") {
            UsdGeomImageable imageable(prim);
            bool pruneChildren = false;
            attr = imageable.GetVisibilityAttr();
            if (attr && attr.HasAuthoredValue()) {

                pruneChildren |= (attr.Get(&visibility, frame) && 
                        visibility == UsdGeomTokens->invisible);
            }

            attr = imageable.GetPurposeAttr();
            if (attr && attr.HasAuthoredValue()) {
                pruneChildren |= ((attr.Get(&purpose, frame) && 
                        !purpose.IsEmpty() && 
                        purpose != UsdGeomTokens->default_ && 
                        purpose != reader->GetPurpose()));
            }
            
            if (pruneChildren) {
                iter.PruneChildren();
                continue;
            }
        }

        // Each thread only considers one primitive for every amount of threads.
        // Note that this must happen after the above visibility test, so that all 
        // threads count prims the same way
        if ((!multithread) || ((index++ + threadId) % threadCount) == 0) {
            reader->ReadPrimitive(prim, context, isInstanceable, matrix);
            // Note: if the registry didn't find any primReader, we're not prunning
            // its children nodes, but just skipping this one.
        }

        // If this prim was a point instancer, we want to skip its children
        if (isPointInstancer)
        {
            ++pointInstancerCount;
            threadContext.SetHidden(true);
        }
    }
}

unsigned int UsdArnoldReader::ReaderThread(void *data)
{
    UsdThreadData *threadData = (UsdThreadData *)data;
    if (!threadData) 
        return 0;

    UsdArnoldReaderThreadContext &threadContext = threadData->threadContext;
    UsdArnoldReader *reader = threadContext.GetReader();
    const TimeSettings &time = reader->GetTimeSettings();
    // Each thread context will have a stack of primvars vectors,
    // which represent the primvars at the current level of hierarchy.
    // Every time we find a Xform prim, we add an element to the stack 
    // with the updated primvars list. In every "post" visit, we pop the last
    // element. Thus, every time we'll read a prim, the last element of this 
    // stack will represent its input primvars that it inherits (see #282)
    std::vector<std::vector<UsdGeomPrimvar> > &primvarsStack = threadContext.GetPrimvarsStack();
    primvarsStack.clear(); 
    primvarsStack.reserve(64); // reserve first to avoid frequent memory allocations
    primvarsStack.push_back(std::vector<UsdGeomPrimvar>()); // add an empty element first
    
    // all nodes under a point instancer hierarchy need to be hidden. So during our 
    // traversal we want to count the amount of point instancers below the current hierarchy,
    // so that we can re-enable visibility when the count is back to 0 (#458)
    int pointInstancerCount = 0;
    reader->TraverseStage(threadData->rootPrim, *threadData->context, threadData->threadId, threadData->threadCount, true, true, nullptr);

    // Wait until all the jobs we started finished the translation
    if (reader->GetDispatcher())
        reader->GetDispatcher()->Wait();

    return 0;
}
unsigned int UsdArnoldReader::ProcessConnectionsThread(void *data)
{
    UsdThreadData *threadData = (UsdThreadData *)data;
    if (threadData) {
        threadData->threadContext.ProcessConnections();
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
        AiMsgWarning("%s", txt.c_str());
    }
    // If this is read through a procedural, we don't want to read
    // options, drivers, filters, etc...
    int procMask = (_procParent) ? (AI_NODE_CAMERA | AI_NODE_LIGHT | AI_NODE_SHAPE | AI_NODE_SHADER | AI_NODE_OPERATOR)
                                 : AI_NODE_ALL;

    // We want to consider the intersection of the reader's mask,
    // and the eventual procedural mask set above
    _mask = _mask & procMask;

    // eventually use a dedicated registry
    if (_registry == nullptr) {
        // No registry was set (default), let's use the global one
        {
            std::lock_guard<AtMutex> guard(s_globalReaderMutex);
            if (s_readerRegistry == nullptr) {
                s_readerRegistry = new UsdArnoldReaderRegistry(); // initialize the global registry
                s_readerRegistry->RegisterPrimitiveReaders();
            }
        }
        _registry = s_readerRegistry;
    } else
        _registry->RegisterPrimitiveReaders();

    UsdPrim *rootPrimPtr = nullptr;

    if (!path.empty()) {
        SdfPath sdfPath(path);
        _hasRootPrim = true;
        _rootPrim = _stage->GetPrimAtPath(sdfPath);

        // If this primitive is a prototype, then its name won't be consistent between sessions 
        // (/__Prototype1 , /__Prototype2, etc...), it will therefore cause random results.
        // In this case, we'll have stored a user data "parent_instance", with the name of a parent
        // instanceable prim pointing to this prototype. It will allow us to find the expected prototype.
        // Note that we don't want to do this if we have a cacheId, as in this case the prototype is 
        // already the correct one
        if (_cacheId == 0 && _procParent && _rootPrim &&
#if PXR_VERSION >= 2011
                _rootPrim.IsPrototype()
#else
                _rootPrim.IsMaster()
#endif
                && AiNodeLookUpUserParameter(_procParent, str::parent_instance)) {
            
            AtString parentInstance = AiNodeGetStr(_procParent, str::parent_instance); 
            UsdPrim parentInstancePrim = _stage->GetPrimAtPath(SdfPath(parentInstance.c_str()));
            if (parentInstancePrim) {
                // our usd procedural has a uer-data "parent_instance" which returns the name of
                // the instanceable prim. We want to check what is its prototype
#if PXR_VERSION >= 2011
                auto proto = parentInstancePrim.GetPrototype();
#else
                auto proto = parentInstancePrim.GetMaster();
#endif
                if (proto) {
                    // We found a prototype, this is the primitive we want to use as a root prim
                    _rootPrim = proto;
                }

            }            
        }

        if (!_rootPrim) {
            AiMsgError(
                "[usd] %s : Object Path %s is not valid", (_procParent) ? AiNodeGetName(_procParent) : "",
                path.c_str());
            return;
        }
        if (!_rootPrim.IsActive()) {
            AiMsgWarning(
                "[usd] %s : Object Path primitive %s is not active", (_procParent) ? AiNodeGetName(_procParent) : "",
                path.c_str());
            return;   
        }
        rootPrimPtr = &_rootPrim;
    } else {
        _hasRootPrim = false;
    }

    // If there is not parent procedural, and we need to lookup the options, then we first need to find the
    // render camera and check its shutter, in order to know if we need to read motion data or not (#346)
    if (_procParent == nullptr) {
        ChooseRenderSettings(_stage, _renderSettings, _time, rootPrimPtr);
        if (!_renderSettings.empty()) {
            auto prim = _stage->GetPrimAtPath(SdfPath(_renderSettings));
            ComputeMotionRange(_stage, prim, _time);
        }
    }

    // Apply eventual skinning in the scene, for the desired time interval
    UsdPrimRange range = (rootPrimPtr) ? UsdPrimRange(*rootPrimPtr) : _stage->Traverse();
    // we want to slightly extend the interval to bake the skinning, in order to
    // include the surrounding integer frames #951
    GfInterval interval(std::floor(_time.start()), std::ceil(_time.end()));

    // Apply the skinning to the whole scene. Note that we don't want to do this
    // with a cache id since the usd stage is owned by someone else and we 
    // shouldn't modify it
    /*
    if (_cacheId == 0)
        ArnoldUsdSkelBakeSkinning(range, interval);
        */
    

    size_t threadCount = _threadCount; // do we want to do something
                                       // automatic when threadCount = 0 ?

    // If threads = 0, we'll start a single thread to traverse the stage,
    // and every time it finds a primitive to translate it will run a 
    // WorkDispatcher job. 
    if (threadCount == 0) {
        threadCount = 1;
        _dispatcher = new WorkDispatcher();
    }

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
        threadData[i].threadContext.SetReader(this);
        threadData[i].rootPrim = rootPrimPtr;
        threadData[i].threadContext.SetDispatcher(_dispatcher);
        threadData[i].context = new UsdArnoldReaderContext(&threadData[i].threadContext);
        threads[i] = AiThreadCreate(UsdArnoldReader::ReaderThread, &threadData[i], AI_PRIORITY_HIGH);
    }

    // Wait until all threads are finished and merge all the nodes that
    // they have created to our list
    for (size_t i = 0; i < threadCount; ++i) {
        AiThreadWait(threads[i]);
        AiThreadClose(threads[i]);
        UsdArnoldReaderThreadContext &context = threadData[i].threadContext;
        _nodes.insert(_nodes.end(), context.GetNodes().begin(), context.GetNodes().end());
        _nodeNames.insert(context.GetNodeNames().begin(), context.GetNodeNames().end());
        _lightLinksMap.insert(context.GetLightLinksMap().begin(), context.GetLightLinksMap().end());
        _shadowLinksMap.insert(context.GetShadowLinksMap().begin(), context.GetShadowLinksMap().end());
        context.GetNodes().clear();
        context.GetNodeNames().clear();
        context.GetLightLinksMap().clear();
        context.GetShadowLinksMap().clear();
        threads[i] = nullptr;
    }

    // Clear the dispatcher here as we no longer need it.
    if (_dispatcher) {
        delete _dispatcher;
        _dispatcher = nullptr;
    }

    // In a second step, each thread goes through the connections it stacked
    // and processes them given that now all the nodes were supposed to be created.
    _readStep = READ_PROCESS_CONNECTIONS;
    for (size_t i = 0; i < threadCount; ++i) {
        // now I just want to append the links from each thread context
        threads[i] = AiThreadCreate(UsdArnoldReader::ProcessConnectionsThread, &threadData[i], AI_PRIORITY_HIGH);
    }
    std::vector<UsdArnoldReaderThreadContext::Connection> danglingConnections;
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
            danglingConnections.end(), threadData[i].threadContext.GetConnections().begin(),
            threadData[i].threadContext.GetConnections().end());
        threadData[i].threadContext.GetConnections().clear();
    }
    
    // 3rd step, in case some links were pointing to nodes that didn't exist.
    // If they were skipped because of their visibility, we need to force
    // their export now. We handle this in a single thread to avoid costly
    // synchronizations between the threads.
    _readStep = READ_DANGLING_CONNECTIONS;
    if (!danglingConnections.empty()) {
        // We only use the first thread context
        UsdArnoldReaderThreadContext &context = threadData[0].threadContext;
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
                    ReadPrimitive(prim, *threadData[0].context);
            }
            // we can now process the connection
            context.ProcessConnection(conn);
        }
        // Some nodes were possibly created in the above loop,
        // we need to append them to our reader
        _nodes.insert(_nodes.end(), context.GetNodes().begin(), context.GetNodes().end());
        _nodeNames.insert(context.GetNodeNames().begin(), context.GetNodeNames().end());
        _lightLinksMap.insert(context.GetLightLinksMap().begin(), context.GetLightLinksMap().end());
        _shadowLinksMap.insert(context.GetShadowLinksMap().begin(), context.GetShadowLinksMap().end());
        context.GetNodeNames().clear();
        context.GetNodes().clear();
        context.GetLightLinksMap().clear();
        context.GetShadowLinksMap().clear();
    }

    // Finally, process all the light links
    ReadLightLinks();

    for (size_t i = 0; i < threadCount; ++i) {
        delete threadData[i].context;
    }

    if (_cacheId != 0) {
        std::lock_guard<AtMutex> guard(s_globalReaderMutex);        
        const auto &cacheIter = s_cacheRefCount.find(_cacheId);
        if (cacheIter != s_cacheRefCount.end()) {
            if (--cacheIter->second <= 0) {
                UsdStageCache &stageCache = UsdUtilsStageCache::Get();                
                s_cacheRefCount.erase(cacheIter);
                stageCache.Erase(UsdStageCache::Id::FromLongInt(_cacheId));
                _cacheId = 0;
                // now our reader will have a cacheID
            }
        }
    }
    _stage = UsdStageRefPtr(); // clear the shared pointer, delete the stage
    _readStep = READ_FINISHED; // We're done
}

void UsdArnoldReader::ReadPrimitive(const UsdPrim &prim, UsdArnoldReaderContext &context, bool isInstance, AtArray *parentMatrix)
{
    std::string objName = prim.GetPath().GetText();
    const TimeSettings &time = context.GetTimeSettings();

    std::string objType = prim.GetTypeName().GetText();
    UsdArnoldReaderThreadContext *threadContext = context.GetThreadContext();
    if (isInstance) {
#if PXR_VERSION >= 2011
        auto proto = prim.GetPrototype();
#else
        auto proto = prim.GetMaster();
#endif
        if (proto) {
            if (threadContext->GetSkelData()) {
                // if we need to apply skinning to this instance, then we need to expand it
                AtArray *matrix = ReadMatrix(prim, context.GetTimeSettings(), context, prim.IsA<UsdGeomXformable>());
                const std::string prevPrototypeName = context.GetPrototypeName();
                context.SetPrototypeName(prim.GetPath().GetText());
                TraverseStage(&proto, context, 0, 0, false, false, matrix);
                AiArrayDestroy(matrix);
                context.SetPrototypeName(prevPrototypeName);
                return;
            }
            AtArray *protoMatrix = nullptr;
            AtNode *ginstance = context.CreateArnoldNode("ginstance", objName.c_str());
            if (prim.IsA<UsdGeomXformable>()) {
                ReadMatrix(prim, ginstance, time, context);
                if (protoMatrix) {
                    // for each key, divide the ginstance matrix by the protoMatrix
                    AtArray *gMtx = AiNodeGetArray(ginstance, str::matrix);
                    size_t numKeys = AiArrayGetNumKeys(gMtx);
                    size_t numProtoKeys = AiArrayGetNumKeys(protoMatrix);
                    for (size_t i = 0; i < numKeys; ++i) {
                        AtMatrix mtx = AiArrayGetMtx(gMtx, i);
                        AtMatrix protoInvMtx = AiM4Invert(AiArrayGetMtx(protoMatrix, AiMax(i, numProtoKeys - 1)));
                        mtx = AiM4Mult(protoInvMtx, mtx);
                        AiArraySetMtx(gMtx, i, mtx);
                    }
                }
            }
            if (protoMatrix)
                AiArrayDestroy(protoMatrix);

            AiNodeSetFlt(ginstance, str::motion_start, time.motionStart);
            AiNodeSetFlt(ginstance, str::motion_end, time.motionEnd);
            // if this instanceable prim is under the hierarchy of a point instancer it should be hidden
            AiNodeSetByte(ginstance, str::visibility, context.GetThreadContext()->IsHidden() ? 0 : AI_RAY_ALL);
            AiNodeSetBool(ginstance, str::inherit_xform, false);
            {
                // Read primvars assigned to this instance prim
                // We need to use a context that will have the proper primvars stack
                UsdArnoldReaderContext jobContext(context, nullptr, context.GetThreadContext()->GetPrimvarsStack().back(), 
                    threadContext->IsHidden(), nullptr);
                // Read both the regular primvars and also the arnold primvars (#1100) that can be used for matte, etc...
                ReadPrimvars(prim, ginstance, time, jobContext);
                ReadArnoldParameters(prim, jobContext, ginstance, time, "primvars:arnold");
            }
            
            // Add a connection from this instance to the prototype. It's likely not going to be
            // Arnold, and will therefore appear as a "dangling" connection. The prototype will
            // therefore be created by a single thread in ProcessConnection. Given that this prim
            // is a prototype, it will be created as a nested usd procedural with object path set 
            // to the protoype prim's name. This will support instances of hierarchies.
            context.AddConnection(
                        ginstance, "node", proto.GetPath().GetText(), ArnoldAPIAdapter::CONNECTION_PTR);
            return;
        }
    }        


    // We want to ensure we only read a single RenderSettings prim. So we compare
    // if the path provided to the reader. If nothing was set, we'll just look 
    // for the first RenderSettings in the stage
    if (prim.IsA<UsdRenderSettings>()) {
        if (!_renderSettings.empty() && _renderSettings != objName)
            return;
        _renderSettings = objName;
    }

    UsdArnoldPrimReader *primReader = _registry->GetPrimReader(objType);
    if (primReader && (_mask & primReader->GetType())) {
        if (_debug) {
            std::string txt;

            txt += "Object ";
            txt += objName;
            txt += " (type: ";
            txt += objType;
            txt += ")";

            AiMsgInfo("%s", txt.c_str());
        }

        if (_dispatcher) {
            AtArray *matrix = ReadMatrix(prim, context.GetTimeSettings(), context, prim.IsA<UsdGeomXformable>());
            // Read the matrix
            if (parentMatrix)
                ApplyParentMatrices(matrix, parentMatrix);
            UsdArnoldReaderContext *jobContext = new UsdArnoldReaderContext(context, matrix, threadContext->GetPrimvarsStack().back(), 
                        threadContext->IsHidden(), threadContext->GetSkelData() ? new UsdArnoldSkelData(*threadContext->GetSkelData()) : nullptr);

            _UsdArnoldPrimReaderJob job = 
                {prim, primReader, jobContext };
                
            _dispatcher->Run(job);
        } else {
            AtArray *prevMatrices = nullptr;
            AtArray *newMatrices = nullptr;
            if (parentMatrix) {
                prevMatrices = context.GetMatrices();
                newMatrices = ReadMatrix(prim, context.GetTimeSettings(), context, prim.IsA<UsdGeomXformable>());
                ApplyParentMatrices(newMatrices, parentMatrix);
                context.SetMatrices(newMatrices);
            }
            primReader->Read(prim, context); // read this primitive
            if (parentMatrix) {
                context.SetMatrices(prevMatrices);
                AiArrayDestroy(newMatrices);
            }
        }
    }
}
void UsdArnoldReader::SetThreadCount(unsigned int t)
{
    _threadCount = t;
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
    _nodeNames.clear();
    _defaultShader = nullptr; // reset defaultShader
}

void UsdArnoldReader::SetProceduralParent(AtNode *node)
{
    // should we clear the nodes when a new procedural parent is set ?
    ClearNodes();
    _procParent = node;
    _universe = (node) ? AiNodeGetUniverse(node) : nullptr;
}

void UsdArnoldReader::CreateViewportRegistry(AtProcViewportMode mode, const AtParamValueMap* params) {
    s_mustDeleteRegistry = true;
     _registry = new UsdArnoldViewportReaderRegistry(mode, params);
}

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
        _defaultShader = AiNode(_universe, AtString("standard_surface"), AtString("_default_arnold_shader"), _procParent);
        AtNode *userData = AiNode(_universe, AtString("user_data_rgb"), AtString("_default_arnold_shader_color"), _procParent);
        _nodes.push_back(_defaultShader);
        _nodes.push_back(userData);
        AiNodeSetStr(userData, str::attribute, AtString("displayColor"));
        AiNodeSetRGB(userData, str::_default, 1.f, 1.f, 1.f); // neutral white shader if no user data is found
        AiNodeLink(userData, str::base_color, _defaultShader);
    }

    UnlockReader();

    return _defaultShader;
}

// Process eventual light links info, and apply them to the 
// appropriate shapes
void UsdArnoldReader::ReadLightLinks()
{    
    if (_lightLinksMap.empty() && _shadowLinksMap.empty()) {
        return;
    }
        
    // First compute the list of created lights and shapes
    std::vector<AtNode*> lightsList;
    std::vector<AtNode *> shapeList;
    for (auto node : _nodes) {
        int type = AiNodeEntryGetType(AiNodeGetNodeEntry(node));
        if (type == AI_NODE_LIGHT) {
            lightsList.push_back(node);
        } else if (type == AI_NODE_SHAPE) {
            shapeList.push_back(node);
        }
    }

    // store a vector that will be cleared and reused for each shape
    std::vector<AtNode *> shapeLightGroups;
    shapeLightGroups.reserve(lightsList.size());

    auto GetLinksMap = [](std::unordered_map<std::string, UsdCollectionAPI> &linksMap, AtNode *shape,
        const std::vector<AtNode *> &lightsList, const std::unordered_map<std::string, AtNode *> &namesMap, 
        std::vector<AtNode *> &shapeLightGroups) 
    { 

        shapeLightGroups.clear();
        std::string shapeName = AiNodeGetName(shape);
        
        // loop over the lights list, to check which apply to this shape
        for (auto light : lightsList) {
            bool foundShape = false;
            auto it = linksMap.find(AiNodeGetName(light));
            if (it == linksMap.end()) {
                // light not found in the list, it affects all meshes (default behaviour)
                foundShape = true;
            } else {
                // this light has a light links collection, we need to check if it affects
                // the current shape
                const UsdCollectionAPI &collection = it->second;
                VtValue includeRootValue;
                bool includeRoot = (collection.GetIncludeRootAttr().Get(&includeRootValue)) ? VtValueGetBool(includeRootValue) : false;
                
                if (includeRoot) {
                    // we're including the layer root, add all lights to the list
                    foundShape = true;
                } else {
                    SdfPathVector includeTargets;
                    // Get the list of targets included in this collection
                    collection.GetIncludesRel().GetTargets(&includeTargets);
                    UsdStageRefPtr stage = collection.GetPrim().GetStage();
                    for (size_t i = 0; i < includeTargets.size(); ++i) {
                        std::string shapeTargetName = includeTargets[i].GetText();
                        // we need to check if this usd shape from the collection 
                        // is the one we're dealing with. There can be a naming remapping though
                        // between usd and arnold. 

                        // First we compare the name directly                                
                        if (shapeTargetName == shapeName) {
                            foundShape = true;
                            break;
                        } else if (shapeName.length() > shapeTargetName.length() + 1 &&
                             shapeName.substr(0, shapeTargetName.length() + 1) == shapeTargetName + std::string("/")) {
                            // Here the inclusion target path is part of the current shape path, which means that it
                            // should affect us. We need to include this shape
                            foundShape = true;
                            break;
                        } 

                        // USD allows to use a collection with an "instance name" with the format 
                        // {collectionName}.collection:{instanceName}
                        // In that case, we want to propagate the list of includes to the proper "instance"
                        static const std::string s_subCollectionToken(".collection:");
                        size_t collectionPos = shapeTargetName.find(s_subCollectionToken);
                        // Since this is a specific usd format, we check if it's present in the target path
                        if (collectionPos != std::string::npos && collectionPos > 0) {
                            std::string collectionPath = shapeTargetName.substr(0, collectionPos);
                            // The first part of the path should represent a primitive
                            UsdPrim shapeTargetRoot = stage->GetPrimAtPath(SdfPath(collectionPath));
                            if (shapeTargetRoot) {
                                // Then we can use the UsdCollectionAPI with a specific "instanceName"
                                // since the collection is a "multiple-apply API schema"
                                UsdCollectionAPI subCollection(shapeTargetRoot, 
                                    TfToken(shapeTargetName.substr(collectionPos + s_subCollectionToken.length())));
                                if (subCollection) {
                                    // we found the nested collection, we just want to append its includes 
                                    // to the end of our current list so that they're taken into account
                                    // later in this loop
                                    SdfPathVector subCollectionIncludes;
                                    subCollection.GetIncludesRel().GetTargets(&subCollectionIncludes);
                                    includeTargets.insert(includeTargets.end(), subCollectionIncludes.begin(), subCollectionIncludes.end());
                                } 
                            }
                        }
                        
                        // Otherwise, check with the naming map to recognize the shape name
                        auto shapeIt = namesMap.find(shapeTargetName);
                        if (shapeIt != namesMap.end() && shapeIt->second == shape) {
                            foundShape = true;
                            break;
                        }
                    }
                }
                // The light doesn't affect this shape
                if (!foundShape) {
                    continue;
                }

                // At this point, we know the current shape was included in the collection,
                // now let's check if it should be excluded from it
                SdfPathVector excludeTargets;
                collection.GetExcludesRel().GetTargets(&excludeTargets);
                for (size_t i = 0; i < excludeTargets.size(); ++i) {
                    std::string shapeTargetName = excludeTargets[i].GetText();
                    if (shapeTargetName == shapeName) {
                        foundShape = false;
                        break;
                    } else if (shapeName.length() > shapeTargetName.length() + 1 &&
                             shapeName.substr(0, shapeTargetName.length() + 1) == shapeTargetName + std::string("/")) {
                        // Here the exclusion target path is included in the current shape path, which means that it
                        // should affect us. We need to exclude this shape
                        foundShape = false;
                        break;
                    }

                    auto shapeIt = namesMap.find(shapeTargetName);
                    if (shapeIt != namesMap.end() && shapeIt->second == shape) {
                        foundShape = false;
                        break;
                    }
                }                
            }
            if (foundShape) {
                // We finally know that this light is visible to the current shape
                // so we want to add it to the list
                shapeLightGroups.push_back(light);
            }
        }
    };

    // Light-links
    if (!_lightLinksMap.empty())
    {        
        for (auto shape : shapeList) {
            
            GetLinksMap(_lightLinksMap, shape, lightsList, _nodeNames, shapeLightGroups);
            // We checked all lights in the scene, and found which ones were visible for the 
            // current shape. If the list size is smaller than the full lights list, then
            // we need to set the light_group attribute in the arnold shape node
            if (shapeLightGroups.size() < lightsList.size()) {
                AiNodeSetBool(shape, str::use_light_group, true);
                if (!shapeLightGroups.empty()) {
                    AiNodeSetArray(shape, str::light_group, AiArrayConvert(shapeLightGroups.size(), 1, AI_TYPE_NODE, &shapeLightGroups[0]));
                }
            }
        }            
    }

    // Shadow-links
    if (!_shadowLinksMap.empty())
    {
        for (auto shape : shapeList) {

            GetLinksMap(_shadowLinksMap, shape, lightsList, _nodeNames, shapeLightGroups);
            if (shapeLightGroups.size() < lightsList.size()) {
                AiNodeSetBool(shape, str::use_shadow_group, true);
                if (!shapeLightGroups.empty()) {
                    AiNodeSetArray(shape, str::shadow_group, AiArrayConvert(shapeLightGroups.size(), 1, AI_TYPE_NODE, &shapeLightGroups[0]));
                }
            }
        }
    }
}
void UsdArnoldReader::InitCacheId()
{
    // cache ID was already set, nothing to do
    if (_cacheId != 0)
        return;

    // Get a UsdStageCache, insert our current usd stage,
    // and get its ID
    std::lock_guard<AtMutex> guard(s_globalReaderMutex);
    UsdStageCache &stageCache = UsdUtilsStageCache::Get();
    UsdStageCache::Id id = stageCache.Insert(_stage);
    // now our reader will have a cacheID
    _cacheId = id.ToLongInt();
    // there's one ref count for this cache ID, for the current proc
    s_cacheRefCount[_cacheId] = 1; 
}
// Return a AtNode representing a whole part of the scene hierarchy, as needed e.g. for instancing.
// In this case, we create a nested procedural and give it an "object_path" so that it only represents 
// a part of the whole usd stage
AtNode *UsdArnoldReader::CreateNestedProc(const char *objectPath, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
     std::string childUsdEntry = "usd";
    // if the parent procedural has a different type (e.g usd_cache_proc in MtoA)
    // then we want to create a nested proc of the same type
    if (_procParent)
        childUsdEntry = AiNodeEntryGetName(AiNodeGetNodeEntry(_procParent));

    AtNode *proto = context.CreateArnoldNode(childUsdEntry.c_str(), objectPath);
    AiNodeSetStr(proto, str::filename, AtString(_filename.c_str()));

    if (_cacheId == 0) {
        // this reader doesn't have any cache Id. However, we want to create one for its nested procs
        InitCacheId(); 
    }
    {
        // Now increment the ref count for this cache ID
        std::lock_guard<AtMutex> guard(s_globalReaderMutex);
        const auto &cacheIdIter = s_cacheRefCount.find(_cacheId);
        if (cacheIdIter != s_cacheRefCount.end())
            cacheIdIter->second++;        
    }
    
    AiNodeSetInt(proto, str::cache_id, _cacheId);
    AiNodeSetStr(proto, str::object_path, AtString(objectPath));
    AiNodeSetFlt(proto, str::frame, time.frame); // give it the desired frame
    AiNodeSetFlt(proto, str::motion_start, time.motionStart);
    AiNodeSetFlt(proto, str::motion_end, time.motionEnd);
    if (_overrides)
        AiNodeSetArray(proto, str::overrides, AiArrayCopy(_overrides));

    // This procedural is created in addition to the original hierarchy traversal
    // so we always want it to be hidden to avoid duplicated geometries. 
    // We just want the instances to be visible eventually
    AiNodeSetByte(proto, str::visibility, 0);
    AiNodeSetInt(proto, str::threads, _threadCount);
    return proto;
}

UsdArnoldReaderThreadContext::~UsdArnoldReaderThreadContext()
{
    if (_xformCache)
        delete _xformCache;

    for (std::unordered_map<float, UsdGeomXformCache *>::iterator it = _xformCacheMap.begin();
         it != _xformCacheMap.end(); ++it)
        delete it->second;

    _xformCacheMap.clear();

    ClearSkelData();
}
void UsdArnoldReaderThreadContext::SetReader(UsdArnoldReader *r)
{
    if (r == nullptr)
        return; // shouldn't happen
    _reader = r;
    // UsdGeomXformCache will be used to trigger world transformation matrices
    // by caching the already computed nodes xforms in the hierarchy.
    if (_xformCache == nullptr)
        _xformCache = new UsdGeomXformCache(UsdTimeCode(r->GetTimeSettings().frame));
}
void UsdArnoldReaderThreadContext::AddNodeName(const std::string &name, AtNode *node)
{
    if (_dispatcher)
        _addNodeNameLock.lock();
    _nodeNames[name] = node;
    if (_dispatcher)
        _addNodeNameLock.unlock();
}

void UsdArnoldReaderThreadContext::SetDispatcher(WorkDispatcher *dispatcher)
{

    _dispatcher = dispatcher;
}

AtNode *UsdArnoldReaderThreadContext::CreateArnoldNode(const char *type, const char *name)
{    
    AtNode *node = AiNode(_reader->GetUniverse(), AtString(type), AtString(name), _reader->GetProceduralParent());
    // All shape nodes should have an id parameter if we're coming from a parent procedural
    if (_reader->GetProceduralParent() && AiNodeEntryGetType(AiNodeGetNodeEntry(node)) == AI_NODE_SHAPE) {
        AiNodeSetUInt(node, str::id, _reader->GetId());
    }

    if (_dispatcher)
        _createNodeLock.lock();
    _nodes.push_back(node);
    if (_dispatcher)
        _createNodeLock.unlock();

    return node;
}
void UsdArnoldReaderThreadContext::AddConnection(
    AtNode *source, const std::string &attr, const std::string &target, ConnectionType type, 
    const std::string &outputElement)
{
    if (_reader->GetReadStep() == UsdArnoldReader::READ_TRAVERSE) {
        // store a link between attributes/nodes to process it later
        // If we have a dispatcher, we want to lock here
        if (_dispatcher)
            _addConnectionLock.lock();

        _connections.push_back(Connection());
        Connection &conn = _connections.back();
        conn.sourceNode = source;
        conn.sourceAttr = attr;
        conn.target = target;
        conn.type = type;
        conn.outputElement = outputElement;

        if (_dispatcher)
            _addConnectionLock.unlock();

    } else if (_reader->GetReadStep() == UsdArnoldReader::READ_DANGLING_CONNECTIONS) {
        // we're in the main thread, processing the dangling connections. We want to
        // apply the connection right away
        Connection conn;
        conn.sourceNode = source;
        conn.sourceAttr = attr;
        conn.target = target;
        conn.type = type;
        conn.outputElement = outputElement;
        ProcessConnection(conn);
    }
}
void UsdArnoldReaderThreadContext::ProcessConnections()
{
    _primvarsStack.clear();
    _primvarsStack.push_back(std::vector<UsdGeomPrimvar>());

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

bool UsdArnoldReaderThreadContext::ProcessConnection(const Connection &connection)
{
    UsdArnoldReader::ReadStep step = _reader->GetReadStep();
    if (connection.type == UsdArnoldReaderThreadContext::CONNECTION_ARRAY) {
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
                        // We need to compute the full list of primvars, including 
                        // inherited ones. 
                        UsdGeomPrimvarsAPI primvarsAPI(prim);
                        _primvarsStack.back() = primvarsAPI.FindPrimvarsWithInheritance();
                        UsdArnoldReaderContext context(this);
                        _reader->ReadPrimitive(prim, context);
                        target = _reader->LookupNode(token.c_str(), true);
                    }
                }
                if (target == nullptr)
                    return false; // node is missing, we don't process the connection
            }
            vecNodes.push_back(target);
        }
        AiNodeSetArray(
            connection.sourceNode, AtString(connection.sourceAttr.c_str()),
            AiArrayConvert(vecNodes.size(), 1, AI_TYPE_NODE, &vecNodes[0]));

    } else {
        AtNode *target = _reader->LookupNode(connection.target.c_str(), true);
        if (target == nullptr) {
            if (step == UsdArnoldReader::READ_DANGLING_CONNECTIONS) {
                // generate the missing node right away
                SdfPath sdfPath(connection.target.c_str());
                UsdPrim prim = _reader->GetStage()->GetPrimAtPath(sdfPath);
                if (prim) {
                    UsdGeomPrimvarsAPI primvarsAPI(prim);
                    // We need to compute the full list of primvars, including 
                    // inherited ones. 
                    _primvarsStack.back() = primvarsAPI.FindPrimvarsWithInheritance();
                    UsdArnoldReaderContext context(this);
                    _reader->ReadPrimitive(prim, context);
                    target = _reader->LookupNode(connection.target.c_str(), true);

                    if (target == nullptr && connection.type == ArnoldAPIAdapter::CONNECTION_PTR &&
#if PXR_VERSION >= 2011
                        prim.IsPrototype()
#else
                        prim.IsMaster()
#endif
                        ) {
                        
                        // Since the instance can represent any point in the hierarchy, including
                        // xforms that aren't translated to arnold, we need to create a nested
                        // usd procedural that will only read this specific prim. Note that this 
                        // is similar to what is done by the point instancer reader

                        target = _reader->CreateNestedProc(connection.target.c_str(), context);

                        // First time we create the nested proc, we want to add a user data with the first 
                        // instanceable prim pointing to it
                        // Declare the user data
                        AiNodeDeclare(target, str::parent_instance, "constant STRING");
                        AiNodeSetStr(target, str::parent_instance, AiNodeGetName(connection.sourceNode));
                    }
                }
            }
            if (target == nullptr) {
                return false; // node is missing, we don't process the connection
            }
        }
        if (connection.type == ArnoldAPIAdapter::CONNECTION_PTR) {
            if (connection.sourceAttr.back() == ']' ) {
                std::stringstream ss(connection.sourceAttr);
                std::string arrayAttr, arrayIndexStr;
                if (std::getline(ss, arrayAttr, '[') && std::getline(ss, arrayIndexStr, ']')) {
                    int arrayIndex = std::stoi(arrayIndexStr);
                    AtArray *array = AiNodeGetArray(connection.sourceNode,
                                            AtString(arrayAttr.c_str()));
                    if (array == nullptr) {
                        array = AiArrayAllocate(arrayIndex + 1, 1, AI_TYPE_POINTER);
                        for (unsigned i=0; i<(unsigned) arrayIndex; i++)
                            AiArraySetPtr(array, i, nullptr);
                        AiArraySetPtr(array, arrayIndex, (void *) target);
                        AiNodeSetArray(connection.sourceNode, AtString(connection.sourceAttr.c_str()), array);
                    }
                    else if (arrayIndex >= (int) AiArrayGetNumElements(array)) {
                        unsigned numElements = AiArrayGetNumElements(array);
                        AiArrayResize(array, arrayIndex + 1, 1);
                        for (unsigned i=numElements; i<(unsigned) arrayIndex; i++)
                            AiArraySetPtr(array, i, nullptr);
                        AiArraySetPtr(array, arrayIndex, (void *) target);
                    }
                    else
                        AiArraySetPtr(array, arrayIndex, (void *)target);
                }
            } else
                AiNodeSetPtr(connection.sourceNode, AtString(connection.sourceAttr.c_str()), (void *)target);
        }
        else if (connection.type == ArnoldAPIAdapter::CONNECTION_LINK) {

            if (target == nullptr) {
                AiNodeUnlink(connection.sourceNode, AtString(connection.sourceAttr.c_str()));
            } else {

                static const std::string supportedElems ("xyzrgba");
                const std::string &elem = connection.outputElement;
                // Connection to an output component
                const AtNodeEntry *targetEntry = AiNodeGetNodeEntry(target);
#if ARNOLD_VERSION_NUM >= 70000
                int noutputs = AiNodeEntryGetNumOutputs(targetEntry);
#else
                int noutputs = 1;
#endif

                if (noutputs > 1 && elem.find(':') != std::string::npos)
                {
                    std::string outputName = elem.substr(elem.find(':') + 1);
                    if (AiNodeIs(target, str::osl))
                        outputName = "param_" + outputName;

                    if (!AiNodeLinkOutput(target, outputName.c_str(), connection.sourceNode, connection.sourceAttr.c_str()))
                        AiNodeLink(target, AtString(connection.sourceAttr.c_str()), connection.sourceNode);

                } else if (elem.length() > 1 && elem[elem.length() - 2] == ':' && supportedElems.find(elem.back()) != std::string::npos) {
                     // check for per-channel connection 
                    AiNodeLinkOutput(target, std::string(1,elem.back()).c_str(), connection.sourceNode, connection.sourceAttr.c_str());
                } else {
                    AiNodeLink(target, AtString(connection.sourceAttr.c_str()), connection.sourceNode);
                }
            }            
        }
    }
    return true;
}
void UsdArnoldReaderThreadContext::RegisterLightLinks(const std::string &lightName, const UsdCollectionAPI &collectionAPI)
{
    // If we have a dispatcher, we want to lock here
    if (_dispatcher)
        _addConnectionLock.lock();
    _lightLinksMap[lightName] = collectionAPI;
    if (_dispatcher)
        _addConnectionLock.unlock();
}

void UsdArnoldReaderThreadContext::RegisterShadowLinks(const std::string &lightName, const UsdCollectionAPI &collectionAPI)
{
    // If we have a dispatcher, we want to lock here
    if (_dispatcher)
        _addConnectionLock.lock();
    _shadowLinksMap[lightName] = collectionAPI; 
    if (_dispatcher)
        _addConnectionLock.unlock();
}


UsdGeomXformCache *UsdArnoldReaderThreadContext::GetXformCache(float frame)
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
    if (IsHidden())
        return false;
    UsdArnoldReader *reader = _threadContext->GetReader();
    // Only compute the visibility when processing the dangling connections,
    // otherwise we return true to avoid costly computation.
    if (reader->GetReadStep() == UsdArnoldReader::READ_DANGLING_CONNECTIONS) {
        return IsPrimVisible(prim, reader, frame);
    }
    
    return true;
}

 
