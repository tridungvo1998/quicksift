// OWNER: Write-lane classification and retry state machine (see write_retry_policy.h).
#include "core/write_retry_policy.h"

#include <algorithm>

namespace quicksift::core::write_lane {
namespace {
// Win32 error numbers (winerror.h), duplicated as constants to stay portable.
constexpr std::uint32_t kErrorAccessDenied = 5;
constexpr std::uint32_t kErrorWriteProtect = 19;
constexpr std::uint32_t kErrorNotReady = 21;
constexpr std::uint32_t kErrorSharingViolation = 32;
constexpr std::uint32_t kErrorLockViolation = 33;
constexpr std::uint32_t kErrorHandleDiskFull = 39;
constexpr std::uint32_t kErrorUnexpectedNetworkError = 59;
constexpr std::uint32_t kErrorNetnameDeleted = 64;
constexpr std::uint32_t kErrorNetworkBusy = 54;
constexpr std::uint32_t kErrorDriveLocked = 108;
constexpr std::uint32_t kErrorDiskFull = 112;
constexpr std::uint32_t kErrorBusy = 170;
constexpr std::uint32_t kErrorUserMappedFile = 1224;
constexpr std::uint32_t kErrorDeletePending = 303;
constexpr std::uint32_t kErrorDiskQuotaExceeded = 1295;
constexpr std::uint32_t kErrorFileReadOnly = 6009;
} // namespace

std::wstring_view FailureKindName(WriteFailureKind kind) noexcept {
    switch (kind) {
    case WriteFailureKind::None: return L"none";
    case WriteFailureKind::InUse: return L"in_use";
    case WriteFailureKind::DeviceBusy: return L"device_busy";
    case WriteFailureKind::AccessDenied: return L"access_denied";
    case WriteFailureKind::Conflict: return L"conflict";
    case WriteFailureKind::LowStorage: return L"low_storage";
    case WriteFailureKind::ReadOnly: return L"read_only";
    case WriteFailureKind::Verification: return L"verification";
    case WriteFailureKind::Unsupported: return L"unsupported";
    case WriteFailureKind::InvalidValue: return L"invalid_value";
    case WriteFailureKind::Other: return L"other";
    }
    return L"other";
}

bool IsTransient(WriteFailureKind kind) noexcept {
    return kind == WriteFailureKind::InUse || kind == WriteFailureKind::DeviceBusy ||
        kind == WriteFailureKind::AccessDenied;
}

WriteFailureKind ClassifyWin32Error(std::uint32_t error) noexcept {
    switch (error) {
    case 0: return WriteFailureKind::None;
    case kErrorSharingViolation:
    case kErrorLockViolation:
    case kErrorUserMappedFile:
    case kErrorDriveLocked:
    case kErrorDeletePending:
        return WriteFailureKind::InUse;
    // Access denied on an existing file opened for DELETE is usually another handle
    // without FILE_SHARE_DELETE (scanner, sync client), but can also be a permanent
    // ACL. Retry it only through the quick phase, like a device hiccup.
    case kErrorAccessDenied:
        return WriteFailureKind::AccessDenied;
    case kErrorNotReady:
    case kErrorUnexpectedNetworkError:
    case kErrorNetnameDeleted:
    case kErrorNetworkBusy:
    case kErrorBusy:
        return WriteFailureKind::DeviceBusy;
    case kErrorDiskFull:
    case kErrorHandleDiskFull:
    case kErrorDiskQuotaExceeded:
        return WriteFailureKind::LowStorage;
    case kErrorWriteProtect:
    case kErrorFileReadOnly:
        return WriteFailureKind::ReadOnly;
    default:
        return WriteFailureKind::Other;
    }
}

std::chrono::milliseconds QuickBackoff(unsigned retryIndex) noexcept {
    const unsigned shift = std::min(retryIndex, 5u);
    const auto delay = std::chrono::milliseconds(50LL << shift);
    return std::min(delay, kMaxQuickDelay);
}

RetryDecision WriteRetryState::OnAttempt(WriteFailureKind outcome,
    std::chrono::milliseconds elapsed) noexcept {
    ++attempts_;
    RetryDecision decision;
    decision.phase = phase_;
    decision.reason = outcome;
    if (outcome == WriteFailureKind::None) {
        decision.action = RetryAction::Done;
        return decision;
    }
    if (!IsTransient(outcome)) {
        decision.action = RetryAction::Abort;
        return decision;
    }
    if (phase_ == RetryPhase::Quick) {
        if (elapsed < kQuickBudget) {
            const auto delay = QuickBackoff(quickRetries_++);
            decision.action = RetryAction::RetryAfter;
            decision.delay = delay;
            return decision;
        }
        // Device hiccups and access denial do not get the long external wait.
        if (outcome != WriteFailureKind::InUse) {
            decision.action = RetryAction::Abort;
            return decision;
        }
        phase_ = RetryPhase::External;
        decision.phase = phase_;
        decision.enteredExternalPhase = true;
    }
    if (outcome != WriteFailureKind::InUse || elapsed >= kExternalBudget) {
        decision.action = RetryAction::Abort;
        return decision;
    }
    decision.action = RetryAction::RetryAfter;
    const auto remaining = kExternalBudget - elapsed;
    decision.delay = std::min<std::chrono::milliseconds>(kExternalInterval, remaining);
    return decision;
}

RetryDecision WriteRetryState::OnCancel() noexcept {
    RetryDecision decision;
    decision.action = RetryAction::Abort;
    decision.phase = phase_;
    decision.reason = WriteFailureKind::InUse;
    return decision;
}

} // namespace quicksift::core::write_lane
