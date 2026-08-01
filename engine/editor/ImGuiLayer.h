#pragma once

#include "rhi/VulkanRHI.h"

#include <imgui.h>

namespace aige {

class Window;

// Owns the Dear ImGui context and its GLFW/Vulkan backend integration
// (technical.md §4's ImGui editor shell / development_plan.md milestone
// 1.1). One per process. ImGui itself draws into whatever render pass is
// active when EndFrame() records its draw data — callers must have called
// VulkanRHI::BeginSwapchainPass() first.
class ImGuiLayer {
public:
    ImGuiLayer(Window& window, VulkanRHI& rhi);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // Starts a new ImGui frame and opens a fullscreen dockspace host window
    // covering the whole viewport. Returns that dockspace's ID so the caller
    // can dock panels into it (and, the first time, lay out a default split
    // via ImGui's DockBuilder — panel names/arrangement are the caller's
    // concern, not this layer's).
    ImGuiID BeginFrame();

    // Finishes ImGui's layout pass and records its draw data into `cmd`.
    void EndFrame(VkCommandBuffer cmd);

private:
    VulkanRHI& rhi_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace aige
