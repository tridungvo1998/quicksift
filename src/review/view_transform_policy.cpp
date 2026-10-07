// OWNER: Portable image-view geometry and zoom-intent policy.
#include "review/view_transform_policy.h"

#include <algorithm>
#include <cmath>

namespace quicksift::review::transform {
namespace {

float ClampFinite(float value, float minimum, float maximum, float fallback) noexcept {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, minimum, maximum);
}

int NormalizedRotation(int rotation) noexcept {
    rotation %= 360;
    if (rotation < 0) rotation += 360;
    return ((rotation + 45) / 90 * 90) % 360;
}

ViewPoint InverseRotate(ViewPoint point, int rotation) noexcept {
    switch (NormalizedRotation(rotation)) {
    case 90: return { point.y, -point.x };
    case 180: return { -point.x, -point.y };
    case 270: return { -point.y, point.x };
    default: return point;
    }
}

float AreaWidth(const ViewGeometry& geometry) noexcept {
    return std::max(1.0f, geometry.area.right - geometry.area.left);
}

float AreaHeight(const ViewGeometry& geometry) noexcept {
    return std::max(1.0f, geometry.area.bottom - geometry.area.top);
}

float ResolveScaleDip(const ViewState& state, const ViewGeometry& geometry,
    float orientedWidth, float orientedHeight, float fitScaleDip) noexcept {
    const float areaWidth = AreaWidth(geometry);
    const float areaHeight = AreaHeight(geometry);
    const float displayScale = std::max(0.01f, geometry.displayScale);
    switch (state.mode) {
    case quicksift::core::ZoomMode::Fit:
        return fitScaleDip;
    case quicksift::core::ZoomMode::FitWidth:
        return areaWidth / orientedWidth;
    case quicksift::core::ZoomMode::FitHeight:
        return areaHeight / orientedHeight;
    case quicksift::core::ZoomMode::ActualPixels:
        return 1.0f / displayScale;
    case quicksift::core::ZoomMode::Fill:
        return std::max(areaWidth / orientedWidth, areaHeight / orientedHeight);
    case quicksift::core::ZoomMode::Custom: {
        const float fitPhysical = std::max(0.0001f, fitScaleDip * displayScale);
        // Custom is an absolute physical magnification. Clamp only to a broad
        // numerical safety range here so synchronized panes keep exactly the
        // same display-pixels-per-source-pixel value.
        const float physical = ClampFinite(state.zoom, 0.0001f, 128.0f, fitPhysical);
        return physical / displayScale;
    }
    }
    return fitScaleDip;
}

} // namespace

ResolvedView ResolveView(const ViewState& input, const ViewGeometry& geometry) noexcept {
    ResolvedView result;
    result.state = input;
    result.state.rotation = NormalizedRotation(input.rotation);
    result.state.centerX = ClampFinite(input.centerX, 0.0f, 1.0f, 0.5f);
    result.state.centerY = ClampFinite(input.centerY, 0.0f, 1.0f, 0.5f);

    if (geometry.sourceWidth <= 0 || geometry.sourceHeight <= 0) {
        result.state.centerX = 0.5f;
        result.state.centerY = 0.5f;
        result.physicalScale = input.mode == quicksift::core::ZoomMode::ActualPixels ? 1.0f :
            std::max(0.0001f, input.zoom);
        return result;
    }

    const float sourceWidth = static_cast<float>(geometry.sourceWidth);
    const float sourceHeight = static_cast<float>(geometry.sourceHeight);
    const bool quarterTurn = ((result.state.rotation / 90) & 1) != 0;
    const float orientedWidth = quarterTurn ? sourceHeight : sourceWidth;
    const float orientedHeight = quarterTurn ? sourceWidth : sourceHeight;
    const float areaWidth = AreaWidth(geometry);
    const float areaHeight = AreaHeight(geometry);
    result.fitScaleDip = std::max(0.0001f,
        std::min(areaWidth / orientedWidth, areaHeight / orientedHeight));
    result.scaleDip = std::max(0.0001f,
        ResolveScaleDip(result.state, geometry, orientedWidth, orientedHeight, result.fitScaleDip));
    result.physicalScale = result.scaleDip * std::max(0.01f, geometry.displayScale);
    if (result.state.mode == quicksift::core::ZoomMode::Custom) {
        result.state.zoom = result.physicalScale;
    }
    result.relativeZoom = result.scaleDip / result.fitScaleDip;
    result.drawWidthDip = sourceWidth * result.scaleDip;
    result.drawHeightDip = sourceHeight * result.scaleDip;

    if (quarterTurn) {
        result.visibleSourceWidth = areaHeight / result.drawWidthDip;
        result.visibleSourceHeight = areaWidth / result.drawHeightDip;
    } else {
        result.visibleSourceWidth = areaWidth / result.drawWidthDip;
        result.visibleSourceHeight = areaHeight / result.drawHeightDip;
    }
    result.visibleSourceWidth = std::max(0.0f, result.visibleSourceWidth);
    result.visibleSourceHeight = std::max(0.0f, result.visibleSourceHeight);

    if (result.visibleSourceWidth >= 1.0f) {
        result.state.centerX = 0.5f;
    } else {
        const float half = result.visibleSourceWidth * 0.5f;
        result.state.centerX = std::clamp(result.state.centerX, half, 1.0f - half);
    }
    if (result.visibleSourceHeight >= 1.0f) {
        result.state.centerY = 0.5f;
    } else {
        const float half = result.visibleSourceHeight * 0.5f;
        result.state.centerY = std::clamp(result.state.centerY, half, 1.0f - half);
    }
    result.valid = true;
    return result;
}

