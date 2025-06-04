//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2022 Luma Pictures
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
#include "light.h"
#include "mesh.h"
#include "instancer.h"

#include <pxr/usd/usdLux/tokens.h>
#include <pxr/usd/usdLux/blackbody.h>

#include <pxr/usd/sdf/assetPath.h>

#include <vector>

#include <common_utils.h>
#include <constant_strings.h>

#include "node_graph.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// These tokens are not exposed in 19.5.
// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    // Shaping parameters are not part of HdTokens in older USD versions
    ((shapingFocus, "shaping:focus"))
    ((shapingFocusTint, "shaping:focusTint"))
    ((shapingConeAngle, "shaping:cone:angle"))
    ((shapingConeSoftness, "shaping:cone:softness"))
    ((shapingIesFile, "shaping:ies:file"))
    ((shapingIesAngleScale, "shaping:ies:angleScale"))
    ((shapingIesNormalize, "shaping:ies:normalize"))
    (treatAsPoint)
    // Barndoor parameters for Houdini
    (barndoorbottom)
    (barndoorbottomedge)
    (barndoorleft)
    (barndoorleftedge)
    (barndoorright)
    (barndoorrightedge)
    (barndoortop)
    (barndoortopedge)
    (filters)
    (GeometryLight)
    ((filtersArray, "filters:i"))
    ((emptyLink, "__arnold_empty_link__"))
);
// clang-format on

struct ParamDesc {
    ParamDesc(const char* aname, const TfToken& hname) : arnoldName(aname), hdName(hname) {}
    AtString arnoldName;
    TfToken hdName;
};

std::vector<ParamDesc> genericParams = {
    {"intensity", UsdLuxTokens->inputsIntensity},
    {"exposure", UsdLuxTokens->inputsExposure},
    {"color", UsdLuxTokens->inputsColor},
    {"diffuse", UsdLuxTokens->inputsDiffuse},
    {"specular", UsdLuxTokens->inputsSpecular},
    {"normalize", UsdLuxTokens->inputsNormalize},
    {"cast_shadows", UsdLuxTokens->inputsShadowEnable},
    {"shadow_color", UsdLuxTokens->inputsShadowColor},
};

std::vector<ParamDesc> pointParams = {{"radius", UsdLuxTokens->inputsRadius}};

std::vector<ParamDesc> spotParams = {
    {"radius", UsdLuxTokens->inputsRadius}, {"cosine_power", UsdLuxTokens->inputsShapingFocus}};

std::vector<ParamDesc> photometricParams = {
    {"filename", UsdLuxTokens->inputsShapingIesFile}, {"radius", UsdLuxTokens->inputsRadius}};

std::vector<ParamDesc> distantParams = {{"angle", UsdLuxTokens->inputsAngle}};

std::vector<ParamDesc> diskParams = {{"radius", UsdLuxTokens->inputsRadius}};

std::vector<ParamDesc> cylinderParams = {{"radius", UsdLuxTokens->inputsRadius}};

void iterateParams(
    AtNode* light, const AtNodeEntry* nentry, const SdfPath& id, HdSceneDelegate* delegate,
    HdArnoldRenderDelegate* renderDelegate, const std::vector<ParamDesc>& params)
{
    for (const auto& param : params) {
        const auto* pentry = AiNodeEntryLookUpParameter(nentry, param.arnoldName);
        if (pentry == nullptr) {
            continue;
        }

        HdArnoldSetParameter(light, pentry, delegate->GetLightParamValue(id, param.hdName), renderDelegate);
    }
}

