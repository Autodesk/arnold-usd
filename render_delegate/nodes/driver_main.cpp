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

#include <algorithm>
#include <memory>

#include <constant_strings.h>
#include "../render_buffer.h"
#include "../utils.h"

PXR_NAMESPACE_OPEN_SCOPE

AI_DRIVER_NODE_EXPORT_METHODS(HdArnoldDriverMainMtd);

namespace {
const char* supportedExtensions[] = {nullptr};

struct DriverData {
    GfMatrix4f projMtx;
    GfMatrix4f viewMtx;
    HdArnoldRenderBuffer* colorBuffer = nullptr;
    HdArnoldRenderBuffer* depthBuffer = nullptr;
    HdArnoldRenderBuffer* idBuffer = nullptr;
    // Local storage for converting from P to depth.
    std::vector<float> depths[AI_MAX_THREADS];
    // Local storage for the id remapping.
    std::vector<int> ids[AI_MAX_THREADS];
    // Local storage for the color buffer.
    std::vector<AtRGBA> colors[AI_MAX_THREADS];
};

} // namespace

node_parameters
{
    AiParameterMtx(str::projMtx, AiM4Identity());
    AiParameterMtx(str::viewMtx, AiM4Identity());
    AiParameterPtr(str::color_pointer, nullptr);
    AiParameterPtr(str::depth_pointer, nullptr);
    AiParameterPtr(str::id_pointer, nullptr);
}

node_initialize
{
    AiDriverInitialize(node, true);
    AiNodeSetLocalData(node, new DriverData());
}

node_update
{
    auto* data = reinterpret_cast<DriverData*>(AiNodeGetLocalData(node));
    data->projMtx = HdArnoldConvertMatrix(AiNodeGetMatrix(node, str::projMtx));
    data->viewMtx = HdArnoldConvertMatrix(AiNodeGetMatrix(node, str::viewMtx));
    data->colorBuffer = static_cast<HdArnoldRenderBuffer*>(AiNodeGetPtr(node, str::color_pointer));
    data->depthBuffer = static_cast<HdArnoldRenderBuffer*>(AiNodeGetPtr(node, str::depth_pointer));
    data->idBuffer = static_cast<HdArnoldRenderBuffer*>(AiNodeGetPtr(node, str::id_pointer));
}

node_finish {}

driver_supports_pixel_type
{
    return pixel_type == AI_TYPE_RGBA || pixel_type == AI_TYPE_VECTOR || pixel_type == AI_TYPE_UINT;
}

driver_extension { return supportedExtensions; }

driver_open {}

driver_needs_bucket { return true; }

driver_prepare_bucket {}

driver_process_bucket
{
    auto* driverData = reinterpret_cast<DriverData*>(AiNodeGetLocalData(node));
    const char* outputName = nullptr;
    int pixelType = AI_TYPE_RGBA;
    const void* bucketData = nullptr;
    const auto pixelCount = bucket_size_x * bucket_size_y;
    // We should almost always have depth and id.
    auto& ids = driverData->ids[tid];
    ids.clear();
    const void* colorData = nullptr;
    const void* positionData = nullptr;
    while (AiOutputIteratorGetNext(iterator, &outputName, &pixelType, &bucketData)) {
        if (pixelType == AI_TYPE_VECTOR && strcmp(outputName, "P") == 0) {
            positionData = bucketData;
        } else if (pixelType == AI_TYPE_UINT && strcmp(outputName, "ID") == 0) {
            if (driverData->idBuffer) {
                ids.resize(pixelCount, -1);
                const auto* in = static_cast<const unsigned int*>(bucketData);
                for (auto i = decltype(pixelCount){0}; i < pixelCount; i += 1) {
                    ids[i] = static_cast<int>(in[i]) - 1;
                }
                driverData->idBuffer->WriteBucket(
                    bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, HdFormatInt32, ids.data());
            }
        } else if (pixelType == AI_TYPE_RGBA && strcmp(outputName, "RGBA") == 0) {
            colorData = bucketData;
        }
    }
    if (positionData != nullptr && driverData->depthBuffer != nullptr) {
        auto& depth = driverData->depths[tid];
        depth.resize(pixelCount, 1.0f);
        const auto* in = static_cast<const GfVec3f*>(positionData);
        if (ids.empty()) {
            for (auto i = decltype(pixelCount){0}; i < pixelCount; i += 1) {
                const auto p = driverData->projMtx.Transform(driverData->viewMtx.Transform(in[i]));
#ifdef USD_HAS_ZERO_TO_ONE_DEPTH
                depth[i] = (std::max(-1.0f, std::min(1.0f, p[2])) + 1.0f) / 2.0f;
#else
                depth[i] = std::max(-1.0f, std::min(1.0f, p[2]));
#endif
            }
        } else {
            for (auto i = decltype(pixelCount){0}; i < pixelCount; i += 1) {
                if (ids[i] == -1) {
                    depth[i] = 1.0f;
                } else {
                    const auto p = driverData->projMtx.Transform(driverData->viewMtx.Transform(in[i]));
#ifdef USD_HAS_ZERO_TO_ONE_DEPTH
                    depth[i] = (std::max(-1.0f, std::min(1.0f, p[2])) + 1.0f) / 2.0f;
#else
                    depth[i] = std::max(-1.0f, std::min(1.0f, p[2]));
#endif
                }
            }
        }
        driverData->depthBuffer->WriteBucket(
            bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, HdFormatFloat32, depth.data());
    }
    if (colorData != nullptr && driverData->colorBuffer) {
        if (ids.empty()) {
            driverData->colorBuffer->WriteBucket(
                bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, HdFormatFloat32Vec4, colorData);
        } else {
            auto& color = driverData->colors[tid];
            color.resize(pixelCount, AI_RGBA_ZERO);
            const auto* in = static_cast<const AtRGBA*>(colorData);
            for (auto i = decltype(pixelCount){0}; i < pixelCount; i += 1) {
                color[i] = in[i];
                if (ids[i] == -1) {
                    color[i].a = 0.0f;
                }
            }
            driverData->colorBuffer->WriteBucket(
                bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, HdFormatFloat32Vec4, color.data());
        }
    }
}

driver_write_bucket {}

driver_close {}

PXR_NAMESPACE_CLOSE_SCOPE
