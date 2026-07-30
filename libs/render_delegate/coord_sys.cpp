// Copyright 2024 Autodesk, Inc.
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

#include "coord_sys.h"

#include "camera.h"
#include "config.h"
#include "render_param.h"
#include "utils.h"

#include <common_utils.h>
#include <constant_strings.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>

#include <algorithm>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// Build a unique, lookup-safe Arnold node name for a coordinate-system camera
// from its sprim id. Several prims may each bind a coordinate system of the
// *same* name (e.g. "map_proj") to *different* cameras; naming the node after
// the (unique) sprim id rather than the coordinate-system name keeps the Arnold
// camera nodes distinct. The id may carry property separators ('.'/':') which
// would confuse Arnold's "<name>.<suffix>" space parsing, so we replace them.
std::string _CoordSysCameraNodeName(const SdfPath& id)
{
    std::string name = id.GetString();
    std::replace_if(
        name.begin(), name.end(), [](char c) { return c == '.' || c == ':'; }, '_');
    return name;
}

// Flip the coordinate-system camera's up axis (row 1 of the camera-to-world
// matrix). This inverts camera-space Y, and because the projective spaces
// (.NDC/.screen/.raster) are derived from the camera, their V axis flips with
// it, so every named space flips consistently. Used to match Houdini/Karma's
// projection orientation when HDARNOLD_coordsys_flip_v is enabled.
void _FlipCoordSysMatrixV(AtNode* node)
{
    AtArray* matrix = AiNodeGetArray(node, str::matrix);
    if (matrix == nullptr)
        return;
    const uint32_t numKeys = AiArrayGetNumKeys(matrix);
    for (uint32_t key = 0; key < numKeys; ++key) {
        AtMatrix m = AiArrayGetMtx(matrix, key);
        m[1][0] = -m[1][0];
        m[1][1] = -m[1][1];
        m[1][2] = -m[1][2];
        m[1][3] = -m[1][3];
        AiArraySetMtx(matrix, key, m);
    }
}

} // namespace

