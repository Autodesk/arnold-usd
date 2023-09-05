#include "procedural_custom.h"

#include <constant_strings.h>
#include "render_delegate.h"
#include "node_graph.h"
#include "utils.h"


PXR_NAMESPACE_OPEN_SCOPE


#if PXR_VERSION >= 2102
HdArnoldProceduralCustom::HdArnoldProceduralCustom(HdArnoldRenderDelegate* renderDelegate, SdfPath const& id) : HdRprim(id), _renderDelegate(renderDelegate), _node(nullptr) {
    // Ideally we should create the arnold node here, but as it depends on a parameter in the scene delegate which we don't have here, although we could pass it ??
    // It would certainly be better if we could add it here as dependencies management would be easier
}
#else
HdArnoldProceduralCustom::HdArnoldProceduralCustom(HdArnoldRenderDelegate* renderDelegate, const SdfPath &id, SdfPath const &instancerId) : HdRprim(id, instancerId), _renderDelegate(renderDelegate), _node(nullptr) {}
#endif

HdArnoldProceduralCustom::~HdArnoldProceduralCustom()
{
    _renderDelegate->DestroyArnoldNode(_node);
    _node = nullptr;
}

HdDirtyBits HdArnoldProceduralCustom::GetInitialDirtyBitsMask() const {
    return HdChangeTracker::Clean
        | HdChangeTracker::DirtyPoints
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyMaterialId
        | DirtyNodeEntry
        ;
}

void HdArnoldProceduralCustom::Sync(HdSceneDelegate *delegate,
                    HdRenderParam   *renderParam,
                    HdDirtyBits     *dirtyBits,
                    TfToken const   &reprToken) {

    HdArnoldRenderParamInterrupt param(renderParam);
    HdArnoldPrimvarMap primvars; // Do we want to keep them in mem ?? -> NO as they can radically change depending on the node
    std::vector<HdInterpolation> interpolations = {HdInterpolationConstant};
    HdArnoldGetPrimvars(delegate, GetId(), *dirtyBits, false, primvars, &interpolations);

    if (*dirtyBits & DirtyNodeEntry) {
        // node_entry has changed, we need to either recreate the node or destroy the last one
        auto nodeEntryIt = primvars.find(str::t_arnold_node_entry);
        if (nodeEntryIt != primvars.end()) {
            const std::string nodeType = nodeEntryIt->second.value.Get<std::string>();
            param.Interrupt();
            // TODO should check the node type to avoid destroying this node if it's the same ?
            _renderDelegate->DestroyArnoldNode(_node);
            
            // Is the node_type known by arnold ?? if not _node will be null after the following call
            _node = _renderDelegate->CreateArnoldNode(AtString(nodeType.c_str()), AtString(GetId().GetText()));
        }
        *dirtyBits &= ~DirtyNodeEntry;
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar && _node) {
        // Doing the same as:
        //      ReadPrimvars(prim, node, time, context);
        //      ReadArnoldParameters(prim, context, node, time, "arnold", true);
        param.Interrupt();
        for (const auto &p : primvars) {
            // Get the parameter name, removing the arnold:prefix if any
            std::string paramName(TfStringStartsWith(p.first.GetString(), str::arnold) ? p.first.GetString().substr(7) : p.first.GetString());
            const auto* pentry = AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(_node), AtString(paramName.c_str()));
            if (pentry) {
                // Could also use ConvertPrimvarToBuiltinParameter instead of HdArnoldSetParameter
                //ConvertPrimvarToBuiltinParameter(_node, TfToken(paramName.c_str()), p.second.value, &_visibilityFlags, &_sidednessFlags, nullptr);
                HdArnoldSetParameter(_node, pentry, p.second.value);
            } else {
                // TODO: Should we declare the primvars in that case ?? they are not known by the node but might be used by shaders
                // if (HdArnoldDeclare(node, TfToken(paramName), const TfToken& scope, const TfToken& type)) {}
            }
        }
    }
    if (*dirtyBits & HdChangeTracker::DirtyVisibility && _node) {
        _UpdateVisibility(delegate, dirtyBits);
        _visibilityFlags.SetHydraFlag(_sharedData.visible ? AI_RAY_ALL : 0);
        const auto visibility = _visibilityFlags.Compose();
        AiNodeSetByte(_node, str::visibility, visibility);
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId && _node) {
        param.Interrupt();
        const auto materialId = delegate->GetMaterialId(GetId());
        // Ensure the reference from this shape to its material is properly tracked
        // by the render delegate
        _renderDelegate->TrackDependencies(GetId(), HdArnoldRenderDelegate::PathSet {materialId});
        
        const auto* material = reinterpret_cast<const HdArnoldNodeGraph*>(
            delegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));
        if (material != nullptr) {
            AiNodeSetPtr(_node, str::shader, AiNodeIs(_node, str::volume) ? material->GetVolumeShader() : material->GetSurfaceShader() );
        } else {
            AiNodeResetParameter(_node, str::shader);
        }
    }

    // NOTE: HdArnoldSetTransform must be set after the primvars as, at the moment, we might rewrite the transform in the
    // primvars and it doesn't take into account the inheritance.
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, GetId())) {
        param.Interrupt();
        HdArnoldSetTransform(_node, delegate, GetId());
    }

    *dirtyBits = HdChangeTracker::Clean;
}


PXR_NAMESPACE_CLOSE_SCOPE