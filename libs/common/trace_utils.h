//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef TRACE_UTILS_H
#define TRACE_UTILS_H

/// RAII object that enables USD trace collection on construction and, on
/// destruction, flushes the collected data to Arnold's log via AiMsgInfo and
/// then clears the reporter tree.
///
/// Activated only when the environment variable ARNOLD_USD_TRACE is set to a
/// non-empty value, so there is no overhead in normal operation.
///
/// Example usage (mirrors ArnoldUsdDiagnostic in diagnostic_utils.h):
/// \code
/// void ProceduralReader::Read(...)
/// {
///     ArnoldUsdTraceDiagnostic traceDiagnostic;
///     // ... do work ...
/// }   // <-- destructor prints the trace report here
/// \endcode
///
class ArnoldUsdTraceDiagnostic
{
public:
    ArnoldUsdTraceDiagnostic();
    ~ArnoldUsdTraceDiagnostic();

    ArnoldUsdTraceDiagnostic(const ArnoldUsdTraceDiagnostic&) = delete;
    ArnoldUsdTraceDiagnostic& operator=(const ArnoldUsdTraceDiagnostic&) = delete;

private:
    bool _enabled = false;
};

#endif // TRACE_UTILS_H
