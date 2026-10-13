// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Shared domain records passed between core, worker, cache, and UI; do not put behavior here.

#pragma once

#include "decode_budget.h"
#include "face_detection_policy.h"
#include "metadata_state.h"
#include "photo_predicate.h"
#include "../persistent_cache.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace quicksift::core {

struct PhotoItem {
    std::filesystem::path path;
    std::wstring name;
    std::wstring sortName;
    std::wstring extension;
    std::filesystem::file_time_type modified{};
    std::uintmax_t fileSize = 0;
    std::int64_t modifiedStamp = 0;
    std::int64_t sidecarStamp = 0;
    int rating = 0;
    int colorLabel = 0;
    int pickState = 0;
    MetadataKnowledge ratingKnowledge = MetadataKnowledge::Unknown;
    MetadataKnowledge colorLabelKnowledge = MetadataKnowledge::Unknown;
    MetadataKnowledge pickStateKnowledge = MetadataKnowledge::Unknown;
    std::wstring rawRating;
    std::wstring rawColorLabel;
    std::wstring rawPickState;
    std::uint64_t metadataRevision = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    bool facesScanned = false;
    bool faceBlurScanned = false;
    bool faceBlurry = false;          // ANY face flagged (blur, motion blur, eyes closed)
    float faceSharpness = 0.0f;       // primary (default-target) face sharpness score
    std::vector<quicksift::NormalizedFaceRect> faces;   // ordered by core::RankFacesForAnchor
    std::vector<quicksift::FaceQualityRecord> faceQualities; // parallel to faces when analyzed
    // FaceLock target chosen by the user (normalized box, matched to faces by IoU).
    // faceSelectionKnown=false: not yet hydrated from the cache for this session.
    bool faceSelectionKnown = false;
    std::optional<quicksift::NormalizedFaceRect> faceSelection;
    // Search facts (part of the authoritative metadata model, see core/image_metadata.h).
    // Capture time: EXIF DateTimeOriginal, else the file-modified time (local);
    // Unknown until the search-facts worker hydrates it.
    quicksift::core::predicate::LocalDateTime captureTime{};
    quicksift::core::predicate::CaptureTimeSource captureTimeSource =
        quicksift::core::predicate::CaptureTimeSource::Unknown;
    // User comment (comment store); userCommentKnown=false until hydrated.
    std::wstring userComment;
    bool userCommentKnown = false;
    // Camera settings for fast indexed search (0 / empty / -1 = unknown)
    int iso = -1;
    double aperture = 0.0;
    double focalLength = 0.0;
    double shutterSpeed = 0.0;
    std::wstring camera;
    std::wstring lens;
    bool cameraSettingsKnown = false;
};

enum class ViewMode { Thumbnails, Single, Compare };
enum class SortMode { NameAsc, NameDesc, DateNewest, DateOldest, RatingHigh, Extension };
enum class FormatGroup { Jpeg, Png, Heif, Tiff, Basic, WebP, Raw };
enum class DateFilter { Any, Last24Hours, Last7Days, Last30Days, LastYear };
enum class RatingFilter { All, Unrated, Exact1, Exact2, Exact3, Exact4, Exact5, AtLeast4 };
enum class PickFilter { All, Picks, Rejects, Unmarked };
enum class ColorLabelFilter { All, None, Red, Yellow, Green, Blue, Purple };
enum class AppTheme { Dark, Light };
enum class ZoomMode { Fit, FitWidth, FitHeight, ActualPixels, Fill, Custom };
enum class CacheClass { Thumbnail = 0, Preview = 1, Full = 2, Tile = 3 };

enum class JobKind { DecodePreview, DecodeFull, DecodeTile, Metadata, Exif, Face };
// Predictive work sits ahead of face/background analysis, but never ahead of pixels
// already required for the current viewport.
enum class JobPriority { Interactive, Visible, Predictive, Face, Idle, Analysis };
// 0.9.4.0: why a visual decode produced no pixels. Classified on the worker thread
// so the UI retry policy can tell transient I/O trouble (retry with backoff) from
// a deterministic codec rejection (fallback decoder, then a failed placeholder).
enum class DecodeFailureKind { None, Transient, Timeout, Missing, Decode };

