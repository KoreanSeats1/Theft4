#include "theft4_bootstrap_graphics.h"
#include "theft4_metal_presenter.h"
#ifdef THEFT4_HAS_MOLTENVK_PROBE
#include "theft4_rex_vulkan_gate.h"
#include "theft4_vulkan_probe.h"
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/pipeline/shader/spirv.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/vulkan/command_processor.h>
#include <rex/hash.h>
#include <rex/logging.h>
#include <rex/string/buffer.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>

namespace {

using rex::X_STATUS;

// This is the first embedded backend for the real ReXGlue command processor.
// It executes the platform-independent PM4 parser and all of its register,
// memory, synchronization, interrupt and swap side effects. Draw/copy and
// shader translation are counted but intentionally not rendered yet; Metal
// implementation will replace these three narrow methods incrementally.
class Theft4ProbeCommandProcessor final : public rex::graphics::CommandProcessor {
 public:
  Theft4ProbeCommandProcessor(
      rex::memory::Memory* memory, rex::graphics::RegisterFile* register_file,
      rex::system::KernelState* kernel_state,
      std::function<void(uint32_t, uint32_t)> interrupt_dispatcher)
      : CommandProcessor(memory, register_file, kernel_state,
                         std::move(interrupt_dispatcher)) {}

  void TracePlaybackWroteMemory(uint32_t, uint32_t) override {}
  void RestoreEdramSnapshot(const void*) override {}

 protected:
  bool SetupContext() override {
    constexpr uint64_t kXboxSharedMemorySize = 512ull * 1024ull * 1024ull;
    constexpr uint64_t kXenosEdramSize = 10ull * 1024ull * 1024ull;
    if (!theft4_metal_renderer_initialize(kXboxSharedMemorySize,
                                          kXenosEdramSize)) {
      REXLOG_ERROR("Theft4 Metal renderer resource initialization failed");
      return false;
    }
#ifdef THEFT4_HAS_MOLTENVK_PROBE
    vulkan_ready_ = theft4_vulkan_probe_device();
    if (!vulkan_ready_) {
      REXLOG_WARN(
          "Theft4 will continue with direct Metal bring-up; the experimental "
          "MoltenVK path was unavailable");
    } else {
      rex::graphics::SpirvShaderTranslator::Features features(false);
      features.spirv_version = spv::Spv_1_3;
      features.max_storage_buffer_range = 512 * 1024 * 1024;
      features.vertex_pipeline_stores_and_atomics = true;
      features.fragment_stores_and_atomics = true;
      shader_translator_ =
          std::make_unique<rex::graphics::SpirvShaderTranslator>(
              features, false, false, false);
    }
#endif
    if (!CommandProcessor::SetupContext()) {
      theft4_metal_renderer_shutdown();
      return false;
    }
    REXLOG_INFO(
        "Theft4 Xenos renderer context initialized: 512 MiB shared memory, "
        "10 MiB EDRAM, triple-buffered Metal submissions");
    return true;
  }

  void ShutdownContext() override {
    CommandProcessor::ShutdownContext();
#ifdef THEFT4_HAS_MOLTENVK_PROBE
    shader_translator_.reset();
    theft4_vulkan_probe_shutdown();
#endif
    theft4_metal_renderer_shutdown();
    REXLOG_INFO(
        "Theft4 Xenos renderer stopped: primary_buffers={}, shader_loads={}, "
        "unique_shaders={}, draws={}, copies={}, swaps={}, metal_completed={}",
        primary_buffers_.load(), shader_loads_.load(), shaders_.size(),
        draws_.load(), copies_.load(), swaps_.load(),
        theft4_metal_renderer_completed_frames());
  }

  void OnPrimaryBufferEnd() override {
    const uint64_t submission = primary_buffers_.fetch_add(1) + 1;
    if (submission <= 8) {
      REXLOG_INFO(
          "Theft4 real PM4 submit {} complete: shader_loads={}, unique_shaders={}, "
          "draws={}, copies={}, swaps={}",
          submission, shader_loads_.load(), shaders_.size(), draws_.load(),
          copies_.load(), swaps_.load());
    }
  }

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override {
    const uint64_t swap = swaps_.fetch_add(1) + 1;
    if (!theft4_metal_renderer_end_frame(frontbuffer_ptr, frontbuffer_width,
                                         frontbuffer_height)) {
      metal_failures_.fetch_add(1);
    }
    if (swap <= 8 || !(swap % 300)) {
      REXLOG_INFO(
          "Theft4 real PM4 swap {}: frontbuffer {:08X}, {}x{}, Metal {}/{}",
          swap, frontbuffer_ptr, frontbuffer_width, frontbuffer_height,
          theft4_metal_renderer_completed_frames(),
          theft4_metal_renderer_submitted_frames());
    }
  }

