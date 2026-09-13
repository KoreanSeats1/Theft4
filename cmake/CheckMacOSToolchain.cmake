# Fail at configuration, before compiling hundreds of runtime files. Merely
# running Tahoe does not make Clang target Tahoe or select its SDK.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "26.0")
    message(FATAL_ERROR
        "The current desktop runtime requires macOS 26.0 or later. Configure with "
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0. See docs/BUILDING.md.")
endif()

include(CheckCXXSourceCompiles)
# Recheck when the user selects a different SDK/target in an existing tree.
unset(LIBERTY_MACOS_FLOAT_FROM_CHARS CACHE)
check_cxx_source_compiles([=[
    #include <charconv>
    #include <string_view>
    int main(int argc, char** argv) {
        const std::string_view text = argc > 1 ? argv[1] : "1.5";
        float f = 0;
        double d = 0;
        const auto a = std::from_chars(text.data(), text.data() + text.size(), f, std::chars_format::general);
        const auto b = std::from_chars(text.data(), text.data() + text.size(), d, std::chars_format::general);
        return a.ec != std::errc{} || b.ec != std::errc{};
    }
]=] LIBERTY_MACOS_FLOAT_FROM_CHARS)
if(NOT LIBERTY_MACOS_FLOAT_FROM_CHARS)
    message(FATAL_ERROR
        "The selected compiler/SDK cannot compile and link floating-point std::from_chars. "
        "Install current Homebrew LLVM and Xcode 26+ (or Command Line Tools with the "
        "macOS 26+ SDK), select that developer directory, then reconfigure. "
        "See docs/BUILDING.md and CMakeFiles/CMakeConfigureLog.yaml for the diagnostic.")
endif()
