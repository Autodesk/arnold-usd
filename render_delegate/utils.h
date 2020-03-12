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
#include <pxr/imaging/hd/timeSampleArray.h>

#include <ai.h>

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

constexpr int HD_ARNOLD_MAX_PRIMVAR_SAMPLES = 2;
using HdArnoldSampledPrimvarType = HdTimeSampleArray<VtValue, HD_ARNOLD_MAX_PRIMVAR_SAMPLES>;

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
/// If @param visibility is not a nullptr, the visibility calculation will store the value in the pointed uint8_t
/// instead of setting it on the node.
///
/// @param node Pointer to an Arnold node.
/// @param delegate Pointer to the Scene Delegate.
/// @param name Name of the primvar.
/// @param value Value of the primvar.
/// @param visibility Pointer to the output visibility parameter.
/// @return Returns true if the conversion was successful.
HDARNOLD_API
bool ConvertPrimvarToBuiltinParameter(
    AtNode* node, const TfToken& name, const VtValue& value, uint8_t* visibility = nullptr);
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
/// @param name Name of the primvar.
/// @param role Role of the primvar.
/// @param value Value of the primvar.
/// @param visibility Pointer to the output visibility parameter.
HDARNOLD_API
void HdArnoldSetConstantPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, uint8_t* visibility = nullptr);
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
/// @param primvarDesc Description of the primvar.
/// @param visibility Pointer to the output visibility parameter.
HDARNOLD_API
void HdArnoldSetConstantPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc,
    uint8_t* visibility = nullptr);
/// Sets a Uniform scope Primvar on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to an Arnold Node.
/// @param name Name of the primvar.
/// @param role Role of the primvar.
/// @param value Value of the primvar.
HDARNOLD_API
void HdArnoldSetUniformPrimvar(AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value);
/// Sets a Uniform scope Primvar on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to an Arnold Node.
/// @param id Path to the Primitive.
/// @param delegate Pointer to the Scene Delegate.
/// @param primvarDesc Primvar Descriptor for the Primvar to be set.
HDARNOLD_API
void HdArnoldSetUniformPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc);
/// Sets a Vertex scope Primvar on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to an Arnold Node.
/// @param name Name of the primvar.
/// @param role Role of the primvar.
/// @param value Value of the primvar.
HDARNOLD_API
void HdArnoldSetVertexPrimvar(AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value);
/// Sets a Vertex scope Primvar on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to an Arnold Node.
/// @param id Path to the Primitive.
/// @param delegate Pointer to the Scene Delegate.
/// @param primvarDesc Primvar Descriptor for the Primvar to be set.
HDARNOLD_API
void HdArnoldSetVertexPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc);
/// Sets a Face-Varying scope Primvar on an Arnold node from a Hydra Primitive. If @p vertexCounts is not a nullptr
/// and it is not empty, it is used to reverse the order of the generated face vertex indices, to support
/// left handed topologies. The total sum of the @p vertexCounts array is expected to be the same as the number values
/// stored in the primvar.
///
/// @param node Pointer to an Arnold Node.
/// @param name Name of the primvar.
/// @param role Role of the primvar.
/// @param value Value of the primvar.
/// @param vertexCounts Pointer to the VtIntArray holding the face vertex counts for the mesh.
HDARNOLD_API
void HdArnoldSetFaceVaryingPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value,
    const VtIntArray* vertexCounts = nullptr);
/// Sets a Face-Varying scope Primvar on an Arnold node from a Hydra Primitive. If @p vertexCounts is not a nullptr
/// and it is not empty, it is used to reverse the order of the generated face vertex indices, to support
/// left handed topologies. The total sum of the @p vertexCounts array is expected to be the same as the number values
/// stored in the primvar.
///
/// @param node Pointer to an Arnold Node.
/// @param id Path to the Primitive.
/// @param delegate Pointer to the Scene Delegate.
/// @param primvarDesc Primvar Descriptor for the Primvar to be set.
/// @param vertexCounts Pointer to the VtIntArray holding the face vertex counts for the mesh.
HDARNOLD_API
void HdArnoldSetFaceVaryingPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc,
    const VtIntArray* vertexCounts = nullptr);
/// Sets positions attribute on an Arnold shape from a VtVec3fArray primvar.
///
/// @param node Pointer to an Arnold node.
/// @param paramName Name of the positions parameter on the Arnold node.
/// @param id Path to the Hydra Primitive.
/// @param delegate Pointer to the Scene Delegate.
/// @return Number of keys for the position.
HDARNOLD_API
size_t HdArnoldSetPositionFromPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const AtString& paramName);
/// Sets positions attribute on an Arnold shape from a VtValue holding VtVec3fArray.
///
/// @param node Pointer to an Arnold node.
/// @param paramName Name of the positions parameter on the Arnold node.
/// @param value Value holding a VtVec3fArray.
void HdArnoldSetPositionFromValue(AtNode* node, const AtString& paramName, const VtValue& value);
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
/// Generates the idxs array for flattened USD values. When @p vertexCounts is not nullptr and not empty, the
/// the indices are reversed per polygon. The sum of the values stored in @p vertexCounts is expected to match
/// @p numIdxs.
///
/// @param numIdxs Number of face vertex indices to generate.
/// @param vertexCounts VtArrayInt pointer to the face vertex counts of the mesh or nullptr.
/// @return An AtArray with the generated indices of @param numIdxs length.
HDARNOLD_API
AtArray* HdArnoldGenerateIdxs(unsigned int numIdxs, const VtIntArray* vertexCounts = nullptr);

/// Struct storing the cached primvars.
struct HdArnoldPrimvar {
    VtValue value;                 ///< Copy-On-Write Value of the primvar.
    TfToken role;                  ///< Role of the primvar.
    HdInterpolation interpolation; ///< Type of interpolation used for the value.
    bool dirtied;                  ///< If the primvar has been dirtied.;

    ///< Constructor for creating the primvar description.
    ///
    /// @param _value Value to be stored for the primvar.
    /// @param _interpolation Interpolation type for the primvar.
    HdArnoldPrimvar(const VtValue& _value, const TfToken& _role, HdInterpolation _interpolation)
        : value(_value), role(_role), interpolation(_interpolation), dirtied(true)
    {
    }
};

/// Storing precomputed primvars.
using HdArnoldPrimvarMap = std::unordered_map<TfToken, HdArnoldPrimvar, TfToken::HashFunctor>;

/// Get the computed primvars using HdExtComputation.
///
/// @param delegate Pointer to the Hydra Scene Delegate.
/// @param id Path to the Hyra Primitive.
/// @param dirtyBits Dirty bits of what has changed for the current sync.
/// @param primvars Output variable to store the computed primvars.
/// @return Returns true if anything computed False otherwise.
bool HdArnoldGetComputedPrimvars(
    HdSceneDelegate* delegate, const SdfPath& id, HdDirtyBits dirtyBits, HdArnoldPrimvarMap& primvars);

/// Get the non-computed primvars and ignoring the points primvar. If multiple position keys are used, the function
/// does not query the value of the normals.
///
/// @param delegate Pointer to the Hydra Scene Delegate.
/// @param id Path to the Hyra Primitive.
/// @param dirtyBits Dirty bits of what has changed for the current sync.
/// @param multiplePositionKeys If the points primvar has multiple position keys.
/// @param primvars Output variable to store the primvars.
void HdArnoldGetPrimvars(
    HdSceneDelegate* delegate, const SdfPath& id, HdDirtyBits dirtyBits, bool multiplePositionKeys,
    HdArnoldPrimvarMap& primvars);

PXR_NAMESPACE_CLOSE_SCOPE
