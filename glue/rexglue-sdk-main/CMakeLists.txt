# ReXGlue SDK - Xbox 360 Recompilation Toolkit
# Modern C++23 cross-platform SDK for recompiling Xbox 360 executables
cmake_minimum_required(VERSION 3.25)

if(APPLE)
    set(REXGLUE_LANGUAGES C CXX OBJC OBJCXX)
else()
    set(REXGLUE_LANGUAGES C CXX)
endif()

project(ReXGlue
    VERSION 0.9.0
    LANGUAGES ${REXGLUE_LANGUAGES}
    DESCRIPTION "Xbox 360 Recompilation SDK"
)
# Floor only: bump MAJOR.MINOR for API changes. Patch is derived from git tags
# in cmake/rex_version.cmake. See docs/RELEASING.md.

#==========================================================
# Build Options
#==========================================================
option(REXGLUE_BUILD_TESTS       "Build PPC instruction and unit tests" OFF)
option(REXGLUE_CORE_ONLY         "Build core platform libraries without runtime, GPU, or host tools" OFF)
option(REXGLUE_RUNTIME_ONLY      "Also build the headless system runtime without media/UI backends" OFF)
option(REXGLUE_HEADLESS_KERNEL   "Include core HLE exports; stop on unavailable media/UI exports" OFF)
option(REXGLUE_HEADLESS_RENDER_AUDIO
    "Include XAudio render-driver exports for an injected lightweight backend" OFF)
option(REXGLUE_HEADLESS_INPUT
    "Include XAM input exports for an injected lightweight backend" OFF)
if(REXGLUE_HEADLESS_KERNEL AND NOT (REXGLUE_CORE_ONLY AND REXGLUE_RUNTIME_ONLY))
    message(FATAL_ERROR "REXGLUE_HEADLESS_KERNEL requires CORE_ONLY and RUNTIME_ONLY")
endif()
option(REXGLUE_BUILD_GTA4_RECOMP "Build the bundled GTA IV recomp application" OFF)
option(REXGLUE_ENABLE_SANITIZERS "Enable UndefinedBehaviorSanitizer" OFF)
option(REXGLUE_ENABLE_FIDELITYFX "Enable optional AMD FidelityFX integration (experimental)" OFF)
option(REXGLUE_ENABLE_TRACY                "Enable Tracy profiler integration" ON)
option(REXGLUE_ENABLE_PERF_COUNTERS        "Enable lightweight perf counter registry and overlay" ON)
if(REXGLUE_CORE_ONLY)
    set(REXGLUE_ENABLE_TRACY OFF)
    set(REXGLUE_ENABLE_PERF_COUNTERS OFF)
    set(REXGLUE_ENABLE_FIDELITYFX OFF)
endif()
# Propagate profiling defines globally (compiled out in Release)
if(REXGLUE_ENABLE_TRACY)
    add_compile_definitions($<$<NOT:$<CONFIG:Release>>:REXGLUE_ENABLE_PROFILING>)
endif()
if(REXGLUE_ENABLE_PERF_COUNTERS)
    add_compile_definitions($<$<NOT:$<CONFIG:Release>>:REXGLUE_ENABLE_PERF_COUNTERS>)
endif()

# Graphics backend selection
# D3D12: Windows only, ON by default.
# Vulkan: Cross-platform, ON by default on Linux, OFF by default on Windows.
if(WIN32)
    option(REXGLUE_USE_D3D12  "Enable D3D12 graphics backend" ON)
    option(REXGLUE_USE_VULKAN "Enable Vulkan graphics backend" OFF)
else()
    set(REXGLUE_USE_D3D12 OFF)
    option(REXGLUE_USE_VULKAN "Enable Vulkan graphics backend" ON)
endif()

if(REXGLUE_CORE_ONLY)
    set(REXGLUE_USE_D3D12 OFF)
    set(REXGLUE_USE_VULKAN OFF)
endif()

