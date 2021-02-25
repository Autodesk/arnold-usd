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
#include <pxr/imaging/hd/types.h>

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

TEST(HdArnoldSetConstantPrimvar, HalfArray)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfHalf>{1.0f, 2.0f, 3.0f}});
    EXPECT_TRUE(_Compare<float>(AiNodeGetArray(node, "test1"), {1.0f, 2.0f, 3.0f}));
}

TEST(HdArnoldSetConstantPrimvar, DoubleArray)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{VtArray<double>{1.0, 2.0, 3.0}});
    EXPECT_TRUE(_Compare<float>(AiNodeGetArray(node, "test1"), {1.0f, 2.0f, 3.0f}));
}

TEST(HdArnoldSetConstantPrimvar, Half2Array)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfVec2h>{{1.0f, 2.0f}, {3.0f, 4.0f}}});
    EXPECT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "test1"), {{1.0f, 2.0f}, {3.0f, 4.0f}}));
}

TEST(HdArnoldSetConstantPrimvar, Double2Array)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfVec2d>{{1.0, 2.0}, {3.0, 4.0}}});
    EXPECT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "test1"), {{1.0f, 2.0f}, {3.0f, 4.0f}}));
}

TEST(HdArnoldSetConstantPrimvar, Half3Array)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec3h>{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test1"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test2"}, HdPrimvarRoleTokens->color,
        VtValue{VtArray<GfVec3h>{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test2"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    EXPECT_EQ(AiArrayGetType(AiNodeGetArray(node, "test2")), AI_TYPE_RGB);
}

TEST(HdArnoldSetConstantPrimvar, Double3Array)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfVec3d>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test1"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test2"}, HdPrimvarRoleTokens->color,
        VtValue{VtArray<GfVec3d>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test2"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    EXPECT_EQ(AiArrayGetType(AiNodeGetArray(node, "test2")), AI_TYPE_RGB);
}

TEST(HdArnoldSetConstantPrimvar, Half4Array)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec4h>{{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}}});
    EXPECT_TRUE(_Compare<GfVec4f>(AiNodeGetArray(node, "test1"), {{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}}));
}

TEST(HdArnoldSetConstantPrimvar, Double4Array)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec4d>{{1.0, 2.0, 3.0, 4.0}, {5.0, 6.0, 7.0, 8.0}}});
    EXPECT_TRUE(_Compare<GfVec4f>(AiNodeGetArray(node, "test1"), {{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}}));
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
