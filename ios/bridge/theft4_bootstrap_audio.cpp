#include "theft4_bootstrap_audio.h"
#include "theft4_ios_audio_output.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/interfaces/audio.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

namespace {

using rex::X_STATUS;

class Theft4BootstrapAudio final : public rex::system::IAudioSystem {
 public:
  explicit Theft4BootstrapAudio(rex::runtime::FunctionDispatcher* dispatcher)
      : dispatcher_(dispatcher) {}
  ~Theft4BootstrapAudio() override { Shutdown(); }

  X_STATUS Setup(rex::system::KernelState* kernel_state) override {
    if (!dispatcher_ || !dispatcher_->memory() || !kernel_state) {
      return X_STATUS_INVALID_PARAMETER;
    }
    kernel_state_ = kernel_state;
    running_.store(true, std::memory_order_release);
    worker_ = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        kernel_state_, 128 * 1024, 0, [this]() { return WorkerMain(); }));
    worker_->set_name("Theft4 bootstrap audio");
    const X_STATUS status = worker_->Create();
    if (XFAILED(status)) {
      running_.store(false, std::memory_order_release);
      worker_.reset();
      kernel_state_ = nullptr;
      return status;
    }
    output_ = theft4_ios_audio_output_create();
    if (output_) {
      REXLOG_INFO(
          "Theft4 iOS audio active: guest mixer blocks are routed to native output");
    } else {
      REXLOG_WARN(
          "Theft4 native audio output unavailable; retaining paced-silence fallback");
    }
    return X_STATUS_SUCCESS;
  }

  X_STATUS RegisterClient(uint32_t callback, uint32_t callback_arg,
                          size_t* out_index) override {
    if (!callback || !dispatcher_ || !dispatcher_->memory()) {
      return X_STATUS_INVALID_PARAMETER;
    }
    std::lock_guard lock(mutex_);
    for (size_t index = 0; index < clients_.size(); ++index) {
      if (clients_[index].active) {
        continue;
      }
      const uint32_t wrapped_arg = dispatcher_->memory()->SystemHeapAlloc(sizeof(uint32_t));
      if (!wrapped_arg) {
        return X_STATUS_NO_MEMORY;
      }
      rex::memory::store_and_swap<uint32_t>(
          dispatcher_->memory()->TranslateVirtual(wrapped_arg), callback_arg);
      clients_[index] = {callback, wrapped_arg, true};
      allocations_.push_back(wrapped_arg);
      if (out_index) {
        *out_index = index;
      }
      REXLOG_INFO("Theft4 bootstrap audio registered client {} callback {:08X}", index,
                  callback);
      return X_STATUS_SUCCESS;
    }
    return X_STATUS_NO_MEMORY;
  }

  void UnregisterClient(size_t index) override {
    std::lock_guard lock(mutex_);
    if (index < clients_.size()) {
      clients_[index].active = false;
    }
  }

  void SubmitFrame(size_t index, uint32_t samples_ptr) override {
    if (index < submitted_frames_.size()) {
      submitted_frames_[index].fetch_add(1, std::memory_order_relaxed);
    }
    if (output_ && samples_ptr && dispatcher_ && dispatcher_->memory()) {
      const float* samples =
          dispatcher_->memory()->TranslateVirtual<const float*>(samples_ptr);
      theft4_ios_audio_output_submit(output_, samples, 256);
    }
  }

  void Shutdown() override {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }
    if (worker_) {
      worker_->Wait(0, 0, 0, nullptr);
      worker_.reset();
    }
    if (dispatcher_ && dispatcher_->memory()) {
      for (uint32_t allocation : allocations_) {
        dispatcher_->memory()->SystemHeapFree(allocation);
      }
    }
    allocations_.clear();
    theft4_ios_audio_output_destroy(output_);
    output_ = nullptr;
    kernel_state_ = nullptr;
  }

 private:
  struct Client {
    uint32_t callback = 0;
    uint32_t wrapped_arg = 0;
    bool active = false;
  };

  int WorkerMain() {
    // The Xbox render callback produces 256 samples at 48 kHz.
    constexpr auto kBlockDuration = std::chrono::microseconds(5333);
    auto deadline = std::chrono::steady_clock::now();
    while (running_.load(std::memory_order_acquire)) {
      std::array<Client, 8> clients;
      {
        std::lock_guard lock(mutex_);
        clients = clients_;
      }
      for (const Client& client : clients) {
        if (!client.active || !client.callback) {
          continue;
        }
        uint64_t args[] = {client.wrapped_arg};
        dispatcher_->Execute(worker_->thread_state(), client.callback, args, std::size(args));
      }
      deadline += kBlockDuration;
      const auto now = std::chrono::steady_clock::now();
      if (deadline < now) {
        deadline = now;
      }
      std::this_thread::sleep_until(deadline);
    }
    return 0;
  }

  rex::runtime::FunctionDispatcher* dispatcher_ = nullptr;
  rex::system::KernelState* kernel_state_ = nullptr;
  rex::system::object_ref<rex::system::XHostThread> worker_;
  std::atomic<bool> running_{false};
  std::mutex mutex_;
  std::array<Client, 8> clients_{};
  std::array<std::atomic<uint64_t>, 8> submitted_frames_{};
  std::vector<uint32_t> allocations_;
  theft4_ios_audio_output* output_ = nullptr;
};

}  // namespace

std::unique_ptr<rex::system::IAudioSystem> theft4_create_bootstrap_audio(
    rex::runtime::FunctionDispatcher* dispatcher) {
  return std::make_unique<Theft4BootstrapAudio>(dispatcher);
}
