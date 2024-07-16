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
/// @file render_delegate/utils.h
///
/// General utilities for Hydra <> Arnold interop.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>

#include <pxr/base/vt/value.h>

#include <pxr/usd/sdf/path.h>

#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/timeSampleArray.h>

#include <ai.h>

#include <vector>
#include <constant_strings.h>
#include "render_param.h"
#include <parameters_utils.h>

PXR_NAMESPACE_OPEN_SCOPE


/// Utility class to handle ray flags for shapes.
class HdArnoldRayFlags {
public:
    /// Default constructor.
    HdArnoldRayFlags() = default;

    /// Constructor to set up the hydra flag.
    ///
    /// @param hydraFlag Value of the Hydra flag.
    HdArnoldRayFlags(uint8_t hydraFlag) : _hydraFlag(hydraFlag) {}

    /// Compose the ray flags to set on an Arnold shape. Bitflags set from primvars will override flags from Hydra.
    ///
    /// @return Value of the composed ray flags.
    uint8_t Compose() const { return (_hydraFlag & ~_primvarFlagState) | (_primvarFlags & _primvarFlagState); }

    /// Sets the flags coming from Hydra.
    ///
    /// @param flag Value of the flags from Hydra.
    void SetHydraFlag(uint8_t flag) { _hydraFlag = flag; }

    /// Set the flag coming from primvars.
    ///
    /// @param flag
    /// @param state
    void SetPrimvarFlag(uint8_t flag, bool state)
    {
        _primvarFlags = state ? _primvarFlags | flag : _primvarFlags & ~flag;
        _primvarFlagState |= flag;
    }

    /// Clears the primvar flags and resets to their default state.
    void ClearPrimvarFlags()
    {
        _primvarFlags = AI_RAY_ALL;
        _primvarFlagState = 0;
    }

    void SetRayFlag(const char* rayName, const VtValue& value)
    {
        auto flag = true;
        if (value.IsHolding<bool>()) {
            flag = value.UncheckedGet<bool>();
        } else if (value.IsHolding<int>()) {
            flag = value.UncheckedGet<int>() != 0;
        } else if (value.IsHolding<long>()) {
            flag = value.UncheckedGet<long>() != 0;
        } else {
            // Invalid value stored, exit.
            return;
        }
        auto charStartsWithToken = [&](const char *c, const TfToken& t) { return strncmp(c, t.GetText(), t.size()) == 0; };

        uint8_t bitFlag = 0;
        if (charStartsWithToken(rayName, str::t_camera)) {
            bitFlag = AI_RAY_CAMERA;
        } else if (charStartsWithToken(rayName, str::t_shadow)) {
            bitFlag = AI_RAY_SHADOW;
        } else if (charStartsWithToken(rayName, str::t_diffuse_transmit)) {
            bitFlag = AI_RAY_DIFFUSE_TRANSMIT;
        } else if (charStartsWithToken(rayName, str::t_specular_transmit)) {
            bitFlag = AI_RAY_SPECULAR_TRANSMIT;
        } else if (charStartsWithToken(rayName, str::t_volume)) {
            bitFlag = AI_RAY_VOLUME;
        } else if (charStartsWithToken(rayName, str::t_diffuse_reflect)) {
            bitFlag = AI_RAY_DIFFUSE_REFLECT;
        } else if (charStartsWithToken(rayName, str::t_specular_reflect)) {
            bitFlag = AI_RAY_SPECULAR_REFLECT;
        } else if (charStartsWithToken(rayName, str::t_subsurface)) {
            bitFlag = AI_RAY_SUBSURFACE;
        } else {
            // Invalid flag name, exit.
            return;
        }
        SetPrimvarFlag(bitFlag, flag);
    }

private:
    uint8_t _hydraFlag = 0;             ///< Ray flags coming from Hydra.
    uint8_t _primvarFlags = AI_RAY_ALL; ///< Ray flags coming from primvars.
    uint8_t _primvarFlagState = 0;      ///< State of each flag coming from primvars.
};

