#include <gtest/gtest.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/usd/sdf/assetPath.h>

#include "render_delegate/utils.h"

#include <cinttypes>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

TEST(ConvertPrimvarToBuiltinParameter, PrimvarConversion)
{
    auto* node = AiNode("polymesh");
    uint8_t visibility = AI_RAY_ALL;
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:subdiv_iterations"}, VtValue{int{4}}, nullptr, nullptr, nullptr));
    EXPECT_EQ(AiNodeGetInt(node, "subdiv_iterations"), 4);
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:subdiv_iterations"}, VtValue{long{6}}, nullptr, nullptr, nullptr));
    EXPECT_EQ(AiNodeGetInt(node, "subdiv_iterations"), 6);
    EXPECT_FALSE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"subdiv_iterations"}, VtValue{long{12}}, nullptr, nullptr, nullptr));
    EXPECT_EQ(AiNodeGetInt(node, "subdiv_iterations"), 6);
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:subdiv_iterations"}, VtValue{double{16}}, nullptr, nullptr, nullptr));
    EXPECT_EQ(AiNodeGetInt(node, "subdiv_iterations"), 6);
    EXPECT_EQ(AiNodeGetInt(node, "subdiv_type"), 0);
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:subdiv_type"}, VtValue{TfToken{"catclark"}}, nullptr, nullptr, nullptr));
    EXPECT_EQ(AiNodeGetInt(node, "subdiv_type"), 1);
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:subdiv_type"}, VtValue{std::string{"linear"}}, nullptr, nullptr, nullptr));
    EXPECT_EQ(AiNodeGetInt(node, "subdiv_type"), 2);
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:subdiv_type"}, VtValue{long{0}}, nullptr, nullptr, nullptr));
    EXPECT_EQ(AiNodeGetInt(node, "subdiv_type"), 0);
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:subdiv_type"}, VtValue{VtArray<std::string>{std::string{"linear"}}}, nullptr, nullptr,
        nullptr));
    EXPECT_EQ(AiNodeGetInt(node, "subdiv_type"), 2);
}

TEST(ConvertPrimvarToBuiltinParameter, Visibility)
{
    auto* node = AiNode("polymesh");
    HdArnoldRayFlags visibility;
    visibility.SetHydraFlag(AI_RAY_ALL);
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:volume"}, VtValue{false}, &visibility, nullptr, nullptr));
    EXPECT_EQ(visibility.Compose(), AI_RAY_ALL & ~AI_RAY_VOLUME);
    visibility.SetHydraFlag(0);
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:camera"}, VtValue{true}, &visibility, nullptr, nullptr));
    EXPECT_EQ(visibility.Compose(), AI_RAY_CAMERA);
}

TEST(ConvertPrimvarToBuiltinParameter, Sidedness)
{
    auto* node = AiNode("polymesh");
    HdArnoldRayFlags sidedness{AI_RAY_ALL};
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:sidedness:volume"}, VtValue{false}, nullptr, &sidedness, nullptr));
    EXPECT_EQ(AI_RAY_ALL & ~AI_RAY_VOLUME, sidedness.Compose());
}

TEST(ConvertPrimvarToBuiltinParameter, AutobumpVisibility)
{
    auto* node = AiNode("polymesh");
    HdArnoldRayFlags autobumpVisibility{AI_RAY_CAMERA};
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:autobump_visibility:volume"}, VtValue{true}, nullptr, nullptr, &autobumpVisibility));
    EXPECT_EQ(AI_RAY_CAMERA | AI_RAY_VOLUME, autobumpVisibility.Compose());
}

TEST(ConvertPrimvarToBuiltinParameter, VisibilityTypes)
{
    auto* node = AiNode("polymesh");
    HdArnoldRayFlags visibility{0};
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:camera"}, VtValue{true}, &visibility, nullptr, nullptr));
    EXPECT_EQ(AI_RAY_CAMERA, visibility.Compose());
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:camera"}, VtValue{false}, &visibility, nullptr, nullptr));
    EXPECT_EQ(0, visibility.Compose());
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:camera"}, VtValue{int{1}}, &visibility, nullptr, nullptr));
    EXPECT_EQ(AI_RAY_CAMERA, visibility.Compose());
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:camera"}, VtValue{int{0}}, &visibility, nullptr, nullptr));
    EXPECT_EQ(0, visibility.Compose());
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:camera"}, VtValue{long{1}}, &visibility, nullptr, nullptr));
    EXPECT_EQ(AI_RAY_CAMERA, visibility.Compose());
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:camera"}, VtValue{long{0}}, &visibility, nullptr, nullptr));
    EXPECT_EQ(0, visibility.Compose());
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:camera"}, VtValue{double{1}}, &visibility, nullptr, nullptr));
    EXPECT_EQ(0, visibility.Compose());
}

