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

#include "read_skinning.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/trace/trace.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/work/loops.h"

#include "pxr/usd/sdf/abstractData.h"
#include "pxr/usd/sdf/attributeSpec.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/primSpec.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/boundable.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/modelAPI.h"
#include "pxr/usd/usdGeom/pointBased.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/xformCache.h"
#include "pxr/usd/usdSkel/animQuery.h"
#include "pxr/usd/usdSkel/binding.h"
#include "pxr/usd/usdSkel/bindingAPI.h"
#include "pxr/usd/usdSkel/blendShapeQuery.h"
#include "pxr/usd/usdSkel/cache.h"
#include "pxr/usd/usdSkel/debugCodes.h"
#include "pxr/usd/usdSkel/root.h"
#include "pxr/usd/usdSkel/skeletonQuery.h"
#include "pxr/usd/usdSkel/skinningQuery.h"
#include "pxr/usd/usdSkel/utils.h"

#include <algorithm>
#include <atomic>
#include <numeric>
#include <unordered_map>
#include <vector>
#include <iostream>


#include "reader.h"

PXR_NAMESPACE_USING_DIRECTIVE

using DeformationFlags = enum {
    DeformPointsWithLBS = 1 << 0,
    DeformNormalsWithLBS = 1 << 1,
    DeformXformWithLBS = 1 << 2,
    DeformPointsWithBlendShapes = 1 << 3,
    DeformNormalsWithBlendShapes = 1 << 4,
    DeformWithLBS = (DeformPointsWithLBS|
                        DeformNormalsWithLBS|
                        DeformXformWithLBS),
    DeformWithBlendShapes = (DeformPointsWithBlendShapes|
                                DeformNormalsWithBlendShapes),
    DeformAll = DeformWithLBS|DeformWithBlendShapes,
    /// Flags indicating which components of skinned prims may be
    /// modified, based on the active deformations.
    ModifiesPoints = DeformPointsWithLBS|DeformPointsWithBlendShapes,
    ModifiesNormals = DeformNormalsWithLBS|DeformNormalsWithBlendShapes,
    ModifiesXform = DeformXformWithLBS
};



// ------------------------------------------------------------
// _Task
// ------------------------------------------------------------


/// Helper for managing exec of a task over time.
/// This class only manages the state of the computation; The actual computation
/// and its results are maintained externally from this class.
struct _Task
{
    _Task() {
        Clear();
    }

    void Clear() {
        _active = false;
        _required = false;
        _mightBeTimeVarying = false;
        _isFirstSample = true;
        _hasSampleAtCurrentTime = false;
    }

    explicit operator bool() const {
        return _active && _required;
    }

    /// Returns true if a computation is active.
    /// An active computation does not necessarily need to run.
    bool IsActive() const {
        return _active;
    }

    /// Run \p fn at \p time, if necessary.
    template <class Fn>
    bool Run(const UsdTimeCode time,
             const UsdPrim& prim,
             const char* name,
             const Fn& fn) {

        if (!*this) {
            return false;
        }

        // Always compute for defaults.
        // For numeric times, if the task might be time varying, the task
        // is always computed. Otherwise, it is only computed the
        /// first time through.
        if (_mightBeTimeVarying || _isFirstSample || time.IsDefault()) {
                
            _hasSampleAtCurrentTime = fn(time);

            if (time.IsNumeric()) {
                _isFirstSample = false;
            }
        } 
        return _hasSampleAtCurrentTime;
    }

    /// Returns true if the task was successfully processed to update
    /// some cached value.
    /// The actual cached value is held externally.
    bool HasSampleAtCurrentTime() const {
        return _hasSampleAtCurrentTime;
    }

    /// Set a flag indicating that the computation is needed by something.
    void SetRequired(bool required) {
        _required = required;
    }

    /// Set the active status of the computation.
    /// The active status indicates whether or not a computation can be run.
    void SetActive(bool active, bool required=true) {
        _active = active;
        _required = required;
    }

    /// Returns true if the result of this task might vary over time.
    bool MightBeTimeVarying() const {
        return _mightBeTimeVarying;
    }

    /// Set a flag indicating whether or not the result of a computation
    /// *might* vary over time.
    void SetMightBeTimeVarying(bool tf) {
        _mightBeTimeVarying = tf;
    }

    std::string GetDescription() const {
        return TfStringPrintf(
            "active: %d, required: %d, mightBeTimeVarying: %d",
            _active, _required, _mightBeTimeVarying);
    }

private:
    bool _active : 1;
    bool _required : 1;
    bool _mightBeTimeVarying : 1;
    bool _isFirstSample : 1;
    bool _hasSampleAtCurrentTime : 1;
};


// ------------------------------------------------------------
// _OutputHolder
// ------------------------------------------------------------


/// Helper for holding a pending output value.
template <class T>
struct _OutputHolder
{
    void BeginUpdate() {
        hasSampleAtCurrentTime = false;
    }

    T value;
    bool hasSampleAtCurrentTime;
};


static UsdGeomXformCache *_FindXformCache(UsdArnoldReaderContext &context, 
                                    double time,
                                    UsdGeomXformCache &localCache)
{
    // Get the current xform cache, from the reader context. 
    // If there's no thread dispatcher, it is thread safe to use it as-is
    UsdGeomXformCache *xfCache = context.GetXformCache(time);
    if (!context.GetReader()->GetDispatcher())
        return xfCache;

    // Here we have a thread dispatcher and the xform cache isn't 
    // thread-safe. We want to copy it into the local xform cache.
    // If no xfCache was returned we want to create a new one for this time
    localCache = (xfCache) ? UsdGeomXformCache(*xfCache) : 
                             UsdGeomXformCache(time);
    return &localCache;
}
// ------------------------------------------------------------
// _SkelAdapter
// ------------------------------------------------------------


/// Object which interfaces with USD to pull on skel animation data,
/// and cache data where appropriate.
/// This augments a UsdSkelSkeletonQuery to perform additional caching
/// based on variability.
///
/// The execution procedure for a skel adapter may be summarized as:
/// \code
///     UsdGeomXformCache xfCache;
///     for (i,time : times) {
///         xfCache.SetTime(time);
///         skelAdapter.UpdateTransform(i, &xfCache);
///         skelAdapter.UpdateAnimation(time);
///         ...
///         // Apply skinning.
///     }
/// \endcode
///
/// The per-frame update is split into separate calls for the sake of threading:
/// UsdGeomXformCache is not thread-safe, and so the update step that uses an
/// xform cache must be done in serial, whereas UpdateAnimation() may be safely
/// called on different skel adapters in parallel.
struct _SkelAdapter
{
   _SkelAdapter(const UsdSkelSkeletonQuery& skelQuery,
                UsdGeomXformCache* xfCache, const UsdPrim &origin);

    UsdPrim GetPrim() const {
        return _skelQuery.GetPrim();
    }

    /// Append additional time samples of the skel to \p times.
    void ExtendTimeSamples(const GfInterval& interval,
                           std::vector<double>* times);

    /// Use \p xfCache to update any transforms required for skinning.
    void UpdateTransform(const size_t timeIndex, UsdGeomXformCache* xfCache);

    /// Update any animation data needed for skinning.
    void UpdateAnimation(const UsdTimeCode time, const size_t timeIndex);

