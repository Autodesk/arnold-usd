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

#if PXR_VERSION >= 2105
#define USD_HAS_SAMPLE_INDEXED_PRIMVAR
#endif

#if PXR_VERSION >= 2302
#define USD_HAS_RENDERER_PLUGIN_GPU_ENABLE_PARAM
#endif

