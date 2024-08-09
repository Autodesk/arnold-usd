#include "procedural_custom.h"

#include <constant_strings.h>
#include "render_delegate.h"
#include "node_graph.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

void HdArnoldProceduralCustom::Sync(HdSceneDelegate *delegate,
                    HdRenderParam   *renderParam,
                    HdDirtyBits     *dirtyBits,
                    TfToken const   &reprToken) 
{
    const auto val = delegate->Get(GetId(), str::t_arnold_node_entry);
    std::string nodeEntry = VtValueGetString(val);
    if (nodeEntry.empty())
        return;

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        AtNode* node = GetArnoldNode();
        AtString nodeEntryStr(nodeEntry.c_str());
        if (node == nullptr || !AiNodeIs(node, nodeEntryStr)) {
            GetShape().SetShapeType(nodeEntryStr, GetId());
        }
    }
    HdArnoldNativeRprim::Sync(delegate, renderParam, dirtyBits, reprToken);
}


PXR_NAMESPACE_CLOSE_SCOPE