    bool GetSkinningTransforms(VtMatrix4dArray* xforms) const {
        if (_skinningXformsTask.HasSampleAtCurrentTime()) {
            *xforms = _skinningXforms;
            return true;
        }
        return false;
    }

    bool GetSkinningInvTransposeTransforms(VtMatrix3dArray* xforms) const {
        if (_skinningInvTransposeXformsTask.HasSampleAtCurrentTime()) {
            *xforms = _skinningInvTransposeXforms;
            return true;
        }
        return false;
    }

    bool GetBlendShapeWeights(VtFloatArray* weights) const {
        if (_blendShapeWeightsTask.HasSampleAtCurrentTime()) {
            *weights = _blendShapeWeights;
            return true;
        }
        return false;
    }

    bool GetLocalToWorldTransform(GfMatrix4d* xf) const {
        if (_skelLocalToWorldXformTask.HasSampleAtCurrentTime()) {
            *xf = _skelLocalToWorldXform;
            return true;
        }
        return false;
    }

    bool CanComputeSkinningXforms() const {
        return _skinningXformsTask.IsActive();
    }

    void SetSkinningXformsRequired(bool required) {
        _skinningXformsTask.SetRequired(required);
    }

    bool CanComputeSkinningInvTransposeXforms() const {
        return _skinningInvTransposeXformsTask.IsActive();
    }

    void SetSkinningInvTransposeXformsRequired(bool required) {
        _skinningInvTransposeXformsTask.SetRequired(required);
    }

    bool CanComputeBlendShapeWeights() const {
        return _blendShapeWeightsTask.IsActive();
    }

    void SetBlendShapeWeightsRequired(bool required) {
        _blendShapeWeightsTask.SetRequired(required);
    }

    void SetLocalToWorldXformRequired(bool required) {
        _skelLocalToWorldXformTask.SetRequired(required);
    }

    bool HasTasksToRun() const {
        return _skinningXformsTask ||
               _skinningInvTransposeXformsTask ||
               _blendShapeWeightsTask ||
               _skelLocalToWorldXformTask;
    }

private:

    void _ComputeSkinningXforms(const UsdTimeCode time);

    void _ComputeSkinningInvTransposeXforms(const UsdTimeCode time);

    void _ComputeBlendShapeWeights(const UsdTimeCode time);

private:
    UsdSkelSkeletonQuery _skelQuery;

    /// Skinning transforms. Used for LBS xform and point skinning.
    _Task _skinningXformsTask;
    VtMatrix4dArray _skinningXforms;

    /// Inverse tranpose of skinning transforms,
    /// Used for LBS normal skinning.
    _Task _skinningInvTransposeXformsTask;
    VtMatrix3dArray _skinningInvTransposeXforms;

    /// Blend shape weight animation.
    _Task _blendShapeWeightsTask;
    VtFloatArray _blendShapeWeights;

    /// Skel local to world xform. Used for LBS xform and point skinning.
    _Task _skelLocalToWorldXformTask;
    GfMatrix4d _skelLocalToWorldXform;

    /// Origin prim, this saves the instance location
    UsdPrim _origin;
};


using _SkelAdapterRefPtr = std::shared_ptr<_SkelAdapter>;




bool
_WorldTransformMightBeTimeVarying(const UsdPrim& prim,
                                  UsdGeomXformCache* xformCache)
{   
    // If the prim is in a prototype, we don't really know if the final world transform will be
    // time varying, so we have to return true. 
    if (prim.IsInPrototype()) return true;
    for (UsdPrim p = prim; !p.IsPseudoRoot(); p = p.GetParent()) {
        if (xformCache->TransformMightBeTimeVarying(p)) {
            return true;
        }
        if (xformCache->GetResetXformStack(p)) {
            break;
        }
    }
    return false;
}

// We don't want to only use time samples included in a given interval, 
// so we can't rely on USD builtin functions (e.g. GetTimeSamplesInInterval, etc..)
// If an attribute has time sample outside of the interval bounds, we want to consider 
// these interval bounds in our evaluation. Otherwise an animated attribute will show as static
void _InsertTimesInInterval(const GfInterval &interval, 
    std::vector<double>& allTimes, std::vector<double>* outTimes)
{
    if (!outTimes) return;
    if (allTimes.empty()) return;

    const double minTime = interval.GetMin();
    const double maxTime = interval.GetMax();

    outTimes->reserve(outTimes->size() + allTimes.size());

    bool minFound = false;
    bool maxFound = false;
    for  (size_t i = 0; i < allTimes.size(); ++i) {
        double val = allTimes[i];
        if (val <= minTime) {
            if (!minFound) {
                outTimes->push_back(minTime);
                minFound = true;
            }
        } else if (val >= maxTime) {
            if (!maxFound) {
                outTimes->push_back(maxTime);
                maxFound = true;
            }
            // if allTimes is sorted we can break here
        } else {
            outTimes->push_back(val);
        }
    }

}

void
_ExtendWorldTransformTimeSamples(const UsdPrim& prim,
                                 const GfInterval& interval,
                                 std::vector<double>* times)
{
    std::vector<double> tmpTimes;

    for (UsdPrim p = prim; !p.IsPseudoRoot(); p = p.GetParent()) {
        if (p.IsA<UsdGeomXformable>()) {
            const UsdGeomXformable xformable(prim);
            const UsdGeomXformable::XformQuery query(xformable);
            if (query.GetTimeSamples(&tmpTimes)) {
                _InsertTimesInInterval(interval, tmpTimes, times);
            }
            if (query.GetResetXformStack()) {
                break;
            }
        }
    }
}


_SkelAdapter::_SkelAdapter(const UsdSkelSkeletonQuery& skelQuery,
                           UsdGeomXformCache* xformCache, const UsdPrim &origin)
    : _skelQuery(skelQuery), _origin(origin)
{
    if (!TF_VERIFY(_skelQuery)) {
        return;
    }
    
    // Activate skinning transform computations if we have a mappable anim,
    // or if restTransforms are authored as a fallback.

    if (const UsdSkelSkeleton& skel = skelQuery.GetSkeleton()) {
        const auto& animQuery = skelQuery.GetAnimQuery();
        if ((animQuery && !skelQuery.GetMapper().IsNull()) ||
            skel.GetRestTransformsAttr().HasAuthoredValue()) {

            // XXX: Activate computations, but tag them as not required;
            // skinning adapters will tag them as required if needed.
            _skinningXformsTask.SetActive(true, /*required*/ false);
            _skinningInvTransposeXformsTask.SetActive(
                true, /*required*/ false);

            // The animQuery object may not be valid if the skeleton has a
            // rest transform attribute.
            if (animQuery && animQuery.JointTransformsMightBeTimeVarying()) {
                _skinningXformsTask.SetMightBeTimeVarying(true);
                _skinningInvTransposeXformsTask.SetMightBeTimeVarying(true);
            }
            else {
                _skinningXformsTask.SetMightBeTimeVarying(false);
                _skinningInvTransposeXformsTask.SetMightBeTimeVarying(false);
            }

            // Also active computation for skel's local to world transform.
            _skelLocalToWorldXformTask.SetActive(true, /*required*/ false);
            _skelLocalToWorldXformTask.SetMightBeTimeVarying(
                _WorldTransformMightBeTimeVarying(
                    skel.GetPrim(), xformCache));
        }
    }


    // Activate blend shape weight computations if we have authored
    // blend shape anim.
    if (const UsdSkelAnimQuery& animQuery = skelQuery.GetAnimQuery()) {
        // Determine if blend shapes are authored at all.
        std::vector<UsdAttribute> weightAttrs;
        if (animQuery.GetBlendShapeWeightAttributes(&weightAttrs)) {
            _blendShapeWeightsTask.SetActive(
                std::any_of(weightAttrs.begin(), weightAttrs.end(),
                            [](const UsdAttribute& attr)
                            { return attr.HasAuthoredValue(); }),
                /*required*/ false);
            _blendShapeWeightsTask.SetMightBeTimeVarying(
                animQuery.BlendShapeWeightsMightBeTimeVarying());
        }
    }
    
}


