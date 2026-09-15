#include "theft4_core.h"
#include <rex/memory/utils.h>
#include <rex/version.h>
#include <pthread.h>
#include <cstdio>
#include <memory>
#include <string>

struct theft4_core {
    std::string resources;
    std::string support;
    theft4_log_fn log = nullptr;
    void *user_data = nullptr;
    uint32_t state = THEFT4_READY;
    uint32_t warnings = 0;
    void emit(const char *event) const { if (log) log(user_data, event); }
};

static theft4_result check(const theft4_core *core) {
    if (!pthread_main_np()) return THEFT4_WRONG_THREAD;
    return core ? THEFT4_OK : THEFT4_INVALID_ARGUMENT;
}

uint32_t theft4_core_api_version(void) { return THEFT4_CORE_ABI_VERSION; }

theft4_result theft4_core_create(const theft4_core_config *config, theft4_core **out_core) {
    if (!out_core) return THEFT4_INVALID_ARGUMENT;
    *out_core = nullptr;
    if (!pthread_main_np()) return THEFT4_WRONG_THREAD;
    if (!config || config->struct_size < sizeof(*config)) return THEFT4_INVALID_ARGUMENT;
    if (config->abi_version != THEFT4_CORE_ABI_VERSION) return THEFT4_ABI_MISMATCH;
    if (!config->resource_directory || config->resource_directory[0] != '/' ||
        !config->support_directory || config->support_directory[0] != '/')
        return THEFT4_INVALID_ARGUMENT;
    try {
        auto core = std::make_unique<theft4_core>();
        core->resources = config->resource_directory;
        core->support = config->support_directory;
        core->log = config->log;
        core->user_data = config->user_data;
        core->emit("core.created");
        *out_core = core.release();
        return THEFT4_OK;
    } catch (...) {
        return THEFT4_INTERNAL_ERROR;
    }
}

theft4_result theft4_core_get_snapshot(const theft4_core *core, theft4_core_snapshot *out) {
    if (auto result = check(core); result != THEFT4_OK) return result;
    if (!out || out->struct_size < sizeof(*out)) return THEFT4_INVALID_ARGUMENT;
    if (out->abi_version != THEFT4_CORE_ABI_VERSION) return THEFT4_ABI_MISMATCH;
    theft4_core_snapshot value{};
    value.struct_size = sizeof(value);
    value.abi_version = THEFT4_CORE_ABI_VERSION;
    value.state = core->state;
    value.memory_warning_count = core->warnings;
    value.host_page_bytes = rex::memory::page_size();
    value.writable_executable_memory_supported = rex::memory::IsWritableExecutableMemorySupported();
    value.guest_runtime_initialized = 0;
    std::snprintf(value.platform, sizeof(value.platform), "%s", REXGLUE_BUILD_PLATFORM);
    std::snprintf(value.core_version, sizeof(value.core_version), "%s", REXGLUE_VERSION_STRING);
    *out = value;
    return THEFT4_OK;
}

theft4_result theft4_core_activate(theft4_core *core) {
    if (auto result = check(core); result != THEFT4_OK) return result;
    if (core->state == THEFT4_STOPPED) return THEFT4_INVALID_STATE;
    if (core->state != THEFT4_ACTIVE) { core->state = THEFT4_ACTIVE; core->emit("core.active"); }
    return THEFT4_OK;
}

theft4_result theft4_core_pause(theft4_core *core) {
    if (auto result = check(core); result != THEFT4_OK) return result;
    if (core->state == THEFT4_STOPPED) return THEFT4_INVALID_STATE;
    if (core->state == THEFT4_ACTIVE) { core->state = THEFT4_PAUSED; core->emit("core.paused"); }
    return THEFT4_OK;
}

theft4_result theft4_core_memory_warning(theft4_core *core) {
    if (auto result = check(core); result != THEFT4_OK) return result;
    if (core->state == THEFT4_STOPPED) return THEFT4_INVALID_STATE;
    ++core->warnings;
    core->emit("core.memory_warning");
    return THEFT4_OK;
}

theft4_result theft4_core_stop(theft4_core *core) {
    if (auto result = check(core); result != THEFT4_OK) return result;
    if (core->state != THEFT4_STOPPED) { core->state = THEFT4_STOPPED; core->emit("core.stopped"); }
    return THEFT4_OK;
}

theft4_result theft4_core_destroy(theft4_core *core) {
    if (!pthread_main_np()) return THEFT4_WRONG_THREAD;
    if (!core) return THEFT4_OK;
    theft4_core_stop(core);
    core->emit("core.destroyed");
    delete core;
    return THEFT4_OK;
}
