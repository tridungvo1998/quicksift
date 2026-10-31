// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// Session "Theme" value (0 = Dark, 1 = Light). Shared by the early pre-window read
// and LoadSession so both decode it identically (checked by ui_state_checks).
namespace quicksift::core::presentation {
[[nodiscard]] constexpr bool ThemeFromSetting(int stored) noexcept { return stored == 1; }
[[nodiscard]] constexpr int ThemeToSetting(bool light) noexcept { return light ? 1 : 0; }
} // namespace quicksift::core::presentation