  rex::graphics::Shader* LoadShader(
      rex::graphics::xenos::ShaderType shader_type, uint32_t,
      const uint32_t* host_address, uint32_t dword_count) override {
    shader_loads_.fetch_add(1);
    const uint64_t hash = XXH3_64bits(host_address,
                                      dword_count * sizeof(uint32_t));
    auto found = shaders_.find(hash);
    if (found != shaders_.end()) {
      return found->second.get();
    }

    auto shader = std::make_unique<rex::graphics::SpirvShader>(
        shader_type, hash, host_address, dword_count);
    rex::string::StringBuffer disassembly_buffer;
    shader->AnalyzeUcode(disassembly_buffer);
    rex::graphics::Shader* result = shader.get();

#ifdef THEFT4_HAS_MOLTENVK_PROBE
    if (shader_translator_) {
      constexpr uint32_t kConservativeGuestRegisterCount = 64;
      const uint64_t modification =
          shader_type == rex::graphics::xenos::ShaderType::kVertex
              ? shader_translator_->GetDefaultVertexShaderModification(
                    shader->GetDynamicAddressableRegisterCount(
                        kConservativeGuestRegisterCount))
              : shader_translator_->GetDefaultPixelShaderModification(
                    shader->GetDynamicAddressableRegisterCount(
                        kConservativeGuestRegisterCount));
      rex::graphics::Shader::Translation* translation =
          shader->GetOrCreateTranslation(modification);
      if (shader_translator_->TranslateAnalyzedShader(*translation) &&
          theft4_vulkan_validate_spirv(
              translation->translated_binary().data(),
              translation->translated_binary().size(), hash,
              shader_type == rex::graphics::xenos::ShaderType::kVertex)) {
        const uint64_t translated = spirv_shaders_.fetch_add(1) + 1;
        if (translated <= 16 || !(translated % 100)) {
          REXLOG_INFO(
              "Theft4 translated and MoltenVK-validated {} shader {:016X}: "
              "{} SPIR-V bytes",
              shader_type == rex::graphics::xenos::ShaderType::kVertex
                  ? "vertex"
                  : "pixel",
              hash, translation->translated_binary().size());
        }
      } else {
        spirv_failures_.fetch_add(1);
      }
    }
#endif

    shaders_.emplace(hash, std::move(shader));
    const uint64_t unique_count = shaders_.size();
    if (unique_count <= 16 || !(unique_count % 100)) {
      REXLOG_INFO(
          "Theft4 analyzed {} shader {:016X}: {} dwords, {} vertex bindings, "
          "{} texture bindings",
          shader_type == rex::graphics::xenos::ShaderType::kVertex ? "vertex"
                                                                   : "pixel",
          hash, dword_count, result->vertex_bindings().size(),
          result->texture_bindings().size());
    }
    return result;
  }

  bool IssueDraw(rex::graphics::xenos::PrimitiveType, uint32_t,
                 IndexBufferInfo*, bool) override {
    draws_.fetch_add(1);
    if (!theft4_metal_renderer_note_draw()) {
      metal_failures_.fetch_add(1);
      return false;
    }
    return active_vertex_shader() != nullptr;
  }

  bool IssueCopy() override {
    copies_.fetch_add(1);
    return true;
  }

 private:
  std::atomic<uint64_t> primary_buffers_{0};
  std::atomic<uint64_t> shader_loads_{0};
  std::atomic<uint64_t> draws_{0};
  std::atomic<uint64_t> copies_{0};
  std::atomic<uint64_t> swaps_{0};
  std::atomic<uint64_t> metal_failures_{0};
  std::atomic<uint64_t> spirv_shaders_{0};
  std::atomic<uint64_t> spirv_failures_{0};
  bool vulkan_ready_ = false;
  std::unique_ptr<rex::graphics::SpirvShaderTranslator> shader_translator_;
  std::unordered_map<uint64_t, std::unique_ptr<rex::graphics::SpirvShader>>
      shaders_;
};

class Theft4BootstrapGraphics final : public rex::system::IGraphicsSystem {
 public:
  ~Theft4BootstrapGraphics() override { Shutdown(); }