if(NOT REXGLUE_CORE_ONLY AND NOT REXGLUE_USE_D3D12 AND NOT REXGLUE_USE_VULKAN)
    message(FATAL_ERROR "At least one graphics backend must be enabled (REXGLUE_USE_D3D12 or REXGLUE_USE_VULKAN)")
endif()

if (REXGLUE_ENABLE_FIDELITYFX AND
    NOT (REXGLUE_USE_VULKAN OR REXGLUE_USE_D3D12))
    message(FATAL_ERROR
        "REXGLUE_ENABLE_FIDELITYFX requires at least one graphics backend")
endif()

# Root of the SDK source tree
set(REXGLUE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# The live SDK is nested under glue/ in the LibertyRecomp workspace.  Keep the
# location explicit instead of assuming that CMAKE_SOURCE_DIR is the workspace
# root; standalone SDK builds may override it when the title assets live
# elsewhere.
get_filename_component(_REXGLUE_DEFAULT_LIBERTY_WORKSPACE
    "${REXGLUE_ROOT}/../.." ABSOLUTE)
set(LIBERTY_RECOMP_WORKSPACE_ROOT "${_REXGLUE_DEFAULT_LIBERTY_WORKSPACE}"
    CACHE PATH "Root containing LibertyRecompLib and title build tools")
unset(_REXGLUE_DEFAULT_LIBERTY_WORKSPACE)

# SDK install support
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(UNIX AND NOT APPLE)
    # Keep installed binaries self-contained by resolving shared libs relative
    # to the install prefix (for example, bin -> ../lib).
    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
    list(APPEND CMAKE_INSTALL_RPATH
        "$ORIGIN"
        "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    )
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
elseif(APPLE)
    # Use loader-relative install names on macOS so installed binaries remain
    # self-contained within the install prefix.
    list(APPEND CMAKE_INSTALL_RPATH
        "@loader_path"
        "@loader_path/../${CMAKE_INSTALL_LIBDIR}"
    )
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
endif()

# Enforce the Clang family. CMake reports Apple's supported Clang toolchain as
# AppleClang even when the configured compiler executable is clang++.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
    message(FATAL_ERROR "ReXGlue requires Clang compiler. Please set CMAKE_CXX_COMPILER to clang++")
endif()

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "18.0")
    message(FATAL_ERROR "ReXGlue requires Clang 18 or newer (found ${CMAKE_CXX_COMPILER_VERSION})")
endif()

# Enforce x64 architecture
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ReXGlue only supports 64‑bit architectures (x86_64 or ARM64)")
endif()

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OBJCXX_STANDARD 23)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()
set(CMAKE_DEBUG_POSTFIX d)
set(CMAKE_RELWITHDEBINFO_POSTFIX rd)

if(NOT WIN32)
    # Use appropriate code model based on architecture
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64" AND NOT APPLE)
        # Large code model on Linux x86_64 for linking with very large recompiled executables (35MB+).
        # macOS uses small: the ld64 linker on macOS 15+ rejects text relocations produced
        # by -mcmodel=large, and macOS maps binaries in the low address space so ±2GB is sufficient.
        add_compile_options(-mcmodel=large)
    else()
        add_compile_options(-mcmodel=small)
    endif()
    # Position-independent code for all targets
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Global compile options
add_compile_options(
    -fno-strict-aliasing
    -ffp-model=strict
    $<$<CONFIG:DEBUG>:-g>
    $<$<CONFIG:DEBUG>:-O0>
    $<$<CONFIG:RELEASE>:-O3>
    $<$<CONFIG:RELEASE>:-DNDEBUG>
    -fno-char8_t
)

# Platform detection (supports AMD64 and ARM64)
set(REX_TARGET_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES REX_OSX_ARCH_COUNT)
    if(REX_OSX_ARCH_COUNT GREATER 1)
        message(FATAL_ERROR "ReXGlue macOS presets expect a single CMAKE_OSX_ARCHITECTURES value")
    endif()
    list(GET CMAKE_OSX_ARCHITECTURES 0 REX_TARGET_PROCESSOR)
