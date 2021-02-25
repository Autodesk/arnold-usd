#include <gtest/gtest.h>

#include <pxr/base/gf/half.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2h.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4h.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>

#include "render_delegate/utils.h"

#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

template <typename T>
bool _Compare(AtArray* arr, const std::vector<T>& vec)
{
    if (arr == nullptr) {
        return false;
    }
    const auto numElements = vec.size();
    if (AiArrayGetNumElements(arr) != numElements) {
        return false;
    }
    const auto* data = reinterpret_cast<const T*>(AiArrayMap(arr));
    const auto result = std::equal(data, data + numElements, vec.begin());
    AiArrayUnmap(arr);
    return result;
}

TEST(HdArnoldSetParameter, SingleNot32Bit)
{
    auto* node = AiNode("polymesh");
    const auto* nodeEntry = AiNodeGetNodeEntry(node);
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "ray_bias"), VtValue{GfHalf{0.5f}});
    ASSERT_EQ(AiNodeGetFlt(node, "ray_bias"), 0.5f);
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "ray_bias"), VtValue{double{2.0}});
    ASSERT_EQ(AiNodeGetFlt(node, "ray_bias"), 2.0f);
    node = AiNode("image");
    nodeEntry = AiNodeGetNodeEntry(node);
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "uvcoords"), VtValue{GfVec2h{0.5f, 1.5f}});
    ASSERT_EQ(AiNodeGetVec2(node, "uvcoords"), AtVector2(0.5f, 1.5f));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "uvcoords"), VtValue{GfVec2d{1.5, 2.5}});
    ASSERT_EQ(AiNodeGetVec2(node, "uvcoords"), AtVector2(1.5f, 2.5f));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "multiply"), VtValue{GfVec3h{1.5f, 2.5f, 3.5f}});
    ASSERT_EQ(AiNodeGetRGB(node, "multiply"), AtRGB(1.5f, 2.5f, 3.5f));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "multiply"), VtValue{GfVec3d{2.5, 3.5, 4.5}});
    ASSERT_EQ(AiNodeGetRGB(node, "multiply"), AtRGB(2.5f, 3.5f, 4.5f));
    HdArnoldSetParameter(
        node, AiNodeEntryLookUpParameter(nodeEntry, "missing_texture_color"), VtValue{GfVec4h{1.5f, 2.5f, 3.5f, 4.5f}});
    ASSERT_EQ(AiNodeGetRGBA(node, "missing_texture_color"), AtRGBA(1.5f, 2.5f, 3.5f, 4.5f));
    HdArnoldSetParameter(
        node, AiNodeEntryLookUpParameter(nodeEntry, "missing_texture_color"), VtValue{GfVec4d{2.5, 3.5, 4.5, 5.5}});
    ASSERT_EQ(AiNodeGetRGBA(node, "missing_texture_color"), AtRGBA(2.5f, 3.5f, 4.5f, 5.5f));
    node = AiNode("noise");
    nodeEntry = AiNodeGetNodeEntry(node);
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "scale"), VtValue{GfVec3h{1.5f, 2.5f, 3.5f}});
    ASSERT_EQ(AiNodeGetVec(node, "scale"), AtVector(1.5f, 2.5f, 3.5f));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "scale"), VtValue{GfVec3d{2.5, 3.5, 4.5}});
    ASSERT_EQ(AiNodeGetVec(node, "scale"), AtVector(2.5f, 3.5f, 4.5f));
}

