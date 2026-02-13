//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef DIAGNOSTIC_UTILS_H
#define DIAGNOSTIC_UTILS_H

#include <pxr/base/tf/diagnosticMgr.h>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

/// A diagnostic delegate that forwards USD errors and warnings to Arnold's
/// logging system (AiMsgError and AiMsgWarning).
///
/// This class captures diagnostic messages from USD (such as composition
/// errors) and reports them through Arnold's logging functions.
///
/// Example usage:
/// \code
/// ArnoldUsdDiagnostic diagnostic;
/// // USD operations will now be logged through Arnold
/// \endcode
///
class ArnoldUsdDiagnostic : public TfDiagnosticMgr::Delegate
{
public:
    /// Constructor - automatically registers this delegate with TfDiagnosticMgr
    ArnoldUsdDiagnostic();

    /// Destructor - automatically removes this delegate from TfDiagnosticMgr
    virtual ~ArnoldUsdDiagnostic();

    // Delete copy constructor and assignment operator
    ArnoldUsdDiagnostic(const ArnoldUsdDiagnostic&) = delete;
    ArnoldUsdDiagnostic& operator=(const ArnoldUsdDiagnostic&) = delete;

    // TfDiagnosticMgr::Delegate interface overrides    
    void IssueError(const TfError& err) override;
    
    void IssueWarning(const TfWarning& warning) override;
    
    void IssueFatalError(const TfCallContext& ctx, const std::string& msg) override;
    
    void IssueStatus(const TfStatus& status) override;

private:
    /// Helper to format diagnostic message with source location
    std::string _FormatDiagnostic(const TfDiagnosticBase& diagnostic) const;
};

#endif // DIAGNOSTIC_UTILS_H
