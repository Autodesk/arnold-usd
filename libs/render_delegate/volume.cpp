//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2019 Luma Pictures
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
//
// Modifications Copyright 2022 Autodesk, Inc.
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
#include "volume.h"
#include <pxr/base/trace/trace.h>

#include <pxr/base/tf/dl.h>

#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/instancer.h>

#include <pxr/usd/sdf/assetPath.h>

#include <pxr/usd/usdVol/tokens.h>

#include <constant_strings.h>
#include "coord_sys.h"
#include "node_graph.h"
#include "openvdb_asset.h"
#include "utils.h"

#include <iostream>
#include <array>

#include <pxr/base/arch/defines.h>
#include <pxr/base/arch/env.h>
#include <pxr/base/arch/library.h>
#include <pxr/base/tf/pathUtils.h>

/// This is not publicly exposed in USD's TF module.
#if defined(ARCH_OS_WINDOWS)
#include <Windows.h>
#define GETSYM(handle, name) GetProcAddress((HMODULE)handle, name)
#else
#include <dlfcn.h>
#define WINAPI
#define GETSYM(handle, name) dlsym(handle, name)
#endif

PXR_NAMESPACE_OPEN_SCOPE

namespace {

/// Houdini provides two function pointers to access Volume primitives via a
/// dynamic library, removing the need for linking against Houdini libraries.
/// HoudiniGetVdbPrimitive -> Returns a Houdini primitive to work with OpenVDB
/// volumes.
/// HoudiniGetVolumePrimitives -> Returns a Houdini primitive to work with
/// native Houdini volumes.
using HoudiniGetVdbPrimitive = void* (*)(const char*, const char*, int);
using HoudiniGetVolumePrimitive = void* (*)(const char*, const char*, int);
struct HoudiniFnSet {
    HoudiniGetVdbPrimitive getVdbPrimitiveWithIndex = nullptr;
    HoudiniGetVolumePrimitive getVolumePrimitive = nullptr;

    /// We need to load USD_SopVol.(so|dylib|dll) to access the volume function
    /// pointers.
    HoudiniFnSet()
    {
        constexpr auto getVdbNameWithIndex = "SOPgetVDBVolumePrimitiveWithIndex";
        constexpr auto getVolumeName = "SOPgetHoudiniVolumePrimitive";
        const auto HFS = ArchGetEnv("HFS");
        const auto dsoPath = HFS + ARCH_PATH_SEP + "houdini" + ARCH_PATH_SEP + "dso" + ARCH_PATH_SEP + "USD_SopVol" +
                             ARCH_LIBRARY_SUFFIX;
        // We don't have to worry about unloading the library, as our library
        // will be unloaded before Houdini exits.
        auto* sopVol = ArchLibraryOpen(dsoPath, ARCH_LIBRARY_NOW);
        if (sopVol == nullptr) {
            return;
        }
        getVdbPrimitiveWithIndex = reinterpret_cast<HoudiniGetVdbPrimitive>(GETSYM(sopVol, getVdbNameWithIndex));
        getVolumePrimitive = reinterpret_cast<HoudiniGetVolumePrimitive>(GETSYM(sopVol, getVolumeName));
    }
};

const HoudiniFnSet& _GetHoudiniFunctionSet()
{
    static HoudiniFnSet ret;
    return ret;
}

using HtoAConvertPrimVdbToArnold = void (*)(void*, int, void**);

/// HtoA provides a function to read data from a Houdini OpenVDB primitive
/// and write it to a volume node storing the VDB data in-memory.
struct HtoAFnSet {
    HtoAConvertPrimVdbToArnold convertPrimVdbToArnold = nullptr;

