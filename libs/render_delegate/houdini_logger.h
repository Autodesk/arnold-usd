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
/// @file houdini_logger.h
///
/// Houdini-only helper, composed into HdArnoldRenderDelegate, that bridges
/// Arnold's AiMsg log and Pixar's Tf diagnostics into HtoA's Houdini Log
/// Viewer panel.
#pragma once

#include <pxr/pxr.h>

#if defined(HOUDINI_LOGGER_BUILD)

#include <pxr/base/tf/diagnosticMgr.h>

#include <ai.h>

#include <mutex>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdArnoldHoudiniLogger
///
/// Small helper, owned (via std::unique_ptr) by HdArnoldRenderDelegate, that
/// forwards Arnold AiMsg log messages and Pixar Tf diagnostics into HtoA's
/// Houdini Log Viewer panel (via the logPanel*Solaris functions declared in
/// the externally supplied <log_bridge_solaris.h>). It is not a render
/// delegate and does not subclass one - it is plain composition, so every
/// DCC (Maya, Katana, Houdini, kick) keeps using the single, shared
/// HdArnoldRenderDelegate class.
///
/// Arnold's message bus (AiMsgRegisterCallback) is global to the process:
/// every registered callback receives every message from every active
/// AtUniverse. Each instance of this helper registers its OWN callback,
/// passing the AtUniverse it was constructed with as the userPtr, and the
/// callback discards any message whose "universe" metadata entry doesn't
/// match. This gives correct per-instance attribution even with several
/// Solaris viewports or renders (and therefore several live AtUniverse) in
/// the same process.
///
/// Pixar's TfDiagnosticMgr has no per-universe concept at all, so the Tf side
/// is registered once and refcounted across however many instances of this
/// class are alive: the first constructed adds the shared delegate, the last
/// destroyed removes it. Tf-sourced messages from concurrent sessions can
/// therefore not be attributed to a specific viewport - this is a known,
/// accepted limitation.
///
/// This header (and houdini_logger.cpp) is compiled only when
/// HOUDINI_LOGGER_BUILD is defined, which only libs/render_delegate's own
/// CMakeLists.txt defines, and only when the BUILD_HOUDINI_LOGGER CMake
/// option (default OFF) is enabled. Vanilla, Maya and Katana builds never
/// compile or link this class, and have zero dependency on the htoa-supplied
/// log_bridge_solaris.h.
class HdArnoldHoudiniLogger {
public:
    explicit HdArnoldHoudiniLogger(AtUniverse* universe);
    ~HdArnoldHoudiniLogger();

    /// This class does not support copying.
    HdArnoldHoudiniLogger(const HdArnoldHoudiniLogger&) = delete;
    /// This class does not support copying.
    HdArnoldHoudiniLogger& operator=(const HdArnoldHoudiniLogger&) = delete;

private:
    /// Arnold AiMsg callback, shared by every instance of this class (Arnold's
    /// message bus is process-wide). Filters on the "universe" metadata entry
    /// so only messages belonging to userPtr's own AtUniverse are forwarded.
    static void _AiMsgCallback(int logmask, int severity, const char* msg, AtParamValueMap* metadata, void* userPtr);

    /// Forwards Pixar Tf diagnostics (TF_WARN, TF_CODING_ERROR, TF_STATUS,
    /// etc.) to the Houdini Log Viewer panel. A single instance of this
    /// delegate (s_tfDelegate below) is shared and refcounted across every
    /// live HdArnoldHoudiniLogger, since TfDiagnosticMgr has no per-universe
    /// concept to filter on.
    class _TfDelegate : public TfDiagnosticMgr::Delegate {
    public:
        void IssueError(const TfError& err) override;
        void IssueFatalError(const TfCallContext& ctx, const std::string& msg) override;
        void IssueWarning(const TfWarning& warning) override;
        void IssueStatus(const TfStatus& status) override;
    };

    /// Increments s_tfDelegateRefCount, adding s_tfDelegate to TfDiagnosticMgr
    /// the first time it goes from 0 to 1. Guarded by s_tfDelegateMutex.
    static void _AddTfDelegate();
    /// Decrements s_tfDelegateRefCount, removing s_tfDelegate from
    /// TfDiagnosticMgr when it drops back to 0. Guarded by s_tfDelegateMutex.
    static void _RemoveTfDelegate();

    /// Handle returned by AiMsgRegisterCallback, passed back to
    /// AiMsgDeregisterCallback on teardown. -1 when not (yet) registered.
    int _aiMsgCallbackId = -1;

    static _TfDelegate s_tfDelegate;
    static std::mutex s_tfDelegateMutex;
    static int s_tfDelegateRefCount;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HOUDINI_LOGGER_BUILD
