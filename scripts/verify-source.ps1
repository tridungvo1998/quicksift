[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$SourceDir = (Resolve-Path $SourceDir).Path

function Read-SourceText([string]$RelativePath) {
    $Path = Join-Path $SourceDir $RelativePath
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing required source file: $RelativePath"
    }
    return [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8)
}

$VersionHeader = Read-SourceText 'src\version.h'
if ($VersionHeader -notmatch '#define\s+QS_VERSION_STRING\s+"([^"]+)"') {
    throw 'Could not read QS_VERSION_STRING from src\version.h.'
}
$CanonicalProductVersion = $Matches[1]
if ($VersionHeader -notmatch '#define\s+QS_VERSION_FILE_STRING\s+"([^"]+)"') {
    throw 'Could not read QS_VERSION_FILE_STRING from src\version.h.'
}
$CanonicalFileVersion = $Matches[1]
if ($CanonicalFileVersion -notmatch '^\d+\.\d+\.\d+\.0$') {
    throw "Windows/MSIX file version must have a zero revision component; got '$CanonicalFileVersion'."
}
if ($VersionHeader -notmatch '#define\s+QS_VERSION_PACKAGE_STRING\s+"([^"]+)"') {
    throw 'Could not read QS_VERSION_PACKAGE_STRING from src\version.h.'
}
$CanonicalPackageVersion = $Matches[1]
$MsixVersion = $CanonicalFileVersion
$ReleaseSlug = $CanonicalProductVersion -replace '\s+', '-'

$RequiredFiles = @(
    'README.md', 'USER_GUIDE.md', 'BUILDING.md', 'DEVELOPER_GUIDE.md', 'ARCHITECTURE.md', 'TESTING.md',
    'CHANGELOG.md', 'SECURITY.md', 'PRE_RELEASE_CHECKLIST.md', 'THIRD_PARTY_NOTICES.md', 'LICENSE', 'SOURCE_CODE.txt',
    'CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json', '.vsconfig',
    'build.cmd', 'build-release.cmd', 'build.bat', 'package-msix.bat', 'open-in-visual-studio.cmd',
    'scripts\build.ps1', 'scripts\package-msix.ps1', 'scripts\verify-source-portable.py',
    'packaging\msix\AppxManifest.xml.in', 'packaging\msix\Assets\StoreLogo.png',
    'packaging\msix\Assets\Square44x44Logo.png', 'packaging\msix\Assets\Square150x150Logo.png',
    'src\main.cpp', 'src\app\application_support.cpp', 'src\app\application_text_io.cpp', 'src\app\application_support.h',
    'src\app\quicksift_application.cpp', 'src\app\quicksift_application.h',
    'src\app\quicksift_application_internal.h',
    'src\work\background_work_engine.cpp', 'src\work\background_decode_policy.cpp', 'src\work\background_work_engine.h',
    'src\work\folder_scanner.cpp', 'src\work\folder_scanner.h',
    'src\work\background_completion_queue.h',
    'src\review\thumbnail_layout.cpp', 'src\review\thumbnail_layout.h',
    'src\review\thumbnail_prefetch_policy.cpp', 'src\review\thumbnail_prefetch_policy.h',
    'src\review\review_state_model.cpp', 'src\review\review_state_model.h',
    'src\review\review_async_policy.cpp', 'src\review\review_async_policy.h',
    'src\review\view_transform_policy.cpp', 'src\review\view_transform_policy.h',
    'src\core\catalog_store.cpp', 'src\core\catalog_store_mutation.cpp', 'src\core\catalog_store.h',
    'src\core\path_identity.cpp', 'src\core\path_identity.h',
    'src\core\selection_store.cpp', 'src\core\selection_store.h',
    'src\core\history_store.cpp', 'src\core\history_store.h',
    'src\core\history_store_memory.cpp',
    'src\transactions\file_transaction.cpp', 'src\transactions\file_transaction.h',
    'src\transactions\file_history_replay.cpp', 'src\transactions\file_history_group_replay.cpp',
    'src\transactions\file_history_group_replay.h', 'src\transactions\file_history_replay.h',
    'src\transactions\file_transaction_service.cpp', 'src\transactions\file_transaction_service.h',
    'src\transactions\metadata_transaction_service.cpp',
    'src\transactions\metadata_transaction_execution.cpp',
    'src\transactions\metadata_transaction_service.h',
    'src\transactions\transaction_coordinator.cpp',
    'src\transactions\transaction_coordinator.h',
    'src\transactions\verified_file_operations_windows.cpp',
    'src\transactions\verified_file_move_windows.cpp',
    'src\transactions\verified_file_operations_windows.h',
    'src\platform\storage_space.cpp', 'src\platform\storage_space.h',
    'src\platform\locked_file_publication_windows.h',
    'src\ui\framework\ui_models.cpp', 'src\ui\framework\ui_models.h',
    'src\ui\framework\macos_ui_framework.cpp',
    'src\ui\framework\macos_ui_framework_gdi.cpp',
    'src\ui\framework\macos_ui_framework_popup.cpp',
    'src\ui\framework\macos_ui_framework_d2d.cpp',
    'src\ui\framework\macos_ui_framework.h'
)
foreach ($Relative in $RequiredFiles) { [void](Read-SourceText $Relative) }

$GeneratedDirectories = Get-ChildItem -LiteralPath $SourceDir -Recurse -Directory | Where-Object {
    $_.Name -in @('.vs', 'CMakeFiles', 'bin', 'dist') -or $_.Name -match '^build(?:-|$)'
}
if ($GeneratedDirectories) {
    throw "Generated build directories remain in the source tree: $($GeneratedDirectories.FullName -join ', ')"
}
$InlineFragments = Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src') -Recurse -File -Filter '*.inl'
if ($InlineFragments) {
    throw "Obsolete inline implementation fragments remain: $($InlineFragments.FullName -join ', ')"
}

$MainText = Read-SourceText 'src\main.cpp'
if ($MainText -match 'class\s+QuickSiftApplication') {
    throw 'src\main.cpp is no longer a thin process entry point.'
}
$ApplicationHeaderPath = Join-Path $SourceDir 'src\app\quicksift_application.h'
$ApplicationInternalHeaderPath = Join-Path $SourceDir 'src\app\quicksift_application_internal.h'
$ApplicationHeaderText = [IO.File]::ReadAllText($ApplicationHeaderPath)
$ApplicationInternalHeaderText = [IO.File]::ReadAllText($ApplicationInternalHeaderPath)
if (-not $ApplicationHeaderText.Contains('class QuickSiftApplicationImpl;') -or
    -not $ApplicationHeaderText.Contains('std::unique_ptr<QuickSiftApplicationImpl>')) {
    throw 'quicksift_application.h is no longer a thin PIMPL facade.'
}
if (-not $ApplicationInternalHeaderText.StartsWith('// OWNER:') -or
    -not $ApplicationInternalHeaderText.Contains('class QuickSiftApplicationImpl {')) {
    throw 'quicksift_application_internal.h lost its private migration boundary.'
}