    HtoAFnSet()
    {
        /// The symbol is stored in _htoa_pygeo.so in pythonX.Xlibs, and
        /// htoa is typically configured using HOUDINI_PATH. We should refine
        /// this method in the future.
        /// One of the current limitations is that we don't support HtoA
        /// installed in a path containing `;` or `&`.
        constexpr auto convertVdbName = "HtoAConvertPrimVdbToArnold";
        const auto HOUDINI_PATH = ArchGetEnv("HOUDINI_PATH");
        auto searchForLibHtoaGeo = [&](const std::string& path) -> bool {
            if (path == "&") {
                return false;
            }
#if defined(ARCH_OS_WINDOWS)
            constexpr const char* htoaGeoDso = "htoa_geo.dll";
#elif defined(ARCH_OS_LINUX)
            constexpr const char* htoaGeoDso = "libhtoa_geo.so";
#elif defined(ARCH_OS_OSX)
            constexpr const char* htoaGeoDso = "libhtoa_geo.dylib";
#else
            TF_WARN("Error loading %s - unsupported architecture", convertVdbName);
            return false;
#endif

            const std::string dsoPath = path + ARCH_PATH_SEP + "scripts" ARCH_PATH_SEP + "bin" + ARCH_PATH_SEP + htoaGeoDso;
            void* htoaGeo = ArchLibraryOpen(dsoPath, ARCH_LIBRARY_NOW);
            if (!htoaGeo) {
                return false;
            }
            convertPrimVdbToArnold = reinterpret_cast<HtoAConvertPrimVdbToArnold>(GETSYM(htoaGeo, convertVdbName));
            if (convertPrimVdbToArnold == nullptr) {
                TF_WARN("Error loading %s from %s", convertVdbName, dsoPath.c_str());
            }
            return true;
        };
        const auto houdiniPaths = TfStringSplit(HOUDINI_PATH, ARCH_PATH_LIST_SEP);
        for (const auto& houdiniPath : houdiniPaths) {
            if (searchForLibHtoaGeo(houdiniPath)) {
                return;
            }
#ifndef ARCH_OS_WINDOWS
            if (TfStringContains(houdiniPath, ";")) {
                const auto subPaths = TfStringSplit(houdiniPath, ";");
                for (const auto& subPath : subPaths) {
                    if (searchForLibHtoaGeo(subPath)) {
                        return;
                    }
                }
            }
#endif
        }
        /// TF warning, error and status functions don't show up in the terminal
        /// when running on Linux/MacOS and Houdini 18.
        std::cerr << "[HdArnold] Cannot load _htoa_pygeo library required for volume rendering in Solaris" << std::endl;
    }
};

const HtoAFnSet _GetHtoAFunctionSet()
{
    static HtoAFnSet ret;
    return ret;
}

// Pack the field name and index together
struct VdbFieldData {
    TfToken field;
    int fieldIndex = 0;
};


} // namespace

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (openvdbAsset)
    (filePath)
    (fieldName)
    (fieldIndex)
);
// clang-format on

