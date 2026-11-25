#include "ge3d/Heightmap.hpp"

#include "ge3d/Voxel.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
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
    water_.assign(count, 0.0f);
    revisions_.assign(static_cast<std::size_t>(chunkCount()), 0);
    waterRevisions_.assign(static_cast<std::size_t>(chunkCount()), 0);
    for (auto& revision : revisions_) revision = nextRevision();
    for (auto& revision : waterRevisions_) revision = nextRevision();
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

void Heightmap::touchWaterVertex(int x, int z) noexcept {
    const int x0 = std::max(0, (x - 2) / chunkCells), x1 = std::min(chunksX_ - 1, (x + 1) / chunkCells);
    const int z0 = std::max(0, (z - 2) / chunkCells), z1 = std::min(chunksZ_ - 1, (z + 1) / chunkCells);
    const auto revision = nextRevision();
    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) waterRevisions_[static_cast<std::size_t>(chunkIndex(cx, cz))] = revision;
    }
}

void Heightmap::touchWaterChunk(int chunk) noexcept {
    // La malla del agua de un chunk usa la fila de vértices del vecino (borde y normales): se marcan todos.
    const auto [cx, cz] = chunkCoords(chunk);
    const auto revision = nextRevision();
    for (int z = std::max(0, cz - 1); z <= std::min(chunksZ_ - 1, cz + 1); ++z) {
        for (int x = std::max(0, cx - 1); x <= std::min(chunksX_ - 1, cx + 1); ++x) {
            waterRevisions_[static_cast<std::size_t>(chunkIndex(x, z))] = revision;
        }
    }
}

float Heightmap::water(int x, int z) const noexcept {
    x = std::clamp(x, 0, verticesX() - 1);
    z = std::clamp(z, 0, verticesZ() - 1);
    return water_[offset(x, z)];
}

bool Heightmap::setWater(int x, int z, float depth) {
    if (!contains(x, z) || !std::isfinite(depth)) return false;
    depth = std::clamp(depth, 0.0f, 2.0f * maxHeight);
    auto& current = water_[offset(x, z)];
    if (current == depth) return false;
    current = depth;
    touchWaterVertex(x, z);
    return true;
}

bool Heightmap::hasWater() const noexcept {
    return std::any_of(water_.begin(), water_.end(), [](float d) { return d > 0.0f; });
}

bool Heightmap::chunkHasWater(int chunk) const noexcept {
    const auto [cx, cz] = chunkCoords(chunk);
    for (int z = 0; z < chunkVertices; ++z) {
        const auto row = offset(cx * chunkCells, cz * chunkCells + z);
        for (int x = 0; x < chunkVertices; ++x) {
            if (water_[row + static_cast<std::size_t>(x)] > 0.0f) return true;
        }
    }
    return false;
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
        patch.water.insert(patch.water.end(), water_.begin() + static_cast<std::ptrdiff_t>(row),
                           water_.begin() + static_cast<std::ptrdiff_t>(row + chunkVertices));
    }
    return patch;
}

