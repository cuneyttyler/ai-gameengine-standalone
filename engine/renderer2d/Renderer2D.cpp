#include "renderer2d/Renderer2D.h"

#include "renderer2d/Components.h"
#include "rhi/ShaderUtil.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace aige {

namespace {

void VkCheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed with VkResult " + std::to_string(result));
    }
}

struct SpriteVertex {
    float pos[2];
    float uv[2];
    float color[4];
};

constexpr uint32_t kAtlasSize = 64;

// Procedural placeholder atlas: a 2x2 grid of bordered, distinctly colored
// quadrants (grass / dirt / player-marker / npc-marker). Real texture import
// (stb_image, .png assets) is Phase 1 milestone 1.3 — this exists purely to
// prove the atlas-UV/texturing pipeline end to end before real art exists.
std::vector<uint8_t> GenerateAtlasPixels() {
    std::vector<uint8_t> pixels(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4);
    const uint32_t half = kAtlasSize / 2;

    for (uint32_t y = 0; y < kAtlasSize; ++y) {
        for (uint32_t x = 0; x < kAtlasSize; ++x) {
            bool right = x >= half;
            bool bottom = y >= half;

            uint8_t r, g, b;
            if (!right && !bottom) {
                r = 70;
                g = 140;
                b = 60; // grass
            } else if (right && !bottom) {
                r = 120;
                g = 90;
                b = 60; // dirt
            } else if (!right && bottom) {
                r = 200;
                g = 60;
                b = 60; // player marker
            } else {
                r = 60;
                g = 90;
                b = 200; // npc marker
            }

            uint32_t localX = x % half;
            uint32_t localY = y % half;
            bool edge = localX < 2 || localY < 2 || localX >= half - 2 || localY >= half - 2;
            if (edge) {
                r = static_cast<uint8_t>(r * 2 / 3);
                g = static_cast<uint8_t>(g * 2 / 3);
                b = static_cast<uint8_t>(b * 2 / 3);
            }

            size_t idx = (static_cast<size_t>(y) * kAtlasSize + x) * 4;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}

} // namespace

Renderer2D::Renderer2D(VulkanRHI& rhi) : rhi_(rhi) {
    CreateAtlasTexture();
    CreateDescriptorSet();
    CreatePipeline();
    CreateVertexBuffers();
}

Renderer2D::~Renderer2D() {
    VkDevice device = rhi_.Device();
    vkDeviceWaitIdle(device);

    for (auto& buf : vertexBuffers_) {
        if (buf.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(rhi_.Allocator(), buf.buffer, buf.allocation);
        }
    }

    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);

    vkDestroySampler(device, atlasSampler_, nullptr);
    vkDestroyImageView(device, atlasImageView_, nullptr);
    if (atlasImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(rhi_.Allocator(), atlasImage_, atlasAllocation_);
    }
}

