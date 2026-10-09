cmake_minimum_required(VERSION 3.25)
project(theft4_mod_tests LANGUAGES C CXX OBJC OBJCXX)
set(CMAKE_CXX_STANDARD 23)
get_filename_component(ROOT "../../.." ABSOLUTE)
set(SDK "${ROOT}/glue/rexglue-sdk-main")
set(REXGLUE_CORE_ONLY ON CACHE BOOL "" FORCE)
set(REXGLUE_RUNTIME_ONLY OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDK}" rexcore)
add_subdirectory("${SDK}/src/filesystem" rexfilesystem)
target_include_directories(rexfilesystem PUBLIC "${SDK}/include")
add_executable(timecycle_mod_test timecycle_mod_test.cpp)
target_include_directories(timecycle_mod_test PRIVATE "${ROOT}/ios/bridge")
target_link_libraries(timecycle_mod_test PRIVATE rex::filesystem)
target_compile_options(timecycle_mod_test PRIVATE -UNDEBUG)
enable_testing()
add_test(NAME timecycle_mod_test COMMAND timecycle_mod_test
    "${ROOT}/ios/Theft4/Mods/CustomTimeCycle")
add_executable(timecycle_archive_test timecycle_archive_test.cpp
    "${ROOT}/ios/bridge/theft4_timecycle_archive.cpp"
    "${SDK}/thirdparty/crypto/rijndael-alg-fst.c")
target_include_directories(timecycle_archive_test PRIVATE
    "${ROOT}/ios/bridge" "${SDK}/thirdparty/crypto")
target_link_libraries(timecycle_archive_test PRIVATE rex::filesystem xxHash::xxhash)
target_compile_options(timecycle_archive_test PRIVATE -UNDEBUG)
add_test(NAME timecycle_archive_test COMMAND timecycle_archive_test
    "${ROOT}/ios/Theft4/Mods/CustomTimeCycle")

add_executable(native_aspect_policy_test native_aspect_policy_test.cpp)
target_include_directories(native_aspect_policy_test PRIVATE
    "${ROOT}/ios/bridge" "${SDK}/gta4-recomp/src")
target_compile_options(native_aspect_policy_test PRIVATE -UNDEBUG)
add_test(NAME native_aspect_policy_test COMMAND native_aspect_policy_test)
add_executable(native_aspect_resolve_test native_aspect_resolve_test.cpp)
target_include_directories(native_aspect_resolve_test PRIVATE "${SDK}/include" "${SDK}/src"
    "${SDK}/thirdparty/simde" "${SDK}/thirdparty/fmt/include")
target_compile_options(native_aspect_resolve_test PRIVATE -UNDEBUG)
add_test(NAME native_aspect_resolve_test COMMAND native_aspect_resolve_test)
add_executable(original_aspect_policy_test "${ROOT}/ios/tests/lab_resolution_policy_test.cpp")
target_include_directories(original_aspect_policy_test PRIVATE
    "${ROOT}/ios/bridge" "${SDK}/gta4-recomp/src")
target_compile_options(original_aspect_policy_test PRIVATE -UNDEBUG)
add_test(NAME original_aspect_policy_test COMMAND original_aspect_policy_test)
add_executable(aspect_roundtrip_test "${SDK}/tests/regression/aspect_geometry_roundtrip_test.cpp")
target_include_directories(aspect_roundtrip_test PRIVATE "${SDK}/gta4-recomp/src")
target_compile_definitions(aspect_roundtrip_test PRIVATE GTA4_ASPECT_STANDALONE)
target_compile_options(aspect_roundtrip_test PRIVATE -UNDEBUG)
add_test(NAME aspect_roundtrip_test COMMAND aspect_roundtrip_test)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/aspect_camera_extracted.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract_aspect_camera.py"
        "${ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/aspect_camera_extracted.h"
    DEPENDS extract_aspect_camera.py "${SDK}/gta4-recomp/src/gta4_aspect_hooks.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.5.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.10.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.65.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.78.cpp")
add_executable(aspect_camera_test aspect_camera_test.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/aspect_camera_extracted.h")
target_include_directories(aspect_camera_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${SDK}/gta4-recomp/generated" "${SDK}/gta4-recomp/src")
target_link_libraries(aspect_camera_test PRIVATE rex::core)
target_compile_options(aspect_camera_test PRIVATE -UNDEBUG)
add_test(NAME aspect_camera_test COMMAND aspect_camera_test)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/aspect_ui_extracted.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract_aspect_ui.py"
        "${ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/aspect_ui_extracted.h"
    DEPENDS extract_aspect_ui.py "${SDK}/gta4-recomp/src/gta4_aspect_hooks.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.4.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.6.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.17.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.10.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.65.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.78.cpp")
add_executable(aspect_ui_test aspect_ui_test.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/aspect_ui_extracted.h")
target_include_directories(aspect_ui_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${SDK}/gta4-recomp/generated" "${SDK}/gta4-recomp/src")
target_link_libraries(aspect_ui_test PRIVATE rex::core)
target_compile_options(aspect_ui_test PRIVATE -UNDEBUG)
add_test(NAME aspect_ui_test COMMAND aspect_ui_test)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gameplay_extracted.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract_gameplay.py"
        "${ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/gameplay_extracted.h"
    DEPENDS extract_gameplay.py "${SDK}/gta4-recomp/src/gta4_gameplay_mods.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.8.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.10.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.9.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.28.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.39.cpp"
        "${SDK}/gta4-recomp/generated/gta4_recomp.40.cpp")
add_executable(gameplay_mod_test gameplay_mod_test.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/gameplay_extracted.h")
target_include_directories(gameplay_mod_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${SDK}/gta4-recomp/generated" "${SDK}/gta4-recomp/src")
target_link_libraries(gameplay_mod_test PRIVATE rex::core)
target_compile_options(gameplay_mod_test PRIVATE -UNDEBUG)
add_test(NAME gameplay_mod_test COMMAND gameplay_mod_test)
