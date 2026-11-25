#pragma once

#include "GpuContext.hpp"
#include "ge3d/Scene.hpp"
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

// Memoria de vértices en páginas fijas de 4 MB (docs/guidelines.md, 2.1): nunca un VkBuffer por
// objeto ni realloc destructivo. Dentro de cada página, lista de huecos con fusión de vecinos.
// Liberar es diferido: la memoria vuelve a estar disponible cuando la GPU ya no puede leerla.
class GeometryPool {
public:
    static constexpr VkDeviceSize pageSize = 4 * 1024 * 1024;
    struct Allocation {
        int page{-1};
        VkDeviceSize offset{};
        VkDeviceSize size{};
        [[nodiscard]] bool valid() const noexcept { return page >= 0; }
    };

    void init(const GpuContext& context);
    void destroy();
    [[nodiscard]] Allocation allocate(VkDeviceSize size);
    void release(const Allocation& allocation, std::uint64_t frame);
    void collect(std::uint64_t completedFrame);
    [[nodiscard]] VkBuffer buffer(int page) const { return pages_[static_cast<std::size_t>(page)].buffer.handle; }
    [[nodiscard]] std::size_t pageCount() const noexcept { return pages_.size(); }
    [[nodiscard]] VkDeviceSize usedBytes() const noexcept { return used_; }

private:
    struct Page {
        GpuBuffer buffer;
        std::map<VkDeviceSize, VkDeviceSize> free;  // offset -> tamaño
    };
    struct Pending {
        Allocation allocation;
        std::uint64_t frame;
    };
    void insertFree(Page& page, VkDeviceSize offset, VkDeviceSize size);

    GpuContext context_{};
    std::vector<Page> pages_;
    std::vector<Pending> pending_;
    VkDeviceSize used_{};
};

struct TerrainStats {
    int terrains{};
    int chunks{};
    int visibleChunks{};
    int pendingChunks{};
    std::size_t quads{};
    std::size_t visibleQuads{};
    VkDeviceSize gpuBytes{};
};

// Malla y dibuja los terrenos de vóxeles:
//  - Solo remalla los chunks cuya revisión cambió, con un presupuesto por fotograma.
//  - Sube los vértices con un buffer de staging por fotograma en vuelo.
//  - Comparte un único índice de quads (0 1 2, 0 2 3...) para todos los chunks.
//  - Descarta los chunks fuera de la cámara antes de dibujar.
class TerrainRenderer {
public:
    static constexpr int framesInFlight = 2;
    static constexpr VkDeviceSize stagingPerFrame = 4 * 1024 * 1024;
    // Peor caso: tablero de ajedrez 3D, la mitad de los vóxeles sólidos con sus 6 caras.
    static constexpr int maxQuadsPerChunk = ge3d::VoxelGrid::chunkVolume / 2 * 6;

    // Función que traduce un id de material al índice de la tabla del shader.
    using MaterialLookup = std::function<std::uint32_t(const std::string&)>;

    void init(const GpuContext& context);
    void destroy();
    // Remalla lo pendiente (hasta el presupuesto) y graba las copias en el command buffer.
    void update(VkCommandBuffer command, int frameSlot, std::uint64_t frameNumber, std::uint64_t completedFrame,
                const ge3d::Scene& scene, const MaterialLookup& lookup, std::uint64_t materialsVersion);
    // Dibuja los chunks visibles. El pipeline de vóxeles y los descriptores ya deben estar enlazados.
    void draw(VkCommandBuffer command, VkPipelineLayout layout, const glm::mat4& viewProj, const ge3d::Scene& scene,
              const std::function<glm::mat4(const ge3d::Transform&)>& modelMatrix);
    [[nodiscard]] const TerrainStats& stats() const noexcept { return stats_; }

private:
    struct ChunkGpu {
        std::uint64_t revision{~0ull};
        GeometryPool::Allocation allocation;
        std::uint32_t quads{};
    };
    struct TerrainGpu {
        const ge3d::VoxelGrid* grid{};
        std::vector<ChunkGpu> chunks;
        std::array<std::uint16_t, 256> remap{};
        std::size_t remapSize{};
        std::uint64_t materialsVersion{~0ull};
        bool seen{};
    };
    void releaseTerrain(TerrainGpu& terrain, std::uint64_t frame);

    GpuContext context_{};
    GeometryPool pool_;
    GpuBuffer quadIndices_;
    GpuBuffer staging_;
    std::unordered_map<ge3d::EntityId, TerrainGpu> terrains_;
    TerrainStats stats_;
};
