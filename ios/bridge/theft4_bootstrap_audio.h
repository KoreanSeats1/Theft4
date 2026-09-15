#pragma once

#include <memory>

namespace rex::runtime {
class FunctionDispatcher;
}
namespace rex::system {
class IAudioSystem;
}

// Paces GTA IV's guest mixer and accepts its render blocks while native iOS
// output and XMA decoding are integrated.
std::unique_ptr<rex::system::IAudioSystem> theft4_create_bootstrap_audio(
    rex::runtime::FunctionDispatcher* dispatcher);
