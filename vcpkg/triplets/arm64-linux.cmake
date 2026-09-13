# Preserve the pinned upstream triplet, adding legacy CMake compatibility.
include("${CMAKE_CURRENT_LIST_DIR}/../../thirdparty/vcpkg/triplets/community/arm64-linux.cmake")
list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_POLICY_VERSION_MINIMUM=3.5")
