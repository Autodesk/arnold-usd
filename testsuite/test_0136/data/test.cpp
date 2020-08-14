#include <gtest/gtest.h>

#include <pxr/base/vt/value.h>

#include "render_delegate/utils.h"

#include <ai.h>

#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

TEST(HdArnoldSetRadiusFromValue, SettingSingleRadiusValue)
{
    auto* curves = AiNode("curves");
    // Setting from float.
    HdArnoldSetRadiusFromValue(curves, VtValue{2.0f});
    EXPECT_EQ(1.0f, AiNodeGetFlt(curves, "radius"));
    // Setting from double.
    HdArnoldSetRadiusFromValue(curves, VtValue{4.0});
    EXPECT_EQ(2.0f, AiNodeGetFlt(curves, "radius"));
    // Setting from int should not work.
    HdArnoldSetRadiusFromValue(curves, VtValue{8});
    EXPECT_EQ(2.0f, AiNodeGetFlt(curves, "radius"));
    // Setting from VtFloatArray.
    HdArnoldSetRadiusFromValue(curves, VtValue{VtFloatArray{8.0f}});
    EXPECT_EQ(4.0f, AiNodeGetFlt(curves, "radius"));
    // Setting from VtDoubleArray.
    HdArnoldSetRadiusFromValue(curves, VtValue{VtFloatArray{16.0f}});
    EXPECT_EQ(8.0f, AiNodeGetFlt(curves, "radius"));
}

TEST(HdArnoldSetRadiusFromValue, SettingMultipleRadius)
{
    auto* curves = AiNode("curves");
    auto getRadii = [&] () -> std::vector<float> {
        // No need to be efficient here.
        std::vector<float> ret;
        auto* arr = AiNodeGetArray(curves, "radius");
        const auto numElements = AiArrayGetNumElements(arr);
        for (auto i = decltype(numElements){0}; i < numElements; i += 1) {
            ret.push_back(AiArrayGetFlt(arr, i));
        }
        return ret;
    };
    // Using VtFloatArray.
    HdArnoldSetRadiusFromValue(curves, VtValue{VtFloatArray{2.0f, 4.0f, 8.0f, 16.0f}});
    EXPECT_EQ(std::vector<float>({1.0f, 2.0f, 4.0f, 8.0f}), getRadii());
    // Using VtDoubleArray.
    HdArnoldSetRadiusFromValue(curves, VtValue{VtDoubleArray{4.0, 8.0, 16.0, 32.0}});
    EXPECT_EQ(std::vector<float>({2.0f, 4.0f, 8.0f, 16.0f}), getRadii());
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
