#!/usr/bin/env python3
"""Cross-platform architecture and safety contract for QuickSift source."""
from __future__ import annotations

import argparse
import re
from pathlib import Path

OBSOLETE_SUFFIXES = (".inl",)
REQUIRED_MODULES = (
    "src/app/application_support.cpp",
    "src/app/application_text_io.cpp",
    "src/app/quicksift_application.cpp",
    "src/core/catalog_store.cpp",
    "src/core/catalog_store_mutation.cpp",
    "src/core/path_identity.cpp",
    "src/core/performance_policy.cpp",
    "src/core/face_policy.cpp",
    "src/core/face_detection_policy.cpp",
    "src/core/selection_store.cpp",
    "src/core/history_store.cpp",
    "src/core/history_store_memory.cpp",
    "src/review/thumbnail_layout.cpp",
    "src/review/thumbnail_prefetch_policy.cpp",
    "src/review/review_state_model.cpp",
    "src/review/review_async_policy.cpp",
    "src/review/view_transform_policy.cpp",
    "src/core/metadata_state.cpp",
    "src/platform/storage_space.cpp",
    "src/work/background_work_engine.cpp",
    "src/work/background_decode_policy.cpp",
    "src/work/background_performance_controller.cpp",
    "src/work/folder_scanner.cpp",
    "src/ui/ui_design_system.cpp",
    "src/ui/framework/ui_models.cpp",
    "src/ui/framework/macos_ui_framework.cpp",
    "src/ui/framework/macos_ui_framework_gdi.cpp",
    "src/ui/framework/macos_ui_framework_popup.cpp",
    "src/ui/framework/macos_ui_framework_d2d.cpp",
    "src/ui/document_and_menu_models.cpp",
    "src/ui/app_theme_documents_and_localization.cpp",
    "src/ui/main_window_chrome_and_messages.cpp",
    "src/ui/main_window_controls_layout_and_painting.cpp",
    "src/ui/popup_menus_commands_and_folder_tree.cpp",
    "src/ui/catalog_loading_and_result_integration.cpp",
    "src/ui/graphics_resources_and_image_cache.cpp",
    "src/ui/canvas_rendering_and_overlays.cpp",
    "src/ui/view_state_controller.cpp",
    "src/ui/face_analysis_controller.cpp",
    "src/ui/viewport_prefetch_controller.cpp",
    "src/ui/performance_policy_controller.cpp",
    "src/ui/navigation_input_and_session.cpp",
    "src/ui/metadata_history_and_file_safety.cpp",
    "src/ui/settings_filters_and_file_commands.cpp",
    "src/transactions/file_transaction.cpp",
    "src/transactions/file_history_replay.cpp",
    "src/transactions/file_history_group_replay.cpp",
    "src/transactions/file_transaction_service.cpp",
    "src/transactions/metadata_transaction_service.cpp",
    "src/transactions/transaction_coordinator.cpp",
    "src/transactions/verified_file_operations_windows.cpp",
    "src/transactions/verified_file_move_windows.cpp",
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def read(root: Path, relative: str) -> str:
    path = root / relative
    if not path.is_file():
        fail(f"missing required file: {relative}")
    return path.read_text(encoding="utf-8")


def release_metadata(root: Path) -> dict[str, str]:
    header = read(root, "src/version.h")
    product = re.search(r'#define\s+QS_VERSION_STRING\s+"([^"]+)"', header)
    file_version = re.search(r'#define\s+QS_VERSION_FILE_STRING\s+"([^"]+)"', header)
    package = re.search(r'#define\s+QS_VERSION_PACKAGE_STRING\s+"([^"]+)"', header)
    if not product or not file_version or not package:
        fail("src/version.h is missing one or more canonical QuickSift version macros")
    product_version = product.group(1)
    file_version_value = file_version.group(1)
    package_version = package.group(1)
    if not re.fullmatch(r"\d+\.\d+\.\d+\.0", file_version_value):
        fail(f"Windows/MSIX file version must have a zero revision component; got {file_version_value}")
    return {
        "product": product_version,
        "file": file_version_value,
        "package": package_version,
        "slug": re.sub(r"\s+", "-", product_version),
        "msix": file_version_value,
    }


def strip_cpp(text: str) -> str:
    """Replace comments and string/character contents while preserving newlines."""
    output: list[str] = []
    index = 0
    length = len(text)
    state = "code"
    raw_end = ""
    while index < length:
        char = text[index]
        next_char = text[index + 1] if index + 1 < length else ""
        if state == "code":
            if char == "/" and next_char == "/":
                output.extend("  "); index += 2; state = "line_comment"; continue
            if char == "/" and next_char == "*":
                output.extend("  "); index += 2; state = "block_comment"; continue
            raw_match = re.match(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(', text[index:])
            if raw_match:
                token = raw_match.group(0)
                raw_end = ")" + raw_match.group(1) + '"'
                output.extend(" " * len(token)); index += len(token); state = "raw_string"; continue
            if char == '"':
                output.append(" "); index += 1; state = "string"; continue
            if char == "'":
                numeric_prefix = re.search(r"(?:0[xX][0-9A-Fa-f]+|[0-9][0-9A-Za-z]*)$",
                    text[max(0, index - 32):index])
                if numeric_prefix and next_char.isalnum():
                    output.append(char); index += 1; continue
                output.append(" "); index += 1; state = "character"; continue
            output.append(char); index += 1; continue
        if state == "line_comment":
            if char == "\n": output.append("\n"); state = "code"
            else: output.append(" ")
            index += 1; continue
        if state == "block_comment":
            if char == "*" and next_char == "/":
                output.extend("  "); index += 2; state = "code"
            else:
                output.append("\n" if char == "\n" else " "); index += 1
            continue
        if state == "raw_string":
            if text.startswith(raw_end, index):
                output.extend(" " * len(raw_end)); index += len(raw_end); state = "code"
            else:
                output.append("\n" if char == "\n" else " "); index += 1
            continue
        if state in {"string", "character"}:
            if char == "\\" and index + 1 < length:
                output.append(" "); output.append("\n" if next_char == "\n" else " "); index += 2; continue
            terminator = '"' if state == "string" else "'"
            if char == terminator:
                output.append(" "); index += 1; state = "code"
            else:
                output.append("\n" if char == "\n" else " "); index += 1
            continue
    if state in {"block_comment", "string", "character", "raw_string"}:
        fail(f"unterminated C++ lexical construct: {state}")
    return "".join(output)


def check_balanced(path: Path) -> None:
    clean = strip_cpp(path.read_text(encoding="utf-8"))
    stack: list[tuple[str, int]] = []
    pairs = {')': '(', ']': '[', '}': '{'}
    line = 1
    for char in clean:
        if char == "\n": line += 1
        elif char in "([{": stack.append((char, line))
        elif char in ")]}":
            if not stack or stack[-1][0] != pairs[char]:
                fail(f"{path}: unmatched {char} near line {line}")
            stack.pop()
    if stack:
        char, opening = stack[-1]
        fail(f"{path}: unmatched {char} opened near line {opening}")


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0: fail(f"missing function: {signature}")
    opening = text.find("{", start)
    if opening < 0: fail(f"missing function body: {signature}")
    clean = strip_cpp(text[opening:])
    depth = 0
    for index, char in enumerate(clean):
        if char == "{": depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0: return text[opening:opening + index + 1]
    fail(f"unterminated function body: {signature}")
    return ""


def verify(root: Path) -> None:
    versions = release_metadata(root)
    for required in ("CMakeLists.txt", "CODE_GUIDE.md", "ARCHITECTURE.md", "TESTING.md",
                     "build.cmd", "build-release.cmd", "build.bat", "package-msix.bat",
                     "open-in-visual-studio.cmd", "LICENSE", "SOURCE_CODE.txt",
                     "scripts/verify-source.ps1", "scripts/build.ps1", "scripts/package-msix.ps1",
                     "packaging/msix/AppxManifest.xml.in",
                     "packaging/msix/Assets/StoreLogo.png",
                     "packaging/msix/Assets/Square44x44Logo.png",
                     "packaging/msix/Assets/Square150x150Logo.png",
                     "src/platform/application_data_paths.h"):
        read(root, required) if not required.endswith(".png") else (root / required).is_file() or fail(f"missing required file: {required}")

    powershell_verifier = read(root, "scripts/verify-source.ps1")
    for token in ("metadata_transaction_execution.cpp", "$MetadataTransactionText",
                  "$MetadataExecutionText"):
        if token not in powershell_verifier:
            fail(f"Windows source verifier is stale relative to split metadata execution: {token}")
    for token in ("function Assert-PerformanceContract",
                  "Assert-PerformanceContract 'IsThumbnailPathCurrent'"):
        if token not in powershell_verifier:
            fail(f"Windows adaptive-performance verifier lacks the robust contract assertion: {token}")
    if "foreach ($Contract in @(" in powershell_verifier:
        fail("Windows adaptive-performance verifier reintroduced the fragile nested-array contract loop")

    build_script = read(root, "scripts/build.ps1")
    for token in (
        "Stream native output live",
        "Start-Transcript -Path $BuildLogPath -Force",
        "QuickSift-BuildLog-{0}.txt",
        "'--fresh'",
        "Only retry when the diagnostics point to a stale/incomplete",
        "Remove-Item -LiteralPath $InstalledDir -Recurse -Force",
        "Full build transcript: $BuildLogPath",
    ):
        if token not in build_script:
            fail(f"Windows build wrapper lacks resilient configure/diagnostic behavior: {token}")
    if "$CapturedOutput = @(& $FilePath @Arguments 2>&1)" in build_script:
        fail("Windows build wrapper buffers native output instead of streaming it")
    if "[Collections.Generic.Queue[string]]::new()" in build_script or "while ($Tail.Count -gt 30)" in build_script:
        fail("Windows build wrapper still truncates retained build output")
    if "-DQS_BUILD_TESTS=OFF" in build_script or "[switch]$SkipTests" in build_script:
        fail("Retired CTest build controls remain in the Windows build wrapper")

    cmake = read(root, "CMakeLists.txt")
    if "include(CTest)" in cmake or "QS_BUILD_TESTS" in cmake or "enable_testing()" in cmake:
        fail("Retired CTest configuration remains in CMakeLists.txt")
    presets = read(root, "CMakePresets.json")
    if '"testPresets"' in presets or "QS_BUILD_TESTS" in presets:
        fail("Retired CTest configuration remains in CMakePresets.json")

    # Guard native-only compile integration that the Linux portable target cannot compile.
    runtime_support_header = read(root, "src/runtime_support.h")
    if "#include <unordered_map>" not in runtime_support_header:
        fail("native runtime support uses std::unordered_map without its direct include")
    application_support_header = read(root, "src/app/application_support.h")
    application_support = read(root, "src/app/application_support.cpp")
    if "extern std::atomic<unsigned> gRetainedWicDecoderSessions" not in application_support_header or \
            "std::atomic<unsigned> gRetainedWicDecoderSessions" not in application_support:
        fail("retained WIC decoder-session policy is not shared across worker translation units")
    metadata_ui = read(root, "src/ui/metadata_history_and_file_safety.cpp")
    if ("ID_PICK_STATE_MENU" not in metadata_ui or
            re.search(r"(?<![A-Z0-9_])ID_UNMARK(?![A-Z0-9_])", metadata_ui) or
            re.search(r"(?<![A-Z0-9_])ID_PICK(?![A-Z0-9_])", metadata_ui) or
            re.search(r"(?<![A-Z0-9_])ID_REJECT(?![A-Z0-9_])", metadata_ui)):
        fail("metadata UI repaint contract still references removed standalone Pick/Reject/Unmark controls")
    viewport_controller_native = read(root, "src/ui/viewport_prefetch_controller.cpp")
    if "const uint64_t viewportEpoch = PublishThumbnailViewport(0, {});" in viewport_controller_native:
        fail("predictive prefetch retains an unused viewport epoch under MSVC warnings-as-errors")

    main = read(root, "src/main.cpp")
    if "class QuickSiftApplication" in main or '#include "ui/' in main:
        fail("src/main.cpp is not a thin process entry point")
    if "wWinMain" not in main or '#include "app/quicksift_application.h"' not in main:
        fail("src/main.cpp does not route through the application composition root")

    app_header = read(root, "src/app/quicksift_application.h")
    app_internal_header = read(root, "src/app/quicksift_application_internal.h")
    if app_header.count("class QuickSiftApplication {") != 1 or \
            "class QuickSiftApplicationImpl;" not in app_header or \
            "std::unique_ptr<QuickSiftApplicationImpl>" not in app_header:
        fail("QuickSiftApplication does not use the stable PIMPL boundary")
    if not app_internal_header.startswith("// OWNER:") or \
            app_internal_header.count("class QuickSiftApplicationImpl {") != 1:
        fail("the private application implementation boundary is missing")
    if "src/app/quicksift_application_internal.h" not in read(root, "CMakeLists.txt"):
        fail("the private application implementation header is not registered in CMake")

    obsolete = [p.relative_to(root).as_posix() for p in (root / "src").rglob("*")
                if p.is_file() and p.suffix in OBSOLETE_SUFFIXES]
    if obsolete:
        fail("obsolete inline implementation fragments remain: " + ", ".join(sorted(obsolete)))

    generated = [path.relative_to(root).as_posix() for path in root.rglob("*")
                 if path.is_dir() and
                 (path.name in {".vs", "CMakeFiles", "bin", "dist"} or
                  path.name == "build" or path.name.startswith("build-"))]
    if generated:
        fail("generated build directories remain in the source tree: " +
             ", ".join(sorted(generated)))
    forbidden = [path.relative_to(root).as_posix() for path in root.rglob("*")
                 if path.is_file() and
                 (re.search(r"\.(?:orig|rej|user|suo)$", path.name) or
                  ".before-" in path.name or path.name.endswith("~"))]
    if forbidden:
        fail("backup/editor files remain in the source tree: " +
             ", ".join(sorted(forbidden)))

    cmake = read(root, "CMakeLists.txt")
    for module in REQUIRED_MODULES:
        if module not in cmake:
            fail(f"compiled module is not registered in CMake: {module}")
    unregistered = [path.relative_to(root).as_posix()
                    for path in (root / "src").rglob("*.cpp")
                    if path.relative_to(root).as_posix() not in cmake]
    if unregistered:
        fail("compiled source is not registered in CMake: " +
             ", ".join(sorted(unregistered)))

    for path in (root / "src").rglob("*"):
        if path.is_file() and path.suffix in {".cpp", ".h"}:
            check_balanced(path)

    for token in (
        "VS_STARTUP_PROJECT QuickSift",
        "CMAKE_MSVC_RUNTIME_LIBRARY", "QS_WARNINGS_AS_ERRORS",
        "configure_file(", "resource.rc.in", "QS_VERSION_RESOURCE",
        "QS_VERSION_RESOURCE_RES", "QS_VERSION_RESOURCE_OBJECT", "add_custom_command(",
        "CMAKE_RC_COMPILER", "QS_CVTRES_COMPILER", "QuickSiftVersion.res",
        "QuickSiftVersion.obj", "EXTERNAL_OBJECT TRUE", "list(APPEND QS_APP_SOURCES",
        "/DEPENDENTLOADFLAG:0x800",
    ):
        if token not in cmake: fail(f"CMake is missing required target/contract: {token}")

    version_resource = read(root, "resource.rc.in")
    for token in (
        "1 VERSIONINFO",
        "FILEVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,@PROJECT_VERSION_TWEAK@",
        'VALUE "CompanyName", "Vo Hoang Tri Dung\\0"',
        'VALUE "FileDescription", "QuickSift — Fast Photo Culling for Windows\\0"',
        'VALUE "FileVersion", "@QS_FILE_VERSION@\\0"',
        'VALUE "LegalCopyright", "Copyright © 2026 Vo Hoang Tri Dung\\0"',
        'VALUE "ProductVersion", "@QS_DISPLAY_VERSION@\\0"',
        'VALUE "Comments", "https://quicksift.pages.dev/\\0"',
        'IDI_QS ICON "@QS_ICON_RESOURCE_PATH@"',
    ):

        if token not in version_resource:
            fail(f"version-resource template is missing: {token}")
    if "VS_VERSION_INFO VERSIONINFO" in version_resource:
        fail("version-resource template must use numeric resource ID 1, not an undefined VS_VERSION_INFO identifier")

    binary_verifier = read(root, "scripts/verify-binary.ps1")
    for token in ("Get-PeDependentLoadFlags",
                  "IMAGE_LOAD_CONFIG_DIRECTORY",
                  "DependentLoadFlagsOffset = 78",
                  "$PeLoadConfiguration.Flags -band 0x800"):
        if token not in binary_verifier:
            fail(f"binary verifier is missing direct PE dependent-load verification: {token}")
    if '$ExpectedProductVersion' not in binary_verifier:
        fail("binary verifier must report the defined $ExpectedProductVersion variable")
    if re.search(r'\$ExpectedVersion(?![A-Za-z0-9_])', binary_verifier):
        fail("binary verifier references undefined $ExpectedVersion under PowerShell StrictMode")

    worker_execution = read(root, "src/work/background_work_engine.cpp")
    worker_policy = read(root, "src/work/background_performance_controller.cpp")
    worker_header = read(root, "src/work/background_work_engine.h")
    if ("WorkerTelemetrySnapshot Telemetry() const" in worker_header and
            "mutable std::mutex mutex_;" not in worker_header):
        fail("Worker::Telemetry() const would instantiate lock_guard<const mutex> under MSVC")
    worker = worker_execution + worker_policy
    if "BackgroundCompletionQueue" not in worker or "WM_APP_BACKGROUND_COMPLETION" not in worker:
        fail("background work does not use the owned completion channel")
    if re.search(r"PostMessageW?\s*\([^;]*reinterpret_cast<LPARAM>", worker, re.S):
        fail("background work posts raw pointer payloads through Win32 messages")
    if "BeginExclusivePathWrites" not in worker_policy or "BlockPathsAndWait" not in worker_policy:
        fail("metadata batches no longer use path-scoped decoder/read-ahead handoff")
    if "PushWait" not in worker_execution:
        fail("critical worker completions can be dropped when the completion queue is full")

    performance_policy = read(root, "src/core/performance_policy.cpp")
    completion_queue = read(root, "src/work/background_completion_queue.h")
    adaptive_controller = read(root, "src/ui/performance_policy_controller.cpp")
    viewport_controller = read(root, "src/ui/viewport_prefetch_controller.cpp")
    decode_budget = read(root, "src/core/decode_budget.cpp")
    for token, source in (
        ("Stable governor", adaptive_controller + performance_policy),
        ("activeDecodePermits", performance_policy),
        ("codecThreadsPerDecode", performance_policy + worker_policy),
        ("reusableBufferLimit", performance_policy + worker_policy),
        ("allowOversizedDecode", performance_policy),
        ("OversizedActive", decode_budget + worker_execution + worker_policy),
        ("HighestPriorityWaiterLocked", decode_budget),
        ("TryPopNext", completion_queue),
        ("OldestAgeMilliseconds", completion_queue),
        ("uiCompletionTimeBudget", adaptive_controller + read(root, "src/ui/catalog_loading_and_result_integration.cpp")),
        ("PublishThumbnailViewport", viewport_controller),
        ("ApplyThumbnailViewport", viewport_controller + worker_policy),
        ("IsThumbnailPathCurrent", viewport_controller + worker_policy + worker_execution),
        ("precedingBatch", completion_queue),
        ("ShouldServiceIdleAfterForegroundBurst", worker_execution),
    ):
        if token not in source:
            fail(f"adaptive performance contract is missing: {token}")

    scanner = read(root, "src/work/folder_scanner.cpp")
    if "BackgroundCompletionQueue" not in scanner or "WM_APP_BACKGROUND_COMPLETION" not in scanner:
        fail("folder scanning does not use the owned completion channel")
    if "PushWait" not in scanner or "PushTerminal" not in scanner:
        fail("folder scanner lacks bounded batch and stop-independent terminal delivery")
    for token in ("ScanCompletionStatus::Failed", "ScanCompletionStatus::Partial",
                  "GetLastError()", "ERROR_NO_MORE_FILES"):
        if token not in scanner:
            fail(f"folder scanner error reporting is missing {token}")
    if re.search(r"PostMessageW?\s*\([^;]*reinterpret_cast<LPARAM>", scanner, re.S):
        fail("folder scanning posts raw pointer payloads through Win32 messages")
    for forbidden in ("SourceStampFor(candidate)", "MetadataStampFor(candidate)",
                      "last_write_time(candidate)"):
        if forbidden in scanner:
            fail(f"folder discovery performs expensive per-file hydration: {forbidden}")
    cancel_body = function_body(scanner, "void FolderScanner::Cancel")
    if "join()" in cancel_body:
        fail("folder-scan cancellation can synchronously join on the UI thread")

    thumbnail_requests = read(root, "src/ui/graphics_resources_and_image_cache.cpp")
    if "thumbnailViewportEpoch" not in thumbnail_requests and "reviewState_.Epochs().thumbnailViewport" not in thumbnail_requests:
        fail("renderer-triggered thumbnail fallback can bypass the model-owned viewport epoch")

    thumbnail_layout = read(root, "src/review/thumbnail_layout.cpp")
    for token in ("cellHeight", "HitTestThumbnail", "ScrollToRevealThumbnail",
                  "ScrollToCenterThumbnail"):
        if token not in thumbnail_layout:
            fail(f"shared thumbnail-layout contract is missing: {token}")

    thumbnail_prefetch = read(root, "src/review/thumbnail_prefetch_policy.cpp") + read(
        root, "src/review/thumbnail_prefetch_policy.h")
    for token in ("ComputeScrollLoadBudget", "ScrollMotionPhase", "ConcurrentThumbnailDecodeCap",
                  "EffectiveVelocityPixelsPerSecond", "placeholdersOnly"):
        if token not in thumbnail_prefetch:
            fail(f"thumbnail scroll-load policy contract is missing: {token}")
    viewport_prefetch = read(root, "src/ui/viewport_prefetch_controller.cpp")
    for token in ("CurrentThumbnailScrollLoadBudget", "ApplyThumbnailScrollLoadBudget",
                  "ThumbnailScrollPrefersPlaceholders", "ClearThumbnailScrollMotionAdmission"):
        if token not in viewport_prefetch:
            fail(f"viewport scroll-load wiring is missing: {token}")

    data_paths = read(root, "src/platform/application_data_paths.h")
    diagnostics = read(root, "src/diagnostics/diagnostic_log.cpp")
    persistent_cache = read(root, "src/persistent_cache.cpp")
    session_controller = read(root, "src/ui/navigation_input_and_session.cpp")
    main_entry = read(root, "src/main.cpp")
    for token in ("GetCurrentPackageFullName", "GetCurrentPackageFamilyName",
                  'L"Packages"', 'L"LocalState"', 'L"QuickSiftData"',
                  'root / L".cache"', 'root / L"QuickSift.session.ini"',
                  "Never fall back to the executable directory for a packaged process"):
        if token not in data_paths:
            fail(f"portable/MSIX data-root contract is missing: {token}")
    for token in ("kRetainedSessionLogs = 20", "ProbeDirectoryWriteAccess",
                  "PruneSessionLogs", "platform::DataRootPath()", "platform::CacheDirectoryPath()"):
        if token not in diagnostics:
            fail(f"diagnostic retention/data-root contract is missing: {token}")
    if ("platform::CacheDirectoryPath()" not in persistent_cache or
            ": baseDirectory_(quicksift::platform::CacheDirectoryPath())" not in persistent_cache):
        fail("persistent cache is not exclusively rooted below the canonical runtime data root")
    for forbidden in ("ALTER TABLE image_records", "TryMigrateLegacyDirectory", "PreviousQuickSiftUserCacheDirectory"):
        if forbidden in persistent_cache:
            fail(f"obsolete cache/database compatibility remains: {forbidden}")
    for token in ("kDatabaseSchemaVersion = 1", 'PRAGMA user_version;',
                  '"PRAGMA user_version=" + std::to_string(kDatabaseSchemaVersion)',
                  "error != SQLITE_SCHEMA"):
        if token not in persistent_cache:
            fail(f"current-only database schema contract is missing: {token}")
    if "platform::SessionFilePath()" not in session_controller:
        fail("session persistence is not rooted below the canonical runtime data root")
    if ("if (!quicksift::diagnostics::Initialize())" not in main_entry or
            "RunningWithPackageIdentity" not in main_entry or "MessageBoxW" not in main_entry):
        fail("startup does not enforce writable portable/MSIX runtime data")

    msix_script = read(root, "scripts/package-msix.ps1")
    msix_manifest = read(root, "packaging/msix/AppxManifest.xml.in")
    for token in ("QS_MSIX_IDENTITY_NAME", "QS_MSIX_PUBLISHER", "QS_SOURCE_URL",
                  "makeappx.exe", "QS_VERSION_FILE_STRING", "QS_VERSION_STRING"):
        if token not in msix_script:
            fail(f"MSIX packaging contract is missing: {token}")
    manifest_version = re.search(r'<Identity\b[^>]*\bVersion="([^"]+)"', msix_manifest)
    if not manifest_version:
        fail("MSIX manifest does not declare an Identity Version")
    if manifest_version.group(1) != versions["msix"]:
        fail(f"MSIX manifest version {manifest_version.group(1)!r} does not match canonical file version {versions['msix']!r} from src/version.h")
    for token in ('EntryPoint="Windows.FullTrustApplication"', 'Name="Windows.Desktop"',
                  'PublisherDisplayName>Vo Hoang Tri Dung<', 'runFullTrust', 'QuickSift.exe'):
        if token not in msix_manifest:
            fail(f"MSIX manifest contract is missing: {token}")

    textual_suffixes = {".cpp", ".h", ".md", ".txt", ".ps1", ".py", ".json", ".in", ".cmd", ".bat", ".xml"}
    previous_product_tokens = ("quick" + "photo", "photo" + "shortlisting")
    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in textual_suffixes:
            text = path.read_text(encoding="utf-8", errors="ignore").lower()
            if any(token in text for token in previous_product_tokens):
                fail(f"previous product identity remains in release source: {path.relative_to(root)}")

    path_owner = root / "src/platform/application_data_paths.h"
    for path in (root / "src").rglob("*"):
        if path.is_file() and path.suffix in {".cpp", ".h"} and path != path_owner:
            text = path.read_text(encoding="utf-8", errors="ignore")
            if "ExecutableDirectory(" in text or "current_path(" in text:
                fail(f"executable/current-directory ownership escaped the package path resolver: {path.relative_to(root)}")
    source_text = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in (root / "src").rglob("*")
        if path.is_file() and path.suffix in {".cpp", ".h"}
    )
    for forbidden in ("WriteProcessMemory(", "CreateRemoteThread(", "PAGE_EXECUTE_READWRITE",
                      "VirtualProtect(", "ShellExecuteW(", "CreateProcessW(", "WinExec(",
                      "URLDownloadToFile"):
        if forbidden in source_text:
            fail(f"packaged-release hygiene forbids unexpected self-modifying/process-launch primitive: {forbidden}")

    settings = read(root, "src/ui/settings_filters_and_file_commands.cpp")
    for signature in ("void QuickSiftApplicationImpl::ShowFormatsMenu",
                      "void QuickSiftApplicationImpl::ShowDateFilterMenu"):
        if "LoadFolder(currentFolder_)" in function_body(settings, signature):
            fail(f"presentation filter still forces a full folder rescan: {signature}")
    operation = function_body(settings, "void QuickSiftApplicationImpl::FileOperation")
    if "transactions_.SubmitFile" not in operation:
        fail("FileOperation does not submit an immutable asynchronous transaction")
    if re.search(r"\b(?:CopyPath|MovePath|RemoveIfFingerprintMatches)\s*\(", operation):
        fail("FileOperation executes verified transfer primitives on the UI thread")

    # Release contract: RAW preview-only mode must be literal, persisted, and race-safe.
    bundled_codecs = read(root, "src/bundled_codecs.cpp")
    bundled_header = read(root, "src/bundled_codecs.h")
    worker_header = read(root, "src/work/background_work_engine.h")
    worker_source = read(root, "src/work/background_work_engine.cpp") + read(
        root, "src/work/background_decode_policy.cpp")
    graphics_cache = read(root, "src/ui/graphics_resources_and_image_cache.cpp")
    canvas_rendering = read(root, "src/ui/canvas_rendering_and_overlays.cpp")
    result_integration = read(root, "src/ui/catalog_loading_and_result_integration.cpp")
    app_internal_release = read(root, "src/app/quicksift_application_internal.h")
    for token in ("Load only JPG previews for RAW", "ID_SETTING_RAW_JPEG_PREVIEWS_ONLY",
                  "SetRawJpegPreviewOnly", "LoadOnlyRawJpegPreviews"):
        if token not in settings + session_controller + worker_header:
            fail(f"RAW JPG-preview-only setting contract is missing: {token}")
    for token in ("rawJpegPreviewOnly", "thumbnail->type != LIBRAW_IMAGE_JPEG",
                  "options.rawJpegPreviewOnly || Cancelled(options)"):
        if token not in bundled_header + bundled_codecs:
            fail(f"RAW JPG-preview-only codec guard is missing: {token}")
    for token in ("rawJpegPreviewOnly_", "raw-jpeg-only", "aRawJpegOnly == bRawJpegOnly",
                  "job.rawJpegPreviewOnly != rawJpegPreviewOnly_.load",
                  "desiredVisualTargets_.clear()"):
        if token not in worker_header + worker_source:
            fail(f"RAW decode-policy queue/coalescing guard is missing: {token}")
    if "!rawJpegOnly" not in worker_source and "sourceJob.rawJpegPreviewOnly" not in worker_source:
        fail("RAW JPG-preview-only mode can consume or persist ambiguous bitmap/tile cache pixels")
    if "if (loadOnlyRawJpegPreviews_ && IsRawExtension(photo.extension)) return;" not in canvas_rendering:
        fail("RAW JPG-preview-only mode can still request deep-zoom RAW tiles")
    if "result->kind == JobKind::DecodeTile || !result->previewOnly" not in result_integration:
        fail("late full-RAW pixels are not rejected after the JPG-preview-only toggle changes")
    if "if (rawJpegOnly) requestFull = false;" not in graphics_cache:
        fail("RAW JPG-preview-only display requests can still promote to full RAW decode")
    if "bool loadOnlyRawJpegPreviews_ = false;" not in app_internal_release:
        fail("RAW JPG-preview-only setting must default to off")

    transactions = "".join((
        read(root, "src/transactions/file_transaction.cpp"),
        read(root, "src/transactions/file_transaction_group_support.h"),
        read(root, "src/transactions/file_history_replay.cpp"),
        read(root, "src/transactions/file_history_group_replay.cpp"),
    ))
    for token in ("cancellationRequested", "RemoveIfUnchanged", "MoveIfUnchanged",
                  "CopyIfUnchanged", "replayCompleted", "historyItems",
                  "lastingTransferItems", "completed media groups remain recorded",
                  "owners", "companion", "groupId", "CompensateUndo", "CompensateRedo"):
        if token not in transactions: fail(f"transaction executor is missing {token}")

    file_tx_header = read(root, "src/transactions/file_transaction.h")
    file_tx_service = read(root, "src/transactions/file_transaction_service.cpp")
    file_tx_service_header = read(root, "src/transactions/file_transaction_service.h")
    coordinator_header_release = read(root, "src/transactions/transaction_coordinator.h")
    popup_status = read(root, "src/ui/popup_menus_commands_and_folder_tree.cpp")
    navigation_timers = read(root, "src/ui/navigation_input_and_session.cpp")
    app_support_header = read(root, "src/app/application_support.h")
    for token in ("FileTransactionProgress", "completedFiles", "totalFiles",
                  "FileTransactionProgressSink"):
        if token not in file_tx_header:
            fail(f"file-operation progress model is missing: {token}")
    for token in ("completedFiles += group.owners.size()",
                  "progressSink(completedFiles, totalFiles)"):
        if token not in transactions:
            fail(f"file-operation progress does not count committed media files correctly: {token}")
    if "progressSink(completedFiles, totalFiles)" in transactions and             transactions.index("progressSink(completedFiles, totalFiles)") < transactions.index("if (groupSucceeded)"):
        fail("file-operation progress can advance before a media group commits")
    for token in ("Progress() const", "progressStartedAt_", "progress_->token != token"):
        if token not in file_tx_service_header + file_tx_service:
            fail(f"race-safe file-operation progress snapshot is missing: {token}")
    for token in ("FileProgress() const", "FileBusy() const"):
        if token not in coordinator_header_release:
            fail(f"transaction coordinator progress contract is missing: {token}")
    for token in ("FileOperationStatusLine", "complete files", "minutes remaining"):
        if token not in popup_status:
            fail(f"bottom status-bar file progress is missing: {token}")
    if "ID_TIMER_FILE_PROGRESS" not in app_support_header + navigation_timers + settings:
        fail("file-operation status line lacks a bounded UI refresh timer")

    history_ui = read(root, "src/ui/metadata_history_and_file_safety.cpp")
    for method in ("Undo", "Redo"):
        body = function_body(history_ui, f"void QuickSiftApplicationImpl::{method}")
        if "transactions_.SubmitFile" not in body or "FileTransactionMode::" not in body:
            fail(f"{method} does not submit guarded file history to the transaction service")

    windows_ops = read(root, "src/transactions/verified_file_operations_windows.cpp") + read(
        root, "src/transactions/verified_file_move_windows.cpp")
    for token in ("FILE_SHARE_READ", "FILE_FLAG_OPEN_REPARSE_POINT", "CopyFileExW",
                  "FilesAreByteIdentical", "FlushFileBuffers", "LockedRenameRequest",
                  "OpenHandlesAreByteIdenticalCancellable", "ERROR_NOT_SAME_DEVICE", "FileDispositionInfo",
                  "changeStamp", "contentFingerprint"):
        if token not in windows_ops: fail(f"locked-handle transfer safety is missing {token}")
    if "FilesAreByteIdenticalCancellable(source, destination)" not in windows_ops:
        fail("verified copy does not re-compare the final published destination")
    if "MOVEFILE_REPLACE_EXISTING" in windows_ops:
        fail("verified publication allows destination overwrite")

    metadata = read(root, "src/ui/metadata_history_and_file_safety.cpp")
    metadata_service = read(root, "src/transactions/metadata_transaction_service.cpp")
    metadata_execution = read(root, "src/transactions/metadata_transaction_execution.cpp")
    metadata_transaction = metadata_service + metadata_execution
    for token in ("transactions_.SubmitMetadata", "ConfigureMetadataTransactions",
                  "MetadataTransactionMode::UndoHistory",
                  "MetadataTransactionMode::RedoHistory"):
        if token not in metadata: fail(f"asynchronous metadata orchestration is missing {token}")
    for token in ("beginExclusive", "endExclusive", "cancellationRequested_",
                  "completed", "failureCount"):
        if token not in metadata_transaction:
            fail(f"metadata transaction service is missing {token}")
    if "ApplyMetadataSnapshot" in metadata:
        fail("metadata Undo/Redo regressed to synchronous UI-thread writes")
    if "~GateGuard() noexcept" not in metadata_execution or "catch (...)" not in metadata_execution:
        fail("metadata decoder-gate cleanup is not exception-contained")
    if "affected file was not marked complete" not in metadata_execution:
        fail("metadata per-item exceptions do not preserve truthful partial completion")
    metadata_run = function_body(metadata_service, "void MetadataTransactionService::Run() noexcept")
    if "completed.assign" in metadata_run:
        fail("metadata emergency recovery can erase truthful partial completion")
    if "SelectParallelHistoryItems" not in metadata:
        fail("metadata history partition does not conservatively retain missing completion flags")
    metadata_state = read(root, "src/core/metadata_state.cpp") + read(root, "src/core/metadata_state.h")
    catalog_policy = read(root, "src/core/catalog_policy.cpp")
    application_support = read(root, "src/app/application_support.cpp")
    for token in ("MetadataKnowledge", "MetadataPatch", "MetadataExpectedMatches",
                  "MakeReplayPatch", "ApplyMetadataPatch"):
        if token not in metadata_state:
            fail(f"field-specific authoritative metadata model is missing {token}")
    for token in ("metadataRevision", "pendingMetadataIndices_.push_front",
                  "MetadataStorageKind"):
        if token not in metadata:
            fail(f"metadata stale-read or storage-history contract is missing {token}")
    if "ReadPhotoMetadata(image)" not in application_support:
        fail("sidecar compare-and-swap does not use effective metadata")
    if catalog_policy.count("MetadataKnowledge::Known") < 4:
        fail("metadata filters or rating sort no longer require authoritative values")
    if "intentionally keeps selection scoped to visible results" not in read(root, "USER_GUIDE.md"):
        fail("intentional visible-selection scope is not documented")

    application = read(root, "src/app/quicksift_application.cpp")
    chrome = read(root, "src/ui/main_window_chrome_and_messages.cpp")
    for token in ("SetCompletionSinks", "WM_APP_FILE_TRANSACTION_COMPLETION"):
        if token not in application + chrome: fail(f"file completion delivery is missing {token}")
    close_alerts = read(root, "src/ui/canvas_rendering_and_overlays.cpp")
    shutdown_ui = chrome + close_alerts
    for token in ("transactions_.RequestCloseAtSafeBoundary", "transactions_.CancelAll",
                  "transactions_.Stop", "WM_QUERYENDSESSION", "WM_ENDSESSION",
                  "transactions_.CancelCloseRequest"):
        if token not in shutdown_ui: fail(f"shutdown does not contain transaction coordinator action: {token}")
    for token in ("File operations are still running", "Close anyway", 'L"Wait"',
                  "CloseWithPendingOperations", "closingAfterTransactionCancellation_"):
        if token not in chrome + close_alerts + app_internal_release:
            fail(f"pending-file-operation close confirmation is missing: {token}")
    if "transactions_.RequestCloseAtSafeBoundary()" not in close_alerts:
        fail("Close anyway does not cancel transactions at a safe boundary")
    history_reconcile = history_ui + close_alerts + app_support_header + settings
    for token in ("RelinquishUndoHistory", "RelinquishRedoHistory",
                  "RelinquishGuardedHistory", "OfferExternalChangeHistoryReconcile",
                  "HistoryEntryHasExternalIdentityDivergence", "identityDiverged",
                  "Relinquish history", "Keep history"):
        if token not in history_reconcile:
            fail(f"external-change history reconcile pathway is missing: {token}")
    metadata_tx_header = read(root, "src/transactions/metadata_transaction_service.h")
    metadata_tx_exec = read(root, "src/transactions/metadata_transaction_execution.cpp")
    metadata_sidecar = read(root, "src/app/application_support.cpp")
    metadata_embedded = read(root, "src/embedded_metadata.cpp")
    metadata_reconcile = (history_ui + close_alerts + app_support_header + settings +
                          metadata_tx_header + metadata_tx_exec + metadata_sidecar +
                          metadata_embedded)
    for token in ("RefreshMetadataFromDisk", "OfferExternalChangeMetadataReconcile",
                  "RefreshCatalogFromDiskAfterExternalMetadataConflict",
                  "externalConflict", "conflictCount", "Refresh from disk",
                  "Keep catalog", "changed outside QuickSift"):
        if token not in metadata_reconcile:
            fail(f"external-change metadata reconcile pathway is missing: {token}")
    completion_queue = read(root, "src/work/background_completion_queue.h")
    catalog_integration = read(root, "src/ui/catalog_loading_and_result_integration.cpp")
    if "DrainInto(std::deque<BackgroundCompletion>& destination) noexcept" not in completion_queue:
        fail("completion draining does not use the no-throw ownership-transfer API")
    if "pending.swap(queue_)" in completion_queue or "result.reserve(pending.size())" in completion_queue:
        fail("completion draining can lose committed results on allocation failure")
    for token in ("ScanCompletionStatus::Failed", "ScanCompletionStatus::Succeeded",
                  "AcknowledgeBatch", "prospectiveMetadataQueue", "PumpMetadataQueue"):
        if token not in catalog_integration:
            fail(f"catalog scan integration contract is missing {token}")

    framework_header = read(root, "src/ui/framework/macos_ui_framework.h")
    framework_controls = read(root, "src/ui/framework/macos_ui_framework.cpp")
    framework_gdi = read(root, "src/ui/framework/macos_ui_framework_gdi.cpp")
    framework_popup = read(root, "src/ui/framework/macos_ui_framework_popup.cpp")
    framework_d2d = read(root, "src/ui/framework/macos_ui_framework_d2d.cpp")
    framework_models = read(root, "src/ui/framework/ui_models.h")
    for token in ("CreateButton", "CreateTooltip", "PaintButton", "PaintTitleBar",
                  "PaintToast", "PaintAlert", "TrackDropdown", "PaintMediaCard",
                  "PopupMenuModel", "ToastPresenter", "AlertPresenter",
                  "ButtonHoverBlend", "EaseToward", "Smoothstep01"):
        if token not in framework_header + framework_models:
            fail(f"central UI framework contract is missing {token}")
    for implementation, token in ((framework_controls, "ButtonSubclassProc"),
                                  (framework_gdi, "PaintTitleBar"),
                                  (framework_popup, "TrackDropdown"),
                                  (framework_d2d, "PaintToast")):
        if token not in implementation:
            fail(f"central UI framework implementation is missing {token}")
    if "MacOsUiFramework uiFramework_" not in app_internal_header:
        fail("the application does not own one central UI framework instance")

    feature_ui_files = [path for path in (root / "src/ui").glob("*.cpp")
                        if path.name != "ui_design_system.cpp"]
    component_paint = re.compile(
        r"(?:Fill|Draw)RoundedRect(?:angle|Gp)?\s*\(|\bDrawTextW\s*\(|"
        r"\b(?:FillRect|FrameRect|RoundRect|TextOutW|MoveToEx|LineTo|Polygon|Ellipse)\s*\(")
    offenders = []
    direct_creation = {}
    for path in feature_ui_files:
        stripped = strip_cpp(path.read_text(encoding="utf-8"))
        if component_paint.search(stripped):
            offenders.append(path.relative_to(root).as_posix())
        count = len(re.findall(r"\bCreateWindowExW?\s*\(", stripped))
        if count:
            direct_creation[path.name] = count
    if offenders:
        fail("feature modules bypass the central component renderer: " +
             ", ".join(sorted(offenders)))
    allowed_top_level_creation = {
        "app_theme_documents_and_localization.cpp": 1,
        "main_window_chrome_and_messages.cpp": 1,
    }
    if direct_creation != allowed_top_level_creation:
        fail("child controls bypass central creation or an unexpected top-level window appeared: " +
             repr(direct_creation))
    controls_ui = read(root, "src/ui/main_window_controls_layout_and_painting.cpp")
    canvas_ui = read(root, "src/ui/canvas_rendering_and_overlays.cpp")
    document_ui = read(root, "src/ui/app_theme_documents_and_localization.cpp") + read(
        root, "src/ui/document_and_menu_models.cpp")
    popup_ui = read(root, "src/ui/popup_menus_commands_and_folder_tree.cpp")
    settings_ui = read(root, "src/ui/settings_filters_and_file_commands.cpp")
    for token in ("uiFramework_.CreateButton", "uiFramework_.PaintButton",
                  "uiFramework_.PaintPanel"):
        if token not in controls_ui: fail(f"main controls bypass the UI framework: {token}")
    for token in ("uiFramework_.PaintToast", "uiFramework_.PaintAlert",
                  "uiFramework_.PaintMediaCard", "uiFramework_.PaintOverlayLabel"):
        if token not in canvas_ui: fail(f"canvas components bypass the UI framework: {token}")
    for token in ("uiFramework_.CreateButton", "uiFramework_.CreateTooltip",
                  "state->framework->PaintButton", "state->framework->PaintPanel"):
        if token not in document_ui: fail(f"document UI bypasses the UI framework: {token}")
    if "uiFramework_.TrackDropdown" not in popup_ui:
        fail("dropdown menus do not route through the central UI framework")
    if "GetMessageW(&message, menu, 0, 0)" not in framework_popup or \
            "GetMessageW(&message, nullptr" in framework_popup:
        fail("dropdown popup can re-enter unrelated owner-window message handlers")
    if "TrackDropdownMenu" not in settings_ui or "std::vector<MenuItem>" not in settings_ui:
        fail("feature menus do not declare central framework MenuItem models")
    for legacy in ("GlassButtonSubclassProc", "kGlassButtonHotProperty",
                   "PopupMenuRuntime", "PopupMenuWndProc", "DrawGlassMenuItem",
                   "MenuVisualItem", "TrackGlassMenu"):
        if legacy in app_internal_header + controls_ui + chrome + popup_ui + settings_ui:
            fail(f"legacy hand-painted UI path remains: {legacy}")

    catalog_header = read(root, "src/core/catalog_store.h")
    history_header = read(root, "src/core/history_store.h")
    selection_header = read(root, "src/core/selection_store.h")
    if ("CatalogStore" not in catalog_header or "HistoryStore" not in history_header or
            "SelectionStore" not in selection_header):
        fail("catalog/history/selection ownership services are missing")
    for forbidden_api in ("VisibleIndices()", "VisiblePositions()",
                          "UndoEntries()", "RedoEntries()", "MutablePhotoAt"):
        if forbidden_api in catalog_header + history_header:
            fail(f"an invariant-owning store exposes mutable backing storage: {forbidden_api}")
    for legacy_state in ("allPathIndex_", "allPhotos_", "visiblePhotoIndices_",
                         "visiblePositionByCatalogIndex_", "undoHistory_", "redoHistory_",
                         "selected_", "anchorIndex_"):
        if legacy_state in app_internal_header:
            fail(f"the application still owns legacy store state: {legacy_state}")
    for token in ("SetVisibleOrder", "ReplacePath", "AppendBatch", "CompleteReplay",
                  "EstimateEntryBytes", "RecordNew", "Retain", "SetAnchor"):
        if token not in catalog_header + history_header + selection_header:
            fail(f"store invariant API is missing: {token}")

    file_service_header = read(root, "src/transactions/file_transaction_service.h")
    file_service = read(root, "src/transactions/file_transaction_service.cpp")
    metadata_service_header = read(root, "src/transactions/metadata_transaction_service.h")
    metadata_service = read(root, "src/transactions/metadata_transaction_service.cpp")
    metadata_execution = read(root, "src/transactions/metadata_transaction_execution.cpp")
    metadata_transaction = metadata_service + metadata_execution
    coordinator = read(root, "src/transactions/transaction_coordinator.cpp")
    if "std::unique_ptr<VerifiedFileOperations>" not in file_service_header:
        fail("the file transaction service does not inject its verified backend")
    for name, header, implementation in (("file", file_service_header, file_service),
                                          ("metadata", metadata_service_header, metadata_service)):
        if ": thread_(" in implementation or "thread_ = std::thread" not in implementation:
            fail(f"the {name} transaction worker can start before member construction")
        if header.rfind("std::thread thread_") < header.rfind("stopping_"):
            fail(f"the {name} transaction thread is not declared after observed state")
    for token in ("SubmitFile", "SubmitMetadata", "CancelCloseRequest",
                  "CancelAll", "Stop"):
        if token not in coordinator:
            fail(f"transaction coordinator contract is missing: {token}")
    for header, implementation in ((file_service_header, file_service),
                                   (metadata_service_header, metadata_service)):
        if "shared_ptr<const std::function<void()>>" not in header or "(*sink)()" not in implementation:
            fail("a transaction worker can allocate while copying its completion callback")
    if "shared_ptr<const MetadataTransactionCallbacks>" not in metadata_service_header:
        fail("metadata callbacks are copied through an allocating std::function path on the worker")
    if "SetCancellationSource(&cancellationRequested_)" not in file_service:
        fail("long-running verified file operations do not receive cancellation")

    embedded_metadata = read(root, "src/embedded_metadata.cpp")
    text_io = read(root, "src/app/application_text_io.cpp")
    for token in ("PixelEssenceHash", "LockedRenameRequest", "MarkLockedFileForDeletion"):
        if token not in embedded_metadata:
            fail(f"embedded metadata publication hardening is missing {token}")
    for token in ("LockedRenameRequest", "FileHandleMatchesBytes",
                  "publishTemporary.Apply", "MarkLockedFileForDeletion"):
        if token not in text_io:
            fail(f"text/sidecar publication hardening is missing {token}")

    review_model = read(root, "src/review/review_state_model.cpp") + read(root, "src/review/review_state_model.h")
    review_async = read(root, "src/review/review_async_policy.cpp") + read(root, "src/review/review_async_policy.h")
    view_policy = read(root, "src/review/view_transform_policy.cpp")
    view_state = read(root, "src/ui/view_state_controller.cpp")
    navigation = read(root, "src/ui/navigation_input_and_session.cpp")
    app_internal = read(root, "src/app/quicksift_application_internal.h")
    worker = read(root, "src/work/background_work_engine.cpp")
    face_policy = read(root, "src/core/face_policy.cpp")
    for token in ("ZoomViewAt", "PanView", "physicalScale", "visibleSourceWidth",
                  "visibleSourceHeight"):
        if token not in view_policy:
            fail(f"pane-aware view-transform policy is missing: {token}")
    for token in ("IndependentPaneState", "SetSyncCompareView",
                  "DisableFaceLockPreservingViews", "UpdateThumbnailViewport",
                  "IsThumbnailPathDesired", "ActivePath", "ReplaceComparePath", "FreshFitView",
                  "independentPanes_"):
        if token not in review_model:
            fail(f"coordinated review-state contract is missing: {token}")
    for token in ("ClassifyPostedResult", "NavigationInvalidatesPostedResult",
                  "IsStaleThumbnailViewport"):
        if token not in review_async:
            fail(f"review async-admission contract is missing: {token}")
    for token in ("DisableFaceLockPreservingViews", "ViewAreaForPath",
                  "ResolveViewForPath"):
        if token not in view_state + review_model:
            fail(f"view-state/FaceLock adapter contract is missing: {token}")
    for token in ("HitCompareSlot(x, y)", "RecenterFaceLockForPath",
                  "AdvanceReviewNavigationEpoch", "kFaceSharpnessDecodeSize"):
        if token not in navigation + view_state + worker:
            fail(f"zoom, comparison, or face-analysis regression guard is missing: {token}")
    for forbidden in ("compareHits_", "independentViews_", "independentFaceOffsets_",
                      "currentPath_", "focusPath_", "viewZoom_", "viewCenterX_",
                      "viewCenterY_", "viewRotation_", "thumbScroll_",
                      "thumbMaxScroll_", "navigationEpoch_", "thumbnailViewportEpoch_"):
        if forbidden in app_internal:
            fail(f"legacy split review-state authority remains in application object: {forbidden}")
    if "RefreshThumbnailPrefetchQueues" in viewport_controller + read(root, "src/work/background_performance_controller.cpp"):
        fail("worker still generates thumbnail viewport state instead of mirroring the review model")
    if "worker_.IsThumbnailPathCurrent" in read(root, "src/ui/catalog_loading_and_result_integration.cpp"):
        fail("UI result admission consults worker mirror instead of authoritative review state")
    if "result->faceBlurScanned = sharpness.scanned" not in worker:
        fail("small-face sharpness is incorrectly marked complete without analysis")
    if "FaceAnchorComesBefore" not in face_policy:
        fail("primary-face selection lacks deterministic tie breaking")

    face_retry = read(root, "src/core/face_detection_policy.cpp")
    face_controller = read(root, "src/ui/face_analysis_controller.cpp")
    face_detector = read(root, "src/face_detector.cpp")
    catalog_integration = read(root, "src/ui/catalog_loading_and_result_integration.cpp")
    for token in ("MaximumTransientFailures", "CapabilityRefreshDue",
                  "AllowExplicitRetry", "Clock::time_point::max"):
        if token not in face_retry + read(root, "src/core/face_detection_policy.h"):
            fail(f"bounded face-analysis retry policy is missing: {token}")
    for token in ("RefreshFaceDetectorCapability", "EnqueueFaceAnalysis",
                  "HandleFaceAnalysisOutcome", "EnableWindow"):
        if token not in face_controller:
            fail(f"face-detector capability controller is missing: {token}")
    if "worker_.EnqueueFace" in navigation + read(root, "src/ui/viewport_prefetch_controller.cpp") + catalog_integration:
        fail("face jobs bypass the centralized capability/backoff scheduling gate")
    for token in ("QueryFaceDetectorCapability", "FaceAnalysisOutcome::Unavailable",
                  "gThreadFaceDetector = nullptr"):
        if token not in face_detector:
            fail(f"face-detector adapter failure handling is missing: {token}")
    if "HandleFaceAnalysisOutcome(*result)" not in catalog_integration:
        fail("face-analysis failures are not reported back to the retry policy")

    for document in ("README.md", "DEVELOPER_GUIDE.md", "ARCHITECTURE.md", "TESTING.md"):
        if "CODE_GUIDE.md" not in read(root, document):
            fail(f"{document} does not link to CODE_GUIDE.md")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        verify(args.source_dir.resolve())
    except Exception as error:
        print(f"QuickSift portable source verification failed: {error}")
        return 1
    print("QuickSift portable source verification passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
