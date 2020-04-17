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

#include "../constant_strings.h"
#include "../render_buffer.h"
#include "../utils.h"

PXR_NAMESPACE_OPEN_SCOPE

AI_DRIVER_NODE_EXPORT_METHODS(HdArnoldDriverMtd);

namespace {
const char* supportedExtensions[] = {nullptr};

struct BucketData {
    TfToken name;
    int type;
    const void* data;

    BucketData(const TfToken& _name, int _type, const void* _data) : name(_name), type(_type), data(_data) {}
};

struct DriverData {
    GfMatrix4f projMtx;
    GfMatrix4f viewMtx;
    HdArnoldRenderBufferStorage* renderBuffers;
    // Local storage for converting from P to depth.
    std::vector<float> depths[AI_MAX_THREADS];
    // Local storage for the id remapping.
    std::vector<int> ids[AI_MAX_THREADS];
    // Local storage for the color buffer.
    std::vector<AtRGBA> colors[AI_MAX_THREADS];
    // Local storage for bucket data.
    std::vector<BucketData> buckets[AI_MAX_THREADS];
};

} // namespace

node_parameters
{
    AiParameterMtx(str::projMtx, AiM4Identity());
    AiParameterMtx(str::viewMtx, AiM4Identity());
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
    data->projMtx = HdArnoldConvertMatrix(AiNodeGetMatrix(node, str::projMtx));
    data->viewMtx = HdArnoldConvertMatrix(AiNodeGetMatrix(node, str::viewMtx));
    data->renderBuffers = static_cast<HdArnoldRenderBufferStorage*>(AiNodeGetPtr(node, str::aov_pointer));
    // TODO(pal): Clear AOVs.
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
    auto* driverData = reinterpret_cast<DriverData*>(AiNodeGetLocalData(node));
    const char* outputName = nullptr;
    int pixelType = AI_TYPE_RGBA;
    const void* bucketData = nullptr;
    // TODO: IDs are used to detect background pixels, so we have to handle them first. We should do a two phase
    //  iteration, first gather all the available data and handle ids, then process the rest.
    const auto pixelCount = bucket_size_x * bucket_size_y;
    // We should almost always have depth and id.
    auto& ids = driverData->ids[tid];
    ids.clear();
    auto& buckets = driverData->buckets[tid];
    buckets.clear();
    while (AiOutputIteratorGetNext(iterator, &outputName, &pixelType, &bucketData)) {
        if (pixelType == AI_TYPE_VECTOR && strcmp(outputName, "P") == 0) {
            // TODO(pal): Push back to P too if the buffer P exists.
            buckets.emplace_back(HdAovTokens->depth, AI_TYPE_VECTOR, bucketData);
        } else if (pixelType == AI_TYPE_UINT && strcmp(outputName, "ID") == 0) {
            const auto it = driverData->renderBuffers->find(HdAovTokens->primId);
            if (it != driverData->renderBuffers->end()) {
                ids.resize(pixelCount, -1);
                const auto* in = static_cast<const unsigned int*>(bucketData);
                for (auto i = decltype(pixelCount){0}; i < pixelCount; i += 1) {
                    ids[i] = static_cast<int>(in[i]) - 1;
                }
                it->second->WriteBucket(bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, HdFormatInt32, ids.data());
            }
        } else if (pixelType == AI_TYPE_RGB || pixelType == AI_TYPE_RGBA) {
            // TODO(pal): Push back to RGBA too if the buffer RGBA exists.
            buckets.emplace_back(
                strcmp(outputName, "RGBA") == 0 ? HdAovTokens->color : TfToken{outputName}, pixelType, bucketData);
        }
    }
    for (const auto& bucket : buckets) {
        if (bucket.name == HdAovTokens->depth) {
            const auto it = driverData->renderBuffers->find(bucket.name);
            if (it == driverData->renderBuffers->end()) {
                continue;
            }
            auto& depth = driverData->depths[tid];
            depth.resize(pixelCount, 1.0f);
            const auto* in = static_cast<const GfVec3f*>(bucket.data);
            if (ids.empty()) {
                for (auto i = decltype(pixelCount){0}; i < pixelCount; i += 1) {
                    const auto p = driverData->projMtx.Transform(driverData->viewMtx.Transform(in[i]));
                    depth[i] = std::max(-1.0f, std::min(1.0f, p[2]));
                }
            } else {
                for (auto i = decltype(pixelCount){0}; i < pixelCount; i += 1) {
                    if (ids[i] == -1) {
                        depth[i] = 1.0f;
                    } else {
                        const auto p = driverData->projMtx.Transform(driverData->viewMtx.Transform(in[i]));
                        depth[i] = std::max(-1.0f, std::min(1.0f, p[2]));
                    }
                }
            }
            it->second->WriteBucket(bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, HdFormatFloat32, depth.data());
        } else if (bucket.name == HdAovTokens->color) {
            const auto it = driverData->renderBuffers->find(bucket.name);
            if (it == driverData->renderBuffers->end()) {
                continue;
            }
            if (ids.empty()) {
                it->second->WriteBucket(
                    bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, HdFormatFloat32Vec4, bucketData);
            } else {
                auto& color = driverData->colors[tid];
                color.resize(pixelCount, AI_RGBA_ZERO);
                const auto* in = static_cast<const AtRGBA*>(bucket.data);
                for (auto i = decltype(pixelCount){0}; i < pixelCount; i += 1) {
                    if (ids[i] == -1) {
                        color[i] = AI_RGBA_ZERO;
                    } else {
                        color[i] = in[i];
                    }
                }
                it->second->WriteBucket(
                    bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, HdFormatFloat32Vec4, color.data());
            }
        } else if (bucket.type == AI_TYPE_RGB) {
            const auto it = driverData->renderBuffers->find(bucket.name);
            if (it != driverData->renderBuffers->end()) {
                it->second->WriteBucket(
                    bucket_xo, bucket_yo, bucket_size_x, bucket_size_y, HdFormatFloat32Vec3, bucket.data);
            }
        }
    }
}

driver_write_bucket {}

driver_close {}

PXR_NAMESPACE_CLOSE_SCOPE