void readUserData(
    AtNode* light, const SdfPath& id, HdSceneDelegate* delegate, HdArnoldRenderDelegate* renderDelegate)
{
    HdArnoldPrimvarMap primvars;
    std::vector<HdInterpolation> interpolations = {HdInterpolationConstant};
    HdDirtyBits dirtyBits = HdChangeTracker::Clean; // this value doesn't seem to be used in HdArnoldGetPrimvars
    HdArnoldGetPrimvars(delegate, id, dirtyBits, primvars, &interpolations);
    for (const auto &p : primvars) {
        // Get the parameter name, removing the arnold:prefix if any
        std::string paramName(TfStringStartsWith(p.first.GetString(), str::arnold) ? p.first.GetString().substr(7) : p.first.GetString());
        const auto* pentry = AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(light), AtString(paramName.c_str()));
        if (pentry) {
            HdArnoldSetParameter(light, pentry, p.second.value, renderDelegate);
        } else {
            HdArnoldSetConstantPrimvar(light, TfToken(paramName), p.second.role, p.second.value, nullptr,
                nullptr, nullptr, renderDelegate);
        }
    }
}
AtString getLightType(HdSceneDelegate* delegate, const SdfPath& id)
{
    auto isDefault = [&](const TfToken& paramName, float defaultVal) -> bool {
        auto val = delegate->GetLightParamValue(id, paramName);
        if (val.IsEmpty()) {
            return true;
        }
        if (val.IsHolding<float>()) {
            return defaultVal == val.UncheckedGet<float>();
        }
        if (val.IsHolding<double>()) {
            return defaultVal == static_cast<float>(val.UncheckedGet<double>());
        }
        // If it's holding an unexpected type, we won't be
        // able to deal with that anyway, so treat it as
        // default
        return true;
    };
    auto hasIesFile = [&]() -> bool {
        auto val = delegate->GetLightParamValue(id, UsdLuxTokens->inputsShapingIesFile);
        if (val.IsEmpty()) {
            return false;
        }
        if (val.IsHolding<std::string>()) {
            return !val.UncheckedGet<std::string>().empty();
        }
        if (val.IsHolding<SdfAssetPath>()) {
            const auto path = val.UncheckedGet<SdfAssetPath>();
            return !path.GetResolvedPath().empty() || !path.GetAssetPath().empty();
        }
        return false;
    };
    // USD can have a light with spot shaping + photometric IES profile, but arnold 
    // doesn't support both together. Here we first check if a IES Path is set (#1316), 
    // and if so we translate this as an arnold photometric light (which won't have any spot cone). 
    if (hasIesFile())
        return str::photometric_light;

    // Then, if any of the shaping params exists or non-default we have a spot light.
    if (!isDefault(UsdLuxTokens->inputsShapingFocus, 0.0f) ||
        !isDefault(UsdLuxTokens->inputsShapingConeAngle, 180.0f) ||
        !isDefault(UsdLuxTokens->inputsShapingConeSoftness, 0.0f)) {
        return str::spot_light;
    }
    // Finally, we default to a point light
    return str::point_light;
}

auto spotLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                        HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate* renderDelegate) {
    
    iterateParams(light, nentry, id, sceneDelegate, renderDelegate, spotParams);
    const auto treatAsPointValue = sceneDelegate->GetLightParamValue(id, UsdLuxTokens->treatAsPoint);
    if (treatAsPointValue.IsHolding<bool>() && treatAsPointValue.UncheckedGet<bool>()) {
        AiNodeResetParameter(light, str::radius);
        AiNodeResetParameter(light, str::normalize);
    }
    const auto hdAngle =
        sceneDelegate->GetLightParamValue(id, UsdLuxTokens->inputsShapingConeAngle).GetWithDefault(180.0f);
    const auto softness =
        sceneDelegate->GetLightParamValue(id, UsdLuxTokens->inputsShapingConeSoftness).GetWithDefault(0.0f);
    const auto arnoldAngle = hdAngle * 2.0f;
    const auto penumbra = arnoldAngle * softness;
    AiNodeSetFlt(light, str::cone_angle, arnoldAngle);
    AiNodeSetFlt(light, str::penumbra_angle, penumbra);
    // Barndoor parameters are only exposed in houdini for now.
    auto hasBarndoor = false;
    auto getBarndoor = [&](const TfToken& name) -> float {
        const auto barndoor = AiClamp(sceneDelegate->GetLightParamValue(id, name).GetWithDefault(0.0f), 0.0f, 1.0f);
        if (barndoor > AI_EPSILON) {
            hasBarndoor = true;
        }
        return barndoor;
    };
    const auto barndoorbottom = getBarndoor(_tokens->barndoorbottom);
    const auto barndoorbottomedge = getBarndoor(_tokens->barndoorbottomedge);
    const auto barndoorleft = getBarndoor(_tokens->barndoorleft);
    const auto barndoorleftedge = getBarndoor(_tokens->barndoorleftedge);
    const auto barndoorright = getBarndoor(_tokens->barndoorright);
    const auto barndoorrightedge = getBarndoor(_tokens->barndoorrightedge);
    const auto barndoortop = getBarndoor(_tokens->barndoortop);
    const auto barndoortopedge = getBarndoor(_tokens->barndoortopedge);
    auto createBarndoor = [&](const std::string &name, const AtNode *procParent) 
    { *filter = renderDelegate->CreateArnoldNode(str::barndoor, AtString(name.c_str())); };
    if (hasBarndoor) {
        std::string filterName = id.GetString();
        filterName += std::string("@barndoor");

        // We check if the filter is non-zero and if it's a barndoor
        if (*filter == nullptr) {
            createBarndoor(filterName, renderDelegate->GetProceduralParent());
        } else if (!AiNodeIs(*filter, str::barndoor)) {
            renderDelegate->DestroyArnoldNode(*filter);
            createBarndoor(filterName, renderDelegate->GetProceduralParent());
        }
        // The edge parameters behave differently in Arnold vs Houdini.
        // For bottom left/right and right top/bottom we have to invert the Houdini value.
        AiNodeSetFlt(*filter, str::barndoor_bottom_left, 1.0f - barndoorbottom);
        AiNodeSetFlt(*filter, str::barndoor_bottom_right, 1.0f - barndoorbottom);
        AiNodeSetFlt(*filter, str::barndoor_bottom_edge, barndoorbottomedge);
        AiNodeSetFlt(*filter, str::barndoor_left_top, barndoorleft);
        AiNodeSetFlt(*filter, str::barndoor_left_bottom, barndoorleft);
        AiNodeSetFlt(*filter, str::barndoor_left_edge, barndoorleftedge);
        AiNodeSetFlt(*filter, str::barndoor_right_top, 1.0f - barndoorright);
        AiNodeSetFlt(*filter, str::barndoor_right_bottom, 1.0f - barndoorright);
        AiNodeSetFlt(*filter, str::barndoor_right_edge, barndoorrightedge);
        AiNodeSetFlt(*filter, str::barndoor_top_left, barndoortop);
        AiNodeSetFlt(*filter, str::barndoor_top_right, barndoortop);
        AiNodeSetFlt(*filter, str::barndoor_top_edge, barndoortopedge);
        AiNodeSetPtr(light, str::filters, *filter);
    } else {
        // We disconnect the filter.
        AiNodeSetArray(light, str::filters, AiArray(0, 1, AI_TYPE_NODE));
    }
    readUserData(light, id, sceneDelegate, renderDelegate);
};

