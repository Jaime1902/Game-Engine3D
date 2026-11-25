#include "ModelRenderer.hpp"

#include "EditorLog.hpp"
#include "RenderTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

std::uint32_t packColor(const std::array<float, 4>& c) {
    const auto channel = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return channel(c[0]) | (channel(c[1]) << 8) | (channel(c[2]) << 16) | (channel(c[3]) << 24);
}

// true si la caja (en coordenadas del modelo) queda entera fuera de la cámara.
bool outsideFrustum(const glm::mat4& clipFromModel, const ge3d::Vec3& low, const ge3d::Vec3& high) {
    glm::vec4 corners[8];
    for (int i = 0; i < 8; ++i) {
        corners[i] = clipFromModel * glm::vec4(i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z, 1.0f);
    }
    const auto all = [&](auto test) { return std::all_of(std::begin(corners), std::end(corners), test); };
    return all([](const glm::vec4& c) { return c.x < -c.w; }) || all([](const glm::vec4& c) { return c.x > c.w; }) ||
           all([](const glm::vec4& c) { return c.y < -c.w; }) || all([](const glm::vec4& c) { return c.y > c.w; }) ||
           all([](const glm::vec4& c) { return c.z < 0.0f; }) || all([](const glm::vec4& c) { return c.z > c.w; });
}

}

void ModelRenderer::init(const GpuContext& context) {
    context_ = context;
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkCheck(vkCreateSampler(context.device, &sampler, nullptr, &sampler_), "Create model sampler");
    textureSources_ = {nullptr};   // capa 0: blanca (modelos sin textura)
    rebuildTextures();
}

void ModelRenderer::release(GpuModel& model) {
    destroyGpuBuffer(context_.allocator, model.vertices);
    destroyGpuBuffer(context_.allocator, model.indices);
}

