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
  "${ROOT}/glue/rexglue-sdk-main/include"
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
# Compile the production Darwin wait implementation, including its real
# event/semaphore transitions, in isolation from the game runtime.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${ROOT}/glue/rexglue-sdk-main/src/core/threading_mac.cpp")
file(READ "${ROOT}/glue/rexglue-sdk-main/src/core/threading_mac.cpp" wait_source)
string(FIND "${wait_source}" "struct MultiWaitWakeState {" wait_begin)
string(FIND "${wait_source}" "template <>\nclass PosixCondition<Mutant>" wait_end)
math(EXPR wait_length "${wait_end}-${wait_begin}")
string(SUBSTRING "${wait_source}" ${wait_begin} ${wait_length} wait_body)
file(WRITE "${CMAKE_BINARY_DIR}/darwin-wait-production.inc" "${wait_body}")
add_executable(darwin_wait_poll_test darwin_wait_poll_test.cpp)
target_include_directories(darwin_wait_poll_test PRIVATE "${CMAKE_BINARY_DIR}")
target_compile_options(darwin_wait_poll_test PRIVATE -UNDEBUG)
add_test(NAME darwin_wait_poll COMMAND darwin_wait_poll_test)
add_executable(geometry_retirement_test geometry_retirement_test.cpp)
target_include_directories(geometry_retirement_test PRIVATE "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native")
target_compile_options(geometry_retirement_test PRIVATE -UNDEBUG)
add_test(NAME native_geometry_retirement COMMAND geometry_retirement_test)
add_executable(lab_resolution_policy_test "${ROOT}/ios/tests/lab_resolution_policy_test.cpp")
target_include_directories(lab_resolution_policy_test PRIVATE "${ROOT}/ios/bridge"
  "${ROOT}/glue/rexglue-sdk-main/gta4-recomp/src")
target_compile_options(lab_resolution_policy_test PRIVATE -UNDEBUG)
add_test(NAME lab_resolution_policy COMMAND lab_resolution_policy_test)
# Validate the rewritten selector against the actual generated PowerPC body,
# rather than another hand-written description of the selection rules.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${ROOT}/glue/rexglue-sdk-main/gta4-recomp/generated/gta4_recomp.32.cpp"
  "${ROOT}/glue/rexglue-sdk-main/gta4-recomp/src/gta4_native_hooks.cpp")
file(READ "${ROOT}/glue/rexglue-sdk-main/gta4-recomp/generated/gta4_recomp.32.cpp" lod_title)
string(FIND "${lod_title}" "DEFINE_REX_FUNC(sub_824F3418) {" lod_begin)
string(FIND "${lod_title}" "DEFINE_REX_FUNC(sub_824F34F0) {" lod_end)
math(EXPR lod_length "${lod_end}-${lod_begin}")
string(SUBSTRING "${lod_title}" ${lod_begin} ${lod_length} lod_body)
file(WRITE "${CMAKE_BINARY_DIR}/lod-title-reference.inc" "${lod_body}")
file(READ "${ROOT}/glue/rexglue-sdk-main/gta4-recomp/src/gta4_native_hooks.cpp" lod_hooks)
string(FIND "${lod_hooks}" "extern \"C\" void sub_824F3418(" lod_begin)
string(FIND "${lod_hooks}" "extern \"C\" void sub_821DFFE8(" lod_end)
math(EXPR lod_length "${lod_end}-${lod_begin}")
string(SUBSTRING "${lod_hooks}" ${lod_begin} ${lod_length} lod_body)
file(WRITE "${CMAKE_BINARY_DIR}/lod-native-hook.inc" "${lod_body}")
add_executable(native_lod_selection_test lod_selection_test.cpp)
target_include_directories(native_lod_selection_test PRIVATE "${CMAKE_BINARY_DIR}"
  "${ROOT}/glue/rexglue-sdk-main/gta4-recomp/src"
  "${ROOT}/glue/rexglue-sdk-main/include" "${ROOT}/glue/rexglue-sdk-main/thirdparty/simde")
