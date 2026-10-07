// OWNER: Face-detector capability UI, scheduling gates, and bounded retry handling.
#include "app/quicksift_application_internal.h"
#include "face_detector.h"

namespace quicksift::app {

std::wstring QuickSiftApplicationImpl::FaceLockAvailabilityTooltipText() const {
    using quicksift::core::FaceDetectorCapability;
    switch (faceAnalysisRetryPolicy_.Capability()) {
    case FaceDetectorCapability::Available:
        return Tr(L"Face Lock centers the view on the largest detected face. Detection runs locally through Windows Media FaceAnalysis.");
    case FaceDetectorCapability::Unavailable:
        return Tr(L"Face Lock is unavailable because Windows reports that face detection is not supported on this device.");
    case FaceDetectorCapability::TemporarilyFailed:
        return Tr(L"Windows face detection could not be initialized. QuickSift will retry automatically.");
    case FaceDetectorCapability::Unknown:
    default:
        return Tr(L"Checking Windows face-detection support…");
    }
}

void QuickSiftApplicationImpl::CreateFaceLockAvailabilityTooltip() {
    const auto control = controls_.find(ID_FACE_LOCK);
    if (control == controls_.end() || faceLockAvailabilityTooltip_) return;
    faceLockAvailabilityTooltipText_ = FaceLockAvailabilityTooltipText();
    quicksift::ui::framework::TooltipControlSpec spec{};
    spec.owner = hwnd_;
    spec.target = control->second;
    spec.instance = instance_;
    spec.toolId = static_cast<UINT_PTR>(ID_FACE_LOCK);
    spec.text = &faceLockAvailabilityTooltipText_;
    faceLockAvailabilityTooltip_ = uiFramework_.CreateTooltip(spec);
    if (!faceLockAvailabilityTooltip_) {
        quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Warning,
            L"UI", L"Creating the Face Lock availability tooltip");
    }
}

void QuickSiftApplicationImpl::RefreshFaceLockAvailabilityUi() {
    const auto control = controls_.find(ID_FACE_LOCK);
    if (control == controls_.end()) return;
    using quicksift::core::FaceDetectorCapability;
    const FaceDetectorCapability capability = faceAnalysisRetryPolicy_.Capability();
    const bool available = capability == FaceDetectorCapability::Available;
    if (!available && reviewState_.FaceLockEnabled()) DisableFaceLockPreservingViews();
    EnableWindow(control->second, available ? TRUE : FALSE);
    const wchar_t* label = available ? (reviewState_.FaceLockEnabled() ? L"Face Lock: On" : L"Face Lock: Off") :
        (capability == FaceDetectorCapability::TemporarilyFailed ?
            L"Face Lock: Temporarily unavailable" : L"Face Lock: Unavailable");
    SetWindowTextW(control->second, Tr(label).c_str());
    InvalidateRect(control->second, nullptr, FALSE);

    faceLockAvailabilityTooltipText_ = FaceLockAvailabilityTooltipText();
    if (faceLockAvailabilityTooltip_) {
        TOOLINFOW tool{ sizeof(TOOLINFOW) };
        tool.hwnd = control->second;
        tool.uId = static_cast<UINT_PTR>(ID_FACE_LOCK);
        tool.lpszText = faceLockAvailabilityTooltipText_.data();
        SendMessageW(faceLockAvailabilityTooltip_, TTM_UPDATETIPTEXTW, 0,
            reinterpret_cast<LPARAM>(&tool));
    }
}

void QuickSiftApplicationImpl::RefreshFaceDetectorCapability(bool force) {
    const auto now = quicksift::core::FaceAnalysisRetryPolicy::Clock::now();
    if (!force && !faceAnalysisRetryPolicy_.CapabilityRefreshDue(now)) return;
    const auto previous = faceAnalysisRetryPolicy_.Capability();
    const bool refreshAdapter = force ||
        previous == quicksift::core::FaceDetectorCapability::TemporarilyFailed;
    const auto capability = quicksift::QueryFaceDetectorCapability(refreshAdapter);
    faceAnalysisRetryPolicy_.SetCapability(capability, now);
    if (capability != previous) {
        const wchar_t* state = L"unknown";
        switch (capability) {
        case quicksift::core::FaceDetectorCapability::Available: state = L"available"; break;
        case quicksift::core::FaceDetectorCapability::Unavailable: state = L"unavailable"; break;
        case quicksift::core::FaceDetectorCapability::TemporarilyFailed: state = L"temporarily failed"; break;
        case quicksift::core::FaceDetectorCapability::Unknown: break;
        }
        QS_LOG_INFO(L"FaceDetection", std::wstring(L"Windows face detector capability: ") + state);
        RefreshFaceLockAvailabilityUi();
        UpdateStatus();
    }
}

JobPriority QuickSiftApplicationImpl::FaceAnalysisPriority() const noexcept {
    if (!reviewState_.FaceLockEnabled()) return JobPriority::Idle;
    if (reviewState_.Mode() == ViewMode::Single || reviewState_.Mode() == ViewMode::Compare) {
        return JobPriority::Interactive;
    }
    return JobPriority::Face;
}

bool QuickSiftApplicationImpl::EnqueueFaceAnalysis(const fs::path& path, uint64_t epoch,
    JobPriority priority, int targetSize, bool explicitRequest) {
    RefreshFaceDetectorCapability(false);
    const auto now = quicksift::core::FaceAnalysisRetryPolicy::Clock::now();
    const std::wstring key = path.wstring();
    if (explicitRequest) faceAnalysisRetryPolicy_.AllowExplicitRetry(key, now);
    if (!faceAnalysisRetryPolicy_.CanSchedule(key, now)) return false;
    worker_.EnqueueFace(path, generation_, epoch, priority, targetSize);
    return true;
}

void QuickSiftApplicationImpl::HandleFaceAnalysisOutcome(const WorkResult& result) {
    if (result.faceAnalysisOutcome == quicksift::core::FaceAnalysisOutcome::None) return;
    const auto now = quicksift::core::FaceAnalysisRetryPolicy::Clock::now();
    const auto previousCapability = faceAnalysisRetryPolicy_.Capability();
    const std::wstring path = result.path.wstring();
    faceAnalysisRetryPolicy_.RecordOutcome(path, result.faceAnalysisOutcome, now);
    if (result.faceAnalysisOutcome == quicksift::core::FaceAnalysisOutcome::TransientFailure) {
        const unsigned failures = faceAnalysisRetryPolicy_.FailureCount(path);
        QS_LOG_WARNING(L"FaceDetection", L"Face analysis failed for " + path +
            L"; transient failure count " + std::to_wstring(failures));
        if (faceAnalysisRetryPolicy_.Exhausted(path)) {
            QS_LOG_WARNING(L"FaceDetection", L"Automatic face-analysis retries exhausted for " + path);
        }
    }
    if (faceAnalysisRetryPolicy_.Capability() != previousCapability) {
        RefreshFaceLockAvailabilityUi();
    }
    UpdateStatus();
}

} // namespace quicksift::app
