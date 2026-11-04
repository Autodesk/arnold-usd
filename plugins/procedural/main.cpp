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

#include <ai.h>
#include <ai_file_utils.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
// FIXME: include paths below
#include "../../libs/translator/utils/utils.h"
#include "../../libs/translator/reader/reader.h"
#ifdef ENABLE_HYDRA_IN_USD_PROCEDURAL
#include "../../libs/render_delegate/reader.h"
#endif
#include "registry.h"
#include <constant_strings.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/arch/env.h>
#ifdef ARNOLD_USD_BUNDLE
#include <pxr/base/arch/symbols.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <mutex>
#endif
#include "procedural_reader.h"
#include <pxr/usd/usd/stageCache.h>
#include <pxr/usd/usdUtils/stageCache.h>
#include "writer.h"
#include "asset_utils.h"

#if defined(_DARWIN) || defined(_LINUX)
#include <dlfcn.h>
#define DLLEXPORT __attribute__ ((visibility("default")))
#else
#define DLLEXPORT __declspec(dllexport)
#endif

// Macro magic to expand the procedural name.
#define XARNOLDUSDSTRINGIZE(x) ARNOLDUSDSTRINGIZE(x)
#define ARNOLDUSDSTRINGIZE(x) #x

// For procedurals in interactive mode, we can't attach the ProceduralReader to a node, 
// as it won't be available in procedural_update. Therefore we need a global map (#168)
static std::unordered_map<AtNode*, ProceduralReader*> s_readers;
static std::mutex s_readersMutex;

#ifdef ARNOLD_USD_BUNDLE
// The bundle is linked against a shared USD, so unlike the static usd_proc it doesn't embed
// its plugInfo files. They are installed next to the bundle library and point back to it,
// so we register them ourselves, which makes the bundle work in kick or any other standalone
// Arnold host without setting PXR_PLUGINPATH_NAME (#2793).
// Registering the same path twice is a no-op, and if the host already registered plugins
// with the same names from another location, USD keeps the ones registered first.
// Note that the hydra plugin registries only discover the plugins once, so this has to
// happen before the first render delegate is created.
static void RegisterBundlePlugins()
{
    static std::once_flag s_registerOnce;
    std::call_once(s_registerOnce, []() {
        std::string libraryPath;
        if (!ArchGetAddressInfo(
                reinterpret_cast<void *>(&RegisterBundlePlugins), &libraryPath, nullptr, nullptr, nullptr) ||
            libraryPath.empty()) {
            AiMsgDebug("[usd] Unable to find the location of the bundle library to register its plugins");
            return;
        }
        const std::string plugInfoPath = TfStringCatPaths(TfGetPathName(libraryPath), "plugInfo.json");
        if (!TfIsFile(plugInfoPath)) {
            AiMsgDebug("[usd] No plugInfo.json found next to %s", libraryPath.c_str());
            return;
        }
        PlugRegistry::GetInstance().RegisterPlugins(plugInfoPath);
    });
}
#endif

inline ProceduralReader *CreateProceduralReader(AtUniverse *universe, bool hydra = true, AtNode* procParent = nullptr)
{
#ifdef ARNOLD_USD_BUNDLE
    RegisterBundlePlugins();
#endif
#ifdef ENABLE_HYDRA_IN_USD_PROCEDURAL
    // Enable the hydra procedural if it's required by the procedural parameters, 
    // or if the environment variable is defined
    if (ArchHasEnv("PROCEDURAL_USE_HYDRA")) {
        // The environment variable is defined, it takes precedence on any other setting
        std::string useHydra = ArchGetEnv("PROCEDURAL_USE_HYDRA");
        std::string::size_type i = useHydra.find(" ");
        while(i != std::string::npos) {
            useHydra.erase(i, 1);
            i = useHydra.find(" ");
        }
        hydra = (useHydra != "0");
    } else {
        // If no env variable is defined, we check in the global options to eventually override the hydra value
        AtNode *options = AiUniverseGetOptions(universe);
        if (options && AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(options), str::usd_legacy_translation)) {
            // If the global option "usd_legacy_translation" is activated, we force "hydra" to be off, 
            // even if it was set to true in the procedural. 
            // In other words, we use the legacy "usd" mode if the procedural's "hydra" attribute is disabled, OR
            // if the options "usd_legacy_translation" attribute is enabled.
            if (AiNodeGetBool(options, str::usd_legacy_translation))
                hydra = false;
        }
    }
    if (hydra)
        return new HydraArnoldReader(universe, procParent);

