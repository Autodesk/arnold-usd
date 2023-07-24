//
// SPDX-License-Identifier: Apache-2.0
//

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
#pragma once

#include <pxr/pxr.h>
#include <ai.h>

#if PXR_VERSION >= 2002
/// Depth range in Hydra was changed from -1 .. 1 to 0 .. 1.
#define USD_HAS_ZERO_TO_ONE_DEPTH
#endif

#if PXR_VERSION >= 2005
/// Not blitting to a hardware buffer anymore, following the example of HdEmbree.
#define USD_DO_NOT_BLIT
#endif

#if PXR_VERSION >= 2105
#define USD_HAS_SAMPLE_INDEXED_PRIMVAR
#endif

#if PXR_VERSION >= 2302
#define USD_HAS_RENDERER_PLUGIN_GPU_ENABLE_PARAM
#endif

#if ARNOLD_VERSION_NUM > 60201
#define ARNOLD_MULTIPLE_RENDER_SESSIONS
#endif
