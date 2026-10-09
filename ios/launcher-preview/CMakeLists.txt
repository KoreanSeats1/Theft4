cmake_minimum_required(VERSION 3.25)
project(Theft4LauncherPreview LANGUAGES C OBJC)
set(mod_resources ../Theft4/Mods/CustomTimeCycle/timecyc.dat
    ../Theft4/Mods/CustomTimeCycle/preview.jpg
    ../Theft4/Mods/CustomTimeCycle/metadata.plist)
set_source_files_properties(${mod_resources} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/Mods/CustomTimeCycle")
# Isolated visual test harness; never links the game or touches its container.
add_executable(Theft4LauncherPreview MACOSX_BUNDLE main.m
    ../Theft4/Theft4FrameTimeView.m ../Theft4/Theft4LauncherView.m ../Theft4/Theft4CityView.m
    ${mod_resources})
target_compile_definitions(Theft4LauncherPreview PRIVATE THEFT4_DIRECT_METAL_DEFAULT=1)
target_include_directories(Theft4LauncherPreview PRIVATE ../Theft4 ../bridge)
target_compile_options(Theft4LauncherPreview PRIVATE -fobjc-arc)
target_link_libraries(Theft4LauncherPreview PRIVATE
    "-framework UIKit" "-framework QuartzCore" "-framework Metal" "-framework SceneKit" "-framework CoreGraphics")
set_target_properties(Theft4LauncherPreview PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.theft4.launcher-preview"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
