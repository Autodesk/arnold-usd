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
#include <pxr/usd/usdShade/nodeGraph.h>

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


static AtMutex s_globalReaderMutex;
static std::unordered_map<long int, int> s_cacheRefCount;
UsdArnoldReader::UsdArnoldReader()
        : _procParent(nullptr),
          _universe(nullptr),
          _convert(true),
          _debug(false),
          _threadCount(1),
          _mask(AI_NODE_ALL),
          _defaultShader(nullptr),
          _hasRootPrim(false),
          _readStep(READ_NOT_STARTED),
          _purpose(UsdGeomTokens->render),
          _dispatcher(nullptr),
          _readerRegistry(new UsdArnoldReaderRegistry())
    {}
UsdArnoldReader::~UsdArnoldReader()
{
    delete _readerRegistry;
    // If a TfNotice callback was used, we want to revoke it here
    if (_interactive && _objectsChangedNoticeKey.IsValid()) {
        TfNotice::Revoke(_objectsChangedNoticeKey);
    }
}

void UsdArnoldReader::TraverseStage(UsdPrim *rootPrim, UsdArnoldReaderContext &context, 
                                    int threadId, int threadCount,
                                    bool doPointInstancer, bool doSkelData, AtArray *matrix)
{    
    // Traverse the stage, either the full one, or starting from a root primitive
    // (in case an object_path is set). We need to have "pre" and "post" visits in order
    // to keep track of the primvars list at every point in the hierarchy.
    auto TraverseNodes = [](UsdPrimRange& range, UsdArnoldReaderContext &context, int threadId, int threadCount,
                                    bool doPointInstancer, bool doSkelData, AtArray *matrix, std::unordered_set<SdfPath, TfHash> *includeNodes = nullptr)
    {
        UsdArnoldReaderThreadContext &threadContext = *context.GetThreadContext();
        UsdArnoldReader *reader = threadContext.GetReader();
        TfToken visibility, purpose;
        bool multithread = (threadCount > 1);
        int index = 0;        
        int pointInstancerCount = 0;
        std::vector<std::vector<UsdGeomPrimvar> > &primvarsStack = threadContext.GetPrimvarsStack();
        UsdAttribute attr;
        const TimeSettings &time = reader->GetTimeSettings();
        int includeNodesCount = 0;
        float frame = time.frame;

        SdfPathVector updateHiddenNodes;
        
        for (auto iter = range.begin(); iter != range.end(); ++iter) {
            const UsdPrim &prim(*iter);
            bool isInstanceable = prim.IsInstanceable();
            bool isIncludedNode = false;
                        
            if (includeNodes && includeNodes->find(prim.GetPath()) != includeNodes->end()) {
                isIncludedNode = true;
                // We have a dirty nodes filter, and this primitive is inside of it.
                if (iter.IsPostVisit()) 
                    includeNodesCount = std::max(0, includeNodesCount -1);
                else
                    includeNodesCount++;
            }

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
                if (!updateHiddenNodes.empty() && updateHiddenNodes.back() == prim.GetPath()) {
                    updateHiddenNodes.pop_back();
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
                
                if (pruneChildren ) {

                    if (isIncludedNode) {
                        updateHiddenNodes.push_back(prim.GetPath());
                    }
                    // Only prune this primitive's children if we're not updating some hidden nodes,
                    // otherwise we need to ensure they're properly translated in order to eventually force
                    // their visibility to be hidden
                    if (updateHiddenNodes.empty()) {
                        iter.PruneChildren();
                        continue;
                    }
                }
            }

            // Each thread only considers one primitive for every amount of threads.
            // Note that this must happen after the above visibility test, so that all 
            // threads count prims the same way
            if ((!multithread) || ((index++ + threadId) % threadCount) == 0) {

                if (includeNodes == nullptr || includeNodesCount > 0) {
                    // if we need to hide this node, and if it's not already supposed to be hidden
                    // we force it before calling ReadPrimitive, and we restore it immediately after
                    bool restoreUnhidden = updateHiddenNodes.empty() ? false : !threadContext.IsHidden();
                    if (restoreUnhidden)
                        threadContext.SetHidden(true);

                    reader->ReadPrimitive(prim, context, isInstanceable, matrix);

                    // Eventually restore hidden variable
                    if (restoreUnhidden)
                        threadContext.SetHidden(false);                    
                }
                // Note: if the registry didn't find any primReader, we're not prunning
                // its children nodes, but just skipping this one.
            }
            // Node graph primitives will be read
    #ifdef ARNOLD_USD_MATERIAL_READER
            if (prim.IsA<UsdShadeNodeGraph>()) {
                iter.PruneChildren();
                continue;
            }
    #endif
            // If this prim was a point instancer, we want to skip its children
            if (isPointInstancer)
            {
                ++pointInstancerCount;
                threadContext.SetHidden(true);
            }
        }
    };
    if (!_updating) {
        UsdPrimRange range = UsdPrimRange::PreAndPostVisit((rootPrim) ? 
                        *rootPrim : _stage->GetPseudoRoot());
        TraverseNodes(range, context, threadId, threadCount, doPointInstancer, doSkelData, matrix, nullptr);
    } else {

        UsdPrim updatedPrim;
        bool multiplePrims = false;

        for (const auto& p : _listener._dirtyNodes) {
            UsdPrim prim = _stage->GetPrimAtPath(p);
            if (!prim)
                continue;
            if (updatedPrim) {
                multiplePrims = true;
                break;
            }
            updatedPrim = prim;
        }
        if (!updatedPrim)
            return;

        if (!multiplePrims) {
            UsdPrimRange range = UsdPrimRange::PreAndPostVisit(updatedPrim);
            UsdGeomPrimvarsAPI primvarsAPI(updatedPrim);
            std::vector<std::vector<UsdGeomPrimvar> > &primvarsStack = context.GetThreadContext()->GetPrimvarsStack();
            primvarsStack.resize(1);  
            primvarsStack[0] = primvarsAPI.FindPrimvarsWithInheritance();
            TraverseNodes(
                range, context, threadId, threadCount, doPointInstancer, doSkelData, matrix, &_listener._dirtyNodes);
        } else {
            // if there are multiple prims to update, 
            // we want instead to go through the whole stage and update the primitives that need to
            UsdPrimRange range = UsdPrimRange::PreAndPostVisit((rootPrim) ? 
                        *rootPrim : _stage->GetPseudoRoot());
            TraverseNodes(range, context, threadId, threadCount, doPointInstancer, doSkelData, matrix, &_listener._dirtyNodes);


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
    UsdPrim *rootPrimPtr = nullptr;

    if (!_updating) {
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

        _readerRegistry->RegisterPrimitiveReaders();
    

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
                    _rootPrim.IsPrototype()
                    && AiNodeLookUpUserParameter(_procParent, str::parent_instance)) {
                
                AtString parentInstance = AiNodeGetStr(_procParent, str::parent_instance); 
                UsdPrim parentInstancePrim = _stage->GetPrimAtPath(SdfPath(parentInstance.c_str()));
                if (parentInstancePrim) {
                    // our usd procedural has a uer-data "parent_instance" which returns the name of
                    // the instanceable prim. We want to check what is its prototype
                    auto proto = parentInstancePrim.GetPrototype();
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

        // Check the USD environment variable for custom Materialx node definitions.
        // We need to use this to pass it on to Arnold's MaterialX
        const char *pxrMtlxPath = std::getenv("PXR_MTLX_STDLIB_SEARCH_PATHS");
        if (pxrMtlxPath) {
            _pxrMtlxPath = AtString(pxrMtlxPath);
        }
    }

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
            threadData[i].threadContext.ClearConnections();
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
    _readStep = READ_FINISHED; // We're done

    // For interactive renders, we want to register a TfNotice callback,
    // to be informed of the interactive changes happening in the UsdStage
    // (which must be kept in memory)    
    if (_interactive) {
        // only register the callback if it wasn't already done
        if (!_objectsChangedNoticeKey.IsValid()) {
            _objectsChangedNoticeKey =
                TfNotice::Register(TfCreateWeakPtr(&_listener),
                &StageListener::_OnUsdObjectsChanged, _stage);
        }
        // The eventual "root path" is needed, since we want to ignore changes
        // that aren't part of it
        _listener._rootPath = _hasRootPrim ? _rootPrim.GetPath() : SdfPath();
        
    } else {
        _stage = UsdStageRefPtr(); // clear the shared pointer, delete the stage
    }
    
}

// Callback invoked during interactive USD edits, to notify that a Usd primitive has changed
void UsdArnoldReader::StageListener::_OnUsdObjectsChanged(
        UsdNotice::ObjectsChanged const& notice,
        UsdStageWeakPtr const& sender) {

    auto UpdateDirtyNodes = [](const UsdNotice::ObjectsChanged::PathRange& range,
        std::unordered_set<SdfPath, TfHash>& dirtyNodes, const SdfPath& rootPath) 
    { 
        for (const auto& path : range) {
            // If we have a "root path" and we're just reading a subset of 
            // the usdStage, we want to ensure that the modified node is part
            // of it
            if (!rootPath.IsEmpty() && !path.HasPrefix(rootPath))
                continue;

            // If a change happens on an output attribute, it means we don't need
            // to read this primitive once more since these attributes
            // don't affect the arnold data
            if (path.GetString().find(".outputs:") != std::string::npos)
                continue;

            // Add this primitive path to the list of nodes to be updated
            dirtyNodes.insert(path.GetPrimPath());
        }
    };
    
    // We want to get the changes returned from both "resynced" and "changedInfo" paths
    UpdateDirtyNodes(notice.GetResyncedPaths(), _dirtyNodes, _rootPath);
    UpdateDirtyNodes(notice.GetChangedInfoOnlyPaths(), _dirtyNodes, _rootPath);
}
void UsdArnoldReader::ReadPrimitive(const UsdPrim &prim, UsdArnoldReaderContext &context, bool isInstance, AtArray *parentMatrix)
{
    std::string objName = prim.GetPath().GetText();
    const TimeSettings &time = context.GetTimeSettings();

    std::string objType = prim.GetTypeName().GetText();
    UsdArnoldReaderThreadContext *threadContext = context.GetThreadContext();
    if (isInstance) {
        auto proto = prim.GetPrototype();
        if (proto) {
            if (threadContext->GetSkelData()) {
                // if we need to apply skinning to this instance, then we need to expand it
                AtArray *matrix = ReadMatrix(prim, context.GetTimeSettings(), context, prim.IsA<UsdGeomXformable>());
                const std::string prevPrototypeName = context.GetPrototypeName();
                context.SetPrototypeName(prim.GetPath().GetText());
                TraverseStage(&proto, context, 0, 0, false, false, matrix);
                if (matrix)
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

    UsdArnoldPrimReader *primReader = _readerRegistry->GetPrimReader(objType);
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
            if (parentMatrix && matrix)
                ApplyParentMatrices(matrix, parentMatrix);
            UsdArnoldReaderContext *jobContext = new UsdArnoldReaderContext(context, matrix ? matrix : parentMatrix, threadContext->GetPrimvarsStack().back(), 
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
                if (newMatrices) {
                    ApplyParentMatrices(newMatrices, parentMatrix);
                    context.SetMatrices(newMatrices);
                }
            }
            primReader->Read(prim, context); // read this primitive
            if (parentMatrix && newMatrices) {
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
    delete _readerRegistry;
    _readerRegistry = new UsdArnoldViewportReaderRegistry(mode, params);
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

// Update is invoked when an interactive change happens in a usd procedural.
// We want to go through the list of nodes that were notified as having changed
// and we want to read them once again
void UsdArnoldReader::Update()
{
    if (_listener._dirtyNodes.empty())
        return;

    _updating = true;
    ReadStage(_stage, std::string());
    _updating = false;
    // Clear the list of dirty nodes
    _listener._dirtyNodes.clear();
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
    // stageCache.Insert can return an existing stage, so we increase the ref count for that stage in case it exists
    s_cacheRefCount[_cacheId]++; 
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
    

    // The current USD stageCache implementation use an ID counter which starts at 9223000 and increase it everytime a stage is added.
    // So it should most likely stay in the integer range. But if the implementation changes, we need to make sure we catch it.
    // We could/should probably store it as string. TBD
    if (_cacheId <= std::numeric_limits<int>::max() && _cacheId >= std::numeric_limits<int>::min()) {
        AiNodeSetInt(proto, str::cache_id, static_cast<int>(_cacheId));
    } else {
        AiMsgWarning("[usd] Cache ID is larger that what can be stored in arnold parameter %ld", _cacheId);
    }
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
    // If we're doing an interactive update, we first want to check if the AtNode
    // already exists. If so, we return it
    if (_reader->IsUpdating()) {
        AtNode *node = _reader->LookupNode(name);
        if (node) {
            // Note: should we reset the node ?
            return node;
        }
    }
    const AtNodeEntry *typeEntry = AiNodeEntryLookUp(AtString(type));
    if (!typeEntry) {
        return nullptr;
    }
    if (!(AiNodeEntryGetType(typeEntry) & _reader->GetMask())) {
        return nullptr;
    }

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
    //std::cerr<<"--------------- add connection here "<<std::endl;
    if (_reader->GetReadStep() == UsdArnoldReader::READ_TRAVERSE) {
        // store a link between attributes/nodes to process it later
        // If we have a dispatcher, we want to lock here
        if (_dispatcher)
            _addConnectionLock.lock();


        ArnoldAPIAdapter::AddConnection(source, attr, target, type, outputElement);

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
//std::cerr<<"reader process connection "<<_connections.size()<<std::endl;
    std::vector<Connection> danglingConnections;
    for (const auto& connection : _connections) {
        // if ProcessConnections returns false, it means that the target
        // wasn't found. We want to stack those dangling connections
        // and keep them in our list
        if (!ProcessConnection(connection))
            danglingConnections.push_back(connection);
    }
    // our connections list is now cleared by contains all the ones
    // that couldn't be resolved
    //std::cerr<<"bloblo"<<std::endl;
    _connections = danglingConnections;
}
AtNode* UsdArnoldReaderThreadContext::LookupTargetNode(const char *targetName, const AtNode* source, ConnectionType type)
{
    UsdArnoldReader::ReadStep step = _reader->GetReadStep();
    AtNode *target = _reader->LookupNode(targetName, true);
    if (target == nullptr) {
        if (step == UsdArnoldReader::READ_DANGLING_CONNECTIONS) {
            // generate the missing node right away
            SdfPath sdfPath(targetName);
            UsdPrim prim = _reader->GetStage()->GetPrimAtPath(sdfPath);
            if (prim) {
                // We need to compute the full list of primvars, including 
                // inherited ones. 
                UsdGeomPrimvarsAPI primvarsAPI(prim);
                _primvarsStack.back() = primvarsAPI.FindPrimvarsWithInheritance();
                UsdArnoldReaderContext context(this);
                _reader->ReadPrimitive(prim, context);
                target = _reader->LookupNode(targetName, true);
                if (target == nullptr && type == CONNECTION_PTR &&
                    prim.IsPrototype()
                    ) {
                    
                    // Since the instance can represent any point in the hierarchy, including
                    // xforms that aren't translated to arnold, we need to create a nested
                    // usd procedural that will only read this specific prim. Note that this 
                    // is similar to what is done by the point instancer reader

                    target = _reader->CreateNestedProc(targetName, context);

                    // First time we create the nested proc, we want to add a user data with the first 
                    // instanceable prim pointing to it
                    // Declare the user data
                    AiNodeDeclare(target, str::parent_instance, "constant STRING");
                    AiNodeSetStr(target, str::parent_instance, AiNodeGetName(source));
                }
            }
        }
    }
    return target;
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
    // or when updating a specific primitive.
    // Otherwise we return true to avoid costly computation.
    if (reader->GetReadStep() == UsdArnoldReader::READ_DANGLING_CONNECTIONS || reader->IsUpdating()) {
        return IsPrimVisible(prim, reader, frame);
    }
    
    return true;
}

 
