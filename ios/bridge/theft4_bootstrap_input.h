#pragma once

#include <memory>

namespace rex::system {
class IInputSystem;
}

// Bridges Apple's GameController framework into the Xbox input contract while
// retaining a connected neutral user-0 pad when no physical controller exists.
std::unique_ptr<rex::system::IInputSystem> theft4_create_bootstrap_input();
