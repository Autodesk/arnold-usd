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
#include "../utils.h"
#include "nodes.h"

PXR_NAMESPACE_USING_DIRECTIVE

AI_DRIVER_NODE_EXPORT_METHODS(HdArnoldDriverMtd);

AtString HdArnoldDriver::projMtx("projMtx");
AtString HdArnoldDriver::viewMtx("viewMtx");

namespace {
const char* supportedExtensions[] = {nullptr};

struct DriverData {
    GfMatrix4f projMtx;
    GfMatrix4f viewMtx;
};
} // namespace

tbb::concurrent_queue<HdArnoldBucketData*> bucketQueue;

void hdArnoldEmptyBucketQueue(const std::function<void(const HdArnoldBucketData*)>& f)
{
    HdArnoldBucketData* data = nullptr;
    while (bucketQueue.try_pop(data)) {
        if (data) {
            f(data);
            delete data;
            data = nullptr;
        }
    }
}

node_parameters
{
    AiParameterMtx(HdArnoldDriver::projMtx, AiM4Identity());
    AiParameterMtx(HdArnoldDriver::viewMtx, AiM4Identity());
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
    const auto* driverData = reinterpret_cast<const DriverData*>(AiNodeGetLocalData(node));
    const char* outputName = nullptr;
    int pixelType = AI_TYPE_RGBA;
    const void* bucketData = nullptr;
    auto* data = new HdArnoldBucketData();
    data->xo = bucket_xo;
    data->yo = bucket_yo;
    data->sizeX = bucket_size_x;
    data->sizeY = bucket_size_y;
    const auto bucketSize = bucket_size_x * bucket_size_y;
    while (AiOutputIteratorGetNext(iterator, &outputName, &pixelType, &bucketData)) {
        if (pixelType == AI_TYPE_RGBA && strcmp(outputName, "RGBA") == 0) {
            data->beauty.resize(bucketSize);
            const auto* inRGBA = reinterpret_cast<const AtRGBA*>(bucketData);
            for (auto i = decltype(bucketSize){0}; i < bucketSize; ++i) {
                const auto in = inRGBA[i];
                const auto x = bucket_xo + i % bucket_size_x;
                const auto y = bucket_yo + i / bucket_size_x;
                AtRGBA8 out;
                out.r = AiQuantize8bit(x, y, 0, in.r, true);
                out.g = AiQuantize8bit(x, y, 1, in.g, true);
                out.b = AiQuantize8bit(x, y, 2, in.b, true);
                out.a = AiQuantize8bit(x, y, 3, in.a, true);
                data->beauty[i] = out;
            }

        } else if (pixelType == AI_TYPE_VECTOR && strcmp(outputName, "P") == 0) {
            data->depth.resize(bucketSize, 1.0f);
            const auto* pp = reinterpret_cast<const GfVec3f*>(bucketData);
            auto* pz = data->depth.data();
            for (auto i = decltype(bucketSize){0}; i < bucketSize; ++i) {
                // Rays hitting the background will return a (0,0,0) vector. We don't worry about it, as background
                // pixels will be marked with an ID of 0 by arnold.
                const auto p = driverData->projMtx.Transform(driverData->viewMtx.Transform(pp[i]));
                pz[i] = std::max(-1.0f, std::min(1.0f, p[2]));
            }
        } else if (pixelType == AI_TYPE_UINT && strcmp(outputName, "ID") == 0) {
            data->primId.resize(bucketSize, -1);
            // Technically, we're copying from an unsigned int buffer to a signed int buffer... but the values were
            // originally force-reinterpreted to unsigned on the way in, so we're undoing that on the way out
            memcpy(data->primId.data(), bucketData, bucketSize * sizeof(decltype(data->primId[0])));
        }
    }
    if (data->beauty.empty() || data->depth.empty() || data->primId.empty()) {
        delete data;
    } else {
        for (auto i = decltype(bucketSize){0}; i < bucketSize; ++i) {
            // The easiest way to filter is to check for the zero primID.
            data->primId[i] -= 1;
            if (data->primId[i] == -1) {
                data->depth[i] = 1.0f - AI_EPSILON;
                data->beauty[i].a = 0.0f;
            }
        }
        bucketQueue.push(data);
    }
}

driver_write_bucket {}

driver_close {}