endif()

if(WIN32)
    if(REX_TARGET_PROCESSOR MATCHES "AMD64|x86_64")
        set(REX_PLATFORM "win-amd64")
    elseif(REX_TARGET_PROCESSOR MATCHES "aarch64|ARM64|arm64")
        set(REX_PLATFORM "win-arm64")
    else()
        message(FATAL_ERROR "Unsupported Windows architecture: ${REX_TARGET_PROCESSOR}")
    endif()
    add_compile_definitions(REX_PLATFORM_WINDOWS=1)
elseif(UNIX AND NOT APPLE)
    if(REX_TARGET_PROCESSOR MATCHES "AMD64|x86_64")
        set(REX_PLATFORM "linux-amd64")
    elseif(REX_TARGET_PROCESSOR MATCHES "aarch64|ARM64|arm64")
        set(REX_PLATFORM "linux-arm64")
    else()
        set(REX_PLATFORM "linux-${REX_TARGET_PROCESSOR}")
    endif()
    add_compile_definitions(REX_PLATFORM_LINUX=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(REX_PLATFORM "ios-${REX_TARGET_PROCESSOR}")
    if(CMAKE_OSX_SYSROOT MATCHES "(iphonesimulator|iPhoneSimulator)")
        set(REX_PLATFORM "ios-simulator-${REX_TARGET_PROCESSOR}")
    endif()
    add_compile_definitions(REX_PLATFORM_IOS=1)
elseif(APPLE)
    if(REX_TARGET_PROCESSOR MATCHES "AMD64|x86_64")
        set(REX_PLATFORM "mac-amd64")
    elseif(REX_TARGET_PROCESSOR MATCHES "aarch64|ARM64|arm64")
        set(REX_PLATFORM "mac-arm64")
    else()
        set(REX_PLATFORM "mac-${REX_TARGET_PROCESSOR}")
    endif()
    add_compile_definitions(REX_PLATFORM_MAC=1)
else()
    message(FATAL_ERROR "Unsupported platform. ReXGlue supports Windows, Linux, and macOS only.")
endif()

# Build config as compile definition (multi-config generators don't know config at configure time)
add_compile_definitions(REXGLUE_BUILD_CONFIG="$<CONFIG>")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rex_version.cmake)
rex_resolve_version(REXGLUE_FULL_VERSION
    FLOOR_MAJOR ${PROJECT_VERSION_MAJOR}
    FLOOR_MINOR ${PROJECT_VERSION_MINOR})

# Strip the -id trailer (if any) for the numeric-only version.
string(REGEX REPLACE "-.*$" "" REXGLUE_NUMERIC_VERSION "${REXGLUE_FULL_VERSION}")

# Parse up to 4 numeric components for consumers that want integer access.
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?(\\.([0-9]+))?" _ "${REXGLUE_NUMERIC_VERSION}")
set(REXGLUE_DERIVED_MAJOR "${CMAKE_MATCH_1}")
set(REXGLUE_DERIVED_MINOR "${CMAKE_MATCH_2}")
if(CMAKE_MATCH_4)
    set(REXGLUE_DERIVED_PATCH "${CMAKE_MATCH_4}")
else()
    set(REXGLUE_DERIVED_PATCH "0")
endif()
if(CMAKE_MATCH_6)
    set(REXGLUE_DERIVED_TWEAK "${CMAKE_MATCH_6}")
else()
    set(REXGLUE_DERIVED_TWEAK "0")
endif()

set(REXGLUE_FLOOR_VERSION
    "${REXGLUE_DERIVED_MAJOR}.${REXGLUE_DERIVED_MINOR}.${REXGLUE_DERIVED_PATCH}")

# Generate version header -- must run after REX_PLATFORM is set
# Timestamp is set at configure time -- intentionally not updated on every rebuild
string(TIMESTAMP REXGLUE_BUILD_TIMESTAMP "%Y%m%d_%H%M")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rex/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/rex/version.h
    @ONLY
)

