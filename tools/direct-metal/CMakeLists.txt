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
add_executable(metal_host_shader_export host_shader_export.cpp)
target_include_directories(metal_host_shader_export PRIVATE "${ROOT}/thirdparty/json/include"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native")
target_link_libraries(metal_host_shader_export PRIVATE spirv-cross-msl spirv-cross-glsl spirv-cross-core)
add_executable(metal_descriptor_contract_test descriptor_contract_test.cpp shader_descriptor_contract.cpp
  $<TARGET_OBJECTS:metal_shader_corpus>)
target_include_directories(metal_descriptor_contract_test PRIVATE
  "${ROOT}/LibertyRecompLib/shader" "${ROOT}/tools/XenosRecomp/thirdparty/smol-v/source"
  "${ROOT}/tools/XenosRecomp/thirdparty/zstd/lib")
target_link_libraries(metal_descriptor_contract_test PRIVATE spirv-cross-core libzstd_static)
enable_testing()
add_executable(shader_realization_test shader_realization_test.cpp)
target_include_directories(shader_realization_test PRIVATE "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native")
add_test(NAME shader_realization_contract COMMAND shader_realization_test)
add_test(NAME metal_descriptor_contract COMMAND metal_descriptor_contract_test)
add_library(metal_shader_catalog "${ROOT}/ios/bridge/theft4_metal_shader_catalog.cpp")
add_library(render_plan "${ROOT}/ios/bridge/theft4_render_plan.cpp" "${ROOT}/ios/bridge/theft4_frame_plan.cpp" "${ROOT}/ios/bridge/theft4_postfx_plan.cpp")
target_include_directories(render_plan PUBLIC "${ROOT}/ios/bridge" PRIVATE "${ROOT}/thirdparty/json/include")
add_executable(render_plan_test render_plan_test.cpp
  "${ROOT}/ios/bridge/theft4_render_plan_source.cpp" "${ROOT}/ios/bridge/theft4_draw_capture.cpp")
target_include_directories(render_plan_test PRIVATE "${ROOT}/thirdparty/json/include"
  "${ROOT}/glue/rexglue-sdk-main/include"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native"
  "${ROOT}/glue/rexglue-sdk-main/thirdparty/vulkan-headers/include")
target_link_libraries(render_plan_test PRIVATE render_plan)
add_test(NAME render_plan_contract COMMAND render_plan_test "${CMAKE_BINARY_DIR}/private-plan-test")
add_executable(frame_plan_test frame_plan_test.cpp)
target_link_libraries(frame_plan_test PRIVATE render_plan)
target_compile_options(frame_plan_test PRIVATE -UNDEBUG)
add_test(NAME frame_plan_contract COMMAND frame_plan_test)
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
  "${ROOT}/ios/tests/direct_metal_capture.mm"
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_shader_store.mm"
  "${ROOT}/ios/bridge/theft4_metal_resources.mm" "${ROOT}/ios/bridge/theft4_metal_plan.mm"
  "${ROOT}/ios/bridge/theft4_metal_frame.mm" "${ROOT}/ios/bridge/theft4_metal_host_shaders.mm")