auto pointLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                         HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate* renderDelegate) {
    TF_UNUSED(filter);
    iterateParams(light, nentry, id, sceneDelegate, renderDelegate, pointParams);
    const VtValue treatAsPointValue = sceneDelegate->GetLightParamValue(id, UsdLuxTokens->treatAsPoint);
    if (treatAsPointValue.IsHolding<bool>() && treatAsPointValue.UncheckedGet<bool>()) {
        AiNodeResetParameter(light, str::radius);
        AiNodeResetParameter(light, str::normalize);
    }
    readUserData(light, id, sceneDelegate, renderDelegate);
};

auto photometricLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                               HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate* renderDelegate) {
    TF_UNUSED(filter);
    iterateParams(light, nentry, id, sceneDelegate, renderDelegate, photometricParams);

    const VtValue treatAsPointValue = sceneDelegate->GetLightParamValue(id, UsdLuxTokens->treatAsPoint);
    if (treatAsPointValue.IsHolding<bool>() && treatAsPointValue.UncheckedGet<bool>()) {
        AiNodeResetParameter(light, str::radius);
        AiNodeResetParameter(light, str::normalize);
    }

    readUserData(light, id, sceneDelegate, renderDelegate);
};

// Spot lights are sphere lights with shaping parameters

auto distantLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                           HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate* renderDelegate) {
    TF_UNUSED(filter);
    iterateParams(light, nentry, id, sceneDelegate, renderDelegate, distantParams);
    readUserData(light, id, sceneDelegate, renderDelegate);

    bool ignoreNormalize = true;

#if ARNOLD_VERSION_NUM >= 70400
    AtNode* options = AiUniverseGetOptions(renderDelegate->GetUniverse());
    if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(options), str::usd_legacy_distant_light_normalize)) {
        ignoreNormalize = AiNodeGetBool(options, str::usd_legacy_distant_light_normalize);
    }    
#endif
    if (ignoreNormalize) {
        // For distant lights, we want to ignore the normalize attribute, as it's not behaving
        // as expected in arnold (see #1191)
        AiNodeResetParameter(light, str::normalize);
    }
};

auto diskLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                        HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate* renderDelegate) {
    TF_UNUSED(filter);
    iterateParams(light, nentry, id, sceneDelegate, renderDelegate, diskParams);
    readUserData(light, id, sceneDelegate, renderDelegate);
};

