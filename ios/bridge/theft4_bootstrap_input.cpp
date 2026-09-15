#include "theft4_bootstrap_input.h"

#include <cstring>
#include <memory>

#include <rex/input/input.h>
#include <ios/ios_input_driver.h>
#include <rex/logging.h>
#include <rex/system/interfaces/input.h>

namespace {

using rex::X_RESULT;
using rex::X_STATUS;

class Theft4BootstrapInput final : public rex::system::IInputSystem {
 public:
  rex::X_STATUS Setup() override {
    driver_ = std::make_unique<rex::input::ios::IOSInputDriver>(nullptr, 0);
    const X_STATUS status = driver_->Setup();
    if (status != X_STATUS_SUCCESS) {
      driver_.reset();
      REXLOG_WARN(
          "Theft4 GameController initialization failed; retaining neutral "
          "user-0 bootstrap pad");
      return X_STATUS_SUCCESS;
    }
    REXLOG_INFO(
        "Theft4 iOS input active: Xbox, PlayStation and MFi controllers are "
        "mapped to Xbox 360 input with controller haptics");
    return status;
  }

  void Shutdown() override { driver_.reset(); }

  rex::X_RESULT GetCapabilities(
      uint32_t user_index, uint32_t flags,
      rex::input::X_INPUT_CAPABILITIES* out_caps) override {
    (void)flags;
    if (driver_) {
      const X_RESULT result = driver_->GetCapabilities(user_index, flags, out_caps);
      if (result != X_ERROR_DEVICE_NOT_CONNECTED) return result;
    }
    if (user_index != 0) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out_caps) {
      std::memset(out_caps, 0, sizeof(*out_caps));
      out_caps->type = rex::input::XINPUT_DEVTYPE_GAMEPAD;
      out_caps->sub_type = 0x01;
      out_caps->gamepad.buttons = 0xFFFF;
      out_caps->gamepad.left_trigger = 0xFF;
      out_caps->gamepad.right_trigger = 0xFF;
      out_caps->gamepad.thumb_lx = 0x7FFF;
      out_caps->gamepad.thumb_ly = 0x7FFF;
      out_caps->gamepad.thumb_rx = 0x7FFF;
      out_caps->gamepad.thumb_ry = 0x7FFF;
    }
    return X_ERROR_SUCCESS;
  }

  rex::X_RESULT GetState(uint32_t user_index,
                         rex::input::X_INPUT_STATE* out_state) override {
    if (driver_) {
      const X_RESULT result = driver_->GetState(user_index, out_state);
      if (result != X_ERROR_DEVICE_NOT_CONNECTED) return result;
    }
    if (user_index != 0) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out_state) std::memset(out_state, 0, sizeof(*out_state));
    return X_ERROR_SUCCESS;
  }

  rex::X_RESULT SetState(
      uint32_t user_index,
      rex::input::X_INPUT_VIBRATION* vibration) override {
    if (driver_) {
      const X_RESULT result = driver_->SetState(user_index, vibration);
      if (result != X_ERROR_DEVICE_NOT_CONNECTED) return result;
    }
    if (user_index != 0) return X_ERROR_DEVICE_NOT_CONNECTED;
    return vibration ? X_ERROR_SUCCESS : X_ERROR_BAD_ARGUMENTS;
  }

  rex::X_RESULT GetKeystroke(
      uint32_t user_index, uint32_t flags,
      rex::input::X_INPUT_KEYSTROKE* out_keystroke) override {
    if (driver_) {
      const X_RESULT result =
          driver_->GetKeystroke(user_index, flags, out_keystroke);
      if (result != X_ERROR_DEVICE_NOT_CONNECTED && result != X_ERROR_EMPTY) {
        return result;
      }
    }
    if (user_index != 0) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out_keystroke) std::memset(out_keystroke, 0, sizeof(*out_keystroke));
    return X_ERROR_EMPTY;
  }

 private:
  std::unique_ptr<rex::input::ios::IOSInputDriver> driver_;
};

}  // namespace

std::unique_ptr<rex::system::IInputSystem> theft4_create_bootstrap_input() {
  return std::make_unique<Theft4BootstrapInput>();
}
