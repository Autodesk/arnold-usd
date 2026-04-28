//
// SPDX-License-Identifier: Apache-2.0
//

#include "procedural_reader.h"
#include "diagnostic_utils.h"
#include <ai.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdUtils/stageCache.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>

int s_anonymousOverrideCounter = 0;
static AtMutex s_overrideReaderMutex;

PXR_NAMESPACE_USING_DIRECTIVE

void ProceduralReader::Read(const std::string &filename, 
    AtArray *overrides, const std::string &path)
{
    // Install diagnostic delegate to capture USD composition errors
    ArnoldUsdDiagnostic diagnostic;
    
    // Nodes were already exported, should we skip here,
    // or should we just append the new nodes ?
    if (!GetNodes().empty()) {
        return;
    }

    SdfLayerRefPtr rootLayer = SdfLayer::FindOrOpen(filename);
    _filename = filename;   // Store the filename that is currently being read
    _overrides = nullptr;

    if (overrides == nullptr || AiArrayGetNumElements(overrides) == 0) {
        // Only open the usd file as a root layer
        if (rootLayer == nullptr) {
            AiMsgError("[usd] Failed to open file (%s)", filename.c_str());
            _overrides = nullptr;
            return;
        }
        UsdStageRefPtr stage = UsdStage::Open(rootLayer, UsdStage::LoadAll);
        ReadStage(stage, path);

    } else {
        _overrides = overrides; // Store the overrides that are currently being applied
        auto getLayerName = []() -> std::string {
            int counter;
            {
                std::lock_guard<AtMutex> guard(s_overrideReaderMutex);
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

bool ProceduralReader::Read(long int cacheId, const std::string &path)
{
    // Install diagnostic delegate to capture USD composition errors
    ArnoldUsdDiagnostic diagnostic;
    
    if (!GetNodes().empty()) {
        return true;
    }
    _cacheId = cacheId;
    // Load the USD stage in memory using a cache ID
    UsdStageCache &stageCache = UsdUtilsStageCache::Get();
    UsdStageCache::Id id = UsdStageCache::Id::FromLongInt(cacheId);
   
    UsdStageRefPtr stage = (id.IsValid()) ? stageCache.Find(id) : nullptr;
    if (!stage) {
        AiMsgWarning("[usd] Cache ID not valid %ld", cacheId);
        _cacheId = 0;
        return false;
    }
    ReadStage(stage, path);
    return true;

}


