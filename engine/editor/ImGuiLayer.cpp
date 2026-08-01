#include "editor/ImGuiLayer.h"

#include "platform/Window.h"

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <stdexcept>

namespace aige {

namespace {
void VkCheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed with VkResult " + std::to_string(result));
    }
}
} // namespace

ImGuiLayer::ImGuiLayer(Window& window, VulkanRHI& rhi) : rhi_(rhi) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    // Square/rectangular editor style (project preference): no rounded
    // corners anywhere.
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;

    std::array<VkDescriptorPoolSize, 1> poolSizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64}}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 64;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    VkCheck(vkCreateDescriptorPool(rhi_.Device(), &poolInfo, nullptr, &descriptorPool_),
            "vkCreateDescriptorPool(imgui)");

    ImGui_ImplGlfw_InitForVulkan(window.NativeHandle(), true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_2;
    initInfo.Instance = rhi_.Instance();
    initInfo.PhysicalDevice = rhi_.PhysicalDevice();
    initInfo.Device = rhi_.Device();
    initInfo.QueueFamily = rhi_.GraphicsQueueFamily();
    initInfo.Queue = rhi_.GraphicsQueue();
    initInfo.DescriptorPool = descriptorPool_;
    initInfo.RenderPass = rhi_.RenderPass();
    initInfo.MinImageCount = rhi_.SwapchainImageCount();
    initInfo.ImageCount = rhi_.SwapchainImageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&initInfo);

    ImGui_ImplVulkan_CreateFontsTexture();
}

ImGuiLayer::~ImGuiLayer() {
    vkDeviceWaitIdle(rhi_.Device());

    ImGui_ImplVulkan_DestroyFontsTexture();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vkDestroyDescriptorPool(rhi_.Device(), descriptorPool_, nullptr);
}

ImGuiID ImGuiLayer::BeginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    return ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_None);
}

void ImGuiLayer::EndFrame(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

} // namespace aige
