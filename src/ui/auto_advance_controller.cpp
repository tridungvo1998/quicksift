// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Keyboard culling edits (1-5, 0, P, X, U) and the auto-advance that may
// follow them. The rule (when, where to, last photo, filters) is portable in
// core/auto_advance_policy.h; this file only reads Caps Lock and moves the view.
#include "app/quicksift_application_internal.h"
#include "core/auto_advance_policy.h"

namespace quicksift::app {

bool QuickSiftApplicationImpl::AutoAdvanceActive() const noexcept {
    return quicksift::core::auto_advance::Active(autoAdvance_, (GetKeyState(VK_CAPITAL) & 1) != 0);
}

void QuickSiftApplicationImpl::ApplyCullingShortcut(quicksift::core::shortcuts::Action action, int value) {
    namespace sc = quicksift::core::shortcuts;
    namespace aa = quicksift::core::auto_advance;
    const ViewMode mode = reviewState_.Mode();
    // Only a single-photo edit in Single View or Thumbnails advances; Compare and
    // multi-selection edits stay where they are.
    const bool mayAdvance = AutoAdvanceActive() && !catalog_.VisibleEmpty() &&
        (mode == ViewMode::Single || mode == ViewMode::Thumbnails) && ActivePaths().size() <= 1;
    const std::wstring path = mayAdvance ? reviewState_.ActivePath() : std::wstring{};
    const std::optional<size_t> before = mayAdvance ? IndexForPath(path) : std::nullopt;

    switch (action) {
    case sc::Action::Rate: ApplyRating(value); break;
    case sc::Action::ClearRating: ApplyRating(0); break;
    case sc::Action::Pick: ApplyPickState(1); break;
    case sc::Action::Reject: ApplyPickState(-1); break;
    case sc::Action::Unmark: ApplyPickState(0); break;
    default: return;
    }
    if (!before || path.empty()) return;

    const aa::Target target = aa::NextTarget(IndexForPath(path), *before, catalog_.VisibleCount());
    if (target.outcome == aa::Outcome::StayAtEnd) {
        ShowToast(L"Last photo — auto-advance stopped here.");
        return;
    }
    if (target.outcome != aa::Outcome::MoveTo) return;
    if (mode == ViewMode::Single) {
        NavigateToSingleIndex(target.index, 1, false);
    } else {
        SelectIndex(target.index, false);
        EnsureFocusedThumbnailVisible(target.index);
    }
    UpdateColorLabelButton();
    UpdateStatus();
    InvalidateCanvas();
}

} // namespace quicksift::app