void
_SkelAdapter::ExtendTimeSamples(const GfInterval& interval,
                                std::vector<double>* times)
{
    std::vector<double> tmpTimes;
    if (_skinningXformsTask) {
        if (const auto& animQuery = _skelQuery.GetAnimQuery()) {
            if (animQuery.GetJointTransformTimeSamples(&tmpTimes)) {
                _InsertTimesInInterval(interval, tmpTimes, times);
            }
        }
    }
    if (_blendShapeWeightsTask) {
        if (const auto& animQuery = _skelQuery.GetAnimQuery()) {
            if (animQuery.GetBlendShapeWeightTimeSamples(&tmpTimes)) {
                _InsertTimesInInterval(interval, tmpTimes, times);
            }
        }
    }
    if (_skelLocalToWorldXformTask) {
        _ExtendWorldTransformTimeSamples(GetPrim(), interval, times);
    }
}


inline bool IsInPrototype(const UsdPrim &prim) {
    return prim.IsInPrototype();
}

void
_SkelAdapter::UpdateTransform(const size_t timeIndex,
                              UsdGeomXformCache* xfCache)
{
    // The original code was only updating the transforms if there 
    // were keys in this specific time, but here we need to sample
    // all the required times to fill the arnold AtArrays. 
    //if (ShouldProcessAtTime(timeIndex))
    {

        _skelLocalToWorldXformTask.Run(
            xfCache->GetTime(), GetPrim(), "compute skel local to world xform",
            [&](UsdTimeCode time) {
                const UsdPrim &destPrim = IsInPrototype(_skelQuery.GetPrim()) ? _origin : _skelQuery.GetPrim();
                _skelLocalToWorldXform =
                    xfCache->GetLocalToWorldTransform(destPrim);
                return true;
            });
    }
}


void
_SkelAdapter::_ComputeSkinningXforms(const UsdTimeCode time)
{
    _skinningXformsTask.Run(
        time, GetPrim(), "compute skinning xforms",
        [&](UsdTimeCode time) {
            return _skelQuery.ComputeSkinningTransforms(&_skinningXforms, time);
        });
}


void
_SkelAdapter::_ComputeSkinningInvTransposeXforms(const UsdTimeCode time)
{
    if (_skinningXformsTask.HasSampleAtCurrentTime()) {
        _skinningInvTransposeXformsTask.Run(
            time, GetPrim(), "compute skinning inverse transpose xforms",
            [&](UsdTimeCode time) {
                _skinningInvTransposeXforms.resize(_skinningXforms.size());
                const auto skinningXforms = TfMakeConstSpan(_skinningXforms);
                const auto dst = TfMakeSpan(_skinningInvTransposeXforms);
                for (size_t i = 0;i < dst.size(); ++i) {
                    dst[i] = skinningXforms[i].ExtractRotationMatrix()
                        .GetInverse().GetTranspose();
                }
                return true;
            });
    }
}


void
_SkelAdapter::_ComputeBlendShapeWeights(const UsdTimeCode time)
{
    _blendShapeWeightsTask.Run(
        time, GetPrim(), "compute blend shape weights",
        [&](UsdTimeCode time) {
            return _skelQuery.GetAnimQuery().ComputeBlendShapeWeights(
                &_blendShapeWeights, time);
        });
}


void
_SkelAdapter::UpdateAnimation(const UsdTimeCode time, const size_t timeIndex)
{
    // The original code was only updating the animation if there 
    // were keys in this specific time, but here we need to sample
    // all the required times to fill the arnold AtArrays. 
    // if (ShouldProcessAtTime(timeIndex))
    {

        _ComputeSkinningXforms(time);
        _ComputeSkinningInvTransposeXforms(time);
        _ComputeBlendShapeWeights(time);
    }
}


// ------------------------------------------------------------
// _SkinningAdapter
// ------------------------------------------------------------


/// Object used to store the output of skinning.
/// This object is bound to a single skinnable primitive, and manages
/// both intermediate computations, as well as authoring of final values.
///
/// The overall skinning procedure for a single prim may be summarized as:
/// \code
///     for (time : times) {
///         adapter.Update(time);
///         adapter.Write();
///     }
/// \endcode
///
/// The procedure is split into two calls for the sake of threading:
/// The Update() step may be safely called for different adapters in
/// parallel, whereas writes for each layer must be called in serial.
struct _SkinningAdapter
{
    /// Flags indicating which deformation paths are active.
    enum ComputationFlags {
        RequiresSkinningXforms =
            DeformWithLBS,
        RequiresSkinningInvTransposeXforms =
            DeformNormalsWithLBS,
        RequiresBlendShapeWeights = 
            DeformWithBlendShapes,
        RequiresGeomBindXform =
            DeformWithLBS,
        RequiresGeomBindInvTransposeXform =
            DeformNormalsWithLBS,
        RequiresJointInfluences =
            DeformWithLBS,
        RequiresSkelLocalToWorldXform =
            DeformWithLBS,
        RequiresPrimLocalToWorldXform =
            (DeformPointsWithLBS|
             DeformNormalsWithLBS),
        RequiresPrimParentToWorldXform =
            DeformXformWithLBS
    };

    _SkinningAdapter(const UsdSkelSkinningQuery& skinningQuery,
                     const _SkelAdapterRefPtr& skelAdapter,
                     UsdGeomXformCache* xformCache);

    /// Returns the skel adapter that manages skel animation associated with
    /// this adapter.
    const _SkelAdapterRefPtr& GetSkelAdapter() const {
        return _skelAdapter;
    }

    const UsdPrim& GetPrim() const {
        return _skinningQuery.GetPrim();
    }

    /// Append additional time samples of the skel to \p times.
    void ExtendTimeSamples(const GfInterval& interval,
                           std::vector<double>* times);

    /// Use \p xfCache to update cached transform data at the \p timeIndex'th
    /// time sample. Cached values are stored only if necessary.
    void UpdateTransform(const size_t timeIndex, UsdGeomXformCache* xfCache);

    void Update(const UsdTimeCode time, const size_t timeIndex);


    bool HasTasksToRun() const { return _flags; }

    /// Returns true if the extent of the skinned prim must be updated
    /// separately, after skinning is completed.
    bool RequiresPostExtentUpdate() const {
        return false;
        //return _flags & ModifiesPoints
        //    && !_extentWriter;
    }

    bool GetPoints(VtVec3fArray &points, size_t timeIndex) const {
        points = _points.value;
        return true;
    }

    bool GetNormals(VtVec3fArray &normals, size_t timeIndex) const {
        normals = _normals.value;
        return true;
    }

