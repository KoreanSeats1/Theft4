#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// One-shot loader preparation. Call on a background queue; callbacks run on
// that queue. No UI or core-handle access is permitted from the callback.
typedef void (*theft4_boot_event_fn)(void* context, const char* event);
// Call on the host main thread before starting the loader worker.
int theft4_configure_boot_diagnostics(void);
int theft4_prepare_game(const char* game_directory, const char* support_directory,
                       theft4_boot_event_fn event, void* context);
// Experimental one-shot execution. Call off the main thread after diagnostics
// setup, with no other runtime active. Missing media/UI exports stop the app.
int theft4_start_game(const char* game_directory, const char* support_directory,
                     theft4_boot_event_fn event, void* context);
#ifdef __cplusplus
}
#endif
