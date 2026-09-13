# Building Liberty Recompiled

This guide covers contributor setup and native desktop builds. Dependency sources for
all platforms and tools are fetched by the setup command. Platform SDKs and compilers
are installed separately using the prerequisites below.

## 1. Install Prerequisites

Use **Git, Python 3.10+, CMake 3.29+, Ninja, and Clang 18+** with a C++23-capable
standard library. CMake 4 is supported; do not downgrade a distro's system CMake.

### Windows

Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang Compiler
for Windows**, and **C++ CMake tools for Windows**. Install Python and Git for Windows.
Use a Visual Studio developer terminal and check `clang-cl --version` and
`cmake --version` against the minimum versions above. For ARM64, also install the
ARM64/ARM64EC C++ build tools and use an ARM64 developer terminal.

### Linux

For Arch Linux:

```bash
sudo pacman -Syu --needed base-devel git python cmake ninja clang lld pkgconf curl zip unzip \
  autoconf automake libtool gtk3 libx11 libxrandr libxcursor libxi libxinerama libxext \
  libxkbcommon wayland wayland-protocols libdecor alsa-lib libpulse dbus libusb \
  openssl vulkan-icd-loader vulkan-headers nasm
```

For Ubuntu/Debian, install the corresponding development packages:

```bash
sudo apt update
sudo apt install git python3 cmake ninja-build clang lld build-essential pkg-config \
  curl zip unzip autoconf automake libtool libgtk-3-dev libx11-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxinerama-dev libxext-dev libxkbcommon-dev libwayland-dev \
  wayland-protocols libdecor-0-dev libasound2-dev libpulse-dev libdbus-1-dev \
  libusb-1.0-0-dev libssl-dev libvulkan-dev nasm
```

Check the installed versions. Older distro releases need a newer CMake/LLVM installation
on PATH; the bundled SDK requires Clang 18 or newer. Install the Vulkan driver for your GPU.

### macOS

Install Xcode and select its developer directory. With Homebrew:

```bash
brew install cmake ninja pkg-config llvm openssl@3 vulkan-loader
```

Compiler discovery honors explicit `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER` overrides
and `CC`/`CXX`, then looks in `brew --prefix llvm`, then PATH. It supports both Homebrew
installation prefixes. The desktop macOS application uses system libraries and the
bundled RexGlue sources; its preset does not use vcpkg.

The LunarG Vulkan SDK is an alternative source of the Vulkan loader. Set `VULKAN_SDK`
or `REX_VULKAN_SDK` to its macOS directory if it is installed in a custom location.

## 2. Clone and Set Up

```bash
git clone https://github.com/OZORDI/LibertyRecomp.git
cd LibertyRecomp
python3 tools/setup_repo.py
```

On Windows, use `py -3 tools/setup_repo.py`. The `update_submodules.bat` and
`./update_submodules.sh` wrappers run the same helper.

Setup initializes **every pinned submodule and nested submodule**, including
XenosRecomp, its shader compiler dependencies, LLVM/libc++, and console tool sources.
Initial submodule downloads use shallow history while checking out the complete source
at the recorded commit. To work on a dependency's history later, run
`git -C <dependency-path> fetch --unshallow` if that dependency is shallow.

Setup also applies the reviewed dependency patches and bootstraps the bundled vcpkg.
It can be rerun after an interrupted download. It does not pull the main repository,
install OS packages, or change global Git settings.

A recursive clone is supported too; run setup afterward to apply the patches. GitHub's
**Download ZIP** omits submodule contents, so use Git for development.

Check a prepared checkout offline without changing it:

```bash
python3 tools/setup_repo.py --check
```

### Updating an existing checkout

```bash
git -c submodule.recurse=false pull --ff-only
python3 tools/setup_repo.py
```

The explicit non-recursive pull lets setup coordinate dependency pin changes with our
source patches. Setup records managed patch state in local Git metadata and migrates
recognized old patch versions, including the earlier local XenosRecomp revision. It
preserves local branches and unrelated files. If an affected file has an unrecognized
edit or staged changes, setup stops with its path so you can preserve/review it first.

If you previously copied a dependency from a ZIP or MediaFire, keep that unversioned
folder as a backup outside its expected submodule path, then rerun setup. The helper
will not overwrite a nonempty unversioned directory. Do not use `git clean`, forced
submodule updates, or `git submodule update --remote` as a repair step.

### CMake 4 / Arch Linux compatibility

Presets supply the legacy policy minimum for directly included dependencies. The
checked-in vcpkg overlay triplets also pass `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` into
individual port configure commands, including their debug/release builds. A setting
only in the top-level project does not configure those separate processes.

If an old build directory still fails, configure a new build directory rather than
changing system packages. Report the failing port and its complete configure log.

## 3. Build the Desktop Application

| Host / target | Release preset |
|---|---|
| Windows x64 | `x64-Clang-Release` |
| Windows ARM64 | `arm64-Clang-Release` |
| Linux x64 | `linux-release` |
| Linux ARM64 | `linux-arm64-release` |
| macOS Apple Silicon | `macos-release` |
| macOS Intel | `macos-release` with `-DCMAKE_OSX_ARCHITECTURES=x86_64` |

