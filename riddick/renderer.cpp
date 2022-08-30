
#include "renderer.h"

#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/driver.h"
#include "pxr/imaging/hd/engine.h"
#include "pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/rendererPlugin.h"
#include "pxr/imaging/hd/rendererPluginRegistry.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hdSt/hioConversions.h"
#include "pxr/imaging/hdx/renderTask.h"
#include "pxr/imaging/hio/image.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usdImaging/usdImaging/delegate.h"

#include <iostream>
#include <string>

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

static HdPluginRenderDelegateUniqueHandle CreateRenderDelegate()
{
    HdRendererPluginRegistry& registry = HdRendererPluginRegistry::GetInstance();
    return registry.CreateRenderDelegate(TfToken("HdArnoldRendererPlugin"));
}

static void WriteBufferToFile(HdRenderBuffer* renderBuffer, const std::string& outputImagePath)
{
    TF_VERIFY(renderBuffer != nullptr);
    renderBuffer->Resolve();

    HioImage::StorageSpec storage;
    storage.width = renderBuffer->GetWidth();
    storage.height = renderBuffer->GetHeight();
    storage.format = HdStHioConversions::GetHioFormat(renderBuffer->GetFormat());
    storage.flipped = true;
    storage.data = renderBuffer->Map();

    VtDictionary metadata;

    HioImageSharedPtr image = HioImage::OpenForWriting(outputImagePath);
    if (image) {
        image->Write(storage, metadata);
    }

    renderBuffer->Unmap();
}

//
// The main function to render to file with the arnold render delegate
//
void RenderToFile(
    UsdStageRefPtr stage, int width, int height, const SdfPath& cameraId, const std::string& outputImagePath)
{
    HdEngine _engine;

    HdPluginRenderDelegateUniqueHandle _renderDelegate = CreateRenderDelegate();
    TF_VERIFY(_renderDelegate);

    HdRenderIndex* _renderIndex = HdRenderIndex::New(_renderDelegate.Get(), HdDriverVector());
    TF_VERIFY(_renderIndex != nullptr);

    // Construct a new scene delegate to populate the render index.
    // TODO With the new sceneIndex mechanism, sceneDelegate will be deprecated in the future, so this will need to be
    // updated
    SdfPath _sceneDelegateId = SdfPath::AbsoluteRootPath();
    UsdImagingDelegate* _sceneDelegate = new UsdImagingDelegate(_renderIndex, _sceneDelegateId);
    TF_VERIFY(_sceneDelegate != nullptr);

    // A private scene delegate to store the tasks data
    PrivateSceneDelegate _privateSceneDelegate(_renderIndex, SdfPath("/privateScene/Delegate"));

    // Add a classic hydra render task. The data is stored in our private scene delegate
    SdfPath renderTaskId("/renderTask");
    _renderIndex->InsertTask<HdxRenderTask>(&_privateSceneDelegate, renderTaskId);

    // Populate the scene delegate with the content of the stage. We don't exclude any prims
    SdfPathVector _excludedPrimPaths;
    _sceneDelegate->Populate(stage->GetPrimAtPath(SdfPath::AbsoluteRootPath()), _excludedPrimPaths);

    //
    // Prepare the render task settings.
    //

    // First start with the aov. We are only interested by the color for the moment.
    HdRenderPassAovBinding aovBinding;
    HdFormat format = HdFormatInvalid;
    format = HdFormatUNorm8Vec4;
    aovBinding.aovName = HdAovTokens->color;
    aovBinding.clearValue = VtValue(GfVec4f(1.0f, 0.0f, 0.0f, 1.0f));
    SdfPath renderBufferId("/renderBuffer");
    aovBinding.renderBufferId = renderBufferId;

    HdxRenderTaskParams renderParams;
    renderParams.camera = cameraId;
    renderParams.viewport = GfVec4f(0, 0, width, height);
    renderParams.aovBindings.push_back(aovBinding);
    _renderIndex->InsertBprim(HdPrimTypeTokens->renderBuffer, &_privateSceneDelegate, renderBufferId);
    HdRenderBufferDescriptor desc;
    desc.dimensions = GfVec3i(width, height, 1);
    desc.format = HdFormatUNorm8Vec4; // This could be float, but arnold-usd needs to be setup consequently
    // desc.multiSampled = outputDescs[i].multiSampled; // TODO
    _privateSceneDelegate.SetParameter(renderBufferId, TfToken("renderBufferDescriptor"), desc);

    // Specify which prims we want to render
    TfToken materialTag = HdMaterialTagTokens->defaultMaterialTag;
    HdRprimCollection collection(
        HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull), false, materialTag);
    collection.SetRootPath(SdfPath::AbsoluteRootPath());

    TfTokenVector renderTags = {HdRenderTagTokens->geometry};

    _privateSceneDelegate.SetParameter(renderTaskId, HdTokens->params, renderParams);
    _privateSceneDelegate.SetParameter(renderTaskId, HdTokens->collection, collection);
    _privateSceneDelegate.SetParameter(renderTaskId, HdTokens->renderTags, renderTags);

    // Now we can start the rendering, picking up the renderTask
    std::shared_ptr<HdxRenderTask> renderTask =
        std::static_pointer_cast<HdxRenderTask>(_renderIndex->GetTask(renderTaskId));

    // We probably want to add the color correction task as well
    HdTaskSharedPtrVector tasks = {renderTask};
    do {
        _engine.Execute(_renderIndex, &tasks);
    } while (!renderTask->IsConverged());

    // Render is done, let's write the render buffer in an image
    HdRenderBuffer* renderBuffer =
        static_cast<HdRenderBuffer*>(_renderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, renderBufferId));
    WriteBufferToFile(renderBuffer, outputImagePath);
}

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
