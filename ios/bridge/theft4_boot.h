#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// One-shot loader preparation. Call on a background queue; callbacks run on
// that queue. No UI or core-handle access is permitted from the callback.
typedef void (*theft4_boot_event_fn)(void* context, const char* event);
// Call on the host main thread before starting the loader worker.
int theft4_configure_boot_diagnostics(void);
// Imports a raw STFS title-update package or default.xexp selected through the
// iOS Files picker. The update is validated against the existing base game and
// installed below game_directory. Call on a background queue.
// Returns zero on success and writes a short user-facing result to message.
int theft4_install_title_update(const char* game_directory, const char* update_source,
                                char* message, size_t message_capacity);

// Validates a transferred extracted base game before the app lets the user
// select a title update. This only reads the selected directory.
int theft4_validate_base_game(const char* game_directory, char* message,
                              size_t message_capacity);
// Validates an already-installed base/TU8 pair without writing to it.
int theft4_validate_installed_game(const char* game_directory, char* message,
                                   size_t message_capacity);
int theft4_prepare_game(const char* game_directory, const char* support_directory,
                       theft4_boot_event_fn event, void* context);
// Experimental one-shot execution. Call off the main thread after diagnostics
// setup, with no other runtime active. Missing media/UI exports stop the app.
int theft4_start_game(const char* game_directory, const char* support_directory,
                     theft4_boot_event_fn event, void* context);
#ifdef __cplusplus
}
#endif
