// CODE GUIDE: See CODE_GUIDE.md -> "Logging and crashes".
// OWNER: Portable core of the shared log-and-continue helper used by catch (...)
// boundaries. Never throws; rate-limits per call site so a failing hot path costs
// one atomic operation once its burst is spent.
#pragma once

#include "core/log_rate_limiter.h"

#include <exception>
#include <string>
#include <string_view>

// Marks a catch (...) that must stay silent by design (the guarded statement is
// itself a diagnostic write, or the handler runs inside the logger). The reason is
// a code token so source verifiers can see it.
#define QS_CATCH_SILENT_BY_DESIGN(reason) static_cast<void>(sizeof(reason))

namespace quicksift::core::log_and_continue {

// Burst/window applied to each call site: the first few failures are logged with
// full detail, later duplicates within the window are counted and reported with
// the next allowed message.
inline constexpr unsigned kBurstPerSite = 3;
inline constexpr long long kWindowMs = 30000;

// Describes the exception currently being handled. Must be called inside a catch
// handler; returns a fixed text when there is none or describing it fails.
inline std::string DescribeCurrentException() noexcept {
    try {
        const std::exception_ptr current = std::current_exception();
        if (!current) return "no active exception";
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& error) {
            const char* what = error.what();
            return what && *what ? std::string(what) : std::string("std::exception");
        } catch (...) {
            return "non-standard exception";
        }
    } catch (...) {
        return {};
    }
}

// Widens ASCII/UTF-8 bytes losslessly for ASCII and byte-wise otherwise (exception
// texts are diagnostic only; the platform layer may substitute a real converter).
inline std::wstring WidenForLog(std::string_view text) {
    std::wstring wide;
    wide.reserve(text.size());
    for (const char c : text) wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return wide;
}

// Builds "<site> failed and continued: <what>[ (N similar suppressed)]".
inline std::wstring FormatContinueMessage(std::wstring_view site, std::string_view what, std::uint64_t suppressed) {
    std::wstring message(site);
    message += L" failed and continued: ";
    message += what.empty() ? std::wstring(L"unknown exception") : WidenForLog(what);
    if (suppressed != 0) {
        message += L" (";
        message += std::to_wstring(suppressed);
        message += L" similar suppressed)";
    }
    return message;
}

// Rate-limits, describes the active exception and hands the message to `sink`.
// Returns true when the sink was called. Never throws, whatever `sink` does.
template <class Sink>
bool Report(LogRateLimiter& limiter, std::wstring_view site, Sink&& sink) noexcept {
    std::uint64_t suppressed = 0;
    if (!limiter.Allow(&suppressed)) return false;
    try {
        sink(FormatContinueMessage(site, DescribeCurrentException(), suppressed));
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace quicksift::core::log_and_continue
