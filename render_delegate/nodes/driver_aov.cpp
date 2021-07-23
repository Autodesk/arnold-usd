// Copyright 2020 Autodesk, Inc.
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
#include <ai.h>

#include <constant_strings.h>
#include "../render_buffer.h"

PXR_NAMESPACE_OPEN_SCOPE

AI_DRIVER_NODE_EXPORT_METHODS(HdArnoldDriverAOVMtd);

namespace {
const char* supportedExtensions[] = {nullptr};

struct DriverData {
    HdArnoldRenderBuffer* renderBuffer;
};

HdFormat _GetFormatFromArnoldType(const int arnoldType)
{
    if (arnoldType == AI_TYPE_RGBA) {
        return HdFormatFloat32Vec4;
    } else if (arnoldType == AI_TYPE_RGB || arnoldType == AI_TYPE_VECTOR) {
        return HdFormatFloat32Vec3;
    } else if (arnoldType == AI_TYPE_VECTOR2) {
        return HdFormatFloat32Vec2;
    } else if (arnoldType == AI_TYPE_FLOAT) {
        return HdFormatFloat32;
    } else if (arnoldType == AI_TYPE_INT) {
        return HdFormatInt32;
    } else {
        return HdFormatUNorm8;
    }
}

} // namespace

node_parameters { AiParameterPtr(str::aov_pointer, nullptr); }

node_initialize
{
    AiDriverInitialize(node, true);
    AiNodeSetLocalData(node, new DriverData());
}

node_update
{
    auto* data = reinterpret_cast<DriverData*>(AiNodeGetLocalData(node));
    data->renderBuffer = static_cast<HdArnoldRenderBuffer*>(AiNodeGetPtr(node, str::aov_pointer));
}

node_finish {}

driver_supports_pixel_type
{
    return pixel_type == AI_TYPE_RGBA || pixel_type == AI_TYPE_RGB || pixel_type == AI_TYPE_VECTOR ||
           pixel_type == AI_TYPE_VECTOR2 || pixel_type == AI_TYPE_FLOAT || pixel_type == AI_TYPE_FLOAT;
}

driver_extension { return supportedExtensions; }

driver_open {}

driver_needs_bucket { return true; }

driver_prepare_bucket {}

driver_process_bucket
{
    auto* driverData = reinterpret_cast<DriverData*>(AiNodeGetLocalData(node));
#if ARNOLD_VERSION_NUM > 60201
    AtString outputName;
#else
    const char* outputName = nullptr;
#endif
    int pixelType = AI_TYPE_RGBA;
    const void* bucketData = nullptr;
    while (AiOutputIteratorGetNext(iterator, &outputName, &pixelType, &bucketData)) {
        if (Ai_likely(driverData->renderBuffer != nullptr)) {
            driverData->renderBuffer->WriteBucket(
                bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, _GetFormatFromArnoldType(pixelType), bucketData);
        }
        // There will be only one aov assigned to each driver.
        break;
    }
}

driver_write_bucket {}

driver_close {}

PXR_NAMESPACE_CLOSE_SCOPE
