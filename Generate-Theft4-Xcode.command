#!/bin/zsh
set -euo pipefail

script_dir="${0:A:h}"
cd "$script_dir"

export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"

# Normal play uses the optimized build. Debug is an explicit developer choice.
build_configuration="${THEFT4_BUILD_CONFIGURATION:-Release}"
case "$build_configuration" in
  Release) device_preset=ios-device-release ;;
  Debug) device_preset=ios-device-debug ;;
  *) print -u2 "THEFT4_BUILD_CONFIGURATION must be Release or Debug."; exit 1 ;;
esac

if command -v cmake >/dev/null 2>&1; then
  cmake_bin="$(command -v cmake)"
elif [[ -x /Applications/CMake.app/Contents/bin/cmake ]]; then
  cmake_bin=/Applications/CMake.app/Contents/bin/cmake
else
  print -u2 "CMake 3.29 or newer is required. Install it, then run this file again."
  exit 1
fi

if [[ ! -x "$DEVELOPER_DIR/usr/bin/xcodebuild" ]]; then
  print -u2 "Full Xcode is required at $DEVELOPER_DIR."
  exit 1
fi

print "Initializing pinned public dependencies..."
python3 tools/setup_repo.py
python3 tools/setup_repo.py --check

xenios_root="${THEFT4_XENIOS_ROOT:-${script_dir:h}/XeniOS}"
xenios_lib_dir="${THEFT4_MOLTENVK_IOS_LIB_DIR:-$xenios_root/build-ios-xcode/obj/iOS/Release}"

required_graphics_libs=(
  libMoltenVK.a
  libMoltenVK_ShaderConverter.a
  libMoltenVK_Common.a
  libspirv-cross.a
  libSPIRV-Tools.a
)

missing_graphics=0
for library_name in "${required_graphics_libs[@]}"; do
  if [[ ! -f "$xenios_lib_dir/$library_name" ]]; then
    print -u2 "Missing graphics archive: $xenios_lib_dir/$library_name"
    missing_graphics=1
  fi
done

if (( missing_graphics )); then
  print -u2 ""
  print -u2 "The current bring-up uses public MoltenVK archives built by XeniOS."
  print -u2 "Build the XeniOS iOS reference checkout first, or set"
  print -u2 "THEFT4_MOLTENVK_IOS_LIB_DIR to a directory containing the five archives above."
  print -u2 "See docs/IOS_RELEASE_BUILD.md (graphics dependency prerequisite)."
  exit 1
fi

team_id="${1:-${THEFT4_DEVELOPMENT_TEAM:-}}"
configure_args=(
  --preset "$device_preset"
  -DREXGLUE_RUNTIME_ONLY=ON
  -DREXGLUE_HEADLESS_KERNEL=ON
  -DTHEFT4_BUILD_GAME_CODE=ON
  -DTHEFT4_ENABLE_GAME_STARTUP=ON
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
  -DTHEFT4_XENIOS_IOS_LIB_DIR="$xenios_lib_dir"
)

if [[ -n "$team_id" ]]; then
  configure_args+=(
    -DTHEFT4_SIGN_DEVICE=ON
    -DLIBERTY_IOS_DEVELOPMENT_TEAM="$team_id"
  )
else
  configure_args+=(-DTHEFT4_SIGN_DEVICE=OFF)
  print "No Apple team ID supplied; generating an unsigned Xcode project."
  print "Pass your team ID as the first argument to enable device signing."
fi

"$cmake_bin" "${configure_args[@]}"

project_path="$script_dir/out/build/$device_preset/LibertyRecomp-ALL.xcodeproj"
if [[ ! -d "$project_path" ]]; then
  print -u2 "CMake completed without producing the expected Xcode project: $project_path"
  exit 1
fi

print "Generated: $project_path"
print "Configuration: $build_configuration. Select the Theft4 scheme and your iPad."
if [[ "$build_configuration" == Release ]]; then
  print "For normal play, uncheck Run > Info > Debug executable in Edit Scheme,"
  print "or install the app and launch it from the iPad without Xcode attached."
fi
print "Game-file transfer and launch instructions: docs/IOS_RELEASE_BUILD.md"
open "$project_path"
