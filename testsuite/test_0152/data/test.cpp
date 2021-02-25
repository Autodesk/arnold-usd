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

TEST(HdArnoldSetConstantPrimvar, HalfColorBuiltin)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"color"}, HdPrimvarRoleTokens->color, VtValue{GfVec4h{1.0f, 2.0f, 3.0f, 4.0f}});
    EXPECT_EQ(AiNodeGetRGBA(node, "color"), AtRGBA(1.0f, 2.0f, 3.0f, 4.0f));
    node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"color"}, HdPrimvarRoleTokens->color, VtValue{VtVec4fArray{GfVec4h{2.0f, 3.0f, 4.0f, 5.0f}}});
    EXPECT_EQ(AiNodeGetRGBA(node, "color"), AtRGBA(2.0f, 3.0f, 4.0f, 5.0f));
    node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(node, TfToken{"color"}, HdPrimvarRoleTokens->color, VtValue{VtVec4hArray{}});
    EXPECT_EQ(AiNodeGetRGBA(node, "color"), AtRGBA(0.0f, 0.0f, 0.0f, 0.0f));
}

TEST(HdArnoldSetConstantPrimvar, DoubleColorBuiltin)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"color"}, HdPrimvarRoleTokens->color, VtValue{GfVec4d{1.0f, 2.0f, 3.0f, 4.0f}});
    EXPECT_EQ(AiNodeGetRGBA(node, "color"), AtRGBA(1.0f, 2.0f, 3.0f, 4.0f));
    node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"color"}, HdPrimvarRoleTokens->color, VtValue{VtVec4dArray{GfVec4d{2.0f, 3.0f, 4.0f, 5.0f}}});
    EXPECT_EQ(AiNodeGetRGBA(node, "color"), AtRGBA(2.0f, 3.0f, 4.0f, 5.0f));
    node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(node, TfToken{"color"}, HdPrimvarRoleTokens->color, VtValue{VtVec4dArray{}});
    EXPECT_EQ(AiNodeGetRGBA(node, "color"), AtRGBA(0.0f, 0.0f, 0.0f, 0.0f));
}

TEST(HdArnoldSetConstantPrimvar, Half)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{GfHalf{2.0f}});
    EXPECT_EQ(AiNodeGetFlt(node, "test1"), 2.0f);
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:subdiv_adaptive_error"}, HdPrimvarRoleTokens->none, VtValue{GfHalf{0.5f}});
    EXPECT_EQ(AiNodeGetFlt(node, "subdiv_adaptive_error"), 0.5f);
}

TEST(HdArnoldSetConstantPrimvar, Double)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{2.0});
    EXPECT_EQ(AiNodeGetFlt(node, "test1"), 2.0f);
    HdArnoldSetConstantPrimvar(node, TfToken{"arnold:subdiv_adaptive_error"}, HdPrimvarRoleTokens->none, VtValue{0.5});
    EXPECT_EQ(AiNodeGetFlt(node, "subdiv_adaptive_error"), 0.5f);
}

TEST(HdArnoldSetConstantPrimvar, Half2)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{GfVec2h{1.0f, 2.0f}});
    EXPECT_EQ(AiNodeGetVec2(node, "test1"), AtVector2(1.0f, 2.0f));
    node = AiNode("image");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:uvcoords"}, HdPrimvarRoleTokens->none, VtValue{GfVec2h{2.0f, 3.0f}});
    EXPECT_EQ(AiNodeGetVec2(node, "uvcoords"), AtVector2(2.0f, 3.0f));
}

TEST(HdArnoldSetConstantPrimvar, Double2)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{GfVec2d{1.0f, 2.0f}});
    EXPECT_EQ(AiNodeGetVec2(node, "test1"), AtVector2(1.0f, 2.0f));
    node = AiNode("image");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:uvcoords"}, HdPrimvarRoleTokens->none, VtValue{GfVec2d{2.0f, 3.0f}});
    EXPECT_EQ(AiNodeGetVec2(node, "uvcoords"), AtVector2(2.0f, 3.0f));
}

