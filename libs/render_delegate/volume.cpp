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

#include <pxr/base/tf/dl.h>

#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/instancer.h>

#include <pxr/usd/sdf/assetPath.h>

#include <constant_strings.h>
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
using HoudiniGetVdbPrimitive = void* (*)(const char*, const char*);
using HoudiniGetVolumePrimitive = void* (*)(const char*, const char*, int);
struct HoudiniFnSet {
    HoudiniGetVdbPrimitive getVdbPrimitive = nullptr;
    HoudiniGetVolumePrimitive getVolumePrimitive = nullptr;

    /// We need to load USD_SopVol.(so|dylib|dll) to access the volume function
    /// pointers.
    HoudiniFnSet()
    {
        constexpr auto getVdbName = "SOPgetVDBVolumePrimitive";
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
        getVdbPrimitive = reinterpret_cast<HoudiniGetVdbPrimitive>(GETSYM(sopVol, getVdbName));
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

} // namespace

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (openvdbAsset)
    (filePath)
);
// clang-format on

#if PXR_VERSION >= 2102
HdArnoldVolume::HdArnoldVolume(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdVolume(id), _renderDelegate(renderDelegate)
{
}
#else
HdArnoldVolume::HdArnoldVolume(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id, const SdfPath& instancerId)
    : HdVolume(id, instancerId), _renderDelegate(renderDelegate)
{
}
#endif

HdArnoldVolume::~HdArnoldVolume()
{
    // Stop tracking dependencies for this prim
    _renderDelegate->ClearDependencies(GetId());
    _ForEachVolume([](HdArnoldShape* s) { delete s; });
}

void HdArnoldVolume::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    TF_UNUSED(reprToken);
    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id = GetId();
    auto volumesChanged = false;
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        param.Interrupt();
        _CreateVolumes(id, sceneDelegate);
        volumesChanged = true;
    }

    if (volumesChanged || (*dirtyBits & HdChangeTracker::DirtyMaterialId)) {
        param.Interrupt();
        const auto materialId = sceneDelegate->GetMaterialId(id);
        // Ensure the reference from this shape to its material is properly tracked
        // by the render delegate
        _renderDelegate->TrackDependencies(id, HdArnoldRenderDelegate::PathSetWithDirtyBits {{materialId, HdChangeTracker::DirtyMaterialId}});

        const auto* material = reinterpret_cast<const HdArnoldNodeGraph*>(
            sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));
        auto* volumeShader =
            material != nullptr ? material->GetVolumeShader() : _renderDelegate->GetFallbackVolumeShader();
        _ForEachVolume([&](HdArnoldShape* s) { AiNodeSetPtr(s->GetShape(), str::shader, volumeShader); });
    }

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param.Interrupt();
        _ForEachVolume([&](HdArnoldShape* s) { HdArnoldSetTransform(s->GetShape(), sceneDelegate, GetId()); });
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
        const auto doubleSided = sceneDelegate->GetDoubleSided(id);
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

#if PXR_VERSION >= 2102
    // Newer USD versions need to update the instancer before accessing the instancer id.
    _UpdateInstancer(sceneDelegate, dirtyBits);
    // We also force syncing of the parent instancers.
    HdInstancer::_SyncInstancerAndParents(sceneDelegate->GetRenderIndex(), GetInstancerId());
#endif

    _ForEachVolume(
        [&](HdArnoldShape* shape) { shape->Sync(this, *dirtyBits, sceneDelegate, param, transformDirtied); });

    *dirtyBits = HdChangeTracker::Clean;
}

void HdArnoldVolume::_CreateVolumes(const SdfPath& id, HdSceneDelegate* sceneDelegate)
{
    std::unordered_map<std::string, std::vector<TfToken>> openvdbs;
    std::unordered_map<std::string, std::vector<TfToken>> houVdbs;
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
            if (TfStringStartsWith(path, "op:")) {
                auto& fields = houVdbs[path];
                if (std::find(fields.begin(), fields.end(), field.fieldName) == fields.end()) {
                    fields.push_back(field.fieldName);
                }
                continue;
            }
            auto& fields = openvdbs[path];
            if (std::find(fields.begin(), fields.end(), field.fieldName) == fields.end()) {
                fields.push_back(field.fieldName);
            }
        }
    }

    _volumes.erase(
        std::remove_if(
            _volumes.begin(), _volumes.end(),
            [&openvdbs](HdArnoldShape* shape) -> bool {
                if (openvdbs.find(std::string(AiNodeGetStr(shape->GetShape(), str::filename).c_str())) ==
                    openvdbs.end()) {
                    delete shape;
                    return true;
                }
                return false;
            }),
        _volumes.end());

    for (const auto& openvdb : openvdbs) {
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
            AiArraySetStr(fields, i, AtString(openvdb.second[i].GetText()));
        }
        AiNodeSetArray(volume, str::grids, fields);
    }

    for (auto* volume : _inMemoryVolumes) {
        delete volume;
    }
    _inMemoryVolumes.clear();

    if (houVdbs.empty()) {
        return;
    }

    const auto& houdiniFnSet = _GetHoudiniFunctionSet();
    if (houdiniFnSet.getVdbPrimitive == nullptr || houdiniFnSet.getVolumePrimitive == nullptr) {
        return;
    }

    const auto& htoaFnSet = _GetHtoAFunctionSet();
    if (htoaFnSet.convertPrimVdbToArnold == nullptr) {
        return;
    }

    for (const auto& houVdb : houVdbs) {
        std::vector<void*> gridVec;
        for (const auto& field : houVdb.second) {
            auto* primVdb = houdiniFnSet.getVdbPrimitive(houVdb.first.c_str(), field.GetText());
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
