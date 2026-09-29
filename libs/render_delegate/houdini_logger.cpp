//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2026 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "houdini_logger.h"

#if defined(HOUDINI_LOGGER_BUILD)

#include <pxr/base/tf/diagnostic.h>

#include <log_bridge_solaris.h>

#include <sstream>

PXR_NAMESPACE_OPEN_SCOPE

HdArnoldHoudiniLogger::_TfDelegate HdArnoldHoudiniLogger::s_tfDelegate;
std::mutex HdArnoldHoudiniLogger::s_tfDelegateMutex;
int HdArnoldHoudiniLogger::s_tfDelegateRefCount = 0;

namespace {

// Formats a source location as "file:line", or an empty string when no file
// name is available (mirrors the pattern already used by
// libs/common/diagnostic_utils.cpp for the equivalent Tf->AiMsg bridge).
std::string _FormatFileLine(const char* file, size_t line)
{
    if (file == nullptr || file[0] == '\0') {
        return std::string();
    }
    std::stringstream ss;
    ss << file;
    if (line > 0) {
        ss << ":" << line;
    }
    return ss.str();
}

std::string _FormatFileLine(const TfDiagnosticBase& diagnostic)
{
    return _FormatFileLine(diagnostic.GetSourceFileName().c_str(), diagnostic.GetSourceLineNumber());
}

} // namespace

HdArnoldHoudiniLogger::HdArnoldHoudiniLogger(AtUniverse* universe)
{
    // Registered with the given universe (the owning delegate's own Arnold
    // universe, be it owned or provided) as userPtr, so _AiMsgCallback can
    // tell this instance's messages apart from every other universe's, since
    // AiMsgRegisterCallback is process-wide, not per-universe.
    //
    // The mask excludes AI_LOG_PROGRESS and AI_LOG_STATS, which are
    // high-frequency/render-statistics-only messages not meant for a log
    // panel (AI_LOG_STATUS, used for the narrower status-only callback in
    // render_param.cpp, is already excluded from AI_LOG_ALL).
    _aiMsgCallbackId = static_cast<int>(
        AiMsgRegisterCallback(_AiMsgCallback, AI_LOG_ALL & ~(AI_LOG_PROGRESS | AI_LOG_STATS), universe));

    _AddTfDelegate();
}

HdArnoldHoudiniLogger::~HdArnoldHoudiniLogger()
{
    if (_aiMsgCallbackId >= 0) {
        AiMsgDeregisterCallback(static_cast<unsigned int>(_aiMsgCallbackId));
        _aiMsgCallbackId = -1;
    }

    _RemoveTfDelegate();
}

void HdArnoldHoudiniLogger::_AiMsgCallback(
    int logmask, int severity, const char* msg, AtParamValueMap* metadata, void* userPtr)
{
    if (msg == nullptr || userPtr == nullptr) {
        return;
    }

    // Every callback registered via AiMsgRegisterCallback receives messages
    // from every active AtUniverse in the process. The "universe" metadata
    // entry is the only way to tell which universe a message belongs to, so
    // we discard anything that isn't ours (userPtr is this instance's
    // universe, passed in at registration time).
    void* messageUniverse = nullptr;
    if (metadata == nullptr || !AiParamValueMapGetPtr(metadata, AtString("universe"), &messageUniverse) ||
        messageUniverse != userPtr) {
        return;
    }

    // AI_LOG_DEBUG is a message category (part of logmask), not a severity:
    // debug messages are otherwise reported with AI_SEVERITY_INFO, so we have
    // to check the category first to route them to the debug panel.
    if (logmask & AI_LOG_DEBUG) {
        logPanelDebugSolaris(msg, "");
        return;
    }

    switch (severity) {
        case AI_SEVERITY_WARNING:
            logPanelWarningSolaris(msg, "");
            break;
        case AI_SEVERITY_ERROR:
            logPanelErrorSolaris(msg, "");
            break;
        case AI_SEVERITY_FATAL:
            logPanelFatalSolaris(msg, "");
            break;
        case AI_SEVERITY_INFO:
        default:
            logPanelInfoSolaris(msg, "");
            break;
    }
}

void HdArnoldHoudiniLogger::_AddTfDelegate()
{
    std::lock_guard<std::mutex> lock(s_tfDelegateMutex);
    if (s_tfDelegateRefCount++ == 0) {
        TfDiagnosticMgr::GetInstance().AddDelegate(&s_tfDelegate);
    }
}

void HdArnoldHoudiniLogger::_RemoveTfDelegate()
{
    std::lock_guard<std::mutex> lock(s_tfDelegateMutex);
    if (--s_tfDelegateRefCount == 0) {
        TfDiagnosticMgr::GetInstance().RemoveDelegate(&s_tfDelegate);
    }
}

void HdArnoldHoudiniLogger::_TfDelegate::IssueError(const TfError& err)
{
    if (err.GetQuiet()) {
        return;
    }
    const std::string message = err.GetCommentary();
    if (!message.empty()) {
        logPanelErrorSolaris(message.c_str(), _FormatFileLine(err).c_str());
    }
}

void HdArnoldHoudiniLogger::_TfDelegate::IssueFatalError(const TfCallContext& ctx, const std::string& msg)
{
    // Kept minimal: the process aborts shortly after a fatal error, so this
    // must not do anything that could re-enter Tf/Arnold or block.
    logPanelFatalSolaris(msg.c_str(), _FormatFileLine(ctx.GetFile(), static_cast<size_t>(ctx.GetLine())).c_str());
}

void HdArnoldHoudiniLogger::_TfDelegate::IssueWarning(const TfWarning& warning)
{
    if (warning.GetQuiet()) {
        return;
    }
    const std::string message = warning.GetCommentary();
    if (!message.empty()) {
        logPanelWarningSolaris(message.c_str(), _FormatFileLine(warning).c_str());
    }
}

void HdArnoldHoudiniLogger::_TfDelegate::IssueStatus(const TfStatus& status)
{
    const std::string message = status.GetCommentary();
    if (!message.empty()) {
        logPanelInfoSolaris(message.c_str(), _FormatFileLine(status).c_str());
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HOUDINI_LOGGER_BUILD
