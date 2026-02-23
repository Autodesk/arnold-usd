//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <ai.h>
#include <string>

// Asset API was added in Arnold 7.4.5.0
#if ARNOLD_VERSION_NUM >= 70405

PXR_NAMESPACE_OPEN_SCOPE

/**
 * Represents a dependency in a USD scene.
 */
struct USDDependency
{
    enum class Type
    {
        Unknown,
        Attribute,
        Sublayer,
        Reference,
        Payload,
    };

    Type type;                 // attribute, sublayer, reference, payload, etc.
    std::string authoredPath;  // authored asset path (may be relative)
    std::string resolvedPath;  // absolute resolved filesystem path
    SdfLayerRefPtr layer;      // the layer where the dependency was authored
    SdfPath primPath;          // prim that introduced the dependency
    TfToken primTypeName;      // prim type name
    SdfPath attribute;         // prim attribute that introduced the dependency

    USDDependency() = default;
    USDDependency(Type p_type,
                  const std::string& p_authoredPath,
                  const std::string& p_resolvedPath,
                  const SdfLayerRefPtr p_layer,
                  const SdfPath& p_primPath,
                  const TfToken& p_primTypeName,
                  const SdfPath& p_attribute) : 
        type(p_type),
        authoredPath(p_authoredPath),
        resolvedPath(p_resolvedPath),
        layer(p_layer),
        primPath(p_primPath),
        primTypeName(p_primTypeName),
        attribute(p_attribute)
    {
    }

    static std::string GetTypeName(USDDependency::Type type)
    {
        switch (type)
        {
            case USDDependency::Type::Attribute: return "attribute";
            case USDDependency::Type::Sublayer: return "sublayer";
            case USDDependency::Type::Reference: return "reference";
            case USDDependency::Type::Payload: return "payload";
            default: return "unknown";
        }
    }
};

/**
 * Returns all dependencies found in a USD scene.
 */
std::vector<USDDependency> CollectDependencies(UsdStageRefPtr stage);

/**
 * Returns all assets found in the given USD scene. 
 */
bool CollectSceneAssets(const std::string& filename, bool isProcedural, std::vector<AtAsset*>& assets);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // Asset API