target_compile_options(native_lod_selection_test PRIVATE -UNDEBUG)
add_test(NAME native_lod_selection COMMAND native_lod_selection_test)
# Differentially check the exact live shared-bank writer and semantic key,
# including every key field and immutable environment ownership transition.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native/metal_draw_frontend.inc")
file(READ "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp" shared_source)
string(FIND "${shared_source}" "struct NativeSharedConstants {" shared_begin)
string(FIND "${shared_source}" "struct NativeDrawDescriptorKey {" shared_end)
math(EXPR shared_length "${shared_end}-${shared_begin}")
string(SUBSTRING "${shared_source}" ${shared_begin} ${shared_length} shared_body)
file(WRITE "${CMAKE_BINARY_DIR}/metal-shared-abi.inc" "${shared_body}")
string(FIND "${shared_source}" "float MotionBlurTimeScale(" shared_begin)
string(FIND "${shared_source}" "VkPrimitiveTopology ConvertPrimitiveTopology(" shared_end)
math(EXPR shared_length "${shared_end}-${shared_begin}")
string(SUBSTRING "${shared_source}" ${shared_begin} ${shared_length} shared_body)
file(APPEND "${CMAKE_BINARY_DIR}/metal-shared-abi.inc" "${shared_body}")
file(READ "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native/metal_draw_frontend.inc" shared_source)
string(FIND "${shared_source}" "  NativeMetalSharedConstantKey shared_semantics;" shared_begin)
string(FIND "${shared_source}" "  auto shared_bytes=native_metal_frame_->shared_constants.Find" shared_end)
math(EXPR shared_length "${shared_end}-${shared_begin}")
string(SUBSTRING "${shared_source}" ${shared_begin} ${shared_length} shared_body)
file(WRITE "${CMAKE_BINARY_DIR}/metal-shared-key.inc" "${shared_body}return shared_semantics;\n")
string(FIND "${shared_source}" "    NativeSharedConstants shared;" shared_begin)
string(FIND "${shared_source}" "    const auto shared_data=" shared_end)
math(EXPR shared_length "${shared_end}-${shared_begin}")
string(SUBSTRING "${shared_source}" ${shared_begin} ${shared_length} shared_body)
file(WRITE "${CMAKE_BINARY_DIR}/metal-shared-writer.inc" "${shared_body}return shared;\n")
add_executable(metal_shared_constant_test metal_shared_constant_test.cpp)
target_include_directories(metal_shared_constant_test PRIVATE "${CMAKE_BINARY_DIR}"
  "${ROOT}/glue/rexglue-sdk-main/thirdparty/xxHash"
  "${ROOT}/glue/rexglue-sdk-main/include"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native")
target_compile_options(metal_shared_constant_test PRIVATE -UNDEBUG)
add_test(NAME metal_shared_constant_contract COMMAND metal_shared_constant_test)
# Exercise the live converter and its live two-entry CPU cache. Reference
# conversion uses each original shader/offset; sharing must preserve every
# byte reachable through that draw's vertex view.
file(READ "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp" vertex_source)
string(FIND "${vertex_source}" "uint32_t ConvertVertexUsageToLocation(" vertex_begin)
string(FIND "${vertex_source}" "VkFormat ConvertVertexElementFormat(" vertex_end)
math(EXPR vertex_length "${vertex_end}-${vertex_begin}")
string(SUBSTRING "${vertex_source}" ${vertex_begin} ${vertex_length} vertex_body)
file(WRITE "${CMAKE_BINARY_DIR}/metal-vertex-converter.inc" "${vertex_body}")
string(FIND "${vertex_source}" "void CopyGuestWordsToHost(" vertex_begin)
string(FIND "${vertex_source}" "uint32_t LoadGuestWord(" vertex_end)
math(EXPR vertex_length "${vertex_end}-${vertex_begin}")
string(SUBSTRING "${vertex_source}" ${vertex_begin} ${vertex_length} vertex_body)
file(APPEND "${CMAKE_BINARY_DIR}/metal-vertex-converter.inc" "${vertex_body}")
string(FIND "${vertex_source}" "const std::vector<uint8_t>&\nGta4NativeGraphicsSystem::NativeBufferResource::ConvertedVertexPayload::Data()" vertex_begin)
string(FIND "${vertex_source}" "bool Gta4NativeGraphicsSystem::UploadBufferResource(" vertex_end)
math(EXPR vertex_length "${vertex_end}-${vertex_begin}")
string(SUBSTRING "${vertex_source}" ${vertex_begin} ${vertex_length} vertex_body)
file(WRITE "${CMAKE_BINARY_DIR}/metal-vertex-cache.inc" "${vertex_body}")
file(READ "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native/metal_draw_frontend.inc" vertex_frontend)
string(FIND "${vertex_frontend}" "const std::shared_ptr<const theft4::render::Bytes>& Gta4NativeGraphicsSystem::PrepareNativeMetalVertexPayload(" vertex_begin)
string(FIND "${vertex_frontend}" "bool Gta4NativeGraphicsSystem::PrepareNativeMetalFetch(" vertex_end)
math(EXPR vertex_length "${vertex_end}-${vertex_begin}")
string(SUBSTRING "${vertex_frontend}" ${vertex_begin} ${vertex_length} vertex_body)
file(WRITE "${CMAKE_BINARY_DIR}/metal-geometry-publish.inc" "${vertex_body}")
add_executable(metal_vertex_conversion_test metal_vertex_conversion_test.cpp)
target_include_directories(metal_vertex_conversion_test PRIVATE "${CMAKE_BINARY_DIR}"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native" "${ROOT}/ios/bridge"
  "${ROOT}/glue/rexglue-sdk-main/thirdparty/xxHash")
target_compile_options(metal_vertex_conversion_test PRIVATE -UNDEBUG)
add_test(NAME metal_vertex_conversion_contract COMMAND metal_vertex_conversion_test)
add_executable(batch_payload_cache_test batch_payload_cache_test.cpp)
target_include_directories(batch_payload_cache_test PRIVATE "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native"
  "${ROOT}/thirdparty/json/include")
target_compile_options(batch_payload_cache_test PRIVATE -UNDEBUG)
add_test(NAME batch_payload_cache COMMAND batch_payload_cache_test)
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
add_executable(index_range_cache_test index_range_cache_test.cpp)
target_link_libraries(index_range_cache_test PRIVATE render_plan)
target_compile_options(index_range_cache_test PRIVATE -UNDEBUG)
add_test(NAME index_range_cache_contract COMMAND index_range_cache_test)
add_executable(frame_plan_test frame_plan_test.cpp)
target_link_libraries(frame_plan_test PRIVATE render_plan)
target_compile_options(frame_plan_test PRIVATE -UNDEBUG)
add_test(NAME frame_plan_contract COMMAND frame_plan_test)
add_executable(frame_admission_storage_test frame_admission_storage_test.cpp)
target_link_libraries(frame_admission_storage_test PRIVATE render_plan)
target_compile_options(frame_admission_storage_test PRIVATE -UNDEBUG)
add_test(NAME frame_admission_storage COMMAND frame_admission_storage_test)
add_executable(trait_cache_test trait_cache_test.cpp)
target_include_directories(trait_cache_test PRIVATE "${ROOT}/ios/bridge")
target_compile_options(trait_cache_test PRIVATE -UNDEBUG)
add_test(NAME metal_trait_cache COMMAND trait_cache_test)
add_executable(resource_admission_cache_test resource_admission_cache_test.cpp)
target_link_libraries(resource_admission_cache_test PRIVATE render_plan)
target_compile_options(resource_admission_cache_test PRIVATE -UNDEBUG)
add_test(NAME resource_admission_cache COMMAND resource_admission_cache_test)
target_include_directories(metal_shader_catalog PUBLIC "${ROOT}/ios/bridge")
add_executable(metal_shader_catalog_test shader_catalog_test.cpp $<TARGET_OBJECTS:metal_shader_corpus>)
target_include_directories(metal_shader_catalog_test PRIVATE "${ROOT}/LibertyRecompLib/shader"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native"
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
add_executable(incremental_owner_cache_test incremental_owner_cache_test.cpp)
target_include_directories(incremental_owner_cache_test PRIVATE "${ROOT}/thirdparty/json/include")
add_executable(frontend_storage_test frontend_storage_test.cpp)
target_include_directories(frontend_storage_test PRIVATE "${ROOT}/ios/bridge")
target_link_libraries(frontend_storage_test PRIVATE render_plan)
add_executable(exclusive_generation_retirement_test exclusive_generation_retirement_test.cpp)
add_executable(masked_constant_replay_test "${ROOT}/ios/tests/masked_constant_replay_test.cpp")
add_executable(constant_projection_test "${ROOT}/ios/tests/constant_projection_test.cpp")
foreach(test_target frontend_storage_test metal_constant_projection_test incremental_owner_cache_test exclusive_generation_retirement_test masked_constant_replay_test constant_projection_test)
  target_include_directories(${test_target} PRIVATE
    "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native" "${ROOT}/glue/rexglue-sdk-main/include")
  target_compile_options(${test_target} PRIVATE -UNDEBUG)
endforeach()
add_test(NAME metal_constant_projection COMMAND metal_constant_projection_test)
add_test(NAME incremental_owner_cache COMMAND incremental_owner_cache_test)
add_test(NAME frontend_storage COMMAND frontend_storage_test)
add_test(NAME exclusive_generation_retirement COMMAND exclusive_generation_retirement_test)
add_test(NAME masked_constant_replay COMMAND masked_constant_replay_test)
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

# Exercise the actual headless Metal startup and AA controller bodies. iOS
# initializes the guest GPU without calling the windowed presentation setup.
set(native_graphics_source "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${native_graphics_source}")
file(READ "${native_graphics_source}" native_graphics)
string(FIND "${native_graphics}" "void InitializeRendererAntiAliasingController() {" aa_begin)
string(FIND "${native_graphics}" "uint32_t GetNativePresentationAABits()" aa_end)
math(EXPR aa_length "${aa_end}-${aa_begin}")
string(SUBSTRING "${native_graphics}" ${aa_begin} ${aa_length} aa_body)
file(WRITE "${CMAKE_BINARY_DIR}/metal-aa-controller.inc" "${aa_body}")
string(FIND "${native_graphics}" "X_STATUS Gta4NativeGraphicsSystem::SetupGuestGpu(" guest_begin)
string(SUBSTRING "${native_graphics}" ${guest_begin} -1 guest_tail)
string(FIND "${guest_tail}" "\n#endif" guest_end)
math(EXPR guest_length "${guest_end}+7")
string(SUBSTRING "${guest_tail}" 0 ${guest_length} guest_body)
file(WRITE "${CMAKE_BINARY_DIR}/metal-guest-startup.inc" "${guest_body}\nreturn X_STATUS_UNSUCCESSFUL;\n}\n")
set(ios_startup_source "${ROOT}/ios/bridge/theft4_startup.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ios_startup_source}")
file(READ "${ios_startup_source}" ios_startup)
string(FIND "${ios_startup}" "        const char* anti_aliasing_override" ios_aa_begin)
string(FIND "${ios_startup}" "        const char* sharpening_override" ios_aa_end)
math(EXPR ios_aa_length "${ios_aa_end}-${ios_aa_begin}")
string(SUBSTRING "${ios_startup}" ${ios_aa_begin} ${ios_aa_length} ios_aa_body)
file(WRITE "${CMAKE_BINARY_DIR}/metal-ios-aa-startup.inc" "void ApplyIosAntiAliasing() {\n${ios_aa_body}\n}\n")
add_executable(metal_aa_startup_test metal_aa_startup_test.cpp)
target_include_directories(metal_aa_startup_test PRIVATE "${CMAKE_BINARY_DIR}" "${ROOT}/glue/rexglue-sdk-main/include")
target_compile_definitions(metal_aa_startup_test PRIVATE THEFT4_DIRECT_METAL_BACKEND=1)
target_compile_options(metal_aa_startup_test PRIVATE -UNDEBUG)
add_test(NAME metal_headless_aa_startup COMMAND metal_aa_startup_test)
add_executable(metal_profiling_policy_test metal_profiling_policy_test.cpp)
target_include_directories(metal_profiling_policy_test PRIVATE "${ROOT}/ios/bridge")
target_compile_options(metal_profiling_policy_test PRIVATE -UNDEBUG)
add_test(NAME metal_profiling_policy COMMAND metal_profiling_policy_test)

# Repeatable synthetic admission/submission comparison; requires a real Metal device.
add_executable(metal_upload_stress_test upload_stress_test.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_resources.mm")
target_include_directories(metal_upload_stress_test PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_upload_stress_test PRIVATE -fobjc-arc)
target_compile_definitions(metal_upload_stress_test PRIVATE THEFT4_PACKED_UPLOAD_STRESS=1)
target_link_libraries(metal_upload_stress_test PRIVATE "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_executable(metal_upload_page_benchmark upload_page_benchmark.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_resources.mm")
target_include_directories(metal_upload_page_benchmark PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_upload_page_benchmark PRIVATE -fobjc-arc)
target_link_libraries(metal_upload_page_benchmark PRIVATE "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_executable(metal_geometry_stream_test geometry_stream_upload_test.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_resources.mm")
target_include_directories(metal_geometry_stream_test PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_geometry_stream_test PRIVATE -fobjc-arc -UNDEBUG)
target_link_libraries(metal_geometry_stream_test PRIVATE "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_executable(metal_geometry_pressure_test geometry_pressure_test.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_resources.mm")
target_include_directories(metal_geometry_pressure_test PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_geometry_pressure_test PRIVATE -fobjc-arc -UNDEBUG)
target_link_libraries(metal_geometry_pressure_test PRIVATE "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_executable(metal_upload_lifetime_test upload_lifetime_test.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_resources.mm")
target_include_directories(metal_upload_lifetime_test PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_upload_lifetime_test PRIVATE -fobjc-arc -UNDEBUG)
target_link_libraries(metal_upload_lifetime_test PRIVATE "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_executable(metal_frame_upload_test frame_upload_test.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_resources.mm")
target_include_directories(metal_frame_upload_test PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_frame_upload_test PRIVATE -fobjc-arc -UNDEBUG)
target_link_libraries(metal_frame_upload_test PRIVATE "-framework Foundation" "-framework Metal" "-framework QuartzCore")

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

# Alternating A/B GPU shader comparison using private saved game draws.
get_target_property(shader_comparison_sources metal_probe SOURCES)
list(REMOVE_ITEM shader_comparison_sources probe_main.mm)
add_executable(metal_shader_comparison shader_comparison_main.mm ${shader_comparison_sources})
target_include_directories(metal_shader_comparison PRIVATE "${ROOT}/ios/bridge" "${ROOT}/ios/tests"
  "${ROOT}/thirdparty/json/include" "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native")
target_compile_options(metal_shader_comparison PRIVATE -fobjc-arc)
target_compile_definitions(metal_shader_comparison PRIVATE THEFT4_SHADER_BENCHMARK=1
  "THEFT4_METAL_SOURCE_REVISION=\"${METAL_SOURCE_REVISION}\"")
target_link_libraries(metal_shader_comparison PRIVATE metal_shader_catalog render_plan
  "-framework Foundation" "-framework Metal" "-framework QuartzCore"
  "-framework CoreGraphics" "-framework ImageIO")

add_executable(metal_texture_usage_benchmark texture_usage_benchmark.mm)
target_include_directories(metal_texture_usage_benchmark PRIVATE "${ROOT}/thirdparty/json/include")
target_compile_options(metal_texture_usage_benchmark PRIVATE -fobjc-arc)
target_compile_definitions(metal_texture_usage_benchmark PRIVATE
  "THEFT4_METAL_SOURCE_REVISION=\"${METAL_SOURCE_REVISION}\"")
target_link_libraries(metal_texture_usage_benchmark PRIVATE "-framework Foundation" "-framework Metal")

add_executable(metal_hardware_resolve_comparison hardware_resolve_comparison.mm)
target_include_directories(metal_hardware_resolve_comparison PRIVATE "${ROOT}/thirdparty/json/include")
target_compile_options(metal_hardware_resolve_comparison PRIVATE -fobjc-arc)
target_link_libraries(metal_hardware_resolve_comparison PRIVATE "-framework Foundation" "-framework Metal")

add_executable(retail_diagnostic_policy_test "${ROOT}/ios/tests/retail_diagnostic_policy_test.cpp"
  "${ROOT}/glue/rexglue-sdk-main/src/core/diagnostics_policy.cpp")
target_include_directories(retail_diagnostic_policy_test PRIVATE "${ROOT}/ios/bridge"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native"
  "${ROOT}/glue/rexglue-sdk-main/include")
target_compile_options(retail_diagnostic_policy_test PRIVATE -UNDEBUG)
add_test(NAME retail_diagnostics_disabled COMMAND retail_diagnostic_policy_test retail)
add_test(NAME diagnostics_restored_on_fresh_launch COMMAND retail_diagnostic_policy_test diagnostics)

# Representative CPU preparation comparison; private game data remains external.
add_executable(metal_draw_preparation_benchmark draw_preparation_benchmark.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_shader_store.mm"
  "${ROOT}/ios/bridge/theft4_metal_resources.mm" "${ROOT}/ios/bridge/theft4_metal_plan.mm")
target_include_directories(metal_draw_preparation_benchmark PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_draw_preparation_benchmark PRIVATE -fobjc-arc)
target_link_libraries(metal_draw_preparation_benchmark PRIVATE metal_shader_catalog render_plan
  "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_executable(metal_draw_binding_storage_test draw_binding_storage_test.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm" "${ROOT}/ios/bridge/theft4_metal_shader_store.mm"
  "${ROOT}/ios/bridge/theft4_metal_resources.mm" "${ROOT}/ios/bridge/theft4_metal_plan.mm"
  "${ROOT}/ios/bridge/theft4_metal_frame.mm" "${ROOT}/ios/bridge/theft4_metal_host_shaders.mm")
target_include_directories(metal_draw_binding_storage_test PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_draw_binding_storage_test PRIVATE -fobjc-arc -UNDEBUG)
target_link_libraries(metal_draw_binding_storage_test PRIVATE metal_shader_catalog render_plan
  "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_executable(metal_driver_trait_cache_test driver_trait_cache_test.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm")
target_include_directories(metal_driver_trait_cache_test PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_driver_trait_cache_test PRIVATE -fobjc-arc -UNDEBUG)
target_compile_definitions(metal_driver_trait_cache_test PRIVATE THEFT4_EXPECT_ASSOCIATIVE_TRAITS=1)
target_link_libraries(metal_driver_trait_cache_test PRIVATE "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_executable(metal_pipeline_archive_test pipeline_archive_test.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm")
target_include_directories(metal_pipeline_archive_test PRIVATE "${ROOT}/ios/bridge" "${ROOT}/thirdparty/json/include")
target_compile_options(metal_pipeline_archive_test PRIVATE -fobjc-arc -UNDEBUG)
target_link_libraries(metal_pipeline_archive_test PRIVATE "-framework Foundation" "-framework Metal" "-framework QuartzCore")
add_test(NAME pipeline_archive_contract COMMAND metal_pipeline_archive_test "${CMAKE_BINARY_DIR}/private-deferred-archive")

file(READ "${ROOT}/ios/bridge/theft4_metal_resources.mm" pool_source)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ROOT}/ios/bridge/theft4_metal_resources.mm" "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.h")
string(FIND "${pool_source}" "struct UploadPagePool {" pool_begin)
string(FIND "${pool_source}" "struct UploadPageBacking {" pool_end)
math(EXPR pool_length "${pool_end}-${pool_begin}")
string(SUBSTRING "${pool_source}" ${pool_begin} ${pool_length} pool_body)
file(WRITE "${CMAKE_BINARY_DIR}/upload-pool-body.inc" "${pool_body}")
add_executable(upload_pool_lock_test upload_pool_lock_test.cpp)
target_include_directories(upload_pool_lock_test PRIVATE "${CMAKE_BINARY_DIR}")
target_compile_options(upload_pool_lock_test PRIVATE -UNDEBUG)
add_test(NAME upload_pool_lock_scope COMMAND upload_pool_lock_test)
file(READ "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.h" pipeline_header)
string(FIND "${pipeline_header}" "  struct NativePipelineKey {" key_begin)
string(FIND "${pipeline_header}" "  struct NativePipeline {" key_end)
math(EXPR key_length "${key_end}-${key_begin}")
string(SUBSTRING "${pipeline_header}" ${key_begin} ${key_length} key_body)
file(WRITE "${CMAKE_BINARY_DIR}/pipeline-template-key.inc" "${key_body}")
string(FIND "${native_graphics}" "size_t Gta4NativeGraphicsSystem::NativePipelineKeyHash::operator()(" hash_begin)
string(FIND "${native_graphics}" "size_t Gta4NativeGraphicsSystem::NativePersistentBufferKeyHash::operator()(" hash_end)
math(EXPR hash_length "${hash_end}-${hash_begin}")
string(SUBSTRING "${native_graphics}" ${hash_begin} ${hash_length} hash_body)
file(WRITE "${CMAKE_BINARY_DIR}/pipeline-template-hash.inc" "${hash_body}")
add_executable(pipeline_template_pressure_test pipeline_template_pressure_test.cpp)
target_include_directories(pipeline_template_pressure_test PRIVATE "${CMAKE_BINARY_DIR}" "${ROOT}/ios/bridge" "${ROOT}/glue/rexglue-sdk-main/include"
 "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native" "${ROOT}/glue/rexglue-sdk-main/thirdparty/xxHash"
 "${ROOT}/glue/rexglue-sdk-main/thirdparty/vulkan-headers/include" "${ROOT}/thirdparty/json/include")
target_compile_options(pipeline_template_pressure_test PRIVATE -UNDEBUG)
add_test(NAME pipeline_template_pressure COMMAND pipeline_template_pressure_test "${CMAKE_BINARY_DIR}/PIPELINE_PRESSURE.json")
