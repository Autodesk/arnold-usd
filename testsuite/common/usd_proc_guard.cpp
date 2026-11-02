// A plugin named like the usd procedural shipped in the Arnold SDK (usd_proc), that installs no node. The testsuite
// appends its folder to ARNOLD_PLUGIN_PATH, after the procedural being tested (see testsuite/SConscript).
// When AiBegin scans a plugin folder, it skips a file named like an already loaded plugin, before opening it. The
// plugin folder of the Arnold SDK is scanned last, so:
//  - if the procedural being tested loads, this plugin is skipped, as well as the SDK's usd_proc.
//  - if it's missing or can't be loaded, this plugin is loaded instead, so the SDK's usd_proc is still skipped: the
//    tests fail, rather than silently testing the procedural shipped in the SDK.
// The interactive USD tests with a static USD only use this plugin, as the procedural is compiled in the test
// program. Not loading the SDK's usd_proc there matters, as it has its own copy of USD, see interactive_usd_test.cpp.
#include <ai.h>

#include <cstring>

node_loader
{
    // Arnold only records a plugin whose loader sets a valid version, even if it installs no node
    strcpy(node->version, AI_VERSION);
    return false;
}
