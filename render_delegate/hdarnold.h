// Copyright 2019 Autodesk, Inc.
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

#include "../arnold_usd.h"

/// From 19.05.
#if USED_USD_VERSION_GREATER_EQ(19, 5)
/// Hydra has the new GetInstancerTransform API.
#define USD_HAS_NEW_INSTANCER_TRANSFORM
#endif

#if USED_USD_VERSION_GREATER_EQ(19, 10)
/// Hydra has the new material terminal tokens.
#define USD_HAS_NEW_MATERIAL_TERMINAL_TOKENS
/// Hydra has the new renderer plugin base class
#define USD_HAS_NEW_RENDERER_PLUGIN
#endif

#if USED_USD_VERSION_GREATER_EQ(19, 11)
/// Hydra has the new renderer plugin base class
#define USD_HAS_UPDATED_TIME_SAMPLE_ARRAY
/// Hydra has the updated render buffer class.
#define USD_HAS_UPDATED_RENDER_BUFFER
#endif

#if AI_VERSION_NUMBER >= 60003
/// Using the core instancer procedural instead of inline instances.
#define HDARNOLD_USE_INSTANCER
#endif