    bool GetXform(GfMatrix4d &xform, size_t timeIndex) const {
        if (_xform.hasSampleAtCurrentTime) {
            xform = _xform.value;
            return true;
        }
        return false;
    }

    bool HasFlags(int flags) const {
        return flags & _flags;
    }

private:

    bool _ComputeRestPoints(const UsdTimeCode time);
    bool _ComputeRestNormals(const UsdTimeCode time);
    bool _ComputeGeomBindXform(const UsdTimeCode time);
    bool _ComputeJointInfluences(const UsdTimeCode time);

    void _DeformWithBlendShapes();

    void _DeformWithLBS(const UsdTimeCode time, const size_t timeIndex);

    void _DeformPointsWithLBS(const GfMatrix4d& skelToGprimXform);

    void _DeformNormalsWithLBS(const GfMatrix4d& skelToGprimXform);

    void _DeformXformWithLBS(const GfMatrix4d& skelLocalToWorldXform);

private:
    
    UsdSkelSkinningQuery _skinningQuery;
    _SkelAdapterRefPtr _skelAdapter;

    int _flags = 0;
    
    // Blend shape bindings.
    std::shared_ptr<UsdSkelBlendShapeQuery> _blendShapeQuery;
    std::vector<VtIntArray> _blendShapePointIndices;
    std::vector<VtVec3fArray> _subShapePointOffsets;
    std::vector<VtVec3fArray> _subShapeNormalOffsets;

    // Rest points.
    _Task _restPointsTask;
    VtVec3fArray _restPoints;
    UsdAttributeQuery _restPointsQuery;

    // Rest normals.
    _Task _restNormalsTask;
    VtVec3fArray _restNormals;
    UsdAttributeQuery _restNormalsQuery;

    // Geom bind transform.
    _Task _geomBindXformTask;
    GfMatrix4d _geomBindXform;
    UsdAttributeQuery _geomBindXformQuery;

    // Inverse transpose of the geom bind xform.
    _Task _geomBindInvTransposeXformTask;
    GfMatrix3d _geomBindInvTransposeXform;

    // Joint influences.
    _Task _jointInfluencesTask;
    VtIntArray _jointIndices;
    VtFloatArray _jointWeights;

    // Local to world gprim xform.
    // Used for LBS point/normal skinning only.
    _Task _localToWorldXformTask;
    GfMatrix4d _localToWorldXform;

    // Parent to world gprim xform.
    // Used for LBS xform skinning.
    _Task _parentToWorldXformTask;
    GfMatrix4d _parentToWorldXform;

    // Computed outputs and associated writers.

    // Deformed points.
    _OutputHolder<VtVec3fArray> _points;
    //_AttrWriter _pointsWriter;

    // Deformed normals.
    _OutputHolder<VtVec3fArray> _normals;
    //_AttrWriter _normalsWriter;

    // Point extent (UsdGeomMesh prims only).
    _OutputHolder<VtVec3fArray> _extent;
    //_AttrWriter _extentWriter;

    // Deformed xform.
    _OutputHolder<GfMatrix4d> _xform;
    //_AttrWriter _xformWriter;
};


using _SkinningAdapterRefPtr = std::shared_ptr<_SkinningAdapter>;


_SkinningAdapter::_SkinningAdapter(
    const UsdSkelSkinningQuery& skinningQuery,
    const _SkelAdapterRefPtr& skelAdapter,
    UsdGeomXformCache* xformCache)
    : _skinningQuery(skinningQuery),
      _skelAdapter(skelAdapter)
{
    if (!TF_VERIFY(skinningQuery) || !TF_VERIFY(skelAdapter)) {
        return;
    }
    UsdPrim skinnedPrim = skinningQuery.GetPrim();
    const bool isPointBased = skinnedPrim.IsA<UsdGeomPointBased>();
    const bool isXformable =
        isPointBased || skinnedPrim.IsA<UsdGeomXformable>();

    // Get normal/point queries, but only if authored.
    if (isPointBased) {
        const UsdGeomPointBased pointBased(skinningQuery.GetPrim());
        _restPointsQuery = UsdAttributeQuery(pointBased.GetPointsAttr());
        if (!_restPointsQuery.HasAuthoredValue()) {
            _restPointsQuery = UsdAttributeQuery();
        }

        _restNormalsQuery = UsdAttributeQuery(GetNormalsAttribute(pointBased));
        const TfToken& normalsInterp = GetNormalsInterpolation(pointBased);
        // Can only process vertex/varying normals.
        if (!_restNormalsQuery.HasAuthoredValue() ||
            (normalsInterp != UsdGeomTokens->vertex &&
                normalsInterp != UsdGeomTokens->varying)) {
            _restNormalsQuery = UsdAttributeQuery();
        }
    }

    // LBS Skinning.
    if (skinningQuery.HasJointInfluences()) {
        if (skinningQuery.IsRigidlyDeformed() && isXformable) {
            if (skelAdapter->CanComputeSkinningXforms()) {
                _flags |= DeformXformWithLBS;
            }
        } else if (isPointBased) {
            if (_restPointsQuery.IsValid() &&
                skelAdapter->CanComputeSkinningXforms()) {
                _flags |= DeformPointsWithLBS;
            }
            if (_restNormalsQuery.IsValid() &&
                skelAdapter->CanComputeSkinningInvTransposeXforms()) {
                _flags |= DeformNormalsWithLBS;
            }
        }
    }

    // Blend shapes.
    if (skelAdapter->CanComputeBlendShapeWeights() &&
        isPointBased && skinningQuery.HasBlendShapes() &&
        (_restPointsQuery || _restNormalsQuery)) {
        
        // Create a blend shape query to help process blend shapes.
        _blendShapeQuery.reset(new UsdSkelBlendShapeQuery(
                                   UsdSkelBindingAPI(skinningQuery.GetPrim())));
        if (_blendShapeQuery->IsValid()) {
            if (_restPointsQuery) {

                _subShapePointOffsets =
                    _blendShapeQuery->ComputeSubShapePointOffsets();

                const bool hasPointOffsets =
                    std::any_of(_subShapePointOffsets.begin(),
                                _subShapePointOffsets.end(),
                                [](const VtVec3fArray& points)
                                { return !points.empty(); });
                if (hasPointOffsets) {
                    _flags |=
                        DeformPointsWithBlendShapes;
                }
            }
            if (_restNormalsQuery) {
                _subShapeNormalOffsets =
                    _blendShapeQuery->ComputeSubShapeNormalOffsets();
                const bool hasNormalOffsets =
                    std::any_of(_subShapeNormalOffsets.begin(),
                                _subShapeNormalOffsets.end(),
                                [](const VtVec3fArray& normals)
                                { return !normals.empty(); });
                if (hasNormalOffsets) {
                    _flags |=
                        DeformNormalsWithBlendShapes;
                }
            }
            if (_flags & DeformWithBlendShapes) {
                _blendShapePointIndices =
                    _blendShapeQuery->ComputeBlendShapePointIndices();
            }
        }
        if (!(_flags & DeformWithBlendShapes)) {
            _blendShapeQuery.reset();
        }
    }

    if (!_flags) {
        return;
    }

    // Activate computations.

    if (_flags & ModifiesPoints) {
        // Will need rest points.
        _restPointsTask.SetActive(true);
        _restPointsTask.SetMightBeTimeVarying(
            _restPointsQuery.ValueMightBeTimeVarying());
    }
    
    if (_flags & ModifiesNormals) {
        // Will need rest normals.
        _restNormalsTask.SetActive(true);
        _restNormalsTask.SetMightBeTimeVarying(
            _restNormalsQuery.ValueMightBeTimeVarying());
    }

    if (_flags & RequiresGeomBindXform) {
        _geomBindXformTask.SetActive(true);
        if ((_geomBindXformQuery = UsdAttributeQuery(
                 _skinningQuery.GetGeomBindTransformAttr()))) {
            _geomBindXformTask.SetMightBeTimeVarying(
                _geomBindXformQuery.ValueMightBeTimeVarying());
        }

        if (_flags & RequiresGeomBindInvTransposeXform) {
            _geomBindInvTransposeXformTask.SetActive(true);
            _geomBindInvTransposeXformTask.SetMightBeTimeVarying(
                _geomBindXformTask.MightBeTimeVarying());
        }
    }
    
    if (_flags & RequiresJointInfluences) {
        _jointInfluencesTask.SetActive(true);
        _jointInfluencesTask.SetMightBeTimeVarying(
            _skinningQuery.GetJointIndicesPrimvar().ValueMightBeTimeVarying() ||
            _skinningQuery.GetJointWeightsPrimvar().ValueMightBeTimeVarying());
    }
    
    if (_flags & RequiresPrimLocalToWorldXform) {
        _localToWorldXformTask.SetActive(true);
        _localToWorldXformTask.SetMightBeTimeVarying(
            _WorldTransformMightBeTimeVarying(
                skinningQuery.GetPrim(), xformCache));
    }

    if (_flags & RequiresPrimParentToWorldXform) {

        if (!xformCache->GetResetXformStack(skinningQuery.GetPrim())) {
            _parentToWorldXformTask.SetActive(true);
            _parentToWorldXformTask.SetMightBeTimeVarying(
                _WorldTransformMightBeTimeVarying(
                    skinningQuery.GetPrim().GetParent(), xformCache));
        } else {
            // Parent xform will always be identity.
            // Initialize the parent xform, but keep the computation inactive.
            _parentToWorldXform.SetIdentity();
        }
    }

    // Mark dependent computations on the skel as required where needed.
    if (_flags & RequiresBlendShapeWeights) {
        skelAdapter->SetBlendShapeWeightsRequired(true);
    }
    if (_flags & RequiresSkinningXforms) {
        skelAdapter->SetSkinningXformsRequired(true);
    }
    if (_flags & RequiresSkinningInvTransposeXforms) {
        skelAdapter->SetSkinningInvTransposeXformsRequired(true);
    }
    if (_flags & RequiresSkelLocalToWorldXform) {
        skelAdapter->SetLocalToWorldXformRequired(true);
    }
}


