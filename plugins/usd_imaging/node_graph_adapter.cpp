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
#include "node_graph_adapter.h"

#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/materialSchema.h>
#include <pxr/usdImaging/usdImaging/indexProxy.h>
#include <pxr/imaging/hd/dirtyBitsTranslator.h>
#include "constant_strings.h"

#if PXR_VERSION >= 2108

#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/ar/resolverScopedCache.h>

#include "material_param_utils.h"

#endif

#ifdef ENABLE_SCENE_INDEX // Hydra 2
#include <pxr/imaging/hd/utils.h>
#include <pxr/usdImaging/usdImaging/dataSourceMaterial.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

// namespace {
// static bool
// _FindLocator(HdDataSourceLocator const& locator,
//              HdDataSourceLocatorSet::const_iterator const& end,
//              HdDataSourceLocatorSet::const_iterator *it,
//              const bool advanceToNext = true)
// {
//     if (*it == end) {
//         return false;
//     }

//     // The range between *it and end can be divided into:
//     // 1.) items < locator and not a prefix.
//     // 2.) items < locator and a prefix.
//     // 3.) locator
//     // 4.) items > locator and a suffix.
//     // 5.) items > locator and not a suffix.

//     // We want to return true if sets [2-4] are nonempty.
//     // If (advanceToNext) is true, we leave it pointing at the first element
//     // of 5; otherwise, we leave it pointing at the first element of [2-4].
//     bool found = false;
//     for (; (*it) != end; ++(*it)) {
//         if ((*it)->Intersects(locator)) {
//             found = true;
//             if (!advanceToNext) {
//                 break;
//             }
//         } else if (locator < (**it)) {
//             break;
//         }
//     }
//     return found;
// }
// void
// _ConvertLocatorSetToDirtyBitsForNodeGraph(
//     HdDataSourceLocatorSet const& set, HdDirtyBits *bitsOut)
// {
//     // if (set.Intersects(HdDataSourceLocator(_tokens->taco, _tokens->protein))) {
//     //     (*bits) |= DirtyProtein;
//     // }

//     // if (set.Intersects(HdDataSourceLocator(_tokens->taco, _tokens->tortilla))) {
//     //     (*bits) |= DirtyTortilla;
//     // }

//     // if (set.Intersects(HdDataSourceLocator(_tokens->taco, _tokens->salsa))) {
//     //     (*bits) |= DirtySalsa;
//     // }
//     HdDataSourceLocatorSet::const_iterator it = set.begin();

//     if (it == set.end()) {
//         *bitsOut = HdChangeTracker::Clean;
//         return;
//     }
//     HdDataSourceLocatorSet::const_iterator end = set.end();
//     HdDirtyBits bits = HdChangeTracker::Clean;

//     if (_FindLocator(HdMaterialSchema::GetDefaultLocator(), end, &it)) {
//         bits |= HdMaterial::DirtyParams | HdMaterial::DirtyResource;
//         for (const auto& locator : set) {
//             static const HdDataSourceLocator materialLocator(
//                 HdMaterialSchema::GetDefaultLocator());
//             if (locator == materialLocator) {
//                 bits |= HdMaterial::AllDirty;
//             } else {
//                 TfToken terminal = HdMaterialSchema::GetLocatorTerminal(
//                     locator, TfTokenVector());
//                 if (terminal == HdMaterialSchemaTokens->surface) {
//                     bits |= HdMaterial::DirtySurface;
//                 }
//                 else if (terminal == HdMaterialSchemaTokens->displacement) {
//                     bits |= HdMaterial::DirtyDisplacement;
//                 }
//                 else if (terminal == HdMaterialSchemaTokens->volume) {
//                     bits |= HdMaterial::DirtyVolume;
//                 } else {
//                     std::cout << "unknown terminal dirtied" << std::endl;
//                 }
//             }
//         }
//     }
//     *bitsOut = bits;
// }

