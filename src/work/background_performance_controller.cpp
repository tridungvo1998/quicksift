// OWNER: Fixed maximum-performance worker policy, path-scoped decoder coordination, and telemetry.
#include "work/background_work_engine.h"
#include "embedded_metadata.h"

namespace quicksift::app {
namespace {

std::size_t TelemetryFormatIndex(const fs::path& path) noexcept {
    try {
        const std::wstring extension = ExtensionLower(path);
        if (extension == L".jpg" || extension == L".jpeg") return 0;
        if (IsRawExtension(extension)) return 1;
        if (extension == L".png") return 2;
        if (extension == L".heic" || extension == L".heif" || extension == L".avif") return 3;
        if (extension == L".tif" || extension == L".tiff") return 4;
        if (extension == L".webp") return 5;
    } catch (...) {
    }
    return 6;
}

double UpdateEwma(double current, double sample, double weight = 0.15) noexcept {
    if (!std::isfinite(sample) || sample < 0.0) return current;
    if (current <= 0.0 || !std::isfinite(current)) return sample;
    return current + (sample - current) * weight;
}

std::wstring ThumbnailPathKey(const fs::path& path) {
    std::error_code error;
    fs::path absolute = fs::absolute(path, error);
    std::wstring key = (error ? path : absolute).lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(character)));
    });
    return key;
}

} // namespace


    void Worker::RefreshReadAheadGate() noexcept {
        try {
            bool foregroundVisualPending = activeVisualJobs_.load(std::memory_order_acquire) != 0;
            {
                std::lock_guard lock(mutex_);
                foregroundVisualPending = foregroundVisualPending ||
                    !interactiveQueue_.empty() || !visibleQueue_.empty();
            }
            if (foregroundVisualPending) readAhead_.Pause();
            else readAhead_.Resume();
        } catch (...) {
            // A conservative failure mode is to keep read-ahead paused. Visual
            // delivery is more important than a speculative I/O hint.
            readAhead_.Pause();
        }
    }
    void Worker::ConfigureReadAheadLocked() {
        const unsigned requested = runtimeReadAheadFiles_.load(std::memory_order_acquire);
        if (requested == 0) {
            readAhead_.CancelAll();
            return;
        }
        const unsigned storageLimit = std::max(1u, storageProfile_.readAheadFiles);
        const unsigned active = std::min(requested, storageLimit);
        readAhead_.Configure(profile_.cpuTopology, active,
            storageProfile_.remote || storageProfile_.removable ? 16ull * kMebibyte :
                (storageProfile_.seekPenalty ? 32ull * kMebibyte : 64ull * kMebibyte),
            std::min(profile_.reusableBufferLimit,
                runtimeReusableBufferLimit_.load(std::memory_order_acquire)));
    }

    void Worker::SetStorageProfile(const quicksift::StorageProfile& storage) {
        {
            std::lock_guard readAheadLock(readAheadGateMutex_);
            storageProfile_ = storage;
            mappedInputLimit_.store(std::min(storage.mappedFileLimit,
                policyMappedInputLimit_.load(std::memory_order_acquire)), std::memory_order_release);
            readAhead_.CancelAll();
            ConfigureReadAheadLocked();
        }
        RefreshReadAheadGate();
    }

    void Worker::ApplyPerformancePolicy(const quicksift::core::PerformancePolicy& policy) {
        {
            std::lock_guard lock(mutex_);
            performancePolicy_ = policy;
            cacheWriteQueueLimit_ = std::max<size_t>(1, policy.cacheWriteQueueLimit);
            maintenanceWriteThreshold_ = cacheWriteQueueLimit_ > SIZE_MAX / 8 ? SIZE_MAX :
                std::max<size_t>(128ull * kMebibyte, cacheWriteQueueLimit_ * 8);
            auto clearQueue = [&](std::deque<WorkJob>& queue) {
                for (const WorkJob& job : queue) pending_.erase(job.key.empty() ? JobKey(job) : job.key);
                queue.clear();
            };
            if (!policy.allowPredictive) clearQueue(predictiveQueue_);
            if (!policy.allowAnalysis) clearQueue(analysisQueue_);
            if (!policy.allowSpeculativeCacheWrites) {
                for (auto it = cacheWriteQueue_.begin(); it != cacheWriteQueue_.end();) {
                    if (it->cacheClass == CacheClass::Thumbnail) {
                        ++it;
                    } else {
                        cacheWriteBytes_ = it->bytes > cacheWriteBytes_ ? 0 : cacheWriteBytes_ - it->bytes;
                        it = cacheWriteQueue_.erase(it);
                    }
                }
            }
        }
        allowPredictive_.store(policy.allowPredictive, std::memory_order_release);
        allowAnalysis_.store(policy.allowAnalysis, std::memory_order_release);
        allowDerivatives_.store(policy.allowDerivatives, std::memory_order_release);
        allowSpeculativeCacheWrites_.store(policy.allowSpeculativeCacheWrites, std::memory_order_release);
        runtimeReadAheadFiles_.store(policy.readAheadFiles, std::memory_order_release);
        runtimeDerivativeLevels_.store(policy.derivativeLevels, std::memory_order_release);
        policyMappedInputLimit_.store(policy.mappedInputLimit, std::memory_order_release);
        runtimeReusableBufferLimit_.store(policy.reusableBufferLimit, std::memory_order_release);
        gRetainedWicDecoderSessions.store(policy.retainedDecoderSessions, std::memory_order_release);
        quicksift::ConfigureBundledCodecRuntime(policy.codecThreadsPerDecode,
            profile_.maximumDecodedPixelBytes, policy.mappedInputLimit, profile_.enableTurboJpeg);
        if (decodeBudget_) decodeBudget_->SetPolicy(policy.decodeByteLimit,
            policy.activeDecodePermits, policy.allowOversizedDecode);
        {
            std::lock_guard readAheadLock(readAheadGateMutex_);
            mappedInputLimit_.store(std::min(storageProfile_.mappedFileLimit,
                policy.mappedInputLimit), std::memory_order_release);
            readAhead_.CancelAll();
            ConfigureReadAheadLocked();
        }
        decoderSessionTrimEpoch_.fetch_add(1, std::memory_order_acq_rel);
        cv_.notify_all();
        cacheCv_.notify_all();
        RefreshReadAheadGate();
    }

    WorkerTelemetrySnapshot Worker::Telemetry() const {
        WorkerTelemetrySnapshot snapshot;
        {
            std::lock_guard telemetryLock(telemetryMutex_);
            snapshot = telemetry_;
        }
        {
            std::lock_guard lock(mutex_);
            snapshot.interactiveQueued = interactiveQueue_.size();
            snapshot.visibleQueued = visibleQueue_.size();
            snapshot.predictiveQueued = predictiveQueue_.size();
            snapshot.faceQueued = faceQueue_.size();
            snapshot.idleQueued = idleQueue_.size();
            snapshot.analysisQueued = analysisQueue_.size() + analysisInFlight_;
            snapshot.activeVisualJobs = activeVisualJobs_.load(std::memory_order_acquire);
            snapshot.inFlightVisualFamilies = activeVisualFamilies_.size();
        }
        snapshot.persistentCacheHits = persistentCacheHits_.load(std::memory_order_relaxed);
        snapshot.persistentCacheMisses = persistentCacheMisses_.load(std::memory_order_relaxed);
        return snapshot;
    }

    void Worker::SetThumbnailDragLoadMode(int mode) {
        const int clamped = std::clamp(mode, 0, 2);
        thumbnailDragLoadMode_.store(clamped, std::memory_order_release);
        // Default concurrent caps when the UI has not published a size-aware value yet.
        const unsigned fallback = clamped <= 0 ? 0u : (clamped == 1 ? 1u : 2u);
        thumbnailScrollConcurrentCap_.store(fallback, std::memory_order_release);
        cv_.notify_all();
    }

    void Worker::SetThumbnailScrollConcurrentCap(unsigned maxConcurrent) {
        thumbnailScrollConcurrentCap_.store(maxConcurrent, std::memory_order_release);
        cv_.notify_all();
    }

    void Worker::SetThumbnailDragActive(bool active) {
        const bool previous = thumbnailDragActive_.exchange(active, std::memory_order_acq_rel);
        if (previous == active) {
            if (active && decodeBudget_) decodeBudget_->NotifyWaiters();
            return;
        }

        if (active) {
            // A scrollbar drag is a high-frequency UI gesture. Any queued thumbnail
            // work represents an obsolete visual state once the thumb starts moving.
            // Drop it immediately rather than allowing a large catalog to decode
            // hundreds of stale thumbnails while the pointer is moving.
            readAhead_.Pause();
            std::lock_guard lock(mutex_);
            auto discardQueuedThumbnails = [&](std::deque<WorkJob>& queue) {
                for (auto it = queue.begin(); it != queue.end();) {
                    if (it->cacheClass == CacheClass::Thumbnail &&
                        (it->kind == JobKind::DecodePreview || it->kind == JobKind::DecodeFull)) {
                        pending_.erase(it->key.empty() ? JobKey(*it) : it->key);
                        it = queue.erase(it);
                    } else {
                        ++it;
                    }
                }
            };
            discardQueuedThumbnails(interactiveQueue_);
            discardQueuedThumbnails(visibleQueue_);
            discardQueuedThumbnails(predictiveQueue_);
            discardQueuedThumbnails(faceQueue_);
            discardQueuedThumbnails(idleQueue_);
            discardQueuedThumbnails(analysisQueue_);
        }

        if (decodeBudget_) decodeBudget_->NotifyWaiters();
        cv_.notify_all();
        RefreshReadAheadGate();
    }

    void Worker::QueueEncodedReadAhead(const fs::path& path) {
        if (runtimeReadAheadFiles_.load(std::memory_order_acquire) == 0 ||
            (decodeBudget_ && decodeBudget_->OversizedActive()) ||
            IsPathBlockedForDecoder(path)) return;
        {
            std::lock_guard lock(mutex_);
            if (activeVisualJobs_.load(std::memory_order_acquire) != 0 ||
                !interactiveQueue_.empty() || !visibleQueue_.empty()) return;
        }
        std::lock_guard readAheadLock(readAheadGateMutex_);
        readAhead_.Queue(path);
    }

    void Worker::MarkInteractiveActivity() {
        readAhead_.Pause();
        {
            std::lock_guard lock(mutex_);
            idleNotBefore_ = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(idleDelayMilliseconds_);
        }
        cv_.notify_all();
    }

    void Worker::ApplyThumbnailViewport(std::uint64_t viewportEpoch,
        const std::vector<std::wstring>& desiredPaths,
        const std::vector<std::wstring>& visiblePaths) {
        const std::uint64_t epoch = std::max<std::uint64_t>(1, viewportEpoch);
        std::unordered_set<std::wstring> desired;
        std::unordered_set<std::wstring> visible;
        desired.reserve(desiredPaths.size());
        visible.reserve(visiblePaths.size());
        for (const std::wstring& path : desiredPaths) {
            if (!path.empty()) desired.insert(ThumbnailPathKey(fs::path(path)));
        }
        for (const std::wstring& path : visiblePaths) {
            if (!path.empty()) visible.insert(ThumbnailPathKey(fs::path(path)));
        }
        bool desiredChanged = false;
        bool visibleChanged = false;
        {
            std::lock_guard viewportLock(thumbnailViewportMutex_);
            desiredChanged = desiredThumbnailPaths_ != desired;
            visibleChanged = visibleThumbnailPaths_ != visible;
            desiredThumbnailPaths_.swap(desired);
            visibleThumbnailPaths_.swap(visible);
        }
        thumbnailViewportActive_.store(!visible.empty(), std::memory_order_release);
        const std::uint64_t previousEpoch = thumbnailViewportEpoch_.exchange(
            epoch, std::memory_order_acq_rel);
        if (previousEpoch == epoch && !desiredChanged && !visibleChanged) return;

        {
            std::lock_guard lock(mutex_);
            // A viewport change is a new thumbnail job list. Do not preserve the
            // old queue order: overlapping work may have been scheduled for a
            // previous left-to-right frontier. Drop queued thumbnail decodes and
            // let ScheduleVisibleWork repopulate the exact new order immediately.
            auto discardQueuedThumbnails = [&](std::deque<WorkJob>& queue) {
                for (auto it = queue.begin(); it != queue.end();) {
                    if (it->cacheClass == CacheClass::Thumbnail &&
                        (it->kind == JobKind::DecodePreview || it->kind == JobKind::DecodeFull)) {
                        pending_.erase(it->key.empty() ? JobKey(*it) : it->key);
                        it = queue.erase(it);
                    } else {
                        ++it;
                    }
                }
            };
            discardQueuedThumbnails(interactiveQueue_);
            discardQueuedThumbnails(visibleQueue_);
            discardQueuedThumbnails(predictiveQueue_);
            discardQueuedThumbnails(faceQueue_);
            discardQueuedThumbnails(idleQueue_);
            discardQueuedThumbnails(analysisQueue_);

            // Cache writes for the old viewport should not keep stale decoded
            // bitmaps alive while the new visible window is being populated. Keep
            // overlapping requested thumbnails; stale off-window writes are dropped.
            for (auto it = cacheWriteQueue_.begin(); it != cacheWriteQueue_.end();) {
                if (it->cacheClass == CacheClass::Thumbnail && it->viewportEpoch < epoch) {
                    if (IsThumbnailPathCurrent(it->path)) {
                        it->viewportEpoch = epoch;
                        bool isVisible = false;
                        try {
                            std::lock_guard viewportLock(thumbnailViewportMutex_);
                            isVisible = visibleThumbnailPaths_.contains(ThumbnailPathKey(it->path));
                        } catch (...) {
                        }
                        it->priority = isVisible ? JobPriority::Interactive : JobPriority::Predictive;
                        ++it;
                    } else {
                        cacheWriteBytes_ = it->bytes > cacheWriteBytes_ ? 0 :
                            cacheWriteBytes_ - it->bytes;
                        it = cacheWriteQueue_.erase(it);
                    }
                } else {
                    ++it;
                }
            }
        }
        if (decodeBudget_) decodeBudget_->NotifyWaiters();
        cv_.notify_all();
        cacheCv_.notify_all();
        RefreshReadAheadGate();
    }

    bool Worker::IsThumbnailJobPendingOrActive(const fs::path& path) const {
        if (path.empty()) return false;
        WorkJob probe;
        probe.kind = JobKind::DecodePreview;
        probe.cacheClass = CacheClass::Thumbnail;
        const std::wstring familyPrefix = path.wstring() + L"|" +
            std::to_wstring(static_cast<int>(probe.kind)) + L"|" +
            std::to_wstring(static_cast<int>(probe.cacheClass)) + L"|";
        std::lock_guard lock(mutex_);
        if (std::any_of(pending_.begin(), pending_.end(), [&](const std::wstring& key) {
            return key.rfind(familyPrefix, 0) == 0;
        })) return true;
        return std::any_of(activeVisualFamilies_.begin(), activeVisualFamilies_.end(),
            [&](const auto& entry) { return entry.first.rfind(familyPrefix, 0) == 0; });
    }

    bool Worker::IsThumbnailPathCurrentKey(const std::wstring& key) const noexcept {
        try {
            std::lock_guard viewportLock(thumbnailViewportMutex_);
            return desiredThumbnailPaths_.contains(key);
        } catch (...) {
            return false;
        }
    }

    bool Worker::IsThumbnailPathCurrent(const fs::path& path) const noexcept {
        try {
            return IsThumbnailPathCurrentKey(ThumbnailPathKey(path));
        } catch (...) {
            return false;
        }
    }

    bool Worker::BeginExclusivePathWrite(const fs::path& path,
        std::chrono::milliseconds timeout) {
        return BeginExclusivePathWrites(std::vector<fs::path>{ path }, timeout);
    }

    bool Worker::ExclusivePathsReadyLocked(const std::vector<std::wstring>& keys) const {
        const auto matches = [&](const fs::path& candidate) {
            return std::any_of(keys.begin(), keys.end(), [&](const std::wstring& key) {
                return _wcsicmp(candidate.c_str(), key.c_str()) == 0;
            });
        };
        // Metadata reads are intentionally absent from activeHandleJobs_. A stuck
        // Exiv2/XMP read must not hold the culling write gate; writer preference
        // on the metadata library already stops new reads from starting.
        for (const auto& [activePath, count] : activeHandleJobs_) {
            if (count != 0 && matches(fs::path(activePath))) return false;
        }
        for (const auto& threadPaths : decoderSessionPathsByThread_) {
            if (std::any_of(threadPaths.begin(), threadPaths.end(), matches)) return false;
        }
        return true;
    }

    bool Worker::BeginExclusivePathWrites(const std::vector<fs::path>& paths,
        std::chrono::milliseconds timeout) {
        std::vector<std::wstring> keys;
        std::vector<fs::path> uniquePaths;
        keys.reserve(paths.size());
        uniquePaths.reserve(paths.size());
        for (const fs::path& path : paths) {
            if (path.empty()) continue;
            const std::wstring key = path.native();
            const bool duplicate = std::any_of(keys.begin(), keys.end(), [&](const std::wstring& existing) {
                return _wcsicmp(existing.c_str(), key.c_str()) == 0;
            });
            if (!duplicate) {
                keys.push_back(key);
                uniquePaths.push_back(path);
            }
        }
        if (keys.empty()) return false;

        const auto deadline = std::chrono::steady_clock::now() +
            std::max(timeout, std::chrono::milliseconds::zero());
        std::size_t registered = 0;
        bool writePreferenceHeld = false;
        auto releaseWritePreference = [&]() noexcept {
            if (!writePreferenceHeld) return;
            writePreferenceHeld = false;
            quicksift::ReleaseEmbeddedMetadataWritePreference();
        };
        try {
            {
                std::lock_guard blockedLock(blockedMutex_);
                for (const std::wstring& key : keys) {
                    const auto found = FindBlockedPathLocked(key.c_str());
                    if (found == blockedPaths_.end()) blockedPaths_.emplace(key, 1u);
                    else ++found->second;
                    ++registered;
                    exclusivePathWrites_.fetch_add(1, std::memory_order_acq_rel);
                }
            }
            if (decodeBudget_) decodeBudget_->NotifyWaiters();
            // Announce the write before draining readers so the metadata lane and
            // the Exiv2 gate both yield. Decode/face handles still block below.
            quicksift::PreferEmbeddedMetadataWrites();
            writePreferenceHeld = true;
            bool readAheadReleased = false;
            {
                std::lock_guard readAheadLock(readAheadGateMutex_);
                const auto remaining = deadline > std::chrono::steady_clock::now() ?
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - std::chrono::steady_clock::now()) :
                    std::chrono::milliseconds::zero();
                readAheadReleased = readAhead_.BlockPathsAndWait(uniquePaths,
                    std::min(remaining, std::chrono::milliseconds(750)));
            }
            if (!readAheadReleased) {
                QS_LOG_WARNING(L"Worker", L"Timed out draining path-scoped read-ahead; the source write was not started.");
                releaseWritePreference();
                for (std::size_t index = 0; index < registered; ++index)
                    ReleaseExclusivePathWriteKey(keys[index]);
                return false;
            }
            {
                std::lock_guard desiredLock(desiredMutex_);
                for (const std::wstring& key : keys) {
                    const std::wstring prefix = key + L'|';
                    for (auto iterator = desiredVisualTargets_.begin();
                        iterator != desiredVisualTargets_.end();) {
                        if (iterator->first.starts_with(prefix)) iterator = desiredVisualTargets_.erase(iterator);
                        else ++iterator;
                    }
                }
            }
            std::vector<WorkJob> removedJobs;
            {
                std::lock_guard lock(mutex_);
                auto removePaths = [&](std::deque<WorkJob>& queue) {
                    for (auto iterator = queue.begin(); iterator != queue.end();) {
                        const bool handleHolding = iterator->kind == JobKind::DecodePreview ||
                            iterator->kind == JobKind::DecodeFull || iterator->kind == JobKind::DecodeTile ||
                            iterator->kind == JobKind::Face || iterator->kind == JobKind::Metadata ||
                            iterator->kind == JobKind::Exif;
                        const bool blocked = handleHolding && std::any_of(keys.begin(), keys.end(),
                            [&](const std::wstring& key) {
                                return _wcsicmp(iterator->path.c_str(), key.c_str()) == 0;
                            });
                        if (blocked) {
                            removedJobs.push_back(std::move(*iterator));
                            iterator = queue.erase(iterator);
                        } else {
                            ++iterator;
                        }
                    }
                };
                removePaths(interactiveQueue_);
                removePaths(visibleQueue_);
                removePaths(predictiveQueue_);
                removePaths(faceQueue_);
                removePaths(idleQueue_);
                removePaths(analysisQueue_);
            }
            for (const WorkJob& removed : removedJobs) {
                AcknowledgeUnpostedJob(removed, removed.key.empty() ? JobKey(removed) : removed.key);
                TryPostMetadataAbort(removed);
                FinishAnalysisJob(removed);
            }

            decoderSessionTrimEpoch_.fetch_add(1, std::memory_order_acq_rel);
            cv_.notify_all();
            std::unique_lock blockedLock(blockedMutex_);
            const bool released = blockedCv_.wait_until(blockedLock, deadline, [&] {
                return stoppingAtomic_.load(std::memory_order_acquire) ||
                    ExclusivePathsReadyLocked(keys);
            });
            if (released && !stoppingAtomic_.load(std::memory_order_acquire)) return true;
        } catch (...) {
            releaseWritePreference();
            for (std::size_t index = 0; index < registered; ++index) {
                ReleaseExclusivePathWriteKey(keys[index]);
            }
            throw;
        }

        releaseWritePreference();
        for (std::size_t index = 0; index < registered; ++index) {
            ReleaseExclusivePathWriteKey(keys[index]);
        }
        return false;
    }

    void Worker::EndExclusivePathWrite(const fs::path& path) noexcept {
        if (path.empty()) return;
        EndExclusivePathWrites(std::vector<fs::path>{ path });
    }

    void Worker::EndExclusivePathWrites(const std::vector<fs::path>& paths) noexcept {
        quicksift::ReleaseEmbeddedMetadataWritePreference();
        std::vector<std::wstring> released;
        released.reserve(paths.size());
        for (const fs::path& path : paths) {
            if (path.empty()) continue;
            const std::wstring key = path.native();
            const bool duplicate = std::any_of(released.begin(), released.end(),
                [&](const std::wstring& existing) {
                    return _wcsicmp(existing.c_str(), key.c_str()) == 0;
                });
            if (!duplicate) {
                released.push_back(key);
                ReleaseExclusivePathWriteKey(key);
            }
        }
    }

    void Worker::ReleaseExclusivePathWriteKey(const std::wstring& key) noexcept {
        try {
            {
                std::lock_guard blockedLock(blockedMutex_);
                const auto found = FindBlockedPathLocked(key.c_str());
                if (found != blockedPaths_.end()) {
                    if (found->second <= 1) blockedPaths_.erase(found);
                    else --found->second;
                }
            }
            {
                std::lock_guard readAheadLock(readAheadGateMutex_);
                readAhead_.UnblockPaths(std::vector<fs::path>{ fs::path(key) });
            }
        } catch (...) {
        }
        unsigned active = exclusivePathWrites_.load(std::memory_order_acquire);
        while (active != 0 && !exclusivePathWrites_.compare_exchange_weak(active, active - 1,
            std::memory_order_acq_rel, std::memory_order_acquire)) {}
        if (decodeBudget_) decodeBudget_->NotifyWaiters();
        blockedCv_.notify_all();
        cv_.notify_all();
    }

    void Worker::PublishDecoderSessionPaths(size_t threadIndex,
        const std::vector<std::filesystem::path>& paths) noexcept {
        try {
            std::lock_guard lock(blockedMutex_);
            if (threadIndex >= decoderSessionPathsByThread_.size())
                decoderSessionPathsByThread_.resize(threadIndex + 1);
            decoderSessionPathsByThread_[threadIndex] = paths;
        } catch (...) {
        }
        blockedCv_.notify_all();
    }

    bool Worker::IsPathBlockedForDecoder(const fs::path& path) const noexcept {
        try {
            std::lock_guard lock(blockedMutex_);
            return FindBlockedPathLocked(path.c_str()) != blockedPaths_.end();
        } catch (...) {
            return true;
        }
    }

    bool Worker::TryBeginHandleJob(const WorkJob& job) {
        // Metadata reads must not register as exclusive-write blockers. Rating
        // and pick updates are culling operations and outrank a stuck XMP read.
        // Decode, face, and EXIF jobs still hold a real file/session handle.
        const bool handleHolding = job.kind == JobKind::DecodePreview ||
            job.kind == JobKind::DecodeFull || job.kind == JobKind::DecodeTile ||
            job.kind == JobKind::Face || job.kind == JobKind::Exif;
        if (job.kind == JobKind::Metadata) {
            std::lock_guard lock(blockedMutex_);
            if (FindBlockedPathLocked(job.path.c_str()) != blockedPaths_.end()) return false;
            return true;
        }
        if (!handleHolding) return true;
        std::lock_guard lock(blockedMutex_);
        if (FindBlockedPathLocked(job.path.c_str()) != blockedPaths_.end()) return false;
        auto found = std::find_if(activeHandleJobs_.begin(), activeHandleJobs_.end(),
            [&](const auto& item) { return _wcsicmp(item.first.c_str(), job.path.c_str()) == 0; });
        if (found == activeHandleJobs_.end()) activeHandleJobs_.emplace(job.path.native(), 1u);
        else ++found->second;
        return true;
    }

    void Worker::EndHandleJob(const WorkJob& job) noexcept {
        const bool handleHolding = job.kind == JobKind::DecodePreview ||
            job.kind == JobKind::DecodeFull || job.kind == JobKind::DecodeTile ||
            job.kind == JobKind::Face || job.kind == JobKind::Exif;
        if (job.kind == JobKind::Metadata || !handleHolding) return;
        try {
            std::lock_guard lock(blockedMutex_);
            auto found = std::find_if(activeHandleJobs_.begin(), activeHandleJobs_.end(),
                [&](const auto& item) { return _wcsicmp(item.first.c_str(), job.path.c_str()) == 0; });
            if (found != activeHandleJobs_.end()) {
                if (found->second <= 1) activeHandleJobs_.erase(found);
                else --found->second;
            }
        } catch (...) {
        }
        blockedCv_.notify_all();
    }

    void Worker::TrimForMemoryPressure(bool critical) {
        {
            std::lock_guard lock(mutex_);
            auto clearQueuedJobs = [&](std::deque<WorkJob>& queue) {
                for (const WorkJob& job : queue) pending_.erase(job.key.empty() ? JobKey(job) : job.key);
                queue.clear();
            };
            clearQueuedJobs(predictiveQueue_);
            // Metadata and the currently requested EXIF text have no automatic
            // completion retry. Dropping them made the UI retain a permanently
            // non-zero pending count after a pressure event. Keep those small,
            // bounded jobs and discard only pixel/face prefetch work.
            for (auto it = idleQueue_.begin(); it != idleQueue_.end();) {
                if (it->kind == JobKind::Metadata || it->kind == JobKind::Exif) {
                    ++it;
                } else {
                    pending_.erase(it->key.empty() ? JobKey(*it) : it->key);
                    it = idleQueue_.erase(it);
                }
            }
            clearQueuedJobs(analysisQueue_);
            if (critical) clearQueuedJobs(faceQueue_);

            const size_t targetBytes = critical ? 0 : cacheWriteQueueLimit_ / 4;
            const auto writePriority = [](CacheClass cacheClass) {
                switch (cacheClass) {
                case CacheClass::Full: return 3;
                case CacheClass::Preview: return 2;
                case CacheClass::Thumbnail: return 1;
                case CacheClass::Tile: return 0;
                }
                return 0;
            };
            while (!cacheWriteQueue_.empty() && cacheWriteBytes_ > targetBytes) {
                auto victim = cacheWriteQueue_.begin();
                int victimPriority = writePriority(victim->cacheClass);
                for (auto it = std::next(cacheWriteQueue_.begin()); it != cacheWriteQueue_.end(); ++it) {
                    const int priority = writePriority(it->cacheClass);
                    if (priority < victimPriority) {
                        victim = it;
                        victimPriority = priority;
                        if (priority == 0) break;
                    }
                }
                cacheWriteBytes_ = victim->bytes > cacheWriteBytes_ ? 0 : cacheWriteBytes_ - victim->bytes;
                cacheWriteQueue_.erase(victim);
            }
            idleNotBefore_ = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(critical ? 2000 : idleDelayMilliseconds_ * 2);
        }
        cv_.notify_all();
        cacheCv_.notify_all();
    }

    void Worker::RecordTelemetry(const WorkJob& job, double queueWaitMs,
        double budgetWaitMs, double executionMs, bool cancelled, bool oversized) noexcept {
        try {
            std::lock_guard lock(telemetryMutex_);
            const bool decodeJob = job.kind == JobKind::DecodePreview ||
                job.kind == JobKind::DecodeFull || job.kind == JobKind::DecodeTile ||
                job.kind == JobKind::Face;
            if (decodeJob) {
                telemetry_.queueWaitMilliseconds = UpdateEwma(
                    telemetry_.queueWaitMilliseconds, queueWaitMs);
                telemetry_.decodeBudgetWaitMilliseconds = UpdateEwma(
                    telemetry_.decodeBudgetWaitMilliseconds, budgetWaitMs);
                telemetry_.decodeMilliseconds = UpdateEwma(
                    telemetry_.decodeMilliseconds, executionMs);
                const std::size_t index = TelemetryFormatIndex(job.path);
                telemetry_.decodeMillisecondsByFormat[index] = UpdateEwma(
                    telemetry_.decodeMillisecondsByFormat[index], executionMs);
            }
            ++telemetry_.completedJobs;
            if (cancelled) ++telemetry_.cancelledJobs;
            if (oversized) ++telemetry_.oversizedReservations;
        } catch (...) {
        }
    }

} // namespace quicksift::app
