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
#include "light.h"

#include <pxr/usd/usdLux/tokens.h>

#include <pxr/usd/sdf/assetPath.h>

#include <vector>

#include "constant_strings.h"
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
    // Barndoor parameters for Houdini
    (barndoorbottom)
    (barndoorbottomedge)
    (barndoorleft)
    (barndoorleftedge)
    (barndoorright)
    (barndoorrightedge)
    (barndoortop)
    (barndoortopedge)
);
// clang-format on

struct ParamDesc {
    ParamDesc(const char* aname, const TfToken& hname) : arnoldName(aname), hdName(hname) {}
    AtString arnoldName;
    TfToken hdName;
};

std::vector<ParamDesc> genericParams = {
    {"intensity", HdLightTokens->intensity},
    {"exposure", HdLightTokens->exposure},
    {"color", HdLightTokens->color},
    {"diffuse", HdLightTokens->diffuse},
    {"specular", HdLightTokens->specular},
    {"normalize", HdLightTokens->normalize},
    {"cast_shadows", HdLightTokens->shadowEnable},
    {"shadow_color", HdLightTokens->shadowColor},
};

std::vector<ParamDesc> pointParams = {{"radius", HdLightTokens->radius}};

std::vector<ParamDesc> spotParams = {{"radius", HdLightTokens->radius}, {"cosine_power", HdLightTokens->shapingFocus}};

std::vector<ParamDesc> photometricParams = {{"filename", _tokens->shapingIesFile}, {"radius", HdLightTokens->radius}};

std::vector<ParamDesc> distantParams = {{"angle", HdLightTokens->angle}};

std::vector<ParamDesc> diskParams = {{"radius", HdLightTokens->radius}};

std::vector<ParamDesc> cylinderParams = {{"radius", HdLightTokens->radius}};

void iterateParams(
    AtNode* light, const AtNodeEntry* nentry, const SdfPath& id, HdSceneDelegate* delegate,
    const std::vector<ParamDesc>& params)
{
    for (const auto& param : params) {
        const auto* pentry = AiNodeEntryLookUpParameter(nentry, param.arnoldName);
        if (pentry == nullptr) {
            continue;
        }

        HdArnoldSetParameter(light, pentry, delegate->GetLightParamValue(id, param.hdName));
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
        auto val = delegate->GetLightParamValue(id, _tokens->shapingIesFile);
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
    // If any of the shaping params exists or non-default we have a spot light.
    if (!isDefault(_tokens->shapingFocus, 0.0f) || !isDefault(_tokens->shapingConeAngle, 180.0f) ||
        !isDefault(_tokens->shapingConeSoftness, 0.0f)) {
        return str::spot_light;
    }
    return hasIesFile() ? str::photometric_light : str::point_light;
}

auto spotLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                        HdSceneDelegate* delegate) {
    iterateParams(light, nentry, id, delegate, spotParams);

    const auto hdAngle = delegate->GetLightParamValue(id, _tokens->shapingConeAngle).GetWithDefault(180.0f);
    const auto softness = delegate->GetLightParamValue(id, _tokens->shapingConeSoftness).GetWithDefault(0.0f);
    const auto arnoldAngle = hdAngle * 2.0f;
    const auto penumbra = arnoldAngle * softness;
    AiNodeSetFlt(light, str::cone_angle, arnoldAngle);
    AiNodeSetFlt(light, str::penumbra_angle, penumbra);
    // Barndoor parameters are only exposed in houdini for now.
    auto hasBarndoor = false;
    auto getBarndoor = [&](const TfToken& name) -> float {
        const auto barndoor = AiClamp(delegate->GetLightParamValue(id, name).GetWithDefault(0.0f), 0.0f, 1.0f);
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
    auto createBarndoor = [&]() { *filter = AiNode(str::barndoor); };
    if (hasBarndoor) {
        // We check if the filter is non-zero and if it's a barndoor
        if (*filter == nullptr) {
            createBarndoor();
        } else if (!AiNodeIs(*filter, str::barndoor)) {
            AiNodeDestroy(*filter);
            createBarndoor();
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
};

auto pointLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                         HdSceneDelegate* delegate) {
    TF_UNUSED(filter);
    iterateParams(light, nentry, id, delegate, pointParams);
};

auto photometricLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                               HdSceneDelegate* delegate) {
    TF_UNUSED(filter);
    iterateParams(light, nentry, id, delegate, photometricParams);
};

// Spot lights are sphere lights with shaping parameters

auto distantLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                           HdSceneDelegate* delegate) {
    TF_UNUSED(filter);
    iterateParams(light, nentry, id, delegate, distantParams);
};

auto diskLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                        HdSceneDelegate* delegate) {
    TF_UNUSED(filter);
    iterateParams(light, nentry, id, delegate, diskParams);
};

