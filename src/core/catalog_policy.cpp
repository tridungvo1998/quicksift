// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Catalog filter/sort implementation; this is the single owner of those rules.

#include "catalog_policy.h"

namespace quicksift::core {

static_assert(static_cast<int>(RatingFilter::Unrated) == 1 && static_cast<int>(RatingFilter::Exact1) == 2 &&
    static_cast<int>(RatingFilter::Exact5) == 6 && static_cast<int>(RatingFilter::AtLeast4) == 7,
    "FilterCriteria::ratingFilter mirrors RatingFilter");
static_assert(static_cast<int>(PickFilter::Picks) == 1 && static_cast<int>(PickFilter::Rejects) == 2 &&
    static_cast<int>(PickFilter::Unmarked) == 3, "FilterCriteria::pickFilter mirrors PickFilter");
static_assert(static_cast<int>(ColorLabelFilter::None) == 1 && static_cast<int>(ColorLabelFilter::Red) == 2 &&
    static_cast<int>(ColorLabelFilter::Purple) == 6, "FilterCriteria::colorFilter mirrors ColorLabelFilter");
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
    // The Filter tab is expressed in the shared predicate structure (the same clauses
    // the search box produces) and evaluated by the one predicate engine. Semantics are
    // unchanged: an active rating/pick/label filter requires Known knowledge.
    return MatchesFilter(FactsOf(item), FilterCriteriaFor(rating, pick, label));
}

predicate::FilterCriteria FilterCriteriaFor(RatingFilter rating, PickFilter pick,
    ColorLabelFilter label) noexcept {
    return { static_cast<int>(rating), static_cast<int>(pick), static_cast<int>(label) };
}

bool MatchesFilter(const predicate::MetadataFactsView& facts,
    const predicate::FilterCriteria& criteria) noexcept {
    if (criteria.ratingFilter == 0 && criteria.pickFilter == 0 && criteria.colorFilter == 0) return true;
    try {
        const predicate::FilterClauses clauses = predicate::CompileFilterCriteria(criteria);
        return predicate::MatchesAll(clauses.View(), facts);
    } catch (...) {
        return false; // rating/pick/color clauses never allocate; defensive only
    }
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
