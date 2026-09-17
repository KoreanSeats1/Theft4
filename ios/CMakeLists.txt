# Theft4's native host is independent of the legacy desktop/SDL application.
enable_language(OBJC)
set(THEFT4_BUNDLE_IDENTIFIER "com.theft4.bringup" CACHE STRING "Theft4 development bundle ID")
set(THEFT4_DISPLAY_NAME "Theft4" CACHE STRING "Name shown on the device")
option(THEFT4_LAB_BUILD "Identify an isolated experimental app" OFF)
if(THEFT4_LAB_BUILD)
    if(NOT THEFT4_BUNDLE_IDENTIFIER STREQUAL "com.theft4.m5lab" OR
       NOT THEFT4_DISPLAY_NAME STREQUAL "Theft4 Lab")
        message(FATAL_ERROR "Lab builds require com.theft4.m5lab and display name Theft4 Lab")
    endif()
    set(THEFT4_LAB_PLIST_VALUE "<true/>")
else()
    set(THEFT4_LAB_PLIST_VALUE "<false/>")
endif()
option(THEFT4_SIGN_DEVICE "Sign Theft4 with the configured development team" OFF)
set(THEFT4_ARM64_TUNE "apple-m5" CACHE STRING
    "ARM64 scheduling target; empty uses the compiler default without changing ISA requirements")
option(THEFT4_ENABLE_THIN_LTO
    "Enable Release ThinLTO for Theft4-owned AOT, bridge, and app targets" OFF)
option(THEFT4_COMPILE_GTA4_NATIVE_BACKEND
    "Compile-check LibertyRecomp's GTA IV native renderer for iOS without selecting it" OFF)
option(THEFT4_ENABLE_GTA4_NATIVE_BACKEND
    "Embed the GTA IV native renderer behind THEFT4_GRAPHICS_BACKEND=native" OFF)

set(THEFT4_NATIVE_FONT_ASSET_ROOT
    "${CMAKE_SOURCE_DIR}/LibertyRecompLib/font_atlases")

if(THEFT4_PUBLIC_BUILD)
    # The source-tree fallback cannot exist on an installed iOS device, and
    # embedding it as a string would disclose the builder's local checkout.
    set(THEFT4_NATIVE_FONT_ASSET_ROOT "")
endif()

# Compile the actual checked-in AOT title, not a synthetic PPC fixture. Keep
# this separate until the kernel/runtime imports can be linked into the host.
option(THEFT4_BUILD_GAME_CODE "Build the complete GTA IV AOT archive" OFF)
option(THEFT4_ENABLE_GAME_STARTUP "Attempt actual guest startup using the headless HLE kernel" OFF)
if(THEFT4_ENABLE_GAME_STARTUP AND NOT (THEFT4_BUILD_GAME_CODE AND REXGLUE_HEADLESS_KERNEL))
    message(FATAL_ERROR "Game startup requires THEFT4_BUILD_GAME_CODE and REXGLUE_HEADLESS_KERNEL")
