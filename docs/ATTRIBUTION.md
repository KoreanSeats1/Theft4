# Third-party projects and attribution

Theft4 is an integration and porting effort built on substantial public
open-source work. This page highlights the major projects involved; it is not a
replacement for the copyright and license notices within each repository or
submodule.

## Foundation

- [LibertyRecomp](https://github.com/OZORDI/LibertyRecomp) is the direct project
  foundation and provides the GTA IV-specific recompilation, runtime integration,
  installer, and renderer work from which Theft4 was derived.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) provides the Xbox 360
  ahead-of-time translation and compatibility-runtime architecture. This tree
  currently uses the LibertyRecomp/ReXGlue integration checked into `glue/`.
- [Xenia](https://github.com/xenia-project/xenia) provides foundational Xbox 360
  research and implementations used throughout the ReXGlue lineage.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) pioneered much of the
  modern Xbox 360 static-recompilation approach that inspired ReXGlue.

## Graphics and media

- [XenosRecomp](https://github.com/sonicnext-dev/XenosRecomp) provides Xbox 360
  shader analysis and translation used by the LibertyRecomp graphics path.
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK) translates Vulkan calls to
  Metal on Apple platforms.
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross),
  [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools), and
  [glslang](https://github.com/KhronosGroup/glslang) support shader translation
  and validation.
- [FFmpeg](https://ffmpeg.org/) and [SDL](https://www.libsdl.org/) are used by
  existing runtime/media and platform layers. The exact iOS audio closure is
  still under development.

## iOS reference work

- [XeniOS](https://github.com/xenios-jp/XeniOS) was built and tested separately
  as a behavioral reference for Xbox 360 execution and Apple-platform graphics.
  Its emulator/JIT is not embedded in Theft4, and it is not Theft4's CPU backend.

## Dependency licenses

The root project is distributed under GPL-3.0. Each submodule or vendored
dependency remains governed by its own license and notices. Consult `.gitmodules`,
the dependency directories, and their upstream repositories before redistributing
binaries. No project credit implies endorsement of Theft4.