For example, on Linux x64:

```bash
cmake --preset linux-release
cmake --build --preset linux-release --target LibertyRecomp
```

On macOS:

```bash
cmake --preset macos-release
cmake --build --preset macos-release --target LibertyRecomp
open "out/build/macos-release/LibertyRecomp/Liberty Recompiled.app"
```

For Intel macOS, add `-DCMAKE_OSX_ARCHITECTURES=x86_64` to the configure command.
For Windows, substitute the appropriate Windows preset in the configure/build commands.
The Windows and Linux presets set `VCPKG_ROOT` to the bundled checkout automatically.

Debug and RelWithDebInfo presets are also available (`cmake --list-presets`). Build
`LibertyRecomp` directly; its required libraries are dependencies of that target.
The macOS consumer does not expose a separate `LibertyRecompLib` target.

## 4. Game Files and Other Platforms

The generated PPC sources and shader caches are checked in. **Normal desktop builds
do not require copying an XEX/RPF into the source tree or regenerating game code.**
Provide your own Xbox 360 game files through the application installer to run the game.
See [the dumping guide](DUMPING-en.md) for obtaining files from your copy.

Code-generation work requires the matching executable and documented recompiler inputs;
keep those local. Embedded builds (iOS, PS4, Switch) additionally require a local game
payload at packaging time. Android supports runtime asset selection. See
[platform setup](PLATFORM_SETUP.md) and `tools/local_game_payload/README.md` for SDK and
payload requirements. Fetching all tool sources does not install those platform SDKs.

## 5. Shader Tools

Run repository setup before configuring XenosRecomp as a standalone tool:

```bash
python3 tools/setup_repo.py
cmake -S tools/XenosRecomp -B out/build/xenosrecomp -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DXENOS_RECOMP_GTA4=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build out/build/xenosrecomp --target XenosRecomp
```

Shader development and cache regeneration are separate from normal application builds.
See [the shader pipeline guide](SHADER_PIPELINE.md) for the extraction/conversion flow.

## 6. Project Structure

```
LibertyRecomp/
├── LibertyRecomp/          # Main application code
│   ├── api/                # Public API headers (RAGE, Havok, stdx)
│   ├── apu/                # Audio processing (XMA decoder, embedded player)
│   ├── cpu/                # CPU emulation / guest thread / PPC context
│   ├── gpu/                # Graphics / video rendering / shaders / upscaling
│   ├── hid/                # Human input devices (SDL, DualSense)
│   ├── install/            # Installer, shader converter, RPF/ISO extraction
│   ├── kernel/             # Kernel imports, memory, VFS, XAM, file I/O
│   ├── locale/             # Localization / language support
│   ├── mod/                # Mod loader and INI file parsing
│   ├── os/                 # Platform-specific code (win32, linux, macos, ios, android, ps4, switch)
│   ├── patches/            # GTA IV specific patches (input, camera, FPS, audio)
│   ├── runtime/            # Rex runtime adapters (sync, threads, xobject)
│   ├── ui/                 # ImGui-based UI (menus, overlays, installer wizard)
│   ├── user/               # User config, saves, achievements, registry
│   └── utils/              # Utility classes (bit_stream, ring_buffer)
├── LibertyRecompLib/       # Recompiled game code
│   ├── config/             # Recompiler configuration (TOML, switch tables)
│   ├── shader/             # Shader cache (generated)
│   └── private/            # Game files (not in repo)
├── tools/                  # Development tools
│   # Xbox 360 PPC recompilation is provided by rexglue (graine SDK) under glue/
│   ├── XenosRecomp/        # Xbox 360 shader recompiler
│   ├── rage_fxc_extractor/ # RAGE FXC shader extractor
│   └── bc_diff/            # Binary comparison tool
└── docs/                   # Documentation
```

## 7. Decompiled Subsystems

The `patches/` directory contains both high-level game hooks and fully decompiled RAGE engine subsystems reimplemented in clean C++. Decompiled patches were produced from IDA Hex-Rays pseudocode cross-referenced against the PPC recomp scaffolds generated by rexglue (graine SDK).

### Engine Core

| File | Subsystem | Description |
|-|-|-|
| `grm_setup_patches.cpp` | `rage::grmSetup` | Graphics setup singleton: device init/teardown, frame timing EMA, v-sync state machine, vtable dispatch thunks |
| `scene_tick_patches.cpp` | Scene tick | Entity update chain: streaming subsystem update, 24-slot entity tick loop, world update, entity finalize, sector dirty marking |
| `frontend_state_hooks.cpp` | Front-end FSM | Episode selection / title screen state machine with PC keyboard input injection for controller-less operation |
| `postfx_hooks.cpp` | PostFX | Native post-processing intercept: bloom/motion-blur/DOF disable when custom effects active, sun direction extraction |

### Audio

