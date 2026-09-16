cmake_minimum_required(VERSION 3.24)
project(Theft4MetalReplay LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD 20)
set(XENIOS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../../XeniOS" CACHE PATH "Public XeniOS checkout")
set(XENIOS_BUILD "${XENIOS_SOURCE}/build-ios-xcode" CACHE PATH "Matching built iOS XeniOS libraries")
set(REPLAY_TEAM "" CACHE STRING "Development signing team")
add_executable(Theft4MetalReplay MACOSX_BUNDLE main.mm)
target_sources(Theft4MetalReplay PRIVATE "${XENIOS_SOURCE}/src/xenia/base/cache_control_ios.cc")
target_include_directories(Theft4MetalReplay PRIVATE
  "${XENIOS_SOURCE}" "${XENIOS_SOURCE}/src" "${XENIOS_BUILD}"
  "${XENIOS_SOURCE}/third_party/fmt/include"
  "${XENIOS_SOURCE}/third_party/rapidjson/include"
  "${XENIOS_SOURCE}/third_party/metal-shader-converter/include"
  "${XENIOS_SOURCE}/third_party")
target_compile_definitions(Theft4MetalReplay PRIVATE NDEBUG IMGUI_DISABLE_OBSOLETE_FUNCTIONS
  IMGUI_DISABLE_DEFAULT_FONT IMGUI_USE_STB_SPRINTF METAL_SHADER_CONVERTER_AVAILABLE
  IR_RUNTIME_METALCPP XBYAK_NO_OP_NAMES XBYAK_ENABLE_OMITTED_OPERAND)
target_compile_options(Theft4MetalReplay PRIVATE -fobjc-arc)
# Reuse already-built public dependencies without changing the reference app.
file(GLOB replay_archives "${XENIOS_BUILD}/obj/iOS/Release/*.a")
if(NOT replay_archives)
  message(FATAL_ERROR "Build the matching XeniOS Release libraries first")
endif()
target_link_libraries(Theft4MetalReplay PRIVATE ${replay_archives}
  "-framework UIKit" "-framework Foundation" "-framework CoreFoundation"
  "-framework Metal" "-framework MetalKit" "-framework QuartzCore"
  "-framework AVFoundation" "-framework AudioToolbox" "-framework CoreAudio"
  "-framework GameController" "-framework Security" "-framework UniformTypeIdentifiers"
  "-framework CoreGraphics" "-framework CoreHaptics" "-framework CoreMotion"
  "-framework CoreVideo" "-framework IOSurface" "-framework OpenGLES"
  "-framework Accelerate" "-framework VideoToolbox" "-framework CoreMedia"
  iconv z bz2)
set(replay_dylibs)
foreach(name dxilconv metalirconverter)
  set(dylib "${XENIOS_BUILD}/bin/iOS/Release/XeniOS.app/Frameworks/lib${name}.dylib")
  target_link_libraries(Theft4MetalReplay PRIVATE "${dylib}")
  list(APPEND replay_dylibs "${dylib}")
endforeach()
target_link_options(Theft4MetalReplay PRIVATE "-Wl,-rpath,@executable_path/Frameworks"
  "-Wl,-dead_strip" "-Wl,-u,___clear_cache"
  # MoltenVK contains Objective-C categories not referenced as ordinary linker
  # symbols. Match XeniOS's app target so those selectors survive dead stripping.
  "-Wl,-force_load,${XENIOS_BUILD}/obj/iOS/Release/libMoltenVK.a")
set_target_properties(Theft4MetalReplay PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.theft4.metalreplay"
  XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${REPLAY_TEAM}"
  XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_EMBED_FRAMEWORKS "${replay_dylibs}"
  XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES)
