#include <gtest/gtest.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/imaging/hd/types.h>

#include "render_delegate/render_buffer.h"

#include <cinttypes>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

const SdfPath path("/a");

template <typename T>
T getSingleValue(HdArnoldRenderBuffer& buffer)
{
    const auto val = *reinterpret_cast<const T*>(buffer.Map());
    buffer.Unmap();
    return val;
}

template <typename T>
void setSingleValue(HdArnoldRenderBuffer& buffer, const T& in)
{
    *reinterpret_cast<T*>(buffer.Map()) = in;
    buffer.Unmap();
}

TEST(HdArnoldRenderBuffer, Allocation)
{
    HdArnoldRenderBuffer buffer(path);
    // Simple allocation test.
    buffer.Allocate({3, 4, 1}, HdFormatUNorm8, false);
    EXPECT_EQ(buffer.GetWidth(), 3);
    EXPECT_EQ(buffer.GetHeight(), 4);
    EXPECT_NE(buffer.Map(), nullptr);
    buffer.Unmap();
    // Testing invalid format allocation.
    buffer.Allocate({32, 32, 1}, HdFormatInvalid, false);
    EXPECT_EQ(buffer.GetWidth(), 0);
    EXPECT_EQ(buffer.GetHeight(), 0);
    EXPECT_EQ(buffer.Map(), nullptr);
    buffer.Unmap();
}

TEST(HdArnoldRenderBuffer, SimpleBucketWrite)
{
    HdArnoldRenderBuffer buffer(path);
    buffer.Allocate({2, 2, 1}, HdFormatUNorm8, false);
    std::vector<uint8_t> data = {4, 3, 2, 1};
    buffer.WriteBucket(0, 0, 2, 2, HdFormatUNorm8, data.data());
    // While we could keep mapped around, because it's guaranteed not to change for now, we won't be doing it.
    const auto* mapped = reinterpret_cast<const uint8_t*>(buffer.Map());
    // Y is flipped with writes.
    EXPECT_EQ(mapped[2], data[0]);
    EXPECT_EQ(mapped[3], data[1]);
    EXPECT_EQ(mapped[0], data[2]);
    EXPECT_EQ(mapped[1], data[3]);
    buffer.Unmap();
    std::vector<uint8_t> data2 = {14, 13, 12, 11};
    // Small bucket writes
    buffer.WriteBucket(0, 0, 1, 1, HdFormatUNorm8, data2.data());
    mapped = reinterpret_cast<const uint8_t*>(buffer.Map());
    EXPECT_EQ(mapped[2], data2[0]);
    EXPECT_EQ(mapped[3], data[1]);
    EXPECT_EQ(mapped[0], data[2]);
    EXPECT_EQ(mapped[1], data[3]);
    buffer.Unmap();
    buffer.WriteBucket(1, 1, 2, 2, HdFormatUNorm8, data2.data());
    mapped = reinterpret_cast<const uint8_t*>(buffer.Map());
    EXPECT_EQ(mapped[2], data2[0]);
    EXPECT_EQ(mapped[3], data[1]);
    EXPECT_EQ(mapped[0], data[2]);
    EXPECT_EQ(mapped[1], data2[0]);
    buffer.Unmap();
}

TEST(HdArnoldRenderBuffer, ConvertingValues)
{
    HdArnoldRenderBuffer buffer(path);
    // We are testing to writing to unorm8, float32 and int32 types for now.
    // Writing to unorm8 types.
    buffer.Allocate({1, 1, 1}, HdFormatUNorm8, false);
    {
        float data = 0.5f;
        buffer.WriteBucket(0, 0, 1, 1, HdFormatFloat32, &data);
        EXPECT_EQ(getSingleValue<uint8_t>(buffer), 127);
    }
    {
        GfVec3f data = {0.8f, 2.0f, 3.0f};
        buffer.WriteBucket(0, 0, 1, 1, HdFormatFloat32Vec3, &data);
        EXPECT_EQ(getSingleValue<uint8_t>(buffer), 204);
    }
    {
        int data = 42;
        buffer.WriteBucket(0, 0, 1, 1, HdFormatInt32, &data);
        EXPECT_EQ(getSingleValue<uint8_t>(buffer), 42);
    }
    buffer.Allocate({1, 1, 1}, HdFormatFloat32Vec3, false);
    setSingleValue(buffer, GfVec3f{2.0f, 4.0f, 6.0f});
    {
        float data = 0.5f;
        buffer.WriteBucket(0, 0, 1, 1, HdFormatFloat32, &data);
        EXPECT_EQ(getSingleValue<GfVec3f>(buffer), GfVec3f(0.5f, 4.0f, 6.0f));
    }
    {
        GfVec3f data = {0.8f, 2.0f, 3.0f};
        buffer.WriteBucket(0, 0, 1, 1, HdFormatFloat32Vec3, &data);
        EXPECT_EQ(getSingleValue<GfVec3f>(buffer), GfVec3f(0.8f, 2.0f, 3.0f));
    }
    {
        int data = 42;
        buffer.WriteBucket(0, 0, 1, 1, HdFormatInt32, &data);
        EXPECT_EQ(getSingleValue<GfVec3f>(buffer), GfVec3f(42.0f, 2.0f, 3.0f));
    }
    buffer.Allocate({1, 1, 1}, HdFormatInt32, false);
    setSingleValue(buffer, 137);
    {
        float data = 20.0f;
        buffer.WriteBucket(0, 0, 1, 1, HdFormatFloat32, &data);
        EXPECT_EQ(getSingleValue<int>(buffer), 20);
    }
    {
        GfVec3f data = {40.0f, 2.0f, 3.0f};
        buffer.WriteBucket(0, 0, 1, 1, HdFormatFloat32Vec3, &data);
        EXPECT_EQ(getSingleValue<int>(buffer), 40);
    }
    {
        int data = 42;
        buffer.WriteBucket(0, 0, 1, 1, HdFormatInt32, &data);
        EXPECT_EQ(getSingleValue<int>(buffer), 42);
    }
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