endif()
if(THEFT4_BUILD_GAME_CODE)
    set(THEFT4_GENERATED_DIR "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/gta4-recomp/generated")
    include("${THEFT4_GENERATED_DIR}/sources.cmake")
    add_library(theft4_game_code STATIC ${GENERATED_SOURCES})
    target_link_libraries(theft4_game_code PRIVATE rex::core)
    target_include_directories(theft4_game_code PRIVATE "${THEFT4_GENERATED_DIR}")
    target_compile_features(theft4_game_code PRIVATE cxx_std_23)
    # Tune instruction scheduling while preserving the deployment target's
    # default instruction set. Strict FP preserves generated guest math.
    target_compile_options(theft4_game_code PRIVATE
        -fno-strict-aliasing -ffp-model=strict
        -gline-tables-only)
    if(THEFT4_ARM64_TUNE AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        target_compile_options(theft4_game_code PRIVATE "-mtune=${THEFT4_ARM64_TUNE}")
    endif()
    # The 38,606-call registrar only populates the function table once at
    # startup. Optimizing it has no gameplay benefit and makes clean Release
    # builds spend minutes in Apple Clang. Keep the actual generated source
    # and all registrations unchanged while compiling this one table at O0;
    # SelectionDAG also avoids the measured GlobalISel localizer bottleneck.
    set_source_files_properties("${THEFT4_GENERATED_DIR}/gta4_register.cpp"
        PROPERTIES COMPILE_OPTIONS "-O0;-fno-global-isel")
    set_target_properties(theft4_game_code PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game-code")
    if(THEFT4_ENABLE_THIN_LTO)
        set_property(TARGET theft4_game_code PROPERTY
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endif()

if(REXGLUE_RUNTIME_ONLY)
    set(THEFT4_GAME_INSTALL_DIR "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/gta4-recomp/src/install")
    add_library(theft4_game_source STATIC "${THEFT4_GAME_INSTALL_DIR}/gta4_source_inspector.cpp")
    target_include_directories(theft4_game_source PUBLIC "${THEFT4_GAME_INSTALL_DIR}")
    target_include_directories(theft4_game_source PRIVATE
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/crypto")
    target_link_libraries(theft4_game_source PUBLIC rex::filesystem PRIVATE xxHash::xxhash)
    target_compile_features(theft4_game_source PRIVATE cxx_std_23)
    add_executable(theft4_inspect tools/inspect_game.cpp)
    target_link_libraries(theft4_inspect PRIVATE theft4_game_source)
    set_target_properties(theft4_inspect PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game-tools")
    add_library(theft4_game_install STATIC
        "${THEFT4_GAME_INSTALL_DIR}/gta4_installer.cpp"
        "${THEFT4_GAME_INSTALL_DIR}/gta4_rpf_extractor.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/crypto/rijndael-alg-fst.c")
    target_link_libraries(theft4_game_install PUBLIC theft4_game_source PRIVATE xxHash::xxhash z)
    target_include_directories(theft4_game_install PRIVATE
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/crypto")
    target_compile_definitions(theft4_game_install PRIVATE
        GTA4_RPF_AES_KEY_SOURCE="${CMAKE_SOURCE_DIR}/LibertyRecompLib/aes_key.bin")
    target_compile_features(theft4_game_install PRIVATE cxx_std_23)
    add_executable(theft4_stage tools/stage_game.cpp)
    target_link_libraries(theft4_stage PRIVATE theft4_game_install)
    set_target_properties(theft4_stage PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game-tools")
endif()

add_library(theft4_core_bridge STATIC bridge/theft4_core.cpp bridge/theft4_touch_input.cpp)
target_include_directories(theft4_core_bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/bridge")
target_link_libraries(theft4_core_bridge PRIVATE rex::core)
target_compile_features(theft4_core_bridge PRIVATE cxx_std_23)
if(REXGLUE_RUNTIME_ONLY)
    target_sources(theft4_core_bridge PRIVATE bridge/theft4_boot.cpp)
    target_link_libraries(theft4_core_bridge PRIVATE rex::runtime theft4_game_install)
    target_compile_definitions(theft4_core_bridge PUBLIC THEFT4_HAS_GAME_LOADER=1)
    add_executable(theft4_load tools/load_game.cpp)
    target_link_libraries(theft4_load PRIVATE theft4_core_bridge)
    target_link_options(theft4_load PRIVATE "LINKER:-dead_strip")
    set_target_properties(theft4_load PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game-tools")
    add_executable(theft4_start_runtime EXCLUDE_FROM_ALL tools/start_runtime.cpp)
    target_link_libraries(theft4_start_runtime PRIVATE rex::runtime)
    target_link_options(theft4_start_runtime PRIVATE "LINKER:-dead_strip")
    set_target_properties(theft4_start_runtime PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game-tools")
endif()

if(THEFT4_ENABLE_GAME_STARTUP)
    # Build LibertyRecomp's exact glslang revision for the iOS C++ ABI. The
    # command processor uses both SPIR-V emission and runtime compilation of
    # its small internal shaders, so a partial emitter archive is insufficient.
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    # SpirvBuilder derives from spv::Builder and the app is built with RTTI,
    # so the base library must emit matching typeinfo.
    set(ENABLE_RTTI ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory(
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/glslang"
        "${CMAKE_BINARY_DIR}/theft4-glslang" EXCLUDE_FROM_ALL)

    # Compile the mature GTA-IV-specific renderer as an inert archive before
    # attempting to select it from the iOS runtime. This target intentionally
    # has no consumer yet: it lets Apple Clang identify actual portability
    # blockers without disturbing the known-working generic Xenos backend.
    if(THEFT4_COMPILE_GTA4_NATIVE_BACKEND OR THEFT4_ENABLE_GTA4_NATIVE_BACKEND)
        set(THEFT4_GTA4_NATIVE_ROOT
            "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/gta4_native")
        set(THEFT4_GTA4_NATIVE_SHADER_CACHE_ROOT
            "${CMAKE_SOURCE_DIR}/LibertyRecompLib")
        set(THEFT4_GTA4_NATIVE_SMOLV_ROOT
            "${CMAKE_SOURCE_DIR}/tools/XenosRecomp/thirdparty/smol-v/source")
        set(THEFT4_GTA4_NATIVE_ZSTD_ROOT
            "${CMAKE_SOURCE_DIR}/tools/XenosRecomp/thirdparty/zstd")
        set(THEFT4_GTA4_RECOMP_SOURCE_ROOT
            "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/gta4-recomp/src")

        add_library(theft4_gta4_native_compile STATIC EXCLUDE_FROM_ALL
            "${THEFT4_GTA4_NATIVE_ROOT}/graphics_system.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/modern_shader_options.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/native_buffer_arena.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/native_descriptor_backend.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/native_frame_context.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/native_gpu_attribution.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/native_memory_samples.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/native_performance_samples.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/native_submission_lifetime.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/postfx_resource_pool.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/smaa_pipeline.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/split_postfx_pass.cpp"
            "${THEFT4_GTA4_NATIVE_ROOT}/sun_shafts_pass.cpp"
            "${THEFT4_GTA4_RECOMP_SOURCE_ROOT}/gta4_aspect_hooks.cpp"
            "${THEFT4_GTA4_RECOMP_SOURCE_ROOT}/gta4_font_selection_trace.cpp"
            "${THEFT4_GTA4_RECOMP_SOURCE_ROOT}/gta4_native_hooks.cpp"
            bridge/theft4_motion_blur.cpp
            "${THEFT4_GTA4_NATIVE_SHADER_CACHE_ROOT}/shader/shader_cache.cpp"
            "${THEFT4_GTA4_NATIVE_SMOLV_ROOT}/smolv.cpp")
        target_include_directories(theft4_gta4_native_compile PRIVATE
            "${THEFT4_GTA4_NATIVE_ROOT}"
            "${THEFT4_GTA4_RECOMP_SOURCE_ROOT}"
            "${THEFT4_GENERATED_DIR}"
            "${THEFT4_GTA4_NATIVE_SHADER_CACHE_ROOT}"
            "${THEFT4_GTA4_NATIVE_SMOLV_ROOT}"
            "${THEFT4_GTA4_NATIVE_ZSTD_ROOT}/lib"
            "${CMAKE_SOURCE_DIR}/thirdparty/json/include"
            "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/spirv-headers/include"
            "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/vulkan-headers/include"
            "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/vulkan-memory-allocator/include")
        target_link_libraries(theft4_gta4_native_compile PRIVATE
            rex::runtime
            xxHash::xxhash)
        target_compile_definitions(theft4_gta4_native_compile PRIVATE
            REX_HAS_VULKAN=1
            REXGLUE_BUILD_CONFIG="$<CONFIG>"
            GTA4_NATIVE_FONT_ASSET_ROOT="${THEFT4_NATIVE_FONT_ASSET_ROOT}")
        target_compile_features(theft4_gta4_native_compile PRIVATE cxx_std_23)
        target_compile_options(theft4_gta4_native_compile PRIVATE
            -Wno-unused-variable)
        set_source_files_properties(
            "${THEFT4_GTA4_NATIVE_SHADER_CACHE_ROOT}/shader/shader_cache.cpp"
            PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
        if(THEFT4_ARM64_TUNE AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
            target_compile_options(theft4_gta4_native_compile PRIVATE
                "-mtune=${THEFT4_ARM64_TUNE}")
        endif()

        if(THEFT4_ENABLE_GTA4_NATIVE_BACKEND)
            target_sources(theft4_gta4_native_compile PRIVATE
                bridge/theft4_empty_shader_overrides.cpp)

            if(NOT TARGET libzstd_static)
                set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
                set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
                set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
                set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
                set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
                add_subdirectory("${THEFT4_GTA4_NATIVE_ZSTD_ROOT}/build/cmake"
                    "${CMAKE_BINARY_DIR}/theft4-gta4-native-zstd" EXCLUDE_FROM_ALL)
            endif()
            target_link_libraries(theft4_gta4_native_compile PRIVATE libzstd_static)

            target_sources(theft4_core_bridge PRIVATE
                bridge/theft4_gta4_native_graphics.cpp
                "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/image_decode.cpp"
                "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/immediate_drawer.cpp"
                "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/windowed_app_context.cpp"
                "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_immediate_drawer.cpp"
                "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_provider.cpp")
            target_include_directories(theft4_core_bridge PRIVATE
                "${THEFT4_GTA4_NATIVE_ROOT}"
                "${THEFT4_GTA4_NATIVE_SHADER_CACHE_ROOT}"
                "${THEFT4_GTA4_NATIVE_SMOLV_ROOT}"
                "${THEFT4_GTA4_NATIVE_ZSTD_ROOT}/lib"
                "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src"
                "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/stb")
            target_link_libraries(theft4_core_bridge PRIVATE
                theft4_gta4_native_compile libzstd_static)
            target_compile_definitions(theft4_core_bridge PRIVATE
                THEFT4_HAS_GTA4_NATIVE_BACKEND=1)
            # The native hook wrappers intentionally interpose weak generated
            # AOT entry points, so every archive object must be retained.
            target_link_options(theft4_core_bridge INTERFACE
                "LINKER:-force_load,$<TARGET_FILE:theft4_gta4_native_compile>")
        endif()
    endif()

    target_sources(theft4_core_bridge PRIVATE
        bridge/theft4_startup.cpp
        bridge/theft4_bootstrap_graphics.cpp
        bridge/theft4_metal_presenter.mm
        bridge/theft4_bootstrap_audio.cpp
        bridge/theft4_ios_audio_output.mm
        bridge/theft4_ios_audio_hotpaths.cpp
        bridge/theft4_bootstrap_input.cpp
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/input/absolute_pointer.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/input/ios/ios_input_driver.mm"
        bridge/theft4_graphics_flags.cpp
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/command_processor.cpp"
        # Compile the production Vulkan command processor now so iOS drift is
        # caught before its full cache/backend dependency closure is linked.
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/vulkan/command_processor.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/format/ucode.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/shader.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/spirv.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/spirv_builder.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/spirv_translator.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/spirv_translator_alu.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/spirv_translator_fetch.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/spirv_translator_memexport.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/spirv_translator_rb.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/translator.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/translator_disasm.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/util/draw.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/register_file.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/registers.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/trace_writer.cpp"
        # Keep the generated AOT bodies authoritative while interposing the
        # same platform-neutral world-transition diagnostics used by the
        # desktop host. Apple codegen emits weak public entry points and
        # separate __imp__ originals specifically for these strong wrappers.
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/gta4-recomp/src/gta4_transition_hooks.cpp")
    target_sources(theft4_core_bridge PRIVATE
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/flags.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/renderdoc_api.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/graphics_upload_buffer_pool.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/graphics_util.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/presenter.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/linked_type_descriptor_set_allocator.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/single_layout_descriptor_set_pool.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/ui_samplers.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_instance.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_device.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_mem_alloc.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_submission_tracker.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_upload_buffer_pool.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_util.cpp")

    # Production Xenos/Vulkan backend closure, excluding the desktop
    # GraphicsSystem and presenter. Static archive extraction keeps this inert
    # until the embedded bootstrap constructs VulkanCommandProcessor.
    target_sources(theft4_core_bridge PRIVATE
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/sampler_info.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/packet_disassembler.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/primitive_processor.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/shared_memory.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/render_target/cache.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/texture/cache.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/pipeline/shader/interpreter.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/util/draw_extent_estimator.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/vulkan/deferred_command_buffer.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/vulkan/pipeline_cache.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/vulkan/primitive_processor.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/vulkan/render_target_cache.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/vulkan/shader.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/vulkan/shared_memory.cpp"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/graphics/vulkan/texture_cache.cpp")
    set_source_files_properties(bridge/theft4_metal_presenter.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(bridge/theft4_ios_audio_output.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(bridge/theft4_ios_audio_hotpaths.cpp
        PROPERTIES COMPILE_OPTIONS "-ffp-model=strict")
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/input/ios/ios_input_driver.mm"
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_include_directories(theft4_core_bridge PRIVATE
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/src/input"
        "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/gta4-recomp/src"
        "${THEFT4_GENERATED_DIR}")
    target_link_libraries(theft4_core_bridge PRIVATE theft4_game_code
        SPIRV glslang MachineIndependent GenericCodeGen OSDependent OGLCompiler
        xxHash::xxhash)
    target_compile_definitions(theft4_core_bridge PUBLIC THEFT4_HAS_GAME_STARTUP=1)
    if(THEFT4_ARM64_TUNE AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        target_compile_options(theft4_core_bridge PRIVATE "-mtune=${THEFT4_ARM64_TUNE}")
    endif()
    # Kernel exports register themselves during static initialization. Retain
    # all real exports, including ones found later by guest ordinal lookup.
    target_link_options(theft4_core_bridge INTERFACE "LINKER:-force_load,$<TARGET_FILE:rexruntime>")

    # XeniOS is kept as an independent reference app, but its locally built,
    # public MoltenVK archive gives us a fast device capability gate before the
    # full LibertyRecomp Vulkan source closure is enabled for iOS. No XeniOS
    # emulator code is linked into Theft4.
    set(THEFT4_XENIOS_IOS_LIB_DIR
        "${CMAKE_SOURCE_DIR}/../XeniOS/build-ios-xcode/obj/iOS/Release"
        CACHE PATH "Optional XeniOS iOS library directory for MoltenVK validation")
    set(THEFT4_XENIOS_GENERATED_SHADER_DIR
        "${CMAKE_SOURCE_DIR}/../XeniOS/build-ios-xcode/generated"
        CACHE PATH "Optional XeniOS generated shader directory for direct resolve experiments")
    option(THEFT4_ENABLE_XENIOS_DIRECT_RESOLVE
           "Build the experimental XeniOS-derived direct host resolve path" OFF)
    if(EXISTS "${THEFT4_XENIOS_IOS_LIB_DIR}/libMoltenVK.a")
        target_sources(theft4_core_bridge PRIVATE
            bridge/theft4_vulkan_probe.cpp
            bridge/theft4_rex_vulkan_gate.cpp)
        target_include_directories(theft4_core_bridge PRIVATE
            "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/vulkan-headers/include"
            "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/vulkan-memory-allocator/include"
            "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/glslang"
            "${CMAKE_SOURCE_DIR}/glue/rexglue-sdk-main/thirdparty/renderdoc")
        target_compile_definitions(theft4_core_bridge PRIVATE
            THEFT4_HAS_MOLTENVK_PROBE=1
            REX_HAS_VULKAN=1)
        if(THEFT4_ENABLE_XENIOS_DIRECT_RESOLVE AND EXISTS
           "${THEFT4_XENIOS_GENERATED_SHADER_DIR}/xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_host_color_32bpp_1xmsaa_cs.h")
            # Experimental bridge to XeniOS's public, generated SPIR-V. Only
            # the GTA IV variants included by render_target_cache.cpp are
            # compiled; all unsupported cases retain the existing EDRAM path.
            target_include_directories(theft4_core_bridge PRIVATE
                "${THEFT4_XENIOS_GENERATED_SHADER_DIR}")
            target_compile_definitions(theft4_core_bridge PRIVATE
                THEFT4_XENIOS_DIRECT_RESOLVE_SHADERS=1)
        endif()
        target_link_libraries(theft4_core_bridge PRIVATE
            "${THEFT4_XENIOS_IOS_LIB_DIR}/libMoltenVK.a"
            "${THEFT4_XENIOS_IOS_LIB_DIR}/libMoltenVK_ShaderConverter.a"
            "${THEFT4_XENIOS_IOS_LIB_DIR}/libMoltenVK_Common.a"
            "${THEFT4_XENIOS_IOS_LIB_DIR}/libspirv-cross.a"
            "${THEFT4_XENIOS_IOS_LIB_DIR}/libSPIRV-Tools.a"
            "-framework CoreFoundation"
            "-framework Foundation"
            "-framework CoreGraphics"
            "-framework IOSurface")
        # MoltenVK implements several Metal additions as Objective-C
        # categories. Those archive members expose no ordinary linker symbol
        # and are otherwise dead-stripped, causing selectors such as
        # setBorderColorMVK: to be missing at runtime.
        target_link_options(theft4_core_bridge INTERFACE
            "LINKER:-force_load,${THEFT4_XENIOS_IOS_LIB_DIR}/libMoltenVK.a")
    endif()
endif()

set(THEFT4_ASSET_CATALOG "${CMAKE_CURRENT_SOURCE_DIR}/Theft4/Assets.xcassets")
add_executable(Theft4 MACOSX_BUNDLE Theft4/main.m Theft4/Theft4TouchControls.m
    Theft4/Theft4LauncherView.m Theft4/Theft4CityView.m
    Theft4/XeniOS-Touch-LICENSE.txt "${THEFT4_ASSET_CATALOG}")
set_source_files_properties(Theft4/XeniOS-Touch-LICENSE.txt PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties("${THEFT4_ASSET_CATALOG}" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
target_link_libraries(Theft4 PRIVATE theft4_core_bridge
    "-framework UIKit" "-framework Metal" "-framework QuartzCore" "-framework SceneKit" "-framework CoreGraphics"
    "-framework GameController" "-framework CoreHaptics"
    "-framework AVFoundation" "-framework AudioToolbox")
target_link_options(Theft4 PRIVATE "LINKER:-dead_strip")
target_compile_options(Theft4 PRIVATE -fobjc-arc)
if(THEFT4_ENABLE_THIN_LTO)
    set_property(TARGET theft4_core_bridge PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET Theft4 PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
set_target_properties(Theft4 PROPERTIES
    LINKER_LANGUAGE CXX
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Theft4/Info.plist.in"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${THEFT4_BUNDLE_IDENTIFIER}"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    XCODE_ATTRIBUTE_ENABLE_BITCODE NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/theft4")

# Publish a Release play scheme from the Release preset, not Xcode's implicit
# Debug launch default. Debug remains available in its separate build tree.
if(CMAKE_CONFIGURATION_TYPES STREQUAL "Release")
    set_target_properties(Theft4 PROPERTIES
        XCODE_GENERATE_SCHEME TRUE
        XCODE_SCHEME_LAUNCH_CONFIGURATION Release
        XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE Disabled
        XCODE_SCHEME_ENABLE_GPU_API_VALIDATION OFF
        XCODE_SCHEME_ENABLE_GPU_SHADER_VALIDATION OFF)
endif()
if(THEFT4_SIGN_DEVICE AND NOT CMAKE_OSX_SYSROOT MATCHES "(iphonesimulator|iPhoneSimulator)")
    if(NOT LIBERTY_IOS_DEVELOPMENT_TEAM)
        message(FATAL_ERROR "THEFT4_SIGN_DEVICE requires LIBERTY_IOS_DEVELOPMENT_TEAM")
    endif()
    set_target_properties(Theft4 PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED YES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${LIBERTY_IOS_DEVELOPMENT_TEAM}")
endif()

# Compile the consumer as C, proving the public boundary has no C++/UIKit types.
add_executable(theft4_bridge_tests ../tests/ios/theft4_bridge_tests.c)
target_link_libraries(theft4_bridge_tests PRIVATE theft4_core_bridge)
set_target_properties(theft4_bridge_tests PROPERTIES LINKER_LANGUAGE CXX
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ios-smoke")
