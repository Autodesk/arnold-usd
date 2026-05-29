//
// SPDX-License-Identifier: Apache-2.0
//
#include "mtoaSIP.h"

#if defined(ENABLE_SCENE_INDEX) && defined(MTOA_BUILD)

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/dataSourceLocator.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdLux/tokens.h>

#include <common_utils.h>

#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdArnoldMtoaSceneIndexPlugin"))
    (mayaCustomDagNode)
    (mayaNode)
    (mayaTypeName)
    (mayaAttributes)
    (aiPhotometricLight)
    (aiSkyDomeLight)
    (aiAreaLight)
    (aiStandIn)
    (aiVolume)
    ((ArnoldProcedural, "ArnoldProcedural"))
    ((ArnoldVolume, "ArnoldVolume"))
    ((arnoldFilename, "arnold:filename")));

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldMtoaSceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName, nullptr, insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

namespace {

// Matches an MtoA-style primvar name: starts with "ai" followed by an
// uppercase letter. Rejects "ai_foo", "ai2", "aifoo", etc.
inline bool _IsMtoaAiName(const TfToken& name)
{
    const std::string& s = name.GetString();
    return s.size() >= 3 && s[0] == 'a' && s[1] == 'i' && s[2] >= 'A' && s[2] <= 'Z';
}

// aiSubdivIterations -> arnold:subdiv_iterations
// (The "primvars:" scope is a USD-side namespace that UsdImaging strips when
// translating to Hydra, so the render delegate expects the plain "arnold:"
// prefix on primvars inside HdPrimvarsSchema.)
TfToken _RemapMtoaAiName(const TfToken& name)
{
    const std::string& s = name.GetString();
    std::string stripped = s.substr(2);
    return TfToken(std::string("arnold:") + ArnoldUsdMakeSnakeCase(stripped));
}

// Wraps an opaque VtValue as an HdSampledDataSource so it can be stored as a
// primvar value without double-wrapping (unlike HdRetainedTypedSampledDataSource<VtValue>).
class _VtValueDataSource : public HdSampledDataSource {
public:
    // HdSampledDataSourceHandle switched from TfRefPtr to std::shared_ptr in
    // USD 25.11.  Constructor is public so std::make_shared can reach it.
    _VtValueDataSource(VtValue v) : _value(std::move(v)) {}

