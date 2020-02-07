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
/// @file render_delegate/utils.h
///
/// General utilities for Hydra <> Arnold interop.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>

#include <pxr/base/vt/value.h>

#include <pxr/imaging/hd/sceneDelegate.h>

#include <ai.h>

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// Converts a double precision GfMatrix to AtMatrix.
///
/// @param in Double Precision GfMatrix.
/// @return AtMatrix converted from the GfMatrix.
HDARNOLD_API
AtMatrix HdArnoldConvertMatrix(const GfMatrix4d& in);
/// Converts a single precision GfMatrix to AtMatrix.
///
/// @param in Single Precision GfMatrix.
/// @return AtMatrix converted from the GfMatrix.
HDARNOLD_API
AtMatrix HdArnoldConvertMatrix(const GfMatrix4f& in);
/// Converts an AtMatrix to a single precision GfMatrix.
///
/// @param in AtMatrix.
/// @return GfMatrix converted from the AtMatrix.
HDARNOLD_API
GfMatrix4f HdArnoldConvertMatrix(const AtMatrix& in);
/// Sets the transform on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to the Arnold Node.
/// @param delegate Pointer to the Scene Delegate.
/// @param id Path to the primitive.
HDARNOLD_API
void HdArnoldSetTransform(AtNode* node, HdSceneDelegate* delegate, const SdfPath& id);
/// Sets the transform on multiple Arnold nodes from a single Hydra Primitive.
///
/// @param node Vector holding all the Arnold Nodes.
/// @param delegate Pointer to the Scene Delegate.
/// @param id Path to the primitive.
HDARNOLD_API
void HdArnoldSetTransform(const std::vector<AtNode*>& nodes, HdSceneDelegate* delegate, const SdfPath& id);
/// Sets a Parameter on an Arnold Node from a VtValue.
///
/// @param node Pointer to the Arnold Node.
/// @param pentry Pointer to a Constant AtParamEntry struct for the Parameter.
/// @param value VtValue of the Value to be set.
HDARNOLD_API
void HdArnoldSetParameter(AtNode* node, const AtParamEntry* pentry, const VtValue& value);
/// Converts constant scope primvars to built-in parameters.
///
/// @param node Pointer to an Arnold node.
/// @param id Path to the Primitive.
/// @param delegate Pointer to the Scene Delegate.
/// @param primvarDesc Primvar Descriptor for the Primvar to be set.
HDARNOLD_API
bool ConvertPrimvarToBuiltinParameter(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc);
/// Sets a Constant scope Primvar on an Arnold node from a Hydra Primitive.
///
/// There is some additional type remapping done to deal with various third
/// party apps:
/// bool -> bool / int / long
/// int -> int / long
/// float -> float / double
///
/// The function also calls ConvertPrimvarToBuiltinParameter.
///
/// @param node Pointer to an Arnold Node.
/// @param id Path to the Primitive.
/// @param delegate Pointer to the Scene Delegate.
/// @param primvarDesch Primvar Descriptor for the Primvar to be set.
HDARNOLD_API
void HdArnoldSetConstantPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc);
/// Sets a Uniform scope Primvar on an Arnold node from a Hydra Primitive.
///
/// If the parameter is named arnold:xyz (so it exist in the form of
/// primvars:arnold:xyz), the function checks if a normal parameter exist with
/// the same name as removing the arnold: prefix.
///
/// @param node Pointer to an Arnold Node.
/// @param id Path to the Primitive.
/// @param delegate Pointer to the Scene Delegate.
/// @param primvarDesch Primvar Descriptor for the Primvar to be set.
HDARNOLD_API
void HdArnoldSetUniformPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc);
/// Sets a Vertex scope Primvar on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to an Arnold Node.
/// @param id Path to the Primitive.
/// @param delegate Pointer to the Scene Delegate.
/// @param primvarDesch Primvar Descriptor for the Primvar to be set.
HDARNOLD_API
void HdArnoldSetVertexPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc);
/// Sets a Face-Varying scope Primvar on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to an Arnold Node.
/// @param id Path to the Primitive.
/// @param delegate Pointer to the Scene Delegate.
/// @param primvarDesch Primvar Descriptor for the Primvar to be set.
HDARNOLD_API
void HdArnoldSetFaceVaryingPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc);
/// Sets positions attribute on an Arnold shape from a VtVec3fArray primvar.
///
/// @param node Pointer to an Arnold node.
/// @param paramName Name of the positions parameter on the Arnold node.
/// @param id Path to the Hydra Primitive.
/// @param delegate Pointer to the Scene Delegate.
HDARNOLD_API
void HdArnoldSetPositionFromPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const AtString& paramName);
/// Sets radius attribute on an Arnold shape from a float primvar.
///
/// This function looks for a widths primvar, which will be multiplied by 0.5
/// before set on the node.
///
/// @param node Pointer to an Arnold node.
/// @param paramName Name of the positions parameter on the Arnold node.
/// @param id Path to the Hydra Primitive.
/// @param delegate Pointer to the Scene Delegate.
HDARNOLD_API
void HdArnoldSetRadiusFromPrimvar(AtNode* node, const SdfPath& id, HdSceneDelegate* delegate);

PXR_NAMESPACE_CLOSE_SCOPE