TEST(ConvertPrimvarToBuiltinParameter, CameraFlagNames)
{
    auto* node = AiNode("polymesh");
    HdArnoldRayFlags visibility{0};
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:camera"}, VtValue{true}, &visibility, nullptr, nullptr));
    EXPECT_EQ(AI_RAY_CAMERA, visibility.Compose());
    visibility.ClearPrimvarFlags();
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:shadow"}, VtValue{true}, &visibility, nullptr, nullptr));
    EXPECT_EQ(AI_RAY_SHADOW, visibility.Compose());
    visibility.ClearPrimvarFlags();
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:diffuse_transmit"}, VtValue{true}, &visibility, nullptr, nullptr));
    EXPECT_EQ(AI_RAY_DIFFUSE_TRANSMIT, visibility.Compose());
    visibility.ClearPrimvarFlags();
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:specular_transmit"}, VtValue{true}, &visibility, nullptr, nullptr));
    EXPECT_EQ(AI_RAY_SPECULAR_TRANSMIT, visibility.Compose());
    visibility.ClearPrimvarFlags();
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:diffuse_reflect"}, VtValue{true}, &visibility, nullptr, nullptr));
    EXPECT_EQ(AI_RAY_DIFFUSE_REFLECT, visibility.Compose());
    visibility.ClearPrimvarFlags();
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:specular_reflect"}, VtValue{true}, &visibility, nullptr, nullptr));
    EXPECT_EQ(AI_RAY_SPECULAR_REFLECT, visibility.Compose());
    visibility.ClearPrimvarFlags();
    EXPECT_TRUE(ConvertPrimvarToBuiltinParameter(
        node, TfToken{"arnold:visibility:volume"}, VtValue{true}, &visibility, nullptr, nullptr));
    EXPECT_EQ(AI_RAY_VOLUME, visibility.Compose());
}

TEST(HdArnoldSetParameter, Base)
{
    auto* node = AiNode("standard_surface");
    auto* entry = AiNodeGetNodeEntry(node);
    auto getParam = [&](const char* paramName) -> const AtParamEntry* {
        return AiNodeEntryLookUpParameter(entry, paramName);
    };
    HdArnoldSetParameter(node, getParam("base_color"), VtValue{GfVec3f{0.0f, 2.0f, 0.0}});
    EXPECT_EQ(AiNodeGetRGB(node, "base_color"), AtRGB(0.0f, 2.0f, 0.0f));
    HdArnoldSetParameter(node, getParam("base_color"), VtValue{GfVec2f{0.0f, 4.0f}});
    EXPECT_EQ(AiNodeGetRGB(node, "base_color"), AtRGB(0.0f, 2.0f, 0.0f));
    HdArnoldSetParameter(node, getParam("base"), VtValue{double{2.0}});
    EXPECT_EQ(AiNodeGetFlt(node, "base"), 2.0f);
}

TEST(HdArnoldSetParameter, Matrix4d)
{
    auto* node = AiNode("light_blocker");
    auto* entry = AiNodeGetNodeEntry(node);
    auto getParam = [&](const char* paramName) -> const AtParamEntry* {
        return AiNodeEntryLookUpParameter(entry, paramName);
    };
    HdArnoldSetParameter(
        node, getParam("geometry_matrix"), VtValue{GfMatrix4d{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}});
    EXPECT_EQ(
        AiNodeGetMatrix(node, "geometry_matrix"),
        AtMatrix({{{0, 1, 2, 3}, {4, 5, 6, 7}, {8, 9, 10, 11}, {12, 13, 14, 15}}}));
}

TEST(HdArnoldSetParameter, Array)
{
    auto* node = AiNode("standard_surface");
    auto* entry = AiNodeGetNodeEntry(node);
    auto getParam = [&](const char* paramName) -> const AtParamEntry* {
        return AiNodeEntryLookUpParameter(entry, paramName);
    };
    HdArnoldSetParameter(node, getParam("base"), VtValue{VtArray<double>{2.0}});
    EXPECT_EQ(AiNodeGetFlt(node, "base"), 2.0f);
    HdArnoldSetParameter(node, getParam("base"), VtValue{VtArray<double>{4.0, 2.0}});
    EXPECT_EQ(AiNodeGetFlt(node, "base"), 4.0f);
    HdArnoldSetParameter(node, getParam("base"), VtValue{VtArray<double>{}});
    EXPECT_EQ(AiNodeGetFlt(node, "base"), 4.0f);
    HdArnoldSetParameter(node, getParam("subsurface_type"), VtValue{VtArray<double>{1.0}});
    EXPECT_EQ(AiNodeGetStr(node, "subsurface_type"), AtString("randomwalk"));
    HdArnoldSetParameter(node, getParam("subsurface_type"), VtValue{VtArray<TfToken>{TfToken{"diffusion"}}});
    EXPECT_EQ(AiNodeGetStr(node, "subsurface_type"), AtString("diffusion"));
    HdArnoldSetParameter(node, getParam("subsurface_type"), VtValue{VtArray<std::string>{"randomwalk_v2"}});
    EXPECT_EQ(AiNodeGetStr(node, "subsurface_type"), AtString("randomwalk_v2"));
}

