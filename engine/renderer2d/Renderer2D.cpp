#include "renderer2d/Renderer2D.h"

#include "renderer2d/Components.h"
#include "rhi/ShaderUtil.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
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

// Descriptor sets are one per texture; the pool bounds how many distinct
// textures can be live at once.
constexpr uint32_t kMaxTextures = 512;

// Procedural placeholder atlas: a 2x2 grid of bordered, distinctly colored
// quadrants (grass / dirt / player-marker / npc-marker). Sprites with no
// `texture` sample it; it's also the fallback when a texture fails to load.
ImageData GenerateAtlas() {
    ImageData image;
    image.width = kAtlasSize;
    image.height = kAtlasSize;
    image.rgba.resize(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4);
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
            image.rgba[idx + 0] = r;
            image.rgba[idx + 1] = g;
            image.rgba[idx + 2] = b;
            image.rgba[idx + 3] = 255;
        }
    }
    return image;
}

} // namespace

Renderer2D::Renderer2D(VulkanRHI& rhi, VkRenderPass targetRenderPass) : rhi_(rhi), targetRenderPass_(targetRenderPass) {
    CreateSampler();
    CreateDescriptorLayoutAndPool();
    builtinAtlas_ = UploadTexture(GenerateAtlas());
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

    for (auto& [path, texture] : textures_) {
        DestroyTexture(texture);
    }
    DestroyTexture(builtinAtlas_);

    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
    vkDestroySampler(device, sampler_, nullptr);
}

void Renderer2D::SetTextureLoader(TextureLoader loader) {
    loader_ = std::move(loader);
}

void Renderer2D::InvalidateTexture(const std::string& path) {
    auto it = textures_.find(path);
    if (it == textures_.end()) {
        return;
    }
    // Rare (asset reimport), so a full wait is simpler than deferring the
    // destroy until no in-flight frame references the texture.
    vkDeviceWaitIdle(rhi_.Device());
    DestroyTexture(it->second);
    textures_.erase(it);
}

std::optional<Renderer2D::TextureSize> Renderer2D::TextureSizeOf(const std::string& path) {
    const Texture& texture = Resolve(path);
    if (&texture == &builtinAtlas_) {
        return std::nullopt;
    }
    return TextureSize{texture.width, texture.height};
}

const Renderer2D::Texture& Renderer2D::Resolve(const std::string& path) {
    if (path.empty()) {
        return builtinAtlas_;
    }
    auto it = textures_.find(path);
    if (it != textures_.end()) {
        return it->second.image != VK_NULL_HANDLE ? it->second : builtinAtlas_;
    }

    // First use: load through the app-provided loader. A failure is cached
    // as an empty entry so a missing file is reported once, not every frame.
    Texture texture;
    std::optional<ImageData> image = loader_ ? loader_(path) : std::nullopt;
    if (image && image->width > 0 && image->height > 0 &&
        image->rgba.size() == static_cast<size_t>(image->width) * image->height * 4) {
        texture = UploadTexture(*image);
    } else {
        std::cerr << "[Renderer2D] texture unavailable, using placeholder: " << path << std::endl;
    }
    auto [inserted, ok] = textures_.emplace(path, texture);
    return inserted->second.image != VK_NULL_HANDLE ? inserted->second : builtinAtlas_;
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

void Renderer2D::CreateSampler() {
    // Nearest filtering: the target look is crisp pixel art.
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkCheck(vkCreateSampler(rhi_.Device(), &samplerInfo, nullptr, &sampler_), "vkCreateSampler");
}

void Renderer2D::CreateDescriptorLayoutAndPool() {
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
    poolSize.descriptorCount = kMaxTextures;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // textures can be invalidated
    poolInfo.maxSets = kMaxTextures;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkCheck(vkCreateDescriptorPool(rhi_.Device(), &poolInfo, nullptr, &descriptorPool_), "vkCreateDescriptorPool");
}

Renderer2D::Texture Renderer2D::UploadTexture(const ImageData& image) {
    Texture texture;
    texture.width = image.width;
    texture.height = image.height;
    VkDeviceSize imageSize = image.rgba.size();

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
    std::memcpy(stagingInfoOut.pMappedData, image.rgba.data(), static_cast<size_t>(imageSize));

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {image.width, image.height, 1};
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

    VkCheck(vmaCreateImage(rhi_.Allocator(), &imageInfo, &imageAllocInfo, &texture.image, &texture.allocation, nullptr),
            "vmaCreateImage");

    RunOneTimeCommands([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = texture.image;
        toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &toDst);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {image.width, image.height, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toShaderRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.image = texture.image;
        toShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &toShaderRead);
    });

    vmaDestroyBuffer(rhi_.Allocator(), stagingBuffer, stagingAllocation);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkCheck(vkCreateImageView(rhi_.Device(), &viewInfo, nullptr, &texture.view), "vkCreateImageView");

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;
    VkCheck(vkAllocateDescriptorSets(rhi_.Device(), &allocInfo, &texture.descriptorSet), "vkAllocateDescriptorSets");

    VkDescriptorImageInfo descriptorImage{};
    descriptorImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImage.imageView = texture.view;
    descriptorImage.sampler = sampler_;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texture.descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &descriptorImage;
    vkUpdateDescriptorSets(rhi_.Device(), 1, &write, 0, nullptr);

    return texture;
}