// void
// _ConvertDirtyBitsToLocatorSetForNodeGraph(
//     const HdDirtyBits bits, HdDataSourceLocatorSet *set)
// {
//     if (bits & HdMaterial::AllDirty) {
//         set->append(HdMaterialSchema::GetDefaultLocator());
//     }
// }
// }

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = ArnoldNodeGraphAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();

    // HdDirtyBitsTranslator::RegisterTranslatorsForCustomSprimType(
    //     str::t_ArnoldNodeGraph, _ConvertLocatorSetToDirtyBitsForNodeGraph, _ConvertDirtyBitsToLocatorSetForNodeGraph);
}

#if PXR_VERSION >= 2108

SdfPath ArnoldNodeGraphAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    TF_UNUSED(instancerContext);
    index->InsertSprim(HdPrimTypeTokens->material, prim.GetPath(), prim);

    // Also register dependencies on behalf of any descendent
    // UsdShadeShader prims, since they are consumed to
    // create the node network.
    for (UsdPrim const& child: prim.GetDescendants()) {
        if (child.IsA<UsdShadeShader>()) {
            index->AddDependency(prim.GetPath(), child);
        }
    }

    return prim.GetPath();
}

void ArnoldNodeGraphAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    TF_UNUSED(instancerContext);
    TF_UNUSED(cachePath);
    // TODO: This is checking for the connected parameters on the primitive, which is not exactly what we want.
    // So it would be better to check all the terminals, check for their time variability.
    // if (UsdImagingArnoldIsHdMaterialNetworkTimeVarying(prim)) {
    //     *timeVaryingBits |= HdMaterial::DirtyResource;
    // }
}

VtValue ArnoldNodeGraphAdapter::GetMaterialResource(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time) const
{
    // To do correct asset resolution.
    ArResolverContextBinder binder(prim.GetStage()->GetPathResolverContext());
    ArResolverScopedCache resolverCache;

    // TODO: replicate the code found in pxr/usd/usdShade/material.cpp (ComputeNamedOutputSources),
    // since this is simplified.
    HdMaterialNetworkMap materialNetworkMap;
    UsdShadeConnectableAPI connectableAPI(prim);
    const auto outputs = connectableAPI.GetOutputs(true);
    for (auto output : outputs) {
        const auto sources = output.GetConnectedSources();
        if (sources.empty()) {
            continue;
        }
        UsdImagingArnoldBuildHdMaterialNetworkFromTerminal(
            sources[0].source.GetPrim(), output.GetBaseName(), TfTokenVector{}, TfTokenVector{}, &materialNetworkMap,
            time);
    }
    return VtValue{materialNetworkMap};
}

bool ArnoldNodeGraphAdapter::IsSupported(const UsdImagingIndexProxy* index) const
{
    // We limit the node graph adapter to the Arnold render delegate, and by default we are checking
    // for the support of "ArnoldUsd". Note, "ArnoldUsd" is an RPrim.
    return index->IsSprimTypeSupported(HdPrimTypeTokens->material) && index->IsRprimTypeSupported(str::t_ArnoldUsd);
}

#else

SdfPath ArnoldNodeGraphAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    TF_UNUSED(prim);
    TF_UNUSED(index);
    TF_UNUSED(instancerContext);
    return {};
}

void ArnoldNodeGraphAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    TF_UNUSED(prim);
    TF_UNUSED(cachePath);
    TF_UNUSED(timeVaryingBits);
    TF_UNUSED(instancerContext);
}

bool ArnoldNodeGraphAdapter::IsSupported(const UsdImagingIndexProxy* index) const
{
    TF_UNUSED(index);
    return false;
}

#endif

#ifdef ENABLE_SCENE_INDEX // Hydra 2

