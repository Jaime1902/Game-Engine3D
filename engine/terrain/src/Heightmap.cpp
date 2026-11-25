#include "ge3d/Heightmap.hpp"

#include "ge3d/Voxel.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace ge3d {

namespace {
std::atomic<std::uint64_t> revisionCounter{0};
std::uint64_t nextRevision() noexcept {
    return ++revisionCounter;
}
}

Heightmap::Heightmap(int chunksX, int chunksZ) : chunksX_(chunksX), chunksZ_(chunksZ) {
    if (chunksX < 1 || chunksZ < 1 || chunksX > maxChunks || chunksZ > maxChunks) {
        throw std::invalid_argument("Heightmap: tamano de terreno fuera de rango");
    }
    const auto count = static_cast<std::size_t>(verticesX()) * static_cast<std::size_t>(verticesZ());
    heights_.assign(count, 0.0f);
    weights_.assign(count, firstLayer);
    revisions_.assign(static_cast<std::size_t>(chunkCount()), 0);
    for (auto& revision : revisions_) revision = nextRevision();
}

float Heightmap::height(int x, int z) const noexcept {
    x = std::clamp(x, 0, verticesX() - 1);
    z = std::clamp(z, 0, verticesZ() - 1);
    return heights_[offset(x, z)];
}

Heightmap::Weights Heightmap::weights(int x, int z) const noexcept {
    x = std::clamp(x, 0, verticesX() - 1);
    z = std::clamp(z, 0, verticesZ() - 1);
    return weights_[offset(x, z)];
}

void Heightmap::touchVertex(int x, int z) noexcept {
    // La normal de un vértice depende de sus vecinos: se marcan los chunks a una celda de distancia.
    const int x0 = std::max(0, (x - 2) / chunkCells), x1 = std::min(chunksX_ - 1, (x + 1) / chunkCells);
    const int z0 = std::max(0, (z - 2) / chunkCells), z1 = std::min(chunksZ_ - 1, (z + 1) / chunkCells);
    const auto revision = nextRevision();
    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) revisions_[static_cast<std::size_t>(chunkIndex(cx, cz))] = revision;
    }
}

bool Heightmap::setHeight(int x, int z, float value) {
    if (!contains(x, z) || !std::isfinite(value)) return false;
    value = std::clamp(value, -maxHeight, maxHeight);
    auto& current = heights_[offset(x, z)];
    if (current == value) return false;
    current = value;
    touchVertex(x, z);
    return true;
}

bool Heightmap::setWeights(int x, int z, Weights value) {
    if (!contains(x, z)) return false;
    auto& current = weights_[offset(x, z)];
    if (current == value) return false;
    current = value;
    touchVertex(x, z);
    return true;
}

float Heightmap::surfaceHeight(float x, float z) const noexcept {
    x = std::clamp(x, 0.0f, static_cast<float>(cellsX()));
    z = std::clamp(z, 0.0f, static_cast<float>(cellsZ()));
    const int ix = std::min(static_cast<int>(x), cellsX() - 1);
    const int iz = std::min(static_cast<int>(z), cellsZ() - 1);
    const float fx = x - static_cast<float>(ix);
    const float fz = z - static_cast<float>(iz);
    const float h00 = height(ix, iz), h10 = height(ix + 1, iz), h01 = height(ix, iz + 1), h11 = height(ix + 1, iz + 1);
    // Triángulos (00, 10, 11) y (00, 11, 01).
    if (fx >= fz) return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
    return h00 + (h11 - h01) * fx + (h01 - h00) * fz;
}

std::array<float, 3> Heightmap::normal(int x, int z, float cellSize) const noexcept {
    const float dx = (height(x + 1, z) - height(x - 1, z)) / (2.0f * cellSize);
    const float dz = (height(x, z + 1) - height(x, z - 1)) / (2.0f * cellSize);
    const float length = std::sqrt(dx * dx + 1.0f + dz * dz);
    return {-dx / length, 1.0f / length, -dz / length};
}

