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

#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>

namespace rex::graphics {

// Keep the desktop backend convenience constructor out of command_processor.cpp
// so embedded hosts can link the platform-independent PM4 processor without
// pulling in the desktop GraphicsSystem and presenter implementation.
CommandProcessor::CommandProcessor(GraphicsSystem* graphics_system,
                                   system::KernelState* kernel_state)
    : CommandProcessor(
          graphics_system->memory(), graphics_system->register_file(), kernel_state,
          [graphics_system](uint32_t source, uint32_t cpu) {
            graphics_system->DispatchInterruptCallback(source, cpu);
          },
          graphics_system) {}

}  // namespace rex::graphics
