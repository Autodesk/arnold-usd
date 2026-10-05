//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2026 Autodesk, Inc.
// Extent computation for the Arnold procedural schemas (ArnoldProcedural, ArnoldUsd).
//
// These prims are Boundable, but their geometry only exists once Arnold expands the procedural,
// so unless an extent is authored UsdGeomBBoxCache returns an empty bound for them. Registering a
// compute extent function lets any USD client (turntable, usdview, DCCs) get a bound, without
// requiring Arnold:
//   - USD files are opened and bounded with a UsdGeomBBoxCache,
//   - .ass files are bounded from the "### bounds:" header that Arnold writes.
// Other formats (e.g. alembic, compressed .ass) need Arnold to be expanded and are not handled.
//
// The schema plugInfo.json flags these types with "implementsComputeExtent", so that USD loads
// this library on demand the first time an extent is requested for one of them.

#include <pxr/pxr.h>
#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/timestamp.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/boundable.h>
#include <pxr/usd/usdGeom/boundableComputeExtent.h>
#include <pxr/usd/usdGeom/tokens.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((arnoldFilename, "arnold:filename"))
    (filename)
    ((arnoldObjectPath, "arnold:object_path"))
    ((arnoldFrame, "arnold:frame"))
);
// clang-format on

namespace {

// Bounds already computed for a given file, object path, time and file modification time.
// UsdGeomBBoxCache instances are short lived in most clients (usdview creates a new one on every
// time change), so without this cache the procedural files would be parsed again on every query.
std::mutex _boundsCacheMutex;
std::unordered_map<std::string, GfRange3d> _boundsCache;

// Stages opened by this library, mapped to the chain of files expanded to reach them. A procedural
// pointing to one of its ancestors' files would otherwise recurse forever. The chain is keyed by
// stage rather than kept per thread, as UsdGeomBBoxCache computes nested bounds on worker threads.
std::mutex _chainsMutex;
std::unordered_map<const UsdStage *, std::vector<std::string>> _chains;

// Returns the path of the file loaded by the procedural, resolved if possible.
std::string _GetProceduralFilename(const UsdPrim &prim, const UsdTimeCode &time)
{
    UsdAttribute attr = prim.GetAttribute(_tokens->arnoldFilename);
    // for backward compatibility, check the attribute without namespace
    if (!attr || !attr.HasAuthoredValue())
        attr = prim.GetAttribute(_tokens->filename);

    VtValue value;
    if (!attr || !attr.Get(&value, time))
        return {};

    if (value.IsHolding<SdfAssetPath>()) {
        const SdfAssetPath &assetPath = value.UncheckedGet<SdfAssetPath>();
        return assetPath.GetResolvedPath().empty() ? assetPath.GetAssetPath() : assetPath.GetResolvedPath();
    }
    if (!value.IsHolding<std::string>())
        return {};

    const std::string &filename = value.UncheckedGet<std::string>();
    if (filename.empty())
        return {};

    // String attributes (e.g. ArnoldUsd's filename) aren't resolved by USD,
    // so we anchor them to the strongest layer authoring the attribute.
    const SdfPropertySpecHandleVector specs = attr.GetPropertyStack(time);
    if (!specs.empty()) {
        const std::string resolved = SdfResolveAssetPathRelativeToLayer(specs.front()->GetLayer(), filename);
        if (!resolved.empty())
            return resolved;
    }
    return filename;
}

// Reads the bounds that Arnold writes in the header of .ass files, e.g.
// ### bounds: -1.000000 -1.000000 -1.000000 1.000000 1.000000 1.000000
bool _ReadAssBounds(const std::string &filename, GfRange3d &range)
{
    std::ifstream file(filename);
    if (!file)
        return false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        // The header is the block of comments at the top of the file
        if (line[0] != '#')
            break;
        double v[6];
        if (sscanf(line.c_str(), "### bounds: %lf %lf %lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) ==
            6) {
            range = GfRange3d(GfVec3d(v[0], v[1], v[2]), GfVec3d(v[3], v[4], v[5]));
            return !range.IsEmpty();
        }
    }
    return false;
}

std::vector<std::string> _GetChain(const UsdStage *stage)
{
    std::lock_guard<std::mutex> lock(_chainsMutex);
    const auto it = _chains.find(stage);
    return it == _chains.end() ? std::vector<std::string>() : it->second;
}

// Opens a USD file and computes the world bound of the prim at objectPath, or of the whole stage.
// Nested Arnold procedurals in this file will be bounded by this library as well.
bool _ComputeUsdFileBounds(
    const std::string &filename, const std::string &objectPath, const UsdTimeCode &time, const UsdStage *parentStage,
    GfRange3d &range)
{
    std::vector<std::string> chain = _GetChain(parentStage);
    if (std::find(chain.begin(), chain.end(), filename) != chain.end())
        return false; // cyclic procedural, this file is already being expanded
    chain.push_back(filename);

    UsdStageRefPtr stage = UsdStage::Open(filename, UsdStage::LoadAll);
    if (!stage)
        return false;

    UsdPrim root = stage->GetPseudoRoot();
    if (!objectPath.empty()) {
        if (!SdfPath::IsValidPathString(objectPath))
            return false;
        root = stage->GetPrimAtPath(SdfPath(objectPath));
        if (!root)
            return false;
    }

    const UsdStage *stagePtr = get_pointer(stage);
    {
        std::lock_guard<std::mutex> lock(_chainsMutex);
        _chains[stagePtr] = chain;
    }
    // The usd procedural renders the default and render purposes
    UsdGeomBBoxCache bboxCache(time, {UsdGeomTokens->default_, UsdGeomTokens->render}, /*useExtentsHint=*/true);
    range = bboxCache.ComputeWorldBound(root).ComputeAlignedRange();
    {
        std::lock_guard<std::mutex> lock(_chainsMutex);
        _chains.erase(stagePtr);
    }
    return !range.IsEmpty();
}

bool _ComputeProceduralExtent(
    const UsdGeomBoundable &boundable, const UsdTimeCode &time, const GfMatrix4d *transform, VtVec3fArray *extent)
{
    const UsdPrim prim = boundable.GetPrim();
    const std::string filename = _GetProceduralFilename(prim, time);
    if (filename.empty())
        return false;

    std::string objectPath;
    if (UsdAttribute attr = prim.GetAttribute(_tokens->arnoldObjectPath))
        attr.Get(&objectPath, time);

    // An authored frame overrides the time at which the procedural file is read
    UsdTimeCode fileTime = time;
    if (UsdAttribute attr = prim.GetAttribute(_tokens->arnoldFrame)) {
        float frame = 0.f;
        if (attr.HasAuthoredValue() && attr.Get(&frame, time))
            fileTime = UsdTimeCode(frame);
    }

    const ArTimestamp timestamp = ArGetResolver().GetModificationTimestamp(filename, ArResolvedPath(filename));
    const std::string cacheKey = TfStringPrintf(
        "%s|%s|%s|%.17g", filename.c_str(), objectPath.c_str(), TfStringify(fileTime).c_str(),
        timestamp.IsValid() ? timestamp.GetTime() : 0.0);

    GfRange3d range;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(_boundsCacheMutex);
        const auto it = _boundsCache.find(cacheKey);
        if (it != _boundsCache.end()) {
            range = it->second;
            found = true;
        }
    }

    if (!found) {
        // Don't hold the lock while computing, nested procedurals need it
        const std::string extension = TfStringToLower(TfGetExtension(filename));
        if (extension == "ass") {
            _ReadAssBounds(filename, range);
        } else if (UsdStage::IsSupportedFile(filename)) {
            _ComputeUsdFileBounds(filename, objectPath, fileTime, get_pointer(prim.GetStage()), range);
        }
        std::lock_guard<std::mutex> lock(_boundsCacheMutex);
        _boundsCache[cacheKey] = range;
    }

    if (range.IsEmpty())
        return false;

    if (transform)
        range = GfBBox3d(range, *transform).ComputeAlignedRange();

    extent->resize(2);
    (*extent)[0] = GfVec3f(range.GetMin());
    (*extent)[1] = GfVec3f(range.GetMax());
    return true;
}

} // namespace

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(UsdGeomBoundable)
{
    // The Arnold schemas are codeless, their types are only declared by the plugInfo.json
    for (const char *typeName : {"UsdArnoldProcedural", "UsdArnoldUsd"}) {
        const TfType type = PlugRegistry::FindTypeByName(typeName);
        if (!type.IsUnknown())
            UsdGeomRegisterComputeExtentFunction(type, _ComputeProceduralExtent);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
