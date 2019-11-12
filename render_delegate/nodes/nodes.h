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
/// @file nodes.h
///
/// Interfaces for Arnold nodes used by the Render Delegate.
#ifndef HDARNOLD_NODES_H
#define HDARNOLD_NODES_H

#include <ai.h>

#include <functional>
#include <vector>

namespace HdArnoldNodeNames {
extern AtString driver;
} // namespace HdArnoldNodeNames

namespace HdArnoldDriver {
extern AtString projMtx;
extern AtString viewMtx;
} // namespace HdArnoldDriver

/// Installs Arnold nodes that are used by the Render Delegate.
void hdArnoldInstallNodes();

/// Uninstalls Arnold nodes that are used by the Render Delegate.
void hdArnoldUninstallNodes();

/// Simple structure holding a 4 component, 8 bit per component color.
struct AtRGBA8 {
    uint8_t r = 0; ///< Red component of the color.
    uint8_t g = 0; ///< Green component of the color.
    uint8_t b = 0; ///< Blue component of the color.
    uint8_t a = 0; ///< Alpha component of the color.
};

/// Structure holding rendered bucket data.
///
/// HdArnoldBucketData holds the screen space coordinates of the bucket and
/// 8 bit beauty alongside a single precision floating point depth.
struct HdArnoldBucketData {
    HdArnoldBucketData() = default;
    ~HdArnoldBucketData() = default;
    HdArnoldBucketData(const HdArnoldBucketData&) = delete;
    HdArnoldBucketData(HdArnoldBucketData&&) = delete;
    HdArnoldBucketData& operator=(const HdArnoldBucketData&) = delete;

    int xo = 0;    ///< X pixel coordinate origin of the bucket.
    int yo = 0;    ///< Y pixel coorindate origin of the bucket.
    int sizeX = 0; ///< Width  of the bucket in pixels.
    int sizeY = 0; ///< Height of the bucket in pixels.

    /// These values are dithered and quatized from the beauty AOV using
    /// AiQuantize8bit.
    std::vector<AtRGBA8> beauty; ///< BeautyV of the rendered image.

    /// These values are computed from the P AOV and the projection matrix
    /// provided by Hydra.
    std::vector<float> depth; ///< Depth of the rendered image.

    /// These values are computed from the ID AOV (and we set the id
    /// attribute to the primId returned by Hydra).
    std::vector<int32_t> primId;
};

/// Empties the bucket queue held by the driver.
///
/// @param f Function object thar receives each bucket stored by the queue.
void hdArnoldEmptyBucketQueue(const std::function<void(const HdArnoldBucketData*)>& f);

#endif