auto rectLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                        HdSceneDelegate* delegate) {
    TF_UNUSED(filter);
    float width = 1.0f;
    float height = 1.0f;
    const auto& widthValue = delegate->GetLightParamValue(id, HdLightTokens->width);
    if (widthValue.IsHolding<float>()) {
        width = widthValue.UncheckedGet<float>();
    }
    const auto& heightValue = delegate->GetLightParamValue(id, HdLightTokens->height);
    if (heightValue.IsHolding<float>()) {
        height = heightValue.UncheckedGet<float>();
    }

    width /= 2.0f;
    height /= 2.0f;

    AiNodeSetArray(
        light, "vertices",
        AiArray(
            4, 1, AI_TYPE_VECTOR, AtVector(-width, height, 0.0f), AtVector(width, height, 0.0f),
            AtVector(width, -height, 0.0f), AtVector(-width, -height, 0.0f)));
};

auto cylinderLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                            HdSceneDelegate* delegate) {
    TF_UNUSED(filter);
    iterateParams(light, nentry, id, delegate, cylinderParams);
    float length = 1.0f;
    const auto& lengthValue = delegate->GetLightParamValue(id, UsdLuxTokens->length);
    if (lengthValue.IsHolding<float>()) {
        length = lengthValue.UncheckedGet<float>();
    }
    length /= 2.0f;
    AiNodeSetVec(light, "bottom", 0.0f, -length, 0.0f);
    AiNodeSetVec(light, "top", 0.0f, length, 0.0f);
};

auto domeLightSync = [](AtNode* light, AtNode** filter, const AtNodeEntry* nentry, const SdfPath& id,
                        HdSceneDelegate* delegate) {
    TF_UNUSED(filter);
    const auto& formatValue = delegate->GetLightParamValue(id, UsdLuxTokens->textureFormat);
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
};

/// Utility class to translate Hydra lights for th Render Delegate.
class HdArnoldGenericLight : public HdLight {
public:
    using SyncParams = void (*)(AtNode*, AtNode** filter, const AtNodeEntry*, const SdfPath&, HdSceneDelegate*);

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

private:
    SyncParams _syncParams;            ///< Function object to sync light parameters.
    HdArnoldRenderDelegate* _delegate; ///< Pointer to the Render Delegate.
    AtNode* _light;                    ///< Pointer to the Arnold Light.
    AtNode* _texture = nullptr;        ///< Pointer to the Arnold Texture Shader.
    AtNode* _filter = nullptr;         ///< Pointer to the Arnold Light filter for barndoor effects.
    bool _supportsTexture = false;     ///< Value indicating texture support.
};

HdArnoldGenericLight::HdArnoldGenericLight(
    HdArnoldRenderDelegate* delegate, const SdfPath& id, const AtString& arnoldType,
    const HdArnoldGenericLight::SyncParams& sync, bool supportsTexture)
    : HdLight(id), _syncParams(sync), _delegate(delegate), _supportsTexture(supportsTexture)
{
    _light = AiNode(_delegate->GetUniverse(), arnoldType);
    if (id.IsEmpty()) {
        AiNodeSetFlt(_light, "intensity", 0.0f);
    } else {
        AiNodeSetStr(_light, "name", id.GetText());
    }
}

HdArnoldGenericLight::~HdArnoldGenericLight()
{
    AiNodeDestroy(_light);
    if (_texture != nullptr) {
        AiNodeDestroy(_texture);
    }
    if (_filter != nullptr) {
        AiNodeDestroy(_filter);
    }
}

void HdArnoldGenericLight::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    auto* param = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
    TF_UNUSED(sceneDelegate);
    TF_UNUSED(dirtyBits);
    if (*dirtyBits & HdLight::DirtyParams) {
        // If the params have changed, we need to see if any of the shaping parameters were applied to the
        // sphere light.
        const auto id = GetId();
        const auto* nentry = AiNodeGetNodeEntry(_light);
        const auto lightType = AiNodeEntryGetNameAtString(nentry);
        auto interrupted = false;
        if (lightType == str::spot_light || lightType == str::point_light || lightType == str::photometric_light) {
            const auto newLightType = getLightType(sceneDelegate, id);
            if (newLightType != lightType) {
                param->Interrupt();
                interrupted = true;
                const AtString oldName{AiNodeGetName(_light)};
                AiNodeDestroy(_light);
                _light = AiNode(newLightType);
                nentry = AiNodeGetNodeEntry(_light);
                AiNodeSetStr(_light, str::name, oldName);
                if (newLightType == str::point_light) {
                    _syncParams = pointLightSync;
                } else if (newLightType == str::spot_light) {
                    _syncParams = spotLightSync;
                } else {
                    _syncParams = photometricLightSync;
                }
            }
        }
        // We need to force dirtying the transform, because AiNodeReset resets the transformation.
        *dirtyBits |= HdLight::DirtyTransform;
        if (!interrupted) {
            param->Interrupt();
        }

        AiNodeReset(_light);
        iterateParams(_light, nentry, id, sceneDelegate, genericParams);
        _syncParams(_light, &_filter, nentry, id, sceneDelegate);
        if (_supportsTexture) {
            SetupTexture(sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile));
        }
        for (const auto& primvar : sceneDelegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationConstant)) {
            ConvertPrimvarToBuiltinParameter(_light, primvar.name, sceneDelegate->Get(id, primvar.name));
        }
    }

    if (*dirtyBits & HdLight::DirtyTransform) {
        param->Interrupt();
        HdArnoldSetTransform(_light, sceneDelegate, GetId());
    }

    *dirtyBits = HdLight::Clean;
}