HdArnoldVolume::HdArnoldVolume(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdVolume(id), _renderDelegate(renderDelegate)
{
}

HdArnoldVolume::~HdArnoldVolume()
{
    // Stop tracking dependencies for this prim
    _renderDelegate->ClearDependencies(GetId());
    _ForEachVolume([](HdArnoldShape* s) { delete s; });
}

void HdArnoldVolume::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    AiProfileBlock("hydra_proc:HdArnoldVolume:Sync");
    TRACE_FUNCTION();
    if (!_renderDelegate->CanUpdateScene())
        return;
 
    TF_UNUSED(reprToken);
    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id = GetId();
    auto volumesChanged = false;
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        param.Interrupt();
        _CreateVolumes(id, sceneDelegate);
        volumesChanged = true;
    }

    // DirtyCategories carries the coordinate-system bindings, and the material's
    // "space" inputs are rewritten to the cameras bound here, so a binding change
    // has to re-assign the material (see HdArnoldGetCoordSysBinding).
    if (volumesChanged || (*dirtyBits & (HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtyCategories))) {
        param.Interrupt();
        const auto materialId = sceneDelegate->GetMaterialId(id);
        // Ensure the reference from this shape to its material is properly tracked
        // by the render delegate
        _renderDelegate->TrackDependencies(id, HdArnoldRenderDelegate::PathSetWithDirtyBits {{materialId, HdChangeTracker::DirtyMaterialId}});
        auto* material = HdArnoldNodeGraph::GetNodeGraph(sceneDelegate->GetRenderIndex(), materialId, _renderDelegate);
        const auto coordSysBinding = HdArnoldGetCoordSysBinding(sceneDelegate, id);
        auto* volumeShader = material != nullptr
                                 ? material->GetCachedVolumeShader(coordSysBinding)
                                 : _renderDelegate->GetFallbackVolumeShader();
        auto assignShader = [](HdArnoldShape* s, AtNode* shader) {
            if (shader) AiNodeSetPtr(s->GetShape(), str::shader, shader); else AiNodeResetParameter(s->GetShape(), str::shader);
        };
        for (auto* v : _volumes) { assignShader(v, volumeShader); }
        for (auto* v : _inMemoryVolumes) { assignShader(v, volumeShader); }
        // Points nodes created from vdb points grids are regular (non-volumetric) shapes, so
        // they need a surface shader rather than the volume shader (#2740).
        if (!_pointsVolumes.empty()) {
            auto* surfaceShader = material != nullptr ? material->GetCachedSurfaceShader(coordSysBinding)
                                                       : _renderDelegate->GetFallbackSurfaceShader();
            for (auto* v : _pointsVolumes) { assignShader(v, surfaceShader); }
        }
    }

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param.Interrupt();
        HdArnoldRenderParam * renderParam = reinterpret_cast<HdArnoldRenderParam*>(_renderDelegate->GetRenderParam());
        _ForEachVolume([&](HdArnoldShape* s) { HdArnoldSetTransform(s->GetShape(), sceneDelegate, GetId(), renderParam->GetShutterRange()); });
        transformDirtied = true;
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        param.Interrupt();
        _UpdateVisibility(sceneDelegate, dirtyBits);
        _visibilityFlags.SetHydraFlag(_sharedData.visible ? AI_RAY_ALL : 0);
        const auto visibility = _visibilityFlags.Compose();
        _ForEachVolume([&](HdArnoldShape* s) { s->SetVisibility(_sharedData.visible ? visibility : 0); });
    }

    if (HdChangeTracker::IsDoubleSidedDirty(*dirtyBits, id)) {
        param.Interrupt();

        bool doubleSided = sceneDelegate->GetDoubleSided(id);
#if ARNOLD_VERSION_NUM >= 70500
        // For arnold 7.5.0 and up, the option's attribute usd_override_double_sided
        // tells us if we should consider doubleSided or ignore it (#2099)
        if (AiNodeGetBool(AiUniverseGetOptions(_renderDelegate->GetUniverse()), str::usd_override_double_sided))
            doubleSided = true;
#endif
        _sidednessFlags.SetHydraFlag(doubleSided ? AI_RAY_ALL : AI_RAY_SUBSURFACE);
        const auto sidedness = _sidednessFlags.Compose();
        _ForEachVolume([&](HdArnoldShape* s) { AiNodeSetByte(s->GetShape(), str::sidedness, sidedness); });
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        _visibilityFlags.ClearPrimvarFlags();
        _sidednessFlags.ClearPrimvarFlags();
        param.Interrupt();
        for (const auto& primvar : sceneDelegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationConstant)) {
            _ForEachVolume([&](HdArnoldShape* s) {
                HdArnoldSetConstantPrimvar(
                    s->GetShape(), id, sceneDelegate, primvar, &_visibilityFlags, &_sidednessFlags, nullptr, _renderDelegate);
            });
        }
        const auto visibility = _visibilityFlags.Compose();
        const auto sidedness = _sidednessFlags.Compose();
        _ForEachVolume([&](HdArnoldShape* s) {
            s->SetVisibility(_sharedData.visible ? visibility : 0);
            AiNodeSetByte(s->GetShape(), str::sidedness, sidedness);
        });
    }

    // Newer USD versions need to update the instancer before accessing the instancer id.
    _UpdateInstancer(sceneDelegate, dirtyBits);
    // We also force syncing of the parent instancers.
    HdInstancer::_SyncInstancerAndParents(sceneDelegate->GetRenderIndex(), GetInstancerId());

    _ForEachVolume(
        [&](HdArnoldShape* shape) { shape->Sync(this, *dirtyBits, sceneDelegate, param, transformDirtied); });

    *dirtyBits = HdChangeTracker::Clean;
}