void
_SkinningAdapter::ExtendTimeSamples(const GfInterval& interval,
                                    std::vector<double>* times)
{
    std::vector<double> tmpTimes;
    if (_restPointsTask) {
        if (_restPointsQuery.GetTimeSamples(&tmpTimes)) {
            _InsertTimesInInterval(interval, tmpTimes, times);
        }
    }
    if (_restNormalsTask) {
        if (_restNormalsQuery.GetTimeSamples(&tmpTimes)) {
            _InsertTimesInInterval(interval, tmpTimes, times);
        }
    }
    if (_geomBindXformTask && _geomBindXformQuery) {
        if (_geomBindXformQuery.GetTimeSamples(&tmpTimes)) {
            _InsertTimesInInterval(interval, tmpTimes, times);
        }
    }
    if (_jointInfluencesTask) {
        for (const auto& pv : {_skinningQuery.GetJointIndicesPrimvar(),
                               _skinningQuery.GetJointWeightsPrimvar()}) {
            if (pv.GetTimeSamples(&tmpTimes)) {
                _InsertTimesInInterval(interval, tmpTimes, times);
            }
        }
    }
    if (_localToWorldXformTask) {
        _ExtendWorldTransformTimeSamples(_skinningQuery.GetPrim(),
                                         interval, times);
    }
    if  (_parentToWorldXformTask) {
        _ExtendWorldTransformTimeSamples(_skinningQuery.GetPrim().GetParent(),
                                         interval, times);
    }
}

void
_SkinningAdapter::UpdateTransform(const size_t timeIndex,
                                  UsdGeomXformCache* xfCache)
{
    // The original code was only updating the transforms if there 
    // were keys in this specific time, but here we need to sample
    // all the required times to fill the arnold AtArrays. 
    // if (ShouldProcessAtTime(timeIndex))
    {
        
        _localToWorldXformTask.Run(
            xfCache->GetTime(), GetPrim(), "compute prim local to world xform",
            [&](UsdTimeCode time) {
                _localToWorldXform =
                    xfCache->GetLocalToWorldTransform(GetPrim());
                return true;
            });

        _parentToWorldXformTask.Run(
            xfCache->GetTime(), _skinningQuery.GetPrim(),
            "compute prim parent to world xform",
            [&](UsdTimeCode time) {
                _parentToWorldXform =
                    xfCache->GetParentToWorldTransform(
                        _skinningQuery.GetPrim());
                return true;
            });
    }
}


bool
_SkinningAdapter::_ComputeRestPoints(const UsdTimeCode time)
{
    return _restPointsTask.Run(
        time, GetPrim(), "compute rest points",
        [&](UsdTimeCode time) -> bool {
            return _restPointsQuery.Get(&_restPoints, time);
        });
}


bool
_SkinningAdapter::_ComputeRestNormals(const UsdTimeCode time)
{
    return _restNormalsTask.Run(
        time, GetPrim(), "compute rest normals",
        [&](UsdTimeCode time) {
            return _restNormalsQuery.Get(&_restNormals, time);
        });
}


bool
_SkinningAdapter::_ComputeGeomBindXform(const UsdTimeCode time)
{
    _geomBindXformTask.Run(
        time, GetPrim(), "compute geom bind xform",
        [&](UsdTimeCode time) {
            _geomBindXform = _skinningQuery.GetGeomBindTransform(time);
            return true;
        });
    if (_geomBindXformTask.HasSampleAtCurrentTime()) {
        _geomBindInvTransposeXformTask.Run(
            time, GetPrim(),
            "compute geom bind inverse transpose xform",
            [&](UsdTimeCode time) {
                _geomBindInvTransposeXform =
                    _geomBindXform.ExtractRotationMatrix()
                    .GetInverse().GetTranspose();
                return true;
            });
    }
    return true;
}


bool
_SkinningAdapter::_ComputeJointInfluences(const UsdTimeCode time)
{
    return _jointInfluencesTask.Run(
        time, GetPrim(), "compute joint influences",
        [&](UsdTimeCode time) {
            return _skinningQuery.ComputeJointInfluences(
                &_jointIndices, &_jointWeights, time);
        });
}