void ModelRenderer::destroy() {
    for (auto& [path, model] : models_) release(model);
    models_.clear();
    if (textures_.view) vkDestroyImageView(context_.device, textures_.view, nullptr);
    if (textures_.image) vmaDestroyImage(context_.allocator, textures_.image, textures_.allocation);
    textures_ = {};
    if (sampler_) vkDestroySampler(context_.device, sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    keepAlive_.clear();
    textureSources_.clear();
}

void ModelRenderer::rebuildTextures() {
    // Todas las texturas reescaladas (nearest) a 128 x 128.
    const auto layers = static_cast<std::uint32_t>(textureSources_.size());
    const std::size_t layerBytes = static_cast<std::size_t>(textureSize) * textureSize * 4;
    std::vector<std::uint8_t> staging(layerBytes * layers, 255);
    for (std::uint32_t layer = 1; layer < layers; ++layer) {
        const auto* image = textureSources_[layer];
        if (!image || image->width <= 0 || image->height <= 0) continue;
        auto* out = staging.data() + layerBytes * layer;
        for (std::uint32_t y = 0; y < textureSize; ++y) {
            const auto sy = static_cast<std::size_t>(y * static_cast<std::uint32_t>(image->height) / textureSize);
            for (std::uint32_t x = 0; x < textureSize; ++x) {
                const auto sx = static_cast<std::size_t>(x * static_cast<std::uint32_t>(image->width) / textureSize);
                std::memcpy(out + (static_cast<std::size_t>(y) * textureSize + x) * 4,
                            image->rgba.data() + (sy * static_cast<std::size_t>(image->width) + sx) * 4, 4);
            }
        }
    }
    if (textures_.view) vkDestroyImageView(context_.device, textures_.view, nullptr);
    if (textures_.image) vmaDestroyImage(context_.allocator, textures_.image, textures_.allocation);
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {textureSize, textureSize, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vkCheck(vmaCreateImage(context_.allocator, &imageInfo, &allocationInfo, &textures_.image, &textures_.allocation, nullptr),
            "Allocate model texture array");
    textures_.layers = layers;
    auto upload = createGpuBuffer(context_.allocator, staging.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(upload.mapped, staging.data(), staging.size());
    vmaFlushAllocation(context_.allocator, upload.allocation, 0, VK_WHOLE_SIZE);
    const VkImageSubresourceRange everything{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    submitOnce(context_, [&](VkCommandBuffer command) {
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = textures_.image;
        barrier.subresourceRange = everything;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers};
        copy.imageExtent = {textureSize, textureSize, 1};
        vkCmdCopyBufferToImage(command, upload.handle, textures_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier2(command, &dependency);
    });
    destroyGpuBuffer(context_.allocator, upload);
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = textures_.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = everything;
    vkCheck(vkCreateImageView(context_.device, &viewInfo, nullptr, &textures_.view), "Create model texture view");
}

void ModelRenderer::upload(GpuModel& model, const ge3d::ModelData& data) {
    release(model);
    model.data = &data;
    const VkDeviceSize vertexBytes = data.vertices.size() * sizeof(ge3d::ModelVertex);
    const VkDeviceSize indexBytes = data.indices.size() * sizeof(std::uint32_t);
    model.vertices = createGpuBuffer(context_.allocator, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    model.indices = createGpuBuffer(context_.allocator, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    auto staging = createGpuBuffer(context_.allocator, vertexBytes + indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, data.vertices.data(), vertexBytes);
    std::memcpy(static_cast<std::uint8_t*>(staging.mapped) + vertexBytes, data.indices.data(), indexBytes);
    vmaFlushAllocation(context_.allocator, staging.allocation, 0, VK_WHOLE_SIZE);
    submitOnce(context_, [&](VkCommandBuffer command) {
        const VkBufferCopy vertexCopy{0, 0, vertexBytes};
        const VkBufferCopy indexCopy{vertexBytes, 0, indexBytes};
        vkCmdCopyBuffer(command, staging.handle, model.vertices.handle, 1, &vertexCopy);
        vkCmdCopyBuffer(command, staging.handle, model.indices.handle, 1, &indexCopy);
    });
    destroyGpuBuffer(context_.allocator, staging);
}

bool ModelRenderer::prepare(const ge3d::Scene& scene, ge3d::ModelLibrary& library) {
    bool texturesChanged = false;
    bool waited = false;
    for (const auto& entity : scene.entities()) {
        if (!entity.model || entity.model->path.empty()) continue;
        const auto data = library.cached(entity.model->path);
        if (!data) continue;
        auto& model = models_[entity.model->path];
        if (model.data == data.get()) continue;
        // Modelo nuevo o reimportado: la GPU puede estar usando el anterior, se espera una vez.
        if (!waited && model.data) {
            vkDeviceWaitIdle(context_.device);
            waited = true;
        }
        keepAlive_.push_back(data);
        upload(model, *data);
        model.textureLayers.clear();
        for (const auto& texture : data->textures) {
            if (textureSources_.size() >= maxTextures) {
                model.textureLayers.push_back(0);
                editorLog().add(LogLevel::warning, "Modelos: mas de 256 texturas; las siguientes se ven en blanco");
                continue;
            }
            model.textureLayers.push_back(static_cast<int>(textureSources_.size()));
            textureSources_.push_back(&texture);
            texturesChanged = true;
        }
        model.parts.clear();
        for (const auto& part : data->parts) {
            Part gpuPart{part.firstIndex, part.indexCount, -1, 0xFFFFFFFFu};
            if (part.material >= 0 && part.material < static_cast<int>(data->materials.size())) {
                const auto& material = data->materials[static_cast<std::size_t>(part.material)];
                gpuPart.baseColor = packColor(material.baseColor);
                if (material.texture >= 0 && material.texture < static_cast<int>(model.textureLayers.size())) {
                    gpuPart.layer = model.textureLayers[static_cast<std::size_t>(material.texture)];
                }
            }
            model.parts.push_back(gpuPart);
        }
    }
    if (texturesChanged) {
        if (!waited) vkDeviceWaitIdle(context_.device);
        rebuildTextures();
    }
    return texturesChanged;
}

void ModelRenderer::draw(VkCommandBuffer command, VkPipelineLayout layout, const glm::mat4& viewProj, const ge3d::Scene& scene,
                         ge3d::ModelLibrary& library, const std::function<glm::mat4(const ge3d::Entity&)>& worldMatrix,
                         ge3d::EntityId hidden) {
    stats_ = {};
    stats_.models = static_cast<int>(models_.size());
    for (const auto& entity : scene.entities()) {
        if (!entity.model || !ge3d::activeInHierarchy(scene, entity)) continue;
        if (hidden && (entity.id == hidden || ge3d::isAncestor(scene, hidden, entity.id))) continue;
        const auto found = models_.find(entity.model->path);
        if (found == models_.end() || !found->second.data) continue;
        const auto& model = found->second;
        const glm::mat4 world = worldMatrix(entity);
        if (outsideFrustum(viewProj * world, model.data->boundsMin, model.data->boundsMax)) continue;
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &model.vertices.handle, &offset);
        vkCmdBindIndexBuffer(command, model.indices.handle, 0, VK_INDEX_TYPE_UINT32);
        for (const auto& part : model.parts) {
            ScenePush push;
            push.model = world;
            push.grid.w = modeModel;
            push.data = {static_cast<std::uint32_t>(part.layer + 1), part.baseColor, 0u, 0u};
            vkCmdPushConstants(command, layout, pushStages, 0, sizeof(push), &push);
            vkCmdDrawIndexed(command, part.indexCount, 1, part.firstIndex, 0, 0);
        }
        ++stats_.drawn;
        stats_.triangles += model.data->triangleCount();
    }
    (void)library;
}