struct WorkJob {
    JobKind kind = JobKind::DecodeFull;
    JobPriority priority = JobPriority::Visible;
    CacheClass cacheClass = CacheClass::Preview;
    std::filesystem::path path;
    int targetSize = 0;
    std::uint64_t generation = 0;
    std::uint64_t requestEpoch = 0;
    std::uint64_t viewportEpoch = 0;
    std::uint64_t viewInteractionEpoch = 0;
    std::uint64_t metadataRevision = 0;
    std::chrono::steady_clock::time_point enqueuedAt{};
    std::chrono::steady_clock::time_point startedAt{};
    int tileX = 0;
    int tileY = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int tileLevel = 0;
    bool rawJpegPreviewOnly = false;
    int retryAttempt = 0;
    int timeoutMs = 5000;
    // 0.9.4.0: when true, timeoutMs is provisional (computed on the enqueueing UI
    // thread without touching the file system) and the worker refines it with the
    // real file size when execution starts. Never part of the job identity.
    bool timeoutDeferred = false;
    bool timeoutSingleView = false;
    bool forceWic = false;
    bool ignoreThumbnailViewport = false;
    // 0.9.4.0: Single/Compare decodes run on the dedicated view lane (never on the
    // thumbnail pool). viewPrefetch marks low-priority neighbour prefetch on that
    // lane; it is cancelled by any foreground view request or a new prefetch set.
    bool viewLane = false;
    bool viewPrefetch = false;
    std::wstring key;
};

struct WorkResult {
    JobKind kind = JobKind::DecodeFull;
    JobPriority priority = JobPriority::Visible;
    CacheClass cacheClass = CacheClass::Preview;
    std::filesystem::path path;
    int targetSize = 0;
    std::uint64_t generation = 0;
    std::uint64_t requestEpoch = 0;
    std::uint64_t viewportEpoch = 0;
    std::uint64_t viewInteractionEpoch = 0;
    std::uint64_t metadataRevision = 0;
    int width = 0;
    int height = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    std::uint64_t fileSize = 0;
    std::int64_t modifiedStamp = 0;
    std::int64_t sidecarStamp = 0;
    int rating = 0;
    int colorLabel = 0;
    int pickState = 0;
    MetadataKnowledge ratingKnowledge = MetadataKnowledge::Unknown;
    MetadataKnowledge colorLabelKnowledge = MetadataKnowledge::Unknown;
    MetadataKnowledge pickStateKnowledge = MetadataKnowledge::Unknown;
    std::wstring rawRating;
    std::wstring rawColorLabel;
    std::wstring rawPickState;
    int tileX = 0;
    int tileY = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int tileLevel = 0;
    bool rawJpegPreviewOnly = false;
    int retryAttempt = 0;
    int timeoutMs = 5000;
    bool forceWic = false;
    bool timedOut = false;
    bool previewOnly = false;
    bool fromPersistentCache = false;
    bool facesScanned = false;
    bool faceBlurScanned = false;
    bool faceBlurry = false;
    float faceSharpness = 0.0f;
    bool workFailed = false;
    bool cancelled = false;
    bool viewLane = false;
    DecodeFailureKind failureKind = DecodeFailureKind::None;
    FaceAnalysisOutcome faceAnalysisOutcome = FaceAnalysisOutcome::None;
    std::wstring exifText;
    std::vector<quicksift::NormalizedFaceRect> faces;
    std::vector<quicksift::FaceQualityRecord> faceQualities;
    bool faceSelectionKnown = false;
    std::optional<quicksift::NormalizedFaceRect> faceSelection;
    std::vector<std::uint8_t> pixels;
    std::shared_ptr<DecodeReservation> decodeReservation;
    std::wstring pendingKey;
};

struct BitmapCacheWrite {
    std::filesystem::path path;
    std::uint64_t fileSize = 0;
    std::int64_t modifiedStamp = 0;
    std::int64_t sidecarStamp = 0;
    int targetSize = 0;
    std::uint64_t generation = 0;
    std::uint64_t requestEpoch = 0;
    std::uint64_t viewportEpoch = 0;
    CacheClass cacheClass = CacheClass::Thumbnail;
    JobPriority priority = JobPriority::Visible;
    bool ignoreThumbnailViewport = false;
    std::shared_ptr<const WorkResult> result;
    std::size_t bytes = 0;
    std::uint64_t cacheEpoch = 0;
};

struct RecordCacheWrite {
    std::filesystem::path path;
    quicksift::CachedImageRecord record;
    std::uint64_t cacheEpoch = 0;
};

struct ScanBatch {
    std::uint64_t generation = 0;
    std::vector<PhotoItem> photos;
};

struct AppState {
    ViewMode mode = ViewMode::Thumbnails;
    bool faceLockEnabled = false;
    bool filtersActive = false;
    bool viewportScrolling = false;
    bool mouseOverScrollbar = false;
    int navigationMomentum = 0;
    std::wstring currentFocusKey;
};

} // namespace quicksift::core
