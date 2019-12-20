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
// Modifications Copyright 2019 Autodesk, Inc.
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

#include <pxr/usd/sdf/assetPath.h>

#include "constant_strings.h"
#include "material.h"
#include "openvdb_asset.h"
#include "utils.h"

#include <pxr/base/arch/defines.h>
#include <pxr/base/arch/env.h>
#include <pxr/base/arch/library.h>
#include <pxr/base/tf/pathUtils.h>

// These don't seem to be publicly exposed anywhere?
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

using HouGetHoudiniVdbPrimitive = void* (*)(const char*, const char*);
using HouGetHoudiniVolumePrimitive = void* (*)(const char*, const char*, int);
struct HouFnSet {
    HouGetHoudiniVdbPrimitive getVdbVolumePrimitive = nullptr;
    HouGetHoudiniVolumePrimitive getHoudiniVolumePrimitive = nullptr;

    HouFnSet()
    {
        constexpr auto getVdbName = "SOPgetVDBVolumePrimitive";
        constexpr auto getHoudiniName = "SOPgetHoudiniVolumePrimitive";
        const auto HFS = ArchGetEnv("HFS");
        const auto dsoPath = HFS + ARCH_PATH_SEP + "houdini" + ARCH_PATH_SEP + "dso" + ARCH_PATH_SEP + "USD_SopVol" +
                             ARCH_LIBRARY_SUFFIX;
        // We don't have to worry about unloading the library, as our library
        // will be unloaded before Houdini exits.
        auto* sopVol = ArchLibraryOpen(dsoPath, ARCH_LIBRARY_NOW);
        if (sopVol == nullptr) {
            return;
        }
        getVdbVolumePrimitive = reinterpret_cast<HouGetHoudiniVdbPrimitive>(GETSYM(sopVol, getVdbName));
        getHoudiniVolumePrimitive = reinterpret_cast<HouGetHoudiniVolumePrimitive>(GETSYM(sopVol, getHoudiniName));
    }
};

const HouFnSet& GetHouFunctionSet()
{
    static HouFnSet ret;
    return ret;
}

using HtoAConvertPrimVdbToArnold = void (*)(void*, int, void**);

struct HtoAFnSet {
    HtoAConvertPrimVdbToArnold convertPrimVdbToArnold = nullptr;

    HtoAFnSet()
    {
        // The symbol is stored in _htoa_pygeo.so in python2.7libs, and
        // htoa is typically configured using HOUDINI_PATH. We should refine
        // this method in the future.
        constexpr auto convertVdbName = "HtoAConvertPrimVdbToArnold";
        const auto HOUDINI_PATH = ArchGetEnv("HOUDINI_PATH");
        void* htoaPygeo = nullptr;
        const auto houdiniPaths = TfStringSplit(HOUDINI_PATH, ARCH_PATH_LIST_SEP);
        for (const auto& houdiniPath : houdiniPaths) {
            const auto dsoPath = houdiniPath + ARCH_PATH_SEP + "python2.7libs" + ARCH_PATH_SEP + "_htoa_pygeo" +
// HTOA sets this library's extension to .so even on linux.
#ifdef ARCH_OS_WINDOWS
                                 ".dll"
#else
                                 ".so"
#endif
                ;
            htoaPygeo = ArchLibraryOpen(dsoPath, ARCH_LIBRARY_NOW);
            if (htoaPygeo != nullptr) {
                break;
            }
        }

        if (htoaPygeo == nullptr) {
            return;
        }

        convertPrimVdbToArnold = reinterpret_cast<HtoAConvertPrimVdbToArnold>(GETSYM(htoaPygeo, convertVdbName));
    }
};

const HtoAFnSet GetHtoAFunctionSet()
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

HdArnoldVolume::HdArnoldVolume(HdArnoldRenderDelegate* delegate, const SdfPath& id, const SdfPath& instancerId)
    : HdVolume(id, instancerId), _delegate(delegate)
{
}

HdArnoldVolume::~HdArnoldVolume()
{
    for (auto* volume : _volumes) {
        AiNodeDestroy(volume);
    }
    for (auto* volume : _inMemoryVolumes) {
        AiNodeDestroy(volume);
    }
}

