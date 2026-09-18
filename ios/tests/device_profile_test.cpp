#include "theft4_device_profile.h"

#include <cassert>
#include <cstring>

int main() {
    constexpr uint64_t GiB = UINT64_C(1) << 30;
    assert(theft4_is_legacy_ipad(true, 4 * GiB));
    assert(theft4_is_legacy_ipad(true, 6 * GiB));
    assert(!theft4_is_legacy_ipad(true, 8 * GiB));
    assert(!theft4_is_legacy_ipad(false, 4 * GiB));
    assert(!theft4_is_legacy_ipad(true, 0));
    assert(std::strcmp(theft4_device_profile_name(true, 4 * GiB, false),
                       "legacy-ipad") == 0);
    assert(std::strcmp(theft4_device_profile_name(true, 8 * GiB, false),
                       "ipad") == 0);
    assert(std::strcmp(theft4_device_profile_name(false, 6 * GiB, true),
                       "a19") == 0);
}
