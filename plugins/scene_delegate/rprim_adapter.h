// Copyright 2022 Autodesk, Inc.
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
/// @file scene_delegate/rprim_adapter.h
///
/// Base adapter for converting Arnold shapes to Hydra render primitives.
#pragma once
#include "api.h"

#include <pxr/pxr.h>

#include "prim_adapter.h"

PXR_NAMESPACE_OPEN_SCOPE

/// @class ImagingArnoldRprimAdapter
///
/// Intermediate utility class to handle generic rprim related functions.
class ImagingArnoldRprimAdapter : public ImagingArnoldPrimAdapter {
public:
    using BaseAdapter = ImagingArnoldPrimAdapter;
};

PXR_NAMESPACE_CLOSE_SCOPE