void Renderer2D::RunOneTimeCommands(const std::function<void(VkCommandBuffer)>& recordFn) {
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = rhi_.CommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCheck(vkAllocateCommandBuffers(rhi_.Device(), &allocInfo, &cmd), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkCheck(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer");

    recordFn(cmd);

    VkCheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VkCheck(vkQueueSubmit(rhi_.GraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
    vkQueueWaitIdle(rhi_.GraphicsQueue());

    vkFreeCommandBuffers(rhi_.Device(), rhi_.CommandPool(), 1, &cmd);
}

void Renderer2D::CreateAtlasTexture() {
    std::vector<uint8_t> pixels = GenerateAtlasPixels();
    VkDeviceSize imageSize = pixels.size();

    VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    stagingInfo.size = imageSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    VmaAllocationInfo stagingInfoOut{};
    VkCheck(vmaCreateBuffer(rhi_.Allocator(), &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation,
                             &stagingInfoOut),
            "vmaCreateBuffer(staging)");
    std::memcpy(stagingInfoOut.pMappedData, pixels.data(), static_cast<size_t>(imageSize));

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {kAtlasSize, kAtlasSize, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo imageAllocInfo{};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkCheck(vmaCreateImage(rhi_.Allocator(), &imageInfo, &imageAllocInfo, &atlasImage_, &atlasAllocation_, nullptr),
            "vmaCreateImage");

    RunOneTimeCommands([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = atlasImage_;
        toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &toDst);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {kAtlasSize, kAtlasSize, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, atlasImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toShaderRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.image = atlasImage_;
        toShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &toShaderRead);
    });

    vmaDestroyBuffer(rhi_.Allocator(), stagingBuffer, stagingAllocation);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = atlasImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkCheck(vkCreateImageView(rhi_.Device(), &viewInfo, nullptr, &atlasImageView_), "vkCreateImageView");

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkCheck(vkCreateSampler(rhi_.Device(), &samplerInfo, nullptr, &atlasSampler_), "vkCreateSampler");
}

void Renderer2D::CreateDescriptorSet() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VkCheck(vkCreateDescriptorSetLayout(rhi_.Device(), &layoutInfo, nullptr, &descriptorSetLayout_),
            "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkCheck(vkCreateDescriptorPool(rhi_.Device(), &poolInfo, nullptr, &descriptorPool_), "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;
    VkCheck(vkAllocateDescriptorSets(rhi_.Device(), &allocInfo, &descriptorSet_), "vkAllocateDescriptorSets");

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = atlasImageView_;
    imageInfo.sampler = atlasSampler_;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(rhi_.Device(), 1, &write, 0, nullptr);
}

void Renderer2D::CreatePipeline() {
    VkShaderModule vertModule = LoadShaderModule(rhi_.Device(), "sprite.vert.spv");
    VkShaderModule fragModule = LoadShaderModule(rhi_.Device(), "sprite.frag.spv");

    VkPipelineShaderStageCreateInfo vertStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(SpriteVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributes{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SpriteVertex, pos)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SpriteVertex, uv)};
    attributes[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteVertex, color)};

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 2;

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    VkCheck(vkCreatePipelineLayout(rhi_.Device(), &layoutInfo, nullptr, &pipelineLayout_), "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = rhi_.RenderPass();
    pipelineInfo.subpass = 0;

    VkCheck(vkCreateGraphicsPipelines(rhi_.Device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_),
            "vkCreateGraphicsPipelines");

    vkDestroyShaderModule(rhi_.Device(), vertModule, nullptr);
    vkDestroyShaderModule(rhi_.Device(), fragModule, nullptr);
}

void Renderer2D::CreateVertexBuffers() {
    vertexBuffers_.resize(VulkanRHI::MaxFramesInFlight());

    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(kMaxSprites) * kVerticesPerSprite * sizeof(SpriteVertex);

    for (auto& buf : vertexBuffers_) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocResult{};
        VkCheck(vmaCreateBuffer(rhi_.Allocator(), &bufferInfo, &allocInfo, &buf.buffer, &buf.allocation,
                                 &allocResult),
                "vmaCreateBuffer(vertex)");
        buf.mapped = allocResult.pMappedData;
    }
}

void Renderer2D::Render(VkCommandBuffer cmd, Registry& registry) {
    struct Instance {
        Entity entity;
        float depth;
    };
    std::vector<Instance> instances;

    auto view = registry.view<ScreenTransform2D, Sprite>();
    instances.reserve(view.size_hint());
    for (auto entity : view) {
        instances.push_back({entity, view.get<ScreenTransform2D>(entity).depth});
    }
    std::stable_sort(instances.begin(), instances.end(),
                      [](const Instance& a, const Instance& b) { return a.depth < b.depth; });

    Buffer& frameBuffer = vertexBuffers_[rhi_.CurrentFrameIndex()];
    auto* vertices = static_cast<SpriteVertex*>(frameBuffer.mapped);
    uint32_t vertexCount = 0;
    const uint32_t maxVertices = kMaxSprites * kVerticesPerSprite;

    for (const auto& inst : instances) {
        if (vertexCount + kVerticesPerSprite > maxVertices) {
            break;
        }
        const auto& t = view.get<ScreenTransform2D>(inst.entity);
        const auto& s = view.get<Sprite>(inst.entity);

        float x0 = t.x - s.width * 0.5f;
        float y0 = t.y - s.height * 0.5f;
        float x1 = t.x + s.width * 0.5f;
        float y1 = t.y + s.height * 0.5f;

        SpriteVertex quad[kVerticesPerSprite] = {
            {{x0, y0}, {s.u0, s.v0}, {s.r, s.g, s.b, s.a}}, {{x1, y0}, {s.u1, s.v0}, {s.r, s.g, s.b, s.a}},
            {{x1, y1}, {s.u1, s.v1}, {s.r, s.g, s.b, s.a}}, {{x0, y0}, {s.u0, s.v0}, {s.r, s.g, s.b, s.a}},
            {{x1, y1}, {s.u1, s.v1}, {s.r, s.g, s.b, s.a}}, {{x0, y1}, {s.u0, s.v1}, {s.r, s.g, s.b, s.a}},
        };
        std::memcpy(vertices + vertexCount, quad, sizeof(quad));
        vertexCount += kVerticesPerSprite;
    }

    if (vertexCount == 0) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

    VkExtent2D extent = rhi_.SwapchainExtent();
    float viewportSize[2] = {static_cast<float>(extent.width), static_cast<float>(extent.height)};
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(viewportSize), viewportSize);

    VkBuffer buffers[] = {frameBuffer.buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    vkCmdDraw(cmd, vertexCount, 1, 0, 0);
}

} // namespace aige
