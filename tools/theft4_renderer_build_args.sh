# Sourced by the two zsh iOS build entry points. Never inherit a stale CMake
# renderer selection: 0.3's default is the live direct Metal backend.
renderer_build_args=()
case "${THEFT4_RENDERER:-metal}" in
  metal)
    metal_game_dir="${THEFT4_METAL_LIBRARIES:-}"
    metal_host_dir="${THEFT4_METAL_HOST_LIBRARIES:-${metal_game_dir}/Host}"
    if [[ -z "$metal_game_dir" || ! -f "$metal_game_dir/manifest.tsv" ||
          ! -f "$metal_host_dir/HOST_SHADER_MANIFEST.json" ]]; then
      print -u2 "Theft4 0.3 requires offline iPhoneOS Metal shaders."
      print -u2 "Set THEFT4_METAL_LIBRARIES and, if separate, THEFT4_METAL_HOST_LIBRARIES."
      print -u2 "See docs/IOS_RELEASE_BUILD.md; renderer selection will not fall back silently."
      exit 66
    fi
    renderer_build_args=(
      -DTHEFT4_DIRECT_METAL_BACKEND=ON
      -DTHEFT4_DIRECT_METAL_DEFAULT=ON
      -DTHEFT4_METAL_LIBRARIES="$metal_game_dir"
      -DTHEFT4_METAL_HOST_LIBRARIES="$metal_host_dir"
    )
    ;;
  vulkan)
    renderer_build_args=(-DTHEFT4_DIRECT_METAL_BACKEND=OFF -DTHEFT4_DIRECT_METAL_DEFAULT=OFF)
    ;;
  *) print -u2 "THEFT4_RENDERER must be metal or vulkan."; exit 64 ;;
esac
# Normal builds omit the private game-asset draw recorder. Performance timing
# tools remain available when the user turns Retail Mode off.
renderer_source_revision="$(git rev-parse --short=8 HEAD 2>/dev/null || print development)"
renderer_build_args+=(
  -DTHEFT4_NATIVE_METAL_CAPTURE=OFF
  -DTHEFT4_RENDER_SOURCE_REVISION="$renderer_source_revision"
  -DTHEFT4_SUSTAINED_EXECUTION="${THEFT4_SUSTAINED_EXECUTION:-ON}"
)