// Recusively check nodes starting at the terminal to find the dirty prim.
// If the dirty prim is the source material also check the specific dirty
// property.
bool
_IsArnoldConnectionDirty(
    const UsdPrim& dirtyPrim,
    const TfTokenVector& dirtyProperties,
    const UsdShadeMaterial& material,
    const UsdShadeConnectionSourceInfo& connection)
{
    if (!connection.IsValid())
        return false;

    // If we reach the root material only dirty if we are connected to the
    // specific property which is dirty and don't recurse further.
    if (connection.source.GetPrim() == material.GetPrim()) {
        if (connection.source.GetPrim() == dirtyPrim) {
            for (const TfToken& dirtyProperty : dirtyProperties) {
                if ((connection.sourceType == UsdShadeAttributeType::Output
                     && dirtyProperty
                         == connection.source.GetOutput(connection.sourceName)
                                .GetFullName())
                    || (connection.sourceType == UsdShadeAttributeType::Input
                        && dirtyProperty
                            == connection.source.GetInput(connection.sourceName)
                                   .GetFullName())) {
                    return true;
                }
            }
        }
        return false;
    }

    // We are connected to the dirty prim
    if (connection.source.GetPrim() == dirtyPrim) {
        return true;
    }

    // If the output we connected to had a direct connection check this.
    if (connection.sourceType == UsdShadeAttributeType::Output) {
        const UsdShadeOutput& output
            = connection.source.GetOutput(connection.sourceName);
        if (output) {
            for (UsdShadeConnectionSourceInfo& outputConnection :
                 output.GetConnectedSources()) {
                if (_IsArnoldConnectionDirty(
                        dirtyPrim, dirtyProperties, material,
                        outputConnection)) {
                    return true;
                }
            }
        }
    }

    // Check the input connections on the node.
    for (UsdShadeInput& input : connection.source.GetInputs()) {
        for (UsdShadeConnectionSourceInfo& inputConnection :
             input.GetConnectedSources()) {
            if (_IsArnoldConnectionDirty(
                    dirtyPrim, dirtyProperties, material, inputConnection)) {
                return true;
            }
        }
    }

    return false;
}

HdDataSourceLocator
_CreateArnoldTerminalLocator(const TfToken& output)
{
    const std::vector<std::string> baseNameComponents
        = SdfPath::TokenizeIdentifier(output);

    // If it's not namespaced use the universal token.
    if (baseNameComponents.size() == 1u) {
        return HdDataSourceLocator(
            HdMaterialSchema::GetSchemaToken(),
            HdMaterialSchemaTokens->universalRenderContext,
            HdMaterialSchemaTokens->terminals, TfToken(baseNameComponents[0])
        );
    }
    // If it's namespaced (eg. mtlx) include that.
    else if (baseNameComponents.size() > 1u) {
        return HdDataSourceLocator(
            HdMaterialSchema::GetSchemaToken(), TfToken(baseNameComponents[0]),
            HdMaterialSchemaTokens->terminals,
            TfToken(SdfPath::StripPrefixNamespace(output, baseNameComponents[0]).first)
        );
    }

    // Just point to the whole data source.
    return HdMaterialSchema::GetDefaultLocator();
}


TfTokenVector ArnoldNodeGraphAdapter::GetImagingSubprims(UsdPrim const& prim) {
    return { TfToken() };
}

TfToken ArnoldNodeGraphAdapter::GetImagingSubprimType(UsdPrim const& prim, TfToken const& subprim)
{
    if (subprim.IsEmpty()) {
        // we were previously returning HdPrimTypeTokens->material, now ArnoldNodeGraph use its own type token
        return str::t_ArnoldNodeGraph;
    }
    return TfToken();
}

class ArnoldNodeGraphDataSource : public HdContainerDataSource {
public:
    HD_DECLARE_DATASOURCE(ArnoldNodeGraphDataSource);

    USDIMAGINGARNOLD_API
    TfTokenVector GetNames() override
    {
        TfTokenVector renderContexts;
        // Always add the 'all' render context
        renderContexts.push_back(HdMaterialSchemaTokens->all);
        return renderContexts;
    }