TEST(HdArnoldSetParameter, StringArray)
{
    auto* node = AiNode("polymesh");
    auto* entry = AiNodeGetNodeEntry(node);
    auto* traceSetsEntry = AiNodeEntryLookUpParameter(entry, AtString{"trace_sets"});
    auto compareSets = [&](const std::vector<const char*>& strings) -> bool {
        const auto* arr = AiNodeGetArray(node, AtString{"trace_sets"});
        if (AiArrayGetNumElements(arr) != strings.size()) {
            return false;
        }
        auto id = 0;
        for (auto str : strings) {
            if (AiArrayGetStr(arr, id) != AtString{str}) {
                return false;
            }
            id += 1;
        }
        return true;
    };
    HdArnoldSetParameter(node, traceSetsEntry, VtValue{VtArray<std::string>{"set1"}});
    EXPECT_TRUE(compareSets({"set1"}));
    HdArnoldSetParameter(node, traceSetsEntry, VtValue{VtArray<TfToken>{TfToken{"set1"}, TfToken{"set2"}}});
    EXPECT_TRUE(compareSets({"set1", "set2"}));
    HdArnoldSetParameter(
        node, traceSetsEntry,
        VtValue{VtArray<SdfAssetPath>{SdfAssetPath{"/set1"}, SdfAssetPath{"/set2"}, SdfAssetPath{"/set3"}}});
    EXPECT_TRUE(compareSets({"/set1", "/set2", "/set3"}));
}

TEST(HdArnoldSetParameter, AssetPath)
{
    auto* node = AiNode("image");
    auto* entry = AiNodeGetNodeEntry(node);
    auto* filename = AiNodeEntryLookUpParameter(entry, "filename");
    HdArnoldSetParameter(node, filename, VtValue{SdfAssetPath("first", "second")});
    EXPECT_EQ(AiNodeGetStr(node, "filename"), AtString("second"));
    HdArnoldSetParameter(node, filename, VtValue{SdfAssetPath("first", "")});
    EXPECT_EQ(AiNodeGetStr(node, "filename"), AtString("first"));
    HdArnoldSetParameter(node, filename, VtValue{VtArray<SdfAssetPath>{SdfAssetPath("first", "second")}});
    EXPECT_EQ(AiNodeGetStr(node, "filename"), AtString("second"));
}

TEST(HdArnoldSetParameter, ByteConversions)
{
    auto* node = AiNode("polymesh");
    auto* entry = AiNodeGetNodeEntry(node);
    auto* subdiv_iterations = AiNodeEntryLookUpParameter(entry, "subdiv_iterations");
    uint8_t u8 = 11;
    HdArnoldSetParameter(node, subdiv_iterations, VtValue{u8});
    EXPECT_EQ(AiNodeGetByte(node, "subdiv_iterations"), u8);
    int i = 12;
    HdArnoldSetParameter(node, subdiv_iterations, VtValue{i});
    EXPECT_EQ(AiNodeGetByte(node, "subdiv_iterations"), i);
    unsigned char uc = 13;
    HdArnoldSetParameter(node, subdiv_iterations, VtValue{uc});
    EXPECT_EQ(AiNodeGetByte(node, "subdiv_iterations"), uc);
    long l = 14;
    HdArnoldSetParameter(node, subdiv_iterations, VtValue{l});
    EXPECT_EQ(AiNodeGetByte(node, "subdiv_iterations"), l);
    unsigned int ui = 15;
    HdArnoldSetParameter(node, subdiv_iterations, VtValue{ui});
    EXPECT_EQ(AiNodeGetByte(node, "subdiv_iterations"), ui);
}

