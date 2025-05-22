//
// SPDX-License-Identifier: Apache-2.0
//

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
// Modifications Copyright 2022 Autodesk, Inc.
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

#include "../utils.h"
#include "nodes.h"

PXR_NAMESPACE_OPEN_SCOPE

AI_DRIVER_NODE_EXPORT_METHODS(HdArnoldDriverMainMtd);

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


namespace {
const char* supportedExtensions[] = {nullptr};

} // namespace

node_parameters
{
    AiParameterMtx(str::projMtx, AiM4Identity());
    AiParameterMtx(str::viewMtx, AiM4Identity());
    AiParameterPtr(str::color_pointer, nullptr);
    AiParameterPtr(str::depth_pointer, nullptr);
    AiParameterPtr(str::id_pointer, nullptr);
    AiParameterArray(str::buffer_names, AiArray(0, 0, AI_TYPE_STRING));
    AiParameterArray(str::buffer_pointers, AiArray(0, 0, AI_TYPE_POINTER));
    AiMetaDataSetBool(nentry, NULL, "parallel_driver_needs_bucket", true);
    AiMetaDataSetBool(nentry, NULL, "parallel_driver_prepare_bucket", true);
    AiMetaDataSetBool(nentry, NULL, "parallel_driver_write_bucket", true); 
}

node_initialize
{
    AiDriverInitialize(node, true);
    AiNodeSetLocalData(node, new DriverMainData());
}

node_update
{
    auto* data = reinterpret_cast<DriverMainData*>(AiNodeGetLocalData(node));
    ConvertValue(data->projMtx, AiNodeGetMatrix(node, str::projMtx));
    ConvertValue(data->viewMtx, AiNodeGetMatrix(node, str::viewMtx));
    data->colorBuffer = static_cast<HdArnoldRenderBuffer*>(AiNodeGetPtr(node, str::color_pointer));
    data->depthBuffer = static_cast<HdArnoldRenderBuffer*>(AiNodeGetPtr(node, str::depth_pointer));
    data->idBuffer = static_cast<HdArnoldRenderBuffer*>(AiNodeGetPtr(node, str::id_pointer));

    // Store the region min X/Y so that we can apply an offset when
    // negative pixel coordinates are needed for overscan.
    AtNode *options = AiUniverseGetOptions(AiNodeGetUniverse(node));
    data->regionMinX = AiNodeGetInt(options, str::region_min_x);
    data->regionMinY = AiNodeGetInt(options, str::region_min_y);

    // Check the default value for "region_min". It should be INT_MI, but I'm testing it for safety.
    const AtParamEntry* pentryMinx = AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(options), str::region_min_x);
    int defaultValue = (pentryMinx) ? AiParamGetDefault(pentryMinx)->INT() : INT_MIN;

    // If the region min is left to default, we don't want to apply any offset
    if (data->regionMinX == defaultValue)
        data->regionMinX = 0;
    if (data->regionMinY == defaultValue)
        data->regionMinY = 0;
    
    // This driver might have an array of names & pointers, that we will use 
    // to create a buffers map. It will return us the appropriate buffer for each AOV name
    AtArray *buffer_names = AiNodeGetArray(node, str::buffer_names);
    AtArray *buffer_pointers = AiNodeGetArray(node, str::buffer_pointers);
    unsigned int buffer_names_count = buffer_names ? AiArrayGetNumElements(buffer_names) : 0;
    unsigned int buffer_pointers_count = buffer_pointers ? AiArrayGetNumElements(buffer_pointers) : 0;
    unsigned int buffer_count = AiMin(buffer_names_count, buffer_pointers_count);
    data->buffers.clear();
    for (unsigned int i = 0; i < buffer_count; ++i) {
        AtString buffer_name = AiArrayGetStr(buffer_names, i);
        HdArnoldRenderBuffer *buffer_pointer = (HdArnoldRenderBuffer*)AiArrayGetPtr(buffer_pointers, i);
        if (buffer_pointer != nullptr && !buffer_name.empty()) {
            data->buffers[buffer_name] = buffer_pointer;
        }
    }
}

