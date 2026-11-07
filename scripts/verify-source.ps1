[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Plain-language failure message (scripts\build-failure-help.ps1): every error that
# stops this script prints "Build failed / What to do / Technical detail" and exits 1.
. (Join-Path $PSScriptRoot 'build-failure-help.ps1')
trap {
    if (Get-Command Write-QuickSiftFailure -ErrorAction SilentlyContinue) {
        Write-QuickSiftFailure $_ 'Source' ''
    } else {
        Write-Host "Build failed: $_" -ForegroundColor Red
    }
    exit 1
}
$SourceDir = (Resolve-Path $SourceDir).Path

function Read-SourceText([string]$RelativePath) {
    $Path = Join-Path $SourceDir $RelativePath
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing required source file: $RelativePath"
    }
    return [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8)
}

# The QuickSiftApplicationImpl declaration: the internal header plus the plain state
# structs it inherits (src\app\state\*.h, 0.9.5.0 round 19).
function Get-AppInternalText {
    $Text = Read-SourceText 'src\app\quicksift_application_internal.h'
    foreach ($File in (Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src\app\state') -File -Filter '*.h' | Sort-Object Name)) {
        $Text += "`n" + [IO.File]::ReadAllText($File.FullName, [Text.Encoding]::UTF8)
    }
    return $Text
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
    'scripts\build-failure-help.ps1',
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
    ([regex]::Matches($ApplicationInternalHeaderText, 'class QuickSiftApplicationImpl\s*(?::[^{]*)?\{')).Count -ne 1) {
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
$ReleaseAppInternalText = Get-AppInternalText
foreach ($Token in @('Load only JPG previews for RAW', 'ID_SETTING_RAW_JPEG_PREVIEWS_ONLY',
        'SetRawJpegPreviewOnly', 'LoadOnlyRawJpegPreviews')) {
    # The caption lives in the pane control catalog (the old Settings popup menu was dead code).
    if (-not (($ReleaseSettingsText + $SessionControllerText + $ReleaseWorkerHeaderText +
            (Read-SourceText 'src\ui\ui_control_catalog.h')).Contains($Token))) {
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
        'job.rawJpegPreviewOnly != RawPreviewOnlyFor(job.path)', 'desiredVisualTargets_.clear()')) {
    if (-not (($ReleaseWorkerHeaderText + $ReleaseWorkerText).Contains($Token))) {
        throw "RAW decode-policy queue/coalescing guard is missing: $Token"
    }
}
if (-not (($ReleaseWorkerText.Contains('persistentCache_ && !rawJpegOnly')) -or
        ($ReleaseWorkerText.Contains('persistentCache_') -and $ReleaseWorkerText.Contains('sourceJob.rawJpegPreviewOnly')))) {
    throw 'RAW JPG-preview-only mode can consume or persist ambiguous bitmap/tile cache pixels.'
}
if (-not $CanvasRenderingText.Contains('if (IsRawExtension(photo.extension) && RawPreviewOnlyFor(photo.path.native())) return;')) {
    throw 'RAW JPG-preview-only mode can still request deep-zoom RAW tiles.'
}
if (-not $ReleaseCatalogIntegrationText.Contains('result->kind == JobKind::DecodeTile || !result->previewOnly')) {
    throw 'Late full-RAW pixels are not rejected after the JPG-preview-only toggle changes.'
}
if (-not $GraphicsCacheText.Contains('if (rawJpegOnly) requestFull = false;')) {
    throw 'RAW JPG-preview-only display requests can still promote to full RAW decode.'
}
if (-not $ReleaseAppInternalText.Contains('bool loadOnlyRawJpegPreviews_ = true;')) {
    throw 'RAW JPG-preview-only setting must default to on (0.9.6.0).'
}
# 0.9.6.0: zoom past Fit decodes the one zoomed RAW in full; worker and UI agree.
$DecodePolicyText = Read-SourceText 'src\work\background_decode_policy.cpp'
if (-not ($DecodePolicyText.Contains('fullRawOverrideHash_') -and $GraphicsCacheText.Contains('SetFullRawOverridePath') -and
        $CanvasRenderingText.Contains('UpdateFullRawOverride(reviewState_.SinglePath()'))) {
    throw 'JPG-preview mode no longer decodes the full RAW when Single View zooms past Fit.'
}
$MigrationText = Read-SourceText 'src\core\settings_migration.h'
$SessionIoText = Read-SourceText 'src\ui\navigation_input_and_session.cpp'
if (-not ($MigrationText.Contains('kCurrentSettingsVersion') -and $SessionIoText.Contains('SettingsVersion') -and
        $SessionIoText.Contains('RawPreviewSetting('))) {
    throw 'Settings migration (SettingsVersion / RawPreviewSetting) is not wired into the session store.'
}
# 0.9.6.0: Lightroom/Capture One reject interop goes through one policy in every writer.
foreach ($Rel in @('src\embedded_metadata.cpp', 'src\app\application_support.cpp')) {
    if (-not (Read-SourceText $Rel).Contains('EncodeRatingAndPick(')) {
        throw "$Rel writes rating/pick without xmp_interop::EncodeRatingAndPick"
    }
}
foreach ($Rel in @('src\embedded_metadata.cpp', 'src\app\application_support.cpp', 'src\metadata\jpeg_xmp_locator.cpp')) {
    if (-not (Read-SourceText $Rel).Contains('DecodeRatingAndPick(')) {
        throw "$Rel reads rating/pick without xmp_interop::DecodeRatingAndPick"
    }
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
# 0.9.5.0: close goes through the live blocking-wait dialog (Force quit / Wait).
$BlockingWaitUiText = Read-SourceText 'src\ui\blocking_wait_controller.cpp'
foreach ($Token in @('ShowBlockingWaitIfNeeded(quicksift::core::blocking_wait::Command::Close)', 'Force quit', 'L"Wait"',
        'BlockingWaitPrimary', 'closingAfterTransactionCancellation_', 'force_quit_abandoned')) {
    if (-not (($ChromeText + $CanvasRenderingText + $ReleaseAppInternalText + $BlockingWaitUiText).Contains($Token))) {
        throw "Pending-file-operation close confirmation is missing: $Token"
    }
}
# Force quit (the only close-with-pending-work path) stops transactions at a safe
# boundary; the never-raised CloseWithPendingOperations alert was removed in round 18.
if (-not $BlockingWaitUiText.Contains('transactions_.RequestCloseAtSafeBoundary()')) {
    throw 'Force quit does not cancel transactions at a safe boundary.'
}
if (($ApplicationSupportHeaderText + $CanvasRenderingText).Contains('CloseWithPendingOperations')) {
    throw 'The dead CloseWithPendingOperations alert action must not come back (close goes through the blocking-wait dialog).'
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
        'AcknowledgeBatch', 'QueueAppendTransaction', 'PumpMetadataQueue')) {
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
$AppInternalText = Get-AppInternalText
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
foreach ($Token in @('RankFacesForAnchor', 'MatchFaceSelection', 'HitTestFaceBoxes', 'kFaceClickFillFraction', 'IsNoiseFace')) {
    if (-not ($FacePolicyText + (Read-SourceText 'src\core\face_policy.h')).Contains($Token)) { throw "Multi-face FaceLock policy is missing: $Token" }
}
$FaceQualityText = (Read-SourceText 'src\core\face_quality_policy.cpp') + (Read-SourceText 'src\core\face_quality_policy.h')
foreach ($Token in @('AnalyzeFaceQualityBgra', 'kFaceQualityAnalysisVersion', 'FaceQualityThresholds', 'RequiredFaceQualityDecodeEdge', 'FaceQualityFlagged')) {
    if (-not $FaceQualityText.Contains($Token)) { throw "Per-face quality policy is missing: $Token" }
}
foreach ($Token in @('AnalyzeFaceQualitiesBgra', 'core::RankFacesForAnchor', 'faceSelectionKnown')) {
    if (-not $WorkerText.Contains($Token)) { throw "Worker multi-face analysis is missing: $Token" }
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
foreach ($Token in @('CleanupOldDiagnosticFiles', 'core/log_retention.h', 'ProbeDirectoryWriteAccess',
        'platform::DataRootPath()', 'platform::CacheDirectoryPath()')) {
    if (-not $DiagnosticText.Contains($Token)) { throw "Diagnostic data-root/retention contract is missing: $Token" }
}
if (-not $PersistentCacheText.Contains('platform::CacheDirectoryPath()') -or
        -not $PersistentCacheText.Contains(': baseDirectory_(quicksift::platform::CacheDirectoryPath())')) {
    throw 'Persistent cache must live exclusively below the canonical runtime data root.'
}
foreach ($Forbidden in @('TryMigrateLegacyDirectory', 'PreviousQuickSiftUserCacheDirectory')) {
    if ($PersistentCacheText.Contains($Forbidden)) { throw "Obsolete cache/database compatibility remains: $Forbidden" }
}
# Schema v3 (0.9.4.0): in-place upgrades are v1 -> v2 (face quality) and v2 -> v3 (camera settings).
$MigrateStart = $PersistentCacheText.IndexOf('bool PersistentCache::MigrateSchema(int fromVersion)')
$MigrateEnd = if ($MigrateStart -ge 0) { $PersistentCacheText.IndexOf('bool PersistentCache::InitializeSchema()', $MigrateStart) } else { -1 }
if ($MigrateStart -lt 0 -or $MigrateEnd -lt 0) {
    throw 'Versioned cache schema migration (MigrateSchema before InitializeSchema) is missing.'
}
$OutsideMigration = $PersistentCacheText.Substring(0, $MigrateStart) + $PersistentCacheText.Substring($MigrateEnd)
if ($OutsideMigration.Contains('ALTER TABLE image_records')) {
    throw 'Unversioned cache/database compatibility remains: ALTER TABLE image_records outside MigrateSchema'
}
foreach ($Token in @('kDatabaseSchemaVersion = 3', 'PRAGMA user_version;',
        '"PRAGMA user_version=" + std::to_string(kDatabaseSchemaVersion)', 'error != SQLITE_SCHEMA',
        'fromVersion < 1', 'MigrateSchema(storedVersion)', 'face_quality_version', 'face_selection',
        'core::kFaceQualityAnalysisVersion', 'camera_settings_known', 'LookupCameraSettings',
        'UpdateCameraSettings')) {
    if (-not $PersistentCacheText.Contains($Token)) { throw "Versioned database schema contract is missing: $Token" }
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
# One audited self-launch: isolated Shell preview, never an arbitrary executable.
$ShellService = Read-SourceText 'src\work\shell_preview_service.cpp'
if ([regex]::Matches($ShellService, 'CreateProcessW\(').Count -ne 1) { throw 'Shell preview must have exactly one audited self-launch' }
foreach ($Token in @('GetModuleFileNameW(nullptr', 'CreateProcessW(executable, command.data()',
        'PROC_THREAD_ATTRIBUTE_HANDLE_LIST', 'CREATE_SUSPENDED', 'JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE',
        'AssignProcessToJobObject', 'WaitForSingleObject(child.value, 50)', 'kShellPreviewTimeoutMs')) {
    if (-not $ShellService.Contains($Token)) { throw "Shell preview isolation contract missing: $Token" }
}
$SourceCodeText = $SourceCodeText.Replace($ShellService, $ShellService.Replace('CreateProcessW(', 'AuditedSelfLaunch('))
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

# --- Parity with scripts/verify-source-portable.py (0.9.5.0 rounds 1-11) ---------
function Remove-CppComments([string]$Text) {
    # Line and block comments only; string literals are preserved (sufficient for token rules).
    $NoBlock = [regex]::Replace($Text, '/\*.*?\*/', ' ', 'Singleline')
    return [regex]::Replace($NoBlock, '(?m)//.*$', '')
}
function Get-FunctionBody([string]$Text, [string]$Signature) {
    $Start = $Text.IndexOf($Signature)
    if ($Start -lt 0) { throw "Missing function: $Signature" }
    $Open = $Text.IndexOf('{', $Start)
    if ($Open -lt 0) { throw "Missing function body: $Signature" }
    $Depth = 0
    for ($Index = $Open; $Index -lt $Text.Length; $Index++) {
        $Char = $Text[$Index]
        if ($Char -eq '{') { $Depth++ }
        elseif ($Char -eq '}') { $Depth--; if ($Depth -eq 0) { return $Text.Substring($Open, $Index - $Open + 1) } }
    }
    throw "Unterminated function body: $Signature"
}

# Theme/language are loaded before any window class or window exists (no dark flash).
$RunBody = Get-FunctionBody (Read-SourceText 'src\app\quicksift_application.cpp') 'int QuickSiftApplicationImpl::Run('
$EarlyIndex = $RunBody.IndexOf('LoadEarlyPresentationSettings()')
$RegisterIndex = $RunBody.IndexOf('RegisterClasses()')
$CreateIndex = $RunBody.IndexOf('CreateMainWindow(')
if ($EarlyIndex -lt 0 -or -not ($EarlyIndex -lt $RegisterIndex -and $RegisterIndex -lt $CreateIndex)) {
    throw 'Theme/language must be loaded (LoadEarlyPresentationSettings) before any window class or window is created.'
}

# Central relocalize pass.
$ControlsPaintText = Read-SourceText 'src\ui\main_window_controls_layout_and_painting.cpp'
if (-not (Get-FunctionBody $ControlsPaintText 'bool QuickSiftApplicationImpl::CreateMainWindowButton(').Contains('controlTextKeys_[id]')) {
    throw 'CreateMainWindowButton must register every caption for the relocalize pass.'
}
$ThemeLocalizationText = Read-SourceText 'src\ui\app_theme_documents_and_localization.cpp'
$RelocalizeBody = Get-FunctionBody $ThemeLocalizationText 'void QuickSiftApplicationImpl::RefreshLocalizedText('
foreach ($Token in @('controlTextKeys_', 'LayoutControls(', 'RefreshOpenDocumentWindows()', 'UpdateBlockingWaitDialog()',
        'userCommentLabel_', 'settingsSectionLabels_', 'InvalidateCanvas()')) {
    if (-not $RelocalizeBody.Contains($Token)) { throw "Central relocalize pass does not cover $Token" }
}
$AllSourceFiles = Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src') -Recurse -File -Filter '*.cpp'
$AllSourceText = ($AllSourceFiles | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
$UpdateButtons = [regex]::Matches($AllSourceText, 'void QuickSiftApplicationImpl::(Update\w+Button)\(\)') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
foreach ($Name in $UpdateButtons) {
    if (-not $RelocalizeBody.Contains("$Name()")) { throw "Central relocalize pass does not refresh $Name" }
}

# 0.9.6.0: one shortcut table drives OnKeyDown and the ? cheat sheet.
$OnKeyDownBody = Get-FunctionBody (Read-SourceText 'src\ui\navigation_input_and_session.cpp') 'void QuickSiftApplicationImpl::OnKeyDown('
if (-not $OnKeyDownBody.Contains('DispatchShortcut(') -or [regex]::IsMatch($OnKeyDownBody, "key\s*==\s*'[A-Z0-9]'")) {
    throw 'OnKeyDown must dispatch through core/keyboard_shortcuts.h, not compare key literals.'
}
$SheetText = Read-SourceText 'src\ui\shortcut_sheet_and_tips_overlay.cpp'
if (-not $SheetText.Contains('kShortcuts') -or
        (Get-FunctionBody $SheetText 'void QuickSiftApplicationImpl::DrawShortcutSheetOverlay(').Contains('Create')) {
    throw 'The shortcut sheet must be generated from kShortcuts and must not create resources per frame.'
}

# Shared custom-drawn checkbox style; no native Win32 checkboxes.
foreach ($File in $AllSourceFiles) {
    if ((Remove-CppComments ([IO.File]::ReadAllText($File.FullName))) -match '\bBS_(AUTO)?CHECKBOX\b') {
        throw "$($File.FullName): native Win32 checkbox; use the shared custom-drawn PaintCheckbox style."
    }
}

# Theme switch redraws everything, including the closed-rail indicator.
$ApplyThemeBody = Get-FunctionBody $ThemeLocalizationText 'void QuickSiftApplicationImpl::ApplyTheme('
foreach ($Token in @('flyoutBackdrop_', 'RDW_ALLCHILDREN')) {
    if (-not $ApplyThemeBody.Contains($Token)) { throw "ApplyTheme does not redraw $Token" }
}

# Every dropdown anchor draws the popup chevron (single registry).
$CommandIdsText = Read-SourceText 'src\ui\ui_command_ids.h'
$RegistryMatch = [regex]::Match($CommandIdsText, 'kPopupMenuButtonIds\{(.*?)\};', 'Singleline')
$RegisteredIds = @()
if ($RegistryMatch.Success) { $RegisteredIds = [regex]::Matches($RegistryMatch.Groups[1].Value, 'ID_[A-Z0-9_]+') | ForEach-Object { $_.Value } }
foreach ($File in (Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src\ui') -Recurse -File -Filter '*.cpp')) {
    $Code = Remove-CppComments ([IO.File]::ReadAllText($File.FullName))
    foreach ($Match in [regex]::Matches($Code, 'TrackDropdownMenu\(controls_\[(ID_[A-Z0-9_]+)\]')) {
        if ($RegisteredIds -notcontains $Match.Groups[1].Value) {
            throw "$($Match.Groups[1].Value) opens a dropdown but is missing from kPopupMenuButtonIds (no chevron)."
        }
    }
}

# Filter menus: no UI-thread I/O, incremental FilterValues() index.
$FiltersCode = Remove-CppComments (Read-SourceText 'src\ui\settings_filters_and_file_commands.cpp')
$HydrateMatch = [regex]::Match($FiltersCode, 'HydrateMetadataForFilters\(\)\s*\{(.*?)\n    \}', 'Singleline')
if (-not $HydrateMatch.Success) { throw 'HydrateMetadataForFilters is missing.' }
if ($HydrateMatch.Groups[1].Value -match 'CreateFileW|ReadFile|ReadExifHeaderFacts|LookupCameraSettings|UpdateCameraSettings') {
    throw 'HydrateMetadataForFilters performs I/O on the UI thread; queue it with QueueFactHydration.'
}
if (-not $FiltersCode.Contains('FilterValues().')) { throw 'Filter menus must read the incremental CatalogStore::FilterValues() index.' }
if ($FiltersCode -match '(?s)Show(Iso|Aperture|Focal|Shutter|Camera|Hour|Date)FilterMenu\(\)\s*\{[^}]*?PhotoCount\(\)') {
    throw 'A filter menu scans the whole catalog; use CatalogStore::FilterValues().'
}
foreach ($Token in @('camera_settings_known', 'LookupCameraSettings', 'UpdateCameraSettings')) {
    if (-not $AllSourceText.Contains($Token)) { throw "Persistent cache camera-settings contract is missing: $Token" }
}

# Deterministic check programs (see TESTING.md).
foreach ($Check in @('animation_checks', 'batch_match_checks', 'design_tokens_checks', 'face_interaction_checks',
        'face_policy_checks', 'filter_index_and_queue_checks', 'hydrated_facts_checks', 'jpeg_xmp_checks',
        'localization_checks', 'lock_order_checks', 'metadata_progress_checks', 'metadata_store_checks',
        'overlay_and_frost_checks', 'priority_policy_checks', 'ratings_exchange_checks', 'raw_preview_checks',
        'search_query_checks', 'thumbnail_layout_checks', 'ui_state_checks', 'write_lane_checks',
        'sidecar_write_checks', 'lifecycle_checks', 'view_load_checks', 'filmstrip_checks',
        'xmp_interop_checks', 'keyboard_shortcut_checks')) {
    [void](Read-SourceText "scripts\checks\$Check.cpp")
}
foreach ($Relative in @('src\core\blocking_wait.cpp', 'src\core\blocking_wait.h', 'src\core\presentation_settings.h',
        'src\core\hud_snapshot.h', 'src\core\pane_scroll_policy.h', 'src\core\sidecar_write_policy.h', 'src\core\metadata_gate_policy.h', 'src\core\metadata_lane_policy.h', 'src\core\metadata_progress.h',
        'src\ui\blocking_wait_controller.cpp', 'scripts\check-localization.py')) {
    [void](Read-SourceText $Relative)
    if (-not $CMakeText.Contains($Relative.Replace('\', '/')) -and $Relative.StartsWith('src')) {
        throw "CMake does not list $Relative"
    }
}

# 0.9.5.0 round 16: no paint at unscrolled layout rects; vacated rects repainted;
# overflow tooltip is a self-painted, mouse-transparent framework popup.
$PaneScrollText = Remove-CppComments $ControlsPaintText
# Round 19: LayoutControls' section helpers sit directly above it, starting at
# PlacePaneChild; the pre-scroll window covers them and LayoutControls.
$LayoutStart = $PaneScrollText.IndexOf('void QuickSiftApplicationImpl::PlacePaneChild(')
$LayoutRest = $PaneScrollText.Substring($LayoutStart)
$LayoutCut = $LayoutRest.IndexOf('closeFlyoutLayout();')
$LayoutBodyText = $LayoutRest.Substring(0, $LayoutCut)
if ($LayoutBodyText.Contains('UpdateLanguageSelectorButton(') -or
    [regex]::IsMatch($LayoutBodyText, '\bSetWindowTextW\s*\(\s*(?!filmstripToggle_\b)') -or
    [regex]::IsMatch($LayoutBodyText, '\b(?:UpdateWindow|RDW_UPDATENOW)\b')) {
    throw 'LayoutControls must not repaint pane children at their unscrolled layout rects (no SetWindowTextW/UpdateWindow/RDW_UPDATENOW before ApplyPaneScroll).'
}
$AfterLayout = $LayoutRest.Substring($LayoutCut)
if (-not $AfterLayout.Substring(0, [Math]::Min(400, $AfterLayout.Length)).Contains('ApplyPaneScroll(flyout)')) {
    throw 'LayoutControls must apply the pane scroll right after the section layout.'
}
$RepositionBody = Remove-CppComments (Get-FunctionBody $ControlsPaintText 'void QuickSiftApplicationImpl::RepositionPaneScroll(')
if ([regex]::Matches($RepositionBody, 'InvalidateUnderHiddenPaneChild\(').Count -lt 2) {
    throw 'RepositionPaneScroll must invalidate the vacated rect on every move and hide.'
}
if (-not (Remove-CppComments (Get-FunctionBody $ControlsPaintText 'void QuickSiftApplicationImpl::HideLeftPaneSectionContentExcept(')).Contains('InvalidateUnderHiddenPaneChild(')) {
    throw 'Hiding a pane child must repaint what was under it.'
}
if (-not (Remove-CppComments (Get-FunctionBody $ControlsPaintText 'void QuickSiftApplicationImpl::ApplyPaneScroll(')).Contains('InvalidateUnderHiddenPaneChild(')) {
    throw 'ApplyPaneScroll must repaint the area under children laid out outside the viewport.'
}
if (-not (Remove-CppComments (Get-FunctionBody (Read-SourceText 'src\ui\popup_menus_commands_and_folder_tree.cpp') 'void QuickSiftApplicationImpl::UpdateLanguageSelectorButton(')).Contains('wcscmp')) {
    throw 'UpdateLanguageSelectorButton must only set the caption when it changes.'
}
$GdiText = Read-SourceText 'src\ui\framework\macos_ui_framework_gdi.cpp'
$FrameworkCoreText = Read-SourceText 'src\ui\framework\macos_ui_framework.cpp'
$OverflowCreate = Remove-CppComments (Get-FunctionBody $GdiText 'HWND MacOsUiFramework::CreateOverflowTooltip(')
if ($OverflowCreate.Contains('TOOLTIPS_CLASS') -or -not $OverflowCreate.Contains('WS_EX_TRANSPARENT')) {
    throw "The overflow tooltip must be the framework's own mouse-transparent popup."
}
if (-not (Remove-CppComments $GdiText).Contains('HTTRANSPARENT')) { throw 'The overflow tooltip window must be hit-test transparent.' }
foreach ($TipEvent in @('hover-enter', 'armed', 'timer-fired', 'shown', 'hidden', 'marked-truncated', 'show-skipped')) {
    if (-not ($GdiText.Contains("L`"$TipEvent`"") -or $FrameworkCoreText.Contains("L`"$TipEvent`""))) {
        throw "Overflow tooltip trace event is missing: $TipEvent"
    }
}

# 0.9.5.0 round 18: the first pane paint froze for seconds because the sidebar tab
# label was GDI+ text (Gdiplus::Font from a family name loads GDI+'s whole installed-font
# collection on first use). No GDI+ text anywhere; [Pane] timing stays wired in.
foreach ($File in (Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src') -Recurse -File -Include '*.cpp','*.h')) {
    $Text = Remove-CppComments ([System.IO.File]::ReadAllText($File.FullName))
    if ([regex]::IsMatch($Text, '\bGdiplus::(?:Font|FontFamily|InstalledFontCollection|PrivateFontCollection)\b') -or
        [regex]::IsMatch($Text, '\.\s*(?:DrawString|MeasureString)\s*\(')) {
        throw "GDI+ text is not allowed (first use loads every installed font on the UI thread): $($File.Name)"
    }
}
$SidebarTabBody = Remove-CppComments (Get-FunctionBody $GdiText 'void MacOsUiFramework::PaintSidebarTab(')
if (-not ($SidebarTabBody.Contains('SidebarTabFont(') -and $SidebarTabBody.Contains('DrawTextW('))) {
    throw 'PaintSidebarTab must draw its label with GDI and the cached SidebarTabFont.'
}
foreach ($PaneEvent in @('pane_open', 'pane_reveal', 'pane_first_paint', 'pane_frost_capture', 'pane_slow_paint')) {
    if (-not $ControlsPaintText.Contains("L`"$PaneEvent`"")) { throw "[Pane] timing event is missing: $PaneEvent" }
}
if (-not (Get-AppInternalText).Contains('core/pane_timing.h')) {
    throw 'The [Pane] timing recorder (core/pane_timing.h) must stay wired in.'
}

# 0.9.5.0 round 19: app-owned worker threads (ratings, batch, search, deferred startup)
# carry a shutdown token, wake the UI only through PostFromWorker, and are all joined by
# StopAllWorkersForTeardown before the persistent cache closes (normal close and Force quit).
foreach ($Folder in @('src\app', 'src\ui')) {
    foreach ($File in (Get-ChildItem -LiteralPath (Join-Path $SourceDir $Folder) -Recurse -File -Filter '*.cpp')) {
        $Text = Remove-CppComments ([System.IO.File]::ReadAllText($File.FullName))
        foreach ($Match in [regex]::Matches($Text, 'std::thread\s*\(\s*\[([^\]]*)\]')) {
            $Captures = $Match.Groups[1].Value
            if ([regex]::IsMatch($Captures, '(?<![\w.])this\b') -and -not $Captures.Contains('token = workerLifetime_.Issue()')) {
                throw "Worker thread captures this without a shutdown token: $($File.Name)"
            }
        }
        if ([regex]::IsMatch($Text, 'PostMessageW\s*\([^;]*WM_APP_(?:RATINGS_WORKER_DONE|BATCH_WORKER_DONE|SEARCH_WORKER|STARTUP_READY)\b')) {
            throw "Worker completion must be posted through PostFromWorker: $($File.Name)"
        }
    }
}
$LifetimeAppText = Remove-CppComments (Read-SourceText 'src\app\quicksift_application.cpp')
$StopAllBody = Get-FunctionBody $LifetimeAppText 'void QuickSiftApplicationImpl::StopAllWorkersForTeardown('
foreach ($Token in @('workerLifetime_.Revoke()', 'deferredStartupThread_.join()', 'StopRatingsWorker()', 'StopBatchWorker()', 'StopSearchWorker()')) {
    if (-not $StopAllBody.Contains($Token)) { throw "StopAllWorkersForTeardown is missing: $Token" }
}
if ($StopAllBody.IndexOf('workerLifetime_.Revoke()') -gt $StopAllBody.IndexOf('deferredStartupThread_.join()')) {
    throw 'StopAllWorkersForTeardown must revoke the worker token before joining.'
}
if (-not (Get-FunctionBody $LifetimeAppText 'QuickSiftApplicationImpl::~QuickSiftApplicationImpl(').Contains('StopAllWorkersForTeardown()')) {
    throw 'The application destructor must stop all workers.'
}
if (-not (Get-FunctionBody $LifetimeAppText 'bool QuickSiftApplicationImpl::PostFromWorker(').Contains('RunIfAlive')) {
    throw 'PostFromWorker must post only while the worker token is alive.'
}
$LifetimeChromeText = Remove-CppComments (Read-SourceText 'src\ui\main_window_chrome_and_messages.cpp')
# Round 19: WM_DESTROY delegates to OnMainWindowDestroyed, which joins every app
# worker before StopServicesForShutdown closes the persistent cache.
$DestroyAt = $LifetimeChromeText.IndexOf('case WM_DESTROY: {')
$DestroyCase = if ($DestroyAt -ge 0) { $LifetimeChromeText.Substring($DestroyAt, [Math]::Min(120, $LifetimeChromeText.Length - $DestroyAt)) } else { '' }
$DestroyedBody = Get-FunctionBody $LifetimeChromeText 'void QuickSiftApplicationImpl::OnMainWindowDestroyed('
$ServicesBody = Get-FunctionBody $LifetimeChromeText 'void QuickSiftApplicationImpl::StopServicesForShutdown('
$StopAt = $DestroyedBody.IndexOf('StopAllWorkersForTeardown()')
$ServicesAt = $DestroyedBody.IndexOf('StopServicesForShutdown()')
if ($DestroyAt -lt 0 -or -not $DestroyCase.Contains('OnMainWindowDestroyed()') -or $StopAt -lt 0 -or $ServicesAt -lt 0 -or
    $StopAt -gt $ServicesAt -or -not $ServicesBody.Contains('persistentCache_.CloseForShutdown()') -or
    $DestroyedBody.Contains('persistentCache_.CloseForShutdown()')) {
    throw 'WM_DESTROY (normal close and Force quit) must stop all workers before the cache closes.'
}
$SearchLoopBody = Get-FunctionBody (Remove-CppComments (Read-SourceText 'src\ui\thumbnail_search_controller.cpp')) 'void QuickSiftApplicationImpl::SearchWorkerLoop('
if ([regex]::IsMatch($SearchLoopBody, '\bhwnd_\b')) { throw 'SearchWorkerLoop runs on the worker thread and must not read hwnd_.' }
$DeferredBody = Get-FunctionBody $LifetimeAppText 'void QuickSiftApplicationImpl::StartDeferredStartup('
$DeferredLambda = $DeferredBody.Substring($DeferredBody.IndexOf('std::thread'))
if ([regex]::IsMatch($DeferredLambda.Substring($DeferredLambda.IndexOf(']')), '\bhwnd_\b')) {
    throw 'The deferred startup worker must not read hwnd_.'
}
[void](Read-SourceText 'scripts\checks\lifecycle_checks.cpp')
[void](Read-SourceText 'src\core\worker_lifetime.h')

# 0.9.5.0 round 19: diagnostic files older than 7 days are deleted on a background-priority
# thread: only QuickSift's own names in its own two folders, never the current session log,
# never a reparse point, never recursive, every deletion logged.
$RetentionPolicyText = Read-SourceText 'src\core\log_retention.h'
foreach ($Token in @('kMaxAge{ 24 * 7 }', 'Skip::CurrentSession', 'Skip::ReparsePoint', 'Skip::NotOurs', 'Skip::WrongFolder')) {
    if (-not $RetentionPolicyText.Contains($Token)) { throw "Diagnostic retention policy is missing: $Token" }
}
$RetentionLogText = Remove-CppComments (Read-SourceText 'src\diagnostics\diagnostic_log.cpp')
$CleanupBody = Get-FunctionBody $RetentionLogText 'CleanupSummary CleanupOldDiagnosticFiles('
foreach ($Token in @('lr::Plan(', 'SessionLogPath()', 'DeleteOwnDiagnosticFile(', 'L"old_file_deleted"', 'L"cleanup_complete"', 'cancelled()')) {
    if (-not $CleanupBody.Contains($Token)) { throw "Diagnostic cleanup is missing: $Token" }
}
if ($CleanupBody.Contains('DeleteFileW') -or $CleanupBody.Contains('fs::remove')) {
    throw 'Diagnostic cleanup must delete only through DeleteOwnDiagnosticFile (handle re-check, no link following).'
}
$DeleterBody = Get-FunctionBody $RetentionLogText 'DeleteOutcome DeleteOwnDiagnosticFile('
foreach ($Token in @('FILE_FLAG_OPEN_REPARSE_POINT', 'FILE_ATTRIBUTE_REPARSE_POINT', 'kMaxAge', 'FileDispositionInfo')) {
    if (-not $DeleterBody.Contains($Token)) { throw "DeleteOwnDiagnosticFile is missing: $Token" }
}
if (-not (Get-FunctionBody $RetentionLogText 'bool ListRetentionCandidates(').Contains('FILE_ATTRIBUTE_REPARSE_POINT')) {
    throw 'Diagnostic cleanup must skip a Logs/CrashReports folder that is a reparse point.'
}
foreach ($Forbidden in @('recursive_directory_iterator', 'remove_all', 'SHFileOperation')) {
    if ($RetentionLogText.Contains($Forbidden)) { throw "Diagnostic cleanup must never recurse or bulk-delete: $Forbidden" }
}
$CleanupStartBody = Get-FunctionBody $LifetimeAppText 'void QuickSiftApplicationImpl::StartDiagnosticsCleanup('
foreach ($Token in @('THREAD_MODE_BACKGROUND_BEGIN', 'CleanupOldDiagnosticFiles', 'token.StopRequested()')) {
    if (-not $CleanupStartBody.Contains($Token)) { throw "Diagnostic cleanup must run on a cancellable background-priority thread: $Token" }
}
if (-not $DeferredBody.Contains('StartDiagnosticsCleanup()')) { throw 'Diagnostic cleanup must start after the first frame (StartDeferredStartup).' }
if (-not $StopAllBody.Contains('diagnosticsCleanupThread_.join()')) { throw 'Teardown must join the diagnostic cleanup thread.' }
$HelpLocalizationText = Read-SourceText 'src\localization.cpp'
if (-not ($HelpLocalizationText.Contains('older than 7 days') -and $HelpLocalizationText.Contains('cũ hơn 7 ngày'))) {
    throw 'Help (EN and VI) must describe the 7-day diagnostic cleanup.'
}
if (-not (Read-SourceText 'USER_GUIDE.md').Contains('older than 7 days')) { throw 'USER_GUIDE.md must describe the 7-day diagnostic cleanup.' }
[void](Read-SourceText 'src\core\log_retention.h')

# 0.9.5.0 round 19: the Direct2D image-source cache (wicImageSources_) was never drawn from;
# it stays gone. The Shell fallback owns its WIC factory creation.
foreach ($File in (Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src') -Recurse -File -Include '*.cpp','*.h')) {
    $Text = Remove-CppComments ([System.IO.File]::ReadAllText($File.FullName))
    foreach ($Token in @('wicImageSources_', 'WicImageSourceEntry', 'GetOrCreateWicImageSource', 'DrawWicImageSourceFit',
            'ClearWicImageSources', 'CreateImageSourceFromWic', 'ID2D1ImageSourceFromWic')) {
        if ([regex]::IsMatch($Text, "\b$Token\b")) { throw "The removed Direct2D image-source cache is back ($Token): $($File.Name)" }
    }
}
$ShellFallbackBody = Remove-CppComments (Get-FunctionBody (Read-SourceText 'src\ui\graphics_resources_and_image_cache.cpp') 'ComPtr<ID2D1Bitmap> QuickSiftApplicationImpl::GetOrCreateShellFallback(')
foreach ($Forbidden in @('GetImage(', 'CoCreateInstance(', 'SHCreateItemFromParsingName(')) {
    if ($ShellFallbackBody.Contains($Forbidden)) { throw "Shell fallback paint must stay asynchronous: $Forbidden" }
}
if (-not $ShellFallbackBody.Contains('shellPreviewService_.Request(')) { throw 'Shell fallback must use the isolated service' }
$ShellChild = Read-SourceText 'src\work\shell_preview_service.cpp'
if ($ShellChild.IndexOf('CoCreateInstance(CLSID_WICImagingFactory') -gt $ShellChild.IndexOf('shell->GetImage(')) { throw 'Shell child must create WIC before expensive handler extraction' }
$OptimizationContracts = @{
    'src\ui\graphics_resources_and_image_cache.cpp' = @('CacheKeyView', 'std::array<int, 2>', 'bitmapEvictionScratch_', 'ClearScratch')
    'src\ui\canvas_rendering_and_overlays.cpp' = @('compareLayoutKey_.Matches', 'canvasGradientBrushes_', 'DrainShellFallbackResults')
    'src\ui\navigation_input_and_session.cpp' = @('sessionWriter_.Submit', 'sessionWriter_.Drain')
    'src\ui\main_window_chrome_and_messages.cpp' = @('SaveSession(true)', 'shellPreviewService_.Stop()')
    'src\bundled_codecs.cpp' = @('FindLargestEmbeddedJpeg(bytes, options.isCancelled)')
    'src\persistent_cache.cpp' = @('derivativeSpace_.NeedsRefresh', 'derivativeSpace_.Reserve', 'FlushNativeOutputFile(file)', 'ReplaceFileAtomically(temporary, destination)')
}
foreach ($Path in $OptimizationContracts.Keys) {
    $Text = Read-SourceText $Path
    foreach ($Token in $OptimizationContracts[$Path]) {
        if (-not $Text.Contains($Token)) { throw "Optimization safety contract missing: $Token" }
    }
}
[void](Read-SourceText 'scripts\checks\optimization_checks.cpp')

# 0.9.5.0 round 19: LayoutControls re-lays the title bar only when one of its inputs changed
# (TitleLayoutKey) or it was marked dirty (DPI, fonts, language, theme).
$TitleHeaderText = Read-SourceText 'src\app\quicksift_application_internal.h'
$TitleKeyText = $TitleHeaderText.Substring($TitleHeaderText.IndexOf('struct TitleLayoutKey {'))
$TitleKeyText = $TitleKeyText.Substring(0, $TitleKeyText.IndexOf("`n    };"))
foreach ($Token in @('int width', 'UINT dpi', 'bool fullscreen', 'bool fullscreenTitleVisible', 'bool singleOrCompare', 'AppTheme theme', 'widths', 'operator==')) {
    if (-not $TitleKeyText.Contains($Token)) { throw "TitleLayoutKey is missing an input: $Token" }
}
$TitleLayoutText = Read-SourceText 'src\ui\main_window_controls_layout_and_painting.cpp'
$TitleBody = Remove-CppComments (Get-FunctionBody $TitleLayoutText 'void QuickSiftApplicationImpl::LayoutTitleControls(')
foreach ($Token in @('CurrentTitleLayoutKey(width)', 'titleLayoutApplied_ == key', 'titleLayoutDirty_', 'key.fullscreen', 'key.widths')) {
    if (-not $TitleBody.Contains($Token)) { throw "LayoutTitleControls skip guard is missing: $Token" }
}
if ($TitleBody.Contains('MeasuredTitleButtonWidthsPx()')) { throw 'LayoutTitleControls must use the widths measured into its TitleLayoutKey.' }
if (-not (Get-FunctionBody $TitleLayoutText 'QuickSiftApplicationImpl::CurrentTitleLayoutKey(').Contains('MeasuredTitleButtonWidthsPx()')) {
    throw 'TitleLayoutKey must include the measured caption widths.'
}
$GuardedControlsBody = Remove-CppComments (Get-FunctionBody $TitleLayoutText 'void QuickSiftApplicationImpl::LayoutControls(')
if (-not [regex]::IsMatch($GuardedControlsBody, 'LayoutTitleControls\(width,\s*false\)')) {
    throw 'LayoutControls must call the guarded LayoutTitleControls(width, false).'
}
foreach ($Pair in @(@('src\ui\main_window_controls_layout_and_painting.cpp', 'void QuickSiftApplicationImpl::OnDpiChanged('),
        @('src\ui\app_theme_documents_and_localization.cpp', 'void QuickSiftApplicationImpl::RefreshLocalizedText('),
        @('src\ui\app_theme_documents_and_localization.cpp', 'void QuickSiftApplicationImpl::ApplyTheme('),
        @('src\ui\main_window_chrome_and_messages.cpp', 'void QuickSiftApplicationImpl::RecreateUiFonts('))) {
    if (-not (Get-FunctionBody (Read-SourceText $Pair[0]) $Pair[1]).Contains('MarkTitleLayoutDirty()')) {
        throw "$($Pair[1]) must mark the title layout dirty."
    }
}

# 0.9.5.0 round 19: QuickSiftApplicationImpl's plain state lives in cohesive structs
# (src\app\state\*.h) inherited privately; data only (no methods, no COM objects).
$StateHeaderText = Read-SourceText 'src\app\quicksift_application_internal.h'
$StateStructs = @('ChromeState', 'PaneState', 'ViewerState', 'BrowseState', 'MetadataState', 'RatingsExchangeState', 'BatchState', 'SearchState', 'HudState', 'SessionLogBrowserState')
foreach ($Name in $StateStructs) {
    if (-not $StateHeaderText.Contains("private state::$Name")) { throw "QuickSiftApplicationImpl must inherit state::$Name privately." }
}
$FoundStateStructs = @()
foreach ($File in (Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src\app\state') -File -Filter '*.h')) {
    $Text = Remove-CppComments ([IO.File]::ReadAllText($File.FullName))
    foreach ($Match in [regex]::Matches($Text, '(?m)^struct (\w+State) \{')) { $FoundStateStructs += $Match.Groups[1].Value }
    if ($Text.Contains('ComPtr<')) { throw "COM objects stay in QuickSiftApplicationImpl (release order): $($File.Name)" }
    foreach ($Line in ($Text -split "`n")) {
        if ([regex]::IsMatch($Line, '^\s+(?:static\s+)?[\w:<>,\s*&]+\s+[A-Z]\w*\([^)]*\)\s*(?:const)?\s*(?:noexcept)?\s*;\s*$')) {
            throw "State structs hold data only (method declared): $($File.Name): $($Line.Trim())"
        }
    }
    if (-not $CMakeText.Contains("src/app/state/$($File.Name)")) { throw "State header is not registered in CMake: $($File.Name)" }
}
foreach ($Name in $StateStructs) {
    if ($FoundStateStructs -notcontains $Name) { throw "State struct is missing: $Name" }
}

# 0.9.5.0 round 19: catch (...) in the Windows app layer logs through QS_LOG_AND_CONTINUE
# (noexcept, per-site rate limit) or states why it is silent (QS_CATCH_SILENT_BY_DESIGN).
$LacCore = Remove-CppComments (Read-SourceText 'src\core\log_and_continue.h')
foreach ($Token in @('bool Report(LogRateLimiter& limiter', ') noexcept {', 'DescribeCurrentException() noexcept',
        '#define QS_CATCH_SILENT_BY_DESIGN(reason)')) {
    if (-not $LacCore.Contains($Token)) { throw "Log-and-continue core contract is missing: $Token" }
}
$LacGlue = Remove-CppComments (Read-SourceText 'src\diagnostics\log_and_continue.h')
foreach ($Token in @('inline void LogAndContinue(core::LogRateLimiter& limiter', 'std::wstring_view site) noexcept {',
        '#define QS_LOG_AND_CONTINUE(category, site)', 'static ::quicksift::core::LogRateLimiter', 'kBurstPerSite')) {
    if (-not $LacGlue.Contains($Token)) { throw "QS_LOG_AND_CONTINUE contract is missing: $Token" }
}
$LacCmake = Read-SourceText 'CMakeLists.txt'
foreach ($Header in @('src/core/log_and_continue.h', 'src/diagnostics/log_and_continue.h')) {
    if (-not $LacCmake.Contains($Header)) { throw "Log-and-continue header is not registered in CMake: $Header" }
}
$LacCoreBlock = [regex]::Match($LacCmake, 'add_library\(QuickSiftCore STATIC(.*?)\)', 'Singleline')
$LacCoreFiles = @($LacCoreBlock.Groups[1].Value -split '\s+' | Where-Object { $_ })
$LacSrc = Join-Path $SourceDir 'src'
foreach ($File in Get-ChildItem -LiteralPath $LacSrc -Recurse -Filter '*.cpp' -File) {
    $Relative = 'src/' + $File.FullName.Substring($LacSrc.Length + 1).Replace('\', '/')
    if ($LacCoreFiles -contains $Relative -or $Relative -eq 'src/diagnostics/diagnostic_log.cpp' -or
        $Relative -eq 'src/main.cpp' -or $Relative.StartsWith('src/core/') -or
        $Relative.StartsWith('src/ui/framework/') -or $Relative.StartsWith('src/platform/')) { continue }
    if (-not $LacCmake.Contains($Relative)) { continue }
    $LacRaw = [IO.File]::ReadAllText($File.FullName, [Text.Encoding]::UTF8)
    $LacText = Remove-CppComments $LacRaw
    if ([regex]::IsMatch($LacText, 'catch\s*\(\s*\.\.\.\s*\)\s*\{\s*\}')) {
        throw "Empty catch (...) in ${Relative}; use QS_LOG_AND_CONTINUE or QS_CATCH_SILENT_BY_DESIGN."
    }
    if ($LacText.Contains('QS_LOG_AND_CONTINUE(') -and -not $LacRaw.Contains('#include "diagnostics/log_and_continue.h"')) {
        throw "$Relative uses QS_LOG_AND_CONTINUE without including diagnostics/log_and_continue.h."
    }
}
$LacChecks = Read-SourceText 'scripts\checks\lifecycle_checks.cpp'
if (-not ($LacChecks.Contains('LogAndContinueChecks();') -and $LacChecks.Contains('#include "core/log_and_continue.h"'))) {
    throw 'lifecycle_checks must exercise the log-and-continue helper.'
}

# 0.9.5.0 round 19: layout DIP values come from src\ui\ui_layout_constants.h, never literals.
$LayoutConstants = Remove-CppComments (Read-SourceText 'src\ui\ui_layout_constants.h')
foreach ($Token in @('namespace main_window', 'namespace left_pane', 'namespace status_bar', 'namespace document',
        'kMinTrackWidthDip', 'kInfoCommentHeightDip', 'kSettingsRowHeightDip', 'kCloseLightDiameterDip', 'kButtonWidthDip')) {
    if (-not $LayoutConstants.Contains($Token)) { throw "Layout constants header is missing: $Token" }
}
if (-not (Read-SourceText 'CMakeLists.txt').Contains('src/ui/ui_layout_constants.h')) {
    throw 'src/ui/ui_layout_constants.h is not registered in CMake.'
}
$LayoutLiteral = '\b(?:DipToPx|px|Px)\(\s*(?:\w+\s*\?\s*)?\d|\?\s*\d+\s*:\s*\d+\s*\)|FlyoutButtonWidthPx\([^;]*,\s*\d+\s*\)'
$LayoutConstantFunctions = @(
    @('src\ui\main_window_controls_layout_and_painting.cpp', 'void QuickSiftApplicationImpl::LayoutFoldersSection('),
    @('src\ui\main_window_controls_layout_and_painting.cpp', 'void QuickSiftApplicationImpl::LayoutInfoSection('),
    @('src\ui\main_window_controls_layout_and_painting.cpp', 'void QuickSiftApplicationImpl::LayoutButtonFlowSection('),
    @('src\ui\main_window_controls_layout_and_painting.cpp', 'void QuickSiftApplicationImpl::LayoutSettingsSection('),
    @('src\ui\main_window_controls_layout_and_painting.cpp', 'void QuickSiftApplicationImpl::LayoutStatusBar('),
    @('src\ui\main_window_chrome_and_messages.cpp', 'LRESULT QuickSiftApplicationImpl::OnGetMinMaxInfo('),
    @('src\ui\document_and_menu_models.cpp', 'void PaintDocumentChrome('),
    @('src\ui\document_and_menu_models.cpp', 'RECT DocumentTitleCloseRect('),
    @('src\ui\document_and_menu_models.cpp', 'std::optional<LRESULT> DocumentHitTest('),
    @('src\ui\document_and_menu_models.cpp', 'void LayoutDocumentWindow(')
)
foreach ($Entry in $LayoutConstantFunctions) {
    $LayoutSource = Read-SourceText $Entry[0]
    if (-not $LayoutSource.Contains('#include "ui/ui_layout_constants.h"')) { throw "$($Entry[0]) must include ui/ui_layout_constants.h." }
    $LayoutBody = Get-FunctionBody (Remove-CppComments $LayoutSource) $Entry[1]
    $LayoutMatch = [regex]::Match($LayoutBody, $LayoutLiteral)
    if ($LayoutMatch.Success) {
        throw "Magic layout number in $($Entry[1]) ($($Entry[0])): $($LayoutMatch.Value); use ui_layout_constants.h."
    }
}

# 0.9.5.0 round 20: the diagnostic log window can open previous session logs. Read-only
# (never deletes or writes), only QuickSift's own session-log names in the one Logs folder,
# reparse points skipped, bounded read, worker off the UI thread.
$SessionPolicyText = Read-SourceText 'src\core\session_log_browser.h'
foreach ($Token in @('kMaxDisplayBytes', 'kMaxListed', 'candidate.reparsePoint', 'currentSessionName', 'Kind::SessionLog')) {
    if (-not $SessionPolicyText.Contains($Token)) { throw "Session log browser policy is missing: $Token" }
}
$SessionLogText = Remove-CppComments (Read-SourceText 'src\diagnostics\diagnostic_log.cpp')
if (-not (Get-FunctionBody $SessionLogText 'fs::path SessionLogDirectory(').Contains('root / L"Logs"')) {
    throw 'SessionLogDirectory must be the one Logs folder under DataRootPath.'
}
$SessionReaderBody = Get-FunctionBody $SessionLogText 'SessionLogText ReadSessionLogTail('
foreach ($Token in @('sl::IsBrowsableName(', 'BrowsableLogsFolder()', 'GENERIC_READ', 'FILE_FLAG_OPEN_REPARSE_POINT', 'FILE_ATTRIBUTE_REPARSE_POINT', 'sl::PlanTail(')) {
    if (-not $SessionReaderBody.Contains($Token)) { throw "ReadSessionLogTail is missing: $Token" }
}
if ($SessionReaderBody.Contains('GENERIC_WRITE') -or $SessionReaderBody.Replace('FILE_SHARE_DELETE', '').Contains('DELETE')) {
    throw 'ReadSessionLogTail must open logs read-only.'
}
if (-not (Get-FunctionBody $SessionLogText 'fs::path BrowsableLogsFolder(').Contains('FILE_ATTRIBUTE_REPARSE_POINT')) {
    throw 'The session log browser must not follow a Logs folder that is a reparse point.'
}
$SessionControllerText = Remove-CppComments (Read-SourceText 'src\ui\session_log_browser_controller.cpp')
foreach ($Forbidden in @('DeleteFile', 'fs::remove', 'std::filesystem::remove', 'MoveFile', 'CreateFileW', 'GENERIC_WRITE')) {
    if ($SessionControllerText.Contains($Forbidden)) { throw "The session log browser is read-only and goes through diagnostic_log: $Forbidden" }
}
foreach ($Token in @('token = workerLifetime_.Issue()', 'WM_APP_SESSION_LOG_WORKER_DONE', 'ListPreviousSessionLogs()', 'ReadSessionLogTail(', 'TrackDropdownMenuVerbatim(', 'DocumentKind::SessionLog')) {
    if (-not $SessionControllerText.Contains($Token)) { throw "Session log browser controller is missing: $Token" }
}
if (-not ($CMakeText.Contains('src/ui/session_log_browser_controller.cpp') -and $CMakeText.Contains('src/core/session_log_browser.h'))) {
    throw 'Session log browser files are not registered in CMake.'
}
if (-not $StopAllBody.Contains('StopSessionLogWorker()')) { throw 'Teardown must join the session log worker.' }
$SessionIdsText = Read-SourceText 'src\ui\ui_command_ids.h'
foreach ($Token in @('ID_DOCUMENT_PREVIOUS_SESSIONS', 'ID_DOCUMENT_SESSION_LOG_FIRST', 'ID_DOCUMENT_SESSION_LOG_LAST')) {
    if (-not $SessionIdsText.Contains($Token)) { throw "Session log browser command id is missing from ui_command_ids.h: $Token" }
}
if (-not (Read-SourceText 'scripts\checks\lifecycle_checks.cpp').Contains('SessionLogBrowserChecks()')) {
    throw 'lifecycle_checks must exercise the session log browser policy.'
}

# 0.9.5.0 round 20: a build failure prints a plain-language explanation
# (Build failed / What to do / Technical detail) and keeps a non-zero exit code.
$FailureHelpText = Read-SourceText 'scripts\build-failure-help.ps1'
foreach ($Token in @('function Write-QuickSiftFailure', 'function Add-QuickSiftFailureHint', '"Build failed: "', "'What to do:'", '"Technical detail: "')) {
    if (-not $FailureHelpText.Contains($Token)) { throw "Build failure helper is missing: $Token" }
}
if ($FailureHelpText -match '[^\x00-\x7F]') { throw 'scripts\build-failure-help.ps1 must stay ASCII (Windows PowerShell 5.1 reads it as ANSI).' }
foreach ($ScriptName in @('scripts\build.ps1', 'scripts\verify-source.ps1', 'scripts\verify-binary.ps1', 'scripts\package-msix.ps1')) {
    $ScriptText = Read-SourceText $ScriptName
    foreach ($Token in @(". (Join-Path `$PSScriptRoot 'build-failure-help.ps1')", 'Write-QuickSiftFailure $_', 'exit 1')) {
        if (-not $ScriptText.Contains($Token)) { throw "$ScriptName must explain failures through build-failure-help.ps1: $Token" }
    }
}
if (-not (Read-SourceText 'scripts\build.ps1').Contains('Add-QuickSiftFailureHint $Line')) { throw 'build.ps1 must remember diagnostic lines for the failure explanation.' }
foreach ($Wrapper in @('build.cmd', 'build-release.cmd', 'open-in-visual-studio.cmd', 'package-msix.bat')) {
    $WrapperText = Read-SourceText $Wrapper
    foreach ($Token in @('set "QS_EXIT=%ERRORLEVEL%"', 'exit /b %QS_EXIT%', ':no_powershell', 'exit /b 9009')) {
        if (-not $WrapperText.Contains($Token)) { throw "$Wrapper must keep the exit code and explain a missing PowerShell: $Token" }
    }
}

# 0.9.5.0 round 21 (t29u): Single View load-stall fixes stay in place.
if (-not $CMakeText.Contains('src/core/view_load_policy.h')) { throw 'CMake does not list src/core/view_load_policy.h' }
$ViewEngineText = Read-SourceText 'src\work\background_work_engine.cpp'
if (-not (Get-FunctionBody $ViewEngineText 'bool Worker::DecodeFullJob(').Contains('!IsCancelled(job) && DecodeWithShell(')) {
    throw 'DecodeFullJob must not run the uncancellable Shell fallback after cancellation/timeout.'
}
if (-not (Get-FunctionBody $ViewEngineText 'void Worker::EnqueueViewDecode(').Contains('ViewDecodeInteractionEpoch(')) {
    throw 'View-lane decodes must not be cancelled by the pan/zoom interaction epoch.'
}
if (-not (Get-FunctionBody $ViewEngineText 'bool Worker::Enqueue(').Contains('RunningViewJobIsStaleForLocked(job)')) {
    throw 'A stale running view job must not absorb a new foreground request.'
}
foreach ($Signature in @('void Worker::AbandonJobWithoutBudget(', 'void Worker::AbandonCancelledJob(')) {
    if (-not (Get-FunctionBody $ViewEngineText $Signature).Contains('RefreshReadAheadGate()')) { throw "$Signature must re-evaluate the read-ahead gate." }
}
$ViewCacheText = Read-SourceText 'src\ui\graphics_resources_and_image_cache.cpp'
if (-not (Get-FunctionBody $ViewCacheText 'BitmapEntry* QuickSiftApplicationImpl::GetOrRequestBitmap(').Contains('ShouldRequestFullViewDecode(')) {
    throw 'The RAW full request must stop once the exact bucket holds a full-quality result.'
}
if (-not (Get-FunctionBody $ViewCacheText 'int QuickSiftApplicationImpl::BucketFor(').Contains('MaximumViewDecodeEdge()')) {
    throw "Decode buckets must be capped by the device's maximum bitmap edge."
}
$AdmitBody = Get-FunctionBody (Read-SourceText 'src\ui\catalog_loading_and_result_integration.cpp') 'void QuickSiftApplicationImpl::AdmitDecodedPixels('
if (([regex]::Matches($AdmitBody, 'RejectUndisplayableDecode\(')).Count -lt 2) {
    throw 'Undisplayable decoded pixels must enter the bounded retry policy, not be dropped silently.'
}
$ViewCanvasText = Read-SourceText 'src\ui\canvas_rendering_and_overlays.cpp'
if (-not (Get-FunctionBody $ViewCanvasText 'void QuickSiftApplicationImpl::RenderSingle(').Contains('singleViewStallWatch_.Observe(')) {
    throw 'RenderSingle must feed the Single View stall watchdog.'
}
if (-not (Get-FunctionBody $ViewCanvasText 'void QuickSiftApplicationImpl::LogSingleViewStall(').Contains('LogRateLimiter')) {
    throw 'The Single View stall log must be rate-limited.'
}
$ViewChecksText = Read-SourceText 'scripts\checks\view_load_checks.cpp'
foreach ($Token in @('DecodeBudgetChecks()', 'StallWatchChecks()', 'StaleRunningJobChecks()')) {
    if (-not $ViewChecksText.Contains($Token)) { throw "view_load_checks must run $Token" }
}

# Round 22: Single View filmstrip audit guards.
$FilmTarget = 'filmstrip::ThumbnailTarget(ThumbnailDecodeTarget())'
if (-not (Get-FunctionBody (Read-SourceText 'src\app\quicksift_application.cpp') 'void QuickSiftApplicationImpl::PrefetchFilmstrip(').Contains($FilmTarget)) {
    throw 'PrefetchFilmstrip must request the grid thumbnail target (shared cache).'
}
$FilmRender = Get-FunctionBody $ViewCanvasText 'void QuickSiftApplicationImpl::RenderFilmstrip('
if (-not $FilmRender.Contains($FilmTarget)) { throw 'RenderFilmstrip must look up the grid thumbnail target.' }
if ($FilmRender.Replace('ComPtr<ID2D1SolidColorBrush>& brush', '').Contains('ComPtr<ID2D1SolidColorBrush>')) {
    throw 'RenderFilmstrip must reuse cached brushes, not create them per frame.'
}
if (-not ($FilmRender.Contains('palette.accent') -and $FilmRender.Contains('palette.border'))) {
    throw 'RenderFilmstrip borders must follow the theme palette.'
}
if (-not (Get-FunctionBody $ViewCanvasText 'void QuickSiftApplicationImpl::RenderSingle(').Contains('SingleImageArea(')) {
    throw 'RenderSingle must use the shared (filmstrip-aware) image area.'
}
if (-not (Get-FunctionBody (Read-SourceText 'src\ui\view_state_controller.cpp') 'D2D1_RECT_F QuickSiftApplicationImpl::ViewAreaForPath(').Contains('SingleImageArea(')) {
    throw 'ViewAreaForPath must reserve the open filmstrip band.'
}
$FilmDiscard = Get-FunctionBody $ViewCacheText 'void QuickSiftApplicationImpl::DiscardDeviceResources('
foreach ($Token in @('shellFallbackCache_.clear()', 'filmstripBackgroundBrush_.Reset()', 'filmstripActiveBrush_.Reset()', 'filmstripInactiveBrush_.Reset()')) {
    if (-not $FilmDiscard.Contains($Token)) { throw "DiscardDeviceResources must run $Token" }
}
$FilmNavText = Read-SourceText 'src\ui\navigation_input_and_session.cpp'
if (-not (Get-FunctionBody $FilmNavText 'void QuickSiftApplicationImpl::OnCanvasWheel(').Contains('TakeWheelSteps(')) { throw 'Wheel over the filmstrip must step images.' }
if (-not (Get-FunctionBody $FilmNavText 'void QuickSiftApplicationImpl::OnCanvasDoubleClick(').Contains('FilmstripContains(')) { throw 'A double-click on the filmstrip must not toggle zoom.' }
if (-not $FilmNavText.Contains('L"Filmstrip"')) { throw 'The filmstrip preference must be saved in the session.' }
if (-not (Get-FunctionBody $ViewEngineText 'WorkJob Worker::PopWork(').Contains('ThumbnailDecodeAdmitted(')) {
    throw 'Filmstrip thumbnail decodes must share the one-thumbnail cap during view load.'
}
# Round 23: fade, decode window, badges, dashed blur outline.
$FilmAppText = Read-SourceText 'src\app\quicksift_application.cpp'
if (-not (Get-FunctionBody $FilmAppText 'void QuickSiftApplicationImpl::UpdateAnimations(').Contains('SyncFilmstripMotion(')) { throw 'The animation tick must drive the filmstrip fade through SyncFilmstripMotion.' }
if (-not (Get-FunctionBody $FilmAppText 'void QuickSiftApplicationImpl::ToggleFilmstrip(').Contains('SyncFilmstripMotion(')) { throw 'ToggleFilmstrip must start the fade at once.' }
$FilmSetMode = Get-FunctionBody $FilmNavText 'void QuickSiftApplicationImpl::SetMode('
if (-not ($FilmSetMode.Contains('SyncFilmstripMotion(') -and $FilmSetMode.Contains('PublishFilmstripDecodeWindow('))) { throw 'SetMode must sync the filmstrip fade and drop strip decodes outside Single View.' }
if ((Get-FunctionBody $FilmAppText 'bool QuickSiftApplicationImpl::FilmstripContains(').Contains('filmstripOpacity_')) { throw 'Filmstrip hit-testing must follow the logical state, not the animated opacity.' }
$FilmPrefetch = Get-FunctionBody $FilmAppText 'void QuickSiftApplicationImpl::PrefetchFilmstrip('
$FilmPublishAt = $FilmPrefetch.IndexOf('PublishFilmstripDecodeWindow(')
$FilmRequestAt = $FilmPrefetch.IndexOf('GetOrRequestBitmap(')
if ($FilmPublishAt -lt 0 -or $FilmRequestAt -lt 0 -or $FilmPublishAt -gt $FilmRequestAt) { throw 'PrefetchFilmstrip must publish the decode window before requesting cells.' }
if (-not (Get-FunctionBody $ViewEngineText 'bool Worker::IsCancelled(').Contains('FilmstripDecodeDropped(')) { throw 'In-flight filmstrip decodes outside the window must be cancelled.' }
$FilmPerfText = Read-SourceText 'src\work\background_performance_controller.cpp'
$FilmWindowBody = Get-FunctionBody $FilmPerfText 'void Worker::SetFilmstripDecodeWindow('
if (-not ($FilmWindowBody.Contains('pending_.erase(') -and $FilmWindowBody.Contains('RefreshReadAheadGate()'))) { throw 'Dropped filmstrip decodes must release their pending keys and re-evaluate the read-ahead gate.' }
if (-not (Get-FunctionBody $FilmPerfText 'bool Worker::FilmstripDecodeDropped(').Contains('filmstrip::DecodeDropped(')) { throw 'The filmstrip drop rule must come from core/filmstrip_policy.h.' }
$FilmBadges = Get-FunctionBody $ViewCanvasText 'void QuickSiftApplicationImpl::DrawThumbnailBadges(const PhotoItem& photo, D2D1_RECT_F area,'
if ($FilmBadges.Contains('std::vector') -or $FilmBadges.Contains('Tr(')) { throw 'DrawThumbnailBadges must not allocate per frame.' }
if (-not ($FilmRender.Contains('DrawThumbnailBadges(') -and $FilmRender.Contains('blurOutlineStrokeStyle_'))) { throw 'The filmstrip must reuse the grid badges and the dashed blur outline.' }
if (-not (Get-FunctionBody $ViewCanvasText 'void QuickSiftApplicationImpl::RenderThumbnails(').Contains('blurOutlineStrokeStyle_')) { throw "The grid's blurry-photo outline must be dashed." }
foreach ($Token in @('blurOutlineBrush_.Reset()', 'blurOutlineStrokeStyle_.Reset()')) {
    if (-not $FilmDiscard.Contains($Token)) { throw "DiscardDeviceResources must run $Token" }
}
if (-not (Get-FunctionBody (Read-SourceText 'src\ui\app_theme_documents_and_localization.cpp') 'void QuickSiftApplicationImpl::ApplyTheme(').Contains('blurOutlineBrush_.Reset()')) { throw 'ApplyTheme must recreate the theme-dependent blur outline brush.' }
$FilmChecksText = Read-SourceText 'scripts\checks\filmstrip_checks.cpp'
foreach ($Token in @('CheckWindow()', 'CheckHitTesting()', 'CheckGeometry()', 'CheckWheel()', 'CheckScheduling()', 'CheckFade()', 'CheckDecodeWindow()', 'CheckBadges()', 'CheckBlurOutline()')) {
    if (-not $FilmChecksText.Contains($Token)) { throw "filmstrip_checks must run $Token" }
}

Write-Host 'QuickSift native source verification passed.' -ForegroundColor Green
