#pragma once
#include <pxr/pxr.h>
#include "api.h"

#include <pxr/imaging/hd/rprim.h>

#include "hdarnold.h"
#include "render_delegate.h"
#include "rprim.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldProceduralCustom: public HdRprim {
public:
    // TODO constructor for < 21.02 ?
    HDARNOLD_API
    HdArnoldProceduralCustom(HdArnoldRenderDelegate* renderDelegate, SdfPath const& id);

    HDARNOLD_API
    ~HdArnoldProceduralCustom();

    // ---------------------------------------------------------------------- //
    /// \name Rprim Hydra Engine API : Pre-Sync & Sync-Phase
    // ---------------------------------------------------------------------- //

    /// Returns the set of dirty bits that should be
    /// added to the change tracker for this prim, when this prim is inserted.
    HDARNOLD_API HdDirtyBits GetInitialDirtyBitsMask() const override;
    HDARNOLD_API void Sync(HdSceneDelegate *delegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits,
                      TfToken const   &reprToken) override;
    HDARNOLD_API
    TfTokenVector const & GetBuiltinPrimvarNames() const override {return _builtinPrimvars;}


protected:
    /// This callback from Rprim gives the prim an opportunity to set
    /// additional dirty bits based on those already set.  This is done
    /// before the dirty bits are passed to the scene delegate, so can be
    /// used to communicate that extra information is needed by the prim to
    /// process the changes.
    ///
    /// The return value is the new set of dirty bits, which replaces the bits
    /// passed in.
    ///
    /// See HdRprim::PropagateRprimDirtyBits()
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override {return bits & HdChangeTracker::AllDirty; };

    /// Initialize the given representation of this Rprim.
    /// This is called prior to syncing the prim, the first time the repr
    /// is used.
    ///
    /// reprToken is the name of the representation to initalize.
    ///
    /// dirtyBits is an in/out value.  It is initialized to the dirty bits
    /// from the change tracker.  InitRepr can then set additional dirty bits
    /// if additional data is required from the scene delegate when this
    /// repr is synced.  InitRepr occurs before dirty bit propagation.
    ///
    /// See HdRprim::InitRepr()
    void _InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits) override
    {
        TF_UNUSED(reprToken);
        TF_UNUSED(dirtyBits);
    }

private:
    HdArnoldRenderDelegate* _renderDelegate;
    TfTokenVector _builtinPrimvars;
    const int DirtyNodeEntry = 1 << 25; // This should be shared in common 
    HdArnoldRayFlags _visibilityFlags{AI_RAY_ALL}; ///< Visibility.
    AtNode *_node;
};


PXR_NAMESPACE_CLOSE_SCOPE