    static HdSampledDataSourceHandle New(const VtValue& v)
    {
#if PXR_VERSION >= 2511
        return std::make_shared<_VtValueDataSource>(v);
#else
        return TfCreateRefPtr(new _VtValueDataSource(v));
#endif
    }
    bool GetContributingSampleTimesForInterval(float, float, std::vector<float>*) override { return false; }
    VtValue GetValue(float) override { return _value; }

private:
    VtValue _value;
};

// Builds a "primvars" container data source from all "ai<Upper>..." entries in
// mayaAttrs that are NOT listed in 'skip', remapping them via _RemapMtoaAiName.
// Returns nullptr when there are no matching entries.
HdDataSourceBaseHandle _BuildAiPrimvarsFromAttrs(
    const VtDictionary& mayaAttrs, const std::unordered_set<std::string>& skip)
{
    std::vector<TfToken> names;
    std::vector<HdDataSourceBaseHandle> sources;
    for (const auto& [key, value] : mayaAttrs) {
        if (skip.count(key))
            continue;
        const TfToken keyTok(key);
        if (!_IsMtoaAiName(keyTok))
            continue;
        names.push_back(_RemapMtoaAiName(keyTok));
        sources.push_back(
            HdPrimvarSchema::Builder()
                .SetPrimvarValue(_VtValueDataSource::New(value))
                .SetInterpolation(
                    HdPrimvarSchema::BuildInterpolationDataSource(HdPrimvarSchemaTokens->constant))
                .Build());
    }
    if (names.empty())
        return nullptr;
    return HdRetainedContainerDataSource::New(names.size(), names.data(), sources.data());
}

// Translates a mayaCustomDagNode of type aiPhotometricLight to a sphereLight
// prim with the standard UsdLux attribute mapping from the reference
// maya-hydra custom-node-translation scene index.  Any "ai<Upper>..." attrs
// not covered by the explicit mapping are passed through as primvars:arnold:*
// so the render delegate can forward them to Arnold.
HdSceneIndexPrim _TranslatePhotometricLight(
    const HdSceneIndexPrim& inputPrim, const VtDictionary& mayaAttrs)
{
    auto getAttr = [&](const char* key, auto def) {
        using T = decltype(def);
        auto it = mayaAttrs.find(key);
        if (it == mayaAttrs.end())
            return def;
        if (it->second.IsHolding<T>())
            return it->second.UncheckedGet<T>();
        if constexpr (std::is_same_v<T, float>) {
            if (it->second.IsHolding<double>())
                return static_cast<float>(it->second.UncheckedGet<double>());
        }
        return def;
    };

    const float       intensity   = getAttr("intensity",     1.0f);
    const GfVec3f     color       = getAttr("color",         GfVec3f(1.0f));
    const float       exposure    = getAttr("aiExposure",    0.0f);
    const std::string iesFile     = getAttr("aiFilename",    std::string());
    const float       radius      = getAttr("aiRadius",      0.0f);
    const bool        normalize   = getAttr("aiNormalize",   true);
    const float       diffuse     = getAttr("aiDiffuse",     1.0f);
    const float       specular    = getAttr("aiSpecular",    1.0f);
    const bool        castShadows = getAttr("aiCastShadows", true);
    const GfVec3f     shadowColor = getAttr("aiShadowColor", GfVec3f(0.0f));

    // Attributes consumed above — excluded from the catch-all ai primvar pass.
    static const std::unordered_set<std::string> s_explicit{
        "aiExposure", "aiFilename", "aiRadius",      "aiNormalize",
        "aiDiffuse",  "aiSpecular", "aiCastShadows", "aiShadowColor"};

    HdSceneIndexPrim result;
    result.primType = HdPrimTypeTokens->sphereLight;

    // Use the array form of HdRetainedContainerDataSource::New — the fixed-arity
    // overloads were capped at 3 pairs starting with USD 25.11.
    const TfToken lightDsNames[] = {
        UsdLuxTokens->inputsIntensity,
        UsdLuxTokens->inputsColor,
        UsdLuxTokens->inputsExposure,
        UsdLuxTokens->inputsShapingIesFile,
        UsdLuxTokens->inputsRadius,
        UsdLuxTokens->inputsNormalize,
        UsdLuxTokens->inputsDiffuse,
        UsdLuxTokens->inputsSpecular,
        UsdLuxTokens->inputsShadowEnable,
        UsdLuxTokens->inputsShadowColor,
    };
    const HdDataSourceBaseHandle lightDsSources[] = {
        HdRetainedTypedSampledDataSource<float>::New(intensity),
        HdRetainedTypedSampledDataSource<GfVec3f>::New(color),
        HdRetainedTypedSampledDataSource<float>::New(exposure),
        HdRetainedTypedSampledDataSource<SdfAssetPath>::New(SdfAssetPath(iesFile)),
        HdRetainedTypedSampledDataSource<float>::New(radius),
        HdRetainedTypedSampledDataSource<bool>::New(normalize),
        HdRetainedTypedSampledDataSource<float>::New(diffuse),
        HdRetainedTypedSampledDataSource<float>::New(specular),
        HdRetainedTypedSampledDataSource<bool>::New(castShadows),
        HdRetainedTypedSampledDataSource<GfVec3f>::New(shadowColor),
    };
    auto lightDs = HdRetainedContainerDataSource::New(
        std::size(lightDsNames), lightDsNames, lightDsSources);

    result.dataSource = HdOverlayContainerDataSource::New(lightDs, inputPrim.dataSource);

    if (auto aiPrimvarsDs = _BuildAiPrimvarsFromAttrs(mayaAttrs, s_explicit)) {
        result.dataSource = HdOverlayContainerDataSource::New(
            HdRetainedContainerDataSource::New(HdPrimvarsSchemaTokens->primvars, aiPrimvarsDs),
            result.dataSource);
    }

    return result;
}

HdSceneIndexPrim _TranslateSkyDomeLight(
    const HdSceneIndexPrim& inputPrim, const VtDictionary& mayaAttrs)
{
    auto getAttr = [&](const char* key, auto def) {
        using T = decltype(def);
        auto it = mayaAttrs.find(key);
        if (it == mayaAttrs.end())
            return def;
        if (it->second.IsHolding<T>())
            return it->second.UncheckedGet<T>();
        if constexpr (std::is_same_v<T, float>) {
            if (it->second.IsHolding<double>())
                return static_cast<float>(it->second.UncheckedGet<double>());
        }
        return def;
    };

    const float   intensity   = getAttr("intensity",     1.0f);
    const GfVec3f color       = getAttr("color",         GfVec3f(1.0f));
    const float   exposure    = getAttr("exposure",      0.0f);
    const bool    normalize   = getAttr("aiNormalize",   true);
    const float   diffuse     = getAttr("aiDiffuse",     1.0f);
    const float   specular    = getAttr("aiSpecular",    1.0f);
    const bool    castShadows = getAttr("aiCastShadows", true);
    const GfVec3f shadowColor = getAttr("aiShadowColor", GfVec3f(0.0f));

    static const std::unordered_set<std::string> s_explicit{
        "aiNormalize", "aiDiffuse", "aiSpecular", "aiCastShadows", "aiShadowColor"};

    HdSceneIndexPrim result;
    result.primType = HdPrimTypeTokens->domeLight;

    const TfToken domeLightDsNames[] = {
        UsdLuxTokens->inputsIntensity,
        UsdLuxTokens->inputsColor,
        UsdLuxTokens->inputsExposure,
        UsdLuxTokens->inputsNormalize,
        UsdLuxTokens->inputsDiffuse,
        UsdLuxTokens->inputsSpecular,
        UsdLuxTokens->inputsShadowEnable,
        UsdLuxTokens->inputsShadowColor,
    };
    const HdDataSourceBaseHandle domeLightDsSources[] = {
        HdRetainedTypedSampledDataSource<float>::New(intensity),
        HdRetainedTypedSampledDataSource<GfVec3f>::New(color),
        HdRetainedTypedSampledDataSource<float>::New(exposure),
        HdRetainedTypedSampledDataSource<bool>::New(normalize),
        HdRetainedTypedSampledDataSource<float>::New(diffuse),
        HdRetainedTypedSampledDataSource<float>::New(specular),
        HdRetainedTypedSampledDataSource<bool>::New(castShadows),
        HdRetainedTypedSampledDataSource<GfVec3f>::New(shadowColor),
    };
    auto lightDs = HdRetainedContainerDataSource::New(
        std::size(domeLightDsNames), domeLightDsNames, domeLightDsSources);

    result.dataSource = HdOverlayContainerDataSource::New(lightDs, inputPrim.dataSource);

    if (auto aiPrimvarsDs = _BuildAiPrimvarsFromAttrs(mayaAttrs, s_explicit)) {
        result.dataSource = HdOverlayContainerDataSource::New(
            HdRetainedContainerDataSource::New(HdPrimvarsSchemaTokens->primvars, aiPrimvarsDs),
            result.dataSource);
    }

    return result;
}

HdSceneIndexPrim _TranslateAreaLight(
    const HdSceneIndexPrim& inputPrim, const VtDictionary& mayaAttrs)
{
    auto getAttr = [&](const char* key, auto def) {
        using T = decltype(def);
        auto it = mayaAttrs.find(key);
        if (it == mayaAttrs.end())
            return def;
        if (it->second.IsHolding<T>())
            return it->second.UncheckedGet<T>();
        if constexpr (std::is_same_v<T, float>) {
            if (it->second.IsHolding<double>())
                return static_cast<float>(it->second.UncheckedGet<double>());
        }
        return def;
    };

    const std::string translator  = getAttr("aiTranslator", std::string());
    const float       intensity   = getAttr("intensity",     1.0f);
    const GfVec3f     color       = getAttr("color",         GfVec3f(1.0f));
    const float       exposure    = getAttr("exposure",      0.0f);
    const bool        normalize   = getAttr("aiNormalize",   true);
    const float       diffuse     = getAttr("aiDiffuse",     1.0f);
    const float       specular    = getAttr("aiSpecular",    1.0f);
    const bool        castShadows = getAttr("aiCastShadows", true);
    const GfVec3f     shadowColor = getAttr("aiShadowColor", GfVec3f(0.0f));

    // aiTranslator drives the prim type but is not an Arnold light parameter.
    static const std::unordered_set<std::string> s_explicit{
        "aiTranslator", "aiNormalize", "aiDiffuse", "aiSpecular", "aiCastShadows", "aiShadowColor"};

    HdSceneIndexPrim result;
    if (translator == "cylinder")
        result.primType = HdPrimTypeTokens->cylinderLight;
    else if (translator == "quad")
        result.primType = HdPrimTypeTokens->rectLight;
    else if (translator == "disk")
        result.primType = HdPrimTypeTokens->diskLight;
    else
        return inputPrim;

    const TfToken areaLightDsNames[] = {
        UsdLuxTokens->inputsIntensity,
        UsdLuxTokens->inputsColor,
        UsdLuxTokens->inputsExposure,
        UsdLuxTokens->inputsNormalize,
        UsdLuxTokens->inputsDiffuse,
        UsdLuxTokens->inputsSpecular,
        UsdLuxTokens->inputsShadowEnable,
        UsdLuxTokens->inputsShadowColor,
    };
    const HdDataSourceBaseHandle areaLightDsSources[] = {
        HdRetainedTypedSampledDataSource<float>::New(intensity),
        HdRetainedTypedSampledDataSource<GfVec3f>::New(color),
        HdRetainedTypedSampledDataSource<float>::New(exposure),
        HdRetainedTypedSampledDataSource<bool>::New(normalize),
        HdRetainedTypedSampledDataSource<float>::New(diffuse),
        HdRetainedTypedSampledDataSource<float>::New(specular),
        HdRetainedTypedSampledDataSource<bool>::New(castShadows),
        HdRetainedTypedSampledDataSource<GfVec3f>::New(shadowColor),
    };
    auto lightDs = HdRetainedContainerDataSource::New(
        std::size(areaLightDsNames), areaLightDsNames, areaLightDsSources);

    result.dataSource = HdOverlayContainerDataSource::New(lightDs, inputPrim.dataSource);

    if (auto aiPrimvarsDs = _BuildAiPrimvarsFromAttrs(mayaAttrs, s_explicit)) {
        result.dataSource = HdOverlayContainerDataSource::New(
            HdRetainedContainerDataSource::New(HdPrimvarsSchemaTokens->primvars, aiPrimvarsDs),
            result.dataSource);
    }

    return result;
}

HdSceneIndexPrim _TranslateStandIn(
    const HdSceneIndexPrim& inputPrim, const VtDictionary& mayaAttrs)
{
    std::string dso;
    auto it = mayaAttrs.find("dso");
    if (it != mayaAttrs.end() && it->second.IsHolding<std::string>())
        dso = it->second.UncheckedGet<std::string>();

    HdSceneIndexPrim result;
    result.primType = _tokens->ArnoldProcedural;
    result.dataSource = HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            _tokens->arnoldFilename, HdRetainedTypedSampledDataSource<std::string>::New(dso)),
        inputPrim.dataSource);
    return result;
}

