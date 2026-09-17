/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 * Extracted from xam_ui.cpp so embedded hosts can select their content device
 * without linking the desktop dialog implementation.
 */

#include <chrono>
#include <functional>

#include <rex/hook.h>
#include <rex/kernel/xam/private.h>
#include <rex/logging.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xtypes.h>
#include <rex/thread.h>
#include <rex/types.h>

namespace rex::kernel::xam {
using namespace rex::system;

X_RESULT xeXamDispatchHeadless(std::function<X_RESULT()> run_callback, uint32_t overlapped) {
  auto pre = []() {
    REX_KERNEL_STATE()->BroadcastNotification(0x9, true);  // XN_SYS_UI
  };
  auto post = []() {
    // Match desktop headless dispatch: some games register the notification
    // listener only after observing their overlapped completion.
    rex::thread::Sleep(std::chrono::milliseconds(100));
    REX_KERNEL_STATE()->BroadcastNotification(0x9, false);
  };
  if (!overlapped) {
    pre();
    const auto result = run_callback();
    post();
    return result;
  }
  REX_KERNEL_STATE()->CompleteOverlappedDeferred(run_callback, overlapped, pre, post);
  return X_ERROR_IO_PENDING;
}

u32 XamShowDeviceSelectorUI_entry(u32 user_index, u32 content_type, u32 content_flags,
                                u64 total_requested, mapped_u32 device_id_ptr,
                                mapped_void overlapped) {
  if (!device_id_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }
  REXKRNL_INFO("XamShowDeviceSelectorUI: user={} type={} flags={:08X} requested={} "
               "overlapped={:08X}; selecting content HDD",
               uint32_t(user_index), uint32_t(content_type), uint32_t(content_flags),
               uint64_t(total_requested), overlapped.guest_address());
  return xeXamDispatchHeadless(
      [device_id_ptr]() -> X_RESULT {
        // This is the same device enumerated by xam_content_device.cpp. Manual
        // saves and autosaves continue through ContentManager's normal paths.
        *device_id_ptr = static_cast<uint32_t>(system::xam::DummyDeviceId::HDD);
        return X_ERROR_SUCCESS;
      },
      overlapped.guest_address());
}

}  // namespace rex::kernel::xam

REX_EXPORT(__imp__XamShowDeviceSelectorUI, rex::kernel::xam::XamShowDeviceSelectorUI_entry)