ViewState NormalizeView(const ViewState& state, const ViewGeometry& geometry) noexcept {
    return ResolveView(state, geometry).state;
}

float CustomPhysicalScaleFor(const ViewState& state, const ViewGeometry& geometry) noexcept {
    return ResolveView(state, geometry).physicalScale;
}

bool IsBeyondFit(const ViewState& state, const ViewGeometry& geometry) noexcept {
    const ResolvedView resolved = ResolveView(state, geometry);
    return resolved.valid && resolved.relativeZoom > 1.001f;
}

ViewState PanView(const ViewState& input, const ViewGeometry& geometry,
    ViewPoint screenDelta) noexcept {
    const ResolvedView resolved = ResolveView(input, geometry);
    if (!resolved.valid) return input;
    const ViewPoint sourceAxes = InverseRotate(screenDelta, resolved.state.rotation);
    ViewState output = resolved.state;
    output.mode = quicksift::core::ZoomMode::Custom;
    output.zoom = resolved.physicalScale;
    output.centerX -= sourceAxes.x / resolved.drawWidthDip;
    output.centerY -= sourceAxes.y / resolved.drawHeightDip;
    return NormalizeView(output, geometry);
}

ViewState ZoomViewAt(const ViewState& input, const ViewGeometry& geometry,
    ViewPoint screenPoint, float factor) noexcept {
    const ResolvedView oldView = ResolveView(input, geometry);
    if (!oldView.valid || !std::isfinite(factor) || factor <= 0.0f) return input;

    const float areaCenterX = (geometry.area.left + geometry.area.right) * 0.5f;
    const float areaCenterY = (geometry.area.top + geometry.area.bottom) * 0.5f;
    const ViewPoint fromCenter = InverseRotate(
        { screenPoint.x - areaCenterX, screenPoint.y - areaCenterY }, oldView.state.rotation);
    const float sourcePointX = oldView.state.centerX + fromCenter.x / oldView.drawWidthDip;
    const float sourcePointY = oldView.state.centerY + fromCenter.y / oldView.drawHeightDip;

    ViewState output = oldView.state;
    output.mode = quicksift::core::ZoomMode::Custom;
    const float fitPhysical = oldView.fitScaleDip * std::max(0.01f, geometry.displayScale);
    output.zoom = ClampFinite(oldView.physicalScale * factor,
        std::max(0.0001f, fitPhysical * 0.05f),
        std::max(0.0001f, fitPhysical * 32.0f), oldView.physicalScale);

    const ResolvedView newView = ResolveView(output, geometry);
    output.centerX = sourcePointX - fromCenter.x / newView.drawWidthDip;
    output.centerY = sourcePointY - fromCenter.y / newView.drawHeightDip;
    return NormalizeView(output, geometry);
}

} // namespace quicksift::review::transform
