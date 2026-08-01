#pragma once

#include <volk.h>

#include <filesystem>

namespace aige {

// Directory containing the running executable (Windows-only for now; this
// engine is Windows-first through Phase 0-1, see technical.md §15).
std::filesystem::path ExecutableDir();

// Loads a compiled SPIR-V binary (name relative to ExecutableDir()/"shaders")
// and creates a VkShaderModule from it. Throws std::runtime_error on failure.
VkShaderModule LoadShaderModule(VkDevice device, const char* spirvFileName);

} // namespace aige