TEST(HdArnoldSetParameter, IntConversions)
{
    auto* node = AiNode("standard_surface");
    auto* entry = AiNodeGetNodeEntry(node);
    auto* dielectric_priority = AiNodeEntryLookUpParameter(entry, "dielectric_priority");
    int i = 1;
    HdArnoldSetParameter(node, dielectric_priority, VtValue{i});
    EXPECT_EQ(AiNodeGetInt(node, "dielectric_priority"), i);
    long l = 2;
    HdArnoldSetParameter(node, dielectric_priority, VtValue{l});
    EXPECT_EQ(AiNodeGetInt(node, "dielectric_priority"), l);
    unsigned int ui = 3;
    HdArnoldSetParameter(node, dielectric_priority, VtValue{ui});
    EXPECT_EQ(AiNodeGetInt(node, "dielectric_priority"), ui);
}

TEST(HdArnoldSetParameter, UnsignedIntConversions)
{
    auto* node = AiNode("standard_hair");
    auto* entry = AiNodeGetNodeEntry(node);
    auto* extra_samples = AiNodeEntryLookUpParameter(entry, "extra_samples");
    unsigned int ui = 1;
    HdArnoldSetParameter(node, extra_samples, VtValue{ui});
    EXPECT_EQ(AiNodeGetUInt(node, "extra_samples"), ui);
    int i = 2;
    HdArnoldSetParameter(node, extra_samples, VtValue{i});
    EXPECT_EQ(AiNodeGetUInt(node, "extra_samples"), i);
}

TEST(HdArnoldSetParameter, BoolConversions)
{
    auto* node = AiNode("polymesh");
    auto* entry = AiNodeGetNodeEntry(node);
    auto* matte = AiNodeEntryLookUpParameter(entry, "matte");
    bool b = true;
    HdArnoldSetParameter(node, matte, VtValue{b});
    EXPECT_EQ(AiNodeGetBool(node, "matte"), b);
    int i = 0;
    HdArnoldSetParameter(node, matte, VtValue{i});
    EXPECT_EQ(AiNodeGetBool(node, "matte"), i != 0);
    unsigned int ui = 1;
    HdArnoldSetParameter(node, matte, VtValue{ui});
    EXPECT_EQ(AiNodeGetBool(node, "matte"), ui != 0);
    long l = 0;
    HdArnoldSetParameter(node, matte, VtValue{l});
    EXPECT_EQ(AiNodeGetBool(node, "matte"), l != 0);
}

TEST(HdArnoldSetConstantPrimvar, Base)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"primvar1"}, HdPrimvarRoleTokens->none, VtValue{int{4}}, nullptr, nullptr, nullptr);
    EXPECT_EQ(AiNodeGetInt(node, "primvar1"), 4);
    HdArnoldSetConstantPrimvar(
        node, TfToken{"primvar4"}, HdPrimvarRoleTokens->color, VtValue{GfVec3f{1.0f, 2.0f, 3.0f}}, nullptr, nullptr,
        nullptr);
    EXPECT_NE(AiNodeGetVec(node, "primvar4"), AtVector(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(AiNodeGetRGB(node, "primvar4"), AtRGB(1.0f, 2.0f, 3.0f));
    HdArnoldSetConstantPrimvar(
        node, TfToken{"primvar5"}, HdPrimvarRoleTokens->none, VtValue{GfVec3f{1.0f, 2.0f, 3.0f}}, nullptr, nullptr,
        nullptr);
    EXPECT_EQ(AiNodeGetVec(node, "primvar5"), AtVector(1.0f, 2.0f, 3.0f));
    EXPECT_NE(AiNodeGetRGB(node, "primvar5"), AtRGB(1.0f, 2.0f, 3.0f));
    HdArnoldSetConstantPrimvar(
        node, TfToken{"primvar6"}, HdPrimvarRoleTokens->none, VtValue{VtArray<GfVec3f>{GfVec3f{1.0f, 2.0f, 3.0f}}},
        nullptr, nullptr, nullptr);
    EXPECT_EQ(AiNodeGetVec(node, "primvar6"), AtVector(1.0f, 2.0f, 3.0f));
}

TEST(HdArnoldSetConstantPrimvar, Builtin)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:subdiv_iterations"}, HdPrimvarRoleTokens->none, VtValue{int{4}}, nullptr, nullptr,
        nullptr);
    EXPECT_EQ(AiNodeGetByte(node, "subdiv_iterations"), 4);
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:subdiv_iterations"}, HdPrimvarRoleTokens->none, VtValue{VtArray<int>{8}}, nullptr,
        nullptr, nullptr);
    EXPECT_EQ(AiNodeGetByte(node, "subdiv_iterations"), 8);
    HdArnoldSetConstantPrimvar(
        node, TfToken{"arnold:subdiv_iterations"}, HdPrimvarRoleTokens->none, VtValue{VtArray<long>{12, 16}}, nullptr,
        nullptr, nullptr);
    EXPECT_EQ(AiNodeGetByte(node, "subdiv_iterations"), 12);
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