#endif
    return new UsdArnoldReader(universe, procParent);
}

//-*************************************************************************
// Code for the Arnold procedural node loading USD files

AI_PROCEDURAL_NODE_EXPORT_METHODS(UsdProceduralMethods);

node_parameters
{
    AiParameterStr("filename", "");
    AiParameterStr("object_path", "");
    AiParameterFlt("frame", 0.0);
    AiParameterArray("overrides", AiArray(0, 1, AI_TYPE_STRING));

    AiParameterInt("cache_id", 0);
    AiParameterBool("interactive", false);
    
    AiParameterBool("debug", false);
    AiParameterInt("threads", 0);
    AiParameterBool("hydra", true);
    
    // Note : if a new attribute is added here, it should be added to the schema in createSchemaFile.py
    
    // Set metadata that triggers the re-generation of the procedural contents when this attribute
    // is modified (see #176)
    AiMetaDataSetBool(nentry, AtString("filename"), AtString("_triggers_reload"), true);
    AiMetaDataSetBool(nentry, AtString("object_path"), AtString("_triggers_reload"), true);
    AiMetaDataSetBool(nentry, AtString("overrides"), AtString("_triggers_reload"), true);
    AiMetaDataSetBool(nentry, AtString("cache_id"), AtString("_triggers_reload"), true);
    AiMetaDataSetBool(nentry, AtString("hydra"), AtString("_triggers_reload"), true);

    // Note that the frame does *not* trigger a reload. Frame changes are handled in procedural_update,
    // either incrementally when the usd stage was kept in memory, or by regenerating the procedural contents.

    // This type of procedural can be initialized in parallel
    AiMetaDataSetBool(nentry, AtString(""), AtString("parallel_init"), true);

    // These 2 attributes are needed internally but should not be exposed to the
    // user interface
    AiMetaDataSetBool(nentry, str::cache_id, str::hide, true);
    AiMetaDataSetBool(nentry, str::interactive, str::hide, true);
    
    // deprecated parameters
    AiMetaDataSetBool(nentry, str::debug, str::deprecated, true);
    AiMetaDataSetBool(nentry, str::threads, str::deprecated, true);
    AiMetaDataSetBool(nentry, str::hydra, str::deprecated, true);
}
typedef std::vector<std::string> PathList;

void applyProceduralSearchPath(std::string &filename, const AtUniverse *universe)
{
    AtNode *optionsNode = AiUniverseGetOptions(universe);
    if (optionsNode) {
        // We want to allow using the procedural search path to point to directories containing .abc files in the same
        // way procedural search paths are used to resolve procedural .ass files.
        // To do this we extract the asset path from the options node, where environment variables specified using
        // the Arnold standard (e.g. [HOME]) are expanded. If our .abc file exists in any of the directories we
        // concatenate the path and the relative filename to create a new procedural argument filename using the full
        // path.
#if ARNOLD_VERSION_NUM <= 70403
        std::string proceduralPath = std::string(AiNodeGetStr(optionsNode, AtString("procedural_searchpath")));
        std::string expandedSearchpath = ExpandEnvironmentVariables(proceduralPath.c_str());
#else
        std::string assetPath = std::string(AiNodeGetStr(optionsNode, AtString("asset_searchpath")));
        std::string expandedSearchpath = ExpandEnvironmentVariables(assetPath.c_str());
#endif
        PathList pathList;
        TokenizePath(expandedSearchpath, pathList, ":;", true);
        if (!pathList.empty()) {
            for (PathList::const_iterator it = pathList.begin(); it != pathList.end(); ++it) {
                std::string path = *it;
                std::string fullPath = PathJoin(path.c_str(), filename.c_str());
                if (IsFileAccessible(fullPath)) {
                    filename = fullPath;
                    return;
                }
            }
        }
    }
}

