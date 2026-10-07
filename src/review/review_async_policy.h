// OWNER: Review-subsystem async epoch semantics shared by workers and UI result admission.
#pragma once

#include "core/app_types.h"

#include <cstdint>

namespace quicksift::review {

enum class ResultAdmission {
    Accept,
    StaleGeneration,
    StaleNavigation,
    StaleThumbnailViewport,
};

// Navigation cancellation is intentionally stronger inside workers than at the
// UI completion boundary. Face analysis may stop while navigating to save CPU,
// but a face result that already completed remains reusable catalog knowledge.
[[nodiscard]] constexpr bool NavigationInvalidatesWorkerJob(
    quicksift::core::JobKind kind) noexcept {
    using quicksift::core::JobKind;
    return kind == JobKind::DecodePreview || kind == JobKind::DecodeFull ||
        kind == JobKind::DecodeTile || kind == JobKind::Face;
}

[[nodiscard]] constexpr bool NavigationInvalidatesPostedResult(
    quicksift::core::JobKind kind) noexcept {
    using quicksift::core::JobKind;
    return kind == JobKind::DecodePreview || kind == JobKind::DecodeFull ||
        kind == JobKind::DecodeTile;
}

[[nodiscard]] constexpr bool IsStaleNavigationRequest(std::uint64_t requestEpoch,
    std::uint64_t currentEpoch, quicksift::core::JobKind kind,
    bool includeFace) noexcept {
    if (requestEpoch == 0 || requestEpoch >= currentEpoch) return false;
    return includeFace ? NavigationInvalidatesWorkerJob(kind) :
        NavigationInvalidatesPostedResult(kind);
}

[[nodiscard]] constexpr bool IsStaleThumbnailViewport(std::uint64_t requestEpoch,
    std::uint64_t currentEpoch, bool pathStillDesired) noexcept {
    return requestEpoch != 0 && requestEpoch < currentEpoch && !pathStillDesired;
}

[[nodiscard]] ResultAdmission ClassifyPostedResult(
    const quicksift::core::WorkResult& result,
    std::uint64_t currentGeneration,
    std::uint64_t currentNavigationEpoch,
    std::uint64_t currentThumbnailViewportEpoch,
    bool thumbnailPathStillDesired) noexcept;

} // namespace quicksift::review
