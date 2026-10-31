// OWNER: Portable checks for thumbnail-grid virtualization (visible row range).
#include "review/thumbnail_layout.h"

#include <cstdio>

namespace {
int g_checks = 0;
int g_failures = 0;
void Check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) { ++g_failures; std::printf("FAIL: %s\n", name); }
}
} // namespace

int main() {
    using namespace quicksift::review::thumbnail;
    const ThumbnailLayoutMetrics layout = CalculateThumbnailLayout(1000.0f, 600.0f, 160, 100, 12.0f, 0.0f);
    Check(layout.rows > 0 && layout.cellHeight > 0.0f, "layout has rows");
    const ThumbnailRowRange top = VisibleThumbnailRows(layout, 0.0f, 600.0f);
    Check(!top.Empty() && top.first == 0, "first row visible at scroll 0");
    // Every painted row must intersect the viewport; every intersecting row is painted.
    for (float scroll : { 0.0f, 37.5f, 200.0f, layout.maximumScroll }) {
        const ThumbnailRowRange range = VisibleThumbnailRows(layout, scroll, 600.0f);
        bool exact = !range.Empty();
        for (int row = 0; row < layout.rows; ++row) {
            const float y0 = layout.topInset + row * layout.cellHeight - scroll;
            const float y1 = y0 + layout.cellHeight;
            const bool intersects = y1 > 0.0f && y0 < 600.0f;
            const bool painted = row >= range.first && row <= range.last;
            if (intersects != painted) exact = false;
        }
        Check(exact, "row range == rows intersecting the viewport");
    }
    Check(VisibleThumbnailRows(layout, 0.0f, 0.0f).Empty(), "zero viewport paints nothing");
    ThumbnailLayoutMetrics degenerate = layout;
    degenerate.cellHeight = 0.0f;
    Check(VisibleThumbnailRows(degenerate, 0.0f, 600.0f).Empty(), "zero cell height is safe");
    const ThumbnailLayoutMetrics empty = CalculateThumbnailLayout(1000.0f, 600.0f, 160, 0, 12.0f, 0.0f);
    Check(VisibleThumbnailRows(empty, 0.0f, 600.0f).Empty(), "no items, no rows");
    std::printf("thumbnail_layout_checks: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
