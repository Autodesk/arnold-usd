//
// SPDX-License-Identifier: Apache-2.0
//

#include "trace_utils.h"

#include <ai.h>

#include <pxr/base/trace/collector.h>
#include <pxr/base/trace/reporter.h>

#include <cstdlib>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

ArnoldUsdTraceDiagnostic::ArnoldUsdTraceDiagnostic()
{
    const char *envVar = std::getenv("ARNOLD_USD_TRACE");
    if (envVar == nullptr || envVar[0] == '\0')
        return;

    _enabled = true;
    TraceCollector::GetInstance().SetEnabled(true);
}

ArnoldUsdTraceDiagnostic::~ArnoldUsdTraceDiagnostic()
{
    if (!_enabled)
        return;

    TraceCollector::GetInstance().SetEnabled(false);

    TraceReporterPtr reporter = TraceReporter::GetGlobalReporter();

    std::ostringstream ss;
    reporter->ReportTimes(ss);
    const std::string report = ss.str();
    if (!report.empty()) {
        AiMsgInfo("[usd] Trace report:\n%s", report.c_str());
    }

    reporter->ClearTree();
    TraceCollector::GetInstance().Clear();
}