HdSceneIndexPrim _TranslateVolume(
    const HdSceneIndexPrim& inputPrim, const VtDictionary& mayaAttrs)
{
    std::vector<TfToken> names;
    std::vector<HdDataSourceBaseHandle> sources;
    names.reserve(mayaAttrs.size());
    sources.reserve(mayaAttrs.size());
    for (const auto& [key, value] : mayaAttrs) {
        names.push_back(TfToken("arnold:" + key));
        sources.push_back(_VtValueDataSource::New(value));
    }

    HdSceneIndexPrim result;
    result.primType = _tokens->ArnoldVolume;
    result.dataSource = HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(names.size(), names.data(), sources.data()),
        inputPrim.dataSource);
    return result;
}

///////////////////////////////////////////////////////////////////////////////
// A data source wrapping a prim. When "primvars" is requested we overlay a
// rewritten primvars container; other fields pass through untouched.
///////////////////////////////////////////////////////////////////////////////
class _MtoaPrimvarRemapDataSource : public HdContainerDataSource {
public:
    HD_DECLARE_DATASOURCE(_MtoaPrimvarRemapDataSource);

    TfTokenVector GetNames() override { return _inputPrimDs ? _inputPrimDs->GetNames() : TfTokenVector{}; }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (!_inputPrimDs) {
            return nullptr;
        }
        HdDataSourceBaseHandle result = _inputPrimDs->Get(name);
        if (name == HdPrimvarsSchemaTokens->primvars) {
            if (HdPrimvarsSchema primvars = HdPrimvarsSchema::GetFromParent(_inputPrimDs)) {
                return _RemapPrimvars(primvars);
            }
        }
        return result;
    }

