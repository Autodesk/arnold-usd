
#include "renderer.h"

#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/driver.h"
#include "pxr/imaging/hd/engine.h"
#include "pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/rendererPlugin.h"
#include "pxr/imaging/hd/rendererPluginRegistry.h"
#include "pxr/imaging/hdSt/hioConversions.h"
#include "pxr/imaging/hdx/renderTask.h"
#include "pxr/imaging/hio/image.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usdImaging/usdImaging/delegate.h"

#include "privateSceneDelegate.h"

#include <iostream>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

/// Returns the HdArnold render delegate
static HdPluginRenderDelegateUniqueHandle CreateRenderDelegate()
{
    HdRendererPluginRegistry& registry = HdRendererPluginRegistry::GetInstance();
    return registry.CreateRenderDelegate(TfToken("HdArnoldRendererPlugin"));
}

/// Simple function to write a render buffer in an image file
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
    UsdStageRefPtr stage, int width, int height, const UsdTimeCode &timeCode, const SdfPath& cameraId, const std::string& outputImagePath)
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
    _sceneDelegate->SetTime(timeCode);

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

    // Free memory
    delete _sceneDelegate;
    delete _renderIndex;
}

PXR_NAMESPACE_CLOSE_SCOPE
