#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ge3d {

// Terreno de malla: una rejilla regular de alturas (en metros) que se esculpe con pinceles
// (elevar, bajar, suavizar, aplanar) y se pinta con hasta 4 capas de material que se mezclan.
// Se organiza en chunks de 64 x 64 celdas para remallar y deshacer solo lo que cambia.
// Un chunk abarca los vértices [64·c, 64·c + 64] (incluido el borde que comparte con el siguiente).
class Heightmap {
public:
    static constexpr int chunkCells = 64;
    static constexpr int chunkVertices = chunkCells + 1;
    static constexpr int maxChunks = 16;          // 1.024 celdas por lado
    static constexpr int layerCount = 4;
    static constexpr float maxHeight = 500.0f;    // |altura| máxima en metros

    // 4 pesos de 8 bits, capa 0 en el byte bajo. Suman 255 (se normalizan al pintar).
    using Weights = std::uint32_t;
    static constexpr Weights firstLayer = 255u;

    // Contenido de un chunk (vértices del rectángulo incluido el borde) para deshacer.
    struct Patch {
        std::vector<float> heights;
        std::vector<Weights> weights;
        friend bool operator==(const Patch&, const Patch&) = default;
    };

    Heightmap(int chunksX, int chunksZ);

    [[nodiscard]] int chunksX() const noexcept { return chunksX_; }
    [[nodiscard]] int chunksZ() const noexcept { return chunksZ_; }
    [[nodiscard]] int cellsX() const noexcept { return chunksX_ * chunkCells; }
    [[nodiscard]] int cellsZ() const noexcept { return chunksZ_ * chunkCells; }
    [[nodiscard]] int verticesX() const noexcept { return cellsX() + 1; }
    [[nodiscard]] int verticesZ() const noexcept { return cellsZ() + 1; }
    [[nodiscard]] int chunkCount() const noexcept { return chunksX_ * chunksZ_; }
    [[nodiscard]] bool contains(int x, int z) const noexcept { return x >= 0 && z >= 0 && x < verticesX() && z < verticesZ(); }

    // Fuera de la rejilla devuelve el valor del borde más cercano.
    [[nodiscard]] float height(int x, int z) const noexcept;
    [[nodiscard]] Weights weights(int x, int z) const noexcept;
    // Devuelven true si cambió. Marcan para remallar los chunks que usan el vértice (y sus normales).
    bool setHeight(int x, int z, float height);
    bool setWeights(int x, int z, Weights weights);

    // Altura de la superficie en coordenadas de celda (x, z continuos), con la misma triangulación
    // que la malla: cada celda se parte por la diagonal (0,0)-(1,1).
    [[nodiscard]] float surfaceHeight(float x, float z) const noexcept;
    // Normal de la superficie en un vértice (diferencias centrales), con celdas de cellSize metros.
    [[nodiscard]] std::array<float, 3> normal(int x, int z, float cellSize) const noexcept;

    [[nodiscard]] int chunkIndex(int cx, int cz) const noexcept { return cz * chunksX_ + cx; }
    [[nodiscard]] std::array<int, 2> chunkCoords(int index) const noexcept { return {index % chunksX_, index / chunksX_}; }
    [[nodiscard]] Patch chunkData(int chunk) const;
    void setChunkData(int chunk, const Patch& patch);
    [[nodiscard]] std::uint64_t chunkRevision(int chunk) const noexcept { return revisions_[static_cast<std::size_t>(chunk)]; }
    // Alturas mínima y máxima de un chunk (para descartar por frustum y acotar rayos).
    [[nodiscard]] std::array<float, 2> chunkHeightRange(int chunk) const noexcept;

    [[nodiscard]] const std::vector<float>& heights() const noexcept { return heights_; }
    [[nodiscard]] const std::vector<Weights>& allWeights() const noexcept { return weights_; }

private:
    [[nodiscard]] std::size_t offset(int x, int z) const noexcept {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(verticesX()) + static_cast<std::size_t>(x);
    }
    void touchVertex(int x, int z) noexcept;

    int chunksX_;
    int chunksZ_;
    std::vector<float> heights_;
    std::vector<Weights> weights_;
    std::vector<std::uint64_t> revisions_;
};

[[nodiscard]] constexpr std::uint8_t layerWeight(Heightmap::Weights weights, int layer) noexcept {
    return static_cast<std::uint8_t>((weights >> (layer * 8)) & 0xFFu);
}
// Pesos que dan todo a una capa.
[[nodiscard]] constexpr Heightmap::Weights singleLayer(int layer) noexcept {
    return 255u << (layer * 8);
}
// Mezcla los pesos hacia una capa (amount 0..1) y los vuelve a normalizar a 255.
[[nodiscard]] Heightmap::Weights blendTowardLayer(Heightmap::Weights weights, int layer, float amount) noexcept;

// --- Generación ---------------------------------------------------------------------------------

struct HeightmapGenerator {
    std::uint32_t seed{1234};
    float amplitude{10.0f};     // metros de variación por encima y por debajo de y = 0
    float scale{56.0f};         // metros por periodo del ruido: más alto = colinas más anchas
    int octaves{4};
    float rockSlope{0.45f};     // pendiente (0 llano, 1 vertical) a partir de la que aparece la roca; 0 = no pintar
    friend bool operator==(const HeightmapGenerator&, const HeightmapGenerator&) = default;
};

struct HeightmapSettings {
    int chunksX{2};
    int chunksZ{2};
    float cellSize{1.0f};       // metros por celda
    // Capa 0 = base. Por defecto: césped, tierra, roca y arena.
    std::array<std::string, Heightmap::layerCount> layers{"cesped", "tierra", "piedra", "arena"};
    HeightmapGenerator generator{};
    friend bool operator==(const HeightmapSettings&, const HeightmapSettings&) = default;
};

// Relieve con el ruido fractal del generador; pinta tierra y roca según la pendiente.
[[nodiscard]] std::shared_ptr<Heightmap> generateHeightmap(const HeightmapSettings& settings);
[[nodiscard]] std::shared_ptr<Heightmap> makeFlatHeightmap(const HeightmapSettings& settings, float height = 0.0f);

// --- Mallado ------------------------------------------------------------------------------------

// Vértice de 16 bytes. La posición x, z sale de la celda; la altura va en metros.
struct HeightVertex {
    float height;
    std::uint32_t normal;       // x, y, z en 10 bits cada uno: (n * 0.5 + 0.5) * 1023
    Heightmap::Weights weights;
    std::uint32_t cell;         // x | z << 16, relativos al chunk (0-64)
};

// 65 x 65 vértices por chunk, en filas de z. Los índices son iguales para todos los chunks
// (heightmapChunkIndices) y siguen la diagonal de surfaceHeight.
[[nodiscard]] std::vector<HeightVertex> meshHeightChunk(const Heightmap& map, int chunk, float cellSize);
[[nodiscard]] std::vector<std::uint32_t> heightmapChunkIndices();

}
