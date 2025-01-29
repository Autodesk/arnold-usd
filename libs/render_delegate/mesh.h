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
/// @file mesh.h
///
/// Utilities for translating Hydra Meshes for the Render Delegate.
#pragma once

#include "api.h"

#include <ai.h>
#include <mutex>
#include <pxr/pxr.h>

#include <pxr/imaging/hd/mesh.h>

#include "hdarnold.h"
#include "render_delegate.h"
#include "rprim.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE


// Compile time mapping of USD type to Arnold types
template<typename T> inline uint32_t GetArnoldTypeFor(const T &) {return AI_TYPE_UNDEFINED;}
template<> inline uint32_t GetArnoldTypeFor(const GfVec3f &) {return AI_TYPE_VECTOR;}
template<> inline uint32_t GetArnoldTypeFor(const VtArray<GfVec3f> &) {return AI_TYPE_VECTOR;}
template<> inline uint32_t GetArnoldTypeFor(const std::vector<GfVec3f> &) {return AI_TYPE_VECTOR;}
template<> inline uint32_t GetArnoldTypeFor(const VtArray<int> &) {return AI_TYPE_INT;}
template<> inline uint32_t GetArnoldTypeFor(const VtArray<unsigned int> &) {return AI_TYPE_UINT;}

// 
struct ArrayCopier {

    template <typename T>
    AtArray* CreateAtArrayFromBuffer(
        const T& vtArray, int32_t forcedType = -1, std::string param = std::string())
    {
        const void* vtArr = static_cast<const void*>(vtArray.cdata());
        if (!vtArr) {
            return nullptr;
        }
        const uint32_t nelements = vtArray.size();
        const uint32_t type = forcedType == -1 ? GetArnoldTypeFor(vtArray) : forcedType;
        
        AtArray *atArr = AiArrayAllocate(nelements, 1, type);
        AiArraySetKey(atArr, 0, vtArray.data());
        return atArr;
    }

    template <typename T>
    AtArray* CreateAtArrayFromTimeSamples(const HdArnoldSampledPrimvarType& timeSamples) {
        if (timeSamples.count == 0)
            return nullptr;

        // Unbox
        HdArnoldSampledType<T> xf;
        xf.UnboxFrom(timeSamples);
        const auto &v0 = xf.values[0];
        const uint32_t nelements = v0.size();
        const uint32_t type = GetArnoldTypeFor(v0);
        AtArray* arr = AiArrayAllocate(nelements, xf.count, type);
        for (size_t index = 0; index < xf.count; index++) {
            const auto &data = xf.values[index];
            AiArraySetKey(arr, index, data.data());
        }
        return arr;
    }

    inline bool empty() const {
        return true;
    }
};

// Shared array buffer holder
struct ArrayHolder {
    // We need to keep a count here because we also hold the timesamples which could all point to the same buffer
    struct HeldArray {
        HeldArray(uint32_t nref_, const VtValue& val_) : nref(nref_), val(val_) {}
        uint32_t nref;
        VtValue val;
    };

    // This structure holds a Key Value map in a vector, which should have a smaller footprint in memory and be fast for small numbers of elements (<10)
    // It is interchangeable with a unordered_map in ArrayHolder
    template <typename Key, typename Value>
    struct linear_map : std::vector<std::pair<Key, Value>> {
        auto find(const Key &key) {
            return std::find_if(this->begin(), this->end(), [&key](const std::pair<Key, Value> &val){return val.first == key;});
        }

        auto emplace(const Key &key, const Value &val) {
            return this->emplace_back(key, val);
        }
        // We might want to erase by resetting the value without resizing the vector.
        // At the moment there are only a few elements stored, so it's probably not worth doing it now
        // and we should benchmark it, avd we would have to implement the function "empty" to return the number of non null key
        // auto erase(std::vector<std::pair<Key, Value>>::iterator it) {
        //     it->first = nullptr;
        //     it->second = HeldArray();
        // }
    };

    // Previously we were using an unordered_map:
    //using BufferMapT = std::unordered_map<const void*, HeldArray>;
    using BufferMapT = linear_map<const void*, HeldArray>;
    BufferMapT _bufferMap;
    // bufferMapMutex is used to make sure the bufferMap is not accessed concurrently in the Sync function.
    std::mutex _bufferMapMutex;

