// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Catalog filter/sort implementation; this is the single owner of those rules.

#include "catalog_policy.h"

namespace quicksift::core {
bool IsFormatAllowed(std::wstring_view extension, const FormatSelection& selection) noexcept {
    if (IsJpegExtension(extension) &&
        selection.IsEnabled(FormatGroup::Jpeg)) return true;
    if (extension == L".png" && selection.IsEnabled(FormatGroup::Png)) return true;
    if ((extension == L".avif" || extension == L".heic" || extension == L".heif") &&
        selection.IsEnabled(FormatGroup::Heif)) return true;
    if ((extension == L".tif" || extension == L".tiff") &&
        selection.IsEnabled(FormatGroup::Tiff)) return true;
    if ((extension == L".bmp" || extension == L".dib" || extension == L".gif") &&
        selection.IsEnabled(FormatGroup::Basic)) return true;
    if (extension == L".webp" && selection.IsEnabled(FormatGroup::WebP)) return true;
    return IsCameraRawExtension(extension) && selection.IsEnabled(FormatGroup::Raw);
}

bool IsDateAllowed(std::filesystem::file_time_type modified, DateFilter filter,
    std::filesystem::file_time_type now) noexcept {
    if (filter == DateFilter::Any || modified >= now) return true;

    // Widen before subtracting. Filesystem clocks commonly use a signed 64-bit
    // count, so subtracting hostile min/max timestamps directly can overflow.
    using WideDuration = std::chrono::duration<long double,
        std::filesystem::file_time_type::duration::period>;
    const long double ageTicks = WideDuration(now.time_since_epoch()).count() -
        WideDuration(modified.time_since_epoch()).count();
    const auto within = [ageTicks](std::chrono::hours limit) noexcept {
        return ageTicks <= WideDuration(limit).count();
    };
    switch (filter) {
    case DateFilter::Last24Hours: return within(std::chrono::hours(24));
    case DateFilter::Last7Days: return within(std::chrono::hours(24 * 7));
    case DateFilter::Last30Days: return within(std::chrono::hours(24 * 30));
    case DateFilter::LastYear: return within(std::chrono::hours(24 * 365));
    case DateFilter::Any: return true;
    }
    return true;
}

bool IsCullAllowed(const PhotoItem& item, RatingFilter rating, PickFilter pick,
    ColorLabelFilter label) noexcept {
    if (pick != PickFilter::All) {
        if (item.pickStateKnowledge != MetadataKnowledge::Known) return false;
        switch (pick) {
        case PickFilter::Picks: if (item.pickState <= 0) return false; break;
        case PickFilter::Rejects: if (item.pickState >= 0) return false; break;
        case PickFilter::Unmarked: if (item.pickState != 0) return false; break;
        case PickFilter::All: break;
        }
    }

    const int expectedLabel = [&]() noexcept {
        switch (label) {
        case ColorLabelFilter::None: return 0;
        case ColorLabelFilter::Red: return 1;
        case ColorLabelFilter::Yellow: return 2;
        case ColorLabelFilter::Green: return 3;
        case ColorLabelFilter::Blue: return 4;
        case ColorLabelFilter::Purple: return 5;
        case ColorLabelFilter::All: return -1;
        }
        return -1;
    }();
    if (expectedLabel >= 0) {
        if (item.colorLabelKnowledge != MetadataKnowledge::Known) return false;
        if (item.colorLabel != expectedLabel) return false;
    }

    if (rating != RatingFilter::All &&
        item.ratingKnowledge != MetadataKnowledge::Known) return false;
    switch (rating) {
    case RatingFilter::Unrated: return item.rating == 0;
    case RatingFilter::Exact1: return item.rating == 1;
    case RatingFilter::Exact2: return item.rating == 2;
    case RatingFilter::Exact3: return item.rating == 3;
    case RatingFilter::Exact4: return item.rating == 4;
    case RatingFilter::Exact5: return item.rating == 5;
    case RatingFilter::AtLeast4: return item.rating >= 4;
    case RatingFilter::All: return true;
    }
    return true;
}

bool PhotoSortLess(const PhotoItem& left, const PhotoItem& right, SortMode mode) noexcept {
    switch (mode) {
    case SortMode::NameAsc: return left.sortName < right.sortName;
    case SortMode::NameDesc: return left.sortName > right.sortName;
    case SortMode::DateNewest: return left.modified > right.modified;
    case SortMode::DateOldest: return left.modified < right.modified;
    case SortMode::RatingHigh: {
        const bool leftKnown = left.ratingKnowledge == MetadataKnowledge::Known;
        const bool rightKnown = right.ratingKnowledge == MetadataKnowledge::Known;
        if (leftKnown != rightKnown) return leftKnown;
        if (leftKnown && left.rating != right.rating) return left.rating > right.rating;
        if (left.ratingKnowledge != right.ratingKnowledge)
            return static_cast<int>(left.ratingKnowledge) < static_cast<int>(right.ratingKnowledge);
        return left.sortName < right.sortName;
    }
    case SortMode::Extension:
        if (left.extension != right.extension) return left.extension < right.extension;
        return left.sortName < right.sortName;
    }
    return false;
}

} // namespace quicksift::core
