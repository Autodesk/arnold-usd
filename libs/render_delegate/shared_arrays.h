
#pragma once

#include <ai.h>
#include <mutex>
#include <pxr/pxr.h>
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

// Compile time mapping of USD type to Arnold types
template<typename T> inline uint32_t GetArnoldTypeFor(const T &) {return AI_TYPE_UNDEFINED;}
template<> inline uint32_t GetArnoldTypeFor(const GfVec3f &) {return AI_TYPE_VECTOR;}
template<> inline uint32_t GetArnoldTypeFor(const GfVec2f &) {return AI_TYPE_VECTOR2;}
template<> inline uint32_t GetArnoldTypeFor(const VtArray<GfVec3f> &) {return AI_TYPE_VECTOR;}
template<> inline uint32_t GetArnoldTypeFor(const std::vector<GfVec3f> &) {return AI_TYPE_VECTOR;}
template<> inline uint32_t GetArnoldTypeFor(const VtArray<int> &) {return AI_TYPE_INT;}
template<> inline uint32_t GetArnoldTypeFor(const VtArray<unsigned int> &) {return AI_TYPE_UINT;}
template<> inline uint32_t GetArnoldTypeFor(const VtArray<GfVec2f> &) {return AI_TYPE_VECTOR2;}

// 
template <typename DerivedT>
struct ArrayOperations {
    template <typename T>
    inline AtArray* CreateAtArrayFromVtValue(const VtValue &value, int32_t forcedType = -1) {
        // Make sure the array contained has the correct type
        if (!value.IsHolding<T>()) {
            return nullptr;
        }

        // Unpack VtArray an call AtArray creation from VtArray in the derived class
        const T& vtArray = value.UncheckedGet<T>();
        auto *child = static_cast<DerivedT*>(this);
        return child->template CreateAtArrayFromVtArray<T>(vtArray, forcedType);
    }
};


struct ArrayCopier : public ArrayOperations<ArrayCopier> {

    template <typename T>
    inline AtArray* CreateAtArrayFromVtArray(
        const T& vtArray, int32_t forcedType = -1)
    {
        const void* vtArr = static_cast<const void*>(vtArray.cdata());
        if (!vtArr) {
            return nullptr;
        }
        const uint32_t nelements = vtArray.size();
        const uint32_t type = forcedType == -1 ? GetArnoldTypeFor(vtArray) : forcedType;
        return AiArrayConvert(nelements, 1, type, vtArray.data());
    }

    template <typename T>
    inline AtArray* CreateAtArrayFromTimeSamples(const HdArnoldSampledPrimvarType& timeSamples) {
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

#if ARNOLD_VERSION_NUM >= 70307
// Shared array buffer holder
struct ArrayHolder : public ArrayOperations<ArrayHolder> {
    // We need to keep a count here because we also hold the timesamples which could all point to the same buffer
    struct HeldArray {
        HeldArray(uint32_t nref_, const VtValue& val_) : nref(nref_), val(val_) {}
        uint32_t nref;
        VtValue val;
    };

    // This structure holds a Key Value map in a vector, which should have a smaller footprint in memory and be fast for small numbers of elements (<10)
    // It is interchangeable with a unordered_map in ArrayHolder. However for scenes with many timesamples the map can quickly fill and using a linear search
    // might become to slow compared to unordered_map.
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
            for (size_t i = 0; i < timeSamples.count; ++i) {
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
    AtArray* CreateAtArrayFromVtArray(
        const T& vtArray, int32_t forcedType = -1)
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

#else // ARNOLD_VERSION < 70307
using ArrayHandler = ArrayCopier;
#endif

PXR_NAMESPACE_CLOSE_SCOPE