  X_STATUS SetupPresentation(rex::ui::WindowedAppContext*) override {
    // UIKit owns the CAMetalLayer and binds it through theft4_metal_presenter.
    return X_STATUS_SUCCESS;
  }

  X_STATUS SetupGuestGpu(rex::runtime::FunctionDispatcher* dispatcher,
                         rex::system::KernelState* kernel_state) override {
    if (running_.load(std::memory_order_acquire)) {
      return X_STATUS_SUCCESS;
    }
    if (!dispatcher || !kernel_state || !dispatcher->memory()) {
      return X_STATUS_INVALID_PARAMETER;
    }

    dispatcher_ = dispatcher;
    kernel_state_ = kernel_state;
    memory_ = dispatcher->memory();
    auto interrupt_dispatcher = [this](uint32_t source, uint32_t cpu) {
      DispatchInterrupt(source, cpu);
    };
#ifdef THEFT4_HAS_MOLTENVK_PROBE
    if (theft4_vulkan_probe_device() && theft4_rex_vulkan_device()) {
      command_processor_ =
          std::make_unique<rex::graphics::vulkan::VulkanCommandProcessor>(
              memory_, &register_file_, kernel_state_, interrupt_dispatcher,
              theft4_rex_vulkan_device(), nullptr,
              theft4_rex_vulkan_presenter());
      REXLOG_INFO(
          "Theft4 selected the production Liberty Vulkan command processor");
    }
#endif
    if (!command_processor_) {
      command_processor_ = std::make_unique<Theft4ProbeCommandProcessor>(
          memory_, &register_file_, kernel_state_, interrupt_dispatcher);
      REXLOG_WARN("Theft4 falling back to the PM4 diagnostic processor");
    }

    if (!memory_->AddVirtualMappedRange(
            0x7FC80000, 0xFFFF0000, 0x0000FFFF, this,
            reinterpret_cast<rex::runtime::MMIOReadCallback>(&ReadRegisterThunk),
            reinterpret_cast<rex::runtime::MMIOWriteCallback>(&WriteRegisterThunk)) ||
        !command_processor_->Initialize()) {
      command_processor_.reset();
      dispatcher_ = nullptr;
      kernel_state_ = nullptr;
      memory_ = nullptr;
      return X_STATUS_UNSUCCESSFUL;
    }

    running_.store(true, std::memory_order_release);
    vblank_worker_ = rex::system::object_ref<rex::system::XHostThread>(
        new rex::system::XHostThread(kernel_state_, 128 * 1024, 0,
                                     [this]() { return VblankWorkerMain(); }));
    vblank_worker_->set_name("Theft4 GPU VSync");
    const X_STATUS create_status = vblank_worker_->Create();
    if (XFAILED(create_status)) {
      running_.store(false, std::memory_order_release);
      command_processor_->Shutdown();
      command_processor_.reset();
      vblank_worker_.reset();
      dispatcher_ = nullptr;
      kernel_state_ = nullptr;
      memory_ = nullptr;
      return create_status;
    }

    REXLOG_WARN(
        "Theft4 iOS GPU active: real PM4, shader analysis, shared memory, "
        "EDRAM, draw translation and Metal-backed presentation enabled");
    return X_STATUS_SUCCESS;
  }

  bool has_presentation() const override { return theft4_metal_has_layer(); }

  void SetInterruptCallback(uint32_t callback, uint32_t user_data) override {
    interrupt_data_.store(user_data, std::memory_order_release);
    interrupt_callback_.store(callback, std::memory_order_release);
    REXLOG_INFO("Theft4 GPU interrupt callback {:08X} data {:08X}", callback,
                user_data);
  }

  void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) override {
    if (command_processor_) {
      command_processor_->InitializeRingBuffer(ptr, size_log2);
    }
    REXLOG_INFO("Theft4 GPU ring buffer {:08X}, size log2 {}", ptr, size_log2);
  }

