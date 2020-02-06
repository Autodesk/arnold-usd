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
/// @file ndr/utils.h
///
/// Utilities for the NDR Plugin.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Returns a stage containing all the available arnold shaders.
///
/// The function returns a stage holding generic prims, each of them represting
/// an arnold shader. The "filename" metadata specifies the source of the shader
/// either `<built-in>` for built-in shaders or the path pointing to the
/// shader library or the osl file defining the shader.
///
/// The function is either reuses an existing arnold universe, or creates/destroys
/// one as part of the node entry iteration.
///
/// The result is cached, so multiple calls to the function won't result in
/// multiple stage creations.
///
/// @return A UsdStage holding all the available arnold shader definitions.
NDRARNOLD_API
UsdStageRefPtr NdrArnoldGetShaderDefs();

PXR_NAMESPACE_CLOSE_SCOPE
