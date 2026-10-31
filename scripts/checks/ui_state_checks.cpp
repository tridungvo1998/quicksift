// OWNER: Portable checks for round-11 UI state models: blocking-wait dialog text
// (core/blocking_wait.h), early theme persistence (core/presentation_settings.h)
// and the Performance HUD formatter (core/hud_snapshot.h).
//
//   g++ -std=c++20 -O2 -Isrc scripts/checks/ui_state_checks.cpp libQuickSiftCore.a

#include "core/blocking_wait.h"
#include "core/hud_snapshot.h"
#include "core/pane_scroll_policy.h"
#include "core/pane_timing.h"
#include "core/presentation_settings.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace {
int gFailures = 0;
int gChecks = 0;
void Check(bool condition, const char* label) {
    ++gChecks;
    if (!condition) { ++gFailures; std::printf("FAIL: %s\n", label); }
}
} // namespace

int main() {
    namespace bw = quicksift::core::blocking_wait;
    namespace pr = quicksift::core::presentation;
    namespace hud = quicksift::core::hud;
    using namespace std::chrono_literals;
    const bw::Translator identity = [](std::wstring_view k) { return std::wstring(k); };

    Check(bw::FormatElapsed(12000ms) == L"12 s", "elapsed below a minute");
    Check(bw::FormatElapsed(65000ms) == L"1 m 05 s", "elapsed pads seconds");
    Check(bw::FormatElapsed(0ms) == L"0 s", "elapsed zero");

    const std::wstring body = bw::Describe(bw::Command::Close,
        { bw::Blocker{ bw::Reason::FileLockedByOtherApp, L"IMG_1.CR3", 3, 10, 7000ms },
          bw::Blocker{ bw::Reason::UnsavedEdits, L"", 0, 4, 1000ms } }, identity);
    Check(body.find(L"IMG_1.CR3") != std::wstring::npos, "body names the file");
    Check(body.find(L"7 of 10 left") != std::wstring::npos, "body shows how many are left");
    Check(body.find(L"7 s") != std::wstring::npos, "body shows elapsed time");
    Check(body.find(L"locked by another app") != std::wstring::npos, "body shows the reason");
    Check(body.find(L"not saved yet") != std::wstring::npos, "body lists every blocker");
    Check(bw::Describe(bw::Command::Batch, {}, identity) == L"Nothing is blocking any more.", "empty body");
    Check(bw::OffersForceQuit(bw::Command::Close) && !bw::OffersForceQuit(bw::Command::Batch) &&
        !bw::OffersForceQuit(bw::Command::RatingsImport), "force quit only for close");
    Check(bw::ShouldLog(0ms, true) && !bw::ShouldLog(1000ms, false) && bw::ShouldLog(5000ms, false),
        "log cadence");
    Check(bw::CommandLogName(bw::Command::RatingsExport) == L"ratings_export", "stable log names");

    Check(!pr::ThemeFromSetting(0) && pr::ThemeFromSetting(1) && !pr::ThemeFromSetting(7), "theme decode");
    Check(pr::ThemeFromSetting(pr::ThemeToSetting(true)) && !pr::ThemeFromSetting(pr::ThemeToSetting(false)),
        "theme round trip");

    hud::HudSnapshot s;
    s.interactive = 2; s.metadata = 40; s.writes = 3; s.pendingRating = 11; s.pendingPick = 9;
    s.lastRawMs = 812.4; s.lastJpegMs = 41.0; s.diskHits = 3; s.diskMisses = 1; s.cacheMiB = 512;
    s.oldestQueuedMs = 1500; s.metadataLaneState = 1; s.blockingReason = L"X locked"; s.focus = L"THUMBNAILS";
    const std::wstring text = hud::FormatHud(s);
    Check(std::count(text.begin(), text.end(), L'\n') + 1 == hud::kLines, "HUD has a fixed line count");
    const std::wstring empty = hud::FormatHud(hud::HudSnapshot{});
    Check(std::count(empty.begin(), empty.end(), L'\n') + 1 == hud::kLines,
        "HUD line count stable when empty");
    for (const wchar_t* part : { L"I:2", L"meta:40", L"write:3", L"rating:11", L"pick:9", L"RAW 812 ms",
             L"JPEG 41 ms", L"hit 75%", L"512 MiB", L"1500 ms", L"meta:yielding", L"X locked" }) {
        Check(text.find(part) != std::wstring::npos, "HUD shows every requested field");
    }
    Check(hud::HitRatePercent(0, 0) == 0 && hud::HitRatePercent(1, 0) == 100, "hit rate edges");
    Check(hud::FormatMs(0.0) == L"-", "unset decode time");

    // HUD placement: sized from text + padding + handle, default top-right, clamped.
    const hud::PanelRect def = hud::PlacePanel(-1.0f, -1.0f, 300.0f, 200.0f, 1000.0f, 800.0f);
    Check(def.right == 1000.0f - hud::kMarginDip && def.top == hud::kMarginDip, "HUD default top-right");
    Check(def.bottom - def.top == 200.0f + 2.0f * hud::kPaddingDip + hud::kHandleDip &&
        def.right - def.left == 300.0f + 2.0f * hud::kPaddingDip, "HUD sized from measured text");
    const hud::PanelRect off = hud::PlacePanel(5000.0f, 5000.0f, 300.0f, 200.0f, 1000.0f, 800.0f);
    Check(off.right == 1000.0f && off.bottom == 800.0f, "HUD clamped after a shrink / restored position");
    const hud::PanelRect tiny = hud::PlacePanel(50.0f, 50.0f, 300.0f, 200.0f, 100.0f, 100.0f);
    Check(tiny.left == 0.0f && tiny.top == 0.0f, "HUD pinned to origin when the canvas is smaller");
    const hud::PanelRect handle = hud::HandleRect(def);
    Check(hud::Contains(handle, def.left + 5.0f, def.top + 2.0f) &&
        !hud::Contains(handle, def.left + 5.0f, def.bottom - 2.0f), "only the handle strip is interactive");

    // Overlay-aware HUD placement: default and saved positions never leave the
    // drag handle / first line under the search bar or another overlay.
    const hud::PanelRect content{ 25.0f, 0.0f, 1000.0f, 800.0f };
    const hud::PanelRect search{ 600.0f, 10.0f, 990.0f, 62.0f };
    const hud::PanelRect avoided = hud::PlacePanelAvoiding(-1.0f, -1.0f, 300.0f, 200.0f, content, &search, 1);
    Check(avoided.top >= search.bottom && !hud::Intersects(hud::HandleRect(avoided), search),
        "HUD default sits below the search bar");
    Check(avoided.right <= content.right && avoided.bottom <= content.bottom, "HUD default inside content");
    const hud::PanelRect saved = hud::PlacePanelAvoiding(650.0f, 20.0f, 300.0f, 200.0f, content, &search, 1);
    Check(!hud::Intersects(hud::HandleRect(saved), search), "saved HUD position pushed out of the search bar");
    const hud::PanelRect paneObstacle{ 0.0f, 0.0f, 320.0f, 800.0f };
    const hud::PanelRect beside = hud::PlacePanelAvoiding(10.0f, 100.0f, 300.0f, 200.0f, content, &paneObstacle, 1);
    Check(beside.left >= paneObstacle.right && !hud::Intersects(hud::HandleRect(beside), paneObstacle),
        "HUD moved beside a full-height overlay");
    const hud::PanelRect clear = hud::PlacePanelAvoiding(100.0f, 300.0f, 300.0f, 200.0f, content, &search, 1);
    Check(clear.left == 100.0f && clear.top == 300.0f, "free HUD position unchanged");
    const hud::PanelRect leftInset = hud::PlacePanelAvoiding(0.0f, 300.0f, 300.0f, 200.0f, content, nullptr, 0);
    Check(leftInset.left == content.left, "HUD clamped to the content area (rail inset)");

    // Left-pane scrolling geometry.
    namespace pane = quicksift::core::pane;
    Check(pane::MaxScroll(900, 600) == 300 && pane::MaxScroll(500, 600) == 0, "pane max scroll");
    Check(pane::ClampScroll(-5, 900, 600) == 0 && pane::ClampScroll(999, 900, 600) == 300, "pane scroll clamped");
    Check(pane::ClampScroll(250, 500, 600) == 0, "offset reset when content stops overflowing");
    Check(pane::ScrollIntoView(0, 650, 690, 900, 600, 6) == 96, "focused item below the fold scrolled into view");
    Check(pane::ScrollIntoView(300, 100, 140, 900, 600, 6) == 94, "focused item above scrolled into view");
    Check(pane::ScrollIntoView(100, 200, 240, 900, 600, 6) == 100, "visible item does not scroll");
    Check(pane::WheelStep(120, 16, 3) == -48 && pane::WheelStep(-120, 16, 3) == 48, "wheel notch step");
    const pane::Thumb thumb0 = pane::ThumbFor(0, 1200, 600, 0, 600, 24);
    const pane::Thumb thumbEnd = pane::ThumbFor(600, 1200, 600, 0, 600, 24);
    Check(thumb0.top == 0 && thumb0.bottom == 300 && thumbEnd.bottom == 600, "scrollbar thumb geometry");
    Check(pane::ThumbFor(0, 500, 600, 0, 600, 24).bottom == 0, "no scrollbar without overflow");
    Check(pane::Classify(-40, -1, 0, 600) == pane::Visibility::Hidden &&
        pane::Classify(580, 620, 0, 600) == pane::Visibility::Partial &&
        pane::Classify(10, 50, 0, 600) == pane::Visibility::Full, "scrolled child clipped / hidden");

    // Caption truncation detection (drives the overflow tooltip).
    namespace caption = quicksift::core::caption;
    Check(caption::Truncated(200, 150, 12, 0), "long caption truncated");
    Check(!caption::Truncated(126, 150, 12, 0), "exact fit not truncated");
    Check(caption::Truncated(127, 150, 12, 0), "one pixel over truncated");
    Check(caption::Truncated(110, 150, 12, 20), "popup chevron reduces caption width");
    Check(!caption::Truncated(0, 10, 12, 0), "empty caption never truncated");

    // [Pane] timing recorder and log-level decision (first pane paint freeze, round 18).
    namespace timing = quicksift::core::pane_timing;
    timing::Recorder rec;
    rec.Add(L"hide", 0.04);
    rec.Add(L"tabs", 11.4);
    rec.Add(L"compose", 192.6);
    rec.Add(L"tabs", 0.6); // same name accumulates
    Check(rec.Count() == 3 && rec.Ms(L"tabs") > 11.99 && rec.Ms(L"tabs") < 12.01, "segments accumulate by name");
    Check(rec.Slowest() == L"compose", "slowest segment named");
    Check(rec.Format() == L"hide=0.0 tabs=12.0 compose=192.6", "segments formatted in order, one decimal");
    Check(rec.Total() > 204.6 && rec.Total() < 204.7, "segment total");
    rec.Add(L"negative", -3.0);
    Check(rec.Ms(L"negative") == 0.0, "negative durations clamp to zero");
    rec.Clear();
    Check(rec.Count() == 0 && rec.Slowest().empty() && rec.Format().empty(), "cleared recorder is empty");
    for (int i = 0; i < 20; ++i) rec.Add(i % 2 ? L"odd" : L"even", 1.0);
    Check(rec.Count() == 2 && rec.Ms(L"odd") == 10.0, "repeated names do not overflow");
    Check(timing::Decide(true, 0.5) == timing::LogLevel::Info, "first pane open is logged at INFO");
    Check(timing::Decide(false, timing::kSlowPaneMs) == timing::LogLevel::Info, "slow pane step is logged at INFO");
    Check(timing::Decide(false, 12.0) == timing::LogLevel::Trace, "routine pane step stays at TRACE");

    std::printf("ui_state_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
