#ifndef THEFT4_CORE_H
#define THEFT4_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring-up ABI, not yet the game-engine API. No guest runtime is started.
 * All calls, including destroy, must run on the main thread in M2.
 * Events are synchronous and borrowed for the callback duration. Callbacks
 * may read a snapshot, but must not mutate/destroy a handle or throw.
 * No callbacks occur after destroy returns.
 * Config strings are copied. User data must remain valid through destroy.
 * Never pass a destroyed handle; destroy(NULL) is safe.
 * Future guest work belongs on an owned worker, not this main-thread probe. */
#define THEFT4_CORE_ABI_VERSION 1u
typedef struct theft4_core theft4_core;
typedef int32_t theft4_result;
enum { THEFT4_OK = 0, THEFT4_INVALID_ARGUMENT = 1, THEFT4_ABI_MISMATCH = 2,
       THEFT4_INVALID_STATE = 3, THEFT4_WRONG_THREAD = 4, THEFT4_INTERNAL_ERROR = 5 };
enum { THEFT4_READY = 0, THEFT4_ACTIVE = 1, THEFT4_PAUSED = 2, THEFT4_STOPPED = 3 };
typedef void (*theft4_log_fn)(void *user_data, const char *event);

typedef struct theft4_core_config {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *resource_directory;
    const char *support_directory;
    theft4_log_fn log;
    void *user_data;
} theft4_core_config;

typedef struct theft4_core_snapshot {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t state;
    uint32_t memory_warning_count;
    uint64_t host_page_bytes;
    uint32_t writable_executable_memory_supported;
    uint32_t guest_runtime_initialized;
    char platform[48];
    char core_version[64];
} theft4_core_snapshot;

uint32_t theft4_core_api_version(void);
theft4_result theft4_core_create(const theft4_core_config *config, theft4_core **out_core);
theft4_result theft4_core_get_snapshot(const theft4_core *core, theft4_core_snapshot *out);
theft4_result theft4_core_activate(theft4_core *core);
theft4_result theft4_core_pause(theft4_core *core);
theft4_result theft4_core_memory_warning(theft4_core *core);
theft4_result theft4_core_stop(theft4_core *core);
theft4_result theft4_core_destroy(theft4_core *core);

#ifdef __cplusplus
}
#endif
#endif
