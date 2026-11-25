#include "MaterialTextures.hpp"

#include "ge3d/SceneIO.hpp"
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cstring>
#include <map>

void submitOnce(const GpuContext& context, const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = context.commandPool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer command{};
    vkCheck(vkAllocateCommandBuffers(context.device, &allocate, &command), "Allocate one-time command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(command, &begin), "Begin one-time command buffer");
    record(command);
    vkCheck(vkEndCommandBuffer(command), "End one-time command buffer");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    vkCheck(vkQueueSubmit(context.queue, 1, &submit, VK_NULL_HANDLE), "Submit one-time command buffer");
    vkCheck(vkQueueWaitIdle(context.queue), "Wait one-time command buffer");
    vkFreeCommandBuffers(context.device, context.commandPool, 1, &command);
}

GpuBuffer createGpuBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    if (hostVisible) allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    else allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    GpuBuffer buffer;
    VmaAllocationInfo details{};
    vkCheck(vmaCreateBuffer(allocator, &info, &allocation, &buffer.handle, &buffer.allocation, &details), "Allocate buffer");
    buffer.mapped = details.pMappedData;
    return buffer;
}

void destroyGpuBuffer(VmaAllocator allocator, GpuBuffer& buffer) {
    if (buffer.handle) vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
    buffer = {};
}

namespace {

bool isPowerOfTwo(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

std::vector<std::uint8_t> missingTexture(int size) {
    // Cuadros magenta y negro: imposible de confundir con una textura real.
    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
    const int block = std::max(1, size / 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool magenta = ((x / block) + (y / block)) % 2 == 0;
            pixels.insert(pixels.end(), {static_cast<std::uint8_t>(magenta ? 230 : 20), 20,
                                         static_cast<std::uint8_t>(magenta ? 200 : 20), 255});
        }
    }
    return pixels;
}

// Siguiente nivel de mipmap: promedio de cada bloque 2x2.
std::vector<std::uint8_t> downsample(const std::vector<std::uint8_t>& source, int size) {
    const int half = size / 2;
    std::vector<std::uint8_t> result(static_cast<std::size_t>(half) * static_cast<std::size_t>(half) * 4);
    for (int y = 0; y < half; ++y) {
        for (int x = 0; x < half; ++x) {
            for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        sum += source[static_cast<std::size_t>(((y * 2 + dy) * size + (x * 2 + dx)) * 4 + c)];
                    }
                }
                result[static_cast<std::size_t>((y * half + x) * 4 + c)] = static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
    }
    return result;
}

std::uint32_t shaderTint(std::uint32_t rgb) {
    // Catálogo: 0xRRGGBB. Shader: 0xBBGGRR (rojo en el byte bajo).
    return ((rgb >> 16) & 0xFFu) | (rgb & 0xFF00u) | ((rgb & 0xFFu) << 16);
}

}

