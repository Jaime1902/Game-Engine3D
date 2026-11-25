#include "TerrainRenderer.hpp"

#include "EditorLog.hpp"
#include "RenderTypes.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cstring>

// --- GeometryPool -------------------------------------------------------------------------------

void GeometryPool::init(const GpuContext& context) {
    context_ = context;
}

void GeometryPool::destroy() {
    for (auto& page : pages_) destroyGpuBuffer(context_.allocator, page.buffer);
    pages_.clear();
    pending_.clear();
    used_ = 0;
}

void GeometryPool::insertFree(Page& page, VkDeviceSize offset, VkDeviceSize size) {
    auto [it, inserted] = page.free.emplace(offset, size);
    (void)inserted;
    // Fusión con el hueco siguiente y con el anterior.
    if (auto next = std::next(it); next != page.free.end() && it->first + it->second == next->first) {
        it->second += next->second;
        page.free.erase(next);
    }
    if (it != page.free.begin()) {
        auto previous = std::prev(it);
        if (previous->first + previous->second == it->first) {
            previous->second += it->second;
            page.free.erase(it);
        }
    }
}

GeometryPool::Allocation GeometryPool::allocate(VkDeviceSize size) {
    size = (size + 15) & ~VkDeviceSize{15};
    if (size == 0 || size > pageSize) return {};
    for (std::size_t index = 0; index < pages_.size(); ++index) {
        auto& page = pages_[index];
        for (auto it = page.free.begin(); it != page.free.end(); ++it) {
            if (it->second < size) continue;
            const auto offset = it->first;
            const auto remaining = it->second - size;
            page.free.erase(it);
            if (remaining) page.free.emplace(offset + size, remaining);
            used_ += size;
            return {static_cast<int>(index), offset, size};
        }
    }
    // Sin hueco: página nueva, independiente de las demás (sin mover nada ya subido).
    Page page;
    page.buffer = createGpuBuffer(context_.allocator, pageSize,
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    if (size < pageSize) page.free.emplace(size, pageSize - size);
    pages_.push_back(std::move(page));
    used_ += size;
    return {static_cast<int>(pages_.size() - 1), 0, size};
}

void GeometryPool::release(const Allocation& allocation, std::uint64_t frame) {
    if (allocation.valid()) pending_.push_back({allocation, frame});
}

void GeometryPool::collect(std::uint64_t completedFrame) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->frame <= completedFrame) {
            insertFree(pages_[static_cast<std::size_t>(it->allocation.page)], it->allocation.offset, it->allocation.size);
            used_ -= it->allocation.size;
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

// --- TerrainRenderer ----------------------------------------------------------------------------

void TerrainRenderer::init(const GpuContext& context) {
    context_ = context;
    pool_.init(context);
    staging_ = createGpuBuffer(context.allocator, stagingPerFrame * framesInFlight, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);

    // Índices compartidos: el quad k usa los vértices 4k..4k+3.
    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(maxQuadsPerChunk) * 6);
    for (std::uint32_t quad = 0; quad < static_cast<std::uint32_t>(maxQuadsPerChunk); ++quad) {
        const std::uint32_t base = quad * 4;
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    const VkDeviceSize bytes = indices.size() * sizeof(std::uint32_t);
    quadIndices_ = createGpuBuffer(context.allocator, bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    auto upload = createGpuBuffer(context.allocator, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(upload.mapped, indices.data(), bytes);
    vmaFlushAllocation(context.allocator, upload.allocation, 0, VK_WHOLE_SIZE);
    submitOnce(context, [&](VkCommandBuffer command) {
        const VkBufferCopy copy{0, 0, bytes};
        vkCmdCopyBuffer(command, upload.handle, quadIndices_.handle, 1, &copy);
    });
    destroyGpuBuffer(context.allocator, upload);
}

void TerrainRenderer::destroy() {
    terrains_.clear();
    pool_.destroy();
    destroyGpuBuffer(context_.allocator, quadIndices_);
    destroyGpuBuffer(context_.allocator, staging_);
}

void TerrainRenderer::releaseTerrain(TerrainGpu& terrain, std::uint64_t frame) {
    for (auto& chunk : terrain.chunks) {
        pool_.release(chunk.allocation, frame);
        chunk = {};
    }
}

void TerrainRenderer::update(VkCommandBuffer command, int frameSlot, std::uint64_t frameNumber, std::uint64_t completedFrame,
                             const ge3d::Scene& scene, const MaterialLookup& lookup, std::uint64_t materialsVersion) {
    pool_.collect(completedFrame);
    for (auto& [id, terrain] : terrains_) terrain.seen = false;

    // Presupuesto por fotograma: tiempo de mallado y espacio de staging.
    const auto start = std::chrono::steady_clock::now();
    constexpr auto timeBudget = std::chrono::microseconds(6000);
    const VkDeviceSize stagingBase = static_cast<VkDeviceSize>(frameSlot) * stagingPerFrame;
    VkDeviceSize stagingUsed = 0;
    std::vector<std::pair<VkBuffer, VkBufferCopy>> copies;
    bool outOfBudget = false;

    stats_ = {};
    for (const auto& entity : scene.entities()) {
        if (!entity.terrain || !entity.terrain->voxels) continue;
        const auto& grid = *entity.terrain->voxels;
        auto& terrain = terrains_[entity.id];
        terrain.seen = true;
        ++stats_.terrains;
        if (terrain.grid != &grid || terrain.chunks.size() != static_cast<std::size_t>(grid.chunkCount())) {
            releaseTerrain(terrain, frameNumber);
            terrain.grid = &grid;
            terrain.chunks.assign(static_cast<std::size_t>(grid.chunkCount()), {});
            terrain.remapSize = 0;
        }

        // Materiales del terreno -> índices del shader. Si cambia alguno ya usado, se remalla todo.
        std::array<std::uint16_t, 256> remap{};
        const auto& materials = grid.materials();
        for (std::size_t i = 0; i < materials.size(); ++i) remap[i + 1] = static_cast<std::uint16_t>(lookup(materials[i]));
        bool remapChanged = terrain.materialsVersion != materialsVersion;
        for (std::size_t i = 1; i <= std::min(terrain.remapSize, materials.size()) && !remapChanged; ++i) {
            remapChanged = remap[i] != terrain.remap[i];
        }
        if (remapChanged) {
            for (auto& chunk : terrain.chunks) chunk.revision = ~0ull;
        }
        terrain.remap = remap;
        terrain.remapSize = materials.size();
        terrain.materialsVersion = materialsVersion;

        for (int index = 0; index < grid.chunkCount(); ++index) {
            auto& chunk = terrain.chunks[static_cast<std::size_t>(index)];
            ++stats_.chunks;
            if (chunk.revision == grid.chunkRevision(index)) {
                stats_.quads += chunk.quads;
                continue;
            }
            if (outOfBudget || std::chrono::steady_clock::now() - start > timeBudget) {
                outOfBudget = true;
                ++stats_.pendingChunks;
                stats_.quads += chunk.quads;
                continue;
            }
            const auto mesh = ge3d::meshChunk(grid, index, &terrain.remap);
            const VkDeviceSize bytes = mesh.vertices.size() * sizeof(ge3d::VoxelVertex);
            if (stagingUsed + bytes > stagingPerFrame) {
                outOfBudget = true;
                ++stats_.pendingChunks;
                stats_.quads += chunk.quads;
                continue;
            }
            pool_.release(chunk.allocation, frameNumber);
            chunk.allocation = {};
            chunk.quads = static_cast<std::uint32_t>(mesh.quadCount());
            if (bytes > 0) {
                chunk.allocation = pool_.allocate(bytes);
                if (!chunk.allocation.valid()) {
                    editorLog().add(LogLevel::error, "Terreno: un chunk no cabe en una pagina de geometria");
                    chunk.quads = 0;
                } else {
                    std::memcpy(static_cast<std::uint8_t*>(staging_.mapped) + stagingBase + stagingUsed, mesh.vertices.data(), bytes);
                    copies.push_back({pool_.buffer(chunk.allocation.page), {stagingBase + stagingUsed, chunk.allocation.offset, bytes}});
                    stagingUsed += (bytes + 15) & ~VkDeviceSize{15};
                }
            }
            chunk.revision = grid.chunkRevision(index);
            stats_.quads += chunk.quads;
        }
    }

    // Terrenos borrados de la escena: su memoria se libera cuando la GPU termine con ella.
    for (auto it = terrains_.begin(); it != terrains_.end();) {
        if (!it->second.seen) {
            releaseTerrain(it->second, frameNumber);
            it = terrains_.erase(it);
        } else {
            ++it;
        }
    }

    if (!copies.empty()) {
        vmaFlushAllocation(context_.allocator, staging_.allocation, stagingBase, stagingUsed);
        for (const auto& [buffer, copy] : copies) vkCmdCopyBuffer(command, staging_.handle, buffer, 1, &copy);
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
    }
    stats_.gpuBytes = pool_.usedBytes();
}

namespace {
// true si la caja (en coordenadas del modelo) queda completamente fuera de la cámara.
bool outsideFrustum(const glm::mat4& clipFromModel, glm::vec3 minimum, glm::vec3 maximum) {
    glm::vec4 corners[8];
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner{i & 1 ? maximum.x : minimum.x, i & 2 ? maximum.y : minimum.y, i & 4 ? maximum.z : minimum.z};
        corners[i] = clipFromModel * glm::vec4(corner, 1.0f);
    }
    const auto allOutside = [&](auto test) {
        for (const auto& c : corners) {
            if (!test(c)) return false;
        }
        return true;
    };
    return allOutside([](const glm::vec4& c) { return c.x < -c.w; }) || allOutside([](const glm::vec4& c) { return c.x > c.w; }) ||
           allOutside([](const glm::vec4& c) { return c.y < -c.w; }) || allOutside([](const glm::vec4& c) { return c.y > c.w; }) ||
           allOutside([](const glm::vec4& c) { return c.z < 0.0f; }) || allOutside([](const glm::vec4& c) { return c.z > c.w; });
}
}

void TerrainRenderer::draw(VkCommandBuffer command, VkPipelineLayout layout, const glm::mat4& viewProj, const ge3d::Scene& scene,
                           const std::function<glm::mat4(const ge3d::Transform&)>& modelMatrix) {
    vkCmdBindIndexBuffer(command, quadIndices_.handle, 0, VK_INDEX_TYPE_UINT32);
    int boundPage = -1;
    for (const auto& entity : scene.entities()) {
        if (!entity.terrain || !entity.terrain->voxels) continue;
        const auto found = terrains_.find(entity.id);
        if (found == terrains_.end() || found->second.grid != entity.terrain->voxels.get()) continue;
        const auto& terrain = found->second;
        const float voxel = entity.terrain->settings.voxelSize;
        const glm::mat4 model = modelMatrix(entity.transform) * glm::scale(glm::mat4{1.0f}, glm::vec3{voxel});
        const glm::mat4 clipFromModel = viewProj * model;
        for (int index = 0; index < static_cast<int>(terrain.chunks.size()); ++index) {
            const auto& chunk = terrain.chunks[static_cast<std::size_t>(index)];
            if (!chunk.quads || !chunk.allocation.valid()) continue;
            const auto coords = terrain.grid->chunkCoords(index);
            const glm::vec3 origin = glm::vec3(coords[0], coords[1], coords[2]) * static_cast<float>(ge3d::VoxelGrid::chunkSize);
            if (outsideFrustum(clipFromModel, origin, origin + glm::vec3(static_cast<float>(ge3d::VoxelGrid::chunkSize)))) continue;
            if (chunk.allocation.page != boundPage) {
                const VkBuffer buffer = pool_.buffer(chunk.allocation.page);
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(command, 0, 1, &buffer, &offset);
                boundPage = chunk.allocation.page;
            }
            ScenePush push;
            push.model = model;
            push.grid.w = modeVoxel;
            push.data = {static_cast<std::uint32_t>(origin.x), static_cast<std::uint32_t>(origin.y), static_cast<std::uint32_t>(origin.z), 0u};
            vkCmdPushConstants(command, layout, pushStages, 0, sizeof(push), &push);
            vkCmdDrawIndexed(command, chunk.quads * 6, 1, 0,
                             static_cast<std::int32_t>(chunk.allocation.offset / sizeof(ge3d::VoxelVertex)), 0);
            ++stats_.visibleChunks;
            stats_.visibleQuads += chunk.quads;
        }
    }
}
