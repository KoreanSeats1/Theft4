# The setup helper and CMake share one patch engine, lock and applied-state receipt.
# Standalone use:
#   cmake -DLIBERTY_DEPENDENCY_ROOT=/path/to/LibertyRecomp -P cmake/DependencyPatches.cmake
include_guard(GLOBAL)

function(liberty_apply_dependency_patches repository_root)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(setup_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/setup_repo.py")
    set(arguments --root "${repository_root}"
        --patch-directory "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependency-patches")
    if(THEFT4_LAB_BUILD)
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" OR
           (NOT THEFT4_BUNDLE_IDENTIFIER STREQUAL "com.theft4.m5lab" AND
            NOT THEFT4_BUNDLE_IDENTIFIER STREQUAL "com.theft4.bringup" AND
            NOT THEFT4_BUNDLE_IDENTIFIER STREQUAL "com.lukebrosious.theft4"))
            message(FATAL_ERROR "The promoted source-export path requires a Theft4 iOS identity")
        endif()
        # Promoted and frozen Lab inputs are already patched. Check every
        # expected checksum without writing Git state or applying fixes.
        list(APPEND arguments --check-export)
    else()
        list(APPEND arguments --prepare-only)
    endif()
    if(DEFINED LIBERTY_DEPENDENCY_ONLY AND NOT "${LIBERTY_DEPENDENCY_ONLY}" STREQUAL "")
        list(APPEND arguments --only "${LIBERTY_DEPENDENCY_ONLY}")
    endif()
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${setup_script}" ${arguments}
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "Dependency preparation failed. Preserve local changes, then run:\n"
            "  python3 tools/setup_repo.py\nSee docs/BUILDING.md.")
    endif()
    if(THEFT4_LAB_BUILD)
        set(REXGLUE_DEPENDENCY_SOURCE_EXPORT ON PARENT_SCOPE)
    endif()
endfunction()

function(liberty_require_dependency_sources repository_root)
    set(required
        glue/rexglue-sdk-main/thirdparty/sdl3/CMakeLists.txt
        glue/rexglue-sdk-main/thirdparty/fmt/CMakeLists.txt
        glue/rexglue-sdk-main/thirdparty/FFmpeg/config.h
        glue/rexglue-sdk-main/gta4-recomp/generated/sources.cmake)
    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND required thirdparty/MoltenVK/MoltenVK/README.md
            thirdparty/MoltenVK/SPIRV-Cross/CMakeLists.txt)
    else()
        list(APPEND required tools/XenosRecomp/CMakeLists.txt
            tools/XenosRecomp/thirdparty/fmt/CMakeLists.txt
            tools/XenosRecomp/thirdparty/zstd/build/cmake/CMakeLists.txt
            tools/XenosRecomp/thirdparty/dxc-bin/CMakeLists.txt
            thirdparty/msdf-atlas-gen/CMakeLists.txt)
    endif()
    foreach(relative IN LISTS required)
        if(NOT EXISTS "${repository_root}/${relative}")
            message(FATAL_ERROR "Dependency source missing: ${relative}\n"
                "Run python3 tools/setup_repo.py (Windows: py -3 tools/setup_repo.py).\n"
                "See docs/BUILDING.md. No game-code generation is needed for a normal clone.")
        endif()
    endforeach()
endfunction()

if("${CMAKE_SCRIPT_MODE_FILE}" STREQUAL "${CMAKE_CURRENT_LIST_FILE}")
    if(NOT DEFINED LIBERTY_DEPENDENCY_ROOT)
        get_filename_component(LIBERTY_DEPENDENCY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    endif()
    liberty_apply_dependency_patches("${LIBERTY_DEPENDENCY_ROOT}")
endif()
