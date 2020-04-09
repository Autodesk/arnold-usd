#include <gtest/gtest.h>

#include "ndr/parser.h"

PXR_NAMESPACE_USING_DIRECTIVE

TEST(NdrArnoldParserPlugin, GetSourceType)
{
    NdrArnoldParserPlugin plugin;
    EXPECT_EQ(plugin.GetSourceType(), TfToken("arnold"));
}

TEST(NdrArnoldParserPlugin, GetDiscoveryTypes)
{
    NdrArnoldParserPlugin plugin;
    EXPECT_EQ(plugin.GetDiscoveryTypes(), NdrTokenVec{TfToken("arnold")});
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
