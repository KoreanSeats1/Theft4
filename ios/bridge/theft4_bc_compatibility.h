#pragma once
#include <stdbool.h>
#include <stdint.h>

// Keep BC-capable devices on the established build94 paths. Device age or
// memory alone must never enable texture conversion or presentation changes.
static inline bool theft4_bc_needs_preparation(bool supports_bc) {
    return !supports_bc;
}
static inline uint32_t theft4_bc_cached_sampler_capacity(
    bool supports_bc, uint32_t hardware_limit, uint32_t guest_registers) {
    return !supports_bc && hardware_limit < guest_registers
        ? hardware_limit : guest_registers;
}
static inline bool theft4_bc_needs_direct_present(
    bool supports_bc, bool older_gpu, uint32_t sampler_limit, bool hdr) {
    return !supports_bc && older_gpu && sampler_limit > 0 &&
        sampler_limit <= 16 && !hdr;
}