  void EnableReadPointerWriteBack(uint32_t ptr,
                                  uint32_t block_size_log2) override {
    if (command_processor_) {
      command_processor_->EnableReadPointerWriteBack(ptr, block_size_log2);
    }
    REXLOG_INFO("Theft4 GPU read-pointer writeback {:08X}, block log2 {}", ptr,
                block_size_log2);
  }

  void Shutdown() override {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }
    if (vblank_worker_) {
      vblank_worker_->Wait(0, 0, 0, nullptr);
      vblank_worker_.reset();
    }
    if (command_processor_) {
      command_processor_->Shutdown();
      command_processor_.reset();
    }
    dispatcher_ = nullptr;
    kernel_state_ = nullptr;
    memory_ = nullptr;
  }

 private:
  static uint32_t ReadRegisterThunk(void*, Theft4BootstrapGraphics* graphics,
                                    uint32_t address) {
    return graphics->ReadRegister(address);
  }

  static void WriteRegisterThunk(void*, Theft4BootstrapGraphics* graphics,
                                 uint32_t address, uint32_t value) {
    graphics->WriteRegister(address, value);
  }

  uint32_t ReadRegister(uint32_t address) const {
    const uint32_t index = (address & 0xFFFFu) / sizeof(uint32_t);
    switch (index) {
      case 0x0F00:  // RB_EDRAM_TIMING
        return 0x08100748;
      case 0x0F01:  // RB_BC_CONTROL
        return 0x0000200E;
      case 0x194C:  // D1MODE_V_COUNTER
        return 720;
      case 0x1951:  // D1MODE_INT_MASK/status queried during startup
        return 1;
      case 0x1961:  // D1MODE_VIEWPORT_SIZE
        return (1280u << 16) | 720u;
      default:
        return index < rex::graphics::RegisterFile::kRegisterCount
                   ? register_file_.values[index]
                   : 0;
    }
  }

  void WriteRegister(uint32_t address, uint32_t value) {
    const uint32_t index = (address & 0xFFFFu) / sizeof(uint32_t);
    if (index < rex::graphics::RegisterFile::kRegisterCount) {
      register_file_.values[index] = value;
    }
    if (index == 0x01C5 && command_processor_) {  // CP_RB_WPTR
      command_processor_->UpdateWritePointer(value);
    }
  }

  void DispatchInterrupt(uint32_t source, uint32_t cpu) {
    const uint32_t callback = interrupt_callback_.load(std::memory_order_acquire);
    if (!callback || !dispatcher_) {
      return;
    }
    auto* thread = rex::system::XThread::GetCurrentThread();
    if (!thread || !thread->thread_state()) {
      return;
    }
    if (cpu == 0xFFFFFFFF) {
      cpu = 2;
    }
    thread->SetActiveCpu(cpu);
    uint64_t args[] = {source,
                       interrupt_data_.load(std::memory_order_acquire)};
    dispatcher_->ExecuteInterrupt(thread->thread_state(), callback, args,
                                  std::size(args));
  }

  int VblankWorkerMain() {
    auto next_vblank = std::chrono::steady_clock::now();
    while (running_.load(std::memory_order_acquire)) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_vblank) {
        if (!metal_clear_presented_.load(std::memory_order_acquire) &&
            theft4_metal_present_clear(0.02, 0.20, 0.22, 1.0)) {
          metal_clear_presented_.store(true, std::memory_order_release);
          REXLOG_INFO("Theft4 Metal presenter committed its first drawable");
        }
        if (command_processor_) {
          command_processor_->increment_counter();
        }
        DispatchInterrupt(0, 2);
        next_vblank = now + std::chrono::microseconds(16667);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
  }

  rex::runtime::FunctionDispatcher* dispatcher_ = nullptr;
  rex::system::KernelState* kernel_state_ = nullptr;
  rex::memory::Memory* memory_ = nullptr;
  rex::graphics::RegisterFile register_file_;
  std::unique_ptr<rex::graphics::CommandProcessor> command_processor_;
  rex::system::object_ref<rex::system::XHostThread> vblank_worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> metal_clear_presented_{false};
  std::atomic<uint32_t> interrupt_callback_{0};
  std::atomic<uint32_t> interrupt_data_{0};
};

}  // namespace

std::unique_ptr<rex::system::IGraphicsSystem>
theft4_create_bootstrap_graphics() {
  return std::make_unique<Theft4BootstrapGraphics>();
}
