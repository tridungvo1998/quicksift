// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Session-file versioning and the defaults that changed between versions
// (portable; checked by scripts/checks/keyboard_shortcut_checks.cpp).
//
// SettingsVersion was added in 0.9.6.0 (value 2). A session without it was
// written by 0.9.5.0 or older, which always saved LoadOnlyRawJpegPreviews=0
// because that was the default, so "never changed" and "turned off" cannot be
// told apart. Decision: such sessions move to the new default (JPG previews on)
// once, and QuickSift shows a one-time notice pointing at Settings -> RAW
// handling. Sessions with SettingsVersion >= 2 keep whatever the user chose.
#pragma once

namespace quicksift::core::settings_migration {

inline constexpr int kCurrentSettingsVersion = 2;
inline constexpr int kNoSettingsVersion = 0; // key absent (pre-0.9.6.0 or new install)

struct RawPreviewDecision {
    bool previewOnly = true;
    bool showNotice = false;
};

// `hasSession`: a session file existed; `storedValue`: LoadOnlyRawJpegPreviews
// (-1 when absent).
[[nodiscard]] constexpr RawPreviewDecision RawPreviewSetting(bool hasSession, int settingsVersion,
    int storedValue) noexcept {
    if (!hasSession || storedValue < 0) return { true, false };       // new install / key absent
    if (settingsVersion >= kCurrentSettingsVersion) return { storedValue != 0, false };
    // Older session: 1 stays on silently; 0 was the old default -> new default + notice.
    if (storedValue != 0) return { true, false };
    return { true, true };
}

// First-run tip: shown until dismissed; reset from Settings.
[[nodiscard]] constexpr bool ShowFirstRunTip(bool dismissed) noexcept { return !dismissed; }

} // namespace quicksift::core::settings_migration