target_include_directories(metal_probe PRIVATE "${ROOT}/ios/bridge" "${ROOT}/ios/tests"
  "${ROOT}/thirdparty/json/include"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native")
target_compile_options(metal_probe PRIVATE -fobjc-arc)
set(METAL_SOURCE_REVISION "development" CACHE STRING "Source commit represented by the GPU validation")
target_compile_definitions(metal_probe PRIVATE "THEFT4_METAL_SOURCE_REVISION=\"${METAL_SOURCE_REVISION}\"")
target_link_libraries(metal_probe PRIVATE metal_shader_catalog render_plan "-framework Foundation" "-framework Metal"
  "-framework QuartzCore" "-framework CoreGraphics" "-framework ImageIO")

# Exercise the live worker backend, independently from isolated draw replays.
add_executable(metal_backend_worker_test backend_worker_test.mm
  "${ROOT}/ios/bridge/theft4_metal_backend.mm"
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_shader_store.mm"
  "${ROOT}/ios/bridge/theft4_metal_resources.mm" "${ROOT}/ios/bridge/theft4_metal_plan.mm"
  "${ROOT}/ios/bridge/theft4_metal_frame.mm" "${ROOT}/ios/bridge/theft4_metal_host_shaders.mm")
target_include_directories(metal_backend_worker_test PRIVATE "${ROOT}/ios/bridge"
  "${ROOT}/thirdparty/json/include")
target_compile_options(metal_backend_worker_test PRIVATE -fobjc-arc -UNDEBUG)
target_link_libraries(metal_backend_worker_test PRIVATE metal_shader_catalog render_plan
  "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_executable(primitive_expansion_test primitive_expansion_test.cpp "${ROOT}/ios/bridge/theft4_primitive_expansion.cpp")
target_include_directories(primitive_expansion_test PRIVATE "${ROOT}/ios/bridge")
target_compile_options(primitive_expansion_test PRIVATE -UNDEBUG)
add_test(NAME guest_primitive_expansion COMMAND primitive_expansion_test)
add_executable(color_readback_test color_readback_test.cpp)
target_include_directories(color_readback_test PRIVATE "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native")
target_compile_options(color_readback_test PRIVATE -UNDEBUG)
add_test(NAME guest_color_readback COMMAND color_readback_test)

add_executable(virtual_resource_journal_test virtual_resource_journal_test.cpp)
target_include_directories(virtual_resource_journal_test PRIVATE
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native"
  "${ROOT}/glue/rexglue-sdk-main/include")
target_compile_options(virtual_resource_journal_test PRIVATE -UNDEBUG)
add_test(NAME virtual_resource_journal COMMAND virtual_resource_journal_test)

add_executable(metal_frame_continuity_test metal_frame_continuity_test.cpp)
target_include_directories(metal_frame_continuity_test PRIVATE
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native"
  "${ROOT}/glue/rexglue-sdk-main/include"
  "${ROOT}/glue/rexglue-sdk-main/thirdparty/vulkan-headers/include")
target_compile_options(metal_frame_continuity_test PRIVATE -UNDEBUG)
add_test(NAME metal_frame_continuity COMMAND metal_frame_continuity_test)

add_executable(metal_constant_projection_test metal_constant_projection_test.cpp)
add_executable(constant_projection_test "${ROOT}/ios/tests/constant_projection_test.cpp")
foreach(test_target metal_constant_projection_test constant_projection_test)
  target_include_directories(${test_target} PRIVATE
    "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native" "${ROOT}/glue/rexglue-sdk-main/include")
  target_compile_options(${test_target} PRIVATE -UNDEBUG)
endforeach()
add_test(NAME metal_constant_projection COMMAND metal_constant_projection_test)
add_test(NAME constant_projection_reflection COMMAND constant_projection_test)
add_executable(constant_projection_corpus "${ROOT}/ios/tests/constant_projection_corpus.cpp"
  $<TARGET_OBJECTS:metal_shader_corpus>)
target_include_directories(constant_projection_corpus PRIVATE
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native" "${ROOT}/glue/rexglue-sdk-main/include"
  "${ROOT}/LibertyRecompLib/shader" "${ROOT}/tools/XenosRecomp/thirdparty/smol-v/source"
  "${ROOT}/tools/XenosRecomp/thirdparty/zstd/lib"
  "${ROOT}/glue/rexglue-sdk-main/thirdparty/spirv-headers/include")
target_compile_options(constant_projection_corpus PRIVATE -UNDEBUG)
target_link_libraries(constant_projection_corpus PRIVATE libzstd_static)
add_test(NAME constant_projection_corpus COMMAND constant_projection_corpus)

# Repeatable synthetic admission/submission comparison; requires a real Metal device.
add_executable(metal_upload_stress_test upload_stress_test.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_resources.mm")
target_include_directories(metal_upload_stress_test PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_upload_stress_test PRIVATE -fobjc-arc)
target_compile_definitions(metal_upload_stress_test PRIVATE THEFT4_PACKED_UPLOAD_STRESS=1)
target_link_libraries(metal_upload_stress_test PRIVATE "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_executable(metal_performance_benchmark performance_benchmark.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_shader_store.mm"
  "${ROOT}/ios/bridge/theft4_metal_resources.mm" "${ROOT}/ios/bridge/theft4_metal_plan.mm"
  "${ROOT}/ios/bridge/theft4_metal_frame.mm" "${ROOT}/ios/bridge/theft4_metal_host_shaders.mm")
target_include_directories(metal_performance_benchmark PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_performance_benchmark PRIVATE -fobjc-arc)
target_compile_definitions(metal_performance_benchmark PRIVATE THEFT4_PERF_OPTIMIZED=1)
target_compile_definitions(metal_performance_benchmark PRIVATE THEFT4_PACKED_UPLOAD_STATS=1)
target_link_libraries(metal_performance_benchmark PRIVATE metal_shader_catalog render_plan
  "-framework Foundation" "-framework Metal" "-framework QuartzCore")