// Store the reader for this procedural in the global map, deleting an eventual previous one
static void StoreProceduralReader(AtNode *node, ProceduralReader *reader)
{
    ProceduralReader *previousReader = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_readersMutex);
        auto it = s_readers.find(node);
        if (it != s_readers.end()) {
            previousReader = it->second;
            it->second = reader;
        } else {
            s_readers[node] = reader;
        }
    }
    // A previous reader exists when the procedural is reloaded (e.g. its filename changed).
    // Its nodes were already destroyed by arnold, deleting the reader won't touch them
    if (previousReader && previousReader != reader)
        delete previousReader;
}

// Determine if the reader should keep the usd stage in memory after the procedural expansion,
// so that it can later be updated through procedural_update
static bool IsInteractiveReader(AtNode *node, const ProceduralReader *reader)
{
    // The client explicitly asked for interactive updates
    if (AiNodeGetBool(node, str::interactive))
        return true;

    // When the usd stage is owned by the client (cache_id), we only update it if we're explicitly
    // asked to, through the interactive attribute (MTOA-2359)
    if (AiNodeGetInt(node, str::cache_id) != 0)
        return false;

    // For readers able to update to a new frame without regenerating their nodes (hydra),
    // we keep the usd stage in memory during interactive sessions, so that frame changes are fast
    if (!reader->SupportsFrameUpdates())
        return false;
    const AtRenderSession *renderSession = AiUniverseGetRenderSession(AiNodeGetUniverse(node));
    return renderSession && AiGetSessionMode(renderSession) != AI_SESSION_BATCH;
}

// Configure the reader based on the procedural attributes, and read the usd data
static void ReadProcedural(AtNode *node, ProceduralReader *data)
{
    const bool interactive = IsInteractiveReader(node, data);
    std::string objectPath(AiNodeGetStr(node, AtString("object_path")));
    data->SetFrame(AiNodeGetFlt(node, AtString("frame")));
    data->SetId(AiNodeGetUInt(node, AtString("id")));
    data->SetInteractive(interactive);

    AtNode *renderCam = AiUniverseGetCamera(AiNodeGetUniverse(node));
    if (renderCam &&
        (AiNodeGetFlt(renderCam, AtString("shutter_start")) < AiNodeGetFlt(renderCam, AtString("shutter_end")))) {
        float motionStart = AiNodeGetFlt(renderCam, AtString("shutter_start"));
        float motionEnd = AiNodeGetFlt(renderCam, AtString("shutter_end"));
        data->SetMotionBlur((motionStart < motionEnd), motionStart, motionEnd);
    } else {
        data->SetMotionBlur(false);
    }

    int cache_id = AiNodeGetInt(node, AtString("cache_id"));
    if (cache_id != 0) {
        // We have an id to load the Usd Stage in memory, using UsdStageCache
        if (data->Read(cache_id, objectPath))
            return;
        // If the reader didn't manage to load this cache id, then we read the usd data
        // through a filename as usual

    }
    // We load a usd file, with eventual serialized overrides
    const std::string originalFilename(AiNodeGetStr(node, AtString("filename")));
#if ARNOLD_VERSION_NUM >= 70405
    std::string filename(AiResolveFilePath(originalFilename.c_str(), AtFileType::Asset));
#else
    std::string filename(AiResolveFilePath(originalFilename.c_str(), AtFileType::Procedural));
#endif
    applyProceduralSearchPath(filename, nullptr);
    data->Read(filename, AiNodeGetArray(node, AtString("overrides")), objectPath);
}