HdArnoldCoordSys::HdArnoldCoordSys(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdCoordSys(id), _renderDelegate(renderDelegate)
{
    // The Arnold node is created lazily in the first Sync(), once GetName() is
    // known: Arnold resolves named coordinate spaces by looking up a camera
    // node whose name matches the coordinate system name, so the node's name
    // matters and is only reliably available after the name has been synced.
}

HdArnoldCoordSys::~HdArnoldCoordSys()
{
    for (AtNode* node : {_node, _ndcNode}) {
        if (node) {
            _renderDelegate->UnregisterCoordSysCamera(node);
            _renderDelegate->DestroyArnoldNode(node);
        }
    }
}

const HdArnoldCamera* HdArnoldCoordSys::_FindBoundCamera(HdSceneDelegate* sceneDelegate) const
{
    HdRenderIndex& renderIndex = sceneDelegate->GetRenderIndex();
    // When the coordinate system is populated through the scene index, the sprim
    // id identifies a coordSys prim parented under the bound prim itself. The
    // scene-delegate emulation reports it as a prim path with the property
    // separators rewritten, e.g. </grid/proj_cam/__coordSys_map_proj>, so the
    // bound camera is the *parent* path. Try both the prim path and its parent
    // and return the first that resolves to a camera.
    for (const SdfPath& path : {GetId().GetPrimPath(), GetId().GetParentPath()}) {
        const HdSprim* sprim = renderIndex.GetSprim(HdPrimTypeTokens->camera, path);
        if (const auto* camera = dynamic_cast<const HdArnoldCamera*>(sprim))
            return camera;
    }
    // When populated through the legacy delegate the sprim id is the binding
    // relationship path on the geometry (e.g. </grid.coordSys:map_proj:binding>),
    // which does not encode the bound camera path. The delegate does, however,
    // back the coordSys sprim with the *target* camera prim and reports that
    // camera's world transform for GetId(). We recover the camera by matching
    // that transform against the camera sprims, so we can mirror its frustum too:
    // the projective .NDC/.screen/.raster spaces need it (the plain .camera space
    // needs only the matrix, which the transform-only fallback already provided).
    const GfMatrix4d xf = sceneDelegate->GetTransform(GetId());
    const HdArnoldCamera* match = nullptr;
    for (const SdfPath& cameraId :
         renderIndex.GetSprimSubtree(HdPrimTypeTokens->camera, SdfPath::AbsoluteRootPath())) {
        const auto* camera =
            dynamic_cast<const HdArnoldCamera*>(renderIndex.GetSprim(HdPrimTypeTokens->camera, cameraId));
        if (camera == nullptr || !GfIsClose(sceneDelegate->GetTransform(cameraId), xf, AI_EPSILON))
            continue;
        // Two cameras sharing this transform would be ambiguous; fall back to the
        // transform-only node rather than mirror the wrong frustum.
        if (match != nullptr)
            return nullptr;
        match = camera;
    }
    return match;
}

AtNode* HdArnoldCoordSys::_EnsureNode(
    HdArnoldRenderParamInterrupt& param, AtNode* existing, const AtString& cameraType,
    const std::string& nodeName)
{
    if (existing != nullptr && AiNodeIs(existing, cameraType))
        return existing;
    param.Interrupt();
    if (existing != nullptr) {
        // The projection type changed (e.g. the first Sync defaulted to
        // perspective and the bound camera has now resolved to orthographic).
        // Recreate the node in place, following HdArnoldCamera::Sync: free the
        // name so it can be reused, redirect any references (the mesh coord_sys
        // user-data array holds the node pointer) with AiNodeReplace, then
        // destroy the old node. Unregister first so we do not leave a dangling
        // entry in the per-render aspect-correction map.
        _renderDelegate->UnregisterCoordSysCamera(existing);
        AiNodeSetStr(existing, str::name, AtString());
    }
    AtNode* node = _renderDelegate->CreateArnoldNode(cameraType, AtString(nodeName.c_str()));
    if (existing != nullptr) {
        AiNodeReplace(existing, node, false);
        _renderDelegate->DestroyArnoldNode(existing);
    }
    return node;
}

void HdArnoldCoordSys::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    // Save bits before the parent clears them.
    const HdDirtyBits bits = *dirtyBits;

    // Parent syncs the name and resets dirty bits to Clean.
    HdCoordSys::Sync(sceneDelegate, renderParam, dirtyBits);

    HdArnoldRenderParamInterrupt param(renderParam);

    const HdArnoldConfig& config = HdArnoldConfig::GetInstance();

    // Wait until the coordinate system name is known (the parent Sync sets it);
    // an empty name means the sprim is not fully populated yet. Arnold's OSL
    // render services resolve "<name>.camera/.NDC/.screen/.raster" by looking up
    // a camera node named after the space, so the node must carry that name.
    if (GetName().IsEmpty())
        return;

    // Resolve the bound camera first: its projection type decides whether the
    // coordinate-system node(s) must be a persp_camera or an ortho_camera.
    // Cameras sync before coordSys sprims (see _SupportedSprimTypes), so when the
    // coordinate system resolves to a camera its Arnold node is already configured.
    const HdArnoldCamera* boundCamera = _FindBoundCamera(sceneDelegate);
    AtNode* src = boundCamera ? boundCamera->GetCamera() : nullptr;

    // Match the coordinate-system node type to the bound camera. When no camera
    // resolves we keep perspective: the transform-only fallback carries no frustum
    // so the projection type is immaterial, and it matches the historical default.
    const AtString cameraType =
        (src != nullptr && AiNodeIs(src, str::ortho_camera)) ? str::ortho_camera : str::persp_camera;

    // Name the camera node uniquely from the sprim id rather than the coordinate-
    // system name. Arnold resolves named coordinate spaces globally by camera node
    // name (AiNodeLookUpByName), so several prims binding a coordinate system of
    // the same name to different cameras would otherwise collide on a single node.
    // Each rprim rewrites its material's "space" input to this unique node name
    // (see HdArnoldMesh and HdArnoldNodeGraph::RemapCoordSysSpaces).
    const std::string nodeName = _CoordSysCameraNodeName(GetId());
    _node = _EnsureNode(param, _node, cameraType, nodeName);
    // Arnold's NDC convention is Y-opposite to its screen/raster, so a single
    // camera cannot make all named spaces agree with the others (and Karma): any
    // flip that fixes NDC breaks the rest, and vice versa. When the NDC correction
    // is enabled we resolve the ".NDC" space through a *separate* camera node that
    // carries an extra V flip, leaving the other spaces on _node. The ".NDC"-
    // suffixed material inputs are routed here by name.
    if (config.coordsys_flip_ndc_v)
        _ndcNode = _EnsureNode(param, _ndcNode, cameraType, nodeName + "_ndc");

    // Base spaces (.camera/.NDC/.screen/.raster on _node) use coordsys_flip_v; the
    // dedicated NDC node flips once more (XOR) so its NDC is opposite the rest.
    const bool baseFlip = config.coordsys_flip_v;
    const bool ndcFlip = config.coordsys_flip_v != config.coordsys_flip_ndc_v;

    if (src != nullptr) {
        // Re-sync this coordinate system whenever the bound camera changes.
        // Hydra does not propagate a camera's dirtiness to the coordSys sprim, so
        // without this the mirrored frustum goes stale when the camera's aperture
        // or projection type (persp<->ortho) changes at runtime. HdArnoldCamera
        // dirties its dependents at the end of every Sync (DirtyDependency), so we
        // register the coordSys (source) as depending on the camera (target); the
        // DirtyTransform bit just forces our Sync to re-run and re-mirror.
        HdArnoldRenderDelegate::PathSetWithDirtyBits cameraDep;
        cameraDep.insert({boundCamera->GetId(), HdCoordSys::DirtyTransform});
        _renderDelegate->TrackDependencies(GetId(), cameraDep);

        // The coordinate system is bound to a camera we can resolve. Mirror that
        // camera into our node(s): copy its world matrix (which already folds in
        // the parent procedural matrix) and, for the projective spaces, its frustum.
        param.Interrupt();
        _MirrorCamera(_node, src, boundCamera, baseFlip);
        if (_ndcNode != nullptr)
            _MirrorCamera(_ndcNode, src, boundCamera, ndcFlip);
    } else if (bits & DirtyTransform) {
        // No resolvable camera (e.g. the legacy scene delegate exposes the
        // coordinate system on the geometry prim rather than the camera). Fall
        // back to the coordinate system's own world transform.
        param.Interrupt();
        _MirrorTransform(_node, sceneDelegate, baseFlip);
        if (_ndcNode != nullptr)
            _MirrorTransform(_ndcNode, sceneDelegate, ndcFlip);
    }
}

