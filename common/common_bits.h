// Copyright 2021 Autodesk, Inc.
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
/// @file common_bits.h
///
/// Common bits for Hydra and Usd Imaging Adapters.
#include <pxr/pxr.h>

#include <pxr/imaging/hd/changeTracker.h>

PXR_NAMESPACE_OPEN_SCOPE

// For now we reserve a single bit for all the parameter changes.
enum ArnoldUsdRprimBits : HdDirtyBits {
    ArnoldUsdRprimBitsParams = HdChangeTracker::CustomBitsBegin,
};

PXR_NAMESPACE_CLOSE_SCOPE