private:
    _MtoaPrimvarRemapDataSource(const HdContainerDataSourceHandle& inputDs) : _inputPrimDs(inputDs) {}

    HdDataSourceBaseHandle _RemapPrimvars(HdPrimvarsSchema primvars)
    {
        HdContainerDataSourceEditor editor(primvars.GetContainer());
        for (const TfToken& name : primvars.GetPrimvarNames()) {
            if (!_IsMtoaAiName(name)) {
                continue;
            }
            const TfToken remapped = _RemapMtoaAiName(name);
            // If a primvar already exists at the target name, leave it alone
            // rather than stomping on an authored value.
            if (primvars.GetPrimvar(remapped)) {
                continue;
            }
            HdPrimvarSchema src = primvars.GetPrimvar(name);
            editor.Overlay(
                HdDataSourceLocator(remapped),
                HdPrimvarSchema::Builder()
                    .SetPrimvarValue(src.GetPrimvarValue())
                    .SetInterpolation(src.GetInterpolation())
                    .SetRole(src.GetRole())
                    .Build());
            editor.Set(HdDataSourceLocator(name), HdBlockDataSource::New());
        }
        return editor.Finish();
    }

    HdContainerDataSourceHandle _inputPrimDs;
};

TF_DECLARE_REF_PTRS(_MtoaSceneIndex);