auto rectLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                        HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate* renderDelegate) {
    TF_UNUSED(filter);
    float width = 1.0f;
    float height = 1.0f;
    const auto& widthValue = sceneDelegate->GetLightParamValue(id, UsdLuxTokens->inputsWidth);
    if (widthValue.IsHolding<float>()) {
        width = widthValue.UncheckedGet<float>();
    }
    const auto& heightValue = sceneDelegate->GetLightParamValue(id, UsdLuxTokens->inputsHeight);
    if (heightValue.IsHolding<float>()) {
        height = heightValue.UncheckedGet<float>();
    }

    width /= 2.0f;
    height /= 2.0f;

    AiNodeSetArray(
        light, str::vertices,
        AiArray(
            4, 1, AI_TYPE_VECTOR, AtVector(width, -height, 0.0f), AtVector(-width, -height, 0.0f),
            AtVector(-width, height, 0.0f), AtVector(width, height, 0.0f)));

    readUserData(light, id, sceneDelegate, renderDelegate);
};

auto geometryLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                            HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate* renderDelegate)
{
    TF_UNUSED(filter);
    VtValue geomValue = sceneDelegate->Get( id, str::t_geometry);
    if (geomValue.IsHolding<SdfPath>()) {
        SdfPath geomPath = geomValue.UncheckedGet<SdfPath>();
        //const HdArnoldMesh *hdMesh = dynamic_cast<const HdArnoldMesh*>(sceneDelegate->GetRenderIndex().GetRprim(geomPath));
        AtNode *mesh = renderDelegate->LookupNode(geomPath.GetText());
        if (mesh != nullptr && !AiNodeIs(mesh, str::polymesh))
            mesh = nullptr;
        AiNodeSetPtr(light, str::mesh,(void*) mesh);
    }    
    readUserData(light, id, sceneDelegate, renderDelegate);
};

auto cylinderLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                            HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate* renderDelegate) {
    TF_UNUSED(filter);
    iterateParams(light, nentry, id, sceneDelegate, renderDelegate, cylinderParams);
    float length = 1.0f;
    const auto& lengthValue = sceneDelegate->GetLightParamValue(id, UsdLuxTokens->inputsLength);
    if (lengthValue.IsHolding<float>()) {
        length = lengthValue.UncheckedGet<float>();
    }
    length /= 2.0f;
    AiNodeSetVec(light, str::bottom, -length, 0.0f, 0.0f);
    AiNodeSetVec(light, str::top, length, 0.0f, 0.0f);
    readUserData(light, id, sceneDelegate, renderDelegate);
};

auto domeLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                        HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate* renderDelegate) {
    TF_UNUSED(filter);
    const auto& formatValue = sceneDelegate->GetLightParamValue(id, UsdLuxTokens->inputsTextureFormat);
    if (formatValue.IsHolding<TfToken>()) {
        const auto& textureFormat = formatValue.UncheckedGet<TfToken>();
        if (textureFormat == UsdLuxTokens->latlong) {
            AiNodeSetStr(light, str::format, str::latlong);
        } else if (textureFormat == UsdLuxTokens->mirroredBall) {
            AiNodeSetStr(light, str::format, str::mirrored_ball);
        } else {
            AiNodeSetStr(light, str::format, str::angular); // default value
        }
    }
    readUserData(light, id, sceneDelegate, renderDelegate);
};

/// Utility class to translate Hydra lights for th Render Delegate.
class HdArnoldGenericLight : public HdLight {
public:
    using SyncParams = void (*)(
        AtNode*, AtNode** filter, const AtNodeEntry*, const SdfPath&, HdSceneDelegate*, HdArnoldRenderDelegate*);

    /// Internal constructor for creating HdArnoldGenericLight.
    ///
    /// @param delegate Pointer to the Render Delegate.
    /// @param id Path to the Hydra Primitive.
    /// @param arnoldType The type of the Arnold light.
    /// @param sync Function object syncing light specific parameters between
    ///  the Hydra Primitive and the Arnold Light.
    /// @param supportsTexture Value to indicate if the light supports textures.
    HdArnoldGenericLight(
        HdArnoldRenderDelegate* delegate, const SdfPath& id, const AtString& arnoldType, const SyncParams& sync,
        bool supportsTexture = false);

    /// Destructor for the Arnold Light.
    ///
    /// Destroys Arnold Light and any shaders created.
    ~HdArnoldGenericLight() override;