| File | Subsystem | Description |
|-|-|-|
| `audio_pool.cpp` | `audVoicePool` | Fixed-size voice pool with per-frame iteration, vtable dispatch, and voice control block (VCB) state tracking |
| `aud_sound_manager.cpp` | `audSoundManager` | Priority-based voice budget: distance sorting, radix sort, voice allocation, playback cursor advancement, request queue servicing |
| `audio_patches.cpp` | Audio hooks | XMA decoder integration, audio thread management |

### Game Systems

| File | Subsystem | Description |
|-|-|-|
| `fps_patches.cpp` | Frame rate | Unlocked frame rate, delta time fixes |
| `camera_patches.cpp` | Camera | Camera system patches for widescreen and input |
| `aspect_ratio_patches.cpp` | Aspect ratio | Widescreen / ultrawide support |
| `gta4_input_patches.cpp` | Input | Keyboard/mouse input remapping from Xbox controller layout |
| `gta4_ds4_patches.cpp` | DualSense | PS5 DualSense controller support with haptics |
| `gta4_motion_patches.cpp` | Motion | Gyro/accelerometer input for supported controllers |
| `memcpy_patches.cpp` | Memory | Optimized memory copy paths for host architecture |
| `loading_patches.cpp` | Loading | Loading screen event system |
| `misc_patches.cpp` | Miscellaneous | Various small fixes and workarounds |

### UI / Frontend

| File | Subsystem | Description |
|-|-|-|
| `MainMenuTask_patches.cpp` | Main menu | Options menu integration, menu state hooks |
| `SaveDataTask_patches.cpp` | Save data | Storage device alert redirection for PC |
| `TitleTask_patches.cpp` | Title screen | Title screen outro timing, language selection |
| `text_patches.cpp` | Text | Text rendering and localization patches |
| `video_patches.cpp` | Video | Video playback hooks |
| `frontend_listener.cpp` | Frontend events | Frontend event listener bridge |

## 8. Development Tools

### liberty-decomp MCP Server

The project includes an MCP (Model Context Protocol) server that provides AI-assisted decompilation tooling. It exposes the full GTA IV binary analysis database (31,782 functions, 3,332 RTTI classes, 51,494 symbols) through structured tool calls.

**Available tools:**

| Tool | Purpose |
|-|-|
| `get_function_info` | Address, size, class, vtable slot, hook status for any function |
| `get_function_pseudocode` | IDA Hex-Rays decompiled C for ~32K functions |
| `get_function_recomp` | PPC recomp scaffold with annotated addresses |
| `get_class_context` | RTTI inheritance, vtable layout, field clusters, debug strings |
| `search_symbols` | Substring search across functions, symbols, and RTTI classes |
| `get_string_refs` | Find strings referenced by a function, or find functions that reference a string |
| `resolve_address` | Identify any address: symbol, string content, vtable class, nearest symbol |
| `find_callees` / `find_callers` | Call graph traversal (96K forward edges, 139K reverse edges) |
| `suggest_unimplemented_func` | Pick a random un-hooked function with full context card |
| `suggest_file_placement` | Determine which patch file a new hook belongs in |
| `write_source_file` | Surgical editor for patch files |

**Workflow for decompiling a new function:**

1. Use `suggest_unimplemented_func` or `search_symbols` to find a target
2. Call `get_function_info` to check metadata and hook status
3. Read `get_function_pseudocode` and `get_function_recomp` side by side
4. Use `resolve_address` on every unknown `lbl_` address in the scaffold
5. Use `get_class_context` for any classes the function touches
6. Call `suggest_file_placement` to determine the correct patch file
7. Write the clean C++ reimplementation with `write_source_file`

### Codegen (Recompiler)

The codegen step converts the Xbox 360 PPC binary into x86/ARM C++ source. It requires Homebrew LLVM (not Apple Clang) on macOS:

```bash
cmake -B build-codegen -S glue/rexglue-sdk-main -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DREXGLUE_USE_VULKAN=ON \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_CXX_FLAGS="-I$(pwd)/thirdparty/o1heap -nostdlib++" \
  -DCMAKE_EXE_LINKER_FLAGS="-L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++ -lc++"
```

**Codegen statistics:** 38,546 functions discovered, 38,268 recompiled, 278 excluded (246 imports + 32 rexcrt native replacements).

## 9. Known Issues

### Runtime State

- The game boots through the title screen and reaches the main menu
- Audio subsystem initializes and loads all RPF archives
- Save state machine runs (content enumeration, save manager)
- Particle emitter registration storm (~4,608 allocators via `sub_825BF8A8`) uses fallback allocator when heap TLS is not yet configured; this is one-time init and not a deadlock
- The main world init function (`sub_821200D0`) enters but takes extended time to complete during first boot

### Build Notes

- Windows/Linux presets set the bundled `VCPKG_ROOT`; macOS uses the system dependencies listed above
- Codegen requires Homebrew LLVM on macOS; Apple Clang does not support the required flags
- The `RelWithDebInfo` configuration is recommended for iterative development
- CRT functions hooked via rexcrt (32 functions) must appear in both `[hooks]` and `[functions]` sections of the recompiler config to force proper code splitting
