// CODE GUIDE: See CODE_GUIDE.md -> "Logging and crashes".
// OWNER: Shared log-and-continue helper for catch (...) boundaries (round 19).
//
//   try { ... } catch (...) { QS_LOG_AND_CONTINUE(L"Category", L"Site"); }
//
// Writes one Warning line naming the site and the exception text to the
// diagnostic log, then returns so the caller continues. It never throws (even
// under allocation failure) and each call site has its own constant-initialized
// rate limiter, so a failing hot loop logs a short burst and then only counts.
#pragma once

#include "core/log_and_continue.h"
#include "diagnostics/diagnostic_log.h"

#include <chrono>

namespace quicksift::diagnostics {

inline void LogAndContinue(core::LogRateLimiter& limiter, std::wstring_view category,
    std::wstring_view site) noexcept {
    core::log_and_continue::Report(limiter, site, [category](const std::wstring& message) {
        Write(Level::Warning, category, message);
    });
}

} // namespace quicksift::diagnostics

#define QS_LOG_AND_CONTINUE(category, site) \
    do { \
        static ::quicksift::core::LogRateLimiter qsLogAndContinueLimiter_( \
            ::quicksift::core::log_and_continue::kBurstPerSite, \
            std::chrono::milliseconds(::quicksift::core::log_and_continue::kWindowMs)); \
        ::quicksift::diagnostics::LogAndContinue(qsLogAndContinueLimiter_, category, site); \
    } while (false)
