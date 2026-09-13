cmake_minimum_required(VERSION 3.29)

if(NOT IS_DIRECTORY "${APP_BUNDLE}/Contents/MacOS")
    message(FATAL_ERROR "Missing app bundle: ${APP_BUNDLE}")
endif()

# The renderer and Vulkan driver are dlopen-loaded, so scanning only the main
# executable misses their dependencies (including Homebrew LLVM's libc++).
file(GLOB_RECURSE _liberty_bundle_libraries "${APP_BUNDLE}/Contents/*.dylib")
# Frameworks contains outputs from the previous link. Discover replacements
# from the freshly staged executable/plugins, not stale copies of those outputs.
list(FILTER _liberty_bundle_libraries EXCLUDE REGEX "/Contents/Frameworks/")
set(_liberty_bundle_search_dirs ${RUNTIME_SEARCH_DIRS}
    "${APP_BUNDLE}/Contents/MacOS" "${APP_BUNDLE}/Contents/Frameworks")
foreach(_library IN LISTS _liberty_bundle_libraries)
    get_filename_component(_directory "${_library}" DIRECTORY)
    list(APPEND _liberty_bundle_search_dirs "${_directory}")
endforeach()
list(REMOVE_DUPLICATES _liberty_bundle_search_dirs)
# Keep explicitly staged libraries where the application's plugin/Vulkan
# loaders expect them. BundleUtilities otherwise rewrites their references to
# Frameworks even when its copy flag says the library is already embedded.
function(gp_item_default_embedded_path_override item path_variable)
    get_filename_component(_name "${item}" NAME)
    foreach(_directory MacOS Resources/vulkan/lib)
        if(EXISTS "${APP_BUNDLE}/Contents/${_directory}/${_name}")
            set(${path_variable} "@executable_path/../${_directory}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()
include(BundleUtilities)
fixup_bundle("${APP_BUNDLE}" "${_liberty_bundle_libraries}" "${_liberty_bundle_search_dirs}")

# install_name_tool invalidates signatures. Sign embedded code inside-out;
# the target's existing POST_BUILD command signs the outer app afterward.
if(NOT SIGN_IDENTITY)
    set(SIGN_IDENTITY "-")
endif()
file(GLOB_RECURSE _liberty_bundle_libraries "${APP_BUNDLE}/Contents/*.dylib")
foreach(_library IN LISTS _liberty_bundle_libraries)
    if(NOT IS_SYMLINK "${_library}")
        execute_process(COMMAND /usr/bin/codesign --force --sign "${SIGN_IDENTITY}" "${_library}"
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
endforeach()