class _MtoaSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    static _MtoaSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr& inputSceneIndex)
    {
        return TfCreateRefPtr(new _MtoaSceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath& primPath) const override
    {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
        if (!prim.dataSource) {
            return prim;
        }
        // MtoA emits Maya DAG nodes with no built-in Hydra representation as
        // generic "mayaCustomDagNode" prims, tagging the Maya type name on a
        // nested "mayaNode/mayaTypeName" data source. Dispatch Arnold node
        // types ("ai<UpperCase>...") to their dedicated translators.
        if (prim.primType == _tokens->mayaCustomDagNode) {
            if (auto mayaNodeDs = HdContainerDataSource::Cast(prim.dataSource->Get(_tokens->mayaNode))) {
                if (auto typeNameDs =
                        HdTypedSampledDataSource<TfToken>::Cast(mayaNodeDs->Get(_tokens->mayaTypeName))) {
                    const TfToken mayaTypeName = typeNameDs->GetTypedValue(0.0f);
                    if (_IsMtoaAiName(mayaTypeName)) {
                        VtDictionary mayaAttrs;
                        if (auto attrsDs = HdTypedSampledDataSource<VtDictionary>::Cast(
                                mayaNodeDs->Get(_tokens->mayaAttributes))) {
                            mayaAttrs = attrsDs->GetTypedValue(0.0f);
                        }
                        if (mayaTypeName == _tokens->aiPhotometricLight) {
                            return _TranslatePhotometricLight(prim, mayaAttrs);
                        } else if (mayaTypeName == _tokens->aiSkyDomeLight) {
                            return _TranslateSkyDomeLight(prim, mayaAttrs);
                        } else if (mayaTypeName == _tokens->aiAreaLight) {
                            return _TranslateAreaLight(prim, mayaAttrs);
                        } else if (mayaTypeName == _tokens->aiStandIn) {
                            return _TranslateStandIn(prim, mayaAttrs);
                        } else if (mayaTypeName == _tokens->aiVolume) {
                            return _TranslateVolume(prim, mayaAttrs);
                        }
                    }
                }
            }
        }
        // Compose MtoA-compat data-source wrappers here. New fixups layer on
        // top by wrapping the previous result.
        HdContainerDataSourceHandle ds = _MtoaPrimvarRemapDataSource::New(prim.dataSource);
        return {prim.primType, ds};
    }

    SdfPathVector GetChildPrimPaths(const SdfPath& primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _MtoaSceneIndex(const HdSceneIndexBaseRefPtr& inputSceneIndex)
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
#if PXR_VERSION >= 2308
        SetDisplayName("Arnold: MtoA compatibility");
#endif
    }

    void _PrimsAdded(const HdSceneIndexBase&, const HdSceneIndexObserver::AddedPrimEntries& entries) override
    {
        if (_IsObserved()) {
            _SendPrimsAdded(entries);
        }
    }

    void _PrimsRemoved(const HdSceneIndexBase&, const HdSceneIndexObserver::RemovedPrimEntries& entries) override
    {
        if (_IsObserved()) {
            _SendPrimsRemoved(entries);
        }
    }

    void _PrimsDirtied(const HdSceneIndexBase&, const HdSceneIndexObserver::DirtiedPrimEntries& entries) override
    {
        if (_IsObserved()) {
            _SendPrimsDirtied(entries);
        }
    }
};

} // namespace

HdArnoldMtoaSceneIndexPlugin::HdArnoldMtoaSceneIndexPlugin() = default;
HdArnoldMtoaSceneIndexPlugin::~HdArnoldMtoaSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdArnoldMtoaSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputScene, const HdContainerDataSourceHandle& inputArgs)
{
    TF_UNUSED(inputArgs);
    return _MtoaSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX && MTOA_BUILD