    /// Syncing parameters from the Hydra primitive to the Arnold light.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderParam Pointer to HdArnoldRenderParam.
    /// @param dirtyBits Dirty Bits of the Hydra primitive.
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;

    /// Returns the set of initial dirty bits.
    ///
    /// @return Value of the initial dirty bits.
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Sets up the texture for the Arnold Light.
    ///
    /// @param value Value holding an SdfAssetPath to the texture.
    void SetupTexture(const VtValue& value);

    /// Returns the stored arnold light node.
    ///
    /// @return The arnold light node stored.
    AtNode* GetLightNode() const;

private:
    SyncParams _syncParams;            ///< Function object to sync light parameters.
    HdArnoldRenderDelegate* _delegate; ///< Pointer to the Render Delegate.
    AtNode* _light = nullptr;          ///< Pointer to the Arnold Light.
    AtNode* _texture = nullptr;        ///< Pointer to the Arnold Texture Shader.
    AtNode* _filter = nullptr;         ///< Pointer to the Arnold Light filter for barndoor effects.
    TfToken _lightLink;                ///< Light Link collection the light belongs to.
    TfToken _shadowLink;               ///< Shadow Link collection the light belongs to.
    bool _supportsTexture = false;     ///< Value indicating texture support.
    bool _hasNodeGraphs = false;
    std::vector<AtNode*> _instancers;        ///< Pointer to the Arnold instancer and its parent instancers if any.
};

HdArnoldGenericLight::HdArnoldGenericLight(
    HdArnoldRenderDelegate* delegate, const SdfPath& id, const AtString& arnoldType,
    const HdArnoldGenericLight::SyncParams& sync, bool supportsTexture)
    : HdLight(id),
      _syncParams(sync),
      _delegate(delegate),
      _lightLink(_tokens->emptyLink),
      _shadowLink(_tokens->emptyLink),
      _supportsTexture(supportsTexture)
{
    if (!arnoldType.empty()) {
        _light = delegate->CreateArnoldNode(arnoldType, AtString(id.GetText()));
        if (id.IsEmpty()) {
            AiNodeSetFlt(_light, str::intensity, 0.0f);
        } 
    }
}

HdArnoldGenericLight::~HdArnoldGenericLight()
{
    if (_lightLink != _tokens->emptyLink) {
        _delegate->DeregisterLightLinking(_lightLink, this, false);
    }
    if (_shadowLink != _tokens->emptyLink) {
        _delegate->DeregisterLightLinking(_shadowLink, this, true);
    }
    _delegate->DestroyArnoldNode(_light);
    _delegate->DestroyArnoldNode(_texture);
    _delegate->DestroyArnoldNode(_filter);
    _delegate->ClearDependencies(GetId());
    for (auto &instancer : _instancers) {
        _delegate->UntrackRenderTag(instancer);
        _delegate->DestroyArnoldNode(instancer);
    }
}

