#!/bin/zsh
set -euo pipefail

usage() {
  print -u2 "Usage: $0 [expected-version]"
  print -u2 "Environment: THEFT4_MOLTENVK_IOS_LIB_DIR, DEVELOPER_DIR, CMAKE_BIN, THEFT4_BUILD_JOBS"
}

if (( $# > 1 )); then
  usage
  exit 64
fi

repo_root="${0:A:h:h}"
cd "$repo_root"

if [[ "$(uname -s)" != Darwin ]]; then
  print -u2 "The iOS release pipeline requires macOS and full Xcode."
  exit 69
fi

developer_dir="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
cmake_bin="${CMAKE_BIN:-$(command -v cmake || true)}"
build_jobs="${THEFT4_BUILD_JOBS:-4}"
library_dir="${THEFT4_MOLTENVK_IOS_LIB_DIR:-${repo_root:h}/XeniOS/build-ios-xcode/obj/iOS/Release}"
shader_dir="${THEFT4_XENIOS_GENERATED_SHADER_DIR:-${library_dir:h:h:h}/generated}"
plist_template="$repo_root/ios/Theft4/Info.plist.in"
app_path="$repo_root/out/build/ios-device-release/theft4/Release/Theft4.app"

if [[ -z "$cmake_bin" || ! -x "$cmake_bin" ]]; then
  print -u2 "CMake was not found. Set CMAKE_BIN to the executable path."
  exit 69
fi
if [[ ! -d "$developer_dir" || ! -x "$developer_dir/usr/bin/xcodebuild" ]]; then
  print -u2 "Full Xcode was not found at: $developer_dir"
  exit 69
fi
if [[ ! "$build_jobs" =~ '^[1-9][0-9]*$' ]]; then
  print -u2 "THEFT4_BUILD_JOBS must be a positive integer."
  exit 64
fi

required_libraries=(
  libMoltenVK.a
  libMoltenVK_ShaderConverter.a
  libMoltenVK_Common.a
  libspirv-cross.a
  libSPIRV-Tools.a
)
for library in $required_libraries; do
  if [[ ! -f "$library_dir/$library" ]]; then
    print -u2 "Missing iPhoneOS ARM64 dependency: $library_dir/$library"
    exit 66
  fi
done

version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist_template")"
build_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$plist_template")"
if [[ -n "${1:-}" && "$version" != "$1" ]]; then
  print -u2 "Expected release $1, but Info.plist.in contains $version."
  exit 65
fi

export DEVELOPER_DIR="$developer_dir"
export PATH="${cmake_bin:h}:$PATH"

print "Building Theft4 $version ($build_version) for public iOS sideloading"
"$cmake_bin" --preset ios-device-release \
  -DREXGLUE_RUNTIME_ONLY=ON \
  -DREXGLUE_HEADLESS_KERNEL=ON \
  -DTHEFT4_BUILD_GAME_CODE=ON \
  -DTHEFT4_ENABLE_GAME_STARTUP=ON \
  -DTHEFT4_COMPILE_GTA4_NATIVE_BACKEND=ON \
  -DTHEFT4_ENABLE_GTA4_NATIVE_BACKEND=ON \
  -DTHEFT4_SIGN_DEVICE=OFF \
  -DTHEFT4_PUBLIC_BUILD=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DTHEFT4_XENIOS_IOS_LIB_DIR="$library_dir" \
  -DTHEFT4_XENIOS_GENERATED_SHADER_DIR="$shader_dir"
"$cmake_bin" --build --preset theft4-device-release --parallel "$build_jobs"
python3 "$repo_root/tests/ios/verify_release_build.py" "$repo_root/out/build/ios-device-release"
"$repo_root/tools/package_ios_ipa.sh" "$app_path" "$repo_root/dist"
