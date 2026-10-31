// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// Performance HUD text model (portable; checked by ui_state_checks). The UI fills
// a HudSnapshot from Worker::Telemetry() plus a few UI counters (pending counts
// are recomputed at most once a second) and draws the formatted text in one
// DirectWrite call, so the HUD stays cheap at frame rate.
#include <cstddef>
#include <cstdint>
#include <string>

namespace quicksift::core::hud {

struct HudSnapshot {
    // Decode lanes (queued) and in flight.
    std::size_t interactive = 0, visible = 0, predictive = 0, idle = 0, analysis = 0;
    std::size_t view = 0, viewPrefetch = 0;
    std::size_t faces = 0, metadata = 0, writes = 0;
    std::size_t activeVisual = 0, activeForeground = 0, metadataInFlight = 0;
    // Pending (UI): metadata reads, photos whose rating / pick state is still unknown.
    std::size_t pendingMetadata = 0, pendingRating = 0, pendingPick = 0;
    std::int64_t oldestQueuedMs = 0;
    std::uint64_t cacheMiB = 0, ramFreeMiB = 0;
    std::uint64_t diskHits = 0, diskMisses = 0;
    double lastRawMs = 0.0, lastJpegMs = 0.0, avgDecodeMs = 0.0, queueWaitMs = 0.0;
    int metadataLaneState = 0; // 0 idle, 1 yielding, 2 reading
    bool metadataWriteBusy = false, fileOperationBusy = false, ratingsWorkerBusy = false,
        batchWorkerBusy = false, workerStopping = false;
    std::wstring blockingReason; // localized; empty when nothing blocks
    std::wstring focus;          // THUMBNAILS / SINGLE VIEW / COMPARE
};

[[nodiscard]] inline int HitRatePercent(std::uint64_t hits, std::uint64_t misses) noexcept {
    const std::uint64_t total = hits + misses;
    return total == 0 ? 0 : static_cast<int>((hits * 100 + total / 2) / total);
}

[[nodiscard]] inline std::wstring FormatMs(double ms) {
    if (!(ms > 0.0)) return L"-";
    return std::to_wstring(static_cast<long long>(ms + 0.5)) + L" ms";
}

[[nodiscard]] inline const wchar_t* LaneStateName(int state) noexcept {
    return state == 2 ? L"reading" : state == 1 ? L"yielding" : L"idle";
}

// Fixed line count (kLines) so the panel height never changes frame to frame.
inline constexpr int kLines = 13;

[[nodiscard]] inline std::wstring FormatHud(const HudSnapshot& s) {
    const auto n = [](std::size_t v) { return std::to_wstring(v); };
    std::wstring t;
    t.reserve(640);
    t += L"PERFORMANCE  " + s.focus + L"\n";
    t += L"Decode Q  I:" + n(s.interactive) + L" V:" + n(s.visible) + L" P:" + n(s.predictive) +
        L" Idle:" + n(s.idle) + L" An:" + n(s.analysis) + L"\n";
    t += L"View Q    cur:" + n(s.view) + L" pre:" + n(s.viewPrefetch) + L"\n";
    t += L"Other Q   meta:" + n(s.metadata) + L" face:" + n(s.faces) + L" write:" + n(s.writes) + L"\n";
    t += L"In flight vis:" + n(s.activeVisual) + L" (fg " + n(s.activeForeground) + L") meta:" +
        n(s.metadataInFlight) + L"\n";
    t += L"Pending   meta:" + n(s.pendingMetadata) + L" rating:" + n(s.pendingRating) +
        L" pick:" + n(s.pendingPick) + L"\n";
    t += L"Oldest    " + (s.oldestQueuedMs > 0 ? std::to_wstring(s.oldestQueuedMs) + L" ms" : std::wstring(L"-")) +
        L"  wait " + FormatMs(s.queueWaitMs) + L"\n";
    t += L"Last dec  RAW " + FormatMs(s.lastRawMs) + L"  JPEG " + FormatMs(s.lastJpegMs) +
        L"  avg " + FormatMs(s.avgDecodeMs) + L"\n";
    t += L"Cache     " + std::to_wstring(s.cacheMiB) + L" MiB  disk hit " +
        std::to_wstring(HitRatePercent(s.diskHits, s.diskMisses)) + L"% (" +
        std::to_wstring(s.diskHits) + L"/" + std::to_wstring(s.diskHits + s.diskMisses) + L")\n";
    t += L"RAM free  " + std::to_wstring(s.ramFreeMiB) + L" MiB\n";
    t += std::wstring(L"Workers   meta:") + LaneStateName(s.metadataLaneState) +
        L" mwrite:" + (s.metadataWriteBusy ? L"busy" : L"idle") +
        L" files:" + (s.fileOperationBusy ? L"busy" : L"idle") + L"\n";
    t += std::wstring(L"          ratings:") + (s.ratingsWorkerBusy ? L"busy" : L"idle") +
        L" batch:" + (s.batchWorkerBusy ? L"busy" : L"idle") + (s.workerStopping ? L" STOPPING" : L"") + L"\n";
    t += L"Blocking  " + (s.blockingReason.empty() ? std::wstring(L"none") : s.blockingReason);
    return t;
}

// Panel geometry (DIPs). The panel is sized from the measured text plus padding and
// a drag-handle strip; (x, y) < 0 means the default top-right corner. Always kept
// fully inside the canvas (resize / DPI change / restored session position).
struct PanelRect { float left = 0, top = 0, right = 0, bottom = 0; };
inline constexpr float kPaddingDip = 10.0f;
inline constexpr float kHandleDip = 14.0f;
inline constexpr float kMarginDip = 12.0f;

[[nodiscard]] inline bool Intersects(const PanelRect& a, const PanelRect& b) noexcept {
    return a.right > a.left && b.right > b.left && a.left < b.right && b.left < a.right &&
        a.top < b.bottom && b.top < a.bottom;
}

// Overlay-aware placement. `content` is the canvas client area minus fixed insets
// (left rail); `obstacles` are overlays/controls drawn over the canvas (search
// field, filmstrip toggle, open pane...). The default (x or y < 0) is the
// top-right corner of `content` BELOW every obstacle that would cover it; any
// position (saved, dragged, clamped) whose panel would put the drag handle or the
// first text line under an obstacle is pushed below it (or above when there is
// no room below), then re-clamped to `content`.
[[nodiscard]] inline PanelRect PlacePanelAvoiding(float x, float y, float textWidth, float textHeight,
    const PanelRect& content, const PanelRect* obstacles, std::size_t obstacleCount) noexcept {
    const float width = textWidth + 2.0f * kPaddingDip;
    const float height = textHeight + 2.0f * kPaddingDip + kHandleDip;
    const auto clampAxis = [](float v, float lo, float hi) noexcept {
        return hi < lo ? lo : (v < lo ? lo : (v > hi ? hi : v));
    };
    float left = x < 0.0f ? content.right - kMarginDip - width : x;
    float top = y < 0.0f ? content.top + kMarginDip : y;
    left = clampAxis(left, content.left, content.right - width);
    top = clampAxis(top, content.top, content.bottom - height);
    // The protected strip: handle + padding + roughly the first text line.
    const float guard = kHandleDip + kPaddingDip * 2.0f + 16.0f;
    for (int pass = 0; pass < 8; ++pass) {
        bool moved = false;
        for (std::size_t i = 0; i < obstacleCount; ++i) {
            const PanelRect head{ left, top, left + width, top + guard };
            if (!Intersects(head, obstacles[i])) continue;
            const float below = obstacles[i].bottom + kMarginDip * 0.5f;
            if (below + height <= content.bottom || below + guard <= content.bottom) {
                top = clampAxis(below, content.top, content.bottom - height);
                if (top < below) top = below; // tall panel: keep the handle free first
            } else {
                // No room below (full-height pane, tall overlay): beside it instead,
                // on whichever side has room (right first).
                const float right = obstacles[i].right + kMarginDip * 0.5f;
                left = right + width <= content.right
                    ? right
                    : clampAxis(obstacles[i].left - kMarginDip * 0.5f - width, content.left, content.right - width);
            }
            moved = true;
        }
        if (!moved) break;
    }
    return { left, top, left + width, top + height };
}

[[nodiscard]] inline PanelRect PlacePanel(float x, float y, float textWidth, float textHeight,
    float canvasWidth, float canvasHeight) noexcept {
    return PlacePanelAvoiding(x, y, textWidth, textHeight,
        PanelRect{ 0.0f, 0.0f, canvasWidth, canvasHeight }, nullptr, 0);
}

// The drag handle: the top strip of the panel (the rest stays click-through).
[[nodiscard]] inline PanelRect HandleRect(const PanelRect& panel) noexcept {
    return { panel.left, panel.top, panel.right, panel.top + kHandleDip + kPaddingDip * 0.5f };
}

[[nodiscard]] inline bool Contains(const PanelRect& r, float x, float y) noexcept {
    return r.right > r.left && x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

} // namespace quicksift::core::hud