void Renderer2D::DestroyTexture(Texture& texture) {
    if (texture.descriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(rhi_.Device(), descriptorPool_, 1, &texture.descriptorSet);
    }
    if (texture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(rhi_.Device(), texture.view, nullptr);
    }
    if (texture.image != VK_NULL_HANDLE) {
        vmaDestroyImage(rhi_.Allocator(), texture.image, texture.allocation);
    }
    texture = Texture{};
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
    pipelineInfo.renderPass = targetRenderPass_;
    pipelineInfo.subpass = 0;

    VkCheck(vkCreateGraphicsPipelines(rhi_.Device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_),
            "vkCreateGraphicsPipelines");

    vkDestroyShaderModule(rhi_.Device(), vertModule, nullptr);
    vkDestroyShaderModule(rhi_.Device(), fragModule, nullptr);
}

void Renderer2D::CreateVertexBuffers() {
    vertexBuffers_.resize(VulkanRHI::MaxFramesInFlight());
    for (auto& buf : vertexBuffers_) {
        CreateVertexBuffer(buf, kInitialSprites * kVerticesPerSprite);
    }
}

void Renderer2D::EnsureCapacity(Buffer& buf, uint32_t vertices) {
    if (vertices <= buf.capacity) {
        return;
    }
    uint32_t capacity = std::max(buf.capacity, 1u);
    while (capacity < vertices) {
        capacity *= 2;
    }
    vmaDestroyBuffer(rhi_.Allocator(), buf.buffer, buf.allocation);
    buf = {};
    CreateVertexBuffer(buf, capacity);
}

void Renderer2D::CreateVertexBuffer(Buffer& buf, uint32_t vertices) {
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(vertices) * sizeof(SpriteVertex);
    {
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
        buf.capacity = vertices;
    }
}

void Renderer2D::RegisterTexture(const std::string& name, const ImageData& image) {
    if (textures_.contains(name)) {
        return;
    }
    textures_[name] = UploadTexture(image);
}

