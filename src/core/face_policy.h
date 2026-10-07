// OWNER: Portable deterministic face-anchor ordering policy.
#pragma once

#include "persistent_cache.h"

namespace quicksift::core {

bool FaceAnchorComesBefore(const quicksift::NormalizedFaceRect& left,
    const quicksift::NormalizedFaceRect& right) noexcept;

} // namespace quicksift::core