// Regenerate the whole procedural contents, for readers that can't update their nodes
// incrementally. This replaces the reload that arnold used to do when the frame changed.
static void RegenerateProcedural(AtNode *node, ProceduralReader *reader)
{
    // Get rid of the nodes created by the previous reader. Arnold doesn't let us destroy all of them
    // during a procedural update :
    //   - in batch sessions, no node can be destroyed during an update
    //   - destroying lights or cameras during a procedural update crashes
    // In these cases we "retire" the node instead : we reset it to free its data, disable it,
    // and rename it so that its name can be used by the new nodes. Retired nodes remain
    // children of the procedural, and will be destroyed with it.
    const AtRenderSession *renderSession = AiUniverseGetRenderSession(AiNodeGetUniverse(node));
    const bool batch = renderSession == nullptr || AiGetSessionMode(renderSession) == AI_SESSION_BATCH;
    static std::atomic<unsigned int> s_retiredCounter{0};

    const std::vector<AtNode *> previousNodes = reader->GetNodes();
    for (AtNode *previousNode : previousNodes) {
        const int type = AiNodeEntryGetType(AiNodeGetNodeEntry(previousNode));
        if (!batch && (type == AI_NODE_SHAPE || type == AI_NODE_SHADER)) {
            AiNodeDestroy(previousNode);
            continue;
        }
        const std::string retiredName = std::string(AiNodeGetName(previousNode)) +
            "__retired_" + std::to_string(s_retiredCounter++);
        AiNodeReset(previousNode);
        AiNodeSetStr(previousNode, str::name, AtString(retiredName.c_str()));
        AiNodeSetDisabled(previousNode, true);
    }

    // Create a new reader, it will replace (and delete) the previous one in the global map.
    // The new nodes will be added to the procedural
    ProceduralReader *newReader = CreateProceduralReader(AiNodeGetUniverse(node), AiNodeGetBool(node, str::hydra), node);
    StoreProceduralReader(node, newReader);
    ReadProcedural(node, newReader);
}

procedural_init
{
    ProceduralReader *data = CreateProceduralReader(AiNodeGetUniverse(node), AiNodeGetBool(node, AtString("hydra")), node);
    *user_ptr = data;

    // We store the ProceduralReader in the global map, so that we can retrieve it in procedural_update.
    // It will be deleted in procedural_finish
    StoreProceduralReader(node, data);
    ReadProcedural(node, data);
    return 1;
}

//-*************************************************************************

procedural_cleanup
{
    // The ProceduralReader is stored in the global map and will be deleted in procedural_finish,
    // as we need it during procedural_update. Non-interactive readers already released the usd stage
    // at the end of their translation, and with shared arrays we need to hold the VtValues during the render
    return 1;
}
procedural_finish
{
    // This function is called when the procedural is deleted. 
    // We want to cleanup an eventual ProceduralReader stored globally
    // for interactive renders
    {   
        std::lock_guard<std::mutex> lock(s_readersMutex);
        const auto it = s_readers.find(node);
        if(it != s_readers.end()) {
            delete it->second;
            s_readers.erase(it);
        }
    }
}

// Procedural update will be called right after procedural_init, and at every update,
// i.e. every time an attribute of the procedural is modified
procedural_update
{
    ProceduralReader* reader = nullptr;
    {
        // Retrieve the eventual procedural reader stored globally
        std::lock_guard<std::mutex> lock(s_readersMutex);
        const auto it = s_readers.find(node);
        if (it == s_readers.end())
            return;
        reader = it->second;
    }
    if (!reader)
        return;

    // When the usd stage is owned by the client (cache_id), it can disable the interactive
    // attribute to prevent us from accessing the stage from arnold threads (MTOA-2359)
    if (reader->GetInteractive() && !AiNodeGetBool(node, str::interactive) && AiNodeGetInt(node, str::cache_id) != 0)
        return;

    const float frame = AiNodeGetFlt(node, str::frame);
    if (reader->SupportsFrameUpdates()) {
        // The usd stage was kept in memory, we just need to set the new frame and
        // sync the primitives that were modified
        reader->SetFrame(frame);
        reader->Update();
        return;
    }
    if (frame != reader->GetFrame()) {
        // This reader can't update its nodes to the new frame, we need to regenerate them
        RegenerateProcedural(node, reader);
        return;
    }
    // Update the arnold scene based on the modified USD contents
    if (reader->GetInteractive())
        reader->Update();
}
//-*************************************************************************

procedural_num_nodes
{
    ProceduralReader *data = reinterpret_cast<ProceduralReader *>(user_ptr);
    if (data) {
        return data->GetNodes().size();
    }
    return 0;
}

//-*************************************************************************

procedural_get_node
{
    ProceduralReader *data = reinterpret_cast<ProceduralReader *>(user_ptr);
    if (data) {
        return data->GetNodes()[i];
    }
    return NULL;
}