node_finish {}

driver_supports_pixel_type
{
    return pixel_type == AI_TYPE_RGBA || pixel_type == AI_TYPE_RGB || pixel_type == AI_TYPE_VECTOR ||
       pixel_type == AI_TYPE_VECTOR2 || pixel_type == AI_TYPE_FLOAT || pixel_type == AI_TYPE_INT;

}

driver_extension { return supportedExtensions; }

driver_open {}

driver_needs_bucket { return true; }

driver_prepare_bucket {}

driver_process_bucket
{
    auto* driverData = reinterpret_cast<DriverMainData*>(AiNodeGetLocalData(node));
    AtString outputName;
    int pixelType = AI_TYPE_RGBA;
    const void* bucketData = nullptr;
    const auto pixelCount = bucket_size_x * bucket_size_y;
    // We should almost always have depth and id.
    auto& ids = driverData->ids[tid];
    ids.clear();
    const void* colorData = nullptr;
    const void* positionData = nullptr;
    // Apply an offset to the pixel coordinates based on the region_min,
    // since we don't own the render buffer, which just knows the output resolution
    const auto bucket_xo_start = bucket_xo - driverData->regionMinX;
    const auto bucket_yo_start = bucket_yo - driverData->regionMinY;

    auto checkOutputName = [&outputName](const AtString& name) -> bool {
        return outputName == name;
    };
    while (AiOutputIteratorGetNext(iterator, &outputName, &pixelType, &bucketData)) {

        auto it = driverData->buffers.find(outputName);
        if (it != driverData->buffers.end()) {
            it->second->WriteBucket(
            bucket_xo - driverData->regionMinX, bucket_yo - driverData->regionMinY, bucket_size_x, bucket_size_y, _GetFormatFromArnoldType(pixelType), bucketData);
        } else if (pixelType == AI_TYPE_VECTOR && checkOutputName(str::P)) {
            positionData = bucketData;
        } else if (pixelType == AI_TYPE_INT && checkOutputName(str::hydraPrimId)) {
            if (driverData->idBuffer) {
                ids.resize(pixelCount, -1);
                const auto* in = static_cast<const int*>(bucketData);
                std::transform(in, in + pixelCount, ids.begin(), [](int id) -> int { return id < 0 ? -1 : id - 1; });
                driverData->idBuffer->WriteBucket(
                    bucket_xo_start, bucket_yo_start, bucket_size_x, bucket_size_y, HdFormatInt32, ids.data());
            }
        } else if (pixelType == AI_TYPE_RGBA && checkOutputName(str::RGBA)) {
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
                depth[i] = (std::max(-1.0f, std::min(1.0f, p[2])) + 1.0f) / 2.0f;
            }
        } else {
            for (auto i = decltype(pixelCount){0}; i < pixelCount; i += 1) {
                if (ids[i] == -1) {
                    depth[i] = 1.0f;
                } else {
                    const auto p = driverData->projMtx.Transform(driverData->viewMtx.Transform(in[i]));
                    depth[i] = (std::max(-1.0f, std::min(1.0f, p[2])) + 1.0f) / 2.0f;
                }
            }
        }

        driverData->depthBuffer->WriteBucket(
            bucket_xo_start, bucket_yo_start, bucket_size_x, bucket_size_y, HdFormatFloat32, depth.data());
    }
    if (colorData != nullptr && driverData->colorBuffer) {
        if (ids.empty()) {
            driverData->colorBuffer->WriteBucket(
                bucket_xo_start, bucket_yo_start, bucket_size_x, bucket_size_y, HdFormatFloat32Vec4, colorData);
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
                bucket_xo_start, bucket_yo_start, bucket_size_x, bucket_size_y, HdFormatFloat32Vec4, color.data());
        }
    }
}

driver_write_bucket {}

driver_close {}

PXR_NAMESPACE_CLOSE_SCOPE
