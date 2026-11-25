#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ge3d {

// Rejilla de vóxeles organizada en chunks de 32³. Cada vóxel es un byte: 0 = aire y
// n > 0 = materials()[n - 1], un id del catálogo de materiales.
// Los datos de cada chunk son contiguos para copiarlos rápido (deshacer, remallado y guardado).
class VoxelGrid {
public:
    using Voxel = std::uint8_t;
    static constexpr int chunkSize = 32;
    static constexpr int chunkVolume = chunkSize * chunkSize * chunkSize;
    static constexpr int maxChunksXZ = 16;   // 512 vóxeles por lado
    static constexpr int maxChunksY = 4;     // 128 vóxeles de alto (máximo 33 MB por terreno)
    static constexpr std::size_t maxMaterials = 255;

    VoxelGrid(int chunksX, int chunksY, int chunksZ);

    [[nodiscard]] int chunksX() const noexcept { return chunksX_; }
    [[nodiscard]] int chunksY() const noexcept { return chunksY_; }
    [[nodiscard]] int chunksZ() const noexcept { return chunksZ_; }
    [[nodiscard]] int sizeX() const noexcept { return chunksX_ * chunkSize; }
    [[nodiscard]] int sizeY() const noexcept { return chunksY_ * chunkSize; }
    [[nodiscard]] int sizeZ() const noexcept { return chunksZ_ * chunkSize; }
    [[nodiscard]] int chunkCount() const noexcept { return chunksX_ * chunksY_ * chunksZ_; }
    [[nodiscard]] bool contains(int x, int y, int z) const noexcept;

    // Fuera de la rejilla todo es aire.
    [[nodiscard]] Voxel get(int x, int y, int z) const noexcept;
    // Devuelve true si cambió. Marca el chunk (y los vecinos si toca un borde) para remallar.
    bool set(int x, int y, int z, Voxel value);

    // Catálogo local de materiales del terreno (índice 1 = materials()[0]).
    [[nodiscard]] const std::vector<std::string>& materials() const noexcept { return materials_; }
    // Índice del material, añadiéndolo si no estaba. 0 si el catálogo está lleno o el id es vacío.
    Voxel materialIndex(const std::string& id);
    [[nodiscard]] Voxel findMaterial(const std::string& id) const noexcept;
    void setMaterials(std::vector<std::string> materials);

    [[nodiscard]] int chunkIndex(int cx, int cy, int cz) const noexcept { return (cz * chunksY_ + cy) * chunksX_ + cx; }
    [[nodiscard]] std::array<int, 3> chunkCoords(int index) const noexcept;
    [[nodiscard]] std::vector<Voxel> chunkData(int chunk) const;
    void setChunkData(int chunk, const std::vector<Voxel>& data);
    // Cambia cada vez que el contenido visible del chunk puede haber cambiado.
    [[nodiscard]] std::uint64_t chunkRevision(int chunk) const noexcept { return revisions_[static_cast<std::size_t>(chunk)]; }
    [[nodiscard]] std::size_t solidCount() const noexcept;

private:
    [[nodiscard]] std::size_t offset(int x, int y, int z) const noexcept;
    void touch(int cx, int cy, int cz) noexcept;
    void touchAround(int x, int y, int z) noexcept;

    int chunksX_;
    int chunksY_;
    int chunksZ_;
    std::vector<Voxel> data_;
    std::vector<std::uint64_t> revisions_;
    std::vector<std::string> materials_;
};

// --- Generación procedural ----------------------------------------------------------------------

struct TerrainGenerator {
    std::uint32_t seed{1234};
    float baseHeight{14.0f};    // altura media en vóxeles
    float amplitude{9.0f};      // variación de altura en vóxeles
    float scale{48.0f};         // vóxeles por periodo del ruido: más alto = colinas más anchas
    int octaves{4};
    int waterLevel{10};         // 0 = sin agua
    std::string top{"cesped"};
    std::string under{"tierra"};
    std::string deep{"piedra"};
    std::string shore{"arena"};
    std::string water{"agua"};
    friend bool operator==(const TerrainGenerator&, const TerrainGenerator&) = default;
};

struct TerrainSettings {
    int chunksX{4};
    int chunksY{2};
    int chunksZ{4};
    float voxelSize{1.0f};      // metros por vóxel
    TerrainGenerator generator{};
    friend bool operator==(const TerrainSettings&, const TerrainSettings&) = default;
};

// Ruido fractal determinista: misma semilla y coordenadas, mismo resultado en cualquier equipo.
[[nodiscard]] float terrainHeight(const TerrainGenerator& generator, float x, float z);
[[nodiscard]] std::shared_ptr<VoxelGrid> generateTerrain(const TerrainSettings& settings);
[[nodiscard]] std::shared_ptr<VoxelGrid> makeFlatTerrain(const TerrainSettings& settings, int height,
                                                         const std::string& material);

// --- Mallado ------------------------------------------------------------------------------------

// Vértice compacto de 8 bytes: posición dentro del chunk (0-32 por eje, 6 bits), normal (0-5:
// -X +X -Y +Y -Z +Z) y material. Cada 4 vértices forman un quad (índices 0 1 2, 0 2 3).
struct VoxelVertex {
    std::uint32_t position;     // x | y << 6 | z << 12 | normal << 18
    std::uint32_t material;
};

struct VoxelMesh {
    std::vector<VoxelVertex> vertices;
    [[nodiscard]] std::size_t quadCount() const noexcept { return vertices.size() / 4; }
};

// Greedy meshing: solo genera caras entre un vóxel sólido y aire, y fusiona en rectángulos las
// caras contiguas del mismo material. remap traduce el índice local de material al del renderer.
[[nodiscard]] VoxelMesh meshChunk(const VoxelGrid& grid, int chunk, const std::array<std::uint16_t, 256>* remap = nullptr);

}