Heightmap::Patch Heightmap::chunkData(int chunk) const {
    const auto [cx, cz] = chunkCoords(chunk);
    Patch patch;
    patch.heights.reserve(static_cast<std::size_t>(chunkVertices * chunkVertices));
    patch.weights.reserve(static_cast<std::size_t>(chunkVertices * chunkVertices));
    for (int z = 0; z < chunkVertices; ++z) {
        const auto row = offset(cx * chunkCells, cz * chunkCells + z);
        patch.heights.insert(patch.heights.end(), heights_.begin() + static_cast<std::ptrdiff_t>(row),
                             heights_.begin() + static_cast<std::ptrdiff_t>(row + chunkVertices));
        patch.weights.insert(patch.weights.end(), weights_.begin() + static_cast<std::ptrdiff_t>(row),
                             weights_.begin() + static_cast<std::ptrdiff_t>(row + chunkVertices));
    }
    return patch;
}

void Heightmap::setChunkData(int chunk, const Patch& patch) {
    constexpr auto size = static_cast<std::size_t>(chunkVertices * chunkVertices);
    if (chunk < 0 || chunk >= chunkCount() || patch.heights.size() != size || patch.weights.size() != size) {
        throw std::invalid_argument("Heightmap::setChunkData: datos de chunk invalidos");
    }
    const auto [cx, cz] = chunkCoords(chunk);
    for (int z = 0; z < chunkVertices; ++z) {
        for (int x = 0; x < chunkVertices; ++x) {
            const auto index = static_cast<std::size_t>(z * chunkVertices + x);
            setHeight(cx * chunkCells + x, cz * chunkCells + z, patch.heights[index]);
            setWeights(cx * chunkCells + x, cz * chunkCells + z, patch.weights[index]);
        }
    }
}

std::array<float, 2> Heightmap::chunkHeightRange(int chunk) const noexcept {
    const auto [cx, cz] = chunkCoords(chunk);
    float low = maxHeight, high = -maxHeight;
    for (int z = 0; z < chunkVertices; ++z) {
        const auto row = offset(cx * chunkCells, cz * chunkCells + z);
        const auto [minimum, maximum] = std::minmax_element(heights_.begin() + static_cast<std::ptrdiff_t>(row),
                                                            heights_.begin() + static_cast<std::ptrdiff_t>(row + chunkVertices));
        low = std::min(low, *minimum);
        high = std::max(high, *maximum);
    }
    return {low, high};
}

Heightmap::Weights blendTowardLayer(Heightmap::Weights weights, int layer, float amount) noexcept {
    amount = std::clamp(amount, 0.0f, 1.0f);
    float values[Heightmap::layerCount];
    float total = 0.0f;
    for (int i = 0; i < Heightmap::layerCount; ++i) {
        const float target = i == layer ? 255.0f : 0.0f;
        values[i] = static_cast<float>(layerWeight(weights, i));
        values[i] += (target - values[i]) * amount;
        total += values[i];
    }
    if (total <= 0.0f) return singleLayer(layer);
    // Normaliza a 255 y reparte el redondeo en la capa que se pinta para que la suma sea exacta.
    Heightmap::Weights result = 0;
    int sum = 0;
    int rounded[Heightmap::layerCount];
    for (int i = 0; i < Heightmap::layerCount; ++i) {
        rounded[i] = static_cast<int>(std::lround(values[i] * 255.0f / total));
        sum += rounded[i];
    }
    rounded[layer] = std::clamp(rounded[layer] + 255 - sum, 0, 255);
    for (int i = 0; i < Heightmap::layerCount; ++i) result |= static_cast<Heightmap::Weights>(rounded[i]) << (i * 8);
    return result;
}

// --- Generación ---------------------------------------------------------------------------------

