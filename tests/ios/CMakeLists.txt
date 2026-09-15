# Force every core object into the executable so a successful archive build
# cannot conceal missing platform implementations behind dead stripping.
add_executable(rexcore_link_smoke rexcore_link_smoke.cpp)
target_compile_features(rexcore_link_smoke PRIVATE cxx_std_23)
if(CMAKE_OSX_SYSROOT MATCHES "(iphonesimulator|iPhoneSimulator)")
    target_compile_definitions(rexcore_link_smoke PRIVATE THEFT4_EXPECT_SIMULATOR=1)
else()
    target_compile_definitions(rexcore_link_smoke PRIVATE THEFT4_EXPECT_SIMULATOR=0)
endif()
target_link_libraries(rexcore_link_smoke PRIVATE rex::core)
target_link_options(rexcore_link_smoke PRIVATE "LINKER:-force_load,$<TARGET_FILE:rexcore>")
set_target_properties(rexcore_link_smoke PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ios-smoke")

foreach(forbidden rexglue rexgpu-xenos rexgpu-gta4-native plume MoltenVK nfd
                  SDL3-static TracyClient GameNetworkingSockets)
    if(TARGET ${forbidden})
        message(FATAL_ERROR "Unexpected dependency in iOS core build: ${forbidden}")
    endif()
endforeach()
if(TARGET libavcodec AND NOT REXGLUE_HEADLESS_XMA_DECODER)
    message(FATAL_ERROR "Unexpected dependency in iOS core build: libavcodec")
endif()
if(TARGET rexruntime AND NOT REXGLUE_RUNTIME_ONLY)
    message(FATAL_ERROR "Unexpected runtime in core-only build")
endif()