    USDIMAGINGARNOLD_API
    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        // Same as GetResources from arnold
        ArResolverContextBinder binder(_usdPrim.GetStage()->GetPathResolverContext());
        ArResolverScopedCache resolverCache;

        HdMaterialNetworkMap materialNetworkMap;
        UsdShadeConnectableAPI connectableAPI(_usdPrim);
        const auto outputs = connectableAPI.GetOutputs(true);
        for (auto output : outputs) {
            const auto sources = output.GetConnectedSources();
            if (sources.empty()) {
                continue;
            }
            UsdImagingArnoldBuildHdMaterialNetworkFromTerminal(
                sources[0].source.GetPrim(), output.GetBaseName(), TfTokenVector{}, TfTokenVector{},
                &materialNetworkMap, _stageGlobals.GetTime());
        }

        return HdUtils::ConvertHdMaterialNetworkToHdMaterialNetworkSchema(materialNetworkMap);
    }

    ~ArnoldNodeGraphDataSource() override = default;

private:
    ArnoldNodeGraphDataSource(
        const UsdPrim& usdPrim, const UsdImagingDataSourceStageGlobals& stageGlobals)
        : _usdPrim(usdPrim), _stageGlobals(stageGlobals)
    {
        // TODO flags as time varying 
    }

private:
    const UsdPrim _usdPrim;
    const UsdImagingDataSourceStageGlobals& _stageGlobals;
};

HD_DECLARE_DATASOURCE_HANDLES(ArnoldNodeGraphDataSource);

class ArnoldNodeGraphDataSourcePrim : public UsdImagingDataSourcePrim {
public:
    HD_DECLARE_DATASOURCE(ArnoldNodeGraphDataSourcePrim);

    USDIMAGINGARNOLD_API
    TfTokenVector GetNames() override
    {
        TfTokenVector result = UsdImagingDataSourcePrim::GetNames();
        result.push_back(HdMaterialSchema::GetSchemaToken());
        return result;
    }

    USDIMAGINGARNOLD_API
    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HdMaterialSchema::GetSchemaToken()) {
            return ArnoldNodeGraphDataSource::New(_GetUsdPrim(), _GetStageGlobals());
        }
        return UsdImagingDataSourcePrim::Get(name);
    }

    ~ArnoldNodeGraphDataSourcePrim() override = default;

    static HdDataSourceLocatorSet Invalidate(
        const UsdPrim& prim, const TfToken& subprim, const TfTokenVector& properties,
        UsdImagingPropertyInvalidationType invalidationType)
    {
        HdDataSourceLocatorSet result =
            UsdImagingDataSourcePrim::Invalidate(prim, subprim, properties, invalidationType);

        if (subprim.IsEmpty()) {
            UsdShadeMaterial material(prim);
            if (material) {
                // Public interface values changes
                for (const TfToken& propertyName : properties) {
                    if (UsdShadeInput::IsInterfaceInputName(propertyName.GetString())) {
                        // TODO, invalidate specifically connected node parameters.
                        // FOR NOW: just dirty the whole material.
                        result.insert(HdMaterialSchema::GetDefaultLocator());
                        break;
                    }
                }
            }
        }
        return result;
    }

private:    
    ArnoldNodeGraphDataSourcePrim(
        const SdfPath& sceneIndexPath, const UsdPrim& usdPrim, const UsdImagingDataSourceStageGlobals& stageGlobals)
        : UsdImagingDataSourcePrim(sceneIndexPath, usdPrim, stageGlobals)
    {
    }
};

HD_DECLARE_DATASOURCE_HANDLES(ArnoldNodeGraphDataSourcePrim);

