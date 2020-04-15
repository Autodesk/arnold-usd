#include <gtest/gtest.h>

#include "translator/utils/utils.h"

#include "translator/writer/registry.h"
#include "translator/writer/write_geometry.h"

#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

TEST(MakeCamelCase, MakeCamelCase)
{
    EXPECT_EQ(MakeCamelCase("camelCase"), "camelCase");
    EXPECT_EQ(MakeCamelCase("snake_case"), "snakeCase");
    EXPECT_EQ(MakeCamelCase("snake__case"), "snakeCase");
}

TEST(TokenizePath, TokenizePath)
{
    std::vector<std::string> tokens;
    TokenizePath("/a;/b", tokens, ";", false);
    const std::vector<std::string> result1 {"/a", "/b"};
    EXPECT_EQ(tokens, result1);
    tokens.clear();
    TokenizePath("/a/b", tokens, ";", false);
    const std::vector<std::string> result2 {"/a/b"};
    EXPECT_EQ(tokens, result2);
}

TEST(UsdArnoldWriterRegistry, UsdArnoldWriterRegistry)
{
    {
        UsdArnoldWriterRegistry registry{true};
        auto* writer = registry.GetPrimWriter("polymesh");
        EXPECT_NE(writer, nullptr);
        EXPECT_NE(dynamic_cast<UsdArnoldWriteMesh*>(writer), nullptr);
    }

    {
        UsdArnoldWriterRegistry registry{false};
        auto* writer = registry.GetPrimWriter("polymesh");
        // A generic arnold writer is registered, instead of the built-in one.
        EXPECT_EQ(dynamic_cast<UsdArnoldWriteMesh*>(writer), nullptr);
    }
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