void Renderer2D::Render(VkCommandBuffer cmd, Registry& registry, const Camera2D& camera,
                        std::span<const OverlayLine> overlay, std::span<const UiQuad> ui, Registry* extra,
                        std::span<const SpriteItem> items) {
    struct Instance {
        const ScreenTransform2D* transform;
        const Sprite* sprite;
        const Texture* texture;
    };
    std::vector<Instance> instances;
    instances.reserve(items.size());
    for (const auto& item : items) {
        if (const Sprite* s = item.Drawn()) instances.push_back({&item.transform, s, &Resolve(s->texture)});
    }

    // Only what the camera sees, in painter's order (4.6), the scene's and
    // then `extra`'s, merged by depth (stable: extra stays in front at a tie).
    VkExtent2D target = rhi_.SwapchainExtent();
    size_t culled = 0;
    for (Registry* source : {&registry, extra}) {
        if (source == nullptr) {
            continue;
        }
        size_t skipped = 0;
        auto view = source->view<ScreenTransform2D, Sprite>();
        for (Entity entity : VisibleSprites(*source, camera, static_cast<float>(target.width),
                                            static_cast<float>(target.height), &skipped)) {
            // Resolve (and lazily load) textures before recording any draws.
            const auto& sprite = view.get<Sprite>(entity);
            instances.push_back({&view.get<ScreenTransform2D>(entity), &sprite, &Resolve(sprite.texture)});
        }
        if (source == &registry) {
            culled = skipped;
        }
    }
    if (extra != nullptr || !items.empty()) {
        std::stable_sort(instances.begin(), instances.end(), [](const Instance& a, const Instance& b) {
            return a.transform->depth < b.transform->depth;
        });
    }

    // Painter's order must be kept, so consecutive sprites sharing a texture
    // form one draw call and a texture change starts the next.
    struct Batch {
        const Texture* texture;
        uint32_t firstVertex;
        uint32_t vertexCount;
    };
    std::vector<Batch> batches;

    Buffer& frameBuffer = vertexBuffers_[rhi_.CurrentFrameIndex()];
    // No fixed sprite limit: the buffer grows to what this frame needs.
    EnsureCapacity(frameBuffer,
                   static_cast<uint32_t>((instances.size() + overlay.size() + ui.size()) * kVerticesPerSprite));
    auto* vertices = static_cast<SpriteVertex*>(frameBuffer.mapped);
    uint32_t vertexCount = 0;
    const uint32_t maxVertices = frameBuffer.capacity;

    auto appendToBatch = [&](const Texture* texture) {
        if (batches.empty() || batches.back().texture != texture) {
            batches.push_back({texture, vertexCount, 0});
        }
        batches.back().vertexCount += kVerticesPerSprite;
    };

    for (const auto& inst : instances) {
        if (vertexCount + kVerticesPerSprite > maxVertices) {
            break;
        }
        const auto& t = *inst.transform;
        const auto& s = *inst.sprite;

        // Upright rectangle or flat diamond (5.2), as two triangles.
        const SpriteQuad corners = SpriteQuadOf(t, s, flatDiamonds_);
        SpriteVertex v[4];
        for (int i = 0; i < 4; ++i) {
            float vx = 0.0f;
            float vy = 0.0f;
            camera.SceneToViewport(corners.x[i], corners.y[i], vx, vy);
            v[i] = {{vx, vy}, {corners.u[i], corners.v[i]}, {s.r, s.g, s.b, s.a}};
        }
        SpriteVertex quad[kVerticesPerSprite] = {v[0], v[1], v[2], v[0], v[2], v[3]};
        appendToBatch(inst.texture);
        std::memcpy(vertices + vertexCount, quad, sizeof(quad));
        vertexCount += kVerticesPerSprite;
    }

    // Overlay lines as thin quads expanded perpendicular to the line in
    // viewport space. UV (-1, -1) tells the fragment shader to skip the
    // texture and output the vertex color as-is.
    for (const auto& line : overlay) {
        if (vertexCount + kVerticesPerSprite > maxVertices) {
            break;
        }
        float ax = 0.0f;
        float ay = 0.0f;
        float bx = 0.0f;
        float by = 0.0f;
        camera.SceneToViewport(line.x0, line.y0, ax, ay);
        camera.SceneToViewport(line.x1, line.y1, bx, by);
        float dx = bx - ax;
        float dy = by - ay;
        float length = std::sqrt(dx * dx + dy * dy);
        if (length < 1e-4f) {
            continue;
        }
        float half = line.thickness * 0.5f;
        float nx = -dy / length * half;
        float ny = dx / length * half;
        // Extend along the line too, so outline corners meet without notches.
        float ex = dx / length * half;
        float ey = dy / length * half;
        ax -= ex;
        ay -= ey;
        bx += ex;
        by += ey;

        const float noUV[2] = {-1.0f, -1.0f};
        SpriteVertex quad[kVerticesPerSprite] = {
            {{ax + nx, ay + ny}, {noUV[0], noUV[1]}, {line.r, line.g, line.b, line.a}},
            {{bx + nx, by + ny}, {noUV[0], noUV[1]}, {line.r, line.g, line.b, line.a}},
            {{bx - nx, by - ny}, {noUV[0], noUV[1]}, {line.r, line.g, line.b, line.a}},
            {{ax + nx, ay + ny}, {noUV[0], noUV[1]}, {line.r, line.g, line.b, line.a}},
            {{bx - nx, by - ny}, {noUV[0], noUV[1]}, {line.r, line.g, line.b, line.a}},
            {{ax - nx, ay - ny}, {noUV[0], noUV[1]}, {line.r, line.g, line.b, line.a}},
        };
        appendToBatch(&builtinAtlas_); // any valid set works; overlay ignores the texture
        std::memcpy(vertices + vertexCount, quad, sizeof(quad));
        vertexCount += kVerticesPerSprite;
    }

    // In-game UI: already in viewport pixels. A named texture is a registered
    // one or an asset loaded on first use (a portrait); untextured quads use
    // UV -1 like overlays.
    for (const auto& q : ui) {
        if (vertexCount + kVerticesPerSprite > maxVertices) {
            break;
        }
        const Texture* texture = &builtinAtlas_;
        if (!q.texture.empty()) {
            const Texture& resolved = Resolve(q.texture);
            if (&resolved == &builtinAtlas_) {
                continue;
            }
            texture = &resolved;
        }
        SpriteVertex quad[kVerticesPerSprite] = {
            {{q.x0, q.y0}, {q.u0, q.v0}, {q.r, q.g, q.b, q.a}}, {{q.x1, q.y0}, {q.u1, q.v0}, {q.r, q.g, q.b, q.a}},
            {{q.x1, q.y1}, {q.u1, q.v1}, {q.r, q.g, q.b, q.a}}, {{q.x0, q.y0}, {q.u0, q.v0}, {q.r, q.g, q.b, q.a}},
            {{q.x1, q.y1}, {q.u1, q.v1}, {q.r, q.g, q.b, q.a}}, {{q.x0, q.y1}, {q.u0, q.v1}, {q.r, q.g, q.b, q.a}},
        };
        appendToBatch(texture);
        std::memcpy(vertices + vertexCount, quad, sizeof(quad));
        vertexCount += kVerticesPerSprite;
    }

    stats_ = {instances.size(), culled, batches.size(), vertexCount};
    if (vertexCount == 0) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkExtent2D extent = rhi_.SwapchainExtent();
    float viewportSize[2] = {static_cast<float>(extent.width), static_cast<float>(extent.height)};
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(viewportSize), viewportSize);

    VkBuffer buffers[] = {frameBuffer.buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    for (const auto& batch : batches) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                                &batch.texture->descriptorSet, 0, nullptr);
        vkCmdDraw(cmd, batch.vertexCount, 1, batch.firstVertex, 0);
    }
}

} // namespace aige
