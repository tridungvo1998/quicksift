vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://chromium.googlesource.com/libyuv/libyuv
    REF d98915a654d3564e4802a0004add46221c4e4348
)

vcpkg_cmake_get_vars(cmake_vars_file)
include("${cmake_vars_file}")

set(BUILD_OPTIONS
    -DUNIT_TEST=OFF
)

if(VCPKG_DETECTED_CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND NOT VCPKG_TARGET_IS_UWP)
    message(STATUS "QuickSift: building libyuv with clang-cl to enable x64 SIMD acceleration")
    set(VCPKG_POLICY_SKIP_ARCHITECTURE_CHECK enabled)

    vcpkg_find_acquire_program(CLANG)
    if(CLANG MATCHES "-NOTFOUND")
        message(FATAL_ERROR "QuickSift requires clang-cl to build accelerated libyuv. Install the Visual Studio Clang/LLVM component or let vcpkg acquire Clang.")
    endif()

    get_filename_component(CLANG_BIN "${CLANG}" DIRECTORY)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(CLANG_TARGET "x86_64-pc-windows-msvc")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
        set(CLANG_TARGET "i686-pc-windows-msvc")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(CLANG_TARGET "aarch64-pc-windows-msvc")
    else()
        message(FATAL_ERROR "Unsupported QuickSift libyuv target architecture: ${VCPKG_TARGET_ARCHITECTURE}")
    endif()

    string(APPEND VCPKG_DETECTED_CMAKE_CXX_FLAGS " --target=${CLANG_TARGET}")
    string(APPEND VCPKG_DETECTED_CMAKE_C_FLAGS " --target=${CLANG_TARGET}")

    list(APPEND BUILD_OPTIONS
        -DCMAKE_C_COMPILER=${CLANG_BIN}/clang-cl.exe
        -DCMAKE_CXX_COMPILER=${CLANG_BIN}/clang-cl.exe
        -DCMAKE_C_FLAGS=${VCPKG_DETECTED_CMAKE_C_FLAGS}
        -DCMAKE_CXX_FLAGS=${VCPKG_DETECTED_CMAKE_CXX_FLAGS}
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${BUILD_OPTIONS}
    OPTIONS_DEBUG -DCMAKE_DEBUG_POSTFIX=d
)

vcpkg_cmake_install()
vcpkg_cmake_get_vars(cmake_vars_file)
include("${cmake_vars_file}")

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/libyuv")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/libyuv-config.cmake.in"
    "${CURRENT_PACKAGES_DIR}/share/libyuv/libyuv-config.cmake"
    @ONLY
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
