/**
 * @file        system/interfaces/audio.h
 * @brief       Abstract audio system interface for dependency injection
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <rex/system/xtypes.h>

namespace rex::system {
class KernelState;
}

namespace rex::audio {
class XmaDecoder;
}

namespace rex::system {

class IAudioSystem {
 public:
  virtual ~IAudioSystem() = default;
  virtual X_STATUS Setup(KernelState* kernel_state) = 0;
  virtual void Shutdown() = 0;

  // Xbox render-driver services used by the xboxkrnl XAudio exports. Keeping
  // these on the injected interface avoids assuming every platform backend is
  // the desktop AudioSystem implementation.
  virtual X_STATUS RegisterClient(uint32_t callback, uint32_t callback_arg,
                                  size_t* out_index) {
    (void)callback;
    (void)callback_arg;
    (void)out_index;
    return X_STATUS_NOT_IMPLEMENTED;
  }
  virtual void UnregisterClient(size_t index) { (void)index; }
  virtual void SubmitFrame(size_t index, uint32_t samples_ptr) {
    (void)index;
    (void)samples_ptr;
  }
  virtual audio::XmaDecoder* xma_decoder() { return nullptr; }
};

}  // namespace rex::system
