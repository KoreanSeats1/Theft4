set(REXKERNEL_SOURCES
    kernel_init.cpp

    # XAM subsystem - provides __imp__Xam* symbols
    xam/xam_module.cpp
    xam/xam_avatar.cpp
    xam/xam_content.cpp
    xam/xam_content_aggregate.cpp
    xam/xam_content_device.cpp
    xam/xam_debug.cpp
    xam/xam_enum.cpp
    xam/xam_info.cpp
    xam/xam_input.cpp
    xam/xam_locale.cpp
    xam/xam_msg.cpp
    xam/xam_net.cpp
    xam/xam_notify.cpp
    xam/xam_nui.cpp
    xam/xam_party.cpp
    xam/xam_task.cpp
    xam/xam_ui.cpp
    xam/xam_user.cpp
    xam/xam_video.cpp
    xam/xam_voice.cpp
    xam/xam_misc.cpp
    xam/apps/xam_app.cpp
    xam/apps/xgi_app.cpp
    xam/apps/xlivebase_app.cpp
    xam/apps/xmp_app.cpp

    # XBDM subsystem - provides __imp__Xbdm* symbols
    xbdm/xbdm_module.cpp
    xbdm/xbdm_misc.cpp

    # xboxkrnl exports - provides __imp__ symbols for generated code
    # xboxkrnl/cert_monitor.cpp    # TODO: Translate JIT methods for AOT
    # xboxkrnl/debug_monitor.cpp   # TODO: Translate JIT methods for AOT
    xboxkrnl/xboxkrnl_crypt.cpp
    xboxkrnl/xboxkrnl_debug.cpp
    xboxkrnl/xboxkrnl_error.cpp
    xboxkrnl/xboxkrnl_hal.cpp
    xboxkrnl/xboxkrnl_hid.cpp
    xboxkrnl/xboxkrnl_io.cpp
    xboxkrnl/xboxkrnl_io_info.cpp
    xboxkrnl/xboxkrnl_memory.cpp
    xboxkrnl/xboxkrnl_misc.cpp
    xboxkrnl/xboxkrnl_module.cpp
    xboxkrnl/xboxkrnl_modules.cpp
    xboxkrnl/xboxkrnl_ob.cpp
    xboxkrnl/xboxkrnl_rtl.cpp
    xboxkrnl/xboxkrnl_strings.cpp
    xboxkrnl/xboxkrnl_threading.cpp
    # xboxkrnl/xboxkrnl_usbcam.cpp  # TODO: lol eventually.
    xboxkrnl/xboxkrnl_video.cpp
    xboxkrnl/xboxkrnl_xconfig.cpp
    xboxkrnl/xboxkrnl_audio.cpp
    xboxkrnl/xboxkrnl_audio_xma.cpp

    crt/heap.cpp
    crt/file.cpp
    crt/memory.cpp
    crt/string.cpp
    crt/threading.cpp
)

if(REXGLUE_HEADLESS_KERNEL)
    # Retain the real filesystem, memory, threading, loader and XAM services.
    # These four units require concrete media/desktop-UI implementations.
    # Their exports must stop the experiment, never pretend to succeed.
    set(_headless_unavailable_sources
        xam/xam_input.cpp xam/xam_ui.cpp
        xboxkrnl/xboxkrnl_audio.cpp xboxkrnl/xboxkrnl_audio_xma.cpp)
    if(REXGLUE_HEADLESS_RENDER_AUDIO)
        list(REMOVE_ITEM _headless_unavailable_sources xboxkrnl/xboxkrnl_audio.cpp)
        # The complete decoder depends on the desktop audio/FFmpeg target. Use
        # a deterministic silent XMA service during embedded bring-up so the
        # guest-visible buffer protocol remains live without that dependency.
        list(REMOVE_ITEM _headless_unavailable_sources xboxkrnl/xboxkrnl_audio_xma.cpp)
        list(REMOVE_ITEM REXKERNEL_SOURCES xboxkrnl/xboxkrnl_audio_xma.cpp)
        list(APPEND REXKERNEL_SOURCES xboxkrnl/xboxkrnl_audio_xma_headless.cpp)
    endif()
    if(REXGLUE_HEADLESS_INPUT)
        list(REMOVE_ITEM _headless_unavailable_sources xam/xam_input.cpp)
    endif()
    set(HEADLESS_EXPORT_GUARDS "")
    foreach(_source IN LISTS _headless_unavailable_sources)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${_source}")
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${_source}" _export_source)
        string(REGEX MATCHALL "REX_EXPORT[A-Z_]*\\(__imp__[A-Za-z0-9_]+" _exports "${_export_source}")
        foreach(_export IN LISTS _exports)
            string(REGEX REPLACE "^REX_EXPORT[A-Z_]*\\(" "" _name "${_export}")
            string(APPEND HEADLESS_EXPORT_GUARDS "REX_HEADLESS_UNAVAILABLE(${_name})\n")
        endforeach()
    endforeach()
    list(REMOVE_ITEM REXKERNEL_SOURCES ${_headless_unavailable_sources})
    configure_file(headless_exports.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/headless_exports.cpp" @ONLY)
    target_sources(rexruntime PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/headless_exports.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../ui/display_config.cpp")
    target_compile_definitions(rexruntime PRIVATE REXGLUE_HEADLESS_KERNEL=1)
endif()

list(TRANSFORM REXKERNEL_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
target_sources(rexruntime PRIVATE ${REXKERNEL_SOURCES})

target_link_libraries(rexruntime PRIVATE o1heap rexcore rexfilesystem)
if(NOT REXGLUE_HEADLESS_KERNEL)
    target_link_libraries(rexruntime PRIVATE rexui rexaudio rexinput imgui)
endif()