# Output directories (multi-config appends /<Config>/ automatically)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")

# Sanitizer support for debugging memory corruption and undefined behavior
if(REXGLUE_ENABLE_SANITIZERS)
    message(STATUS "Enabling UndefinedBehaviorSanitizer (UBSan only - ASan conflicts with custom memory mapping)")
    # Note: ASan doesn't work well with the custom mmap at 0x100000000, so we only use UBSan
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=undefined)
endif()

if(REXGLUE_BUILD_TESTS)
    list(APPEND CMAKE_MODULE_PATH "${REXGLUE_ROOT}/cmake")
    list(APPEND CMAKE_MODULE_PATH "${REXGLUE_ROOT}/thirdparty/catch2/extras")
endif()

# Third-party dependencies (vendored sources only)
add_subdirectory(thirdparty)

# Enable warnings after thirdparty
add_compile_options(
    -Wall
    -Wextra
)

include_directories(${REXGLUE_ROOT}/include)

add_subdirectory(src/core)
if(REXGLUE_CORE_ONLY)
    if(REXGLUE_RUNTIME_ONLY)
        add_subdirectory(src/filesystem)
        add_subdirectory(src/system)
        if(REXGLUE_HEADLESS_KERNEL)
            add_subdirectory(src/kernel)
        endif()
    endif()
    message(STATUS "ReXGlue minimal: ${REX_PLATFORM}; system runtime=${REXGLUE_RUNTIME_ONLY}; no renderer or host tools")
    return()
endif()
add_subdirectory(src/filesystem)
add_subdirectory(src/ui)
add_subdirectory(src/input)
add_subdirectory(src/audio)
add_subdirectory(src/graphics)
add_subdirectory(src/system)
add_subdirectory(src/kernel)
add_subdirectory(src/codegen)
add_subdirectory(src/rexglue)

# The GTA IV title is kept as an optional SDK consumer so normal RexGlue builds
# stay lightweight while the live application can still be configured entirely
# from this source tree.  gta4-recomp uses rexglue_configure_target(), which is
# normally exported only when RexGlue is consumed by another project.
if(REXGLUE_BUILD_GTA4_RECOMP)
    if(NOT DEFINED LIBERTY_RECOMP_TARGET_PLATFORM)
        set(LIBERTY_RECOMP_TARGET_PLATFORM "desktop" CACHE STRING
            "Liberty Recompiled target platform")
    endif()
    set(REXGLUE_SHARE_DIR "${REXGLUE_ROOT}/src/ui")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rexglue_helpers.cmake)
    add_subdirectory(gta4-recomp)
endif()

# Add tests subdirectory if tests are enabled
if(REXGLUE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "===========================================")
message(STATUS "ReXGlue Configuration Summary:")
message(STATUS "  Version: ${REXGLUE_FULL_VERSION}")
message(STATUS "  Platform: ${REX_PLATFORM}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Configurations: ${CMAKE_CONFIGURATION_TYPES}")
message(STATUS "  Output Dir: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Graphics: D3D12=${REXGLUE_USE_D3D12} Vulkan=${REXGLUE_USE_VULKAN}")
message(STATUS "  FidelityFX: ${REXGLUE_ENABLE_FIDELITYFX}")
message(STATUS "  Tracy: ${REXGLUE_ENABLE_TRACY}")
message(STATUS "  Perf Counters: ${REXGLUE_ENABLE_PERF_COUNTERS}")
message(STATUS "===========================================")

# SDK install/export rules (skipped when consumed as a subdirectory)
if(PROJECT_IS_TOP_LEVEL)
    include(cmake/rexglue_devenv.cmake)
    include(cmake/rexglue_install.cmake)
else()
    # When consumed via add_subdirectory(), provide the same interface as find_package().
    # This sets REXGLUE_SHARE_DIR and makes rexglue_configure_target() available.
    set(REXGLUE_SHARE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/ui" PARENT_SCOPE)
    set(REXGLUE_SHARE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rexglue_helpers.cmake)
endif()
