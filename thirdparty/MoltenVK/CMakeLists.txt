# Simple CMake script to compile MoltenVK with SPIRV-Cross as a shared library target

# Prepare MoltenVK Git revision
find_package(Git)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            OUTPUT_VARIABLE MVK_GIT_REV
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
set(MVK_GENERATED_INCLUDES ${CMAKE_CURRENT_BINARY_DIR}/Generated)
file(WRITE ${MVK_GENERATED_INCLUDES}/mvkGitRevDerived.h "static const char* mvkRevString = \"${MVK_GIT_REV}\";")
message(STATUS "MoltenVK revision: ${MVK_GIT_REV}")

# Prepare MoltenVK version
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK/MoltenVK/MoltenVK/API/mvk_private_api.h MVK_PRIVATE_API)
string(REGEX MATCH "#define MVK_VERSION_MAJOR   [0-9]+" MVK_VERSION_MAJOR_LINE "${MVK_PRIVATE_API}")
string(REGEX MATCH "[0-9]+" MVK_VERSION_MAJOR "${MVK_VERSION_MAJOR_LINE}")
string(REGEX MATCH "#define MVK_VERSION_MINOR   [0-9]+" MVK_VERSION_MINOR_LINE "${MVK_PRIVATE_API}")
string(REGEX MATCH "[0-9]+" MVK_VERSION_MINOR "${MVK_VERSION_MINOR_LINE}")
string(REGEX MATCH "#define MVK_VERSION_PATCH   [0-9]+" MVK_VERSION_PATCH_LINE "${MVK_PRIVATE_API}")
string(REGEX MATCH "[0-9]+" MVK_VERSION_PATCH "${MVK_VERSION_PATCH_LINE}")
set(MVK_VERSION "${MVK_VERSION_MAJOR}.${MVK_VERSION_MINOR}.${MVK_VERSION_PATCH}")
message(STATUS "MoltenVK version: ${MVK_VERSION}")

# Find required system libraries
find_library(APPKIT_LIBRARY AppKit REQUIRED)
find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
find_library(IOKIT_LIBRARY IOKit REQUIRED)
find_library(IOSURFACE_LIBRARY IOSurface REQUIRED)
find_library(METAL_LIBRARY Metal REQUIRED)
find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)

# SPIRV-Cross
option(SPIRV_CROSS_CLI "" OFF)
option(SPIRV_CROSS_ENABLE_TESTS "" OFF)
option(SPIRV_CROSS_ENABLE_HLSL "" OFF)
option(SPIRV_CROSS_ENABLE_CPP "" OFF)
option(SPIRV_CROSS_SKIP_INSTALL "" ON)
add_subdirectory(SPIRV-Cross)

# Use the same header-only Cereal revision as this MoltenVK pin. Without it,
# vkGetPipelineCacheData cannot serialize the application's compiled MSL cache.
if(NOT TARGET cereal::cereal)
    include(FetchContent)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK/ExternalRevisions/cereal_repo_revision"
        MVK_CEREAL_REVISION LIMIT_COUNT 1)
    FetchContent_Declare(mvk_cereal
        GIT_REPOSITORY https://github.com/USCiLab/cereal.git
        GIT_TAG "${MVK_CEREAL_REVISION}"
        # Populate only the headers; do not configure Cereal's tests or tools.
        SOURCE_SUBDIR include)
    FetchContent_MakeAvailable(mvk_cereal)
    add_library(mvk_cereal_headers INTERFACE)
    target_include_directories(mvk_cereal_headers SYSTEM INTERFACE "${mvk_cereal_SOURCE_DIR}/include")
    add_library(cereal::cereal ALIAS mvk_cereal_headers)
endif()