void Heightmap::setChunkData(int chunk, const Patch& patch) {
    constexpr auto size = static_cast<std::size_t>(chunkVertices * chunkVertices);
    if (chunk < 0 || chunk >= chunkCount() || patch.heights.size() != size || patch.weights.size() != size ||
        patch.water.size() != size) {
        throw std::invalid_argument("Heightmap::setChunkData: datos de chunk invalidos");
    }
    const auto [cx, cz] = chunkCoords(chunk);
    for (int z = 0; z < chunkVertices; ++z) {
        for (int x = 0; x < chunkVertices; ++x) {
            const auto index = static_cast<std::size_t>(z * chunkVertices + x);
            setHeight(cx * chunkCells + x, cz * chunkCells + z, patch.heights[index]);
            setWeights(cx * chunkCells + x, cz * chunkCells + z, patch.weights[index]);
            setWater(cx * chunkCells + x, cz * chunkCells + z, patch.water[index]);
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
    const bool sea = g.water;
    const float level = g.waterLevel;
    if (g.rockSlope > 0.0f || sea) {
        // Pendiente suave: tierra; pendiente fuerte: roca. Transiciones graduales que el shader trama.
        const auto smooth = [](float edge0, float edge1, float v) {
            const float t = std::clamp((v - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        for (int z = 0; z < map->verticesZ(); ++z) {
            for (int x = 0; x < map->verticesX(); ++x) {
                const float slope = 1.0f - map->normal(x, z, cell)[1];
                const float rock = g.rockSlope <= 0.0f ? 0.0f : smooth(g.rockSlope * 0.8f, g.rockSlope * 1.2f, slope * 3.0f);
                const float dirt = g.rockSlope <= 0.0f ? 0.0f : smooth(g.rockSlope * 0.35f, g.rockSlope * 0.7f, slope * 3.0f) * (1.0f - rock);
                auto weights = Heightmap::firstLayer;
                if (dirt > 0.0f) weights = blendTowardLayer(weights, 1, dirt);
                if (rock > 0.0f) weights = blendTowardLayer(weights, 2, rock);
                // Playa: arena desde un poco bajo el agua hasta algo más de un metro por encima.
                const float height = map->height(x, z);
                if (sea && height < level + 1.4f) {
                    const float beach = smooth(level + 1.4f, level + 0.6f, height) * (1.0f - rock);
                    if (beach > 0.0f) weights = blendTowardLayer(weights, 3, beach);
                }
                map->setWeights(x, z, weights);
                if (sea && height < level) map->setWater(x, z, level - height);
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


std::vector<WaterVertex> meshWaterChunk(const Heightmap& map, int chunk, float cellSize) {
    const auto [cx, cz] = map.chunkCoords(chunk);
    std::vector<WaterVertex> vertices;
    vertices.reserve(static_cast<std::size_t>(Heightmap::chunkVertices * Heightmap::chunkVertices));
    const auto pack = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f)); };
    // Superficie: en un vértice seco se usa la de un vecino mojado (o un poco bajo el terreno), así el
    // agua corta el terreno justo en la orilla en lugar de caer en rampa hasta el suelo.
    const auto surface = [&](int x, int z) {
        const float depth = map.water(x, z);
        if (depth > 0.0f) return map.height(x, z) + depth;
        float best = -Heightmap::maxHeight * 2.0f;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx || dz) && map.contains(x + dx, z + dz) && map.water(x + dx, z + dz) > 0.0f) {
                    best = std::max(best, map.height(x + dx, z + dz) + map.water(x + dx, z + dz));
                }
            }
        }
        return best > -Heightmap::maxHeight ? std::min(best, map.height(x, z) + 0.02f) : map.height(x, z) - 0.25f;
    };
    for (int z = 0; z < Heightmap::chunkVertices; ++z) {
        for (int x = 0; x < Heightmap::chunkVertices; ++x) {
            const int gx = cx * Heightmap::chunkCells + x, gz = cz * Heightmap::chunkCells + z;
            const float level = surface(gx, gz);
            const float dx = (surface(std::min(gx + 1, map.verticesX() - 1), gz) - surface(std::max(gx - 1, 0), gz)) / (2.0f * cellSize);
            const float dz = (surface(gx, std::min(gz + 1, map.verticesZ() - 1)) - surface(gx, std::max(gz - 1, 0))) / (2.0f * cellSize);
            // Pendientes de la superficie acotadas: en la orilla el corte no debe parecer una cascada.
            const float sx = std::clamp(dx, -0.5f, 0.5f), sz = std::clamp(dz, -0.5f, 0.5f);
            const float length = std::sqrt(sx * sx + 1.0f + sz * sz);
            vertices.push_back({level, map.water(gx, gz),
                                pack(-sx / length) | (pack(1.0f / length) << 10) | (pack(-sz / length) << 20),
                                static_cast<std::uint32_t>(x) | (static_cast<std::uint32_t>(z) << 16)});
        }
    }
    return vertices;
}

// --- Simulación de agua -------------------------------------------------------------------------

void WaterSimulation::reset() noexcept {
    map_ = nullptr;
    left_.clear();
    right_.clear();
    down_.clear();
    up_.clear();
    awake_.clear();
    quiet_.clear();
}

void WaterSimulation::prepare(const Heightmap& map) {
    if (map_ == &map && verticesX_ == map.verticesX() && verticesZ_ == map.verticesZ()) return;
    map_ = &map;
    verticesX_ = map.verticesX();
    verticesZ_ = map.verticesZ();
    const auto count = static_cast<std::size_t>(verticesX_) * static_cast<std::size_t>(verticesZ_);
    left_.assign(count, 0.0f);
    right_.assign(count, 0.0f);
    down_.assign(count, 0.0f);
    up_.assign(count, 0.0f);
    awake_.assign(static_cast<std::size_t>(map.chunkCount()), 0);
    quiet_.assign(static_cast<std::size_t>(map.chunkCount()), 0);
}

void WaterSimulation::wake(const Heightmap& map, int chunk) {
    prepare(map);
    if (chunk < 0 || chunk >= map.chunkCount()) return;
    awake_[static_cast<std::size_t>(chunk)] = 1;
    quiet_[static_cast<std::size_t>(chunk)] = 0;
}

void WaterSimulation::wakeAll(const Heightmap& map) {
    prepare(map);
    for (int chunk = 0; chunk < map.chunkCount(); ++chunk) wake(map, chunk);
}

void WaterSimulation::wakeAround(const Heightmap& map, int x, int z, int radius) {
    prepare(map);
    constexpr int size = Heightmap::chunkCells;
    for (int cz = std::max(0, (z - radius - 1) / size); cz <= std::min(map.chunksZ() - 1, (z + radius + 1) / size); ++cz) {
        for (int cx = std::max(0, (x - radius - 1) / size); cx <= std::min(map.chunksX() - 1, (x + radius + 1) / size); ++cx) {
            wake(map, map.chunkIndex(cx, cz));
        }
    }
}

int WaterSimulation::awakeChunks() const noexcept {
    return static_cast<int>(std::count(awake_.begin(), awake_.end(), std::uint8_t{1}));
}

void WaterSimulation::step(Heightmap& map, float cellSize, float dt) {
    constexpr int size = Heightmap::chunkCells;
    constexpr float gravity = 9.81f;
    constexpr float damping = 0.995f;       // pierde algo de energía: el agua se calma en lugar de oscilar
    const float area = cellSize * cellSize;
    const int vx = verticesX_, vz = verticesZ_;
    const auto& heights = map.heights();
    auto& water = map.waterData();
    const auto at = [vx](int x, int z) { return static_cast<std::size_t>(z) * static_cast<std::size_t>(vx) + static_cast<std::size_t>(x); };
    // Vértices de cada chunk: [64·c, 64·c + 63], y el último chunk incluye la fila final.
    const auto range = [&](int c, int count, int vertices) {
        const int first = c * size;
        const int last = c == count - 1 ? vertices - 1 : first + size - 1;
        return std::array<int, 2>{first, last};
    };

    // 1. Caudales según la diferencia de altura de la superficie, limitados al agua disponible.
    for (int chunk = 0; chunk < map.chunkCount(); ++chunk) {
        if (!awake_[static_cast<std::size_t>(chunk)]) continue;
        const auto [cx, cz] = map.chunkCoords(chunk);
        const auto xs = range(cx, map.chunksX(), vx), zs = range(cz, map.chunksZ(), vz);
        for (int z = zs[0]; z <= zs[1]; ++z) {
            for (int x = xs[0]; x <= xs[1]; ++x) {
                const auto i = at(x, z);
                const float depth = water[i];
                const float level = heights[i] + depth;
                const auto flow = [&](float previous, int nx, int nz) {
                    if (nx < 0 || nz < 0 || nx >= vx || nz >= vz) return 0.0f;
                    const auto n = at(nx, nz);
                    return std::max(0.0f, previous * damping + dt * gravity * (level - heights[n] - water[n]) * cellSize);
                };
                float l = flow(left_[i], x - 1, z), r = flow(right_[i], x + 1, z);
                float d = flow(down_[i], x, z - 1), u = flow(up_[i], x, z + 1);
                const float total = (l + r + d + u) * dt;
                if (total > depth * area) {
                    const float k = total > 0.0f ? depth * area / total : 0.0f;
                    l *= k;
                    r *= k;
                    d *= k;
                    u *= k;
                }
                left_[i] = l;
                right_[i] = r;
                down_[i] = d;
                up_[i] = u;
            }
        }
    }
    // 2. El agua que sale de un chunk despierta al vecino que la recibe.
    std::vector<std::uint8_t> next = awake_;
    constexpr float wakeFlow = 1e-6f;
    for (int chunk = 0; chunk < map.chunkCount(); ++chunk) {
        if (!awake_[static_cast<std::size_t>(chunk)]) continue;
        const auto [cx, cz] = map.chunkCoords(chunk);
        const auto xs = range(cx, map.chunksX(), vx), zs = range(cz, map.chunksZ(), vz);
        const auto mark = [&](int ncx, int ncz) { next[static_cast<std::size_t>(map.chunkIndex(ncx, ncz))] = 1; };
        for (int z = zs[0]; z <= zs[1]; ++z) {
            if (cx > 0 && left_[at(xs[0], z)] > wakeFlow) mark(cx - 1, cz);
            if (cx < map.chunksX() - 1 && right_[at(xs[1], z)] > wakeFlow) mark(cx + 1, cz);
        }
        for (int x = xs[0]; x <= xs[1]; ++x) {
            if (cz > 0 && down_[at(x, zs[0])] > wakeFlow) mark(cx, cz - 1);
            if (cz < map.chunksZ() - 1 && up_[at(x, zs[1])] > wakeFlow) mark(cx, cz + 1);
        }
    }
    for (std::size_t chunk = 0; chunk < next.size(); ++chunk) {
        if (next[chunk] && !awake_[chunk]) quiet_[chunk] = 0;
    }
    awake_ = std::move(next);

    // 3. Profundidad nueva: lo que entra menos lo que sale. Los chunks quietos se duermen.
    constexpr float stillChange = 2e-5f;
    constexpr int stillSteps = 40;
    for (int chunk = 0; chunk < map.chunkCount(); ++chunk) {
        if (!awake_[static_cast<std::size_t>(chunk)]) continue;
        const auto [cx, cz] = map.chunkCoords(chunk);
        const auto xs = range(cx, map.chunksX(), vx), zs = range(cz, map.chunksZ(), vz);
        float largest = 0.0f;
        for (int z = zs[0]; z <= zs[1]; ++z) {
            for (int x = xs[0]; x <= xs[1]; ++x) {
                const auto i = at(x, z);
                float in = 0.0f;
                if (x > 0) in += right_[at(x - 1, z)];
                if (x < vx - 1) in += left_[at(x + 1, z)];
                if (z > 0) in += up_[at(x, z - 1)];
                if (z < vz - 1) in += down_[at(x, z + 1)];
                const float out = left_[i] + right_[i] + down_[i] + up_[i];
                const float change = dt * (in - out) / area;
                water[i] = std::max(0.0f, water[i] + change);
                largest = std::max(largest, std::abs(change));
            }
        }
        if (largest > 1e-7f) map.touchWaterChunk(chunk);
        auto& quiet = quiet_[static_cast<std::size_t>(chunk)];
        quiet = largest < stillChange ? quiet + 1 : 0;
        if (quiet >= stillSteps) {
            // Dormido: sin caudales pendientes (no puede perder agua hacia un vecino sin recalcularlos).
            awake_[static_cast<std::size_t>(chunk)] = 0;
            for (int z = zs[0]; z <= zs[1]; ++z) {
                for (int x = xs[0]; x <= xs[1]; ++x) {
                    const auto i = at(x, z);
                    left_[i] = right_[i] = down_[i] = up_[i] = 0.0f;
                }
            }
        }
    }
}

bool WaterSimulation::advance(Heightmap& map, float cellSize, float seconds) {
    prepare(map);
    if (awakeChunks() == 0) return false;
    // Paso estable: el agua no puede recorrer más de una celda por paso (con profundidades de pocos metros).
    const float stepTime = 0.04f * std::sqrt(std::max(cellSize, 0.05f));
    const int steps = std::clamp(static_cast<int>(std::ceil(seconds / stepTime)), 1, 8);
    for (int i = 0; i < steps && awakeChunks() > 0; ++i) step(map, cellSize, stepTime);
    return awakeChunks() > 0;
}

void WaterSimulation::settle(Heightmap& map, float cellSize, double budgetMs) {
    const auto start = std::chrono::steady_clock::now();
    while (advance(map, cellSize, 0.32f)) {
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() > budgetMs) break;
    }
}

}
