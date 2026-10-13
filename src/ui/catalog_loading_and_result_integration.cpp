// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"
#include "diagnostics/log_categories.h"
#include "core/log_rate_limiter.h"

#include <cmath>
namespace quicksift::app {

bool QuickSiftApplicationImpl::HasRecentDecodeFailure(const std::wstring& key) noexcept {
    const auto it = failedDecodes_.find(key);
    if (it == failedDecodes_.end()) return false;
    const auto now = std::chrono::steady_clock::now();
    if (now - it->second >= kDecodeRetryBackoff) {
        failedDecodes_.erase(it);
        return false;
    }
    return true;
}

void QuickSiftApplicationImpl::RememberTransientDecodeFailure(const std::wstring& key) noexcept {
    if (key.empty()) return;
    if (failedDecodes_.size() >= failedDecodeLimit_ && !failedDecodes_.empty()) {
        const size_t target = failedDecodeLimit_ * 3 / 4;
        while (failedDecodes_.size() > target) failedDecodes_.erase(failedDecodes_.begin());
    }
    failedDecodes_[key] = std::chrono::steady_clock::now();
    // Re-evaluate the visual queue just after the transient retry backoff expires.
    // The timer is one-shot, so this does not create a continuous polling loop.
    StartUiTimer(ID_TIMER_IDLE_QUALITY, 260, L"retry-after-backoff");
}

// ---------------------------------------------------------------------------------
// 0.9.4.0 decode retry policy (thumbnails and the Single/Compare view lane).
//
//  * At most kMaxDecodeAttempts (3) worker attempts per path. The worker escalates the
//    per-attempt timeout (base, 2x, 4x - Worker::CalculateAdaptiveTimeoutMs).
//  * Short backoff between attempts (150 ms, then 600 ms) instead of an immediate
//    re-enqueue that hits the same locked file again.
//  * Transient failures (sharing/lock violation, network/device error, zero-length
//    file still being written) and timeouts use all attempts; the last attempt also
//    switches to the WIC -> Shell thumbnail fallback chain.
//  * Deterministic failures (file readable, every codec rejected it) skip straight to
//    the fallback attempt; a missing file is terminal immediately.
//  * The failed placeholder is shown only after the final attempt (terminal). A
//    terminal entry resets automatically when the file's size/mtime changes, on folder
//    reload, or via manual retry (View > Retry failed images / Ctrl+Shift+R). A
//    transient terminal failure also re-arms itself after 30 s while visible.
// ---------------------------------------------------------------------------------
namespace {
constexpr auto kDecodeRetryFirstBackoff = std::chrono::milliseconds(150);
constexpr auto kDecodeRetrySecondBackoff = std::chrono::milliseconds(600);
constexpr auto kTransientTerminalRearm = std::chrono::seconds(30);

bool IsTransientFailureKind(DecodeFailureKind kind) noexcept {
    return kind == DecodeFailureKind::Transient || kind == DecodeFailureKind::Timeout ||
        kind == DecodeFailureKind::None;
}

const wchar_t* FailureKindName(DecodeFailureKind kind) noexcept {
    switch (kind) {
    case DecodeFailureKind::None: return L"unknown";
    case DecodeFailureKind::Transient: return L"transient";
    case DecodeFailureKind::Timeout: return L"timeout";
    case DecodeFailureKind::Missing: return L"missing";
    case DecodeFailureKind::Decode: return L"decode";
    }
    return L"unknown";
}

template <typename State>
bool FileChangedSinceFailure(const State& state, std::uint64_t fileSize, std::int64_t modifiedStamp) noexcept {
    if (state.fileSize == 0 && state.modifiedStamp == 0) return false;
    if (fileSize == 0 && modifiedStamp == 0) return false;
    return (fileSize != 0 && state.fileSize != 0 && fileSize != state.fileSize) ||
        (modifiedStamp != 0 && state.modifiedStamp != 0 && modifiedStamp != state.modifiedStamp);
}
} // namespace

bool QuickSiftApplicationImpl::ThumbnailDecodeAdmissible(const PhotoItem& photo,
    const std::wstring& key, std::chrono::steady_clock::time_point now,
    int& retryAttempt, bool& forceWic) {
    retryAttempt = 0;
    forceWic = false;
    const auto it = thumbnailRetryState_.find(key);
    if (it == thumbnailRetryState_.end()) {
        // A terminal mark without state (legacy path) is honoured as-is.
        return !failedThumbnailPaths_.contains(key);
    }
    DecodeRetryState& state = it->second;
    if (FileChangedSinceFailure(state, photo.fileSize, photo.modifiedStamp)) {
        // The file was rewritten by another app: forget the failure entirely.
        thumbnailRetryState_.erase(it);
        failedThumbnailPaths_.erase(key);
        return true;
    }
    if (state.terminal) {
        if (IsTransientFailureKind(state.lastFailure) && now >= state.nextRetryAt) {
            thumbnailRetryState_.erase(it);
            failedThumbnailPaths_.erase(key);
            return true;
        }
        return false;
    }
    if (now < state.nextRetryAt) return false;
    retryAttempt = std::clamp(state.failedAttempts, 0, kMaxDecodeAttempts - 1);
    forceWic = state.lastFailure == DecodeFailureKind::Decode ||
        retryAttempt >= kMaxDecodeAttempts - 1;
    return true;
}

void QuickSiftApplicationImpl::RecordThumbnailDecodeFailure(const WorkResult& result) {
    const std::wstring key = NormalizedPathKey(result.path);
    const auto now = std::chrono::steady_clock::now();
    DecodeRetryState& state = thumbnailRetryState_[key];
    state.failedAttempts = std::max(state.failedAttempts + 1, result.retryAttempt + 1);
    DecodeFailureKind kind = result.timedOut ? DecodeFailureKind::Timeout : result.failureKind;
    if (kind == DecodeFailureKind::None) kind = DecodeFailureKind::Transient;
    state.lastFailure = kind;
    state.fileSize = result.fileSize;
    state.modifiedStamp = result.modifiedStamp;
    if (state.fileSize == 0 && state.modifiedStamp == 0) {
        if (const auto index = CatalogIndexForPath(result.path.wstring())) {
            const PhotoItem& photo = catalog_.PhotoAt(*index);
            state.fileSize = photo.fileSize;
            state.modifiedStamp = photo.modifiedStamp;
        }
    }
    const bool fallbackAlreadyTried = result.forceWic;
    state.terminal = kind == DecodeFailureKind::Missing ||
        state.failedAttempts >= kMaxDecodeAttempts ||
        (kind == DecodeFailureKind::Decode && fallbackAlreadyTried);
    if (state.terminal) {
        state.nextRetryAt = IsTransientFailureKind(kind) ?
            now + kTransientTerminalRearm : std::chrono::steady_clock::time_point::max();
        failedThumbnailPaths_.insert(key);
    } else {
        state.nextRetryAt = now + (state.failedAttempts <= 1 ?
            kDecodeRetryFirstBackoff : kDecodeRetrySecondBackoff);
        failedThumbnailPaths_.erase(key);
    }
    // Structured + privacy-respecting (file name and path id instead of the full
    // path); a whole folder of unreadable files is rate-limited, terminal verdicts
    // are always recorded.
    static quicksift::core::LogRateLimiter limiter(20, std::chrono::seconds(10));
    std::uint64_t suppressed = 0;
    if (state.terminal || limiter.Allow(&suppressed)) {
        QS_LOG_EVENT(quicksift::diagnostics::Level::Warning, quicksift::diagnostics::category::kThumbReliability,
            state.terminal ? L"thumbnail_marked_failed" : L"thumbnail_retry_scheduled",
            { L"file", quicksift::diagnostics::LogFileName(result.path) },
            { L"path_id", quicksift::diagnostics::LogPathId(result.path) },
            { L"attempt", std::to_wstring(state.failedAttempts) },
            { L"max_attempts", std::to_wstring(kMaxDecodeAttempts) },
            { L"kind", std::wstring(FailureKindName(kind)) },
            { L"fallback", fallbackAlreadyTried ? L"1" : L"0" },
            { L"suppressed", std::to_wstring(suppressed) });
    }
    ArmDecodeRetryTimer();
    ArmThumbnailReconcile(0);
}

bool QuickSiftApplicationImpl::ViewDecodeAdmissible(const fs::path& path, int& retryAttempt) {
    retryAttempt = 0;
    const std::wstring key = NormalizedPathKey(path);
    const auto it = viewRetryState_.find(key);
    if (it == viewRetryState_.end()) return true;
    DecodeRetryState& state = it->second;
    if (const auto index = CatalogIndexForPath(path.wstring())) {
        const PhotoItem& photo = catalog_.PhotoAt(*index);
        if (FileChangedSinceFailure(state, photo.fileSize, photo.modifiedStamp)) {
            viewRetryState_.erase(it);
            return true;
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (state.terminal) {
        if (IsTransientFailureKind(state.lastFailure) && now >= state.nextRetryAt) {
            viewRetryState_.erase(it);
            return true;
        }
        return false;
    }
    if (now < state.nextRetryAt) return false;
    retryAttempt = std::clamp(state.failedAttempts, 0, kMaxDecodeAttempts - 1);
    return true;
}

bool QuickSiftApplicationImpl::ViewDecodeTerminallyFailed(const fs::path& path) {
    const auto it = viewRetryState_.find(NormalizedPathKey(path));
    return it != viewRetryState_.end() && it->second.terminal;
}

void QuickSiftApplicationImpl::RecordViewDecodeFailure(const WorkResult& result) {
    const std::wstring key = NormalizedPathKey(result.path);
    const auto now = std::chrono::steady_clock::now();
    DecodeRetryState& state = viewRetryState_[key];
    state.failedAttempts = std::max(state.failedAttempts + 1, result.retryAttempt + 1);
    DecodeFailureKind kind = result.timedOut ? DecodeFailureKind::Timeout : result.failureKind;
    if (kind == DecodeFailureKind::None) kind = DecodeFailureKind::Transient;
    state.lastFailure = kind;
    state.fileSize = result.fileSize;
    state.modifiedStamp = result.modifiedStamp;
    if (state.fileSize == 0 && state.modifiedStamp == 0) {
        if (const auto index = CatalogIndexForPath(result.path.wstring())) {
            const PhotoItem& photo = catalog_.PhotoAt(*index);
            state.fileSize = photo.fileSize;
            state.modifiedStamp = photo.modifiedStamp;
        }
    }
    // A view decode already runs the full codec chain including the Shell provider
    // (worker Full/Preview paths), so a deterministic failure gets one more attempt
    // only if it was the first; Missing is terminal immediately.
    state.terminal = kind == DecodeFailureKind::Missing ||
        state.failedAttempts >= kMaxDecodeAttempts ||
        (kind == DecodeFailureKind::Decode && state.failedAttempts >= 2);
    if (state.terminal) {
        state.nextRetryAt = IsTransientFailureKind(kind) ?
            now + kTransientTerminalRearm : std::chrono::steady_clock::time_point::max();
    } else {
        state.nextRetryAt = now + (state.failedAttempts <= 1 ?
            kDecodeRetryFirstBackoff : kDecodeRetrySecondBackoff);
    }
    QS_LOG_WARNING(L"Decode", std::wstring(L"View decode attempt failed") +
        (state.terminal ? L"; marked failed" : L"; retry scheduled") +
        L": " + result.path.wstring() +
        L" attempt=" + std::to_wstring(state.failedAttempts) +
        L"/" + std::to_wstring(kMaxDecodeAttempts) +
        L" kind=" + FailureKindName(kind));
    ArmDecodeRetryTimer();
}

void QuickSiftApplicationImpl::ArmDecodeRetryTimer() {
    if (!hwnd_) return;
    // One timer services every pending backoff. Only deadlines in the future count,
    // so an expired entry can never spin the timer at 1 ms.
    const auto now = std::chrono::steady_clock::now();
    auto next = std::chrono::steady_clock::time_point::max();
    const auto consider = [&](const std::unordered_map<std::wstring, DecodeRetryState>& states) {
        for (const auto& [path, state] : states) {
            (void)path;
            if (state.nextRetryAt > now && state.nextRetryAt < next) next = state.nextRetryAt;
        }
    };
    consider(thumbnailRetryState_);
    consider(viewRetryState_);
    if (next == std::chrono::steady_clock::time_point::max()) {
        KillTimer(hwnd_, ID_TIMER_THUMBNAIL_RETRY_COOLDOWN);
        return;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count();
    const UINT delay = static_cast<UINT>(std::clamp<long long>(remaining + 1, 10, 60000));
    StartUiTimer(ID_TIMER_THUMBNAIL_RETRY_COOLDOWN, delay, L"decode retry backoff");
}

void QuickSiftApplicationImpl::RetryFailedImageLoads() {
    // Manual retry: forget every failure mark (terminal or backing off) and ask the
    // visible surfaces to request again.
    failedThumbnailPaths_.clear();
    thumbnailRetryState_.clear();
    viewRetryState_.clear();
    failedDecodes_.clear();
    shellFallbackCache_.clear();
    if (hwnd_) KillTimer(hwnd_, ID_TIMER_THUMBNAIL_RETRY_COOLDOWN);
    QS_LOG_INFO(L"Decode", L"Manual retry of failed image loads requested");
    if (reviewState_.Mode() == ViewMode::Thumbnails) {
        ScheduleVisibleWork();
        ArmThumbnailReconcile(0);
    }
    InvalidateCanvas();
}
// CODE GUIDE: See CODE_GUIDE.md -> "Opening a folder".
// OWNER: Catalog-load lifecycle and stale-safe worker-result admission on the UI thread.
// Catalog controller: folder loading, scanner batches, result integration,
// metadata queueing, ordering, and visible-position validation.
    void QuickSiftApplicationImpl::LoadFolder(const fs::path& folder) {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        persistentCache_.AddLibraryFolder(folder, now);
        RefreshLibraryList();
        // Privacy (CODE_GUIDE 5.6): folder name + path id, not the full path.
        QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Catalog", L"folder_load",
            { L"folder", quicksift::diagnostics::LogFileName(folder) },
            { L"path_id", quicksift::diagnostics::LogPathId(folder) });
        currentFolder_ = folder;
        ClearFaceZoomToggle();
        faceOutlineFade_.Reset();
        if (pendingRatingsImport_) CancelRatingsImport();
        backgroundFaceCursor_ = 0;
        backgroundFaceAttemptsSinceWork_ = 0;
        backgroundFacePauseUntil_ = std::chrono::steady_clock::time_point{};
        faceAnalysisRetryPolicy_.ClearFileRetries();
        failedThumbnailPaths_.clear();
        thumbnailRetryState_.clear();
        viewRetryState_.clear();
        KillTimer(hwnd_, ID_TIMER_THUMBNAIL_RETRY_COOLDOWN);
        lastScheduledThumbnailViewportEpoch_ = 0;
        cpuSampleValid_ = false;
        storageProfile_ = quicksift::DetectStorageProfile(folder, systemProfile_.lowMemory);
        worker_.SetStorageProfile(storageProfile_);
        ++generation_;
        reviewState_.ResetForFolder();
        worker_.SetActiveGeneration(generation_);
        worker_.SetNavigationEpoch(reviewState_.Epochs().navigation);
        PublishThumbnailViewport(0, {});
        scanner_.Cancel();
        // Folder catalogs can contain hundreds of thousands of paths. `clear()`
        // destroys elements but deliberately retains vector capacity and hash
        // buckets, making a later small folder inherit the previous shoot's RAM.
        // A folder switch is an infrequent, natural boundary where releasing that
        // storage is worth more than preserving allocation capacity.
        catalog_.ClearAndRelease();
        OnSearchFolderChanged(); // keeps the query text; drops per-folder match state
        selection_.Reset(true);
        pendingRatings_ = 0;
        metadataJobsInFlight_ = 0;
        std::deque<size_t>().swap(pendingMetadataIndices_);
        metadataUiDirty_ = false;
        KillTimer(hwnd_, ID_TIMER_METADATA_UI);
        thumbnailPrefetchPlanner_.Reset(reviewState_.Thumbnails().scrollDip);
        std::unordered_map<std::wstring, std::wstring>().swap(exifCache_);
        scanComplete_ = false;
        if (exif_) {
            SetWindowTextW(exif_, Tr(L"Scanning folder…").c_str());
            InvalidateRect(exif_, nullptr, FALSE);
        }
        ClearBitmapCache();
        // The scanner always catalogs every supported image once. Format/date
        // filters are presentation policy and rebuild the in-memory visible order.
        scanner_.Start(folder, generation_);
        SetMode(ViewMode::Thumbnails);
        UpdateStatus();
        InvalidateCanvas();
    }
    void QuickSiftApplicationImpl::PumpMetadataQueue() {
        while (metadataJobsInFlight_ < metadataQueueLimit_ && !pendingMetadataIndices_.empty()) {
            const size_t index = pendingMetadataIndices_.front();
            pendingMetadataIndices_.pop_front();
            if (index >= catalog_.PhotoCount()) continue;
            const PhotoItem& photo = catalog_.PhotoAt(index);
            const bool metadataUrgent = sortMode_ == SortMode::RatingHigh ||
                ratingFilter_ != RatingFilter::All || pickFilter_ != PickFilter::All ||
                colorLabelFilter_ != ColorLabelFilter::All;
            if (worker_.EnqueueMetadata(photo.path, generation_, photo.metadataRevision,
                metadataUrgent ? JobPriority::Predictive : JobPriority::Idle)) {
                ++metadataJobsInFlight_;
            } else if (pendingRatings_ > 0) {
                // A coalesced or rejected job will not produce a second completion.
                // Keep UI progress counters paired with jobs the worker actually owns.
                --pendingRatings_;
            }
        }
    }
    void QuickSiftApplicationImpl::OnScanBatch(const std::shared_ptr<ScanBatch>& batch) {
        if (!batch) return;
        struct BatchAcknowledgement {
            FolderScanner* scanner = nullptr;
            std::uint64_t generation = 0;
            ~BatchAcknowledgement() {
                if (scanner) scanner->AcknowledgeBatch(generation);
            }
        } acknowledgement{ &scanner_, batch->generation };
        if (batch->generation != generation_) return; // intentional stale discard
        bool catalogCommitted = false;
        try {
            // Build the prospective visible order before mutating the authoritative
            // catalog. CatalogStore commits the records, path index, order, and
            // reverse positions together, or leaves all four unchanged.
            const size_t oldCatalogSize = catalog_.PhotoCount();
            if (batch->photos.size() > std::numeric_limits<size_t>::max() - oldCatalogSize) {
                throw std::length_error("incoming scan batch exceeded addressable catalog size");
            }
            std::vector<size_t> metadataIndices;
            metadataIndices.reserve(batch->photos.size());
            std::vector<size_t> visibleOrder = catalog_.CopyVisibleOrder();
            const size_t oldVisibleSize = visibleOrder.size();
            const size_t requiredVisibleCapacity = visibleOrder.size() + batch->photos.size();
            if (requiredVisibleCapacity > visibleOrder.capacity()) {
                const size_t currentCapacity = visibleOrder.capacity();
                const size_t growth = std::max<size_t>(512, currentCapacity / 2);
                const size_t grownCapacity = growth > std::numeric_limits<size_t>::max() - currentCapacity ?
                    std::numeric_limits<size_t>::max() : currentCapacity + growth;
                visibleOrder.reserve(std::max(requiredVisibleCapacity, grownCapacity));
            }
            for (size_t offset = 0; offset < batch->photos.size(); ++offset) {
                const size_t futureIndex = oldCatalogSize + offset;
                const PhotoItem& item = batch->photos[offset];
                if (item.ratingKnowledge == MetadataKnowledge::Unknown ||
                    item.colorLabelKnowledge == MetadataKnowledge::Unknown ||
                    item.pickStateKnowledge == MetadataKnowledge::Unknown)
                    metadataIndices.push_back(futureIndex);
                if (CullAllowed(item)) visibleOrder.push_back(futureIndex);
            }
            auto prospectivePhoto = [&](size_t index) -> const PhotoItem& {
                return index < oldCatalogSize ? catalog_.PhotoAt(index) :
                    batch->photos.at(index - oldCatalogSize);
            };
            auto compare = [&](size_t left, size_t right) {
                return PhotoSortLess(prospectivePhoto(left), prospectivePhoto(right));
            };
            std::stable_sort(visibleOrder.begin() + static_cast<std::ptrdiff_t>(oldVisibleSize),
                visibleOrder.end(), compare);
            std::inplace_merge(visibleOrder.begin(),
                visibleOrder.begin() + static_cast<std::ptrdiff_t>(oldVisibleSize),
                visibleOrder.end(), compare);
            // Prepare the metadata queue before catalog mutation. Deque append may
            // allocate; swapping the fully prepared queue after catalog commit is
            // noexcept and keeps scanner acknowledgement truthful under low memory.
            if (metadataIndices.size() > std::numeric_limits<size_t>::max() -
                pendingRatings_) {
                throw std::length_error("metadata progress counter overflow");
            }
            std::deque<size_t> prospectiveMetadataQueue = pendingMetadataIndices_;
            for (const size_t index : metadataIndices) {
                prospectiveMetadataQueue.push_back(index);
            }
            const std::wstring previousCurrent = reviewState_.SinglePath();
            const std::wstring previousFocus = reviewState_.ThumbnailFocusPath();
            static_cast<void>(catalog_.AppendBatchWithVisibleOrder(
                std::span<const PhotoItem>(batch->photos.data(), batch->photos.size()),
                std::move(visibleOrder)));
            catalogCommitted = true;
            pendingMetadataIndices_.swap(prospectiveMetadataQueue);
            pendingRatings_ += metadataIndices.size();
            RestorePhotoOrderState(previousCurrent, previousFocus);
            if (reviewState_.SinglePath().empty() && !catalog_.VisibleEmpty()) {
                reviewState_.SetSinglePath(VisiblePhoto(0).path.wstring());
            }
            if (reviewState_.ThumbnailFocusPath().empty()) reviewState_.SetThumbnailFocusPath(reviewState_.SinglePath());
            UpdateStatus();
            // Folder discovery is streamed; image delivery must not wait for the
            // scanner to reach its final entry. Only schedule thumbnail-grid work when
            // the user is actually in Thumbnail view. The previous unconditional
            // ScheduleVisibleWork() also ran while Single/Compare was active, flooding
            // the image workers with thumbnail jobs during a folder scan and competing
            // with the current-image Interactive request.
            if (reviewState_.Mode() == ViewMode::Thumbnails) {
                ScheduleVisibleWork();
            } else {
                SchedulePredictivePrefetch(0);
            }
            worker_.MarkInteractiveActivity();
            PumpMetadataQueue();
            InvalidateCanvas();
        } catch (const std::exception& error) {
            if (catalogCommitted) {
                QS_LOG_ERROR(L"Catalog", L"A committed scan batch could not finish UI/metadata integration: " +
                    Utf8ToWide(error.what()));
                ShowGlassAlert(L"Folder batch needs a refresh",
                    L"The photos were committed safely, but QuickSift could not finish scheduling all follow-up work. The final folder rebuild will restore ordering and metadata requests.",
                    GlassAlertKind::Warning);
            } else {
                QS_LOG_ERROR(L"Catalog", L"A scan batch was discarded before catalog commit: " +
                    Utf8ToWide(error.what()));
                ShowGlassAlert(L"Folder scan batch could not be committed",
                    L"QuickSift left the existing catalog unchanged because one incoming batch could not be validated or allocated safely.",
                    GlassAlertKind::Error);
            }
        } catch (...) {
            if (catalogCommitted) {
                QS_LOG_ERROR(L"Catalog", L"A committed scan batch hit an unknown follow-up error");
                ShowGlassAlert(L"Folder batch needs a refresh",
                    L"The photos were committed safely, but follow-up UI work stopped unexpectedly. The final folder rebuild will reconcile the view.",
                    GlassAlertKind::Warning);
            } else {
                QS_LOG_ERROR(L"Catalog", L"A scan batch was discarded after an unknown exception before commit");
                ShowGlassAlert(L"Folder scan batch could not be committed",
                    L"QuickSift left the existing catalog unchanged after an unexpected catalog error.",
                    GlassAlertKind::Error);
            }
        }
    }
    void QuickSiftApplicationImpl::OnScanComplete(uint64_t generation,
        quicksift::work::ScanCompletionStatus status, std::wstring detail,
        std::uint32_t nativeError) {
        if (generation != generation_) return;
        scanComplete_ = true;
        if (status != quicksift::work::ScanCompletionStatus::Succeeded) {
            const bool failed = status == quicksift::work::ScanCompletionStatus::Failed;
            if (detail.empty()) {
                detail = failed ? L"The folder could not be enumerated safely." :
                    L"The folder was only partially enumerated.";
            }
            if (nativeError != 0) {
                detail += L"\n\nWindows error: " + std::to_wstring(nativeError);
            }
            QS_LOG_ERROR(L"Catalog", detail);
            ShowGlassAlert(failed ? L"Folder scan failed" : L"Folder scan incomplete",
                detail, failed ? GlassAlertKind::Error : GlassAlertKind::Warning);
        }
        RebuildVisiblePhotos();
        QS_LOG_INFO(L"Catalog", L"Folder scan completed: " + std::to_wstring(catalog_.PhotoCount()) +
            L" files, " + std::to_wstring(catalog_.VisibleCount()) + L" visible");
        if (!sessionRestored_) {
            sessionRestored_ = true;
            if (!restoredCurrentPath_.empty() && IsPathVisible(restoredCurrentPath_)) {
                reviewState_.SetPrimaryPath(restoredCurrentPath_);
                selection_.Clear();
                selection_.Select(restoredCurrentPath_);
            }
            if (restoredMode_ == ViewMode::Single && !reviewState_.SinglePath().empty()) reviewState_.SetMode(ViewMode::Single);
            // Compare selection is intentionally not fabricated; it is restored as thumbnails.
            if (restoredMode_ == ViewMode::Compare) reviewState_.SetMode(ViewMode::Thumbnails);
            UpdateModeButtons();
            RequestExifForActive();
        }
        // Recover committed metadata work after any later UI-allocation failure.
        PumpMetadataQueue();
        // Re-issue the current viewport after the final batch. This is cheap due
        // to job de-duplication and closes a race where the early scan batches were
        // laid out while the canvas still had its startup dimensions.
        ScheduleVisibleWork();
        SchedulePredictivePrefetch(0);
        if (exif_ && ActiveInfoPath().empty()) {
            SetWindowTextW(exif_, Tr(L"Select a photo to inspect its EXIF information.").c_str());
            InvalidateRect(exif_, nullptr, FALSE);
        }
        UpdateStatus();
        InvalidateCanvas();
    }

    bool QuickSiftApplicationImpl::PhotoSortLess(const PhotoItem& a, const PhotoItem& b) const {
        return quicksift::core::PhotoSortLess(a, b, sortMode_);
    }

    void QuickSiftApplicationImpl::RestorePhotoOrderState(const std::wstring& current, const std::wstring& focus) {
#ifndef NDEBUG
        ValidateVisibleCatalog();
#endif
        if (!current.empty() && IsPathVisible(current)) reviewState_.SetSinglePath(current);
        if (!focus.empty() && IsPathVisible(focus)) reviewState_.SetThumbnailFocusPath(focus);
        if (!reviewState_.ThumbnailFocusPath().empty()) {
            if (const auto focusIndex = IndexForPath(reviewState_.ThumbnailFocusPath())) selection_.SetAnchor(*focusIndex);
            else selection_.ClearAnchor();
        }
    }

    void QuickSiftApplicationImpl::SortPhotos() {
        const std::wstring current = reviewState_.SinglePath();
        const std::wstring focus = reviewState_.ThumbnailFocusPath();
        std::vector<size_t> visibleOrder = catalog_.CopyVisibleOrder();
        std::stable_sort(visibleOrder.begin(), visibleOrder.end(),
            [this](size_t left, size_t right) {
                return PhotoSortLess(catalog_.PhotoAt(left), catalog_.PhotoAt(right));
            });
        catalog_.SetVisibleOrder(std::move(visibleOrder));
        RestorePhotoOrderState(current, focus);
    }

#ifndef NDEBUG
    void QuickSiftApplicationImpl::ValidateVisibleCatalog() const {
        assert(catalog_.ValidateVisibleOrder());
        for (size_t visibleIndex = 0; visibleIndex < catalog_.VisibleCount(); ++visibleIndex) {
            const size_t catalogIndex = catalog_.VisibleCatalogIndex(visibleIndex);
            assert(catalogIndex < catalog_.PhotoCount());
            assert(catalog_.VisiblePosition(catalogIndex) == visibleIndex);
            assert(CullAllowed(catalog_.PhotoAt(catalogIndex)));
        }
        for (size_t catalogIndex = 0; catalogIndex < catalog_.PhotoCount(); ++catalogIndex) {
            const size_t visibleIndex = catalog_.VisiblePosition(catalogIndex);
            if (visibleIndex == kInvalidVisiblePosition) continue;
            assert(visibleIndex < catalog_.VisibleCount());
            assert(catalog_.VisibleCatalogIndex(visibleIndex) == catalogIndex);
        }
    }

#endif

void QuickSiftApplicationImpl::DrainPostedWorkerMessages() {
        // Producers are stopped before this method runs. Closing the queue drops
        // any results that can no longer be integrated into a destroying window.
        completionQueue_.CloseAndClear();
        MSG message{};
        while (PeekMessageW(&message, hwnd_, WM_APP_BACKGROUND_COMPLETION,
            WM_APP_BACKGROUND_COMPLETION, PM_REMOVE)) {
        }
    }

    void QuickSiftApplicationImpl::CompleteMetadataJob() {
        if (pendingRatings_ > 0) --pendingRatings_;
        if (metadataJobsInFlight_ > 0) --metadataJobsInFlight_;
        PumpMetadataQueue();
        metadataUiDirty_ = true;
        if (pendingRatings_ == 0) {
            KillTimer(hwnd_, ID_TIMER_METADATA_UI);
            FlushMetadataUi();
        } else {
            // Debounce the expensive full-canvas/status refresh while a large
            // folder is streaming thousands of metadata completions. The final
            // result is always flushed immediately.
            StartUiTimer(ID_TIMER_METADATA_UI, 80, L"metadata refresh");
        }
    }

    void QuickSiftApplicationImpl::OnMetadataJobAborted(uint64_t generation) {
        if (generation != generation_) return;
        QS_LOG_WARNING(L"Metadata",
            L"A metadata job was canceled or aborted before a result envelope could be posted; continuing the queue.");
        CompleteMetadataJob();
    }

    int QuickSiftApplicationImpl::CalculateAdaptiveTimeoutMs(
        const std::wstring& path, int attempt) const {
        double baseTimeoutMs = 1000.0;

        if (const PhotoItem* photo = PhotoForPath(path)) {
            const double sizeMB =
                static_cast<double>(photo->fileSize) / (1024.0 * 1024.0);
            baseTimeoutMs = std::clamp(1000.0 * (sizeMB / 10.0), 1000.0, 30000.0);

            const std::wstring ext = ToLower(photo->extension);
            if (IsRawExtension(ext)) {
                baseTimeoutMs *= 4.0;
            } else if (ext == L".avif" || ext == L".heic" || ext == L".heif") {
                baseTimeoutMs *= 2.0;
            } else if (ext == L".tif" || ext == L".tiff") {
                baseTimeoutMs *= 1.5;
            }
        }

        const double multiplier = std::pow(2.0, static_cast<double>(std::max(0, attempt)));
        const int finalTimeout = static_cast<int>(baseTimeoutMs * multiplier);
        return std::clamp(finalTimeout, 1000, 60000);
    }

    void QuickSiftApplicationImpl::OnWorkReady(const std::shared_ptr<WorkResult>& result) {
        if (!result) return;
        worker_.AcknowledgePostedResult(*result);
        if (result->viewInteractionEpoch != 0 &&
            result->viewInteractionEpoch < viewInteractionEpoch_.load(std::memory_order_acquire)) return;
        const bool thumbnailPathCurrent = result->cacheClass != CacheClass::Thumbnail ||
            reviewState_.IsThumbnailPathDesired(result->path.wstring());
        const auto admission = quicksift::review::ClassifyPostedResult(*result, generation_,
            reviewState_.Epochs().navigation, reviewState_.Epochs().thumbnailViewport,
            thumbnailPathCurrent);
        if (admission != quicksift::review::ResultAdmission::Accept) return;
        const std::wstring path = result->path.wstring();

        auto updateCatalogPhoto = [&](const std::function<void(PhotoItem&)>& fn) {
            if (const auto index = CatalogIndexForPath(path)) {
                catalog_.EditPhoto(*index, fn);
                PersistPhotoRecord(catalog_.PhotoAt(*index));
            }
        };

        if (result->kind == JobKind::Metadata) {
            if (!result->workFailed) {
                if (const auto index = CatalogIndexForPath(path)) {
                    const PhotoItem& current = catalog_.PhotoAt(*index);
                    if (result->metadataRevision == current.metadataRevision) {
                        updateCatalogPhoto([&](PhotoItem& item) {
                            if (result->fileSize != 0) item.fileSize = result->fileSize;
                            if (result->modifiedStamp != 0) item.modifiedStamp = result->modifiedStamp;
                            item.sidecarStamp = result->sidecarStamp;
                            item.rating = result->rating;
                            item.colorLabel = result->colorLabel;
                            item.pickState = result->pickState;
                            item.ratingKnowledge = result->ratingKnowledge;
                            item.colorLabelKnowledge = result->colorLabelKnowledge;
                            item.pickStateKnowledge = result->pickStateKnowledge;
                            item.rawRating = result->rawRating;
                            item.rawColorLabel = result->rawColorLabel;
                            item.rawPickState = result->rawPickState;
                        });
                    } else QS_LOG_INFO(L"Metadata",
                        L"Discarded stale metadata result for " + path);
                }
            }
            CompleteMetadataJob();
            return;
        }

        if (result->kind == JobKind::Exif) {
            const std::wstring text = result->workFailed ?
                L"EXIF could not be read because the decoder or filesystem failed temporarily." :
                (result->exifText.empty() ? L"No EXIF information was exposed for this file." : result->exifText);
            if (!result->workFailed) {
                if (!exifCache_.contains(path) && exifCache_.size() >= exifCacheLimit_ && !exifCache_.empty()) {
                    exifCache_.erase(exifCache_.begin());
                }
                exifCache_[path] = text;
                if (const auto index = CatalogIndexForPath(path)) {
                    catalog_.EditPhoto(*index, [&](PhotoItem& item) {
                        if (result->fileSize != 0) item.fileSize = result->fileSize;
                        if (result->modifiedStamp != 0) item.modifiedStamp = result->modifiedStamp;
                        item.sidecarStamp = result->sidecarStamp;
                    });
                    PersistPhotoRecord(catalog_.PhotoAt(*index), text);
                }
            }
            if (ActiveInfoPath() == path && exif_) {
                SetWindowTextW(exif_, Tr(text).c_str());
                InvalidateRect(exif_, nullptr, FALSE);
            }
            return;
        }

        if (result->kind == JobKind::Face) {
            HandleFaceAnalysisOutcome(*result);
            // A transient WinRT/decoder failure is not the same as a completed
            // scan with zero faces. Do not poison the persistent record and prevent
            // future retries when memory or the decoder becomes available again.
            if (result->facesScanned) {
                updateCatalogPhoto([&](PhotoItem& item) {
                    if (result->fileSize != 0) item.fileSize = result->fileSize;
                    if (result->modifiedStamp != 0) item.modifiedStamp = result->modifiedStamp;
                    item.sidecarStamp = result->sidecarStamp;
                    item.facesScanned = true;
                    item.faces = result->faces;
                    item.faceBlurScanned = result->faceBlurScanned;
                    item.faceBlurry = result->faceBlurry;
                    item.faceSharpness = result->faceSharpness;
                    item.faceQualities = result->faceBlurScanned ? result->faceQualities :
                        std::vector<quicksift::FaceQualityRecord>{};
                    // A FaceLock choice made in this session (or already hydrated) wins
                    // over the persisted one carried by the worker's cache lookup.
                    if (!item.faceSelectionKnown && result->faceSelectionKnown) {
                        item.faceSelectionKnown = true;
                        item.faceSelection = result->faceSelection;
                    }
                });
                if (reviewState_.FaceLockEnabled() && ActiveInfoPath() == path) ApplyFaceLockForActive(false);
                InvalidateCanvas();
            }
            return;
        }

        if (result->pixels.empty() || result->width <= 0 || result->height <= 0) {
            const bool thumbnail = result->cacheClass == CacheClass::Thumbnail;
            const bool thumbnailMode = reviewState_.Mode() == ViewMode::Thumbnails;
            const bool visibleThumbnail = thumbnail && thumbnailMode &&
                reviewState_.IsThumbnailPathDesired(path);
            const bool timedOut = result->timedOut;
            const bool retryableFailure = result->workFailed || timedOut;
            const std::wstring failureKey = DecodeFailureKey(path, result->targetSize,
                result->kind, result->cacheClass);

            if (result->cancelled && !timedOut) {
                // Viewport/navigation cancellation is not a decode failure. A later
                // viewport pass will request the image again if it becomes visible.
                failedDecodes_.erase(failureKey);
                if (thumbnail && thumbnailMode) {
                    // Cancelled work (fling, viewport jump, stale target) re-enters
                    // through the reconciliation sweep once the grid settles.
                    ScheduleVisibleWork();
                    ArmThumbnailReconcile(0);
                }
                InvalidateCanvas();
                return;
            }

            if (thumbnail && retryableFailure) {
                // 0.9.4.0 retry policy: record the attempt; the backoff timer / the
                // visible-cell reconciliation sweep issues the next attempt (with the
                // worker's escalated timeout) only while the cell is still wanted.
                failedDecodes_.erase(failureKey);
                RecordThumbnailDecodeFailure(*result);
                (void)visibleThumbnail;
                InvalidateCanvas();
                return;
            }

            if (!thumbnail && retryableFailure && result->kind != JobKind::DecodeTile) {
                failedDecodes_.erase(failureKey);
                RecordViewDecodeFailure(*result);
                InvalidateCanvas();
                return;
            }

            if (thumbnail) {
                // Not retryable (e.g. no pixels without a failure flag): treat it as
                // one failed attempt so it still goes through the bounded policy.
                RecordThumbnailDecodeFailure(*result);
            } else {
                RememberTransientDecodeFailure(failureKey);
            }
            QS_LOG_WARNING(L"Decode", L"Decode produced no usable pixels: " + path +
                L" (kind " + std::to_wstring(static_cast<int>(result->kind)) +
                L", workFailed=" + (result->workFailed ? L"1" : L"0") +
                L", timed_out=" + (timedOut ? L"1" : L"0") + L")");
            InvalidateCanvas();
            return;
        }

        failedDecodes_.erase(DecodeFailureKey(path, result->targetSize, result->kind, result->cacheClass));

        // A successful thumbnail/preview/full decode clears any remembered thumbnail
        // failure state. This also lets a successful Single/Compare decode unblock the
        // corresponding thumbnail request.
        if (result->cacheClass == CacheClass::Thumbnail ||
            result->kind == JobKind::DecodePreview || result->kind == JobKind::DecodeFull) {
            const std::wstring thumbnailKey = NormalizedPathKey(result->path);
            failedThumbnailPaths_.erase(thumbnailKey);
            thumbnailRetryState_.erase(thumbnailKey);
            if (result->cacheClass != CacheClass::Thumbnail && !result->previewOnly)
                viewRetryState_.erase(thumbnailKey);
            if (result->viewLane && result->kind == JobKind::DecodeFull && result->previewOnly &&
                !(loadOnlyRawJpegPreviews_ && IsRawExtension(ExtensionLower(path)))) {
                // The full decode could only produce a preview (every codec failed, the
                // Shell fallback succeeded). That is the best this file can give: remember
                // it so paint/backstop do not re-run the expensive full decode forever.
                DecodeRetryState& state = viewRetryState_[thumbnailKey];
                state.failedAttempts = kMaxDecodeAttempts;
                state.lastFailure = DecodeFailureKind::Decode;
                state.terminal = true;
                state.nextRetryAt = std::chrono::steady_clock::time_point::max();
                state.fileSize = result->fileSize;
                state.modifiedStamp = result->modifiedStamp;
            }
        }

        if (loadOnlyRawJpegPreviews_ && IsRawExtension(ExtensionLower(path)) &&
            (result->kind == JobKind::DecodeTile || !result->previewOnly)) {
            // A RAW decode can already be in flight when the setting changes.
            // Never admit stale demosaiced/tile pixels after JPG-preview-only mode
            // becomes authoritative. New jobs are also cancelled/rerouted in Worker.
            return;
        }

        // A thumbnail-size change can leave older jobs completing out of order.
        // Do not upload a now-obsolete large surface only to evict the current grid.
        if (result->cacheClass == CacheClass::Thumbnail &&
            result->targetSize != ThumbnailDecodeTarget()) return;

        // Older cache payloads may contain landscape RAW sensor dimensions beside
        // an already-oriented portrait preview. Repair that geometry on admission
        // so the bitmap is fitted, never stretched, even before the cache refreshes.
        if (result->kind != JobKind::DecodeTile && IsRawExtension(ExtensionLower(path))) {
            NormalizeSourceGeometryToPixelOrientation(result->width, result->height,
                result->sourceWidth, result->sourceHeight);
        }

        if (result->sourceWidth > 0 && result->sourceHeight > 0) {
            if (const auto index = CatalogIndexForPath(path)) {
                const PhotoItem& current = catalog_.PhotoAt(*index);
                if (current.sourceWidth != result->sourceWidth ||
                    current.sourceHeight != result->sourceHeight ||
                    (result->fileSize != 0 && current.fileSize != result->fileSize) ||
                    (result->modifiedStamp != 0 && current.modifiedStamp != result->modifiedStamp)) {
                    catalog_.EditPhoto(*index, [&](PhotoItem& item) {
                        item.sourceWidth = result->sourceWidth;
                        item.sourceHeight = result->sourceHeight;
                        if (result->fileSize != 0) item.fileSize = result->fileSize;
                        if (result->modifiedStamp != 0) item.modifiedStamp = result->modifiedStamp;
                    });
                    PersistPhotoRecord(catalog_.PhotoAt(*index));
                }
            }
        }
        if (reviewState_.Mode() == ViewMode::Single && reviewState_.SinglePath() == path) UpdateStatus();

        const std::uint64_t rowBytes = static_cast<std::uint64_t>(result->width) * 4ull;
        const std::uint64_t expectedBytes = rowBytes * static_cast<std::uint64_t>(result->height);
        if (rowBytes > std::numeric_limits<UINT32>::max() || expectedBytes != result->pixels.size()) {
            return;
        }

        std::optional<CacheKey> incomingBitmapKey;
        if (result->kind != JobKind::DecodeTile) {
            incomingBitmapKey = CacheKey{ path, result->targetSize, result->cacheClass };
            if (const auto existing = bitmapCache_.find(*incomingBitmapKey);
                existing != bitmapCache_.end() && !existing->second.previewOnly && result->previewOnly) {
                // Reject a late embedded preview before evicting anything or asking
                // Direct2D to allocate a GPU surface that will be discarded.
                return;
            }
        }

        // 0.9.4.0: in Thumbnail view the worker cancels every Face job while the grid
        // viewport is active (Worker::IsCancelled), so enqueueing one per thumbnail
        // completion only produced queue churn (a key build, a worker-lock round trip,
        // and a dequeue/stat/cancel cycle on a decode worker). Background face analysis
        // keeps its own scheduler; Single/Compare admission is unchanged.
        const bool thumbnailGridResult = result->cacheClass == CacheClass::Thumbnail &&
            reviewState_.Mode() == ViewMode::Thumbnails;
        if (const auto catalogIndex = thumbnailGridResult ? std::optional<size_t>{} : CatalogIndexForPath(path)) {
            const PhotoItem& photo = catalog_.PhotoAt(*catalogIndex);
            if (!photo.facesScanned || (!photo.faces.empty() && !photo.faceBlurScanned)) {
                const bool foregroundFace = reviewState_.FaceLockEnabled() &&
                    (reviewState_.Mode() == ViewMode::Single || reviewState_.Mode() == ViewMode::Compare);
                EnqueueFaceAnalysis(photo.path, reviewState_.Epochs().navigation,
                    foregroundFace ? JobPriority::Interactive :
                    (result->priority == JobPriority::Visible || result->priority == JobPriority::Interactive ? JobPriority::Face : JobPriority::Idle),
                    photo.facesScanned ? kFaceSharpnessDecodeSize : kFaceDecodeSize);
            }
        }

        EnsureDeviceResources();
        if (!renderTarget_) return;
        const CacheClass incomingClass = result->kind == JobKind::DecodeTile ?
            CacheClass::Tile : result->cacheClass;
        size_t reclaimableBytes = 0;
        if (result->kind == JobKind::DecodeTile) {
            const TileKey existingKey{ path, result->tileX, result->tileY,
                result->tileWidth, result->tileHeight, result->tileLevel };
            if (const auto existing = tileCache_.find(existingKey); existing != tileCache_.end()) {
                reclaimableBytes = existing->second.bytes;
            }
        } else if (incomingBitmapKey) {
            if (const auto existing = bitmapCache_.find(*incomingBitmapKey);
                existing != bitmapCache_.end()) {
                reclaimableBytes = existing->second.bytes;
            }
            if (result->cacheClass == CacheClass::Thumbnail) {
                // 0.9.4.0: probe only this path's indexed size buckets rather than
                // scanning the whole bitmap cache for every thumbnail completion.
                if (const auto sizes = pathToBitmapSizes_.find(path); sizes != pathToBitmapSizes_.end()) {
                    for (const int size : sizes->second) {
                        if (size == incomingBitmapKey->size) continue;
                        const auto cached = bitmapCache_.find(CacheKey{ path, size, CacheClass::Thumbnail });
                        if (cached == bitmapCache_.end()) continue;
                        reclaimableBytes = cached->second.bytes > SIZE_MAX - reclaimableBytes ?
                            SIZE_MAX : reclaimableBytes + cached->second.bytes;
                    }
                }
            }
        }
        const size_t additionalBytes = result->pixels.size() > reclaimableBytes ?
            result->pixels.size() - reclaimableBytes : 0;
        if (incomingBitmapKey && !ShouldAdmitBitmap(*incomingBitmapKey, *result, additionalBytes)) {
            // TinyLFU-style admission prevents speculative one-hit scans from
            // evicting images the photographer repeatedly compares or revisits.
            return;
        }
        // Make room before the GPU allocation rather than after it. This avoids a
        // transient old-cache + CPU-buffer + new-bitmap peak on 8 GB systems. Only
        // reserve the net growth when this result replaces an existing surface;
        // evicting for the full size caused needless cache churn during refinement.
        EvictBitmapCache(false, incomingClass, additionalBytes);
        D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        ComPtr<ID2D1Bitmap> bitmap;
        const HRESULT hr = renderTarget_->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(result->width), static_cast<UINT32>(result->height)),
            result->pixels.data(), static_cast<UINT32>(rowBytes), properties, &bitmap);
        if (FAILED(hr)) {
            if (hr == D2DERR_RECREATE_TARGET) {
                DiscardDeviceResources();
                StartUiTimer(ID_TIMER_IDLE_QUALITY, 250, L"render-target recovery");
            }
            return;
        }

        if (result->kind == JobKind::DecodeTile) {
            TileKey key{ path, result->tileX, result->tileY, result->tileWidth, result->tileHeight, result->tileLevel };
            TileEntry entry;
            entry.bitmap = bitmap;
            entry.sourceWidth = result->sourceWidth;
            entry.sourceHeight = result->sourceHeight;
            entry.bytes = result->pixels.size();
            entry.lastUse = ++useCounter_;
            entry.lastUseTime = std::chrono::steady_clock::now();
            if (const auto existing = tileCache_.find(key); existing != tileCache_.end()) tileCacheBytes_ -= existing->second.bytes;
            tileCacheBytes_ += entry.bytes;
            tileCache_[std::move(key)] = std::move(entry);
            EvictBitmapCache();
            InvalidateCanvas();
            return;
        }

        CacheKey key = std::move(*incomingBitmapKey);
        if (result->cacheClass == CacheClass::Thumbnail) {
            // Keep one GPU thumbnail per photo. Retaining old size buckets is pure
            // memory churn and was the main trigger for Large/Huge grid flicker.
            // 0.9.4.0: index-driven (O(buckets for this path)) instead of a full
            // cache scan per completion, and the size index is kept consistent.
            if (const auto sizes = pathToBitmapSizes_.find(path); sizes != pathToBitmapSizes_.end()) {
                std::vector<int> staleSizes;
                for (const int size : sizes->second) {
                    if (size != key.size) staleSizes.push_back(size);
                }
                for (const int size : staleSizes) {
                    const auto stale = bitmapCache_.find(CacheKey{ path, size, CacheClass::Thumbnail });
                    if (stale == bitmapCache_.end()) continue;
                    RemoveCacheBytes(stale->second.cacheClass, stale->second.bytes);
                    UnindexBitmapSize(stale->first);
                    bitmapCache_.erase(stale);
                }
            }
        }
        BitmapEntry entry;
        entry.bitmap = bitmap;
        entry.width = result->width;
        entry.height = result->height;
        entry.sourceWidth = result->sourceWidth;
        entry.sourceHeight = result->sourceHeight;
        entry.previewOnly = result->previewOnly;
        entry.cacheClass = result->cacheClass;
        entry.bytes = result->pixels.size();
        entry.lastUse = ++useCounter_;
        entry.lastUseTime = std::chrono::steady_clock::now();
        entry.hitCount = 1;
        entry.frequent = CacheFrequency(key) >= 2;

        auto existing = bitmapCache_.find(key);
        if (existing != bitmapCache_.end()) RemoveCacheBytes(existing->second.cacheClass, existing->second.bytes);
        AddCacheBytes(entry.cacheClass, entry.bytes);
        const std::wstring insertedPath = key.path;
        pathToBitmapSizes_[key.path].insert(key.size);
        const bool insertedThumbnail = entry.cacheClass == CacheClass::Thumbnail;
        bitmapCache_[std::move(key)] = std::move(entry);
        // Pruning depends only on this path's Preview/Full entries, which a thumbnail
        // admission never changes, so skip the work for the hot thumbnail path.
        if (!insertedThumbnail) PruneSupersededViewBitmaps(insertedPath);
        EvictBitmapCache();
        InvalidateCanvas();
    }

} // namespace quicksift::app