# Common
set(MVK_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK/Common)
file(GLOB_RECURSE MVK_COMMON_SOURCES CONFIGURE_DEPENDS
    ${MVK_COMMON_DIR}/*.cpp
    ${MVK_COMMON_DIR}/*.m
    ${MVK_COMMON_DIR}/*.mm)
set(MVK_COMMON_INCLUDES ${MVK_COMMON_DIR})

add_library(MoltenVKCommon STATIC ${MVK_COMMON_SOURCES})
target_include_directories(MoltenVKCommon PUBLIC ${MVK_COMMON_INCLUDES})
target_compile_options(MoltenVKCommon PRIVATE -w)

# MoltenVKShaderConverter
set(MVK_SHADER_CONVERTER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK/MoltenVKShaderConverter)
file(GLOB_RECURSE MVK_SHADER_CONVERTER_SOURCES CONFIGURE_DEPENDS
    ${MVK_SHADER_CONVERTER_DIR}/MoltenVKShaderConverter/*.cpp
    ${MVK_SHADER_CONVERTER_DIR}/MoltenVKShaderConverter/*.m
    ${MVK_SHADER_CONVERTER_DIR}/MoltenVKShaderConverter/*.mm)
set(MVK_SHADER_CONVERTER_INCLUDES ${MVK_SHADER_CONVERTER_DIR} ${MVK_SHADER_CONVERTER_DIR}/include)

add_library(MoltenVKShaderConverter STATIC ${MVK_SHADER_CONVERTER_SOURCES})
target_include_directories(MoltenVKShaderConverter PUBLIC ${MVK_SHADER_CONVERTER_INCLUDES})
target_compile_options(MoltenVKShaderConverter PRIVATE -w)
target_link_libraries(MoltenVKShaderConverter PRIVATE spirv-cross-msl spirv-cross-reflect MoltenVKCommon)
target_compile_definitions(MoltenVKShaderConverter PRIVATE MVK_EXCLUDE_SPIRV_TOOLS=1)

# MoltenVK
set(MVK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK/MoltenVK)
file(GLOB_RECURSE MVK_SOURCES CONFIGURE_DEPENDS
    ${MVK_DIR}/MoltenVK/*.cpp
    ${MVK_DIR}/MoltenVK/*.m
    ${MVK_DIR}/MoltenVK/*.mm)
file(GLOB MVK_SRC_INCLUDES LIST_DIRECTORIES ON ${MVK_DIR}/MoltenVK/*)
set(MVK_INCLUDES
    ${MVK_SRC_INCLUDES} ${MVK_GENERATED_INCLUDES} ${MVK_DIR}/include
    ${LIBERTY_RECOMP_THIRDPARTY_ROOT}/plume/contrib/Vulkan-Headers/include)

add_library(MoltenVK SHARED ${MVK_SOURCES})
target_include_directories(MoltenVK PRIVATE ${MVK_INCLUDES})
target_compile_options(MoltenVK PRIVATE -w -Wno-elaborated-enum-base)
# Match the selected LLVM installation on both Intel and Apple Silicon Macs.
get_filename_component(_mvk_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_mvk_compiler_prefix "${_mvk_compiler_bin}" DIRECTORY)
set(_mvk_llvm_cxx "${_mvk_compiler_prefix}/lib/c++")
if(IS_DIRECTORY "${_mvk_llvm_cxx}")
    target_link_options(MoltenVK PRIVATE "-L${_mvk_llvm_cxx}" "-Wl,-rpath,${_mvk_llvm_cxx}" -lc++)
endif()
target_link_libraries(MoltenVK PRIVATE
    ${APPKIT_LIBRARY} ${FOUNDATION_LIBRARY} ${IOKIT_LIBRARY} ${IOSURFACE_LIBRARY} ${METAL_LIBRARY} ${QUARTZCORE_LIBRARY}
    spirv-cross-msl MoltenVKCommon MoltenVKShaderConverter cereal::cereal)
target_compile_definitions(MoltenVK PRIVATE MVK_FRAMEWORK_VERSION=${MVK_VERSION} MVK_USE_CEREAL=1)

# Opt-in ownership regression test. It exercises the actual retention helper
# with completion-handler test doubles, without launching the game or using a GPU.
add_executable(MoltenVKBufferRetentionTest EXCLUDE_FROM_ALL
    tests/mtl_buffer_retention_test.mm
    ${MVK_DIR}/MoltenVK/Commands/MVKMTLBufferRetention.mm)
target_include_directories(MoltenVKBufferRetentionTest PRIVATE ${MVK_DIR}/MoltenVK/Commands)
target_compile_options(MoltenVKBufferRetentionTest PRIVATE -fno-objc-arc -fblocks -Wall -Wextra -Werror)
if(IS_DIRECTORY "${_mvk_llvm_cxx}")
    target_link_options(MoltenVKBufferRetentionTest PRIVATE
        "-L${_mvk_llvm_cxx}" "-Wl,-rpath,${_mvk_llvm_cxx}" -lc++)
endif()
target_link_libraries(MoltenVKBufferRetentionTest PRIVATE ${FOUNDATION_LIBRARY} ${METAL_LIBRARY})