TEST(HdArnoldSetParameter, ArrayNot32Bit)
{
    auto* node = AiNode("polymesh");
    const auto* nodeEntry = AiNodeGetNodeEntry(node);
    HdArnoldSetParameter(
        node, AiNodeEntryLookUpParameter(nodeEntry, "crease_sharpness"), VtValue{VtArray<GfHalf>{0.5f, 1.5f, 2.5f}});
    ASSERT_TRUE(_Compare<float>(AiNodeGetArray(node, "crease_sharpness"), {0.5f, 1.5f, 2.5f}));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "crease_sharpness"), VtValue{GfHalf{0.5f}});
    ASSERT_TRUE(_Compare<float>(AiNodeGetArray(node, "crease_sharpness"), {0.5f}));
    HdArnoldSetParameter(
        node, AiNodeEntryLookUpParameter(nodeEntry, "crease_sharpness"), VtValue{VtArray<double>{1.5, 2.5, 3.5}});
    ASSERT_TRUE(_Compare<float>(AiNodeGetArray(node, "crease_sharpness"), {1.5, 2.5, 3.5}));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "crease_sharpness"), VtValue{double{1.5}});
    ASSERT_TRUE(_Compare<float>(AiNodeGetArray(node, "crease_sharpness"), {1.5}));
    HdArnoldSetParameter(
        node, AiNodeEntryLookUpParameter(nodeEntry, "uvlist"), VtValue{VtArray<GfVec2h>{{0.5f, 1.5f}, {2.5f, 3.5f}}});
    ASSERT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "uvlist"), {{0.5f, 1.5f}, {2.5f, 3.5f}}));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "uvlist"), VtValue{GfVec2h{0.5f, 1.5f}});
    ASSERT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "uvlist"), {{0.5f, 1.5f}}));
    HdArnoldSetParameter(
        node, AiNodeEntryLookUpParameter(nodeEntry, "uvlist"), VtValue{VtArray<GfVec2d>{{1.5f, 2.5f}, {3.5f, 4.5f}}});
    ASSERT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "uvlist"), {{1.5f, 2.5f}, {3.5f, 4.5f}}));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "uvlist"), VtValue{GfVec2d{1.5f, 2.5f}});
    ASSERT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "uvlist"), {{1.5f, 2.5f}}));
    HdArnoldSetParameter(
        node, AiNodeEntryLookUpParameter(nodeEntry, "nlist"),
        VtValue{VtArray<GfVec3h>{{0.5f, 1.5f, 2.5f}, {3.5f, 4.5f, 5.5f}}});
    ASSERT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "nlist"), {{0.5f, 1.5f, 2.5f}, {3.5f, 4.5f, 5.5f}}));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "nlist"), VtValue{GfVec3h{0.5f, 1.5f, 2.5f}});
    ASSERT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "nlist"), {{0.5f, 1.5f, 2.5f}}));
    HdArnoldSetParameter(
        node, AiNodeEntryLookUpParameter(nodeEntry, "nlist"),
        VtValue{VtArray<GfVec3d>{{1.5, 2.5, 3.5}, {4.5, 5.5, 6.5}}});
    ASSERT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "nlist"), {{1.5f, 2.5f, 3.5f}, {4.5f, 5.5f, 6.5f}}));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "nlist"), VtValue{GfVec3d{1.5, 2.5, 3.5}});
    ASSERT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "nlist"), {{1.5f, 2.5f, 3.5f}}));
    node = AiNode("ramp_rgb");
    nodeEntry = AiNodeGetNodeEntry(node);
    HdArnoldSetParameter(
        node, AiNodeEntryLookUpParameter(nodeEntry, "color"),
        VtValue{VtArray<GfVec3h>{{0.5f, 1.5f, 2.5f}, {3.5f, 4.5f, 5.5f}}});
    ASSERT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "color"), {{0.5f, 1.5f, 2.5f}, {3.5f, 4.5f, 5.5f}}));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "color"), VtValue{GfVec3h{0.5f, 1.5f, 2.5f}});
    ASSERT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "color"), {{0.5f, 1.5f, 2.5f}}));
    HdArnoldSetParameter(
        node, AiNodeEntryLookUpParameter(nodeEntry, "color"),
        VtValue{VtArray<GfVec3d>{{1.5, 2.5, 3.5}, {4.5, 5.5, 6.5}}});
    ASSERT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "color"), {{1.5f, 2.5f, 3.5f}, {4.5f, 5.5f, 6.5f}}));
    HdArnoldSetParameter(node, AiNodeEntryLookUpParameter(nodeEntry, "color"), VtValue{GfVec3d{1.5, 2.5, 3.5}});
    ASSERT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "color"), {{1.5f, 2.5f, 3.5f}}));
    // No built-in shader with RGBA [] parameter?
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    AiBegin();
    AiMsgSetConsoleFlags(AI_LOG_NONE);
    auto result = RUN_ALL_TESTS();
    AiEnd();
    return result;
}