void HdArnoldCoordSys::_MirrorCamera(
    AtNode* dst, AtNode* src, const HdArnoldCamera* boundCamera, bool flipV)
{
    // Copying the matrix rather than reading the coordSys prim's own transform via
    // GetId() is deliberate: the scene-index population places the coordSys prim
    // under the camera prim (</cam.__coordSys:name>), so its transform gets the
    // camera transform applied a second time when flattened, whereas the legacy
    // scene delegate reports it directly. Mirroring the resolved camera node is
    // correct for both paths.
    if (AtArray* matrix = AiNodeGetArray(src, str::matrix)) {
        AiNodeSetArray(dst, str::matrix, AiArrayCopy(matrix));
        if (flipV)
            _FlipCoordSysMatrixV(dst);
    }
    AiNodeSetFlt(dst, str::near_clip, AiNodeGetFlt(src, str::near_clip));
    AiNodeSetFlt(dst, str::far_clip, AiNodeGetFlt(src, str::far_clip));

    const AtVector2 windowMin = AiNodeGetVec2(src, str::screen_window_min);
    const AtVector2 windowMax = AiNodeGetVec2(src, str::screen_window_max);
    const float yCenter = 0.5f * (windowMin.y + windowMax.y);

    // Arnold bakes the *render* frame aspect ratio into every camera's vertical
    // mapping (see AiWorldToScreenMatrix), which would tie the projection to the
    // render camera aspect / resolution. Karma instead uses the projector's own
    // aperture. We cancel Arnold's render aspect by driving the vertical screen
    // window as yHalf = frameAspect * ratio, where `ratio` is the projector's
    // native vertical extent. frameAspect is only known once the render resolution
    // is set, so we register the camera with `ratio` and let the render delegate
    // recompute the vertical window per render (UpdateCoordSysCameraProjections);
    // the value seeded here is just a resolution-independent baseline. In both
    // cases we keep the source horizontal window (any aperture offset) and the
    // vertical centre, and only the vertical half-extent is aspect-corrected.
    float ratio = 1.0f;
    if (AiNodeIs(src, str::ortho_camera)) {
        // Orthographic projector: the screen window is in world units and defines
        // the projected rectangle directly (no fov). The native vertical extent is
        // the source window's vertical half-extent; the horizontal extent is kept
        // as-is so the effective horizontal world extent stays constant while the
        // vertical is aspect-corrected to match it, exactly as for perspective.
        ratio = 0.5f * (windowMax.y - windowMin.y);
    } else if (AiNodeIs(src, str::persp_camera)) {
        // Perspective projector: the source window is fov-normalised (±1), so the
        // native vertical extent is the aperture ratio (verticalAperture /
        // horizontalAperture).
        AiNodeSetFlt(dst, str::fov, AiNodeGetFlt(src, str::fov));
        const float hAperture = boundCamera->GetHorizontalAperture();
        const float vAperture = boundCamera->GetVerticalAperture();
        ratio = (hAperture > AI_EPSILON) ? (vAperture / hAperture) : 1.0f;
    } else {
        // Unknown projection: matrix mirrored above, but we cannot mirror a frustum.
        return;
    }
    AiNodeSetVec2(dst, str::screen_window_min, windowMin.x, yCenter - ratio);
    AiNodeSetVec2(dst, str::screen_window_max, windowMax.x, yCenter + ratio);
    _renderDelegate->RegisterCoordSysCamera(dst, ratio);
}

