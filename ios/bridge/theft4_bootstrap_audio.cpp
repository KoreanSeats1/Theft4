#include "theft4_bootstrap_audio.h"
#include "theft4_ios_audio_output.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <pthread/qos.h>
#include <thread>
#include <vector>

#include <rex/logging.h>
#include <rex/audio/xma/decoder.h>
#include <rex/audio/handoff_trace.h>
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
      : dispatcher_(dispatcher), xma_decoder_(
            dispatcher ? std::make_unique<rex::audio::XmaDecoder>(dispatcher) : nullptr) {}
  ~Theft4BootstrapAudio() override { Shutdown(); }

  X_STATUS Setup(rex::system::KernelState* kernel_state) override {
    if (!dispatcher_ || !dispatcher_->memory() || !kernel_state) {
      return X_STATUS_INVALID_PARAMETER;
    }
    kernel_state_ = kernel_state;
    if (!xma_decoder_ || XFAILED(xma_decoder_->Setup(kernel_state))) {
      kernel_state_ = nullptr;
      return X_STATUS_UNSUCCESSFUL;
    }
    rex::audio::handoff::Initialize();
    output_ = theft4_ios_audio_output_create();
    if (output_) {
      REXLOG_INFO(
          "Theft4 iOS audio active: speaker-driven 32-block guest queue");
    } else {
      REXLOG_WARN(
          "Theft4 native audio output unavailable; retaining paced-silence fallback");
    }
    running_.store(true, std::memory_order_release);
    worker_ = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        kernel_state_, 128 * 1024, 0, [this]() { return WorkerMain(); }));
    worker_->set_name("Theft4 bootstrap audio");
    const X_STATUS status = worker_->Create();
    if (XFAILED(status)) {
      running_.store(false, std::memory_order_release);
      worker_.reset();
      kernel_state_ = nullptr;
      theft4_ios_audio_output_destroy(output_);
      output_ = nullptr;
      xma_decoder_->Shutdown();
      rex::audio::handoff::Shutdown();
      return status;
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
      const uint64_t requested =
          theft4_ios_audio_output_requested_blocks(output_);
      pumped_blocks_[index].store(requested > 32 ? requested - 32 : 0,
                                  std::memory_order_relaxed);
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
    uint64_t submitted = 0;
    if (index < submitted_frames_.size())
      submitted = submitted_frames_[index].fetch_add(1, std::memory_order_relaxed) + 1;
    bool accepted = false;
    if (output_ && samples_ptr && dispatcher_ && dispatcher_->memory()) {
      const float* samples =
          dispatcher_->memory()->TranslateVirtual<const float*>(samples_ptr);
      accepted = theft4_ios_audio_output_submit(output_, samples, 256);
    }
    rex::audio::handoff::Record("ios-submit", index,
                                {submitted, samples_ptr, accepted});
  }

  rex::audio::XmaDecoder* xma_decoder() override {
    return xma_decoder_.get();
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
    if (xma_decoder_) {
      xma_decoder_->Shutdown();
    }
    rex::audio::handoff::Shutdown();
  }

 private:
  struct Client {
    uint32_t callback = 0;
    uint32_t wrapped_arg = 0;
    bool active = false;
  };

  int WorkerMain() {
    // The guest mixer is the producer for the native real-time audio ring.
    // Match the priority used by ReXGlue's CoreAudio reliability path so a
    // draw-heavy frame cannot starve it long enough to drain the preroll.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    // The native output callback advances an absolute block target as samples
    // reach the speaker. This is the same credit-driven relationship used by
    // XeniOS, and—unlike the former sleep_until loop—missed work remains queued
    // so the producer can catch up after a long 3D frame.
    constexpr auto kIdlePoll = std::chrono::microseconds(250);
    constexpr auto kFallbackBlockDuration = std::chrono::microseconds(5333);
    auto fallback_deadline = std::chrono::steady_clock::now();
    uint64_t fallback_requested = 1;
    while (running_.load(std::memory_order_acquire)) {
      std::array<Client, 8> clients;
      {
        std::lock_guard lock(mutex_);
        clients = clients_;
      }
      uint64_t requested = 0;
      if (output_) {
        requested = theft4_ios_audio_output_requested_blocks(output_);
      } else {
        const auto now = std::chrono::steady_clock::now();
        if (now >= fallback_deadline) {
          ++fallback_requested;
          fallback_deadline = now + kFallbackBlockDuration;
        }
        requested = fallback_requested;
      }

      bool pumped = false;
      for (size_t index = 0; index < clients.size(); ++index) {
        const Client& client = clients[index];
        if (!client.active || !client.callback) {
          continue;
        }
        if (pumped_blocks_[index].load(std::memory_order_relaxed) >= requested) {
          continue;
        }
        uint64_t args[] = {client.wrapped_arg};
        const uint64_t submitted_before =
            submitted_frames_[index].load(std::memory_order_relaxed);
        {
          rex::audio::handoff::Span mixer_span("ios-mixer-callback", index,
                                               requested, submitted_before);
          dispatcher_->Execute(worker_->thread_state(), client.callback, args,
                               std::size(args));
        }
        const uint64_t submitted_after =
            submitted_frames_[index].load(std::memory_order_relaxed);
        rex::audio::handoff::Record("ios-mixer-result", index,
                                    {requested, submitted_before, submitted_after});
        pumped_blocks_[index].fetch_add(1, std::memory_order_relaxed);
        pumped = true;
      }
      if (!pumped) {
        std::this_thread::sleep_for(kIdlePoll);
      }
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
  std::array<std::atomic<uint64_t>, 8> pumped_blocks_{};
  std::vector<uint32_t> allocations_;
  theft4_ios_audio_output* output_ = nullptr;
  std::unique_ptr<rex::audio::XmaDecoder> xma_decoder_;
};

}  // namespace

std::unique_ptr<rex::system::IAudioSystem> theft4_create_bootstrap_audio(
    rex::runtime::FunctionDispatcher* dispatcher) {
  return std::make_unique<Theft4BootstrapAudio>(dispatcher);
}
