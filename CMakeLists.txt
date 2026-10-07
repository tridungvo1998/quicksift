cmake_minimum_required(VERSION 3.24)
# Keep src/version.h as the single authoritative application version.
# CMake consumes the numeric components for VERSIONINFO/MSIX generation.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" QS_VERSION_HEADER)

string(REGEX MATCH "#define[ \t]+QS_VERSION_MAJOR[ \t]+([0-9]+)" _ "${QS_VERSION_HEADER}")
set(QS_VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define[ \t]+QS_VERSION_MINOR[ \t]+([0-9]+)" _ "${QS_VERSION_HEADER}")
set(QS_VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define[ \t]+QS_VERSION_PATCH[ \t]+([0-9]+)" _ "${QS_VERSION_HEADER}")
set(QS_VERSION_PATCH "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define[ \t]+QS_VERSION_BUILD[ \t]+([0-9]+)" _ "${QS_VERSION_HEADER}")
set(QS_VERSION_BUILD "${CMAKE_MATCH_1}")

if(QS_VERSION_MAJOR STREQUAL "" OR QS_VERSION_MINOR STREQUAL "" OR
   QS_VERSION_PATCH STREQUAL "" OR QS_VERSION_BUILD STREQUAL "")
    message(FATAL_ERROR "Unable to read complete version from src/version.h")
endif()

string(REGEX MATCH "#define[ \t]+QS_VERSION_STRING[ \t]+\"([^\"]+)\"" _ "${QS_VERSION_HEADER}")
set(QS_DISPLAY_VERSION "${CMAKE_MATCH_1}")
if(QS_DISPLAY_VERSION STREQUAL "")
    message(FATAL_ERROR "Unable to read QS_VERSION_STRING from src/version.h")
endif()

project(QuickSift
    VERSION ${QS_VERSION_MAJOR}.${QS_VERSION_MINOR}.${QS_VERSION_PATCH}.${QS_VERSION_BUILD}
    LANGUAGES CXX
)

option(QS_BUNDLE_CODECS "Statically bundle LibRaw, libavif/dav1d, and WebP decoding" ON)
option(QS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(QS_ENABLE_EHCONT "Enable MSVC EH-continuation metadata; requires every linked static library to be compatible" OFF)

# Keep every target on the same MSVC runtime as the static vcpkg triplet.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(QuickSiftCore STATIC
    src/core/catalog_policy.cpp
    src/core/image_formats.cpp
    src/core/decode_budget.cpp
    src/core/face_policy.cpp
    src/core/face_detection_policy.cpp
    src/core/performance_policy.cpp
    src/core/metadata_state.cpp
    src/core/catalog_store.cpp
    src/core/catalog_store_mutation.cpp
    src/core/path_identity.cpp
    src/core/selection_store.cpp
    src/core/history_store.cpp
    src/core/history_store_memory.cpp
    src/transactions/file_transaction.cpp
    src/transactions/file_transaction_service.cpp
    src/transactions/file_history_replay.cpp
    src/transactions/file_history_group_replay.cpp
    src/transactions/metadata_transaction_service.cpp
    src/transactions/metadata_transaction_execution.cpp
    src/transactions/transaction_coordinator.cpp
    src/review/thumbnail_prefetch_policy.cpp
    src/review/thumbnail_layout.cpp
    src/review/review_state_model.cpp
    src/review/review_async_policy.cpp
    src/metadata/jpeg_orientation.cpp
    src/metadata/xmp_simple_property.cpp
    src/metadata/metadata_value_parser.cpp
    src/ui/framework/ui_models.cpp
    src/review/view_transform_policy.cpp
    src/core/catalog_policy.h
    src/core/image_formats.h
    src/core/decode_budget.h
    src/core/face_policy.h
    src/core/face_detection_policy.h
    src/core/performance_policy.h
    src/core/metadata_state.h
    src/core/catalog_store.h
    src/core/path_identity.h
    src/core/selection_store.h
    src/core/history_store.h
    src/transactions/file_transaction.h
    src/transactions/file_transaction_service.h
    src/transactions/file_history_replay.h
    src/transactions/metadata_transaction_service.h
    src/transactions/transaction_coordinator.h
    src/review/thumbnail_prefetch_policy.h
    src/review/thumbnail_layout.h
    src/review/review_state_model.h
    src/review/review_async_policy.h
    src/metadata/jpeg_orientation.h
    src/metadata/xmp_simple_property.h
    src/metadata/metadata_value_parser.h
    src/ui/framework/ui_models.h
    src/review/view_transform_policy.h
    src/core/history_partition.h
    src/core/work_queue_policy.h
    src/core/stable_file_identity.h
    src/core/scoped_boolean_flag.h
)

# Preserve the modular CMake target contracts used by the source verifier and IDE tooling.
# These aliases expose the production module boundaries without duplicating compilation.
add_library(QuickSift.CatalogStore ALIAS QuickSiftCore)
add_library(QuickSift.HistoryStore ALIAS QuickSiftCore)
add_library(QuickSift.SelectionStore ALIAS QuickSiftCore)
add_library(QuickSift.BackgroundCompletionQueue ALIAS QuickSiftCore)
add_library(QuickSift.FileTransaction ALIAS QuickSiftCore)
add_library(QuickSift.FileTransactionService ALIAS QuickSiftCore)
add_library(QuickSift.MetadataTransaction ALIAS QuickSiftCore)
add_library(QuickSift.TransactionCoordinator ALIAS QuickSiftCore)
add_library(QuickSift.ReviewSubsystem ALIAS QuickSiftCore)
add_library(QuickSift.UiModels ALIAS QuickSiftCore)
add_library(QuickSift.WindowsFileOperations ALIAS QuickSiftCore)

target_compile_features(QuickSiftCore PUBLIC cxx_std_20)
target_include_directories(QuickSiftCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    target_compile_options(QuickSiftCore PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus
        $<$<BOOL:${QS_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(QuickSiftCore PRIVATE -Wall -Wextra -Wpedantic
        $<$<BOOL:${QS_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

# The portable core intentionally builds on non-Windows hosts. The former CTest
# suite was retired because its timing-, filesystem-, UI-, and environment-sensitive
# behavior produced unreliable failures without materially improving the release gate.
# The application target itself remains a native x64 Windows program.
if(NOT WIN32)
    message(STATUS "QuickSift application target skipped: native Windows build required.")
    return()
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "QuickSift 0.9 supports x64 Windows builds only.")
endif()
enable_language(RC)

# Generate the executable resource in the build tree. The resource is later
# compiled to .res and converted to a normal x64 COFF .obj before linking, so
# Visual Studio cannot retain it as a decorative project item.
set(QS_FILE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}")
set(QS_ICON_RESOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/QuickSift.ico")
set(QS_GENERATED_RESOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${QS_GENERATED_RESOURCE_DIR}")
set(QS_VERSION_RESOURCE "${QS_GENERATED_RESOURCE_DIR}/QuickSiftVersion.rc")
set(QS_VERSION_RESOURCE_RES "${QS_GENERATED_RESOURCE_DIR}/QuickSiftVersion.res")
set(QS_VERSION_RESOURCE_OBJECT "${QS_GENERATED_RESOURCE_DIR}/QuickSiftVersion.obj")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resource.rc.in"
    "${QS_VERSION_RESOURCE}"
    @ONLY
)
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/resource.rc.in"
    PROPERTIES HEADER_FILE_ONLY TRUE
)

set(QS_APP_SOURCES
    src/main.cpp
    src/app/application_support.cpp
    src/app/application_text_io.cpp
    src/app/application_support.h
    src/app/quicksift_application.cpp
    src/app/quicksift_application.h
    src/app/quicksift_application_internal.h
    src/work/background_work_engine.cpp
    src/work/background_decode_policy.cpp
    src/work/background_performance_controller.cpp
    src/work/background_work_engine.h
    src/work/folder_scanner.cpp
    src/work/folder_scanner.h
    src/work/background_completion_queue.h
    src/transactions/verified_file_operations_windows.cpp
    src/transactions/verified_file_move_windows.cpp
    src/transactions/verified_file_operations_windows.h
    src/platform/locked_file_publication_windows.h
    src/platform/application_data_paths.h
    src/platform/storage_space.cpp
    src/platform/storage_space.h
    src/ui/app_theme_documents_and_localization.cpp
    src/ui/main_window_chrome_and_messages.cpp
    src/ui/main_window_controls_layout_and_painting.cpp
    src/ui/popup_menus_commands_and_folder_tree.cpp
    src/ui/catalog_loading_and_result_integration.cpp
    src/ui/graphics_resources_and_image_cache.cpp
    src/ui/canvas_rendering_and_overlays.cpp
    src/ui/view_state_controller.cpp
    src/ui/face_analysis_controller.cpp
    src/ui/viewport_prefetch_controller.cpp
    src/ui/performance_policy_controller.cpp
    src/ui/navigation_input_and_session.cpp
    src/ui/metadata_history_and_file_safety.cpp
    src/ui/settings_filters_and_file_commands.cpp
    src/ui/document_and_menu_models.cpp
    src/core/system_profile.cpp
    src/diagnostics/diagnostic_log.cpp
    src/persistent_cache.cpp
    src/bundled_codecs.cpp
    src/face_detector.cpp
    src/embedded_metadata.cpp
    src/localization.cpp
    src/runtime_support.cpp
    src/core/app_types.h
    src/core/metadata_state.h
    src/core/system_profile.h
    src/core/catalog_store.h
    src/core/path_identity.h
    src/core/selection_store.h
    src/core/history_store.h
    src/metadata/jpeg_orientation.h
    src/metadata/xmp_simple_property.h
    src/metadata/metadata_value_parser.h
    src/ui/framework/ui_models.h
    src/review/view_transform_policy.h
    src/core/history_partition.h
    src/core/work_queue_policy.h
    src/core/stable_file_identity.h
    src/core/scoped_boolean_flag.h
    src/diagnostics/diagnostic_log.h
    src/persistent_cache.h
    src/bundled_codecs.h
    src/face_detector.h
    src/embedded_metadata.h
    src/localization.h
    src/runtime_support.h
    src/version.h
    src/ui/ui_design_system.cpp
    src/ui/framework/macos_ui_framework.cpp
    src/ui/framework/macos_ui_framework_gdi.cpp
    src/ui/framework/macos_ui_framework_popup.cpp
    src/ui/framework/macos_ui_framework_d2d.cpp
    src/ui/ui_design_system.h
    src/ui/framework/macos_ui_framework.h
    src/ui/framework/ui_models.h
    src/review/view_transform_policy.h
    src/ui/ui_command_ids.h
    src/ui/ui_control_catalog.h
    src/ui/ui_layout_metrics.h
    resource.h
    resource.rc.in
    QuickSift.ico
    app.manifest
)
# rc.exe creates a Win32 .res file, but Visual Studio 18 may retain a raw
# .res as a project item without forwarding it to link.exe. Convert it to an
# ordinary x64 COFF object with cvtres.exe and make that object a first-class
# source of the executable. MSVC's linker cannot silently discard this input.
get_filename_component(QS_MSVC_TOOL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(QS_CVTRES_COMPILER
    NAMES cvtres.exe cvtres
    HINTS "${QS_MSVC_TOOL_DIR}"
    REQUIRED
)
add_custom_command(
    OUTPUT "${QS_VERSION_RESOURCE_OBJECT}"
    BYPRODUCTS "${QS_VERSION_RESOURCE_RES}"
    COMMAND "${CMAKE_RC_COMPILER}" /nologo
        "/fo${QS_VERSION_RESOURCE_RES}"
        "${QS_VERSION_RESOURCE}"
    COMMAND "${QS_CVTRES_COMPILER}" /nologo /machine:x64
        "/out:${QS_VERSION_RESOURCE_OBJECT}"
        "${QS_VERSION_RESOURCE_RES}"
    DEPENDS
        "${QS_VERSION_RESOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/resource.rc.in"
        "${CMAKE_CURRENT_SOURCE_DIR}/QuickSift.ico"
    COMMENT "Compiling and converting QuickSift version and icon resource"
    VERBATIM
)
set_source_files_properties(
    "${QS_VERSION_RESOURCE_OBJECT}"
    PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QS_APP_SOURCES})
list(APPEND QS_APP_SOURCES "${QS_VERSION_RESOURCE_OBJECT}")
add_executable(QuickSift WIN32 ${QS_APP_SOURCES})
set_property(TARGET QuickSift APPEND PROPERTY
    LINK_DEPENDS "${QS_VERSION_RESOURCE_OBJECT}"
)

# Visual Studio should launch the application target, not ALL_BUILD.
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT QuickSift)
set_property(TARGET QuickSift PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:QuickSift>")

source_group("Generated" FILES "${QS_VERSION_RESOURCE}" "${QS_VERSION_RESOURCE_RES}" "${QS_VERSION_RESOURCE_OBJECT}")
target_compile_features(QuickSift PRIVATE cxx_std_20)
target_include_directories(QuickSift PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(QuickSift PRIVATE QuickSiftCore)
target_compile_definitions(QuickSift PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
    QS_REQUIRE_FACE_DETECTOR=1 QS_USE_EXIV2=1 QS_HARDENED_BUILD=1
)

find_package(exiv2 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
target_link_libraries(QuickSift PRIVATE unofficial::sqlite3::sqlite3)
target_compile_definitions(QuickSift PRIVATE QS_USE_SQLITE=1)

target_link_libraries(QuickSift PRIVATE
    comctl32 d2d1 dwrite dwmapi uxtheme msimg32 windowscodecs shlwapi shell32
    propsys ole32 uuid runtimeobject gdiplus d3d11 dxgi dbghelp psapi comdlg32
)

if(TARGET Exiv2::exiv2lib)
    target_link_libraries(QuickSift PRIVATE Exiv2::exiv2lib)
elseif(TARGET exiv2lib)
    target_link_libraries(QuickSift PRIVATE exiv2lib)
elseif(TARGET Exiv2::exiv2)
    target_link_libraries(QuickSift PRIVATE Exiv2::exiv2)
else()
    message(FATAL_ERROR "Exiv2 was found, but no supported CMake target was exported.")
endif()

if(QS_BUNDLE_CODECS)
    find_package(LibRaw REQUIRED)
    if(NOT LibRaw_r_LIBRARIES)
        message(FATAL_ERROR "The re-entrant LibRaw library was not found.")
    endif()
    target_include_directories(QuickSift PRIVATE ${LibRaw_INCLUDE_DIRS})
    target_link_libraries(QuickSift PRIVATE ${LibRaw_r_LIBRARIES})

    find_package(libavif CONFIG REQUIRED)
    if(TARGET avif)
        target_link_libraries(QuickSift PRIVATE avif)
    elseif(TARGET avif::avif)
        target_link_libraries(QuickSift PRIVATE avif::avif)
    elseif(TARGET avif_static)
        target_link_libraries(QuickSift PRIVATE avif_static)
    else()
        message(FATAL_ERROR "libavif was found, but no supported CMake target was exported.")
    endif()

    find_package(WebP CONFIG REQUIRED)
    target_link_libraries(QuickSift PRIVATE WebP::webpdecoder)

    # Use vcpkg's imported targets instead of plain find_library(). Imported
    # targets select the matching Debug/Release archive for multi-config Visual
    # Studio generators and avoid accidentally pulling a /MTd library into Release.
    find_package(libjpeg-turbo CONFIG REQUIRED)
    if(TARGET libjpeg-turbo::turbojpeg)
        target_link_libraries(QuickSift PRIVATE libjpeg-turbo::turbojpeg)
    elseif(TARGET libjpeg-turbo::turbojpeg-static)
        target_link_libraries(QuickSift PRIVATE libjpeg-turbo::turbojpeg-static)
    else()
        message(FATAL_ERROR "libjpeg-turbo was found, but no TurboJPEG target was exported.")
    endif()

    find_package(libyuv CONFIG REQUIRED)
    if(TARGET yuv)
        target_link_libraries(QuickSift PRIVATE yuv)
    elseif(TARGET libyuv::yuv)
        target_link_libraries(QuickSift PRIVATE libyuv::yuv)
    else()
        message(FATAL_ERROR "libyuv was found, but no supported CMake target was exported.")
    endif()

    target_compile_definitions(QuickSift PRIVATE
        QS_USE_LIBRAW=1 QS_USE_LIBAVIF=1 QS_USE_WEBP=1 QS_USE_TURBOJPEG=1
        QS_USE_LIBYUV=1 LIBRAW_NODLL=1
    )
endif()

if(MSVC)
    target_compile_options(QuickSift PRIVATE
        /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus /GS /sdl /guard:cf
        /ZH:SHA_256
        $<$<BOOL:${QS_WARNINGS_AS_ERRORS}>:/WX>
        $<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/Gy> $<$<CONFIG:Release>:/Gw>
    )
    target_link_options(QuickSift PRIVATE
        /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT
        /GUARD:CF /CETCOMPAT /DEPENDENTLOADFLAG:0x800
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
        $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>
    )

    # EH-continuation metadata is intentionally opt-in. QuickSift bundles static
    # vcpkg libraries (including Exiv2 and LibRaw) that are not compiled with
    # /guard:ehcont. Enabling it only on the executable makes current MSVC linkers
    # fail with LNK2047/LNK1386. Do not use /FORCE:GUARDEHCONT: it can create an
    # executable whose exception-unwind targets are incomplete.
    if(QS_ENABLE_EHCONT)
        target_compile_options(QuickSiftCore PRIVATE /guard:ehcont /Gy)
        target_compile_options(QuickSift PRIVATE /guard:ehcont /Gy)
        target_link_options(QuickSift PRIVATE /guard:ehcont)
    endif()
endif()

set_target_properties(QuickSift PROPERTIES
    VS_GLOBAL_ApplicationManifest "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
    OUTPUT_NAME "QuickSift"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel"
)

include(CheckIPOSupported)
check_ipo_supported(RESULT QS_IPO_SUPPORTED OUTPUT QS_IPO_ERROR)
if(QS_IPO_SUPPORTED)
    set_property(TARGET QuickSift PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "Release IPO is unavailable: ${QS_IPO_ERROR}")
endif()