$CMakeText = Read-SourceText 'CMakeLists.txt'
$CompiledModules = Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src') -Recurse -File -Filter '*.cpp'
foreach ($Path in $CompiledModules) {
    $RelativePath = $Path.FullName.Substring($SourceDir.Length + 1).Replace('\', '/')
    if (-not $CMakeText.Contains($RelativePath)) { throw "CMake does not register $RelativePath" }
}
$UnregisteredSources = Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src') -Recurse -File -Filter '*.cpp' |
    Where-Object { -not $CMakeText.Contains($_.FullName.Substring($SourceDir.Length + 1).Replace('\', '/')) }
if ($UnregisteredSources) {
    throw "Compiled source is not registered in CMake: $($UnregisteredSources.FullName -join ', ')"
}

foreach ($Token in @(
    'QuickSift.CatalogStore', 'QuickSift.HistoryStore', 'QuickSift.SelectionStore', 'QuickSift.BackgroundCompletionQueue',
    'QuickSift.FileTransaction', 'QuickSift.FileTransactionService',
    'QuickSift.MetadataTransaction', 'QuickSift.TransactionCoordinator',
    'QuickSift.ReviewSubsystem', 'QuickSift.UiModels',
    'QuickSift.WindowsFileOperations',
    'VS_STARTUP_PROJECT QuickSift', 'CMAKE_MSVC_RUNTIME_LIBRARY', 'QS_WARNINGS_AS_ERRORS',
    'find_package(libjpeg-turbo CONFIG REQUIRED)', 'find_package(libyuv CONFIG REQUIRED)',
    'configure_file(', 'resource.rc.in', 'QS_VERSION_RESOURCE',
    'QS_VERSION_RESOURCE_RES', 'QS_VERSION_RESOURCE_OBJECT', 'add_custom_command(',
    'CMAKE_RC_COMPILER', 'QS_CVTRES_COMPILER', 'QuickSiftVersion.res',
    'QuickSiftVersion.obj', 'EXTERNAL_OBJECT TRUE', 'list(APPEND QS_APP_SOURCES',
    '/DEPENDENTLOADFLAG:0x800'
)) {
    if (-not $CMakeText.Contains($Token)) { throw "CMake contract is missing: $Token" }
}

$VersionResourceText = Read-SourceText 'resource.rc.in'
foreach ($Token in @(
    '1 VERSIONINFO',
    'FILEVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,@PROJECT_VERSION_TWEAK@',
    'VALUE "CompanyName", "Vo Hoang Tri Dung\0"',
    ('VALUE "FileDescription", "QuickSift ' + [char]0x2014 + ' Fast Photo Culling for Windows\0"'),
    'VALUE "FileVersion", "@QS_FILE_VERSION@\0"',
    ('VALUE "LegalCopyright", "Copyright ' + [char]0x00A9 + ' 2026 Vo Hoang Tri Dung\0"'),
    'VALUE "ProductVersion", "@QS_DISPLAY_VERSION@\0"',
    'VALUE "Comments", "https://quicksift.pages.dev/\0"',
    'IDI_QS ICON "@QS_ICON_RESOURCE_PATH@"'
)) {
    if (-not $VersionResourceText.Contains($Token)) {
        throw "Version-resource template is missing: $Token"
    }
}
if ($VersionResourceText.Contains('VS_VERSION_INFO VERSIONINFO')) {
    throw 'Version-resource template must use numeric resource ID 1, not an undefined VS_VERSION_INFO identifier.'
}

$CanvasRendererText = Read-SourceText 'src\ui\canvas_rendering_and_overlays.cpp'
$D2dFrameworkText = Read-SourceText 'src\ui\framework\macos_ui_framework_d2d.cpp'
foreach ($Token in @(
    'PaintSingleLineText(renderTarget_.Get(), photo.name',
    'matchCardSurface = false',
    'ID2D1Brush* fill = item.matchCardSurface ? nullptr : overlayBrush_.Get()',
    'DWRITE_PARAGRAPH_ALIGNMENT_CENTER',
    'DWRITE_WORD_WRAPPING_NO_WRAP',
    'if (fill) target->FillRoundedRectangle',
    'if (border && borderWidth > 0.0f)'
)) {
    if (-not ($CanvasRendererText.Contains($Token) -or $D2dFrameworkText.Contains($Token))) {
        throw "Canvas label rendering contract is missing: $Token"
    }
}

$BinaryVerifierText = Read-SourceText 'scripts\verify-binary.ps1'
foreach ($Token in @(
    'Get-PeDependentLoadFlags',
    'IMAGE_LOAD_CONFIG_DIRECTORY',
    'DependentLoadFlagsOffset = 78',
    '$PeLoadConfiguration.Flags -band 0x800'
)) {
    if (-not $BinaryVerifierText.Contains($Token)) {
        throw "Binary verifier is missing direct PE dependent-load verification: $Token"
    }
}
if (-not $BinaryVerifierText.Contains('$ExpectedProductVersion')) {
    throw 'Binary verifier must report the defined $ExpectedProductVersion variable.'
}
if ($BinaryVerifierText -match '\$ExpectedVersion(?![A-Za-z0-9_])') {
    throw 'Binary verifier references undefined $ExpectedVersion under PowerShell StrictMode.'
}

$WorkerExecutionText = Read-SourceText 'src\work\background_work_engine.cpp'
$WorkerPolicyText = Read-SourceText 'src\work\background_performance_controller.cpp'
$WorkerHeaderText = Read-SourceText 'src\work\background_work_engine.h'
if ($WorkerHeaderText.Contains('WorkerTelemetrySnapshot Telemetry() const') -and
        -not $WorkerHeaderText.Contains('mutable std::mutex mutex_;')) {
    throw 'Worker::Telemetry() const would instantiate lock_guard<const mutex> under MSVC.'
}
$WorkerText = $WorkerExecutionText + $WorkerPolicyText
if (-not $WorkerText.Contains('BackgroundCompletionQueue') -or
    -not $WorkerPolicyText.Contains('BeginExclusivePathWrites') -or
    -not $WorkerPolicyText.Contains('BlockPathsAndWait') -or
    -not $WorkerExecutionText.Contains('PushWait') -or
    $WorkerText -match 'PostMessageW?\s*\([^;]*reinterpret_cast<LPARAM>') {
    throw 'Background ownership or path-scoped decoder-handoff contract failed.'
}
$PerformancePolicyText = Read-SourceText 'src\core\performance_policy.cpp'
$DecodeBudgetText = Read-SourceText 'src\core\decode_budget.cpp'
$CompletionQueueText = Read-SourceText 'src\work\background_completion_queue.h'
$PerformancePolicyControllerText = Read-SourceText 'src\ui\performance_policy_controller.cpp'
$ResultIntegrationText = Read-SourceText 'src\ui\catalog_loading_and_result_integration.cpp'
$ViewportControllerText = Read-SourceText 'src\ui\viewport_prefetch_controller.cpp'
function Assert-MaxPerformanceContract(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$SourceText
) {
    if ($SourceText.IndexOf($Name, [StringComparison]::Ordinal) -lt 0) {
        throw "Maximum-performance contract is missing: $Name"
    }
}

function Assert-PerformanceContract(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$SourceText
) {
    # Compatibility name retained for the portable verifier. QuickSift no longer
    # adapts CPU/RAM performance at runtime; these assertions validate the fixed
    # maximum-performance image-loading contracts instead.
    Assert-MaxPerformanceContract $Name $SourceText
}

Assert-MaxPerformanceContract 'activeDecodePermits' $PerformancePolicyText
Assert-MaxPerformanceContract 'codecThreadsPerDecode' ($PerformancePolicyText + $WorkerPolicyText)
Assert-MaxPerformanceContract 'reusableBufferLimit' ($PerformancePolicyText + $WorkerPolicyText)
Assert-MaxPerformanceContract 'activeDecodePermits' $PerformancePolicyText
Assert-MaxPerformanceContract 'allowOversizedDecode' $PerformancePolicyText
Assert-MaxPerformanceContract 'OversizedActive' ($DecodeBudgetText + $WorkerExecutionText + $WorkerPolicyText)
Assert-MaxPerformanceContract 'HighestPriorityWaiterLocked' $DecodeBudgetText
Assert-MaxPerformanceContract 'TryPopNext' $CompletionQueueText
Assert-MaxPerformanceContract 'OldestAgeMilliseconds' $CompletionQueueText
Assert-MaxPerformanceContract 'uiCompletionTimeBudget' ($PerformancePolicyControllerText + $ResultIntegrationText)
Assert-MaxPerformanceContract 'PublishThumbnailViewport' $ViewportControllerText
Assert-MaxPerformanceContract 'ApplyThumbnailViewport' ($ViewportControllerText + $WorkerPolicyText)
Assert-MaxPerformanceContract 'IsThumbnailPathCurrent' ($ViewportControllerText + $WorkerPolicyText + $WorkerExecutionText)
Assert-PerformanceContract 'IsThumbnailPathCurrent' ($ViewportControllerText + $WorkerPolicyText + $WorkerExecutionText)
Assert-MaxPerformanceContract 'precedingBatch' $CompletionQueueText
Assert-MaxPerformanceContract 'ShouldServiceIdleAfterForegroundBurst' $WorkerExecutionText


# Native-compile contracts that are not exercised by the portable Linux build.
$RuntimeSupportHeaderText = Read-SourceText 'src\runtime_support.h'
if (-not $RuntimeSupportHeaderText.Contains('#include <unordered_map>')) {
    throw 'Native runtime support uses std::unordered_map without its direct standard-library include.'
}
$ApplicationSupportHeaderText = Read-SourceText 'src\app\application_support.h'
$ApplicationSupportText = Read-SourceText 'src\app\application_support.cpp'
if (-not $ApplicationSupportHeaderText.Contains('extern std::atomic<unsigned> gRetainedWicDecoderSessions') -or
    -not $ApplicationSupportText.Contains('std::atomic<unsigned> gRetainedWicDecoderSessions')) {
    throw 'Retained WIC decoder-session policy is not shared across worker translation units.'
}
$MetadataUiText = Read-SourceText 'src\ui\metadata_history_and_file_safety.cpp'
if ($MetadataUiText -match '(?m)(?<![A-Z0-9_])ID_UNMARK(?![A-Z0-9_])' -or
    $MetadataUiText -match '(?m)(?<![A-Z0-9_])ID_PICK(?![A-Z0-9_])' -or
    $MetadataUiText -match '(?m)(?<![A-Z0-9_])ID_REJECT(?![A-Z0-9_])' -or
    -not $MetadataUiText.Contains('ID_PICK_STATE_MENU')) {
    throw 'Metadata UI repaint contract references removed standalone Pick/Reject/Unmark controls.'
}
if ($ViewportControllerText.Contains('const uint64_t viewportEpoch = PublishThumbnailViewport(0, {});')) {
    throw 'Predictive prefetch retains an unused thumbnail viewport epoch and fails warnings-as-errors on MSVC.'
}
$ReleaseSettingsText = Read-SourceText 'src\ui\settings_filters_and_file_commands.cpp'
$SessionControllerText = Read-SourceText 'src\ui\navigation_input_and_session.cpp'
$ChromeText = Read-SourceText 'src\ui\main_window_chrome_and_messages.cpp'
$BundledCodecsText = Read-SourceText 'src\bundled_codecs.cpp'
$BundledCodecsHeaderText = Read-SourceText 'src\bundled_codecs.h'
$ReleaseWorkerHeaderText = Read-SourceText 'src\work\background_work_engine.h'
$ReleaseWorkerText = (Read-SourceText 'src\work\background_work_engine.cpp') +
    (Read-SourceText 'src\work\background_decode_policy.cpp')
$GraphicsCacheText = Read-SourceText 'src\ui\graphics_resources_and_image_cache.cpp'
$CanvasRenderingText = Read-SourceText 'src\ui\canvas_rendering_and_overlays.cpp'
$ReleaseCatalogIntegrationText = Read-SourceText 'src\ui\catalog_loading_and_result_integration.cpp'
$ReleaseAppInternalText = Read-SourceText 'src\app\quicksift_application_internal.h'
foreach ($Token in @('Load only JPG previews for RAW', 'ID_SETTING_RAW_JPEG_PREVIEWS_ONLY',
        'SetRawJpegPreviewOnly', 'LoadOnlyRawJpegPreviews')) {
    if (-not (($ReleaseSettingsText + $SessionControllerText + $ReleaseWorkerHeaderText).Contains($Token))) {
        throw "RAW JPG-preview-only setting contract is missing: $Token"
    }
}
foreach ($Token in @('rawJpegPreviewOnly', 'thumbnail->type != LIBRAW_IMAGE_JPEG',
        'options.rawJpegPreviewOnly || Cancelled(options)')) {
    if (-not (($BundledCodecsHeaderText + $BundledCodecsText).Contains($Token))) {
        throw "RAW JPG-preview-only codec guard is missing: $Token"
    }
}
foreach ($Token in @('rawJpegPreviewOnly_', 'raw-jpeg-only', 'aRawJpegOnly == bRawJpegOnly',
        'job.rawJpegPreviewOnly != rawJpegPreviewOnly_.load', 'desiredVisualTargets_.clear()')) {
    if (-not (($ReleaseWorkerHeaderText + $ReleaseWorkerText).Contains($Token))) {
        throw "RAW decode-policy queue/coalescing guard is missing: $Token"
    }
}
if (-not (($ReleaseWorkerText.Contains('persistentCache_ && !rawJpegOnly')) -or
        ($ReleaseWorkerText.Contains('persistentCache_') -and $ReleaseWorkerText.Contains('sourceJob.rawJpegPreviewOnly')))) {
    throw 'RAW JPG-preview-only mode can consume or persist ambiguous bitmap/tile cache pixels.'
}
if (-not $CanvasRenderingText.Contains('if (loadOnlyRawJpegPreviews_ && IsRawExtension(photo.extension)) return;')) {
    throw 'RAW JPG-preview-only mode can still request deep-zoom RAW tiles.'
}
if (-not $ReleaseCatalogIntegrationText.Contains('result->kind == JobKind::DecodeTile || !result->previewOnly')) {
    throw 'Late full-RAW pixels are not rejected after the JPG-preview-only toggle changes.'
}
if (-not $GraphicsCacheText.Contains('if (rawJpegOnly) requestFull = false;')) {
    throw 'RAW JPG-preview-only display requests can still promote to full RAW decode.'
}
if (-not $ReleaseAppInternalText.Contains('bool loadOnlyRawJpegPreviews_ = false;')) {
    throw 'RAW JPG-preview-only setting must default to off.'
}

$FileTransactionHeaderText = Read-SourceText 'src\transactions\file_transaction.h'
$FileTransactionExecutionText = Read-SourceText 'src\transactions\file_transaction.cpp'
$FileTransactionServiceHeaderText = Read-SourceText 'src\transactions\file_transaction_service.h'
$FileTransactionServiceText = Read-SourceText 'src\transactions\file_transaction_service.cpp'
$CoordinatorHeaderText = Read-SourceText 'src\transactions\transaction_coordinator.h'
$PopupStatusText = Read-SourceText 'src\ui\popup_menus_commands_and_folder_tree.cpp'
$NavigationTimerText = Read-SourceText 'src\ui\navigation_input_and_session.cpp'
$ApplicationSupportHeaderText = Read-SourceText 'src\app\application_support.h'
foreach ($Token in @('FileTransactionProgress', 'completedFiles', 'totalFiles', 'FileTransactionProgressSink')) {
    if (-not $FileTransactionHeaderText.Contains($Token)) { throw "File-operation progress model is missing: $Token" }
}
foreach ($Token in @('completedFiles += group.owners.size()', 'progressSink(completedFiles, totalFiles)')) {
    if (-not $FileTransactionExecutionText.Contains($Token)) {
        throw "File-operation progress does not count committed media files correctly: $Token"
    }
}
foreach ($Token in @('Progress() const', 'progressStartedAt_', 'progress_->token != token')) {
    if (-not (($FileTransactionServiceHeaderText + $FileTransactionServiceText).Contains($Token))) {
        throw "Race-safe file-operation progress snapshot is missing: $Token"
    }
}
foreach ($Token in @('FileProgress() const', 'FileBusy() const')) {
    if (-not $CoordinatorHeaderText.Contains($Token)) { throw "Transaction coordinator progress contract is missing: $Token" }
}
foreach ($Token in @('FileOperationStatusLine', 'complete files', 'minutes remaining')) {
    if (-not $PopupStatusText.Contains($Token)) { throw "Bottom status-bar file progress is missing: $Token" }
}
if (-not (($ApplicationSupportHeaderText + $NavigationTimerText + $ReleaseSettingsText).Contains('ID_TIMER_FILE_PROGRESS'))) {
    throw 'File-operation status line lacks a bounded UI refresh timer.'
}
foreach ($Token in @('File operations are still running', 'Close anyway', 'L"Wait"',
        'CloseWithPendingOperations', 'closingAfterTransactionCancellation_')) {
    if (-not (($ChromeText + $CanvasRenderingText + $ReleaseAppInternalText).Contains($Token))) {
        throw "Pending-file-operation close confirmation is missing: $Token"
    }
}
if (-not $CanvasRenderingText.Contains('transactions_.RequestCloseAtSafeBoundary()')) {
    throw 'Close anyway does not cancel transactions at a safe boundary.'
}
$HistoryUiText = Read-SourceText 'src\ui\metadata_history_and_file_safety.cpp'
$HistoryReconcileText = $HistoryUiText + $CanvasRenderingText + $ApplicationSupportHeaderText + $ReleaseSettingsText
foreach ($Token in @('RelinquishUndoHistory', 'RelinquishRedoHistory',
        'RelinquishGuardedHistory', 'OfferExternalChangeHistoryReconcile',
        'HistoryEntryHasExternalIdentityDivergence', 'identityDiverged',
        'Relinquish history', 'Keep history')) {
    if (-not $HistoryReconcileText.Contains($Token)) {
        throw "External-change history reconcile pathway is missing: $Token"
    }
}
$MetadataTxHeaderText = Read-SourceText 'src\transactions\metadata_transaction_service.h'
$MetadataTxExecText = Read-SourceText 'src\transactions\metadata_transaction_execution.cpp'
$MetadataSidecarText = Read-SourceText 'src\app\application_support.cpp'
$MetadataEmbeddedText = Read-SourceText 'src\embedded_metadata.cpp'
$MetadataReconcileText = $HistoryReconcileText + $MetadataTxHeaderText + $MetadataTxExecText +
    $MetadataSidecarText + $MetadataEmbeddedText
foreach ($Token in @('RefreshMetadataFromDisk', 'OfferExternalChangeMetadataReconcile',
        'RefreshCatalogFromDiskAfterExternalMetadataConflict',
        'externalConflict', 'conflictCount', 'Refresh from disk',
        'Keep catalog', 'changed outside QuickSift')) {
    if (-not $MetadataReconcileText.Contains($Token)) {
        throw "External-change metadata reconcile pathway is missing: $Token"
    }
}

$ScannerText = Read-SourceText 'src\work\folder_scanner.cpp'
if (-not $ScannerText.Contains('BackgroundCompletionQueue') -or
    -not $ScannerText.Contains('WM_APP_BACKGROUND_COMPLETION') -or
    -not $ScannerText.Contains('PushWait') -or
    -not $ScannerText.Contains('PushTerminal') -or
    $ScannerText -match 'PostMessageW?\s*\([^;]*reinterpret_cast<LPARAM>') {
    throw 'Folder scanner completion ownership contract failed.'
}
foreach ($Token in @('ScanCompletionStatus::Failed', 'ScanCompletionStatus::Partial',
        'GetLastError()', 'ERROR_NO_MORE_FILES')) {
    if (-not $ScannerText.Contains($Token)) { throw "Folder scanner error reporting is missing $Token" }
}

$SettingsText = Read-SourceText 'src\ui\settings_filters_and_file_commands.cpp'
if (-not $SettingsText.Contains('transactions_.SubmitFile') -or
    -not $SettingsText.Contains('DrainFileTransactionResults')) {
    throw 'File commands do not use the serialized transaction service.'
}
$FileOperationStart = $SettingsText.IndexOf('void QuickSiftApplicationImpl::FileOperation')
$DrainStart = $SettingsText.IndexOf('void QuickSiftApplicationImpl::DrainFileTransactionResults')
if ($FileOperationStart -lt 0 -or $DrainStart -le $FileOperationStart) { throw 'FileOperation boundary is missing.' }
$FileOperationText = $SettingsText.Substring($FileOperationStart, $DrainStart - $FileOperationStart)
if ($FileOperationText -match '\b(CopyPath|MovePath|RemoveIfFingerprintMatches)\s*\(') {
    throw 'FileOperation performs transfer primitives on the UI thread.'
}

$TransactionText = (Read-SourceText 'src\transactions\file_transaction.cpp') +
    (Read-SourceText 'src\transactions\file_history_replay.cpp') +
    (Read-SourceText 'src\transactions\file_history_group_replay.cpp')
foreach ($Token in @('cancellationRequested', 'RemoveIfUnchanged', 'MoveIfUnchanged',
        'CopyIfUnchanged', 'replayCompleted', 'historyItems', 'owners', 'companion',
        'groupId', 'CompensateUndo', 'CompensateRedo')) {
    if (-not $TransactionText.Contains($Token)) { throw "File transaction executor is missing $Token" }
}
$WindowsOpsText = (Read-SourceText 'src\transactions\verified_file_operations_windows.cpp') +
    (Read-SourceText 'src\transactions\verified_file_move_windows.cpp')
foreach ($Token in @('CopyFileExW', 'FilesAreByteIdentical', 'FlushFileBuffers',
        'LockedRenameRequest', 'OpenHandlesAreByteIdenticalCancellable', 'ERROR_NOT_SAME_DEVICE', 'FileDispositionInfo',
        'changeStamp', 'contentFingerprint')) {
    if (-not $WindowsOpsText.Contains($Token)) { throw "Windows transfer safety is missing $Token" }
}
if (-not $WindowsOpsText.Contains('FilesAreByteIdenticalCancellable(source, destination)')) {
    throw 'Verified copy does not re-compare the final published destination.'
}
if ($WindowsOpsText.Contains('MOVEFILE_REPLACE_EXISTING')) { throw 'Verified publication allows overwrite.' }

$MetadataText = Read-SourceText 'src\ui\metadata_history_and_file_safety.cpp'
foreach ($Token in @('FileTransactionMode::UndoHistory', 'FileTransactionMode::RedoHistory',
        'transactions_.SubmitFile', 'transactions_.SubmitMetadata',
        'MetadataTransactionMode::UndoHistory', 'MetadataTransactionMode::RedoHistory')) {
    if (-not $MetadataText.Contains($Token)) { throw "Asynchronous history/metadata orchestration is missing $Token" }
}
if ($MetadataText.Contains('ApplyMetadataSnapshot')) {
    throw 'Metadata history replay performs synchronous UI-thread writes.'
}
$MetadataServiceText = Read-SourceText 'src\transactions\metadata_transaction_service.cpp'
$MetadataExecutionText = Read-SourceText 'src\transactions\metadata_transaction_execution.cpp'
$MetadataTransactionText = $MetadataServiceText + $MetadataExecutionText
foreach ($Token in @('beginExclusive', 'endExclusive', 'cancellationRequested_',
        'completed', 'failureCount')) {
    if (-not $MetadataTransactionText.Contains($Token)) { throw "Metadata transaction service is missing $Token" }
}
if (-not $MetadataExecutionText.Contains('~GateGuard() noexcept') -or
    -not $MetadataExecutionText.Contains('catch (...)')) {
    throw 'Metadata decoder-gate cleanup is not exception-contained.'
}
if (-not $MetadataExecutionText.Contains('affected file was not marked complete')) {
    throw 'Metadata per-item exceptions do not preserve truthful partial completion.'
}
$MetadataRunStart = $MetadataServiceText.IndexOf('void MetadataTransactionService::Run() noexcept')
if ($MetadataRunStart -lt 0) { throw 'Metadata worker loop is missing.' }
$MetadataRunText = $MetadataServiceText.Substring($MetadataRunStart)
if ($MetadataRunText.Contains('completed.assign')) {
    throw 'Metadata emergency recovery can erase truthful partial completion.'
}
if (-not $MetadataText.Contains('SelectParallelHistoryItems')) {
    throw 'Metadata history partition does not conservatively retain missing completion flags.'
}

$ChromeText = Read-SourceText 'src\ui\main_window_chrome_and_messages.cpp'
foreach ($Token in @('WM_APP_FILE_TRANSACTION_COMPLETION', 'WM_APP_METADATA_TRANSACTION_COMPLETION',
        'transactions_.CancelCloseRequest', 'transactions_.CancelAll', 'transactions_.Stop',
        'WM_QUERYENDSESSION', 'WM_ENDSESSION')) {
    if (-not $ChromeText.Contains($Token)) { throw "Shutdown/completion contract is missing $Token" }
}
$CompletionQueueText = Read-SourceText 'src\work\background_completion_queue.h'
$CatalogIntegrationText = Read-SourceText 'src\ui\catalog_loading_and_result_integration.cpp'
if (-not $CompletionQueueText.Contains('DrainInto(std::deque<BackgroundCompletion>& destination) noexcept') -or
    $CompletionQueueText.Contains('pending.swap(queue_)') -or
    $CompletionQueueText.Contains('result.reserve(pending.size())')) {
    throw 'Completion draining can lose committed results on allocation failure.'
}
foreach ($Token in @('ScanCompletionStatus::Failed', 'ScanCompletionStatus::Succeeded',
        'AcknowledgeBatch', 'prospectiveMetadataQueue', 'PumpMetadataQueue')) {
    if (-not $CatalogIntegrationText.Contains($Token)) { throw "Catalog scan integration is missing $Token" }
}

$FrameworkHeaderText = Read-SourceText 'src\ui\framework\macos_ui_framework.h'
$FrameworkModelsText = Read-SourceText 'src\ui\framework\ui_models.h'
$FrameworkControlsText = Read-SourceText 'src\ui\framework\macos_ui_framework.cpp'
$FrameworkGdiText = Read-SourceText 'src\ui\framework\macos_ui_framework_gdi.cpp'
$FrameworkPopupText = Read-SourceText 'src\ui\framework\macos_ui_framework_popup.cpp'
$FrameworkD2dText = Read-SourceText 'src\ui\framework\macos_ui_framework_d2d.cpp'
foreach ($Token in @('CreateButton', 'CreateTooltip', 'PaintButton', 'PaintTitleBar',
        'PaintToast', 'PaintAlert', 'TrackDropdown', 'PaintMediaCard',
        'PopupMenuModel', 'ToastPresenter', 'AlertPresenter')) {
    if (-not ($FrameworkHeaderText + $FrameworkModelsText).Contains($Token)) {
        throw "Central UI framework contract is missing $Token"
    }
}
if (-not $ApplicationInternalHeaderText.Contains('MacOsUiFramework uiFramework_')) {
    throw 'The application does not own one central UI framework instance.'
}
if (-not $FrameworkControlsText.Contains('ButtonSubclassProc') -or
    -not $FrameworkGdiText.Contains('PaintTitleBar') -or
    -not $FrameworkPopupText.Contains('TrackDropdown') -or
    -not $FrameworkD2dText.Contains('PaintToast')) {
    throw 'Central UI framework implementation is incomplete.'
}
$ControlsUiText = Read-SourceText 'src\ui\main_window_controls_layout_and_painting.cpp'
$CanvasUiText = Read-SourceText 'src\ui\canvas_rendering_and_overlays.cpp'
$DocumentUiText = (Read-SourceText 'src\ui\app_theme_documents_and_localization.cpp') +
    (Read-SourceText 'src\ui\document_and_menu_models.cpp')
$PopupUiText = Read-SourceText 'src\ui\popup_menus_commands_and_folder_tree.cpp'
$SettingsUiText = Read-SourceText 'src\ui\settings_filters_and_file_commands.cpp'
foreach ($Token in @('uiFramework_.CreateButton', 'uiFramework_.PaintButton',
        'uiFramework_.PaintPanel')) {
    if (-not $ControlsUiText.Contains($Token)) { throw "Main controls bypass the UI framework: $Token" }
}
foreach ($Token in @('uiFramework_.PaintToast', 'uiFramework_.PaintAlert',
        'uiFramework_.PaintMediaCard', 'uiFramework_.PaintOverlayLabel')) {
    if (-not $CanvasUiText.Contains($Token)) { throw "Canvas components bypass the UI framework: $Token" }
}
foreach ($Token in @('uiFramework_.CreateButton', 'uiFramework_.CreateTooltip',
        'state->framework->PaintButton', 'state->framework->PaintPanel')) {
    if (-not $DocumentUiText.Contains($Token)) { throw "Document UI bypasses the UI framework: $Token" }
}
if (-not $PopupUiText.Contains('uiFramework_.TrackDropdown')) {
    throw 'Dropdown menus do not route through the central UI framework.'
}
if (-not $FrameworkPopupText.Contains('GetMessageW(&message, menu, 0, 0)') -or
    $FrameworkPopupText.Contains('GetMessageW(&message, nullptr')) {
    throw 'Dropdown popup can re-enter unrelated owner-window message handlers.'
}
if (-not $SettingsUiText.Contains('TrackDropdownMenu') -or
    -not $SettingsUiText.Contains('std::vector<MenuItem>')) {
    throw 'Feature menus do not declare central framework MenuItem models.'
}
$FeatureUiFiles = Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src\ui') -File -Filter '*.cpp' |
    Where-Object { $_.Name -ne 'ui_design_system.cpp' }
$ComponentPaintPattern = '(?:Fill|Draw)RoundedRect(?:angle|Gp)?\s*\(|\bDrawTextW\s*\(|\b(?:FillRect|FrameRect|RoundRect|TextOutW|MoveToEx|LineTo|Polygon|Ellipse)\s*\('
$DirectCreation = [ordered]@{}
foreach ($File in $FeatureUiFiles) {
    $Text = [regex]::Replace([IO.File]::ReadAllText($File.FullName), '(?s)/\*.*?\*/|//[^\r\n]*', '')
    if ($Text -match $ComponentPaintPattern) {
        throw "Feature module bypasses the central component renderer: $($File.Name)"
    }
    $Count = [regex]::Matches($Text, '\bCreateWindowExW?\s*\(').Count
    if ($Count -gt 0) { $DirectCreation[$File.Name] = $Count }
}
$ExpectedCreation = [ordered]@{
    'app_theme_documents_and_localization.cpp' = 1
    'main_window_chrome_and_messages.cpp' = 1
}
if (($DirectCreation.Keys -join '|') -ne ($ExpectedCreation.Keys -join '|')) {
    throw "Unexpected direct window-creation owners: $($DirectCreation.Keys -join ', ')"
}
foreach ($Name in $ExpectedCreation.Keys) {
    if ($DirectCreation[$Name] -ne $ExpectedCreation[$Name]) {
        throw "Unexpected direct CreateWindowEx count in $Name"
    }
}
foreach ($Legacy in @('GlassButtonSubclassProc', 'kGlassButtonHotProperty',
        'PopupMenuRuntime', 'PopupMenuWndProc', 'DrawGlassMenuItem',
        'MenuVisualItem', 'TrackGlassMenu')) {
    if (($ApplicationInternalHeaderText + $ControlsUiText + $ChromeText +
            $PopupUiText + $SettingsUiText).Contains($Legacy)) {
        throw "Legacy hand-painted UI path remains: $Legacy"
    }
}

$CatalogHeaderText = Read-SourceText 'src\core\catalog_store.h'
$HistoryHeaderText = Read-SourceText 'src\core\history_store.h'
$SelectionHeaderText = Read-SourceText 'src\core\selection_store.h'
foreach ($ForbiddenApi in @('VisibleIndices()', 'VisiblePositions()', 'UndoEntries()', 'RedoEntries()', 'MutablePhotoAt')) {
    if (($CatalogHeaderText + $HistoryHeaderText).Contains($ForbiddenApi)) {
        throw "Invariant-owning store exposes mutable backing storage: $ForbiddenApi"
    }
}
foreach ($LegacyState in @('allPathIndex_', 'allPhotos_', 'visiblePhotoIndices_',
        'visiblePositionByCatalogIndex_', 'undoHistory_', 'redoHistory_', 'selected_', 'anchorIndex_')) {
    if ($ApplicationInternalHeaderText.Contains($LegacyState)) {
        throw "Application still owns legacy store state: $LegacyState"
    }
}
foreach ($Token in @('SetVisibleOrder', 'ReplacePath', 'AppendBatch', 'CompleteReplay',
        'EstimateEntryBytes', 'RecordNew', 'Retain', 'SetAnchor')) {
    if (-not ($CatalogHeaderText + $HistoryHeaderText + $SelectionHeaderText).Contains($Token)) {
        throw "Store invariant API is missing: $Token"
    }
}

$FileServiceHeaderText = Read-SourceText 'src\transactions\file_transaction_service.h'
$FileServiceText = Read-SourceText 'src\transactions\file_transaction_service.cpp'
if (-not $FileServiceHeaderText.Contains('std::unique_ptr<VerifiedFileOperations>') -or
    -not $FileServiceText.Contains('thread_ = std::thread') -or
    $FileServiceText.Contains(': thread_(') -or
    -not $FileServiceText.Contains('SetCancellationSource(&cancellationRequested_)')) {
    throw 'File transaction dependency/lifecycle/cancellation contract failed.'
}
$MetadataServiceHeaderText = Read-SourceText 'src\transactions\metadata_transaction_service.h'
if (-not $MetadataServiceText.Contains('thread_ = std::thread') -or
    $MetadataServiceText.Contains(': thread_(')) {
    throw 'Metadata transaction worker can start before member construction.'
}
if (-not $FileServiceHeaderText.Contains('shared_ptr<const std::function<void()>>') -or
    -not $FileServiceText.Contains('(*sink)()') -or
    -not $MetadataServiceHeaderText.Contains('shared_ptr<const std::function<void()>>') -or
    -not $MetadataServiceHeaderText.Contains('shared_ptr<const MetadataTransactionCallbacks>') -or
    -not $MetadataServiceText.Contains('(*sink)()')) {
    throw 'Transaction callback ownership can allocate or terminate on the worker thread.'
}
$CoordinatorText = Read-SourceText 'src\transactions\transaction_coordinator.cpp'
foreach ($Token in @('SubmitFile', 'SubmitMetadata', 'CancelCloseRequest', 'CancelAll', 'Stop')) {
    if (-not $CoordinatorText.Contains($Token)) { throw "Transaction coordinator contract is missing $Token" }
}

$EmbeddedMetadataText = Read-SourceText 'src\embedded_metadata.cpp'
$TextIoText = Read-SourceText 'src\app\application_text_io.cpp'
foreach ($Token in @('PixelEssenceHash', 'LockedRenameRequest', 'MarkLockedFileForDeletion')) {
    if (-not $EmbeddedMetadataText.Contains($Token)) { throw "Embedded metadata hardening is missing $Token" }
}
foreach ($Token in @('LockedRenameRequest', 'FileHandleMatchesBytes', 'publishTemporary.Apply', 'MarkLockedFileForDeletion')) {
    if (-not $TextIoText.Contains($Token)) { throw "Text/sidecar publication hardening is missing $Token" }
}

$ForbiddenFiles = Get-ChildItem -LiteralPath $SourceDir -Recurse -File | Where-Object {
    $_.Name -match '\.(orig|rej|user|suo)$' -or $_.Name -match '\.before-' -or $_.Name.EndsWith('~')
}
if ($ForbiddenFiles) { throw "Backup/editor files remain: $($ForbiddenFiles.FullName -join ', ')" }


$ReviewModelText = (Read-SourceText 'src\review\review_state_model.cpp') +
    (Read-SourceText 'src\review\review_state_model.h')
$ReviewAsyncText = (Read-SourceText 'src\review\review_async_policy.cpp') +
    (Read-SourceText 'src\review\review_async_policy.h')
$ViewTransformText = Read-SourceText 'src\review\view_transform_policy.cpp'
$ViewStateText = Read-SourceText 'src\ui\view_state_controller.cpp'
$NavigationText = Read-SourceText 'src\ui\navigation_input_and_session.cpp'
$AppInternalText = Read-SourceText 'src\app\quicksift_application_internal.h'
$WorkerText = Read-SourceText 'src\work\background_work_engine.cpp'
$FacePolicyText = Read-SourceText 'src\core\face_policy.cpp'
foreach ($Token in @('ZoomViewAt', 'PanView', 'physicalScale',
        'visibleSourceWidth', 'visibleSourceHeight')) {
    if (-not $ViewTransformText.Contains($Token)) {
        throw "Pane-aware view-transform policy is missing: $Token"
    }
}
foreach ($Token in @('IndependentPaneState', 'SetSyncCompareView',
        'DisableFaceLockPreservingViews', 'UpdateThumbnailViewport',
        'IsThumbnailPathDesired', 'ActivePath', 'ReplaceComparePath', 'FreshFitView', 'independentPanes_')) {
    if (-not $ReviewModelText.Contains($Token)) {
        throw "Coordinated review-state contract is missing: $Token"
    }
}
foreach ($Token in @('ClassifyPostedResult', 'NavigationInvalidatesPostedResult',
        'IsStaleThumbnailViewport')) {
    if (-not $ReviewAsyncText.Contains($Token)) {
        throw "Review async-admission contract is missing: $Token"
    }
}
foreach ($Token in @('DisableFaceLockPreservingViews', 'ViewAreaForPath', 'ResolveViewForPath')) {
    if (-not (($ViewStateText + $ReviewModelText).Contains($Token))) {
        throw "View-state/FaceLock adapter contract is missing: $Token"
    }
}
foreach ($Token in @('HitCompareSlot(x, y)', 'RecenterFaceLockForPath',
        'AdvanceReviewNavigationEpoch', 'kFaceSharpnessDecodeSize')) {
    if (-not (($NavigationText + $ViewStateText + $WorkerText).Contains($Token))) {
        throw "Zoom, comparison, or face-analysis regression guard is missing: $Token"
    }
}
foreach ($Forbidden in @('compareHits_', 'independentViews_', 'independentFaceOffsets_',
        'currentPath_', 'focusPath_', 'viewZoom_', 'viewCenterX_', 'viewCenterY_',
        'viewRotation_', 'thumbScroll_', 'thumbMaxScroll_', 'navigationEpoch_',
        'thumbnailViewportEpoch_')) {
    if ($AppInternalText.Contains($Forbidden)) {
        throw "Legacy split review-state authority remains in application object: $Forbidden"
    }
}
if (($ViewportControllerText + $WorkerPolicyText).Contains('RefreshThumbnailPrefetchQueues')) {
    throw 'Worker still generates thumbnail viewport state instead of mirroring the review model.'
}
if ($CatalogIntegrationText.Contains('worker_.IsThumbnailPathCurrent')) {
    throw 'UI result admission consults the worker mirror instead of authoritative review state.'
}
if (-not $WorkerText.Contains('result->faceBlurScanned = sharpness.scanned')) {
    throw 'Small-face sharpness is incorrectly marked complete without analysis.'
}
if (-not $FacePolicyText.Contains('FaceAnchorComesBefore')) {
    throw 'Primary-face selection lacks deterministic tie breaking.'
}

$FaceRetryText = Read-SourceText 'src\core\face_detection_policy.cpp'
$FaceRetryHeaderText = Read-SourceText 'src\core\face_detection_policy.h'
$FaceControllerText = Read-SourceText 'src\ui\face_analysis_controller.cpp'
$FaceDetectorText = Read-SourceText 'src\face_detector.cpp'
$CatalogIntegrationText = Read-SourceText 'src\ui\catalog_loading_and_result_integration.cpp'
$ViewportPrefetchText = Read-SourceText 'src\ui\viewport_prefetch_controller.cpp'
foreach ($Token in @('MaximumTransientFailures', 'CapabilityRefreshDue',
        'AllowExplicitRetry', 'Clock::time_point::max')) {
    if (-not (($FaceRetryText + $FaceRetryHeaderText).Contains($Token))) {
        throw "Bounded face-analysis retry policy is missing: $Token"
    }
}
foreach ($Token in @('RefreshFaceDetectorCapability', 'EnqueueFaceAnalysis',
        'HandleFaceAnalysisOutcome', 'EnableWindow')) {
    if (-not $FaceControllerText.Contains($Token)) {
        throw "Face-detector capability controller is missing: $Token"
    }
}
if (($NavigationText + $ViewportPrefetchText + $CatalogIntegrationText).Contains('worker_.EnqueueFace')) {
    throw 'Face jobs bypass the centralized capability/backoff scheduling gate.'
}
foreach ($Token in @('QueryFaceDetectorCapability', 'FaceAnalysisOutcome::Unavailable',
        'gThreadFaceDetector = nullptr')) {
    if (-not $FaceDetectorText.Contains($Token)) {
        throw "Face-detector adapter failure handling is missing: $Token"
    }
}
if (-not $CatalogIntegrationText.Contains('HandleFaceAnalysisOutcome(*result)')) {
    throw 'Face-analysis failures are not reported back to the retry policy.'
}


$DataPathText = Read-SourceText 'src\platform\application_data_paths.h'
$DiagnosticText = Read-SourceText 'src\diagnostics\diagnostic_log.cpp'
$PersistentCacheText = Read-SourceText 'src\persistent_cache.cpp'
$SessionControllerText = Read-SourceText 'src\ui\navigation_input_and_session.cpp'
$EntryText = Read-SourceText 'src\main.cpp'
foreach ($Token in @('GetCurrentPackageFullName', 'GetCurrentPackageFamilyName', 'L"Packages"', 'L"LocalState"',
        'L"QuickSiftData"', 'root / L".cache"', 'root / L"QuickSift.session.ini"',
        'Never fall back to the executable directory for a packaged process')) {
    if (-not $DataPathText.Contains($Token)) { throw "Portable/MSIX data-root contract is missing: $Token" }
}
foreach ($Token in @('kRetainedSessionLogs = 20', 'ProbeDirectoryWriteAccess', 'PruneSessionLogs',
        'platform::DataRootPath()', 'platform::CacheDirectoryPath()')) {
    if (-not $DiagnosticText.Contains($Token)) { throw "Diagnostic data-root/retention contract is missing: $Token" }
}
if (-not $PersistentCacheText.Contains('platform::CacheDirectoryPath()') -or
        -not $PersistentCacheText.Contains(': baseDirectory_(quicksift::platform::CacheDirectoryPath())')) {
    throw 'Persistent cache must live exclusively below the canonical runtime data root.'
}
foreach ($Forbidden in @('ALTER TABLE image_records', 'TryMigrateLegacyDirectory', 'PreviousQuickSiftUserCacheDirectory')) {
    if ($PersistentCacheText.Contains($Forbidden)) { throw "Obsolete cache/database compatibility remains: $Forbidden" }
}
foreach ($Token in @('kDatabaseSchemaVersion = 1', 'PRAGMA user_version;',
        '"PRAGMA user_version=" + std::to_string(kDatabaseSchemaVersion)', 'error != SQLITE_SCHEMA')) {
    if (-not $PersistentCacheText.Contains($Token)) { throw "Current-only database schema contract is missing: $Token" }
}
if (-not $SessionControllerText.Contains('platform::SessionFilePath()')) {
    throw 'Session persistence must live below the canonical runtime data root.'
}
if (-not $EntryText.Contains('if (!quicksift::diagnostics::Initialize())') -or
        -not $EntryText.Contains('RunningWithPackageIdentity') -or -not $EntryText.Contains('MessageBoxW')) {
    throw 'Startup must enforce writable portable/MSIX runtime data.'
}
$MsixScriptText = Read-SourceText 'scripts\package-msix.ps1'
$MsixManifestText = Read-SourceText 'packaging\msix\AppxManifest.xml.in'
foreach ($Token in @('QS_MSIX_IDENTITY_NAME', 'QS_MSIX_PUBLISHER', 'QS_SOURCE_URL', 'makeappx.exe',
        'QS_VERSION_FILE_STRING', 'QS_VERSION_STRING')) {
    if (-not $MsixScriptText.Contains($Token)) { throw "MSIX packaging contract is missing: $Token" }
}
if ($MsixManifestText -notmatch '<Identity\b[^>]*\bVersion="([^"]+)"') {
    throw 'MSIX manifest does not declare an Identity Version.'
}
if ($Matches[1] -ne $MsixVersion) {
    throw "MSIX manifest version '$($Matches[1])' does not match canonical file version '$MsixVersion' from src\version.h."
}
foreach ($Token in @('EntryPoint="Windows.FullTrustApplication"', 'Name="Windows.Desktop"',
        'PublisherDisplayName>Vo Hoang Tri Dung<', 'runFullTrust', 'QuickSift.exe')) {
    if (-not $MsixManifestText.Contains($Token)) { throw "MSIX manifest contract is missing: $Token" }
}

$PreviousBrandTokens = @(('Quick' + 'Photo'), ('Photo' + 'Shortlisting'))
$OldIdentityHits = Get-ChildItem -LiteralPath $SourceDir -Recurse -File | Where-Object {
    $_.Extension -in @('.cpp','.h','.md','.txt','.ps1','.py','.json','.in','.cmd','.bat','.xml')
} | Select-String -Pattern $PreviousBrandTokens -SimpleMatch -CaseSensitive:$false
if ($OldIdentityHits) { throw "Previous product identity remains in release source: $((@($OldIdentityHits.Path | Select-Object -Unique)) -join ', ')" }
$ExecutablePathOwner = Join-Path $SourceDir 'src\platform\application_data_paths.h'
$UnexpectedExecutablePathUsers = Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src') -Recurse -File -Include '*.cpp','*.h' | Where-Object {
    $_.FullName -ne $ExecutablePathOwner -and
    ([IO.File]::ReadAllText($_.FullName).Contains('ExecutableDirectory(') -or [IO.File]::ReadAllText($_.FullName).Contains('current_path('))
}
if ($UnexpectedExecutablePathUsers) { throw "Executable-directory/current-directory ownership escaped the package path resolver: $($UnexpectedExecutablePathUsers.FullName -join ', ')" }
$SourceCodeText = (Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src') -Recurse -File -Include '*.cpp','*.h' |
    ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
foreach ($Forbidden in @('WriteProcessMemory(', 'CreateRemoteThread(', 'PAGE_EXECUTE_READWRITE', 'VirtualProtect(',
        'ShellExecuteW(', 'CreateProcessW(', 'WinExec(', 'URLDownloadToFile')) {
    if ($SourceCodeText.Contains($Forbidden)) { throw "Packaged-release hygiene forbids unexpected self-modifying/process-launch primitive: $Forbidden" }
}
$ScannerText = Read-SourceText 'src\work\folder_scanner.cpp'
foreach ($Forbidden in @('SourceStampFor(candidate)', 'MetadataStampFor(candidate)', 'last_write_time(candidate)')) {
    if ($ScannerText.Contains($Forbidden)) { throw "Folder discovery performs expensive per-file hydration: $Forbidden" }
}
$CancelStart = $ScannerText.IndexOf('void FolderScanner::Cancel')
$StartStart = $ScannerText.IndexOf('void FolderScanner::Start', $CancelStart)
if ($CancelStart -lt 0 -or $StartStart -lt 0 -or $ScannerText.Substring($CancelStart, $StartStart - $CancelStart).Contains('join()')) {
    throw 'Folder-scan cancellation must not synchronously join the scanner thread.'
}
$ThumbnailRequestText = Read-SourceText 'src\ui\graphics_resources_and_image_cache.cpp'
if (-not $ThumbnailRequestText.Contains('reviewState_.Epochs().thumbnailViewport')) {
    throw 'Renderer-triggered thumbnail fallback can bypass the model-owned viewport epoch.'
}

$ThumbnailLayoutText = Read-SourceText 'src\review\thumbnail_layout.cpp'
foreach ($Token in @('cellHeight', 'HitTestThumbnail', 'ScrollToRevealThumbnail', 'ScrollToCenterThumbnail')) {
    if (-not $ThumbnailLayoutText.Contains($Token)) { throw "Shared thumbnail-layout contract is missing: $Token" }
}

Write-Host 'QuickSift native source verification passed.' -ForegroundColor Green