void HdArnoldVolume::Sync(
    HdSceneDelegate* delegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    TF_UNUSED(reprToken);
    auto* param = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
    const auto& id = GetId();
    auto volumesChanged = false;
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        param->End();
        _CreateVolumes(id, delegate);
        volumesChanged = true;
    }

    if (volumesChanged || (*dirtyBits & HdChangeTracker::DirtyMaterialId)) {
        param->End();
        const auto* material = reinterpret_cast<const HdArnoldMaterial*>(
            delegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, delegate->GetMaterialId(id)));
        auto* volumeShader = material != nullptr ? material->GetVolumeShader() : _delegate->GetFallbackVolumeShader();
        for (auto& volume : _volumes) {
            AiNodeSetPtr(volume, str::shader, volumeShader);
        }
        for (auto& volume : _inMemoryVolumes) {
            AiNodeSetPtr(volume, str::shader, volumeShader);
        }
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param->End();
        HdArnoldSetTransform(_volumes, delegate, GetId());
        HdArnoldSetTransform(_inMemoryVolumes, delegate, GetId());
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void HdArnoldVolume::_CreateVolumes(const SdfPath& id, HdSceneDelegate* delegate)
{
    std::unordered_map<std::string, std::vector<TfToken>> openvdbs;
    std::unordered_map<std::string, std::vector<TfToken>> houVdbs;
    const auto fieldDescriptors = delegate->GetVolumeFieldDescriptors(id);
    for (const auto& field : fieldDescriptors) {
        auto* openvdbAsset = dynamic_cast<HdArnoldOpenvdbAsset*>(
            delegate->GetRenderIndex().GetBprim(_tokens->openvdbAsset, field.fieldId));
        if (openvdbAsset == nullptr) {
            continue;
        }
        openvdbAsset->TrackVolumePrimitive(id);
        const auto vv = delegate->Get(field.fieldId, _tokens->filePath);
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
            [&openvdbs](AtNode* node) -> bool {
                if (openvdbs.find(std::string(AiNodeGetStr(node, str::filename).c_str())) == openvdbs.end()) {
                    AiNodeDestroy(node);
                    return true;
                }
                return false;
            }),
        _volumes.end());

    for (const auto& openvdb : openvdbs) {
        AtNode* volume = nullptr;
        for (auto& v : _volumes) {
            if (openvdb.first == AiNodeGetStr(v, str::filename).c_str()) {
                volume = v;
                break;
            }
        }
        if (volume == nullptr) {
            volume = AiNode(_delegate->GetUniverse(), str::volume);
            AiNodeSetUInt(volume, str::id, static_cast<unsigned int>(GetPrimId()) + 1);
            AiNodeSetStr(volume, str::filename, openvdb.first.c_str());
            AiNodeSetStr(volume, str::name, id.AppendChild(TfToken(TfStringPrintf("p_%p", volume))).GetText());
            _volumes.push_back(volume);
        }
        const auto numFields = openvdb.second.size();
        auto* fields = AiArrayAllocate(numFields, 1, AI_TYPE_STRING);
        for (auto i = decltype(numFields){0}; i < numFields; ++i) {
            AiArraySetStr(fields, i, AtString(openvdb.second[i].GetText()));
        }
        AiNodeSetArray(volume, str::grids, fields);
    }

    for (auto* volume : _inMemoryVolumes) {
        AiNodeDestroy(volume);
    }
    _inMemoryVolumes.clear();

    if (houVdbs.empty()) {
        return;
    }

    const auto& houFnSet = GetHouFunctionSet();
    if (houFnSet.getVdbVolumePrimitive == nullptr || houFnSet.getHoudiniVolumePrimitive == nullptr) {
        return;
    }

    const auto& htoaFnSet = GetHtoAFunctionSet();
    if (htoaFnSet.convertPrimVdbToArnold == nullptr) {
        return;
    }

    for (const auto& houVdb : houVdbs) {
        std::vector<void*> gridVec;
        for (const auto& field : houVdb.second) {
            auto* primVdb = houFnSet.getVdbVolumePrimitive(houVdb.first.c_str(), field.GetText());
            if (primVdb == nullptr) {
                continue;
            }
            gridVec.push_back(primVdb);
        }
        if (gridVec.empty()) {
            continue;
        }

        auto* volume = AiNode(_delegate->GetUniverse(), str::volume);
        AiNodeSetStr(volume, str::name, id.AppendChild(TfToken(TfStringPrintf("p_%p", volume))).GetText());
        htoaFnSet.convertPrimVdbToArnold(volume, static_cast<int>(gridVec.size()), gridVec.data());
        AiNodeSetUInt(volume, str::id, static_cast<unsigned int>(GetPrimId()) + 1);
        _inMemoryVolumes.push_back(volume);
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
