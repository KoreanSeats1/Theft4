#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstdint>
#include <optional>

#include <rex/graphics/pipeline/shader/interpreter.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/trace_writer.h>
#include <rex/memory.h>

namespace rex::graphics {

class SharedMemory;

class DrawExtentEstimator {
 public:
  DrawExtentEstimator(const RegisterFile& register_file, const memory::Memory& memory,
                      TraceWriter* trace_writer, SharedMemory* shared_memory = nullptr)
      : register_file_(register_file),
        memory_(memory),
        trace_writer_(trace_writer),
        shared_memory_(shared_memory),
        shader_interpreter_(register_file, memory) {
    shader_interpreter_.SetTraceWriter(trace_writer);
  }

  // The shader must have its ucode analyzed.
  uint32_t EstimateVertexMaxY(const Shader& vertex_shader);
  uint32_t EstimateMaxY(bool try_to_estimate_vertex_max_y, const Shader& vertex_shader);

 private:
  class PositionYExportSink : public ShaderInterpreter::ExportSink {
   public:
    void Export(ucode::ExportRegister export_register, const float* value,
                uint32_t value_mask) override;

    void Reset() {
      position_y_.reset();
      position_w_.reset();
      point_size_.reset();
      vertex_kill_.reset();
    }

    const std::optional<float>& position_y() const { return position_y_; }
    const std::optional<float>& position_w() const { return position_w_; }
    const std::optional<float>& point_size() const { return point_size_; }
    const std::optional<uint32_t>& vertex_kill() const { return vertex_kill_; }

   private:
    std::optional<float> position_y_;
    std::optional<float> position_w_;
    std::optional<float> point_size_;
    std::optional<uint32_t> vertex_kill_;
  };

  const RegisterFile& register_file_;
  const memory::Memory& memory_;
  TraceWriter* trace_writer_;
  SharedMemory* shared_memory_;

  ShaderInterpreter shader_interpreter_;

  // Low-rate diagnostics for the opt-in CPU draw-extent experiment. These are
  // command-processor-thread counters, so atomics would only add overhead.
  uint64_t diagnostic_eligible_count_ = 0;
  uint64_t diagnostic_interpreted_count_ = 0;
  uint64_t diagnostic_reduced_count_ = 0;
  uint64_t diagnostic_vertex_fetch_accepted_count_ = 0;
  uint64_t diagnostic_unsafe_input_rejected_count_ = 0;
  uint64_t diagnostic_old_height_sum_ = 0;
  uint64_t diagnostic_new_height_sum_ = 0;
};

}  // namespace rex::graphics