void HdArnoldGenericLight::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{

    std::cout << "HdArnoldGenericLight::Sync " << GetId().GetString() << std::endl;
    if (!_delegate->CanUpdateScene())
        return;
    auto* param = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
    TF_UNUSED(sceneDelegate);
    TF_UNUSED(dirtyBits);
    const auto& id = GetId();
    const AtNodeEntry* nentry = _light ? AiNodeGetNodeEntry(_light) : nullptr;
    const AtString lightType = nentry ? AiNodeEntryGetNameAtString(nentry) : AtString();

    // TODO find why we're not getting the proper dirtyBits for mesh lights, even though the 
    // adapter is returning HdLight::AllDirty
    if ((*dirtyBits & HdLight::DirtyParams) || lightType == str::mesh_light || _light == nullptr) {
        param->Interrupt();

        // If the params have changed, we need to see if any of the shaping parameters were applied to the
        // sphere light.
        if (_light == nullptr || lightType == str::spot_light || lightType == str::point_light || lightType == str::photometric_light) {
            const auto newLightType = getLightType(sceneDelegate, id);
            if (newLightType != lightType) {
                if (_light) {
                    AiNodeSetStr(_light, str::name, AtString());
                    _delegate->DestroyArnoldNode(_light);
                }
                
                _light = _delegate->CreateArnoldNode(newLightType, AtString(id.GetText())); 
                nentry = AiNodeGetNodeEntry(_light);
                if (newLightType == str::point_light) {
                    _syncParams = pointLightSync;
                } else if (newLightType == str::spot_light) {
                    _syncParams = spotLightSync;
                } else {
                    _syncParams = photometricLightSync;
                }
                if (_lightLink != _tokens->emptyLink) {
                    _delegate->DeregisterLightLinking(_shadowLink, this, false);
                    _lightLink = _tokens->emptyLink;
                }
                if (_shadowLink != _tokens->emptyLink) {
                    _delegate->DeregisterLightLinking(_shadowLink, this, true);
                    _shadowLink = _tokens->emptyLink;
                }
            }
        }
        // We need to force dirtying the transform, because AiNodeReset resets the transformation.
        *dirtyBits |= HdLight::DirtyTransform;
        AiNodeReset(_light);
                
        // convert the generic lights parameters
        iterateParams(_light, nentry, id, sceneDelegate, _delegate, genericParams);
        // convert the light specific attributes
        _syncParams(_light, &_filter, nentry, id, sceneDelegate, _delegate);
        
        // Primvars are not officially supported on lights, but pre-20.11 the query functions checked for primvars
        // on all primitives uniformly. We have to pass the full name of the primvar post-20.11 to make this bit still
        // work.
        for (const auto& primvar : sceneDelegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationConstant)) {
            ConvertPrimvarToBuiltinParameter(
                _light, primvar.name,
                sceneDelegate->Get(
                  id,
#if PXR_VERSION < 2111
                  TfToken { TfStringPrintf("primvars:%s", primvar.name.GetText()) }
#else
                  primvar.name
#endif
                  ),
               nullptr, nullptr, nullptr, _delegate);
        }
        // Compute the light shaders, through primvars:arnold:shaders, that will eventually
        // connect shaders to the color, or some light filters.
        SdfPath lightShaderPath = HdArnoldLight::ComputeLightShaders(sceneDelegate, _delegate, id, TfToken("primvars:arnold:shaders"), _light);

        // If a light shader was specified, we don't need to take into account the light temperature
        // nor the eventual file texture, as it will be overridden by the connection #2307
        if (!AiNodeIsLinked(_light, str::color)) {

            // Check if light temperature is enabled, and eventually set the light color properly
            const TfToken enableColorTemperatureToken(UsdLuxTokens->inputsEnableColorTemperature);
            const TfToken colorTemperatureToken(UsdLuxTokens->inputsColorTemperature);

            const auto enableTemperatureValue = 
                sceneDelegate->GetLightParamValue(id, enableColorTemperatureToken);
            // We only apply the temperature color if there's no shader connected to the light color
            if (enableTemperatureValue.IsHolding<bool>() && enableTemperatureValue.UncheckedGet<bool>()) {
                const auto temperature =
                    sceneDelegate->GetLightParamValue(id, colorTemperatureToken).GetWithDefault(6500.f);

                // Get the light color that was previously set in iterateParams, 
                // then multiply it with the temperature color      
                GfVec3f tempColor = UsdLuxBlackbodyTemperatureAsRgb(temperature);
                AtRGB atTempColor(tempColor[0], tempColor[1], tempColor[2]);
                AtRGB color = AiNodeGetRGB(_light, str::color);
                color *= atTempColor;
                AiNodeSetRGB(_light, str::color, color[0], color[1], color[2]);
            }
            if (_supportsTexture) {
                SetupTexture(sceneDelegate->GetLightParamValue(id, UsdLuxTokens->inputsTextureFile));
            }
        }

        const auto filtersValue = sceneDelegate->GetLightParamValue(id, _tokens->filters);
        if (filtersValue.IsHolding<SdfPathVector>()) {
            const auto& filterPaths = filtersValue.UncheckedGet<SdfPathVector>();
            std::vector<AtNode*> filters;
            filters.reserve(filterPaths.size());
            for (const auto& filterPath : filterPaths) {
                auto* filterMaterial = HdArnoldNodeGraph::GetNodeGraph(sceneDelegate->GetRenderIndex(), filterPath);
                if (filterMaterial == nullptr) {
                    continue;
                }
                auto* filter = filterMaterial->GetCachedSurfaceShader();
                if (filter == nullptr) {
                    continue;
                }
                auto* filterEntry = AiNodeGetNodeEntry(filter);
                // Light filters are shaders with a none output type.
                if (AiNodeEntryGetOutputType(filterEntry) == AI_TYPE_NONE) {
                    filters.push_back(filter);
                }
            }
            if (filters.empty()) {
                AiNodeSetArray(_light, str::filters, AiArray(0, 0, AI_TYPE_NODE));
            } else {
                AiNodeSetArray(_light, str::filters, AiArrayConvert(filters.size(), 1, AI_TYPE_NODE, filters.data()));
            }
        }
        AiNodeSetDisabled(_light, !sceneDelegate->GetVisible(id));
    
        // Get an eventual hydra instancer and rebuild the arnold instancer nodes.
        for (auto &instancerNode : _instancers) {
            _delegate->DestroyArnoldNode(instancerNode);
        }
        _instancers.clear();

        const SdfPath &instancerId = sceneDelegate->GetInstancerId(id);
        if (!instancerId.IsEmpty()) {
            auto& renderIndex = sceneDelegate->GetRenderIndex();
            auto* instancer = static_cast<HdArnoldInstancer*>(renderIndex.GetInstancer(instancerId));
            HdDirtyBits bits = HdChangeTracker::AllDirty;
            // The Sync function seems to be called automatically for shapes, but 
            // not for lights
            instancer->Sync(sceneDelegate, renderParam, &bits);
            instancer->CalculateInstanceMatrices(_delegate, id, _instancers);
            const TfToken renderTag = sceneDelegate->GetRenderTag(id);
            float lightIntensity = AiNodeGetFlt(_light, str::intensity);
            // For instance of lights, we need to disable the prototype light
            // by setting its intensity to 0. The instancer can then have a user data
            // instance_intensity with the actual intensity value to use for each instance, 
            // and this will be applied to each instance
            for (size_t i = 0; i < _instancers.size(); ++i) {
                AiNodeSetPtr(_instancers[i], str::nodes, (i == 0) ? _light : _instancers[i - 1]);
                _delegate->TrackRenderTag(_instancers[i], renderTag);
                AiNodeDeclare(_instancers[i], str::instance_intensity, "constant ARRAY FLOAT");
                // If the instance array has a single element, it will be applied to all instances,
                // which is what we need to do here for the light intensity
                AiNodeSetArray(_instancers[i], str::instance_intensity, 
                    AiArrayConvert(1, 1, AI_TYPE_FLOAT, &lightIntensity));                
            }
            // Ensure the prototype light is hidden
            AiNodeSetFlt(_light, str::intensity, 0.f);
        }


        HdArnoldRenderDelegate::PathSetWithDirtyBits pathSet;
        if (!lightShaderPath.IsEmpty())
            pathSet.insert({lightShaderPath, HdLight::DirtyParams});

        // If we previously had node graph connected, we need to call TrackDependencies
        // even if our list is empty. This is needed to clear the previous dependencies
        if (_hasNodeGraphs || !pathSet.empty()) {
            _delegate->TrackDependencies(id, pathSet);
        }
        _hasNodeGraphs = !pathSet.empty();
    }

    if (*dirtyBits & HdLight::DirtyTransform) {
        param->Interrupt();
        HdArnoldSetTransform(_light, sceneDelegate, id);
    }

    if (*dirtyBits & (DirtyParams | DirtyShadowParams | DirtyCollection)) {
        auto updateLightLinking = [&](TfToken& currentLink, const TfToken& linkName, bool isShadow) {
            auto linkValue = sceneDelegate->GetLightParamValue(id, linkName);
            if (linkValue.IsHolding<TfToken>()) {
                const auto& link = linkValue.UncheckedGet<TfToken>();
                if (currentLink != link) {
                    param->Interrupt();
                    // The empty link value only exists when creating the class, so link can never match emptyLink.
                    if (currentLink != _tokens->emptyLink) {
                        _delegate->DeregisterLightLinking(currentLink, this, isShadow);
                    }
                    _delegate->RegisterLightLinking(link, this, isShadow);
                    currentLink = link;
                }
            }
        };
        updateLightLinking(_lightLink, UsdLuxTokens->lightLink, false);
        updateLightLinking(_shadowLink, UsdLuxTokens->shadowLink, true);
    }
    *dirtyBits = HdLight::Clean;
}

