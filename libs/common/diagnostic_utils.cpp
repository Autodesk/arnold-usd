//
// SPDX-License-Identifier: Apache-2.0
//

#include "diagnostic_utils.h"
#include <ai.h>
#include <pxr/base/tf/diagnostic.h>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

ArnoldUsdDiagnostic::ArnoldUsdDiagnostic()
{
    TfDiagnosticMgr::GetInstance().AddDelegate(this);
}

ArnoldUsdDiagnostic::~ArnoldUsdDiagnostic()
{
    TfDiagnosticMgr::GetInstance().RemoveDelegate(this);
}

std::string
ArnoldUsdDiagnostic::_FormatDiagnostic(const TfDiagnosticBase& diagnostic) const
{
    return diagnostic.GetCommentary();

/*
    For now we skip the file location. We might want to include it in MsgDebug

    std::stringstream ss;
    
    // Include the commentary (main message)
    const std::string& commentary = diagnostic.GetCommentary();
    if (!commentary.empty()) {
        ss << commentary;
    }
    
    // Include source location if available
    const std::string& sourceFileName = diagnostic.GetSourceFileName();
    if (!sourceFileName.empty()) {
        const size_t sourceLineNumber = diagnostic.GetSourceLineNumber();
        ss << " [" << sourceFileName;
        if (sourceLineNumber > 0) {
            ss << ":" << sourceLineNumber;
        }
        ss << "]";
    }
    
    return ss.str();
*/
}

void
ArnoldUsdDiagnostic::IssueError(const TfError& err)
{
    if (!err.GetQuiet()) {
        std::string message = _FormatDiagnostic(err);
        if (!message.empty()) {
            // Note: we don't want to call AiMsgError that
            // would abort renders by default
            AiMsgWarning("[usd] %s", message.c_str());
        }
    }
}

void
ArnoldUsdDiagnostic::IssueWarning(const TfWarning& warning)
{
    if (!warning.GetQuiet()) {
        std::string message = _FormatDiagnostic(warning);
        if (!message.empty()) {
            AiMsgWarning("[usd] %s", message.c_str());
        }
    }
}

void
ArnoldUsdDiagnostic::IssueFatalError(const TfCallContext& ctx, const std::string& msg)
{
    std::stringstream ss;
    ss << msg;
    
    const char* file = ctx.GetFile();
    if (file && file[0] != '\0') {
        ss << " [" << file;
        if (ctx.GetLine() > 0) {
            ss << ":" << ctx.GetLine();
        }
        ss << "]";
    }
    
    AiMsgWarning("[usd] Fatal error: %s", ss.str().c_str());
}

void
ArnoldUsdDiagnostic::IssueStatus(const TfStatus& status)
{
    // Status messages are typically informational and not errors/warnings
    // We can choose to ignore them or log them as info
    // For now, we'll just ignore them to avoid clutter
}
