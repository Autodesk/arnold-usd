#include "privateSceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

/* virtual */
VtValue PrivateSceneDelegate::Get(SdfPath const& id, TfToken const& key)
{
    _ValueCache* vcache = TfMapLookupPtr(_valueCacheMap, id);
    VtValue ret;
    if (vcache && TfMapLookup(*vcache, key, &ret)) {
        return ret;
    }
    return VtValue();
}

/* virtual */
GfMatrix4d PrivateSceneDelegate::GetTransform(SdfPath const& id)
{
    // Extract from value cache.
    if (_ValueCache* vcache = TfMapLookupPtr(_valueCacheMap, id)) {
        if (VtValue* val = TfMapLookupPtr(*vcache, HdTokens->transform)) {
            if (val->IsHolding<GfMatrix4d>()) {
                return val->Get<GfMatrix4d>();
            }
        }
    }

    TF_CODING_ERROR(
        "Unexpected call to GetTransform for %s in HdxTaskController's "
        "internal scene delegate.\n",
        id.GetText());
    return GfMatrix4d(1.0);
}

/* virtual */
VtValue PrivateSceneDelegate::GetLightParamValue(SdfPath const& id, TfToken const& paramName)
{
    return Get(id, paramName);
}

/* virtual */
VtValue PrivateSceneDelegate::GetMaterialResource(SdfPath const& id) { return Get(id, TfToken("materialNetworkMap")); }

/* virtual */
bool PrivateSceneDelegate::IsEnabled(TfToken const& option) const { return HdSceneDelegate::IsEnabled(option); }

/* virtual */
HdRenderBufferDescriptor PrivateSceneDelegate::GetRenderBufferDescriptor(SdfPath const& id)
{
    return GetParameter<HdRenderBufferDescriptor>(id, TfToken("renderBufferDescriptor"));
}

/* virtual */
TfTokenVector PrivateSceneDelegate::GetTaskRenderTags(SdfPath const& taskId)
{
    if (HasParameter(taskId, TfToken("renderTags"))) {
        return GetParameter<TfTokenVector>(taskId, TfToken("renderTags"));
    }
    return TfTokenVector();
}

PXR_NAMESPACE_CLOSE_SCOPE
