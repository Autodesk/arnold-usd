#include <gtest/gtest.h>

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/assetPath.h>

#include "render_delegate/utils.h"

#include <cinttypes>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

std::vector<AtString> _GetStringArray(const AtNode* node, const char* paramName)
{
    auto* arr = AiNodeGetArray(node, paramName);
    if (arr == nullptr) {
        return {};
    }
    const auto numElements = AiArrayGetNumElements(arr);
    if (numElements == 0) {
        return {};
    }
    auto* str = static_cast<AtString*>(AiArrayMap(arr));
    std::vector<AtString> ret(str, str + numElements);
    AiArrayUnmap(arr);
    return ret;
}

TEST(HdArnoldSetConstantPrimvar, SingleString)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetConstantPrimvar(
        node, TfToken{"primvar1"}, HdPrimvarRoleTokens->none, VtValue{std::string{"hello"}}, nullptr, nullptr, nullptr);
    EXPECT_EQ(AiNodeGetStr(node, "primvar1"), AtString{"hello"});
    HdArnoldSetConstantPrimvar(
        node, TfToken{"primvar2"}, HdPrimvarRoleTokens->none, VtValue{TfToken{"world"}}, nullptr, nullptr, nullptr);
    EXPECT_EQ(AiNodeGetStr(node, "primvar2"), AtString{"world"});
    HdArnoldSetConstantPrimvar(
        node, TfToken{"primvar3"}, HdPrimvarRoleTokens->none, VtValue{SdfAssetPath{"mypath"}}, nullptr, nullptr,
        nullptr);
    EXPECT_EQ(AiNodeGetStr(node, "primvar3"), AtString{"mypath"});
}

#define TEST_SET_PRIMVAR_ARRAY_FUNCTIONS(FUNCNAME, ADDITIONAL_PARAMS)                                                \
    TEST(FUNCNAME, StringsArray)                                                                                     \
    {                                                                                                                \
        auto* node = AiNode("polymesh");                                                                             \
        FUNCNAME(                                                                                                    \
            node, TfToken{"primvar1"}, HdPrimvarRoleTokens->none, VtValue {                                          \
                VtArray<std::string>                                                                                 \
                {                                                                                                    \
                    std::string{"hello"}, std::string { "world" }                                                    \
                }                                                                                                    \
            } ADDITIONAL_PARAMS);                                                                                    \
        EXPECT_EQ(_GetStringArray(node, "primvar1"), std::vector<AtString>({AtString{"hello"}, AtString{"world"}})); \
        FUNCNAME(                                                                                                    \
            node, TfToken{"primvar2"}, HdPrimvarRoleTokens->none, VtValue {                                          \
                VtArray<TfToken>                                                                                     \
                {                                                                                                    \
                    TfToken{"hello"}, TfToken { "world" }                                                            \
                }                                                                                                    \
            } ADDITIONAL_PARAMS);                                                                                    \
        EXPECT_EQ(_GetStringArray(node, "primvar2"), std::vector<AtString>({AtString{"hello"}, AtString{"world"}})); \
        FUNCNAME(                                                                                                    \
            node, TfToken{"primvar3"}, HdPrimvarRoleTokens->none, VtValue {                                          \
                VtArray<SdfAssetPath>                                                                                \
                {                                                                                                    \
                    SdfAssetPath{"hello"}, SdfAssetPath { "world" }                                                  \
                }                                                                                                    \
            } ADDITIONAL_PARAMS);                                                                                    \
        EXPECT_EQ(_GetStringArray(node, "primvar3"), std::vector<AtString>({AtString{"hello"}, AtString{"world"}})); \
    }

#define NULLPTR_ADDITONAL_PARAMS , nullptr, nullptr, nullptr
#define EMPTY_ADDITIONAL_PARAMS

TEST_SET_PRIMVAR_ARRAY_FUNCTIONS(HdArnoldSetConstantPrimvar, NULLPTR_ADDITONAL_PARAMS)
TEST_SET_PRIMVAR_ARRAY_FUNCTIONS(HdArnoldSetUniformPrimvar, EMPTY_ADDITIONAL_PARAMS)
TEST_SET_PRIMVAR_ARRAY_FUNCTIONS(HdArnoldSetVertexPrimvar, EMPTY_ADDITIONAL_PARAMS)

TEST(HdArnoldSetInstancePrimvar, StringArray)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetInstancePrimvar(
        node, TfToken{"primvar1"}, HdPrimvarRoleTokens->none, VtIntArray{0, 1, 0},
        VtValue{VtArray<std::string>{std::string{"hello"}, std::string{"world"}}});
    EXPECT_EQ(
        _GetStringArray(node, "instance_primvar1"),
        std::vector<AtString>({AtString{"hello"}, AtString{"world"}, AtString{"hello"}}));
    HdArnoldSetInstancePrimvar(
        node, TfToken{"primvar2"}, HdPrimvarRoleTokens->none, VtIntArray{0, 1, 0},
        VtValue{VtArray<TfToken>{TfToken{"hello"}, TfToken{"world"}}});
    EXPECT_EQ(
        _GetStringArray(node, "instance_primvar2"),
        std::vector<AtString>({AtString{"hello"}, AtString{"world"}, AtString{"hello"}}));
    HdArnoldSetInstancePrimvar(
        node, TfToken{"primvar3"}, HdPrimvarRoleTokens->none, VtIntArray{0, 1, 0},
        VtValue{VtArray<SdfAssetPath>{SdfAssetPath{"hello"}, SdfAssetPath{"world"}}});
    EXPECT_EQ(
        _GetStringArray(node, "instance_primvar3"),
        std::vector<AtString>({AtString{"hello"}, AtString{"world"}, AtString{"hello"}}));
}

TEST(HdArnoldSetInstancePrimvar, InvalidIndex)
{
    auto* node = AiNode("polymesh");
    HdArnoldSetInstancePrimvar(
        node, TfToken{"primvar1"}, HdPrimvarRoleTokens->none, VtIntArray{0, 42, 0},
        VtValue{VtArray<std::string>{std::string{"hello"}, std::string{"world"}}});
    EXPECT_EQ(
        _GetStringArray(node, "instance_primvar1"),
        std::vector<AtString>({AtString{"hello"}, AtString{}, AtString{"hello"}}));
    HdArnoldSetInstancePrimvar(
        node, TfToken{"primvar2"}, HdPrimvarRoleTokens->none, VtIntArray{0, 42, -1337},
        VtValue{VtArray<std::string>{std::string{"hello"}, std::string{"world"}}});
    EXPECT_EQ(
        _GetStringArray(node, "instance_primvar2"), std::vector<AtString>({AtString{"hello"}, AtString{}, AtString{}}));
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