HdContainerDataSourceHandle ArnoldNodeGraphAdapter::GetImagingSubprimData(
    UsdPrim const& prim, TfToken const& subprim, const UsdImagingDataSourceStageGlobals& stageGlobals)
{
    if (subprim.IsEmpty()) {
        return ArnoldNodeGraphDataSourcePrim::New(
            prim.GetPath(),
            prim,
            stageGlobals);
    }

    return nullptr;
}

HdDataSourceLocatorSet
ArnoldNodeGraphAdapter::InvalidateImagingSubprimFromDescendent(
        UsdPrim const& prim,
        UsdPrim const& descendentPrim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        const UsdImagingPropertyInvalidationType invalidationType)
{
    HdDataSourceLocatorSet result;

    // Otherwise dirty our whole material
    if (result.IsEmpty()) {
        result.insert(HdMaterialSchema::GetDefaultLocator());
    }

    return result;
}


HdDataSourceLocatorSet ArnoldNodeGraphAdapter::InvalidateImagingSubprim(
    UsdPrim const& prim, TfToken const& subprim, TfTokenVector const& properties,
    UsdImagingPropertyInvalidationType invalidationType)
{
    HdDataSourceLocatorSet result;

    // Dirty our whole node graph
    if (result.IsEmpty() && subprim.IsEmpty()) {
        result.insert(ArnoldNodeGraphDataSourcePrim::Invalidate(
            prim, subprim, properties, invalidationType));
    }

    return result;
}
#endif // Hydra 2

void ArnoldNodeGraphAdapter::UpdateForTime(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}

HdDirtyBits ArnoldNodeGraphAdapter::ProcessPrimChange(
    UsdPrim const& prim, SdfPath const& cachePath, TfTokenVector const& changedFields)
{
    return HdChangeTracker::AllDirty;
}

HdDirtyBits ArnoldNodeGraphAdapter::ProcessPropertyChange(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName)
{
    if (propertyName == UsdGeomTokens->visibility) {
        // Materials aren't affected by visibility
        return HdChangeTracker::Clean;
    }

    // The only meaningful change is to dirty the computed resource,
    // an HdMaterialNetwork.
    return HdMaterial::DirtyResource;
}

void ArnoldNodeGraphAdapter::MarkDirty(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index)
{
    // If this is invoked on behalf of a Shader prim underneath a
    // Material prim, walk up to the enclosing Material.
    SdfPath arnoldNodeGraphCachePath = cachePath;
    UsdPrim arnoldNodeGraphPrim = prim;
    while (arnoldNodeGraphPrim && arnoldNodeGraphPrim.GetTypeName() != str::t_ArnoldNodeGraph) {
        arnoldNodeGraphPrim = arnoldNodeGraphPrim.GetParent();
        arnoldNodeGraphCachePath = arnoldNodeGraphCachePath.GetParentPath();
    }
    if (!TF_VERIFY(arnoldNodeGraphPrim)) {
        return;
    }

    index->MarkSprimDirty(arnoldNodeGraphCachePath, dirty);
}

void ArnoldNodeGraphAdapter::MarkMaterialDirty(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdImagingIndexProxy* index)
{
    MarkDirty(prim, cachePath, HdMaterial::DirtyResource, index);
}

void
ArnoldNodeGraphAdapter::ProcessPrimResync(
        SdfPath const& cachePath,
        UsdImagingIndexProxy *index)
{
    // Since we're resyncing a material, we can use the cache path as a
    // usd path.  We need to resync dependents to make sure rprims bound to
    // this material are resynced; this is necessary to make sure the material
    // is repopulated, since we don't directly populate materials.
#if PXR_VERSION >= 2108
    SdfPath const& usdPath = cachePath;
    _ResyncDependents(usdPath, index);
#endif

    UsdImagingPrimAdapter::ProcessPrimResync(cachePath, index);
}

void ArnoldNodeGraphAdapter::ProcessPrimRemoval(SdfPath const& cachePath, UsdImagingIndexProxy* index)
{
}

PXR_NAMESPACE_CLOSE_SCOPE