void
_SkinningAdapter::_DeformWithBlendShapes()
{
    VtFloatArray weights;
    if (_blendShapeQuery && _skelAdapter->GetBlendShapeWeights(&weights)) {
        // Remap the wegiht anim into the order for this prim.
        VtFloatArray weightsForPrim;    
        if (_skinningQuery.GetBlendShapeMapper()->Remap(
                weights, &weightsForPrim)) {

            // Resolve sub shapes (I.e., in-betweens)
            VtFloatArray subShapeWeights;
            VtUIntArray blendShapeIndices, subShapeIndices;
            if (_blendShapeQuery->ComputeSubShapeWeights(
                    weightsForPrim, &subShapeWeights,
                    &blendShapeIndices, &subShapeIndices)) {

                if (_flags & 
                    DeformPointsWithBlendShapes) {

                    // Initialize points to rest if not yet initialized.
                    if (!_points.hasSampleAtCurrentTime) {
                        _points.value = _restPoints;
                    }

                    _points.hasSampleAtCurrentTime =
                        _blendShapeQuery->ComputeDeformedPoints(
                            subShapeWeights, blendShapeIndices,
                            subShapeIndices, _blendShapePointIndices,
                            _subShapePointOffsets, _points.value);

                }
                if (_flags & 
                    DeformNormalsWithBlendShapes) {

                    // Initialize normals to rest if not yet initialized.
                    if (!_normals.hasSampleAtCurrentTime) {
                        _normals.value = _restNormals;
                    }
                    _normals.hasSampleAtCurrentTime =
                        _blendShapeQuery->ComputeDeformedNormals(
                            subShapeWeights, blendShapeIndices,
                            subShapeIndices, _blendShapePointIndices,
                            _subShapeNormalOffsets, _normals.value);
                }
            }
        }
    }
}


void
_SkinningAdapter::_DeformWithLBS(const UsdTimeCode time, const size_t timeIndex)
{
    if (!_ComputeGeomBindXform(time) || !_ComputeJointInfluences(time)) {
        return;
    }

    GfMatrix4d skelLocalToWorldXform;
    if (!_skelAdapter->GetLocalToWorldTransform(&skelLocalToWorldXform)) {
        return;
    }
    
    if (_flags & (DeformPointsWithLBS |
                  DeformNormalsWithLBS)) {
        
        // Skinning deforms points/normals in *skel* space.
        // A world-space point is then computed as:
        //
        //    worldSkinnedPoint = skelSkinnedPoint * skelLocalToWorld
        //
        // Since we're baking points/noramls into a gprim, we must
        // transform these from skel space into gprim space, such that:
        //
        //    localSkinnedPoint * gprimLocalToWorld = worldSkinnedPoint
        //  
        // So the points/normals we store must be transformed as:
        //
        //    localSkinnedPoint = skelSkinnedPoint *
        //       skelLocalToWorld * inv(gprimLocalToWorld)

        TF_VERIFY(_localToWorldXformTask.HasSampleAtCurrentTime());

        const GfMatrix4d skelToGprimXform =
            skelLocalToWorldXform * _localToWorldXform.GetInverse();

        if (_flags & DeformPointsWithLBS) {
            _DeformPointsWithLBS(skelToGprimXform);
        }
        if (_flags & DeformNormalsWithLBS) {
            _DeformNormalsWithLBS(skelToGprimXform);
        }
    } else if (_flags & DeformXformWithLBS) {
        _DeformXformWithLBS(skelLocalToWorldXform);
    }
}


void
_SkinningAdapter::_DeformPointsWithLBS(const GfMatrix4d& skelToGprimXf)
{
    if (!_restPointsTask.HasSampleAtCurrentTime() ||
        !_jointInfluencesTask.HasSampleAtCurrentTime()) {
        return;
    }

    VtMatrix4dArray xforms;
    if (!_skelAdapter->GetSkinningTransforms(&xforms)) {
        return;
    }

    // Handle local skel:joints ordering.
    VtMatrix4dArray xformsForPrim;
    if (_skinningQuery.GetJointMapper()) {
        if (!_skinningQuery.GetJointMapper()->RemapTransforms(
                xforms, &xformsForPrim)) {
            return;
        }
     } else {
        // No mapper; use the same joint order as given on the skel.
        xformsForPrim = xforms;
    }

    // Initialize points from rest points.
    // Keep the current points if already initialized
    // (eg., by blendshape application)
    if (!_points.hasSampleAtCurrentTime) {
        _points.value = _restPoints;
    }

    _points.hasSampleAtCurrentTime =
        UsdSkelSkinPointsLBS(_geomBindXform, xformsForPrim,
                             _jointIndices, _jointWeights,
                             _skinningQuery.GetNumInfluencesPerComponent(),
                             _points.value);

    if (!_points.hasSampleAtCurrentTime) {
        return;
    }

    // Output of skinning is in *skel* space.
    // Transform the result into gprim space.

    for (auto& pointValue : _points.value) {
        pointValue = MatTransform(skelToGprimXf, pointValue);
    }
}


void
_SkinningAdapter::_DeformNormalsWithLBS(const GfMatrix4d& skelToGprimXf)
{
    if (!_restNormalsTask.HasSampleAtCurrentTime() ||
        !_jointInfluencesTask.HasSampleAtCurrentTime()) {
        return;
    }

    VtMatrix3dArray xforms;
    if (!_skelAdapter->GetSkinningInvTransposeTransforms(&xforms)) {
        return;
    }

    // Handle local skel:joints ordering.
    VtMatrix3dArray xformsForPrim;
    if (_skinningQuery.GetJointMapper()) {
        static const GfMatrix3d identity(1);
        if (!_skinningQuery.GetJointMapper()->Remap(
                xforms, &xformsForPrim, /*elemSize*/ 1, &identity)) {
            return;
        }
     } else {
        // No mapper; use the same joint order as given on the skel.
        xformsForPrim = xforms;
    }

    // Initialize normals from rest normals.
    // Keep the current normals if already initialized
    // (eg., by blendshape application)
    if (!_normals.hasSampleAtCurrentTime) {
        _normals.value = _restNormals;
    }

    _normals.hasSampleAtCurrentTime =
        UsdSkelSkinNormalsLBS(_geomBindInvTransposeXform, xformsForPrim,
                              _jointIndices, _jointWeights,
                              _skinningQuery.GetNumInfluencesPerComponent(),
                              _normals.value);
    if (!_normals.hasSampleAtCurrentTime) {
        return;
    }

    // Output of skinning is in *skel* space.
    // Transform the result into gprim space.

    const GfMatrix3d& skelToGprimInvTransposeXform =
        skelToGprimXf.ExtractRotationMatrix().GetInverse().GetTranspose();

    for (GfVec3f & n : _normals.value) {
        GfVec3d n_double(n);
        n_double = n_double * skelToGprimInvTransposeXform;
        n[0] = static_cast<float>(n_double[0]);
        n[1] = static_cast<float>(n_double[1]);
        n[2] = static_cast<float>(n_double[2]);
    }
}


