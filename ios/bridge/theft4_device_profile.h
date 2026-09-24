#pragma once

#include <stdbool.h>
#include <stdint.h>

// M-series iPads start at 8 GiB. Earlier iPads top out at 6 GiB, so the
// memory tier is a more durable compatibility signal than maintaining a list
// of every iPad hardware identifier.
static inline bool theft4_is_legacy_ipad(bool is_ipad,
                                         uint64_t physical_memory_bytes) {
    return is_ipad && physical_memory_bytes > 0 &&
           physical_memory_bytes < (UINT64_C(8) << 30);
}

// iOS reports slightly less than the marketed RAM capacity. Treat the
// 5-7 GiB physical-memory band as the 6 GiB iPhone tier without maintaining a
// fragile hardware-model allowlist.
static inline bool theft4_is_6gb_iphone(bool is_ipad,
                                        uint64_t physical_memory_bytes) {
    return !is_ipad && physical_memory_bytes >= (UINT64_C(5) << 30) &&
           physical_memory_bytes < (UINT64_C(7) << 30);
}

static inline const char *theft4_device_profile_name(
    bool is_ipad, uint64_t physical_memory_bytes, bool is_a19_iphone) {
    if (theft4_is_6gb_iphone(is_ipad, physical_memory_bytes))
        return "iphone-6gb";
    if (is_a19_iphone) return "a19";
    if (theft4_is_legacy_ipad(is_ipad, physical_memory_bytes))
        return "legacy-ipad";
    return is_ipad ? "ipad" : "generic";
}