void HdArnoldGenericLight::SetupTexture(const VtValue& value)
{
    const auto* nentry = AiNodeGetNodeEntry(_light);
    const auto hasShader = AiNodeEntryLookUpParameter(nentry, str::shader) != nullptr;
    if (hasShader) {
        AiNodeSetPtr(_light, str::shader, nullptr);
    } else {
        AiNodeUnlink(_light, str::color);
    }
    if (_texture != nullptr) {
        AiNodeDestroy(_texture);
        _texture = nullptr;
    }
    if (!value.IsHolding<SdfAssetPath>()) {
        return;
    }
    const auto& assetPath = value.UncheckedGet<SdfAssetPath>();
    auto path = assetPath.GetResolvedPath();
    if (path.empty()) {
        path = assetPath.GetAssetPath();
    }

    if (path.empty()) {
        return;
    }
    _texture = AiNode(_delegate->GetUniverse(), str::image);
    AiNodeSetStr(_texture, str::filename, path.c_str());
    if (hasShader) {
        AiNodeSetPtr(_light, str::shader, _texture);
    } else { // Connect to color if filename doesn't exists.
        AiNodeLink(_texture, str::color, _light);
    }
}

HdDirtyBits HdArnoldGenericLight::GetInitialDirtyBitsMask() const
{
    return HdLight::DirtyParams | HdLight::DirtyTransform;
}

class HdArnoldSimpleLight : public HdLight {
public:
    /// Internal constructor for creating HdArnoldSimpleLight.
    ///
    /// @param delegate Pointer to the Render Delegate.
    /// @param id Path to the Hydra Primitive.
    HdArnoldSimpleLight(HdArnoldRenderDelegate* delegate, const SdfPath& id);

    /// Destructor for the Arnold Light.
    ///
    /// Destroys Arnold Light created.
    ~HdArnoldSimpleLight() override;

    /// Syncing parameters from the Hydra primitive to the Arnold light.
    ///
    /// Contrary to the HdArnoldGeneric light, the arnold node is only created
    /// in the sync function.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderParam Pointer to HdArnoldRenderParam.
    /// @param dirtyBits Dirty Bits of the Hydra primitive.
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;

    /// Returns the set of initial dirty bits.
    ///
    /// @return Value of the initial dirty bits.
    HdDirtyBits GetInitialDirtyBitsMask() const override;

private:
    HdArnoldRenderDelegate* _delegate; ///< Pointer to the Render Delegate.
    AtNode* _light;                    ///< Pointer to the Arnold Light.
};

HdArnoldSimpleLight::HdArnoldSimpleLight(HdArnoldRenderDelegate* delegate, const SdfPath& id) : HdLight(id) {}

HdArnoldSimpleLight::~HdArnoldSimpleLight()
{
    if (_light != nullptr) {
        AiNodeDestroy(_light);
    }
}

void HdArnoldSimpleLight::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    *dirtyBits = HdLight::Clean;
}

HdDirtyBits HdArnoldSimpleLight::GetInitialDirtyBitsMask() const
{
    return HdLight::DirtyTransform | HdLight::DirtyParams;
}

} // namespace

namespace HdArnoldLight {

HdLight* CreatePointLight(HdArnoldRenderDelegate* delegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(delegate, id, str::point_light, pointLightSync);
}

HdLight* CreateDistantLight(HdArnoldRenderDelegate* delegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(delegate, id, str::distant_light, distantLightSync);
}

HdLight* CreateDiskLight(HdArnoldRenderDelegate* delegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(delegate, id, str::disk_light, diskLightSync);
}

HdLight* CreateRectLight(HdArnoldRenderDelegate* delegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(delegate, id, str::quad_light, rectLightSync, true);
}

HdLight* CreateCylinderLight(HdArnoldRenderDelegate* delegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(delegate, id, str::cylinder_light, cylinderLightSync);
}

HdLight* CreateDomeLight(HdArnoldRenderDelegate* delegate, const SdfPath& id)
{
    return new HdArnoldGenericLight(delegate, id, str::skydome_light, domeLightSync, true);
}

HdLight* CreateSimpleLight(HdArnoldRenderDelegate* delegate, const SdfPath& id)
{
    return new HdArnoldSimpleLight(delegate, id);
}

} // namespace HdArnoldLight

PXR_NAMESPACE_CLOSE_SCOPE