#if AI_VERSION_ARCH_NUM >= 6
// New API function introduced in Arnold 6 for viewport display of procedurals
//
// ProceduralViewport(const AtNode* node,
//                    AtUniverse* universe,
//                    AtProcViewportMode mode, (AI_PROC_BOXES = 0, AI_PROC_POINTS, AI_PROC_POLYGONS)
//                    AtParamValueMap* params)
procedural_viewport
{
    int cache_id = AiNodeGetInt(node, AtString("cache_id"));

    const std::string originalFilename(AiNodeGetStr(node, AtString("filename")));
#if ARNOLD_VERSION_NUM >= 70405
    std::string filename(AiResolveFilePath(originalFilename.c_str(), AtFileType::Asset));
#else
    std::string filename(AiResolveFilePath(originalFilename.c_str(), AtFileType::Procedural));
#endif
    AtArray *overrides = AiNodeGetArray(node, AtString("overrides"));

    // We support empty filenames if overrides are being set #552
    bool hasOverrides = (overrides &&  AiArrayGetNumElements(overrides) > 0);
    if (cache_id == 0) {
        if (filename.empty()) {
            if (!hasOverrides)
                return false; // no filename + no override, nothing to show here
        } else {
            applyProceduralSearchPath(filename, universe);
            if (!UsdStage::IsSupportedFile(filename)) {
                AiMsgError("[usd] File not supported : %s", filename.c_str());
                return false;
            }
        }
    }

    // For now we always create a new reader for the viewport display,
    // can we reuse the eventual existing one ?
#ifdef ARNOLD_USD_BUNDLE
    RegisterBundlePlugins();
#endif
    ProceduralReader *reader = new UsdArnoldReader(universe);

    std::string objectPath(AiNodeGetStr(node, AtString("object_path")));
    // note that we must *not* set the parent procedural, as we'll be creating
    // nodes in a separate universe
    reader->SetFrame(AiNodeGetFlt(node, str::frame));
    bool listNodes = false;
    // If we receive the bool param value "list" set to true, then we're being
    // asked to return the list of nodes in the usd file. We just need to create
    // the AtNodes, but not to convert them
    if (params && AiParamValueMapGetBool(params, AtString("list"), &listNodes) && listNodes) {
        reader->SetConvertPrimitives(false);
    } else {
        // We want a viewport reader registry, that will load either boxes, points or polygons
        reader->CreateViewportRegistry(mode, params);
        // We want to read the "proxy" purpose
        reader->SetPurpose("proxy"); 
    }

    if (cache_id != 0) 
        reader->Read(cache_id, objectPath);
    else
        reader->Read(filename, overrides, objectPath);

    delete reader;
    return true;
}
#endif

#if defined(_DARWIN) || defined(_LINUX)
std::string USDLibraryPath()
{
    Dl_info info;
    if (dladdr("USDLibraryPath", &info)) {
        std::string path = info.dli_fname;
        return path;
    }

    return std::string();
}
#endif

extern "C"
{
    DLLEXPORT void WriteUsdStageCache ( const AtUniverse* universe, long int cacheId, const AtParamValueMap* params )
    {
        // Get the UsdStageCache, it's common to all libraries linking against the same USD libs
        UsdStageCache &stageCache = UsdUtilsStageCache::Get();
        UsdStageCache::Id id = UsdStageCache::Id::FromLongInt(cacheId);
        // Retrieve the UsdStage associated to this cache ID.
        UsdStageRefPtr stage = (id.IsValid()) ? stageCache.Find(id) : nullptr;
        if (!stage) {
            AiMsgError("[usd] Cache ID not valid %ld", cacheId);
            return;
        }
        // Create an Arnold-USD writer, that can write an Arnold univers to a UsdStage
        UsdArnoldWriter writer;
        writer.SetUsdStage(stage); 
        writer.SetAppendFile(true);

        if (params) {
            // eventually check the input param map in case we have an entry for "frame"
            float frame = 0.f;
            if (AiParamValueMapGetFlt(params, str::frame, &frame))
                writer.SetFrame(frame);
            
            int mask = AI_NODE_ALL;
            if (AiParamValueMapGetInt(params, str::mask, &mask))
                writer.SetMask(mask);
            
            AtString scope;
            if (AiParamValueMapGetStr(params, str::scope, &scope))
                writer.SetScope(std::string(scope.c_str()));

            AtString mtlScope;
            if (AiParamValueMapGetStr(params, str::mtl_scope, &mtlScope))
                writer.SetMtlScope(std::string(mtlScope.c_str()));

            AtString defaultPrim;
            if (AiParamValueMapGetStr(params, str::defaultPrim, &defaultPrim))
                writer.SetDefaultPrim(std::string(defaultPrim.c_str()));

            bool allAttributes;
            if (AiParamValueMapGetBool(params, str::all_attributes, &allAttributes))
                writer.SetWriteAllAttributes(allAttributes);
        }            
        writer.Write(universe);
    }
};