constexpr unsigned int HD_ARNOLD_MAX_PRIMVAR_SAMPLES = 3;
template <typename T>
using HdArnoldSampledType = HdTimeSampleArray<T, HD_ARNOLD_MAX_PRIMVAR_SAMPLES>;
using HdArnoldSampledPrimvarType = HdArnoldSampledType<VtValue>;
using HdArnoldSampledMatrixType = HdArnoldSampledType<GfMatrix4d>;
using HdArnoldSampledMatrixArrayType = HdArnoldSampledType<VtMatrix4dArray>;
template <typename T>
using HdArnoldIndexedSampledType = HdIndexedTimeSampleArray<T, HD_ARNOLD_MAX_PRIMVAR_SAMPLES>;
using HdArnoldIndexedSampledPrimvarType = HdArnoldIndexedSampledType<VtValue>;

/// Struct storing the cached primvars.
struct HdArnoldPrimvar {
    VtValue value; ///< Copy-On-Write Value of the primvar.
    VtIntArray valueIndices; ///< Copy-On-Write face-varyiong indices of the primvar.
    TfToken role;                  ///< Role of the primvar.
    HdInterpolation interpolation; ///< Type of interpolation used for the value.
    bool dirtied;                  ///< If the primvar has been dirtied.;

    ///< Constructor for creating the primvar description.
    ///
    /// @param _value Value to be stored for the primvar.
    /// @param _interpolation Interpolation type for the primvar.
    HdArnoldPrimvar(
        const VtValue& _value,
        const VtIntArray& _valueIndices,
        const TfToken& _role, HdInterpolation _interpolation)
        : value(_value),
          valueIndices(_valueIndices),
          role(_role),
          interpolation(_interpolation),
          dirtied(true)
    {
    }

    bool NeedsUpdate()
    {
        if (dirtied) {
            dirtied = false;
            return true;
        }
        return false;
    }
};

/// Hash map for storing precomputed primvars.
using HdArnoldPrimvarMap = std::unordered_map<TfToken, HdArnoldPrimvar, TfToken::HashFunctor>;

/// Unboxing sampled type with type checking and no error codes thrown. Count on @param out will be equal to the number
/// of samples that could be converted. Sample conversion exits as soon as a single sample doesn't hold the correct
/// type.
///
/// Alternative to HdTimeSampleArray::UnboxFrom which uses the error-throwing VtValue::Get function.
///
/// @param in Input value holding the boxed samples.
/// @param out Output value with the specified type.
template <typename T>
void HdArnoldUnboxSample(const HdArnoldSampledType<VtValue>& in, HdArnoldSampledType<T>& out)
{
    const auto count = std::min(std::min(static_cast<uint32_t>(in.count), in.values.size()), in.times.size());
    out.Resize(count);
    out.count = 0;
    for (auto i = decltype(count){0}; i < count; i += 1, out.count += 1) {
        if (!in.values[i].IsHolding<T>()) {
            break;
        }
        out.values[i] = in.values[i].UncheckedGet<T>();
        out.times[i] = in.times[i];
    }
}

using HdArnoldSubsets = std::vector<SdfPath>;

/// Sets the transform on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to the Arnold Node.
/// @param sceneDelegate Pointer to the Scene Delegate.
/// @param id Path to the primitive.
HDARNOLD_API
void HdArnoldSetTransform(AtNode* node, HdSceneDelegate* sceneDelegate, const SdfPath& id);
/// Sets the transform on multiple Arnold nodes from a single Hydra Primitive.
///
/// @param node Vector holding all the Arnold Nodes.
/// @param sceneDelegate Pointer to the Scene Delegate.
/// @param id Path to the primitive.
HDARNOLD_API
void HdArnoldSetTransform(const std::vector<AtNode*>& nodes, HdSceneDelegate* sceneDelegate, const SdfPath& id);
/// Sets a Parameter on an Arnold Node from a VtValue.
///
/// @param node Pointer to the Arnold Node.
/// @param pentry Pointer to a Constant AtParamEntry struct for the Parameter.
/// @param value VtValue of the Value to be set.
/// @param renderDelegate Pointer to the current render delegate
HDARNOLD_API
void HdArnoldSetParameter(AtNode* node, const AtParamEntry* pentry, 
    const VtValue& value, HdArnoldRenderDelegate *renderDelegate);
