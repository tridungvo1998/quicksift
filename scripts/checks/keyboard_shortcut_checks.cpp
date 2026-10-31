// OWNER: Portable checks for the shortcut table (core/keyboard_shortcuts.h), the
// auto-advance policy and the 0.9.6.0 settings migration.
#include "core/auto_advance_policy.h"
#include "core/keyboard_shortcuts.h"
#include "core/settings_migration.h"

#include <cstdio>

namespace {
int g_checks = 0;
int g_failures = 0;
void Check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) { ++g_failures; std::printf("FAIL: %s\n", name); }
}
using namespace quicksift::core;
namespace sc = quicksift::core::shortcuts;
namespace aa = quicksift::core::auto_advance;
namespace mig = quicksift::core::settings_migration;

bool Is(unsigned key, bool ctrl, bool shift, sc::Action action, int value = -100) {
    const auto m = sc::Find(key, ctrl, shift);
    return m && m->action == action && (value == -100 || m->value == value);
}
} // namespace

int main() {
    // Table integrity: no two dispatched rows with the same key and modifiers.
    for (std::size_t i = 0; i < sc::kShortcuts.size(); ++i) {
        const auto& a = sc::kShortcuts[i];
        Check(!a.keys.empty() && !a.description.empty(), "every row has a key label and description");
        Check(a.firstKey <= a.lastKey, "key ranges are ordered");
        for (std::size_t j = i + 1; j < sc::kShortcuts.size(); ++j) {
            const auto& b = sc::kShortcuts[j];
            if (!a.dispatchedByOnKeyDown || !b.dispatchedByOnKeyDown) continue;
            const bool overlap = a.firstKey <= b.lastKey && b.firstKey <= a.lastKey;
            Check(!(overlap && a.ctrl == b.ctrl && a.shift == b.shift), "no duplicate binding");
        }
    }
    std::size_t total = 0;
    for (const auto section : sc::kSectionOrder) {
        Check(!sc::SectionTitleKey(section).empty(), "every section has a title");
        total += sc::RowsInSection(section);
    }
    Check(total == sc::kShortcuts.size(), "every row belongs to a listed section");

    // Behaviour preserved from the 0.9.5 OnKeyDown switch.
    Check(Is('3', false, false, sc::Action::Rate, 3), "3 rates three stars");
    Check(Is('0', false, false, sc::Action::ClearRating), "0 clears the rating");
    Check(Is('0', true, false, sc::Action::ClearColorLabel), "Ctrl+0 clears the label");
    Check(Is('5', true, false, sc::Action::ColorLabel, 5), "Ctrl+5 purple");
    Check(Is('1', true, false, sc::Action::Rate, 1), "Ctrl+1 still rates (rows without Ctrl ignore it)");
    Check(Is('7', false, false, sc::Action::ColorLabel, 2), "7 yellow");
    Check(Is('P', false, false, sc::Action::Pick) && Is('X', false, false, sc::Action::Reject) &&
        Is('U', false, false, sc::Action::Unmark), "P / X / U");
    Check(Is('Z', true, false, sc::Action::Undo) && Is('Z', false, false, sc::Action::ActualPixels), "Ctrl+Z vs Z");
    Check(Is('R', true, true, sc::Action::RetryFailedLoads) && Is('R', false, false, sc::Action::Rotate),
        "Ctrl+Shift+R vs R");
    Check(Is(sc::kVkF5, false, false, sc::Action::RetryFailedLoads), "F5 retries");
    Check(!sc::Find(sc::kVkCapital, false, false), "Caps Lock is not a command (read live)");
    Check(!sc::Find(sc::kCharQuestion, false, false), "? is handled by WM_CHAR, not OnKeyDown");
    Check(!sc::Find('Q', false, false), "unbound key -> nothing");
    Check(sc::IsCullingEdit(sc::Action::Reject) && !sc::IsCullingEdit(sc::Action::ColorLabel),
        "colour labels do not auto-advance");

    // Auto-advance.
    Check(!aa::Active(false, false) && aa::Active(true, false) && aa::Active(false, true), "setting OR Caps Lock");
    Check(aa::ShowCapsLockIndicator(false, true) && !aa::ShowCapsLockIndicator(true, true),
        "indicator only when Caps Lock alone turns it on");
    const auto next = aa::NextTarget(2, 2, 5);
    Check(next.outcome == aa::Outcome::MoveTo && next.index == 3, "moves to the next photo");
    const auto last = aa::NextTarget(4, 4, 5);
    Check(last.outcome == aa::Outcome::StayAtEnd && last.index == 4, "stays at the last photo");
    const auto filtered = aa::NextTarget(std::nullopt, 2, 4);
    Check(filtered.outcome == aa::Outcome::MoveTo && filtered.index == 2, "filtered out -> photo now in its place");
    const auto filteredLast = aa::NextTarget(std::nullopt, 4, 4);
    Check(filteredLast.outcome == aa::Outcome::StayAtEnd && filteredLast.index == 3, "filtered out at the end");
    Check(aa::NextTarget(std::nullopt, 0, 0).outcome == aa::Outcome::Nothing, "nothing left");

    // Settings migration (RAW JPG-preview default).
    Check(mig::RawPreviewSetting(false, 0, -1).previewOnly && !mig::RawPreviewSetting(false, 0, -1).showNotice,
        "new install -> on, no notice");
    Check(mig::RawPreviewSetting(true, 0, 0).previewOnly && mig::RawPreviewSetting(true, 0, 0).showNotice,
        "old session with the old default -> on + one notice");
    Check(mig::RawPreviewSetting(true, 0, 1).previewOnly && !mig::RawPreviewSetting(true, 0, 1).showNotice,
        "old session already on -> on, no notice");
    Check(!mig::RawPreviewSetting(true, mig::kCurrentSettingsVersion, 0).previewOnly,
        "a 0.9.6 user who turned it off keeps it off");
    Check(mig::ShowFirstRunTip(false) && !mig::ShowFirstRunTip(true), "first-run tip until dismissed");

    std::printf("keyboard_shortcut_checks: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
