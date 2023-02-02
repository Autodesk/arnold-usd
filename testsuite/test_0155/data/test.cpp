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
#include "render_delegate/hdarnold.h"

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

TEST(HdArnoldSetUniformPrimvar, UniformNot32Bit)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetUniformPrimvar(node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfHalf>{1.0f, 2.0f}});
    EXPECT_TRUE(_Compare<float>(AiNodeGetArray(node, "test1"), {1.0f, 2.0f}));
    const auto* param = AiNodeLookUpUserParameter(node, "test1");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_FLOAT);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_UNIFORM);
    HdArnoldSetUniformPrimvar(
        node, TfToken{"test2"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfVec2h>{{1.0f, 2.0f}, {3.0f, 4.0f}}});
    EXPECT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "test2"), {{1.0f, 2.0f}, {3.0f, 4.0f}}));
    param = AiNodeLookUpUserParameter(node, "test2");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR2);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_UNIFORM);
    HdArnoldSetUniformPrimvar(
        node, TfToken{"test3"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec3h>{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test3"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    param = AiNodeLookUpUserParameter(node, "test3");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_UNIFORM);
    HdArnoldSetUniformPrimvar(
        node, TfToken{"test4"}, HdPrimvarRoleTokens->color,
        VtValue{VtArray<GfVec3h>{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test4"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    param = AiNodeLookUpUserParameter(node, "test4");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGB);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_UNIFORM);
    HdArnoldSetUniformPrimvar(
        node, TfToken{"test5"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec4h>{{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}}});
    EXPECT_TRUE(_Compare<GfVec4f>(AiNodeGetArray(node, "test5"), {{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}}));
    param = AiNodeLookUpUserParameter(node, "test5");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGBA);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_UNIFORM);
    HdArnoldSetUniformPrimvar(node, TfToken{"test6"}, HdPrimvarRoleTokens->none, VtValue{VtArray<double>{1.0, 2.0}});
    EXPECT_TRUE(_Compare<float>(AiNodeGetArray(node, "test6"), {1.0f, 2.0f}));
    param = AiNodeLookUpUserParameter(node, "test6");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_FLOAT);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_UNIFORM);
    HdArnoldSetUniformPrimvar(
        node, TfToken{"test7"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfVec2d>{{1.0, 2.0}, {3.0, 4.0}}});
    EXPECT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "test7"), {{1.0f, 2.0f}, {3.0f, 4.0f}}));
    param = AiNodeLookUpUserParameter(node, "test7");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR2);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_UNIFORM);
    HdArnoldSetUniformPrimvar(
        node, TfToken{"test8"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfVec3d>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test8"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    param = AiNodeLookUpUserParameter(node, "test8");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_UNIFORM);
    HdArnoldSetUniformPrimvar(
        node, TfToken{"test9"}, HdPrimvarRoleTokens->color,
        VtValue{VtArray<GfVec3d>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test9"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    param = AiNodeLookUpUserParameter(node, "test9");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGB);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_UNIFORM);
    HdArnoldSetUniformPrimvar(
        node, TfToken{"test10"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec4d>{{1.0, 2.0, 3.0, 4.0}, {5.0, 6.0, 7.0, 8.0}}});
    EXPECT_TRUE(
        _Compare<GfVec4f>(AiNodeGetArray(node, "test10"), {{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}}));
    param = AiNodeLookUpUserParameter(node, "test10");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGBA);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_UNIFORM);
}

TEST(HdArnoldSetVertexPrimvar, VertexNot32Bit)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetVertexPrimvar(node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfHalf>{1.0f, 2.0f}});
    EXPECT_TRUE(_Compare<float>(AiNodeGetArray(node, "test1"), {1.0f, 2.0f}));
    const auto* param = AiNodeLookUpUserParameter(node, "test1");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_FLOAT);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_VARYING);
    HdArnoldSetVertexPrimvar(
        node, TfToken{"test2"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfVec2h>{{1.0f, 2.0f}, {3.0f, 4.0f}}});
    EXPECT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "test2"), {{1.0f, 2.0f}, {3.0f, 4.0f}}));
    param = AiNodeLookUpUserParameter(node, "test2");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR2);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_VARYING);
    HdArnoldSetVertexPrimvar(
        node, TfToken{"test3"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec3h>{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test3"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    param = AiNodeLookUpUserParameter(node, "test3");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_VARYING);
    HdArnoldSetVertexPrimvar(
        node, TfToken{"test4"}, HdPrimvarRoleTokens->color,
        VtValue{VtArray<GfVec3h>{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test4"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    param = AiNodeLookUpUserParameter(node, "test4");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGB);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_VARYING);
    HdArnoldSetVertexPrimvar(
        node, TfToken{"test5"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec4h>{{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}}});
    EXPECT_TRUE(_Compare<GfVec4f>(AiNodeGetArray(node, "test5"), {{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}}));
    param = AiNodeLookUpUserParameter(node, "test5");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGBA);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_VARYING);
    HdArnoldSetVertexPrimvar(node, TfToken{"test6"}, HdPrimvarRoleTokens->none, VtValue{VtArray<double>{1.0, 2.0}});
    EXPECT_TRUE(_Compare<float>(AiNodeGetArray(node, "test6"), {1.0f, 2.0f}));
    param = AiNodeLookUpUserParameter(node, "test6");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_FLOAT);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_VARYING);
    HdArnoldSetVertexPrimvar(
        node, TfToken{"test7"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfVec2d>{{1.0, 2.0}, {3.0, 4.0}}});
    EXPECT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "test7"), {{1.0f, 2.0f}, {3.0f, 4.0f}}));
    param = AiNodeLookUpUserParameter(node, "test7");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR2);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_VARYING);
    HdArnoldSetVertexPrimvar(
        node, TfToken{"test8"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfVec3d>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test8"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    param = AiNodeLookUpUserParameter(node, "test8");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_VARYING);
    HdArnoldSetVertexPrimvar(
        node, TfToken{"test9"}, HdPrimvarRoleTokens->color,
        VtValue{VtArray<GfVec3d>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}});
    EXPECT_TRUE(_Compare<GfVec3f>(AiNodeGetArray(node, "test9"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}));
    param = AiNodeLookUpUserParameter(node, "test9");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGB);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_VARYING);
    HdArnoldSetVertexPrimvar(
        node, TfToken{"test10"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec4d>{{1.0, 2.0, 3.0, 4.0}, {5.0, 6.0, 7.0, 8.0}}});
    EXPECT_TRUE(
        _Compare<GfVec4f>(AiNodeGetArray(node, "test10"), {{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}}));
    param = AiNodeLookUpUserParameter(node, "test10");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGBA);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_VARYING);
}

