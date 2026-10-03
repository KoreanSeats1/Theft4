#include "native_color_output_spirv.h"
#include <stdexcept>
std::vector<uint32_t> ApplyMetalShaderColorContract(std::vector<uint32_t> input) {
  std::string error;
  auto output = rex::graphics::gta4_native::AddNativeColorOutputEpilogue(input, &error);
  if (!output) throw std::runtime_error("color output contract: " + error);
  return std::move(*output);
}