HdArnoldNodeGraph::CoordSysRemap HdArnoldGetCoordSysRemap(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    HdArnoldNodeGraph::CoordSysRemap remap;
    const auto coordSysBindings = sceneDelegate->GetCoordSysBindings(id);
    if (!coordSysBindings)
        return remap;
    for (const SdfPath& coordSysId : *coordSysBindings) {
        // The coordSys sprims are synced before the rprims (see _SupportedSprimTypes),
        // so a bound coordinate system already has its Arnold camera node.
        const auto* coordSys = dynamic_cast<const HdArnoldCoordSys*>(
            sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->coordSys, coordSysId));
        if (coordSys == nullptr || coordSys->GetArnoldNode() == nullptr)
            continue;
        HdArnoldNodeGraph::CoordSysTarget target;
        target.node = AiNodeGetName(coordSys->GetArnoldNode());
        // Arnold's NDC is Y-opposite to its screen/raster; when the coordinate system
        // created a dedicated (extra-flipped) NDC camera, route the ".NDC" space to it.
        if (AtNode* ndcNode = coordSys->GetArnoldNdcNode())
            target.ndcNode = AiNodeGetName(ndcNode);
        remap[coordSys->GetName().GetString()] = std::move(target);
    }
    return remap;
}

void HdArnoldCoordSys::_MirrorTransform(AtNode* dst, HdSceneDelegate* sceneDelegate, bool flipV)
{
    HdArnoldSetTransform(dst, sceneDelegate, GetId());
    // Arnold does not apply the parent procedural matrix to cameras, so we fold it
    // in here, matching HdArnoldCamera.
    ArnoldUsdApplyParentMatrix(dst, _renderDelegate->GetProceduralParent());
    if (flipV)
        _FlipCoordSysMatrixV(dst);
}

PXR_NAMESPACE_CLOSE_SCOPE