    // TODO we could store the created AtArray and reuse it to benefit from usd deduplication
    template <typename T>
    AtArray* CreateAtArrayFromTimeSamples(const HdArnoldSampledPrimvarType& timeSamples)
    {
        if (timeSamples.count == 0)
            return nullptr;

        // Unbox
        HdArnoldSampledType<T> unboxed;
        unboxed.UnboxFrom(timeSamples);

        std::vector<const void*> ptrsToSamples; // use SmallVector ??
        for (int i = 0; i < unboxed.count; ++i) {
            const auto& val = unboxed.values[i];
            ptrsToSamples.push_back(val.data());
        }
        const uint32_t nelements = unboxed.values[0].size(); // TODO make sure that values has something
        const uint32_t type = GetArnoldTypeFor(unboxed.values[0]);
        const uint32_t nkeys = ptrsToSamples.size();
        const void** samples = ptrsToSamples.data();

        const std::lock_guard<std::mutex> lock(_bufferMapMutex);

        AtArray* atArray = AiArrayMakeShared(nelements, nkeys, type, samples, ReleaseArrayCallback, this);
        if (atArray) {
            for (int i = 0; i < timeSamples.count; ++i) {
                const VtValue& val = timeSamples.values[i];
                if (val.template IsHolding<T>()) {
                    const T& arr = val.template UncheckedGet<T>();
                    const void* ptr = static_cast<const void*>(arr.cdata());
                    auto bufferIt = _bufferMap.find(ptr);
                    if (bufferIt == _bufferMap.end()) {
                        _bufferMap.emplace(ptr, HeldArray{1, val});
                    } else {
                        HeldArray& held = bufferIt->second;
                        held.nref++;
                    }
                }
            }
        }
        return atArray;
    }

    template <typename T>
    AtArray* CreateAtArrayFromBuffer(
        const T& vtArray, int32_t forcedType = -1, std::string param = std::string())
    {
        const void* arr = static_cast<const void*>(vtArray.cdata());
        // Look for at AtArray stored with the same buffer
        if (!arr) {
            return nullptr;
        }
        const uint32_t nelements = vtArray.size();
        const uint32_t type = forcedType == -1 ? GetArnoldTypeFor(vtArray) : forcedType;
        
        const std::lock_guard<std::mutex> lock(_bufferMapMutex);
        AtArray*  atArray = AiArrayMakeShared(nelements, type, arr, ReleaseArrayCallback, this);
        if (atArray) {
            auto it = _bufferMap.find(arr);
            if (it == _bufferMap.end()) {
                _bufferMap.emplace(arr, HeldArray{1, VtValue(vtArray)});
            } else {
                // This should rarely happen, only when a single buffer is shared inside keys
                HeldArray& held = it->second;
                held.nref++;
            }
        }
        return atArray;
    }

    inline
    static void ReleaseArrayCallback(uint8_t nkeys, const void** buffers, const void* userData)
    {
        void *bufferHolderPtr = const_cast<void*>(userData);
        if (bufferHolderPtr) {
            ArrayHolder *ArrayHolder = static_cast<struct ArrayHolder*>(bufferHolderPtr);
            ArrayHolder->ReleaseArray(nkeys, buffers);
        }
    }

    inline
    void ReleaseArray(uint8_t nkeys, const void** buffers)
    {
        const std::lock_guard<std::mutex> lock(_bufferMapMutex);
        for (int i = 0; i < nkeys; ++i) {
            const void* arr = buffers[i];
            if (arr) {
                auto it = _bufferMap.find(arr);
                if (it != _bufferMap.end()) {
                    HeldArray& arr = it->second;
                    arr.nref--;
                    if (arr.nref == 0) {
                        _bufferMap.erase(it);
                    }
                } else {
                    assert(false); // this should never happen, catch it in debug mode
                }
            }
        }
    }

    inline bool empty() const {
        return _bufferMap.empty();
    }
};

#ifdef ENABLE_SHARED_ARRAYS
using ArrayHandler = ArrayHolder;
#else
using ArrayHandler = ArrayCopier;
#endif

/// Utility class for translating Hydra Mesh to Arnold Polymesh.
class HdArnoldMesh : public HdArnoldRprim<HdMesh> {
public:
    /// Constructor for HdArnoldMesh.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the mesh.
    HDARNOLD_API
    HdArnoldMesh(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

    /// Destructor for HdArnoldMesh.
    ///
    /// Destory all Arnold Polymeshes and Ginstances.
    ~HdArnoldMesh();

    /// Syncs the Hydra Mesh to the Arnold Polymesh.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderParam Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    /// @param reprToken Token describing the representation of the mesh.
    HDARNOLD_API
    void Sync(
        HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
        const TfToken& reprToken) override;

    /// Returns the initial Dirty Bits for the Primitive.
    ///
    /// @return Initial Dirty Bits.
    HDARNOLD_API
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    /// Returns true if step size is bigger than zero, false otherwise.
    ///
    /// @return True if polymesh is a volume boundary.
    HDARNOLD_API
    bool _IsVolume() const;

    HDARNOLD_API
    AtNode *_GetMeshLight(HdSceneDelegate* sceneDelegate, const SdfPath& id);

    HdArnoldPrimvarMap _primvars;     ///< Precomputed list of primvars.
    HdArnoldSubsets _subsets;         ///< Material ids from subsets.
    VtValue _vertexCountsVtValue;     ///< Vertex nsides
    VtValue _vertexIndicesVtValue;
    size_t _vertexCountSum = 0;       ///< Sum of the vertex counts array.
    size_t _numberOfPositionKeys = 1; ///< Number of vertex position keys for the mesh.
    AtNode *_geometryLight = nullptr; ///< Eventual mesh light for this polymesh
    ArrayHandler _arrayHandler; ///< Structure managing the Vt and At arrays
};

PXR_NAMESPACE_CLOSE_SCOPE
