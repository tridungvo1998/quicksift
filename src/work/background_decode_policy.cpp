// OWNER: RAW decode-mode switching and visual decode coalescing policy.
#include "work/background_work_engine.h"

namespace quicksift::app {

    void Worker::SetRawJpegPreviewOnly(bool enabled) {
        const bool previous = rawJpegPreviewOnly_.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled) return;
        {
            std::lock_guard lock(mutex_);
            auto removeStaleRawPolicy = [&](std::deque<WorkJob>& queue) {
                for (auto it = queue.begin(); it != queue.end();) {
                    const bool decodeJob = it->kind == JobKind::DecodePreview ||
                        it->kind == JobKind::DecodeFull || it->kind == JobKind::DecodeTile ||
                        it->kind == JobKind::Face;
                    if (decodeJob && IsRawExtension(ExtensionLower(it->path)) &&
                        it->rawJpegPreviewOnly != enabled) {
                        pending_.erase(it->key.empty() ? JobKey(*it) : it->key);
                        it = queue.erase(it);
                    } else {
                        ++it;
                    }
                }
            };
            removeStaleRawPolicy(interactiveQueue_);
            removeStaleRawPolicy(visibleQueue_);
            removeStaleRawPolicy(predictiveQueue_);
            removeStaleRawPolicy(faceQueue_);
            removeStaleRawPolicy(idleQueue_);
            removeStaleRawPolicy(analysisQueue_);
            for (auto it = cacheWriteQueue_.begin(); it != cacheWriteQueue_.end();) {
                if (IsRawExtension(ExtensionLower(it->path))) {
                    cacheWriteBytes_ = it->bytes > cacheWriteBytes_ ? 0 : cacheWriteBytes_ - it->bytes;
                    it = cacheWriteQueue_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        {
            std::lock_guard desiredLock(desiredMutex_);
            desiredVisualTargets_.clear();
        }
        RequestDecoderSessionTrim();
        if (decodeBudget_) decodeBudget_->NotifyWaiters();
        cv_.notify_all();
    }

    std::wstring Worker::VisualFamilyKey(const WorkJob& job) {
        if (!IsCoalescibleVisualJob(job)) return {};
        std::wstring key = job.path.wstring();
        key += L'|'; key += std::to_wstring(static_cast<int>(job.kind));
        key += L'|'; key += std::to_wstring(static_cast<int>(job.cacheClass));
        key += L'|'; key += std::to_wstring(job.generation);
        key += L'|'; key += std::to_wstring(job.requestEpoch);
        key += L'|';
        if (job.cacheClass == CacheClass::Thumbnail) key += L"0";
        else key += std::to_wstring(job.viewportEpoch);
        key += L'|';
        key += (IsRawExtension(ExtensionLower(job.path)) && job.rawJpegPreviewOnly) ?
            L"raw-jpeg-only" : L"raw-normal";
        return key;
    }

    bool Worker::IsCoalescibleVisualJob(const WorkJob& job) {
        return (job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull) &&
            job.tileWidth == 0 && job.tileHeight == 0;
    }

    bool Worker::SameDecodeFamily(const WorkJob& a, const WorkJob& b) {
        const bool aRawJpegOnly = IsRawExtension(ExtensionLower(a.path)) && a.rawJpegPreviewOnly;
        const bool bRawJpegOnly = IsRawExtension(ExtensionLower(b.path)) && b.rawJpegPreviewOnly;
        return IsCoalescibleVisualJob(a) && IsCoalescibleVisualJob(b) &&
            a.kind == b.kind && a.cacheClass == b.cacheClass && a.path == b.path &&
            a.generation == b.generation && a.requestEpoch == b.requestEpoch &&
            aRawJpegOnly == bRawJpegOnly &&
            (a.cacheClass == CacheClass::Thumbnail || a.viewportEpoch == b.viewportEpoch);
    }


} // namespace quicksift::app