TEST(HdArnoldSetFaceVaryingPrimvar, FaceVaryingNot32Bit)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetFaceVaryingPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfHalf>{1.0f, 2.0f, 3.0f}}
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        , VtIntArray{}
#endif
 );
    EXPECT_TRUE(_Compare<float>(AiNodeGetArray(node, "test1"), {1.0f, 2.0f, 3.0f}));
    const auto* param = AiNodeLookUpUserParameter(node, "test1");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_FLOAT);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_INDEXED);
    EXPECT_TRUE(_Compare<uint32_t>(AiNodeGetArray(node, "test1idxs"), {0, 1, 2}));
    HdArnoldSetFaceVaryingPrimvar(
        node, TfToken{"test2"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec2h>{{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}}}
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
, VtIntArray{}
#endif
        );
    EXPECT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "test2"), {{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}}));
    param = AiNodeLookUpUserParameter(node, "test2");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR2);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_INDEXED);
    EXPECT_TRUE(_Compare<uint32_t>(AiNodeGetArray(node, "test2idxs"), {0, 1, 2}));
    HdArnoldSetFaceVaryingPrimvar(
        node, TfToken{"test3"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec3h>{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f}}}
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        , VtIntArray{}
#endif
 );
    EXPECT_TRUE(
        _Compare<GfVec3f>(AiNodeGetArray(node, "test3"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f}}));
    param = AiNodeLookUpUserParameter(node, "test3");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_INDEXED);
    EXPECT_TRUE(_Compare<uint32_t>(AiNodeGetArray(node, "test3idxs"), {0, 1, 2}));
    HdArnoldSetFaceVaryingPrimvar(
        node, TfToken{"test4"}, HdPrimvarRoleTokens->color,
        VtValue{VtArray<GfVec3h>{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f}}}
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        , VtIntArray{}
#endif
 );
    EXPECT_TRUE(
        _Compare<GfVec3f>(AiNodeGetArray(node, "test4"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f}}));
    param = AiNodeLookUpUserParameter(node, "test4");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGB);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_INDEXED);
    EXPECT_TRUE(_Compare<uint32_t>(AiNodeGetArray(node, "test4idxs"), {0, 1, 2}));
    HdArnoldSetFaceVaryingPrimvar(
        node, TfToken{"test5"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec4h>{{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}, {9.0f, 10.0f, 11.0f, 12.0f}}}
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        , VtIntArray{}
#endif
 );
    EXPECT_TRUE(_Compare<GfVec4f>(
        AiNodeGetArray(node, "test5"),
        {{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}, {9.0f, 10.0f, 11.0f, 12.0f}}));
    param = AiNodeLookUpUserParameter(node, "test5");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGBA);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_INDEXED);
    EXPECT_TRUE(_Compare<uint32_t>(AiNodeGetArray(node, "test5idxs"), {0, 1, 2}));
    HdArnoldSetFaceVaryingPrimvar(
        node, TfToken{"test6"}, HdPrimvarRoleTokens->none, VtValue{VtArray<double>{1.0, 2.0, 3.0}}
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        , VtIntArray{}
#endif
 );
    EXPECT_TRUE(_Compare<float>(AiNodeGetArray(node, "test6"), {1.0f, 2.0f, 3.0}));
    param = AiNodeLookUpUserParameter(node, "test6");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_FLOAT);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_INDEXED);
    EXPECT_TRUE(_Compare<uint32_t>(AiNodeGetArray(node, "test6idxs"), {0, 1, 2}));
    HdArnoldSetFaceVaryingPrimvar(
        node, TfToken{"test7"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec2d>{{1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}}}
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        , VtIntArray{}
#endif
 );
    EXPECT_TRUE(_Compare<GfVec2f>(AiNodeGetArray(node, "test7"), {{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}}));
    param = AiNodeLookUpUserParameter(node, "test7");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR2);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_INDEXED);
    EXPECT_TRUE(_Compare<uint32_t>(AiNodeGetArray(node, "test7idxs"), {0, 1, 2}));
    HdArnoldSetFaceVaryingPrimvar(
        node, TfToken{"test8"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec3d>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}, {7.0, 8.0, 9.0}}}
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        , VtIntArray{}
#endif
 );
    EXPECT_TRUE(
        _Compare<GfVec3f>(AiNodeGetArray(node, "test8"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f}}));
    param = AiNodeLookUpUserParameter(node, "test8");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_VECTOR);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_INDEXED);
    EXPECT_TRUE(_Compare<uint32_t>(AiNodeGetArray(node, "test8idxs"), {0, 1, 2}));
    HdArnoldSetFaceVaryingPrimvar(
        node, TfToken{"test9"}, HdPrimvarRoleTokens->color,
        VtValue{VtArray<GfVec3d>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}, {7.0, 8.0, 9.0}}}
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        , VtIntArray{}
#endif
 );
    EXPECT_TRUE(
        _Compare<GfVec3f>(AiNodeGetArray(node, "test9"), {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f}}));
    param = AiNodeLookUpUserParameter(node, "test9");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGB);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_INDEXED);
    EXPECT_TRUE(_Compare<uint32_t>(AiNodeGetArray(node, "test9idxs"), {0, 1, 2}));
    HdArnoldSetFaceVaryingPrimvar(
        node, TfToken{"test10"}, HdPrimvarRoleTokens->none,
        VtValue{VtArray<GfVec4d>{{1.0, 2.0, 3.0, 4.0}, {5.0, 6.0, 7.0, 8.0}, {9.0, 10.0, 11.0, 12.0}}}
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        , VtIntArray{}
#endif
 );
    EXPECT_TRUE(_Compare<GfVec4f>(
        AiNodeGetArray(node, "test10"),
        {{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}, {9.0f, 10.0f, 11.0f, 12.0f}}));
    param = AiNodeLookUpUserParameter(node, "test10");
    EXPECT_NE(param, nullptr);
    EXPECT_EQ(AiUserParamGetType(param), AI_TYPE_RGBA);
    EXPECT_EQ(AiUserParamGetCategory(param), AI_USERDEF_INDEXED);
    EXPECT_TRUE(_Compare<uint32_t>(AiNodeGetArray(node, "test10idxs"), {0, 1, 2}));
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    AiBegin();
    AiMsgSetConsoleFlags(nullptr, AI_LOG_NONE);
    auto result = RUN_ALL_TESTS();
    AiEnd();
    return result;
}
