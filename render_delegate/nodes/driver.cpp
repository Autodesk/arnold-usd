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
#include <ai.h>

#include <tbb/concurrent_queue.h>

#include <memory>

#include "../constant_strings.h"
#include "../render_buffer.h"
#include "../utils.h"
#include "nodes.h"

PXR_NAMESPACE_OPEN_SCOPE

AI_DRIVER_NODE_EXPORT_METHODS(HdArnoldDriverMtd);

AtString HdArnoldDriver::projMtx("projMtx");
AtString HdArnoldDriver::viewMtx("viewMtx");

namespace {
const char* supportedExtensions[] = {nullptr};

struct DriverData {
    GfMatrix4f projMtx;
    GfMatrix4f viewMtx;
    HdArnoldRenderBufferStorage* renderBuffers;
    // Local storage for converting from P to depth.
    std::vector<float> depths[AI_MAX_THREADS];
    // Local storage for the id remapping
    std::vector<int> ids[AI_MAX_THREADS];
};

} // namespace

node_parameters
{
    AiParameterMtx(HdArnoldDriver::projMtx, AiM4Identity());
    AiParameterMtx(HdArnoldDriver::viewMtx, AiM4Identity());
    AiParameterPtr(str::aov_pointer, nullptr);
}

node_initialize
{
    AiDriverInitialize(node, true);
    AiNodeSetLocalData(node, new DriverData());
}

node_update
{
    auto* data = reinterpret_cast<DriverData*>(AiNodeGetLocalData(node));
    data->projMtx = HdArnoldConvertMatrix(AiNodeGetMatrix(node, HdArnoldDriver::projMtx));
    data->viewMtx = HdArnoldConvertMatrix(AiNodeGetMatrix(node, HdArnoldDriver::viewMtx));
    data->renderBuffers = static_cast<HdArnoldRenderBufferStorage*>(AiNodeGetPtr(node, str::aov_pointer));
}

node_finish {}

driver_supports_pixel_type
{
    return pixel_type == AI_TYPE_RGBA || pixel_type == AI_TYPE_RGB || pixel_type == AI_TYPE_VECTOR ||
           pixel_type == AI_TYPE_UINT;
}

driver_extension { return supportedExtensions; }

driver_open {}

driver_needs_bucket { return true; }

driver_prepare_bucket {}

driver_process_bucket
{
    const auto* driverData = reinterpret_cast<const DriverData*>(AiNodeGetLocalData(node));
    const char* outputName = nullptr;
    int pixelType = AI_TYPE_RGBA;
    const void* bucketData = nullptr;
    while (AiOutputIteratorGetNext(iterator, &outputName, &pixelType, &bucketData)) {
        if (pixelType == AI_TYPE_VECTOR && strcmp(outputName, "P") == 0) {
            /*data->depth.resize(bucketSize, 1.0f);
            const auto* pp = reinterpret_cast<const GfVec3f*>(bucketData);
            auto* pz = data->depth.data();
            for (auto i = decltype(bucketSize){0}; i < bucketSize; ++i) {
                // Rays hitting the background will return a (0,0,0) vector. We don't worry about it, as background
                // pixels will be marked with an ID of 0 by arnold.
                const auto p = driverData->projMtx.Transform(driverData->viewMtx.Transform(pp[i]));
                pz[i] = std::max(-1.0f, std::min(1.0f, p[2]));
            }*/
        } else if (pixelType == AI_TYPE_UINT && strcmp(outputName, "ID") == 0) {
            // TODO(pal): Remap to int, and decrement the buffer with one
        } else if (pixelType == AI_TYPE_RGB) {
            // We are remapping RGBA buffer to color.
            const TfToken bufferName(strcmp(outputName, "RGBA") == 0 ? "color" : outputName);
            // This shouldn't happen, but we are double checking.
            if (Ai_unlikely(bufferName == HdAovTokens->depth)) {
                continue;
            }
            const auto it = driverData->renderBuffers->find(bufferName);
            if (it != driverData->renderBuffers->end()) {
                if (it->second != nullptr) {
                    it->second->WriteBucket(
                        bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, HdFormatFloat32Vec3, bucketData);
                }
            }
        }
    }
}

driver_write_bucket {}

driver_close {}

PXR_NAMESPACE_CLOSE_SCOPE
