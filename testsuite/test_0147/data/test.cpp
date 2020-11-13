#include <gtest/gtest.h>

#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>

#include "render_delegate/utils.h"

#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

using uint_vec = std::vector<unsigned int>;

uint_vec _GetUIntVector(AtArray* arr)
{
    if (AiArrayGetNumElements(arr) == 0) {
        return {};
    }
    const auto* start = static_cast<const int*>(AiArrayMap(arr));
    uint_vec ret(start, start + AiArrayGetNumElements(arr));
    AiArrayUnmap(arr);
    return ret;
}

TEST(HdArnoldGenerateIdxs, ValidCases)
{
    EXPECT_EQ(_GetUIntVector(HdArnoldGenerateIdxs(0)), uint_vec());
    EXPECT_EQ(_GetUIntVector(HdArnoldGenerateIdxs(4)), uint_vec({0, 1, 2, 3}));
    EXPECT_EQ(_GetUIntVector(HdArnoldGenerateIdxs(7)), uint_vec({0, 1, 2, 3, 4, 5, 6}));
    VtIntArray v1({4});
    EXPECT_EQ(_GetUIntVector(HdArnoldGenerateIdxs(4, &v1)),uint_vec({3, 2, 1, 0}));
    VtIntArray v2({2, 2});
    size_t numIdxs = 4;
    EXPECT_EQ(_GetUIntVector(HdArnoldGenerateIdxs(4, &v2)),uint_vec({1, 0, 3, 2}));
    EXPECT_EQ(_GetUIntVector(HdArnoldGenerateIdxs(4, &v1, &numIdxs)),uint_vec({3, 2, 1, 0}));
    EXPECT_EQ(_GetUIntVector(HdArnoldGenerateIdxs(4, &v2, &numIdxs)),uint_vec({1, 0, 3, 2}));
}

TEST(HdArnoldGenerateIdxs, InvalidCases)
{
    VtIntArray v1({2, -1, 2});
    size_t numIdxs1= 4;
    size_t numIdxs2 = 8;
    EXPECT_EQ(_GetUIntVector(HdArnoldGenerateIdxs(4, &v1)),uint_vec({1, 0, 3, 2}));
    EXPECT_EQ(_GetUIntVector(HdArnoldGenerateIdxs(2, &v1, &numIdxs1)),uint_vec());
    EXPECT_EQ(_GetUIntVector(HdArnoldGenerateIdxs(4, &v1, &numIdxs2)),uint_vec());
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