void HdArnoldGenericLight::SetupTexture(const VtValue& value)
{
    const auto* nentry = AiNodeGetNodeEntry(_light);

    std::string path;
    if (value.IsHolding<SdfAssetPath>()) {
        const auto& assetPath = value.UncheckedGet<SdfAssetPath>();
        path = assetPath.GetResolvedPath();
        if (path.empty()) {
            path = assetPath.GetAssetPath();
        }
    }

    if (path.empty()) {
        // no texture to connect, let's delete the eventual previous texture
        if (_texture)    
            _delegate->DestroyArnoldNode(_texture);
        _texture = nullptr;        
        return;
    }

    std::string imageName(AiNodeGetName(_light));
    imageName += "/texture_file";

    if (_texture == nullptr)
        _texture = _delegate->CreateArnoldNode(str::image, AtString(imageName.c_str()));
    
    AiNodeSetStr(_texture, str::filename, AtString(path.c_str()));
    if (AiNodeEntryGetNameAtString(nentry) == str::quad_light) {
        AiNodeSetBool(_texture, str::sflip, true);
    }
    AtRGB color = AiNodeGetRGB(_light, str::color);
    AiNodeSetRGB(_texture, str::multiply, color[0], color[1], color[2]);
    AiNodeResetParameter(_light, str::color);
    AiNodeLink(_texture, str::color, _light);
}