void HdArnoldVolume::_CreateVolumes(const SdfPath& id, HdSceneDelegate* sceneDelegate)
{
    std::unordered_map<std::string, std::vector<VdbFieldData>> openvdb_fields;
    std::unordered_map<std::string, std::vector<VdbFieldData>> houvdb_fields;
    // Per-path sets to detect duplicate field names in O(1) instead of O(n) linear scan
    std::unordered_map<std::string, std::unordered_set<TfToken, TfToken::HashFunctor>> openvdb_seen;
    std::unordered_map<std::string, std::unordered_set<TfToken, TfToken::HashFunctor>> houvdb_seen;

    const auto fieldDescriptors = sceneDelegate->GetVolumeFieldDescriptors(id);
    for (const auto& field : fieldDescriptors) {
        auto* openvdbAsset = dynamic_cast<HdArnoldOpenvdbAsset*>(
            sceneDelegate->GetRenderIndex().GetBprim(_tokens->openvdbAsset, field.fieldId));
        if (openvdbAsset == nullptr) {
            continue;
        }
        openvdbAsset->TrackVolumePrimitive(id);
        const auto vv = sceneDelegate->Get(field.fieldId, _tokens->filePath);
        if (vv.IsHolding<SdfAssetPath>()) {
            const auto& assetPath = vv.UncheckedGet<SdfAssetPath>();
            auto path = assetPath.GetResolvedPath();
            if (path.empty()) {
                path = assetPath.GetAssetPath();
            }
            TfToken fieldName = field.fieldName;
            const auto fieldNameValue = sceneDelegate->Get(field.fieldId, _tokens->fieldName);
            if (fieldNameValue.IsHolding<TfToken>()) {
                const TfToken& fieldNameToken = fieldNameValue.UncheckedGet<TfToken>();
                if (!fieldNameToken.IsEmpty())
                    fieldName = fieldNameToken;
            }

            int fieldIndex = 0;
            auto fieldIndexValue = sceneDelegate->Get(field.fieldId, _tokens->fieldIndex);
            if (fieldIndexValue.IsHolding<int>()) {
                fieldIndex = fieldIndexValue.UncheckedGet<int>();
            }

            // op: paths denote a live reference to a VDB in the Houdini session
            if (TfStringStartsWith(path, "op:")) {
                auto& fields = houvdb_fields[path];
                if (houvdb_seen[path].insert(fieldName).second) {
                    fields.push_back({fieldName, fieldIndex});
                }
                continue;
            }

            auto& fields = openvdb_fields[path];
            if (openvdb_seen[path].insert(fieldName).second) {
                fields.push_back({fieldName, fieldIndex});
            }
        }
    }

#if ARNOLD_VERSION_NUM >= 70504
    // Since Arnold 7.5.4.0, AiVolumeFileGetChannelTypes lets us know if a vdb grid stores
    // OpenVDB points rather than a regular volume/SDF grid. Arnold volume nodes can't render
    // points grids, so those are routed to dedicated points nodes instead (#2740). The query
    // reopens and reparses the whole file, so its result is cached per filename.
    std::unordered_map<std::string, std::unordered_map<std::string, int>> channelTypesCache;
    auto getChannelType = [&channelTypesCache](const std::string& file, const std::string& grid) -> int {
        auto cacheIt = channelTypesCache.find(file);
        if (cacheIt == channelTypesCache.end()) {
            std::unordered_map<std::string, int> types;
            AtArray* channels = AiVolumeFileGetChannels(file.c_str());
            AtArray* channelTypes = AiVolumeFileGetChannelTypes(file.c_str());
            if (channels != nullptr && channelTypes != nullptr) {
                const auto numChannels = AiArrayGetNumElements(channels);
                for (auto i = decltype(numChannels){0}; i < numChannels; ++i) {
                    types[AiArrayGetStr(channels, i).c_str()] = AiArrayGetInt(channelTypes, i);
                }
            }
            cacheIt = channelTypesCache.emplace(file, std::move(types)).first;
        }
        auto typeIt = cacheIt->second.find(grid);
        return typeIt == cacheIt->second.end() ? AI_VOLUME_CHANNEL_TYPE_UNKNOWN : typeIt->second;
    };

    // Split each file's requested grids into regular volume grids and points grids.
    std::unordered_map<std::string, std::vector<VdbFieldData>> openvdb_points_fields;
    for (auto& openvdb : openvdb_fields) {
        std::vector<VdbFieldData> volumeOnlyFields;
        for (auto& fieldData : openvdb.second) {
            if (getChannelType(openvdb.first, fieldData.field.GetString()) == AI_VOLUME_CHANNEL_TYPE_POINTS) {
                openvdb_points_fields[openvdb.first].push_back(fieldData);
            } else {
                volumeOnlyFields.push_back(fieldData);
            }
        }
        openvdb.second = std::move(volumeOnlyFields);
    }
    // Drop files left with no regular volume grid, so we don't create an empty volume node.
    for (auto it = openvdb_fields.begin(); it != openvdb_fields.end();) {
        it = it->second.empty() ? openvdb_fields.erase(it) : std::next(it);
    }
#endif

    _volumes.erase(
        std::remove_if(
            _volumes.begin(), _volumes.end(),
            [&openvdb_fields](HdArnoldShape* shape) -> bool {
                if (openvdb_fields.find(AiNodeGetStr(shape->GetShape(), str::filename).c_str()) ==
                    openvdb_fields.end()) {
                    delete shape;
                    return true;
                }
                return false;
            }),
        _volumes.end());

    for (const auto& openvdb : openvdb_fields) {

        AtNode* volume = nullptr;
        for (auto* shape : _volumes) {
            auto* v = shape->GetShape();
            if (openvdb.first == AiNodeGetStr(v, str::filename).c_str()) {
                volume = v;
                break;
            }
        }
        if (volume == nullptr) {
            auto* shape = new HdArnoldShape(str::volume, _renderDelegate, id, GetPrimId());
            volume = shape->GetShape();
            AiNodeSetStr(volume, str::filename, AtString(openvdb.first.c_str()));
            AiNodeSetStr(volume, str::name, AtString(TfStringPrintf("%s_p_%p", id.GetText(), volume).c_str()));

            _volumes.push_back(shape);
        }

        const auto numFields = openvdb.second.size();
        auto* fields = AiArrayAllocate(numFields, 1, AI_TYPE_STRING);
        for (auto i = decltype(numFields){0}; i < numFields; ++i) {
            const int fieldIndex = openvdb.second[i].fieldIndex;
            std::string fieldIndexName = openvdb.second[i].field.GetString();
            fieldIndexName += std::string("[") + std::to_string(fieldIndex) + std::string("]");
            AiArraySetStr(fields, i, AtString(fieldIndexName.c_str()));
        }
        AiNodeSetArray(volume, str::grids, fields);
    }

    for (auto* volume : _inMemoryVolumes) {
        delete volume;
    }
    _inMemoryVolumes.clear();

#if ARNOLD_VERSION_NUM >= 70504
    // Arnold's points node can only source a single grid per node (file_grid), so each
    // requested points grid gets its own points node.
    _pointsVolumes.erase(
        std::remove_if(
            _pointsVolumes.begin(), _pointsVolumes.end(),
            [&openvdb_points_fields](HdArnoldShape* shape) -> bool {
                auto* p = shape->GetShape();
                const std::string file = AiNodeGetStr(p, str::file_name).c_str();
                const std::string grid = AiNodeGetStr(p, str::file_grid).c_str();
                auto pointsIt = openvdb_points_fields.find(file);
                const bool stillNeeded = pointsIt != openvdb_points_fields.end() &&
                    std::any_of(
                        pointsIt->second.begin(), pointsIt->second.end(),
                        [&grid](const VdbFieldData& fieldData) { return fieldData.field.GetString() == grid; });
                if (!stillNeeded) {
                    delete shape;
                    return true;
                }
                return false;
            }),
        _pointsVolumes.end());

    for (const auto& openvdbPoints : openvdb_points_fields) {
        for (const auto& fieldData : openvdbPoints.second) {
            const auto& gridName = fieldData.field.GetString();
            AtNode* pointsNode = nullptr;
            for (auto* shape : _pointsVolumes) {
                auto* p = shape->GetShape();
                if (openvdbPoints.first == AiNodeGetStr(p, str::file_name).c_str() &&
                    gridName == AiNodeGetStr(p, str::file_grid).c_str()) {
                    pointsNode = p;
                    break;
                }
            }
            if (pointsNode == nullptr) {
                auto* shape = new HdArnoldShape(str::points, _renderDelegate, id, GetPrimId());
                pointsNode = shape->GetShape();
                AiNodeSetStr(pointsNode, str::file_name, AtString(openvdbPoints.first.c_str()));
                AiNodeSetStr(pointsNode, str::file_grid, AtString(gridName.c_str()));
                AiNodeSetStr(
                    pointsNode, str::name, AtString(TfStringPrintf("%s_pts_%p", id.GetText(), pointsNode).c_str()));
                _pointsVolumes.push_back(shape);
            }
        }
    }
#endif

    if (houvdb_fields.empty()) {
        return;
    }

    const auto& houdiniFnSet = _GetHoudiniFunctionSet();
    if (houdiniFnSet.getVdbPrimitiveWithIndex == nullptr || houdiniFnSet.getVolumePrimitive == nullptr) {
        return;
    }

    const auto& htoaFnSet = _GetHtoAFunctionSet();
    if (htoaFnSet.convertPrimVdbToArnold == nullptr) {
        return;
    }

    for (const auto& houvdb : houvdb_fields) {
        std::vector<void*> gridVec;

        for (const auto& data : houvdb.second) {
            const TfToken& field = data.field;
            
            auto* primVdb =
                houdiniFnSet.getVdbPrimitiveWithIndex(houvdb.first.c_str(), field.GetText(), data.fieldIndex);
            if (primVdb == nullptr) {
                continue;
            }
            gridVec.push_back(primVdb);
        }
        if (gridVec.empty()) {
            continue;
        }

        auto* shape = new HdArnoldShape(str::volume, _renderDelegate, id, GetPrimId());
        auto* volume = shape->GetShape();
        AiNodeSetStr(volume, str::name, AtString(TfStringPrintf("%s_p_%p", id.GetText(), volume).c_str()));
        htoaFnSet.convertPrimVdbToArnold(volume, static_cast<int>(gridVec.size()), gridVec.data());
        _inMemoryVolumes.push_back(shape);
    }
}

HdDirtyBits HdArnoldVolume::GetInitialDirtyBitsMask() const { return HdChangeTracker::AllDirty; }

HdDirtyBits HdArnoldVolume::_PropagateDirtyBits(HdDirtyBits bits) const { return bits & HdChangeTracker::AllDirty; }

void HdArnoldVolume::_InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits)
{
    TF_UNUSED(reprToken);
    TF_UNUSED(dirtyBits);
}

PXR_NAMESPACE_CLOSE_SCOPE
