/**
 * @file        ui/surface_mac.h
 * @brief       macOS CAMetalLayer surface shim for SDL3-backed windows
 *
 * @copyright   Copyright (c) 2026 Rien Gupta <rgupta9@scu.edu>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <cstdint>

#include <rex/ui/surface.h>

struct SDL_Window;

namespace rex::ui {

// Applies the CAMetalLayer presentation contract used by the Vulkan backend.
// Kept in an Objective-C++ translation unit so the rest of the SDL window
// implementation remains platform-neutral C++.
void ConfigureMetalLayerForPresentation(void* layer);

class CAMetalLayerSurface final : public Surface {
 public:
  CAMetalLayerSurface(SDL_Window* sdl_window, void* layer)
      : sdl_window_(sdl_window), layer_(layer) {}

  TypeIndex GetType() const override { return kTypeIndex_CAMetalLayer; }
  void* layer() const { return layer_; }
  void* GetNativePresentationHandle() const override { return layer_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override;

 private:
  SDL_Window* sdl_window_ = nullptr;
  void* layer_ = nullptr;
};

}  // namespace rex::ui