void
_SkinningAdapter::_DeformXformWithLBS(const GfMatrix4d& skelLocalToWorldXform)
{
    if (!_jointInfluencesTask.HasSampleAtCurrentTime() ||
        !_geomBindXformTask.HasSampleAtCurrentTime()) {
        return;
    }

    VtMatrix4dArray xforms;
    if (!_skelAdapter->GetSkinningTransforms(&xforms)) {
        return;
    }

    // Handle local skel:joints ordering.
    VtMatrix4dArray xformsForPrim;
    if (_skinningQuery.GetJointMapper()) {
        if (!_skinningQuery.GetJointMapper()->RemapTransforms(
                xforms, &xformsForPrim)) {
            return;
        }
    } else {
        // No mapper; use the same joint order as given on the skel.
        xformsForPrim = xforms;
    }

    _xform.hasSampleAtCurrentTime =
        UsdSkelSkinTransformLBS(_geomBindXform, xformsForPrim,
                                _jointIndices, _jointWeights,
                                &_xform.value);
    
    if (!_xform.hasSampleAtCurrentTime) {
        return;
    }

    // Skinning a transform produces a new transform in *skel* space.
    // A world-space transform is then computed as:
    //
    //    worldSkinnedXform = skelSkinnedXform * skelLocalToWorld
    //
    // Since we're baking transforms into a prim, we must transform
    // from skel space into the space of that prim's parent, such that:
    //
    //    newLocalXform * parentToWorld = worldSkinnedXform
    //
    // So the skinned, local transform becomes:
    //
    //    newLocalXform = skelSkinnedXform *
    //        skelLocalToWorld * inv(parentToWorld)

    _xform.value = _xform.value * skelLocalToWorldXform *
        _parentToWorldXform.GetInverse();
}


void
_SkinningAdapter::Update(const UsdTimeCode time, const size_t timeIndex)
{
    /*
    // The original code was only doing the update only if there 
    // were keys in this specific time, but here we need to sample
    // all the required times to fill the arnold AtArrays. 

    if (!ShouldProcessAtTime(timeIndex)) {
        return;
    }*/
    
    _points.BeginUpdate();
    _normals.BeginUpdate();
    _extent.BeginUpdate();
    _xform.BeginUpdate();

    // Compute inputs.
    _ComputeRestPoints(time);
    _ComputeRestNormals(time);

    // Blend shapes precede LBS skinning.
    if (_flags & DeformWithBlendShapes) {
        _DeformWithBlendShapes();
    }

    if (_flags & DeformWithLBS) {
        _DeformWithLBS(time, timeIndex);
    }

    // If a valid points sample was computed, also compute a new extent.
    if (_points.hasSampleAtCurrentTime) {
        _extent.hasSampleAtCurrentTime =
            UsdGeomPointBased::ComputeExtent(_points.value, &_extent.value);
    }
}


void
_UnionTimes(const std::vector<double> additionalTimes,
            std::vector<double>* times,
            std::vector<double>* tmpUnionTimes)
{
    tmpUnionTimes->resize(times->size() + additionalTimes.size());
    const auto& it = std::set_union(times->begin(), times->end(),
                                    additionalTimes.begin(),
                                    additionalTimes.end(),
                                    tmpUnionTimes->begin());
    tmpUnionTimes->resize(std::distance(tmpUnionTimes->begin(), it));
    times->swap(*tmpUnionTimes);
}


/// Create skel and skinning adapters from UsdSkelBinding objects to help
/// wrangle I/O.
bool
_CreateAdapters(
    const std::vector<UsdSkelBinding>& bindings,
    const UsdSkelCache& skelCache,
    _SkelAdapterRefPtr& skelAdapter,
    _SkinningAdapterRefPtr& skinningAdapter,
    UsdGeomXformCache* xfCache, 
    const std::string &skinnedPrim)
{    
    skelAdapter.reset(); // May be this could be reused instead of being reset
    skinningAdapter.reset();

    for (size_t i = 0; i < bindings.size(); ++i) {
        const UsdSkelBinding& binding = bindings[i];
        if (!skinnedPrim.empty()) {
            bool foundSkinnedPrim = false;
            for (const UsdSkelSkinningQuery& skinningQuery :
                         binding.GetSkinningTargets()) {
                
                if (skinningQuery.GetPrim().GetPath().GetString() == skinnedPrim) {
                    foundSkinnedPrim = true;
                    break;
                }
            }
            if (!foundSkinnedPrim)
                continue;
        }

        if (!binding.GetSkinningTargets().empty()) {

            if (const UsdSkelSkeletonQuery skelQuery =
                skelCache.GetSkelQuery(binding.GetSkeleton())) {

                auto skelAdapterTmp =
                    std::make_shared<_SkelAdapter>(skelQuery, xfCache, binding.GetSkeleton().GetPrim());

                for (const UsdSkelSkinningQuery& skinningQuery :
                         binding.GetSkinningTargets()) {

                    if (!skinnedPrim.empty() && skinningQuery.GetPrim().GetPath().GetString() != skinnedPrim)
                        continue;

                    auto skinningAdapterTmp =  
                        std::make_shared<_SkinningAdapter>(
                            skinningQuery, skelAdapterTmp,
                            xfCache);
                    
                    // Only add this adapter if it will be used.
                    if (skinningAdapterTmp->HasTasksToRun()) {
                        skinningAdapter = skinningAdapterTmp;
                        break;
                    }
                }

                if (skelAdapterTmp->HasTasksToRun()) {
                    skelAdapter = skelAdapterTmp;
                }
            }
        }
    }

    return (skelAdapter || skinningAdapter);
}


/// Compute an array of time samples over \p interval.
/// The samples are added based on the expected sampling rate for playback.
/// I.e., the exact set of time codes that we expect to be queried when
/// the stage is played back at its configured
/// timeCodesPerSecond/framesPerSecond rate.
std::vector<double>
_GetStagePlaybackTimeCodesInRange(const UsdStagePtr& stage,
                                  const GfInterval& interval)
{
    std::vector<double> times;
    if (!stage->HasAuthoredTimeCodeRange()) {
        return times;
    }
    
    const double timeCodesPerSecond = stage->GetTimeCodesPerSecond();
    const double framesPerSecond = stage->GetFramesPerSecond();
    if (GfIsClose(timeCodesPerSecond, 0.0, 1e-6) ||
        GfIsClose(framesPerSecond, 0.0, 1e-6)) {
        return times;
    }
    // Compute the expected per-frame time step for playback.
    const double timeStep =
        std::abs(timeCodesPerSecond/framesPerSecond);

    const double stageStart = stage->GetStartTimeCode();
    const double stageEnd = stage->GetEndTimeCode();
    if (stageEnd < stageStart) {
        // Malfored time code range.
        return times;
    }
    // Add 1 to the sample count for an inclusive range.
    const int64_t numTimeSamples = (stageEnd-stageStart)/timeStep + 1;
    times.reserve(numTimeSamples);
    for(int64_t i = 0; i <= numTimeSamples; ++i) {
        // Add samples based on integer multiples of the time step
        // to reduce error.
        const double t = stageStart + timeStep*i;
        if (interval.Contains(t)) {
            times.push_back(t);
        }
    }
    return times;
}


