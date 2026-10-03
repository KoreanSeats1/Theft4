cmake_minimum_required(VERSION 3.24)
project(theft4_direct_metal LANGUAGES C CXX OBJCXX ASM)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ROOT "../.." ABSOLUTE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/thirdparty/MoltenVK/SPIRV-Cross" spirv-cross)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/tools/XenosRecomp/thirdparty/zstd/build/cmake" zstd)
add_library(metal_shader_corpus OBJECT
  "${ROOT}/LibertyRecompLib/shader/shader_cache.cpp"
  "${ROOT}/tools/XenosRecomp/thirdparty/smol-v/source/smolv.cpp")
target_include_directories(metal_shader_corpus PRIVATE
  "${ROOT}/LibertyRecompLib/shader" "${ROOT}/tools/XenosRecomp/thirdparty/smol-v/source")
add_executable(metal_shader_export metal_shader_export.cpp shader_contract.cpp shader_descriptor_contract.cpp
  $<TARGET_OBJECTS:metal_shader_corpus>)
target_include_directories(metal_shader_export PRIVATE
  "${ROOT}/LibertyRecompLib/shader"
  "${ROOT}/tools/XenosRecomp/thirdparty/smol-v/source"
  "${ROOT}/tools/XenosRecomp/thirdparty/zstd/lib"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native"
  "${ROOT}/glue/rexglue-sdk-main/thirdparty/spirv-headers/include")
target_link_libraries(metal_shader_export PRIVATE spirv-cross-msl spirv-cross-glsl
  spirv-cross-core libzstd_static)
add_executable(metal_descriptor_contract_test descriptor_contract_test.cpp shader_descriptor_contract.cpp
  $<TARGET_OBJECTS:metal_shader_corpus>)
target_include_directories(metal_descriptor_contract_test PRIVATE
  "${ROOT}/LibertyRecompLib/shader" "${ROOT}/tools/XenosRecomp/thirdparty/smol-v/source"
  "${ROOT}/tools/XenosRecomp/thirdparty/zstd/lib")
target_link_libraries(metal_descriptor_contract_test PRIVATE spirv-cross-core libzstd_static)
enable_testing()
add_test(NAME metal_descriptor_contract COMMAND metal_descriptor_contract_test)
add_library(metal_shader_catalog "${ROOT}/ios/bridge/theft4_metal_shader_catalog.cpp")
target_include_directories(metal_shader_catalog PUBLIC "${ROOT}/ios/bridge")
add_executable(metal_shader_catalog_test shader_catalog_test.cpp $<TARGET_OBJECTS:metal_shader_corpus>)
target_include_directories(metal_shader_catalog_test PRIVATE "${ROOT}/LibertyRecompLib/shader"
  "${ROOT}/tools/XenosRecomp/thirdparty/smol-v/source" "${ROOT}/tools/XenosRecomp/thirdparty/zstd/lib")
target_link_libraries(metal_shader_catalog_test PRIVATE metal_shader_catalog spirv-cross-core libzstd_static)
set(METAL_CATALOG_MANIFEST "" CACHE FILEPATH "Exported full stock Metal manifest to validate")
if(METAL_CATALOG_MANIFEST)
  add_test(NAME metal_shader_catalog COMMAND metal_shader_catalog_test "${METAL_CATALOG_MANIFEST}")
endif()
add_executable(metal_probe probe_main.mm "${ROOT}/ios/tests/direct_metal_probe.mm"
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_shader_store.mm"
  "${ROOT}/ios/bridge/theft4_metal_resources.mm")
target_include_directories(metal_probe PRIVATE "${ROOT}/ios/bridge" "${ROOT}/ios/tests"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native")
target_compile_options(metal_probe PRIVATE -fobjc-arc)
set(METAL_SOURCE_REVISION "development" CACHE STRING "Source commit represented by the GPU validation")
target_compile_definitions(metal_probe PRIVATE "THEFT4_METAL_SOURCE_REVISION=\"${METAL_SOURCE_REVISION}\"")
target_link_libraries(metal_probe PRIVATE metal_shader_catalog "-framework Foundation" "-framework Metal" "-framework QuartzCore")
