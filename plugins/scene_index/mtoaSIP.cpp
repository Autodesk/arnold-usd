//
// SPDX-License-Identifier: Apache-2.0
//
#include "mtoaSIP.h"

#if defined(ENABLE_SCENE_INDEX) && defined(MTOA_BUILD)

#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/dataSourceLocator.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>

#include <common_utils.h>

#include <cctype>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((sceneIndexPluginName, "HdArnoldMtoaSceneIndexPlugin")));

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
