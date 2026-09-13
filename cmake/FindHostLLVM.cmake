# Native macOS compiler discovery. Explicit -D compiler choices and CC/CXX win.
# This can also be used as a preset toolchain and is safe in try_compile().
# Keep the compiler's target independent of the installed CLT SDK's default.
# The desktop runtime uses libc++ floating-point from_chars (macOS 26+).
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

find_program(_liberty_brew brew)
if(_liberty_brew)
    execute_process(COMMAND "${_liberty_brew}" --prefix llvm
        OUTPUT_VARIABLE _liberty_llvm_prefix OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _liberty_llvm_result ERROR_QUIET)
    if(NOT _liberty_llvm_result EQUAL 0)
        unset(_liberty_llvm_prefix)
    endif()
endif()

foreach(_liberty_language C CXX)
    if(_liberty_language STREQUAL "C")
        set(_liberty_compiler_name clang)
        set(_liberty_compiler_env "$ENV{CC}")
    else()
        set(_liberty_compiler_name clang++)
        set(_liberty_compiler_env "$ENV{CXX}")
    endif()
    if(NOT CMAKE_${_liberty_language}_COMPILER AND NOT _liberty_compiler_env)
        unset(_liberty_compiler)
        if(_liberty_llvm_prefix)
            find_program(_liberty_compiler NAMES "${_liberty_compiler_name}"
                PATHS "${_liberty_llvm_prefix}/bin" NO_DEFAULT_PATH NO_CACHE)
        endif()
        if(NOT _liberty_compiler)
            find_program(_liberty_compiler NAMES "${_liberty_compiler_name}" REQUIRED NO_CACHE)
        endif()
        set(CMAKE_${_liberty_language}_COMPILER "${_liberty_compiler}" CACHE FILEPATH "Host LLVM compiler")
    endif()
endforeach()

# OpenSSL is keg-only in Homebrew; make its package discoverable without a
# machine-specific prefix in committed presets or source files.
if(_liberty_brew)
    execute_process(COMMAND "${_liberty_brew}" --prefix openssl@3
        OUTPUT_VARIABLE _liberty_openssl_prefix OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _liberty_openssl_result ERROR_QUIET)
    if(_liberty_openssl_result EQUAL 0 AND EXISTS "${_liberty_openssl_prefix}")
        list(APPEND CMAKE_PREFIX_PATH "${_liberty_openssl_prefix}")
        list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
    endif()
endif()
