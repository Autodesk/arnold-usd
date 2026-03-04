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

inline ProceduralReader *CreateProceduralReader(AtUniverse *universe, bool hydra = true, AtNode* procParent = nullptr)
{
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

    const AtString procEntryName(XARNOLDUSDSTRINGIZE(USD_PROCEDURAL_NAME));
    // in the usd procedural built with arnold, we want the frame to trigger 
    // a reload of the procedural, as it's not possible to change the usd stage between renders.
    if (procEntryName == str::usd) {
        AiMetaDataSetBool(nentry, str::frame, AtString("_triggers_reload"), true);
    }

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

procedural_init
{
    ProceduralReader *data = CreateProceduralReader(AiNodeGetUniverse(node), AiNodeGetBool(node, AtString("hydra")), node);
    *user_ptr = data;
    bool interactive = AiNodeGetBool(node, AtString("interactive"));

    // For interactive renders, we want to store the ProceduralReader in 
    // the global map, so that we can retrieve it in procedural_update
    if (interactive) {
        std::lock_guard<std::mutex> lock(s_readersMutex);
        s_readers[node] = data;
    }

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
            return 1;
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
    
    return 1;
}

//-*************************************************************************

procedural_cleanup
{
    ProceduralReader *data = reinterpret_cast<ProceduralReader *>(user_ptr);

#ifndef ENABLE_SHARED_ARRAYS
    // For interactive procedurals, we don't want to delete the ProceduralReader 
    // when the render finishes, as we will need it later on, during procedural_update.
    // Also with shared arrays we should never delete the data here, as we need to hold 
    // the VtValues during the render
    if (data && !data->GetInteractive())
        delete data;
#endif
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
    bool interactive = AiNodeGetBool(node, AtString("interactive"));
    // If the procedural is not set for interactive updates, we can skip this function
    if (!interactive)
        return;
         
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
 
    reader->SetFrame(AiNodeGetFlt(node, str::frame)); 
    // Update the arnold scene based on the modified USD contents
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