/// Compute the full set of time samples at which data must be sampled.
std::vector<UsdTimeCode>
_ComputeTimeSamples(
    const UsdStagePtr& stage,
    const GfInterval& interval,
    _SkelAdapterRefPtr& skelAdapter,
    const _SkinningAdapterRefPtr& skinningAdapter)
{
    std::vector<UsdTimeCode> times;

    // Pre-compute time samples for each skel adapter.
    std::vector<double> skelTimesMap;
    skelTimesMap.push_back(interval.GetMin());
    skelTimesMap.push_back(interval.GetMax());
    // Pre-populate the skelTimesMap on a single thread. Each worker thread
    // will only access its own members in this map, so access to each vector
    // is safe.
    if (skelAdapter)
        skelAdapter->ExtendTimeSamples(interval, &skelTimesMap);


    // Extend the time samples of each skel adapter with the time samples
    // of each skinning adapter.
    // NOTE: multiple skinning adapters may share the same skel adapter, so in
    // order for this work to be done in parallel the skinning adapters would
    // need to be grouped such that that the same skel adapter isn't modified
    // by multiple threads.
    if (skinningAdapter)
        skinningAdapter->ExtendTimeSamples(
            interval,
            &skelTimesMap);

    // Each times array may now hold duplicate entries. 
    // Sort and remove dupes from each array.
    std::sort(skelTimesMap.begin(), skelTimesMap.end());
    skelTimesMap.erase(std::unique(skelTimesMap.begin(), skelTimesMap.end()),
                skelTimesMap.end());
    
    // XXX: Skinning meshes are baked at each time sample at which joint
    // transforms or blend shapes are authored. If the joint transforms
    // are authored at sparse time samples, then the deformed meshes will
    // be linearly interpolated on sub-frames. But linearly interpolating
    // deformed meshes is not equivalent to linearly interpolating the
    // the driving animation, particularly when considering joint rotations.
    // It is impossible to get a perfect match at every possible sub-frame,
    // since the resulting stage may be read at arbitrary sub-frames, but
    // we can at least make sure that the samples are correct at the
    // frames on which the stage is expected to be sampled, based on the
    // stage's time-code metadata.
    // In other words, we wish to bake skinning at every time ordinate at
    /// which the output is expected to be sampled.
    const std::vector<double> stageTimes =
        _GetStagePlaybackTimeCodesInRange(stage, interval);
    
    // Compute the total union of all time samples.
    std::vector<double> allTimes;
    std::vector<double> tmpUnionTimes;
    _UnionTimes(stageTimes, &allTimes, &tmpUnionTimes);
    _UnionTimes(skelTimesMap, &allTimes, &tmpUnionTimes);

    // Actual time samples will be the times above.
    times.clear();
    times.reserve(allTimes.size() + 1);
    times.insert(times.end(), allTimes.begin(), allTimes.end());

    return times;
}


struct UsdArnoldSkelDataImpl {

    std::vector<UsdTimeCode> times;
    UsdSkelCache skelCache;
    bool isValid = false;

    // Bindings between skeletons and skinned are computed the first time this structure is created on the skelRoot
    std::vector<UsdSkelBinding> bindings;
    
    // skelAdapter and skinningAdapter are allocated per skinned objects.
    _SkelAdapterRefPtr skelAdapter;
    _SkinningAdapterRefPtr skinningAdapter;
};

UsdArnoldSkelData::UsdArnoldSkelData(const UsdPrim &prim)
{
    _impl = new UsdArnoldSkelDataImpl;
    UsdSkelRoot skelRoot(prim);
    if (!skelRoot) 
        return;

    const Usd_PrimFlagsPredicate predicate = UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate);
    _impl->skelCache.Populate(skelRoot, predicate);
    if (!_impl->skelCache.ComputeSkelBindings(skelRoot, &_impl->bindings, predicate)) {
        return;
    }

    if (_impl->bindings.empty()) {
        return;
    }
    // nothing to do
    _impl->isValid = true;

}
UsdArnoldSkelData::UsdArnoldSkelData(const UsdArnoldSkelData &src)
{
    _impl = new UsdArnoldSkelDataImpl(*(src._impl));
}
UsdArnoldSkelData::~UsdArnoldSkelData()
{
    delete _impl;
}

bool UsdArnoldSkelData::HasSkinning(const UsdPrim &prim) const {
    if (!_impl || !_impl->isValid) return false;
    return _impl->skinningAdapter ? true : false;
}

void UsdArnoldSkelData::CreateAdapters(UsdArnoldReaderContext &context, const std::string &primName)
{
    if (!_impl->isValid)
        return;

    const TimeSettings &time = context.GetTimeSettings();
    
    GfInterval interval(time.start(), time.end());
    
    UsdGeomXformCache localXfCache;
    UsdGeomXformCache *xfCache = _FindXformCache(context, time.frame, localXfCache);
    
    // Create adapters to wrangle IO on skels and skinnable prims.
    if (!_CreateAdapters(_impl->bindings, _impl->skelCache, _impl->skelAdapter,
                         _impl->skinningAdapter, xfCache, primName)) {
        return;
    }

    // Look for all the existing keyframes in the interval
    _impl->times = _ComputeTimeSamples(context.GetReader()->GetStage(), interval, _impl->skelAdapter,
                            _impl->skinningAdapter); 

    // Arnold needs an uniform distribution of the time sample so we resample them keeping the same
    // number of keys
    const size_t numKeys = _impl->times.size();
    for (size_t i = 0; i < numKeys; i++) {
        const double div = numKeys > 1 ? static_cast<double>(numKeys) - 1.0 : 1.0;
        _impl->times[i] = interval.GetMin() + ((double)i / div) * (interval.GetMax() - interval.GetMin());
    }
}
const std::vector<UsdTimeCode> &UsdArnoldSkelData::GetTimes() const 
{
    return _impl->times;
}
bool UsdArnoldSkelData::IsValid() const {return _impl->isValid;}

bool UsdArnoldSkelData::ApplyPointsSkinning(const UsdPrim &prim, const VtArray<GfVec3f> &input, VtArray<GfVec3f> &output, UsdArnoldReaderContext &context, double time, SkinningData s)
{    
    if (!_impl->isValid) {
        return false;
    }
    UsdGeomXformCache localXfCache;
    int timeIndex = -1;
    for (size_t ti = 0; ti < _impl->times.size(); ++ti) {
        const UsdTimeCode t = _impl->times[ti];
        // There are different methods for interpolating the time inside the interval,
        // as they don't have the same precision we need to check if the value is close
        if (GfIsClose(t.GetValue(), time, AI_EPSILON))
        {
            timeIndex = ti;
            break;
        }
    }
    if (timeIndex < 0) 
        return false;

    UsdGeomXformCache *xfCache = _FindXformCache(context, time, localXfCache);
    const UsdTimeCode t = _impl->times[timeIndex];

    // FIXME  Ensure that we're only updating the adapters for what we need (points/normals)    
    if (_impl->skelAdapter)
        _impl->skelAdapter->UpdateTransform(timeIndex, xfCache);

    if (_impl->skinningAdapter)
        _impl->skinningAdapter->UpdateTransform(timeIndex, xfCache);

    if (_impl->skelAdapter)
        _impl->skelAdapter->UpdateAnimation(t, timeIndex);

    if (_impl->skinningAdapter)
        _impl->skinningAdapter->Update(t, timeIndex);

    bool fetchedData = false;
    if (_impl->skinningAdapter) {
        if (s == SKIN_POINTS) {
            fetchedData = _impl->skinningAdapter->GetPoints(output, timeIndex);
        } else if (s == SKIN_NORMALS) {
            fetchedData = _impl->skinningAdapter->GetNormals(output, timeIndex);
        }
    }

    if (fetchedData) { 
        if (output.empty() && _impl->skinningAdapter->HasFlags(DeformXformWithLBS)) {
            GfMatrix4d xform;
            if (_impl->skinningAdapter->GetXform(xform, timeIndex)) {
                output = input;
                for (auto &pt: output) {
                    pt = MatTransform(xform, pt);
                }
                return true;
            }
        }
        return true;
    } else {
        return false;
    }
}


