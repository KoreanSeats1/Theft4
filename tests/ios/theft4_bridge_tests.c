#include "theft4_core.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); return 1; } } while (0)
static unsigned event_count;
static void log_event(void *data, const char *event) { (void)data; (void)event; ++event_count; }
static void *wrong_thread(void *context) {
    return (void *)(intptr_t)theft4_core_activate((theft4_core *)context);
}
int main(void) {
    theft4_core *core = NULL;
    theft4_core_config config = {sizeof(config), THEFT4_CORE_ABI_VERSION, "/resources", "/support", log_event, NULL};
    CHECK(theft4_core_api_version() == THEFT4_CORE_ABI_VERSION);
    CHECK(theft4_core_create(NULL, &core) == THEFT4_INVALID_ARGUMENT && core == NULL);
    CHECK(theft4_core_create(&config, NULL) == THEFT4_INVALID_ARGUMENT);
    config.abi_version++;
    CHECK(theft4_core_create(&config, &core) == THEFT4_ABI_MISMATCH && core == NULL);
    config.abi_version--;
    config.struct_size = 0;
    CHECK(theft4_core_create(&config, &core) == THEFT4_INVALID_ARGUMENT);
    config.struct_size = sizeof(config);
    config.support_directory = "relative";
    CHECK(theft4_core_create(&config, &core) == THEFT4_INVALID_ARGUMENT);
    config.support_directory = "/support";
    CHECK(theft4_core_activate(NULL) == THEFT4_INVALID_ARGUMENT);
    CHECK(theft4_core_destroy(NULL) == THEFT4_OK);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        CHECK(theft4_core_create(&config, &core) == THEFT4_OK && core);
        theft4_core_snapshot snapshot = {0};
        CHECK(theft4_core_get_snapshot(core, &snapshot) == THEFT4_INVALID_ARGUMENT);
        snapshot.struct_size = sizeof(snapshot);
        CHECK(theft4_core_get_snapshot(core, &snapshot) == THEFT4_ABI_MISMATCH);
        snapshot.abi_version = THEFT4_CORE_ABI_VERSION;
        CHECK(theft4_core_get_snapshot(core, &snapshot) == THEFT4_OK);
        CHECK(snapshot.state == THEFT4_READY && snapshot.host_page_bytes > 0);
        CHECK(!snapshot.guest_runtime_initialized && !snapshot.writable_executable_memory_supported);
        CHECK(strstr(snapshot.platform, "ios") && snapshot.core_version[0]);
        CHECK(theft4_core_activate(core) == THEFT4_OK);
        unsigned before = event_count;
        CHECK(theft4_core_activate(core) == THEFT4_OK && event_count == before);
        pthread_t thread;
        void *thread_result;
        CHECK(pthread_create(&thread, NULL, wrong_thread, core) == 0);
        CHECK(pthread_join(thread, &thread_result) == 0);
        CHECK((intptr_t)thread_result == THEFT4_WRONG_THREAD);
        CHECK(theft4_core_pause(core) == THEFT4_OK);
        CHECK(theft4_core_get_snapshot(core, &snapshot) == THEFT4_OK && snapshot.state == THEFT4_PAUSED);
        CHECK(theft4_core_activate(core) == THEFT4_OK);
        CHECK(theft4_core_memory_warning(core) == THEFT4_OK);
        CHECK(theft4_core_get_snapshot(core, &snapshot) == THEFT4_OK && snapshot.memory_warning_count == 1);
        CHECK(theft4_core_stop(core) == THEFT4_OK);
        before = event_count;
        CHECK(theft4_core_stop(core) == THEFT4_OK && event_count == before);
        CHECK(theft4_core_activate(core) == THEFT4_INVALID_STATE);
        CHECK(theft4_core_pause(core) == THEFT4_INVALID_STATE);
        CHECK(theft4_core_memory_warning(core) == THEFT4_INVALID_STATE);
        CHECK(theft4_core_destroy(core) == THEFT4_OK);
        core = NULL;
    }
    printf("PASS: C ABI validation, thread guard, and 100 lifecycle/teardown cycles (%u events)\n", event_count);
    return 0;
}
