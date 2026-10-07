// OWNER: Review-subsystem async epoch semantics shared by workers and UI result admission.
#include "review/review_async_policy.h"

namespace quicksift::review {

ResultAdmission ClassifyPostedResult(const quicksift::core::WorkResult& result,
    std::uint64_t currentGeneration, std::uint64_t currentNavigationEpoch,
    std::uint64_t currentThumbnailViewportEpoch, bool thumbnailPathStillDesired) noexcept {
    using quicksift::core::CacheClass;
    if (result.generation != currentGeneration) return ResultAdmission::StaleGeneration;
    if (IsStaleNavigationRequest(result.requestEpoch, currentNavigationEpoch,
        result.kind, false)) return ResultAdmission::StaleNavigation;
    if (result.cacheClass == CacheClass::Thumbnail && IsStaleThumbnailViewport(
        result.viewportEpoch, currentThumbnailViewportEpoch,
        thumbnailPathStillDesired)) return ResultAdmission::StaleThumbnailViewport;
    return ResultAdmission::Accept;
}

} // namespace quicksift::review