node_loader
{
    if (i > 0) {
        return false;
    }

    node->methods = UsdProceduralMethods;
    node->output_type = AI_TYPE_NONE;
    node->name = AtString(XARNOLDUSDSTRINGIZE(USD_PROCEDURAL_NAME));
    node->node_type = AI_NODE_SHAPE_PROCEDURAL;
    strcpy(node->version, AI_VERSION);

    /* Fix the pre-10.13 OSX crashes at shutdown (#8866). Manually dlopening usd
     * prevents it from being unloaded since loads are reference counted
     * see : https://github.com/openssl/openssl/issues/653#issuecomment-206343347
     *       https://github.com/jemalloc/jemalloc/issues/1122
     */
#if defined(_DARWIN) || defined(_LINUX)
    const auto result = dlopen(USDLibraryPath().c_str(), RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    if (!result)
        AiMsgWarning(
            "[USD] failed to re-load usd_proc.dylib. Crashes might happen on pre-10.13 OSX systems: %s\n", dlerror());
#endif
    return true;
}

/** Arnold 6.0.2.0 introduces Scene Format plugins.
 *  The following code is meant to add support for USD format,
 *  and kick directly USD files
 **/
#ifdef ARNOLD_HAS_SCENE_FORMAT_API
#include <ai_scene_format.h>

AI_SCENE_FORMAT_EXPORT_METHODS(UsdSceneFormatMtd);


// SceneLoad(AtUniverse* universe, const char* filename, const AtParamValueMap* params)
scene_load
{
    if (!UsdStage::IsSupportedFile(filename)) {
        AiMsgError("[usd] File not supported : %s", filename);
        return false;
    }

    // Create a reader with no procedural parent
    ProceduralReader *reader = CreateProceduralReader(universe);
    // default to options.frame
    float frame = AiNodeGetFlt(AiUniverseGetOptions(universe), AtString("frame"));
    if (params) {
        AtString commandLine;
        if (AiParamValueMapGetStr(params, str::command_line, &commandLine)) {
            const std::string commandLineStr(commandLine.c_str());
            reader->SetCommandLine(commandLineStr);
        }

        // eventually check the input param map in case we have an entry for "frame"
        AiParamValueMapGetFlt(params, str::frame, &frame);
        int mask = AI_NODE_ALL;
        if (AiParamValueMapGetInt(params, str::mask, &mask))
            reader->SetMask(mask);
        AtString renderSettings;
        if (AiParamValueMapGetStr(params, str::render_settings, &renderSettings) && renderSettings.length() > 0)
            reader->SetRenderSettings(std::string(renderSettings.c_str()));
        // We use the "render_pass" argument to set the path to the render pass to use for rendering
        // With kick you can use -sceneload_arg to pass the argument, note that it works only with the scene index enabled (hydra2)
        //
        // kick -sceneload_arg render_pass /Render/Passes/renderpass2 myscene.usda
        //
        AtString renderPass;
        if (AiParamValueMapGetStr(params, str::render_pass, &renderPass) && renderPass.length() > 0) {
            reader->SetRenderPass(std::string(renderPass.c_str()));
        }
    }
    reader->SetFrame(frame);
    
    // Read the USD file
    reader->Read(filename, nullptr);
    delete reader;
    return true;
}

// bool SceneWrite(AtUniverse* universe, const char* filename,
//                 const AtParamValueMap* params, const AtMetadataStore* mds)
scene_write
{
    std::string filenameStr(filename);
    if (!UsdStage::IsSupportedFile(filenameStr)) {
        // This filename isn't supported, let's see if it's just the extension that is upper-case
        std::string extension = TfGetExtension(filenameStr);
        size_t basenameLength = filenameStr.length() - extension.length();
        std::transform(
            filenameStr.begin() + basenameLength, filenameStr.end(), filenameStr.begin() + basenameLength, ::tolower);

        // Let's try again now, with a lower case extension
        if (UsdStage::IsSupportedFile(filenameStr)) {
            AiMsgWarning("[usd] File extension must be lower case. Saving as %s", filenameStr.c_str());
        } else {
            // Still not good, we cannot write to this file
            AiMsgError("[usd] File not supported : %s", filenameStr.c_str());
            return false;
        }
    }

    bool appendFile = false;
    if (params)
        AiParamValueMapGetBool(params, str::append, &appendFile);

    SdfLayerRefPtr rootLayer = (appendFile) ? SdfLayer::FindOrOpen(filenameStr) :
                                        SdfLayer::CreateNew(filenameStr.c_str());
    UsdStageRefPtr stage = UsdStage::Open(rootLayer, UsdStage::LoadAll);

    if (stage == nullptr) {
        AiMsgError("[usd] Unable to create USD stage from %s", filenameStr.c_str());
        return false;
    }

    // Create a "writer" Translator that will handle the conversion
    UsdArnoldWriter *writer = new UsdArnoldWriter();
    writer->SetAppendFile(appendFile);
    writer->SetUsdStage(stage); // give it the output stage

    // Check if a mask has been set through the params map
    if (params) {
        int mask = AI_NODE_ALL;
        if (AiParamValueMapGetInt(params, str::mask, &mask))
            writer->SetMask(mask); // only write out this type or arnold nodes

        float frame = 0.f;
        if (AiParamValueMapGetFlt(params, str::frame, &frame))
            writer->SetFrame(frame);

        AtString scope;
        if (AiParamValueMapGetStr(params, str::scope, &scope))
            writer->SetScope(std::string(scope.c_str()));

        AtString mtlScope;
        if (AiParamValueMapGetStr(params, str::mtl_scope, &mtlScope))
            writer->SetMtlScope(std::string(mtlScope.c_str()));

        AtString defaultPrim;
        if (AiParamValueMapGetStr(params, str::defaultPrim, &defaultPrim))
            writer->SetDefaultPrim(std::string(defaultPrim.c_str()));

        bool allAttributes;
        if (AiParamValueMapGetBool(params, str::all_attributes, &allAttributes))
            writer->SetWriteAllAttributes(allAttributes);

        AtString upAxis;
        if (AiParamValueMapGetStr(params, str::upAxis, &upAxis))
            writer->SetUpAxis(std::string(upAxis.c_str()));

        float metersPerUnit;
        if (AiParamValueMapGetFlt(params, str::metersPerUnit, &metersPerUnit))
            writer->SetMetersPerUnit(metersPerUnit);
    }
    writer->Write(universe);       // convert this universe please
    stage->GetRootLayer()->Save(); // Ask USD to save out the file

    AiMsgInfo("[usd] Saved scene as %s", filenameStr.c_str());
    delete writer;
    return true;
}

// scene_get_assets function was added in Arnold 7.4.5.0
#if ARNOLD_VERSION_NUM >= 70405
// static AtArray* SceneGetAssets(const char* filename, const AtParamValueMap* params)
scene_get_assets
{
    bool isProcedural = false;
    if (params)
        AiParamValueMapGetBool(params, AtString("is_procedural"), &isProcedural);

    // collect assets from the scene
    std::vector<AtAsset*> assets;
    CollectSceneAssets(filename, isProcedural, assets);

    if (assets.empty())
        return nullptr;

    // convert our list to an Arnold array
    // the ownership of the array and the assets is delegated to the caller
    AtArray* assetArray = AiArrayAllocate(assets.size(), 1, AI_TYPE_POINTER);
    void* assetArrayData = AiArrayMap(assetArray);
    void* assetData = assets.data();
    if (assetArrayData && assetData)
       memcpy(assetArrayData, assetData, assets.size() * sizeof(void*));

    return assetArray;
}
#endif

scene_format_loader
{
    static const char *extensions[] = {".usd", ".usda", ".usdc", ".usdz", NULL};

    format->methods = UsdSceneFormatMtd;
    format->extensions = extensions;
    format->name = "USD";
    format->description = "Load and write USD files in Arnold";
    strcpy(format->version, AI_VERSION);
    return true;
}

#endif
