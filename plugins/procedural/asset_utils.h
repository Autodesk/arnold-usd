//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <pxr/pxr.h>
#include <pxr/usd/usd/stage.h>
//#include <pxr/usd/usd/stageCache.h>
//#include <pxr/usd/usdUtils/stageCache.h>
#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

/**
 * Represents a dependency in a USD scene.
 */
struct USDDependency
{
    std::string type;          // "attr", "sublayer", "reference", "payload"
    std::string authoredPath;  // authored asset path (may be relative)
    std::string resolvedPath;  // absolute resolved filesystem path
    std::string layer;         // the layer where the dependency was authored
    std::string primPath;      // prim that introduced the dependency
    std::string attribute;     // name of the prim attribute that introduced the dependency

    USDDependency() = default;
    USDDependency(const std::string& p_type,
                  const std::string& p_authoredPath,
                  const std::string& p_resolvedPath,
                  const std::string& p_layer,
                  const std::string& p_primPath,
                  const std::string& p_attribute) : 
        type(p_type),
        authoredPath(p_authoredPath),
        resolvedPath(p_resolvedPath),
        layer(p_layer),
        primPath(p_primPath),
        attribute(p_attribute)
    {
    }

    inline AtFileType GetArnoldFileType() const
    {
        // We consider dependencies defined by prim attributes as 'Asset' type,
        // thus they can be resolved using the Arnold asset search path.
        //
        // TODO This is not entirely correct, because not all of these dependencies
        // might be translated as Arnold assets.
        // We would need to explicitly define which dependency is an Arnold asset.
        return type == "attr" ? AtFileType::Asset : AtFileType::Custom;
    }
};

/**
 * Returns all dependencies found in a USD scene.
 */
std::vector<USDDependency> CollectDependencies(UsdStageRefPtr stage);

/**
 * Returns all assets found in the given USD scene. 
 */
bool CollectSceneAssets(const std::string& filename, std::vector<AtAsset*>& assets, bool isProcedural);

PXR_NAMESPACE_CLOSE_SCOPE

