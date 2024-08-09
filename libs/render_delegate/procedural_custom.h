#pragma once
#include <pxr/pxr.h>
#include "api.h"

#include <pxr/imaging/hd/rprim.h>

#include "hdarnold.h"
#include "render_delegate.h"
#include "rprim.h"
#include "native_rprim.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldProceduralCustom: public HdArnoldNativeRprim {
public:

    /// Constructor for HdArnoldProceduralCustom.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the proceduralcustom.
    HDARNOLD_API
    HdArnoldProceduralCustom(HdArnoldRenderDelegate* renderDelegate, 
        SdfPath const& id) :  HdArnoldNativeRprim(renderDelegate, AtString(), id) {}

    HDARNOLD_API
    virtual ~HdArnoldProceduralCustom() {}

    HDARNOLD_API void Sync(HdSceneDelegate *delegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits,
                      TfToken const   &reprToken) override;
};


PXR_NAMESPACE_CLOSE_SCOPE