/// Converts constant scope primvars to built-in parameters. When the attribute holds an array, the first element will
/// be used.
///
/// If @param visibility is not a nullptr, the visibility calculation will store the value in the pointed uint8_t
/// instead of setting it on the node.
///
/// @param node Pointer to an Arnold node.
/// @param delegate Pointer to the Scene Delegate.
/// @param name Name of the primvar.
/// @param value Value of the primvar.
/// @param visibility Pointer to the output visibility parameter.
/// @param sidedness Pointer to the output sidedness parameter.
/// @param autobumpVisibility Pointer to the output autobump_visibility parameter.
/// @param renderDelegate Pointer to the current Render Delegate.
/// @return Returns true if the conversion was successful.
HDARNOLD_API
bool ConvertPrimvarToBuiltinParameter(
    AtNode* node, const TfToken& name, const VtValue& value, HdArnoldRayFlags* visibility, HdArnoldRayFlags* sidedness,
    HdArnoldRayFlags* autobumpVisibility, HdArnoldRenderDelegate *renderDelegate);

/// Converts constant scope primvars to Arnold ray flags. They can be used for parameters "visibility", "sidedness" 
/// and "autobump_visibility". The attributes can come as separate components for ray types, e.g.
/// "arnold:visibility:camera = 0". We need to concatenate each of these statements into a single HdArnoldRayFlags,
/// before we finally compose them into a single AtByte arnold parameter
/// @param node Pointer to an Arnold node.
/// @param name Name of the primvar
/// @param value Value of the primvar.
/// @param visibility Pointer to the output visibility parameter.
/// @param sidedness Pointer to the output sidedness parameter.
/// @param autobumpVisibility Pointer to the output autobump_visibility parameter.
/// @return Returns true if the primvar was actually for a ray flag, false otherwise
HDARNOLD_API
bool ConvertPrimvarToRayFlag(AtNode* node, const TfToken& name, const VtValue& value, HdArnoldRayFlags* visibility, HdArnoldRayFlags* sidedness,
    HdArnoldRayFlags* autobumpVisibility);

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
/// @param sidedness Pointer to the output sidedness parameter.
/// @param autobumpVisibility Pointer to the output autobump_visibility parameter.
/// @param renderDelegate Pointer to the current Render Delegate.
HDARNOLD_API
void HdArnoldSetConstantPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, HdArnoldRayFlags* visibility,
    HdArnoldRayFlags* sidedness, HdArnoldRayFlags* autobumpVisibility, 
    HdArnoldRenderDelegate *renderDelegate);
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
/// @param sceneDelegate Pointer to the Scene Delegate.
/// @param primvarDesc Description of the primvar.
/// @param visibility Pointer to the output visibility parameter.
/// @param sidedness Pointer to the output sidedness parameter.
/// @param autobumpVisibility Pointer to the output autobump_visibility parameter.
/// @param renderDelegate Pointer to the current render delegate.
HDARNOLD_API
void HdArnoldSetConstantPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const HdPrimvarDescriptor& primvarDesc,
    HdArnoldRayFlags* visibility, HdArnoldRayFlags* sidedness, HdArnoldRayFlags* autobumpVisibility,
    HdArnoldRenderDelegate *renderDelegate);
