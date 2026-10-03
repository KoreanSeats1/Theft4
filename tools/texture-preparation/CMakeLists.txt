cmake_minimum_required(VERSION 3.24)
project(theft4_texture_tools LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ROOT "../.." ABSOLUTE)
set(SDK "${ROOT}/glue/rexglue-sdk-main")
set(ASTCENC_CLI OFF CACHE BOOL "" FORCE)
set(ASTCENC_UNITTEST OFF CACHE BOOL "" FORCE)
set(ASTCENC_ISA_NEON ON CACHE BOOL "" FORCE)
set(ASTCENC_UNIVERSAL_BUILD OFF CACHE BOOL "" FORCE)
set(ASTCENC_WERROR OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/thirdparty/astc-encoder" astc)
add_library(texture_sources STATIC
  "${ROOT}/ios/bridge/theft4_texture_manifest.cpp"
  "${ROOT}/ios/bridge/theft4_astc_texture.cpp"
  "${SDK}/src/graphics/pipeline/texture/info.cpp"
  "${SDK}/src/graphics/pipeline/texture/info_formats.cpp"
  "${SDK}/src/graphics/pipeline/texture/extent.cpp"
  "${SDK}/src/graphics/pipeline/texture/util.cpp"
  "${SDK}/src/graphics/pipeline/texture/conversion.cpp"
  "${SDK}/src/system/lzx.cpp"
  "${SDK}/src/core/math_gcc.cpp"
  host_memory.cpp
  "${SDK}/thirdparty/libmspack/libmspack/mspack/lzxd.c"
  "${SDK}/thirdparty/crypto/rijndael-alg-fst.c")
target_include_directories(texture_sources PUBLIC
  "${ROOT}/ios/bridge" "${SDK}/include" "${ROOT}/thirdparty/json/include"
  "${SDK}/thirdparty/xxHash" "${SDK}/thirdparty/fmt/include"
  "${SDK}/thirdparty/spdlog/include" "${SDK}/thirdparty/crypto" "${SDK}/thirdparty/simde"
  "${SDK}/thirdparty/libmspack/libmspack/mspack")
target_compile_definitions(texture_sources PUBLIC NDEBUG XXH_INLINE_ALL FMT_HEADER_ONLY)
target_compile_options(texture_sources PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(texture_sources PUBLIC astcenc-neon-static)
add_executable(texture_manifest main.cpp)
target_link_libraries(texture_manifest PRIVATE texture_sources)
target_link_options(texture_manifest PRIVATE -Wl,-dead_strip)
add_executable(astc_texture_test "${ROOT}/tests/ios/theft4_astc_texture_test.cpp")
target_link_libraries(astc_texture_test PRIVATE texture_sources)
target_link_options(astc_texture_test PRIVATE -Wl,-dead_strip)
add_executable(texture_manifest_test "${ROOT}/tests/ios/theft4_texture_manifest_test.cpp")
target_link_libraries(texture_manifest_test PRIVATE texture_sources)
target_link_options(texture_manifest_test PRIVATE -Wl,-dead_strip)
add_executable(bc_compatibility_test "${ROOT}/tests/ios/theft4_bc_compatibility_test.cpp")
