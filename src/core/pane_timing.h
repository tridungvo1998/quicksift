// OWNER: Left-pane timing records for the diagnostic log ([Pane] / [Startup]).
//
// Header-only and portable (no windows.h) so ui_state_checks can cover it. The
// UI thread fills one Recorder per pane open / section reveal (named segments in
// milliseconds) and logs it once: the first open, the first reveal of each
// section, and anything slower than kSlowPaneMs at INFO; everything else at TRACE.
#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace quicksift::core::pane_timing {

// A pane step slower than this is visible as a hitch; it is always logged at INFO.
inline constexpr double kSlowPaneMs = 50.0;
inline constexpr std::size_t kMaxSegments = 12;

struct Segment {
    std::wstring_view name{};
    double ms = 0.0;
};

class Recorder {
public:
    void Clear() noexcept { count_ = 0; }
    void Add(std::wstring_view name, double ms) noexcept {
        if (ms < 0.0) ms = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            if (segments_[i].name == name) { segments_[i].ms += ms; return; }
        }
        if (count_ < segments_.size()) segments_[count_++] = Segment{ name, ms };
    }
    [[nodiscard]] std::size_t Count() const noexcept { return count_; }
    [[nodiscard]] double Total() const noexcept {
        double total = 0.0;
        for (std::size_t i = 0; i < count_; ++i) total += segments_[i].ms;
        return total;
    }
    [[nodiscard]] double Ms(std::wstring_view name) const noexcept {
        for (std::size_t i = 0; i < count_; ++i) if (segments_[i].name == name) return segments_[i].ms;
        return 0.0;
    }
    [[nodiscard]] std::wstring_view Slowest() const noexcept {
        std::size_t best = count_;
        for (std::size_t i = 0; i < count_; ++i) {
            if (best == count_ || segments_[i].ms > segments_[best].ms) best = i;
        }
        return best == count_ ? std::wstring_view{} : segments_[best].name;
    }
    // "hide=0.1 tabs=0.4 compose=12.3" (one decimal, insertion order).
    [[nodiscard]] std::wstring Format() const {
        std::wstring text;
        for (std::size_t i = 0; i < count_; ++i) {
            if (!text.empty()) text += L' ';
            text.append(segments_[i].name);
            text += L'=';
            text += FormatMs(segments_[i].ms);
        }
        return text;
    }
    [[nodiscard]] static std::wstring FormatMs(double ms) {
        if (ms < 0.0) ms = 0.0;
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%.1f", ms);
        return std::wstring(buffer, buffer + std::char_traits<char>::length(buffer));
    }

private:
    std::array<Segment, kMaxSegments> segments_{};
    std::size_t count_ = 0;
};

enum class LogLevel { Info, Trace };

[[nodiscard]] constexpr LogLevel Decide(bool firstOfItsKind, double totalMs) noexcept {
    return (firstOfItsKind || totalMs >= kSlowPaneMs) ? LogLevel::Info : LogLevel::Trace;
}

} // namespace quicksift::core::pane_timing