HdDirtyBits HdArnoldGenericLight::GetInitialDirtyBitsMask() const
{
    return HdLight::DirtyParams | HdLight::DirtyTransform;
}

AtNode* HdArnoldGenericLight::GetLightNode() const { return _light; }

} // namespace

namespace HdArnoldLight {

HdLight* CreatePointLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
{
    // Special case for Hydra point lights. They can correspond to multiple arnold light types
    // (point, spot, photometric). So we give it an empty node type to defer the node creation
    // to the Sync function (where the actual type will be known)
    return new HdArnoldGenericLight(renderDelegate, id, AtString(), pointLightSync);
}

HdLight* CreateDistantLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(renderDelegate, id, str::distant_light, distantLightSync);
}

HdLight* CreateDiskLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(renderDelegate, id, str::disk_light, diskLightSync);
}

HdLight* CreateRectLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(renderDelegate, id, str::quad_light, rectLightSync, true);
}

HdLight* CreateCylinderLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(renderDelegate, id, str::cylinder_light, cylinderLightSync);
}
HdLight* CreateGeometryLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(renderDelegate, id, str::mesh_light, geometryLightSync);
}

HdLight* CreateDomeLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(renderDelegate, id, str::skydome_light, domeLightSync, true);
}

AtNode* GetLightNode(const HdLight* light)
{
    if (Ai_unlikely(light == nullptr))
        return nullptr;
    return static_cast<const HdArnoldGenericLight*>(light)->GetLightNode();
}

SdfPath ComputeLightShaders(HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate *renderDelegate, const SdfPath &id, const TfToken &attr, AtNode *light )
{
    // get the sdf path for the light shader arnold node graph container
    SdfPath lightShaderPath;
    ArnoldUsdCheckForSdfPathValue(sceneDelegate->GetLightParamValue(id, attr),
                                  [&](const SdfPath& p) { lightShaderPath = p; });

    AtNode *color = nullptr;
    std::vector<AtNode *> lightFilters;
    if (!lightShaderPath.IsEmpty()) {
        HdArnoldNodeGraph *nodeGraph = HdArnoldNodeGraph::GetNodeGraph(&sceneDelegate->GetRenderIndex(), lightShaderPath);
        if (nodeGraph) {
            color = nodeGraph->GetOrCreateTerminal(sceneDelegate, str::t_color);
            if (color) {
                // Only certain types of light can be linked
                if (AiNodeIs(light, str::skydome_light) ||
                        AiNodeIs(light, str::quad_light) ||
                        AiNodeIs(light, str::mesh_light)) {
                    AiNodeLink(color, str::color, light);
                } else {
                    AiMsgWarning("%s : Cannot connect shader to light's color for \"%s\"", AiNodeGetName(light), AiNodeEntryGetName(AiNodeGetNodeEntry(light)));
                }
            } 
            
            lightFilters = nodeGraph->GetOrCreateTerminals(sceneDelegate, _tokens->filtersArray);
            if (!lightFilters.empty()) {
                VtArray<std::string> filtersNodeName;
                for (const AtNode *node:lightFilters) {
                    filtersNodeName.push_back(AiNodeGetName(node));
                }
                // Here we use HdArnoldSetParameter instead of AiNodeSetArray because it will defer connecting the filters
                // to the ProcessConnection method which happens later in the process. This is how the procedural behaves.
                HdArnoldSetParameter(light, AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(light), str::filters), VtValue(filtersNodeName), renderDelegate);
            }
        }
    }
    return lightShaderPath;
}

} // namespace HdArnoldLight

PXR_NAMESPACE_CLOSE_SCOPE