TEST(HdArnoldSetConstantPrimvar, Half3)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{GfVec3h{1.0f, 2.0f, 3.0f}});
    EXPECT_EQ(AiNodeGetVec(node, "test1"), AtVector(1.0f, 2.0f, 3.0f));
    node = AiNode("noise");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:scale"}, HdPrimvarRoleTokens->none, VtValue{GfVec3h{2.0f, 3.0f, 4.0f}});
    EXPECT_EQ(AiNodeGetVec(node, "scale"), AtVector(2.0f, 3.0f, 4.0f));
}

TEST(HdArnoldSetConstantPrimvar, Double3)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(node, TfToken{"test1"}, HdPrimvarRoleTokens->none, VtValue{GfVec3d{1.0f, 2.0f, 3.0f}});
    EXPECT_EQ(AiNodeGetVec(node, "test1"), AtVector(1.0f, 2.0f, 3.0f));
    node = AiNode("noise");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:scale"}, HdPrimvarRoleTokens->none, VtValue{GfVec3d{2.0f, 3.0f, 4.0f}});
    EXPECT_EQ(AiNodeGetVec(node, "scale"), AtVector(2.0f, 3.0f, 4.0f));
}

TEST(HdArnoldSetConstantPrimvar, ColorHalf3)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(node, TfToken{"test1"}, HdPrimvarRoleTokens->color, VtValue{GfVec3h{1.0f, 2.0f, 3.0f}});
    EXPECT_EQ(AiNodeGetRGB(node, "test1"), AtRGB(1.0f, 2.0f, 3.0f));
    node = AiNode("noise");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:color1"}, HdPrimvarRoleTokens->color, VtValue{GfVec3h{2.0f, 3.0f, 4.0f}});
    EXPECT_EQ(AiNodeGetRGB(node, "color1"), AtRGB(2.0f, 3.0f, 4.0f));
}

TEST(HdArnoldSetConstantPrimvar, ColorDouble3)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(node, TfToken{"test1"}, HdPrimvarRoleTokens->color, VtValue{GfVec3d{1.0f, 2.0f, 3.0f}});
    EXPECT_EQ(AiNodeGetRGB(node, "test1"), AtRGB(1.0f, 2.0f, 3.0f));
    node = AiNode("noise");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:color1"}, HdPrimvarRoleTokens->color, VtValue{GfVec3d{2.0f, 3.0f, 4.0f}});
    EXPECT_EQ(AiNodeGetRGB(node, "color1"), AtRGB(2.0f, 3.0f, 4.0f));
}

TEST(HdArnoldSetConstantPrimvar, ColorHalf4)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->color, VtValue{GfVec4h{1.0f, 2.0f, 3.0f, 4.0f}});
    EXPECT_EQ(AiNodeGetRGBA(node, "test1"), AtRGBA(1.0f, 2.0f, 3.0f, 4.0f));
    node = AiNode("image");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:missing_texture_color"}, HdPrimvarRoleTokens->color,
        VtValue{GfVec4h{2.0f, 3.0f, 4.0f, 5.0f}});
    EXPECT_EQ(AiNodeGetRGBA(node, "missing_texture_color"), AtRGBA(2.0f, 3.0f, 4.0f, 5.0f));
}

TEST(HdArnoldSetConstantPrimvar, ColorDouble4)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"test1"}, HdPrimvarRoleTokens->color, VtValue{GfVec4d{1.0f, 2.0f, 3.0f, 4.0f}});
    EXPECT_EQ(AiNodeGetRGBA(node, "test1"), AtRGBA(1.0f, 2.0f, 3.0f, 4.0f));
    node = AiNode("image");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:missing_texture_color"}, HdPrimvarRoleTokens->color,
        VtValue{GfVec4d{2.0f, 3.0f, 4.0f, 5.0f}});
    EXPECT_EQ(AiNodeGetRGBA(node, "missing_texture_color"), AtRGBA(2.0f, 3.0f, 4.0f, 5.0f));
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
