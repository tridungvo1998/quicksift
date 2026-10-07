// OWNER: Portable image-view geometry and zoom-intent policy.
#pragma once

#include "core/app_types.h"

namespace quicksift::review::transform {

struct ViewPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct ViewRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct ViewState {
    // Custom mode stores physical display pixels per source pixel. Semantic
    // modes ignore this value and are resolved from the current pane geometry.
    float zoom = 1.0f;
    float centerX = 0.5f;
    float centerY = 0.5f;
    int rotation = 0;
    quicksift::core::ZoomMode mode = quicksift::core::ZoomMode::Fit;
};

struct ViewGeometry {
    ViewRect area{};
    int sourceWidth = 0;
    int sourceHeight = 0;
    float displayScale = 1.0f;
};

struct ResolvedView {
    ViewState state{};
    float fitScaleDip = 1.0f;
    float scaleDip = 1.0f;
    float physicalScale = 1.0f;
    float relativeZoom = 1.0f;
    float drawWidthDip = 1.0f;
    float drawHeightDip = 1.0f;
    float visibleSourceWidth = 1.0f;
    float visibleSourceHeight = 1.0f;
    bool valid = false;
};

ResolvedView ResolveView(const ViewState& state, const ViewGeometry& geometry) noexcept;
ViewState NormalizeView(const ViewState& state, const ViewGeometry& geometry) noexcept;
ViewState PanView(const ViewState& state, const ViewGeometry& geometry,
    ViewPoint screenDelta) noexcept;
ViewState ZoomViewAt(const ViewState& state, const ViewGeometry& geometry,
    ViewPoint screenPoint, float factor) noexcept;
float CustomPhysicalScaleFor(const ViewState& state, const ViewGeometry& geometry) noexcept;
bool IsBeyondFit(const ViewState& state, const ViewGeometry& geometry) noexcept;

} // namespace quicksift::review::transform
