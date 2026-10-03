cmake_minimum_required(VERSION 3.24)
project(Theft4MetalLab LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ROOT "../../.." ABSOLUTE)
if(NOT METAL_LIBRARIES OR NOT IS_DIRECTORY "${METAL_LIBRARIES}")
  message(FATAL_ERROR "METAL_LIBRARIES must name the tested iOS shader directory")
endif()
add_executable(Theft4MetalLab MACOSX_BUNDLE app.mm
  "${ROOT}/ios/bridge/theft4_native_metal.mm"
  "${ROOT}/ios/bridge/theft4_metal_shader_catalog.cpp"
  "${ROOT}/ios/bridge/theft4_metal_shader_store.mm"
  "${ROOT}/ios/bridge/theft4_metal_resources.mm"
  "${ROOT}/ios/tests/direct_metal_probe.mm")
target_compile_options(Theft4MetalLab PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
set(METAL_SOURCE_REVISION "development" CACHE STRING "Source commit represented by this validation app")
target_compile_definitions(Theft4MetalLab PRIVATE "THEFT4_METAL_SOURCE_REVISION=\"${METAL_SOURCE_REVISION}\"")
target_include_directories(Theft4MetalLab PRIVATE "${ROOT}/ios/bridge" "${ROOT}/ios/tests"
  "${ROOT}/glue/rexglue-sdk-main/src/graphics/gta4_native")
target_link_libraries(Theft4MetalLab PRIVATE "-framework UIKit" "-framework Foundation"
  "-framework Metal" "-framework CoreGraphics" "-framework QuartzCore")
set_target_properties(Theft4MetalLab PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.lukebrosious.theft4.metallab"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
  XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "B9HG8W89FT"
  XCODE_ATTRIBUTE_SKIP_INSTALL NO
  XCODE_ATTRIBUTE_INSTALL_PATH "/Applications"
  XCODE_ATTRIBUTE_ENABLE_BITCODE NO)
if(NOT EXISTS "${METAL_LIBRARIES}/manifest.tsv")
  message(FATAL_ERROR "The offline Metal manifest must accompany the libraries")
endif()
file(GLOB libraries CONFIGURE_DEPENDS "${METAL_LIBRARIES}/*.metallib")
list(APPEND libraries "${METAL_LIBRARIES}/manifest.tsv")
target_sources(Theft4MetalLab PRIVATE ${libraries})
set_source_files_properties(${libraries} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/MetalShaders")
