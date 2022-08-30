#pragma once

#include "pxr/usd/sdf/path.h"
#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

// A private scene delegate we use to store our tasks data
// This code is a copy from the UsdImagingGL testing suite code
class PrivateSceneDelegate : public HdSceneDelegate {
public:
    PrivateSceneDelegate(HdRenderIndex* parentIndex, SdfPath const& delegateID)
        : HdSceneDelegate(parentIndex, delegateID)
    {
    }
    ~PrivateSceneDelegate() override = default;

    // HdxTaskController set/get interface
    template <typename T>
    void SetParameter(SdfPath const& id, TfToken const& key, T const& value)
    {
        _valueCacheMap[id][key] = value;
    }
    template <typename T>
    T GetParameter(SdfPath const& id, TfToken const& key) const
    {
        VtValue vParams;
        _ValueCache vCache;
        TF_VERIFY(
            TfMapLookup(_valueCacheMap, id, &vCache) && TfMapLookup(vCache, key, &vParams) && vParams.IsHolding<T>());
        return vParams.Get<T>();
    }
    bool HasParameter(SdfPath const& id, TfToken const& key) const
    {
        _ValueCache vCache;
        if (TfMapLookup(_valueCacheMap, id, &vCache) && vCache.count(key) > 0) {
            return true;
        }
        return false;
    }

    VtValue Get(SdfPath const& id, TfToken const& key) override;
    GfMatrix4d GetTransform(SdfPath const& id) override;
    VtValue GetLightParamValue(SdfPath const& id, TfToken const& paramName) override;
    VtValue GetMaterialResource(SdfPath const& id) override;
    bool IsEnabled(TfToken const& option) const override;
    HdRenderBufferDescriptor GetRenderBufferDescriptor(SdfPath const& id) override;
    TfTokenVector GetTaskRenderTags(SdfPath const& taskId) override;

private:
    using _ValueCache = TfHashMap<TfToken, VtValue, TfToken::HashFunctor>;
    using _ValueCacheMap = TfHashMap<SdfPath, _ValueCache, SdfPath::Hash>;
    _ValueCacheMap _valueCacheMap;
};

PXR_NAMESPACE_CLOSE_SCOPE