void MaterialTextures::load(const GpuContext& context, const ge3d::MaterialLibrary& library,
                            const std::filesystem::path& contentRoot, std::vector<std::string>& errors) {
    destroy();
    context_ = context;

    // 1. Texturas distintas que usan los materiales (capa 0 = "falta material").
    std::vector<std::string> paths;
    std::map<std::string, std::uint32_t> layerOf;
    const auto materialCount = std::min(library.materials().size(), maxMaterials);
    if (library.materials().size() > maxMaterials) errors.push_back("El catalogo tiene mas de 255 materiales; se ignoran los ultimos");
    for (std::size_t i = 0; i < materialCount; ++i) {
        const auto& material = library.materials()[i];
        for (const auto* path : {&material.topTexture(), &material.side, &material.bottomTexture()}) {
            if (!layerOf.contains(*path)) {
                layerOf[*path] = static_cast<std::uint32_t>(paths.size() + 1);
                paths.push_back(*path);
            }
        }
    }

    // 2. Lectura de los PNG. Todas las texturas deben medir lo mismo (la primera manda).
    std::vector<std::vector<std::uint8_t>> pixels(paths.size() + 1);
    size_ = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        try {
            auto image = ge3d::loadPng(contentRoot / ge3d::pathFromUtf8(paths[i]));
            if (image.width != image.height || !isPowerOfTwo(image.width) || image.width < 4 || image.width > 256) {
                errors.push_back(paths[i] + ": la textura debe ser cuadrada y potencia de dos (4 a 256 px)");
            } else if (size_ != 0 && image.width != size_) {
                errors.push_back(paths[i] + ": mide " + std::to_string(image.width) + " px y las demas " +
                                 std::to_string(size_) + " px");
            } else {
                size_ = image.width;
                pixels[i + 1] = std::move(image.rgba);
            }
        } catch (const std::exception& error) {
            errors.push_back(error.what());
        }
    }
    if (size_ == 0) size_ = 16;
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        if (pixels[i].empty()) {
            pixels[i] = missingTexture(size_);
            if (i > 0) layerOf[paths[i - 1]] = 0;
        }
    }
    layers_ = static_cast<int>(pixels.size());

    // 3. Tabla de materiales para el shader.
    table_.fill({0, 0, 0, 0xFFFFFF});
    ids_.clear();
    thumbnailLayers_.clear();
    for (std::size_t i = 0; i < materialCount; ++i) {
        const auto& material = library.materials()[i];
        table_[i + 1] = {layerOf[material.topTexture()], layerOf[material.side], layerOf[material.bottomTexture()],
                         shaderTint(material.tint)};
        ids_.push_back(material.id);
        thumbnailLayers_.push_back(layerOf[material.topTexture()]);
    }

    // 4. Imagen con mipmaps y subida a la GPU.
    std::uint32_t levels = 1;
    while ((size_ >> levels) > 0) ++levels;
    std::vector<std::uint8_t> staging;
    std::vector<VkBufferImageCopy> copies;
    for (std::uint32_t layer = 0; layer < static_cast<std::uint32_t>(layers_); ++layer) {
        auto level = pixels[layer];
        int levelSize = size_;
        for (std::uint32_t mip = 0; mip < levels; ++mip) {
            VkBufferImageCopy copy{};
            copy.bufferOffset = staging.size();
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1};
            copy.imageExtent = {static_cast<std::uint32_t>(levelSize), static_cast<std::uint32_t>(levelSize), 1};
            copies.push_back(copy);
            staging.insert(staging.end(), level.begin(), level.end());
            if (levelSize > 1) {
                level = downsample(level, levelSize);
                levelSize /= 2;
            }
        }
    }

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {static_cast<std::uint32_t>(size_), static_cast<std::uint32_t>(size_), 1};
    imageInfo.mipLevels = levels;
    imageInfo.arrayLayers = static_cast<std::uint32_t>(layers_);
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vkCheck(vmaCreateImage(context.allocator, &imageInfo, &allocationInfo, &image_, &allocation_, nullptr),
            "Allocate material texture array");

    auto upload = createGpuBuffer(context.allocator, staging.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(upload.mapped, staging.data(), staging.size());
    vmaFlushAllocation(context.allocator, upload.allocation, 0, VK_WHOLE_SIZE);
    const VkImageSubresourceRange everything{VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, static_cast<std::uint32_t>(layers_)};
    submitOnce(context, [&](VkCommandBuffer command) {
        VkImageMemoryBarrier2 toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = image_;
        toTransfer.subresourceRange = everything;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &toTransfer;
        vkCmdPipelineBarrier2(command, &dependency);
        vkCmdCopyBufferToImage(command, upload.handle, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(copies.size()), copies.data());
        VkImageMemoryBarrier2 toShader = toTransfer;
        toShader.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        toShader.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toShader.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toShader.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dependency.pImageMemoryBarriers = &toShader;
        vkCmdPipelineBarrier2(command, &dependency);
    });
    destroyGpuBuffer(context.allocator, upload);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = everything;
    vkCheck(vkCreateImageView(context.device, &viewInfo, nullptr, &view_), "Create material texture view");
    layerViews_.assign(static_cast<std::size_t>(layers_), VK_NULL_HANDLE);
    for (int layer = 0; layer < layers_; ++layer) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, static_cast<std::uint32_t>(layer), 1};
        vkCheck(vkCreateImageView(context.device, &viewInfo, nullptr, &layerViews_[static_cast<std::size_t>(layer)]),
                "Create material layer view");
    }

    // Pixel art: nearest en ampliación, reducción y entre mipmaps (docs/guidelines.md, 4.2).
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = static_cast<float>(levels);
    vkCheck(vkCreateSampler(context.device, &samplerInfo, nullptr, &sampler_), "Create material sampler");
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0;
    vkCheck(vkCreateSampler(context.device, &samplerInfo, nullptr, &thumbnailSampler_), "Create thumbnail sampler");
}

void MaterialTextures::registerThumbnails() {
    thumbnails_.clear();
    for (const auto layer : thumbnailLayers_) {
        const auto set = ImGui_ImplVulkan_AddTexture(thumbnailSampler_, layerViews_[layer], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        thumbnails_.push_back(reinterpret_cast<ImTextureID>(set));
    }
}

void MaterialTextures::unregisterThumbnails() {
    for (const auto id : thumbnails_) ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(id));
    thumbnails_.clear();
}

void MaterialTextures::destroy() {
    if (!context_.device) return;
    unregisterThumbnails();
    for (auto view : layerViews_) {
        if (view) vkDestroyImageView(context_.device, view, nullptr);
    }
    layerViews_.clear();
    if (view_) vkDestroyImageView(context_.device, view_, nullptr);
    if (sampler_) vkDestroySampler(context_.device, sampler_, nullptr);
    if (thumbnailSampler_) vkDestroySampler(context_.device, thumbnailSampler_, nullptr);
    if (image_) vmaDestroyImage(context_.allocator, image_, allocation_);
    view_ = VK_NULL_HANDLE;
    sampler_ = thumbnailSampler_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    layers_ = 0;
}

std::uint32_t MaterialTextures::gpuIndex(std::string_view materialId) const noexcept {
    for (std::size_t i = 0; i < ids_.size(); ++i) {
        if (ids_[i] == materialId) return static_cast<std::uint32_t>(i + 1);
    }
    return 0;
}
