// Shared display settings used by the host window and Xbox video exports.
// Definitions moved unchanged from window.cpp so headless HLE does not link a window.
#include <rex/cvar.h>

REXCVAR_DEFINE_INT32(window_width, 0, "UI/Window",
                     "Startup window width in logical pixels (0 = use app default)")
    .range(0, 8192)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(window_height, 0, "UI/Window",
                     "Startup window height in logical pixels (0 = use app default)")
    .range(0, 8192)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(fullscreen, true, "UI/Window", "Start the window in fullscreen mode")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(monitor, 0, "UI/Window",
                     "Monitor index to display on (0 = default, 1 = primary, 2 = "
                     "second monitor, etc.)")
    .range(0, 16)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(video_mode_width, 1280, "Display", "Guest video mode width in pixels")
    .range(640, 0x0FFF)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(video_mode_height, 720, "Display", "Guest video mode height in pixels")
    .range(480, 0x0FFF)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(resolution, "", "Display",
                      "Common resolution preset for both guest video mode and startup window (for "
                      "example: 720p, 1080p, 1440p, 4k, 1280x720)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_DOUBLE(video_mode_refresh_rate, 60.0, "Display",
                      "Guest video mode refresh rate in Hz")
    .range(24.0, 240.0)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
