#include "theft4_device_profile.h"

#include <cassert>
#include <cstring>

int main() {
    constexpr uint64_t GiB = UINT64_C(1) << 30;
    constexpr uint64_t MiB = UINT64_C(1) << 20;
    assert(theft4_is_legacy_ipad(true, 4 * GiB));
    assert(theft4_is_legacy_ipad(true, 6 * GiB));
    assert(!theft4_is_legacy_ipad(true, 8 * GiB));
    assert(!theft4_is_legacy_ipad(false, 4 * GiB));
    assert(!theft4_is_legacy_ipad(true, 0));
    assert(theft4_is_6gb_iphone(false, 6 * GiB));
    assert(theft4_is_6gb_iphone(false, 5662 * MiB));
    assert(!theft4_is_6gb_iphone(false, 4 * GiB));
    assert(!theft4_is_6gb_iphone(false, 8 * GiB));
    assert(!theft4_is_6gb_iphone(true, 6 * GiB));
    assert(std::strcmp(theft4_device_profile_name(true, 4 * GiB, false),
                       "legacy-ipad") == 0);
    assert(std::strcmp(theft4_device_profile_name(true, 8 * GiB, false),
                       "ipad") == 0);
    assert(std::strcmp(theft4_device_profile_name(false, 6 * GiB, false),
                       "iphone-6gb") == 0);
    assert(std::strcmp(theft4_device_profile_name(false, 6 * GiB, true),
                       "iphone-6gb") == 0);
    assert(std::strcmp(theft4_device_profile_name(false, 8 * GiB, true),
                       "a19") == 0);
}