/// Sets a Uniform scope Primvar on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to an Arnold Node.
/// @param name Name of the primvar.
/// @param role Role of the primvar.
/// @param value Value of the primvar.
HDARNOLD_API
void HdArnoldSetUniformPrimvar(AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, 
    HdArnoldRenderDelegate *renderDelegate);
/// Sets a Uniform scope Primvar on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to an Arnold Node.
/// @param id Path to the Primitive.
/// @param delegate Pointer to the Scene Delegate.
/// @param primvarDesc Primvar Descriptor for the Primvar to be set.
HDARNOLD_API
void HdArnoldSetUniformPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc, HdArnoldRenderDelegate *renderDelegate);
/// Sets a Vertex scope Primvar on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to an Arnold Node.
/// @param name Name of the primvar.
/// @param role Role of the primvar.
/// @param value Value of the primvar.
HDARNOLD_API
void HdArnoldSetVertexPrimvar(AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, 
    HdArnoldRenderDelegate *renderDelegate);
/// Sets a Vertex scope Primvar on an Arnold node from a Hydra Primitive.
///
/// @param node Pointer to an Arnold Node.
/// @param id Path to the Primitive.
/// @param sceneDelegate Pointer to the Scene Delegate.
/// @param primvarDesc Primvar Descriptor for the Primvar to be set.
HDARNOLD_API
void HdArnoldSetVertexPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const HdPrimvarDescriptor& primvarDesc, HdArnoldRenderDelegate *renderDelegate);
/// Sets a Face-Varying scope Primvar on an Arnold node from a Hydra Primitive. If @p vertexCounts is not a nullptr
/// and it is not empty, it is used to reverse the order of the generated face vertex indices, to support
/// left handed topologies.
///
/// @param node Pointer to an Arnold Node.
/// @param name Name of the primvar.
/// @param role Role of the primvar.
/// @param value Value of the primvar.
/// @param valueIndices Face-varying indices for the primvar.
/// @param vertexCounts Optional pointer to the VtIntArray holding the face vertex counts for the mesh.
/// @param vertexCountSum Optional size_t with sum of the vertexCounts.
HDARNOLD_API
void HdArnoldSetFaceVaryingPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, HdArnoldRenderDelegate *renderDelegate,
    const VtIntArray& valueIndices,
    const VtIntArray* vertexCounts = nullptr, const size_t* vertexCountSum = nullptr);
/// Sets instance primvars on an instancer node.
///
/// @param node Pointer to the Arnold instancer node.
/// @param name Name of the variable to be set.
/// @param role Role of the primvar.
/// @param indices Indices telling us which values we need from the array.
/// @param value Value holding a VtArray<T>.
/// @param parentInstanceCount Number of parent instances (for nested instancers)
/// @param childInstanceCount Number of child instances (for nested instancers)
HDARNOLD_API
void HdArnoldSetInstancePrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtIntArray& indices, 
    const VtValue& value, HdArnoldRenderDelegate* renderDelegate);
/// Sets positions attribute on an Arnold shape from a VtVec3fArray primvar.
///
/// If velocities or accelerations are non-zero, the shutter range is non-instantaneous and the scene delegate only
/// returns a single primvar sample, velocities and accelerations are used to extrapolate positions.
///
/// @param node Pointer to an Arnold node.
/// @param paramName Name of the positions parameter on the Arnold node.
/// @param id Path to the Hydra Primitive.
/// @param sceneDelegate Pointer to the Scene Delegate.
/// @param param Constant pointer to the Arnold Render param struct.
/// @param deformKeys Number of geometry time samples to extrapolate when using acceleration.
/// @param primvars Optional constant pointer to all the available primvars on a primitive.
/// @return Number of keys for the position.
HDARNOLD_API
size_t HdArnoldSetPositionFromPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const AtString& paramName,
    const HdArnoldRenderParam* param, int deformKeys = HD_ARNOLD_MAX_PRIMVAR_SAMPLES,
    const HdArnoldPrimvarMap* primvars = nullptr,  HdArnoldSampledPrimvarType *pointsSample = nullptr);
