#pragma once

#include <memory>

namespace rex::system {
class IGraphicsSystem;
}

// Temporary iOS bring-up GPU. It supplies the Xbox video interrupt, MMIO and
// ring-buffer services needed to keep the AOT title running while the real
// Metal command processor is integrated. It intentionally does not render.
std::unique_ptr<rex::system::IGraphicsSystem> theft4_create_bootstrap_graphics();
