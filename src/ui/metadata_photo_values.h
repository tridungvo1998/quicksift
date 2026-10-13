// OWNER: PhotoItem <-> PhotoMetadataValues conversion shared by the metadata
// transaction integration and the write-lane controller (UI thread only).
#pragma once

#include "app/quicksift_application_internal.h"

namespace quicksift::app::metadata_values {

inline PhotoMetadataValues ValuesFromPhoto(const PhotoItem& item) {
    return {
        { item.ratingKnowledge, item.rating, item.rawRating },
        { item.colorLabelKnowledge, item.colorLabel, item.rawColorLabel },
        { item.pickStateKnowledge, item.pickState, item.rawPickState }
    };
}

inline void ApplyValuesToPhoto(PhotoItem& item, const PhotoMetadataValues& values) {
    item.rating = values.rating.value;
    item.colorLabel = values.colorLabel.value;
    item.pickState = values.pickState.value;
    item.ratingKnowledge = values.rating.knowledge;
    item.colorLabelKnowledge = values.colorLabel.knowledge;
    item.pickStateKnowledge = values.pickState.knowledge;
    item.rawRating = values.rating.rawValue;
    item.rawColorLabel = values.colorLabel.rawValue;
    item.rawPickState = values.pickState.rawValue;
}

} // namespace quicksift::app::metadata_values