/// Sets positions attribute on an Arnold shape from a VtValue holding VtVec3fArray.
///
/// @param node Pointer to an Arnold node.
/// @param paramName Name of the positions parameter on the Arnold node.
/// @param value Value holding a VtVec3fArray.
HDARNOLD_API
void HdArnoldSetPositionFromValue(AtNode* node, const AtString& paramName, const VtValue& value);
/// Sets radius attribute on an Arnold shape from a float primvar.
///
/// This function looks for a widths primvar, which will be multiplied by 0.5
/// before set on the node.
///
/// @param node Pointer to an Arnold node.
/// @param paramName Name of the positions parameter on the Arnold node.
/// @param id Path to the Hydra Primitive.
/// @param sceneDelegate Pointer to the Scene Delegate.
HDARNOLD_API
void HdArnoldSetRadiusFromPrimvar(AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate);

/// Insert a primvar into a primvar map. Add a new entry if the primvar is not part of the map, otherwise update
/// the existing entry.
///
/// @param primvars Output reference to the primvar map.
/// @param name Name of the primvar.
/// @param role Role of the primvar.
/// @param interpolation Interpolation of the primvar.
/// @param value Value of the primvar.
/// @param valueIndices Face-varying indices of the primvar.
HDARNOLD_API
void HdArnoldInsertPrimvar(
    HdArnoldPrimvarMap& primvars, const TfToken& name, const TfToken& role, HdInterpolation interpolation,
    const VtValue& value,
    const VtIntArray& valueIndices
);
/// Get the computed primvars using HdExtComputation.
///
/// @param delegate Pointer to the Hydra Scene Delegate.
/// @param id Path to the Hyra Primitive.
/// @param dirtyBits Dirty bits of what has changed for the current sync.
/// @param primvars Output variable to store the computed primvars.
/// @return Returns true if anything computed False otherwise.
/// @param interpolations Optional variable to specify which interpolations to query.
HDARNOLD_API
bool HdArnoldGetComputedPrimvars(
    HdSceneDelegate* delegate, const SdfPath& id, HdDirtyBits dirtyBits, HdArnoldPrimvarMap& primvars,
    const std::vector<HdInterpolation>* interpolations = nullptr, HdArnoldSampledPrimvarType *pointsSample = nullptr);

/// Get the non-computed primvars and ignoring the points primvar. If multiple position keys are used, the function
/// does not query the value of the normals.
///
/// @param delegate Pointer to the Hydra Scene Delegate.
/// @param id Path to the Hyra Primitive.
/// @param dirtyBits Dirty bits of what has changed for the current sync.
/// @param multiplePositionKeys If the points primvar has multiple position keys.
/// @param primvars Output variable to store the primvars.
/// @param interpolations Optional variable to specify which interpolations to query.
HDARNOLD_API
void HdArnoldGetPrimvars(
    HdSceneDelegate* delegate, const SdfPath& id, HdDirtyBits dirtyBits, bool multiplePositionKeys,
    HdArnoldPrimvarMap& primvars, const std::vector<HdInterpolation>* interpolations = nullptr);

/// Get the shidxs from a topology and save the material paths to @param arnoldSubsets.
///
/// @param subsets Const reference to HdGeomSubsets of the shape.
/// @param numFaces Number of faces on the object.
/// @param arnoldSubsets Output parameter containing the path to all the materials for each geometry subset. The
/// ordering
///  of the materials matches the ordering of the shader indices in the returned array.
/// @return Arnold array of uint8_t, with the shader indices for each face.
HDARNOLD_API
AtArray* HdArnoldGetShidxs(const HdGeomSubsets& subsets, int numFaces, HdArnoldSubsets& arnoldSubsets);

PXR_NAMESPACE_CLOSE_SCOPE