std::shared_ptr<Heightmap> makeFlatHeightmap(const HeightmapSettings& settings, float height) {
    auto map = std::make_shared<Heightmap>(settings.chunksX, settings.chunksZ);
    for (int z = 0; z < map->verticesZ(); ++z) {
        for (int x = 0; x < map->verticesX(); ++x) map->setHeight(x, z, height);
    }
    return map;
}

std::shared_ptr<Heightmap> generateHeightmap(const HeightmapSettings& settings) {
    auto map = std::make_shared<Heightmap>(settings.chunksX, settings.chunksZ);
    const auto& g = settings.generator;
    TerrainGenerator noise;
    noise.seed = g.seed;
    noise.baseHeight = 0.0f;
    noise.amplitude = g.amplitude;
    noise.scale = g.scale;
    noise.octaves = g.octaves;
    const float cell = settings.cellSize;
    for (int z = 0; z < map->verticesZ(); ++z) {
        for (int x = 0; x < map->verticesX(); ++x) {
            map->setHeight(x, z, terrainHeight(noise, static_cast<float>(x) * cell, static_cast<float>(z) * cell));
        }
    }
    if (g.rockSlope > 0.0f) {
        // Pendiente suave: tierra; pendiente fuerte: roca. Transiciones graduales que el shader trama.
        const auto smooth = [](float edge0, float edge1, float v) {
            const float t = std::clamp((v - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        for (int z = 0; z < map->verticesZ(); ++z) {
            for (int x = 0; x < map->verticesX(); ++x) {
                const float slope = 1.0f - map->normal(x, z, cell)[1];
                const float rock = smooth(g.rockSlope * 0.8f, g.rockSlope * 1.2f, slope * 3.0f);
                const float dirt = smooth(g.rockSlope * 0.35f, g.rockSlope * 0.7f, slope * 3.0f) * (1.0f - rock);
                auto weights = Heightmap::firstLayer;
                if (dirt > 0.0f) weights = blendTowardLayer(weights, 1, dirt);
                if (rock > 0.0f) weights = blendTowardLayer(weights, 2, rock);
                map->setWeights(x, z, weights);
            }
        }
    }
    return map;
}

// --- Mallado ------------------------------------------------------------------------------------

std::vector<HeightVertex> meshHeightChunk(const Heightmap& map, int chunk, float cellSize) {
    const auto [cx, cz] = map.chunkCoords(chunk);
    std::vector<HeightVertex> vertices;
    vertices.reserve(static_cast<std::size_t>(Heightmap::chunkVertices * Heightmap::chunkVertices));
    const auto pack = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f)); };
    for (int z = 0; z < Heightmap::chunkVertices; ++z) {
        for (int x = 0; x < Heightmap::chunkVertices; ++x) {
            const int gx = cx * Heightmap::chunkCells + x, gz = cz * Heightmap::chunkCells + z;
            const auto n = map.normal(gx, gz, cellSize);
            vertices.push_back({map.height(gx, gz), pack(n[0]) | (pack(n[1]) << 10) | (pack(n[2]) << 20), map.weights(gx, gz),
                                static_cast<std::uint32_t>(x) | (static_cast<std::uint32_t>(z) << 16)});
        }
    }
    return vertices;
}

std::vector<std::uint32_t> heightmapChunkIndices() {
    std::vector<std::uint32_t> indices;
    constexpr int row = Heightmap::chunkVertices;
    indices.reserve(static_cast<std::size_t>(Heightmap::chunkCells * Heightmap::chunkCells * 6));
    for (int z = 0; z < Heightmap::chunkCells; ++z) {
        for (int x = 0; x < Heightmap::chunkCells; ++x) {
            const auto v00 = static_cast<std::uint32_t>(z * row + x);
            const auto v10 = v00 + 1, v01 = v00 + row, v11 = v00 + row + 1;
            indices.insert(indices.end(), {v00, v11, v10, v00, v01, v11});
        }
    }
    return indices;
}

}
