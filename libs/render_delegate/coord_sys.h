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
#pragma once

#include "api.h"

#include <ai.h>

#include <pxr/pxr.h>
#include <pxr/imaging/hd/coordSys.h>

#include "node_graph.h"
#include "render_delegate.h"
#include "render_param.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldCamera;

/// HdArnoldCoordSys represents a USD coordinate system binding as an Arnold
/// camera node whose name matches the coordinate system name (GetName()) and
/// whose transform tracks the bound prim's world transform.
///
/// Arnold's OSL render services resolve named coordinate spaces such as
/// "map_proj.camera", "map_proj.NDC", "map_proj.screen" and "map_proj.raster"
/// by looking up a *camera* node named after the space (the portion before the
/// suffix) via AiNodeLookUpByName. Representing the coordinate system as a
/// camera named after GetName() is therefore what makes those lookups resolve.
///
/// When the coordinate system is bound to a camera prim (the common case, e.g.
/// a projection camera) we also mirror that camera's frustum (fov, screen
/// window, clipping) so that the projective ".NDC"/".screen"/".raster" spaces
/// match the bound camera.
class HdArnoldCoordSys : public HdCoordSys {
public:
    HDARNOLD_API
    HdArnoldCoordSys(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

    HDARNOLD_API
    ~HdArnoldCoordSys() override;

    HDARNOLD_API
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;

    AtNode* GetArnoldNode() const { return _node; }

    /// This coordinate system's full world matrix (forward, local-to-world) and
    /// its inverse (world-to-local), carried by value rather than by an Arnold
    /// node. Unlike the camera node - whose scaling/shear Arnold strips at render
    /// time ("ignoring scaling component in camera matrix") - these carry the
    /// matrix verbatim, so affine coordinate spaces (".camera"/plain, and
    /// transform* node from/to spaces) resolve correctly for non-orthonormal
    /// frames. HdArnoldNodeGraph::RemapCoordSysSpaces copies one of these by
    /// value onto a matrix_multiply_vector helper's "matrix" input (see
    /// HdArnoldGetCoordSysBinding); the projective ".NDC"/".screen"/".raster"
    /// spaces keep using the camera node (GetArnoldNode), which is the only thing
    /// that can carry a frustum.
    ///
    /// By-value copy rather than an AiNodeLink onto a dedicated Arnold node is
    /// deliberate: Hydra makes no destruction-order guarantee between the
    /// coordSys and material Sprims, and a live Arnold node link crossing that
    /// boundary is unsafe to tear down in an unspecified order. Every other
    /// cross-Sprim reference in this file (the camera node names above, the
    /// camera_projection shader's target) is likewise by name/by value, never a
    /// live node link; keep new coordinate-space plumbing consistent with that.
    ///
    /// Returns nullptr until the first Sync has computed a matrix.
    const AtMatrix* GetForwardMatrix() const { return _hasMatrix ? &_fwdMatrix : nullptr; }
    const AtMatrix* GetInverseMatrix() const { return _hasMatrix ? &_invMatrix : nullptr; }

    /// The camera node dedicated to the ".NDC" space, or nullptr when the NDC
    /// correction is disabled. Arnold's NDC convention is Y-opposite to its
    /// screen/raster, so (when HDARNOLD_coordsys_flip_ndc_v is enabled) the ".NDC"
    /// space is resolved through this separately-flipped camera while
    /// ".camera"/".screen"/".raster" keep using GetArnoldNode(). Callers rewrite
    /// the ".NDC"-suffixed material "space" inputs to this node's name.
    AtNode* GetArnoldNdcNode() const { return _ndcNode; }

private:
    /// Returns the HdArnoldCamera the coordinate system is bound to, or nullptr
    /// when the bound prim is not a camera (or cannot be resolved).
    const HdArnoldCamera* _FindBoundCamera(HdSceneDelegate* sceneDelegate) const;

    /// Return an Arnold camera node named @p nodeName of the given @p cameraType
    /// (persp_camera / ortho_camera). Reuses @p existing when it already has that
    /// type; otherwise creates the node, and when @p existing is a node of a
    /// different type recreates it in place (redirecting references and destroying
    /// the old one) so a coordinate system bound to an orthographic camera resolves
    /// to an ortho_camera node. Interrupts the render only when it (re)creates.
    AtNode* _EnsureNode(
        HdArnoldRenderParamInterrupt& param, AtNode* existing, const AtString& cameraType,
        const std::string& nodeName);

    /// Mirror the resolved bound camera (matrix + frustum) into dst, optionally
    /// flipping the V axis, and register it for per-render aspect correction. The
    /// frustum is mirrored for both perspective and orthographic source cameras;
    /// dst must already be of the matching Arnold camera type (see _EnsureNode).
    void _MirrorCamera(AtNode* dst, AtNode* src, const HdArnoldCamera* boundCamera, bool flipV);

    /// Fall back to the coordinate system's own world transform (no frustum),
    /// optionally flipping the V axis. Used when no bound camera resolves.
    void _MirrorTransform(AtNode* dst, HdSceneDelegate* sceneDelegate, bool flipV);

    /// Store @p matrix (which retains scale/shear) and its inverse for
    /// GetForwardMatrix()/GetInverseMatrix() to hand out by value.
    void _UpdateMatrices(const AtMatrix& matrix);

    HdArnoldRenderDelegate* _renderDelegate;
    AtNode* _node = nullptr;
    AtNode* _ndcNode = nullptr;
    AtMatrix _fwdMatrix; ///< Local-to-world, scale/shear preserved.
    AtMatrix _invMatrix; ///< World-to-local, scale/shear preserved.
    bool _hasMatrix = false;
};

/// Build the rprim @p id's coordinate-system binding: the map from each bound
/// coordinate-system name to the uniquely-named Arnold camera node(s) of that
/// coordinate system, tagged with @p id as the owner.
///
/// Arnold resolves named coordinate spaces globally by camera node name, so a
/// material's "space" inputs must be rewritten to the cameras bound by the rprim
/// being shaded. This is the rprim side of that handshake: every rprim that
/// assigns a material should pass the result to the binding-aware
/// HdArnoldNodeGraph::GetCached*Shader accessors. Rprims must therefore also
/// re-assign their material on HdChangeTracker::DirtyCategories (which carries
/// coordinate-system binding changes) and include that bit in their initial
/// dirty bits. The owner is what lets the node graph release this rprim's claim on
/// a network variant when it re-binds.
///
/// Returns an empty map when @p id has no coordinate-system bindings - the common
/// case, which the accessors resolve to the unmodified base material.
HDARNOLD_API
HdArnoldNodeGraph::CoordSysBinding HdArnoldGetCoordSysBinding(HdSceneDelegate* sceneDelegate, const SdfPath& id);

PXR_NAMESPACE_CLOSE_SCOPE
