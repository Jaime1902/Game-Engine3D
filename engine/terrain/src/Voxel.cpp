#include "ge3d/Voxel.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace ge3d {

namespace {
// Contador global: dos rejillas nunca comparten un número de revisión salvo que una sea copia de
// la otra, así el renderer no confunde la malla de una rejilla con la de otra.
std::atomic<std::uint64_t> revisionCounter{0};
std::uint64_t nextRevision() noexcept {
    return ++revisionCounter;
}
}

// --- VoxelGrid ----------------------------------------------------------------------------------

VoxelGrid::VoxelGrid(int chunksX, int chunksY, int chunksZ)
    : chunksX_(chunksX), chunksY_(chunksY), chunksZ_(chunksZ) {
    if (chunksX < 1 || chunksZ < 1 || chunksY < 1 || chunksX > maxChunksXZ || chunksZ > maxChunksXZ ||
        chunksY > maxChunksY) {
        throw std::invalid_argument("VoxelGrid: tamano de terreno fuera de rango");
    }
    data_.assign(static_cast<std::size_t>(chunkCount()) * chunkVolume, 0);
    revisions_.assign(static_cast<std::size_t>(chunkCount()), 0);
}

bool VoxelGrid::contains(int x, int y, int z) const noexcept {
    return x >= 0 && y >= 0 && z >= 0 && x < sizeX() && y < sizeY() && z < sizeZ();
}

std::size_t VoxelGrid::offset(int x, int y, int z) const noexcept {
    const int chunk = chunkIndex(x / chunkSize, y / chunkSize, z / chunkSize);
    const int local = ((z % chunkSize) * chunkSize + (y % chunkSize)) * chunkSize + (x % chunkSize);
    return static_cast<std::size_t>(chunk) * chunkVolume + static_cast<std::size_t>(local);
}

VoxelGrid::Voxel VoxelGrid::get(int x, int y, int z) const noexcept {
    return contains(x, y, z) ? data_[offset(x, y, z)] : Voxel{0};
}

void VoxelGrid::touch(int cx, int cy, int cz) noexcept {
    if (cx < 0 || cy < 0 || cz < 0 || cx >= chunksX_ || cy >= chunksY_ || cz >= chunksZ_) return;
    revisions_[static_cast<std::size_t>(chunkIndex(cx, cy, cz))] = nextRevision();
}

void VoxelGrid::touchAround(int x, int y, int z) noexcept {
    const int cx = x / chunkSize, cy = y / chunkSize, cz = z / chunkSize;
    const int lx = x % chunkSize, ly = y % chunkSize, lz = z % chunkSize;
    touch(cx, cy, cz);
    // Un cambio en el borde altera qué caras del chunk vecino quedan a la vista.
    if (lx == 0) touch(cx - 1, cy, cz);
    if (lx == chunkSize - 1) touch(cx + 1, cy, cz);
    if (ly == 0) touch(cx, cy - 1, cz);
    if (ly == chunkSize - 1) touch(cx, cy + 1, cz);
    if (lz == 0) touch(cx, cy, cz - 1);
    if (lz == chunkSize - 1) touch(cx, cy, cz + 1);
}

bool VoxelGrid::set(int x, int y, int z, Voxel value) {
    if (!contains(x, y, z)) return false;
    if (value > materials_.size()) value = 0;
    auto& voxel = data_[offset(x, y, z)];
    if (voxel == value) return false;
    voxel = value;
    touchAround(x, y, z);
    return true;
}

VoxelGrid::Voxel VoxelGrid::findMaterial(const std::string& id) const noexcept {
    const auto it = std::find(materials_.begin(), materials_.end(), id);
    return it == materials_.end() ? Voxel{0} : static_cast<Voxel>(it - materials_.begin() + 1);
}

VoxelGrid::Voxel VoxelGrid::materialIndex(const std::string& id) {
    if (id.empty()) return 0;
    if (const auto found = findMaterial(id)) return found;
    if (materials_.size() >= maxMaterials) return 0;
    materials_.push_back(id);
    return static_cast<Voxel>(materials_.size());
}

void VoxelGrid::setMaterials(std::vector<std::string> materials) {
    if (materials.size() > maxMaterials) throw std::invalid_argument("VoxelGrid: demasiados materiales");
    materials_ = std::move(materials);
    for (int chunk = 0; chunk < chunkCount(); ++chunk) revisions_[static_cast<std::size_t>(chunk)] = nextRevision();
}

std::array<int, 3> VoxelGrid::chunkCoords(int index) const noexcept {
    const int cx = index % chunksX_;
    const int cy = (index / chunksX_) % chunksY_;
    const int cz = index / (chunksX_ * chunksY_);
    return {cx, cy, cz};
}

std::vector<VoxelGrid::Voxel> VoxelGrid::chunkData(int chunk) const {
    const auto begin = data_.begin() + static_cast<std::ptrdiff_t>(chunk) * chunkVolume;
    return {begin, begin + chunkVolume};
}

void VoxelGrid::setChunkData(int chunk, const std::vector<Voxel>& data) {
    if (chunk < 0 || chunk >= chunkCount() || data.size() != static_cast<std::size_t>(chunkVolume)) {
        throw std::invalid_argument("VoxelGrid::setChunkData: datos de chunk invalidos");
    }
    std::copy(data.begin(), data.end(), data_.begin() + static_cast<std::ptrdiff_t>(chunk) * chunkVolume);
    const auto [cx, cy, cz] = chunkCoords(chunk);
    touch(cx, cy, cz);
    touch(cx - 1, cy, cz);
    touch(cx + 1, cy, cz);
    touch(cx, cy - 1, cz);
    touch(cx, cy + 1, cz);
    touch(cx, cy, cz - 1);
    touch(cx, cy, cz + 1);
}

std::size_t VoxelGrid::solidCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(data_.begin(), data_.end(), [](Voxel v) { return v != 0; }));
}

// --- Ruido y generación -------------------------------------------------------------------------

namespace {

std::uint32_t hash(int x, int z, std::uint32_t seed) noexcept {
    std::uint32_t h = seed * 0x9E3779B9u;
    h ^= static_cast<std::uint32_t>(x) * 0x85EBCA6Bu;
    h ^= static_cast<std::uint32_t>(z) * 0xC2B2AE35u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

float lattice(int x, int z, std::uint32_t seed) noexcept {
    return static_cast<float>(hash(x, z, seed) >> 8) / static_cast<float>(1u << 24);
}

// Ruido de valor con interpolación suave (0..1).
float valueNoise(float x, float z, std::uint32_t seed) noexcept {
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const int ix = static_cast<int>(fx);
    const int iz = static_cast<int>(fz);
    const float tx = x - fx;
    const float tz = z - fz;
    const float u = tx * tx * (3.0f - 2.0f * tx);
    const float v = tz * tz * (3.0f - 2.0f * tz);
    const float a = lattice(ix, iz, seed);
    const float b = lattice(ix + 1, iz, seed);
    const float c = lattice(ix, iz + 1, seed);
    const float d = lattice(ix + 1, iz + 1, seed);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}

}

float terrainHeight(const TerrainGenerator& generator, float x, float z) {
    const float scale = std::max(1.0f, generator.scale);
    const int octaves = std::clamp(generator.octaves, 1, 8);
    float sum = 0.0f;
    float weight = 1.0f;
    float total = 0.0f;
    float frequency = 1.0f / scale;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += weight * valueNoise(x * frequency, z * frequency, generator.seed + static_cast<std::uint32_t>(octave) * 101u);
        total += weight;
        weight *= 0.5f;
        frequency *= 2.0f;
    }
    return generator.baseHeight + generator.amplitude * ((sum / total) * 2.0f - 1.0f);
}

std::shared_ptr<VoxelGrid> generateTerrain(const TerrainSettings& settings) {
    auto grid = std::make_shared<VoxelGrid>(settings.chunksX, settings.chunksY, settings.chunksZ);
    const auto& g = settings.generator;
    const auto top = grid->materialIndex(g.top);
    const auto under = grid->materialIndex(g.under);
    const auto deep = grid->materialIndex(g.deep);
    const auto shore = grid->materialIndex(g.shore);
    const auto water = g.waterLevel > 0 ? grid->materialIndex(g.water) : VoxelGrid::Voxel{0};
    const int maxHeight = grid->sizeY() - 1;
    const int waterLevel = std::clamp(g.waterLevel, 0, maxHeight);
    for (int z = 0; z < grid->sizeZ(); ++z) {
        for (int x = 0; x < grid->sizeX(); ++x) {
            const int height = std::clamp(static_cast<int>(std::lround(terrainHeight(g, static_cast<float>(x),
                                                                                    static_cast<float>(z)))), 1, maxHeight);
            const bool beach = waterLevel > 0 && height <= waterLevel + 1;
            for (int y = 0; y < height; ++y) {
                const int depth = height - 1 - y;
                VoxelGrid::Voxel material = deep;
                if (depth == 0) material = beach && shore ? shore : top;
                else if (depth <= 3) material = beach && shore ? shore : under;
                grid->set(x, y, z, material ? material : deep);
            }
            for (int y = height; y < waterLevel; ++y) grid->set(x, y, z, water);
        }
    }
    return grid;
}

std::shared_ptr<VoxelGrid> makeFlatTerrain(const TerrainSettings& settings, int height, const std::string& material) {
    auto grid = std::make_shared<VoxelGrid>(settings.chunksX, settings.chunksY, settings.chunksZ);
    const auto index = grid->materialIndex(material);
    height = std::clamp(height, 0, grid->sizeY());
    for (int z = 0; z < grid->sizeZ(); ++z) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < grid->sizeX(); ++x) grid->set(x, y, z, index);
        }
    }
    return grid;
}

// --- Greedy meshing -----------------------------------------------------------------------------

VoxelMesh meshChunk(const VoxelGrid& grid, int chunk, const std::array<std::uint16_t, 256>* remap) {
    constexpr int size = VoxelGrid::chunkSize;
    constexpr int padded = size + 2;
    const auto [cx, cy, cz] = grid.chunkCoords(chunk);
    const int ox = cx * size, oy = cy * size, oz = cz * size;

    VoxelMesh mesh;
    // Atajo 1: un chunk todo aire no tiene caras (en un terreno típico, la mitad superior).
    const auto data = grid.chunkData(chunk);
    const bool empty = std::all_of(data.begin(), data.end(), [](VoxelGrid::Voxel v) { return v == 0; });
    if (empty) return mesh;

    // Copia del chunk con un vóxel de borde: las consultas a vecinos son lecturas directas.
    // El interior se copia por filas desde los datos del chunk; solo el borde consulta la rejilla.
    std::vector<VoxelGrid::Voxel> voxels(static_cast<std::size_t>(padded) * padded * padded);
    const auto at = [&](int x, int y, int z) -> VoxelGrid::Voxel& {
        return voxels[static_cast<std::size_t>(((z + 1) * padded + (y + 1)) * padded + (x + 1))];
    };
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            std::copy_n(data.begin() + (z * size + y) * size, size, &at(0, y, z));
        }
    }
    bool sealed = true;   // ¿todo el borde exterior es sólido (o suelo del mundo)?
    for (int z = -1; z <= size; ++z) {
        for (int y = -1; y <= size; ++y) {
            for (int x = -1; x <= size; ++x) {
                const bool border = x < 0 || y < 0 || z < 0 || x == size || y == size || z == size;
                if (!border) continue;
                const auto value = grid.get(ox + x, oy + y, oz + z);
                at(x, y, z) = value;
                if (value == 0 && !(oy + y < 0)) sealed = false;
            }
        }
    }
    // Atajo 2: chunk macizo rodeado de sólido: ninguna cara se ve desde fuera.
    if (sealed && std::none_of(data.begin(), data.end(), [](VoxelGrid::Voxel v) { return v == 0; })) return mesh;

    static constexpr int directions[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    std::array<std::uint16_t, size * size> mask{};
    for (int normal = 0; normal < 6; ++normal) {
        const int d = normal / 2;
        const int sign = normal % 2 ? 1 : -1;
        const int u = (d + 1) % 3;
        const int v = (d + 2) % 3;
        for (int slice = 0; slice < size; ++slice) {
            // Máscara de caras visibles en esta capa: material del vóxel, o 0 si la cara está tapada.
            for (int j = 0; j < size; ++j) {
                for (int i = 0; i < size; ++i) {
                    int p[3];
                    p[d] = slice;
                    p[u] = i;
                    p[v] = j;
                    const auto voxel = at(p[0], p[1], p[2]);
                    std::uint16_t face = 0;
                    if (voxel) {
                        const auto neighbor = at(p[0] + directions[normal][0], p[1] + directions[normal][1],
                                                 p[2] + directions[normal][2]);
                        const bool worldBottom = normal == 2 && oy + p[1] == 0;  // nadie mira bajo el terreno
                        if (!neighbor && !worldBottom) face = voxel;
                    }
                    mask[static_cast<std::size_t>(j * size + i)] = face;
                }
            }
            // Fusión en rectángulos del mismo material.
            for (int j = 0; j < size; ++j) {
                for (int i = 0; i < size;) {
                    const auto material = mask[static_cast<std::size_t>(j * size + i)];
                    if (!material) {
                        ++i;
                        continue;
                    }
                    int width = 1;
                    while (i + width < size && mask[static_cast<std::size_t>(j * size + i + width)] == material) ++width;
                    int height = 1;
                    for (bool grow = true; grow && j + height < size;) {
                        for (int k = 0; k < width; ++k) {
                            if (mask[static_cast<std::size_t>((j + height) * size + i + k)] != material) {
                                grow = false;
                                break;
                            }
                        }
                        if (grow) ++height;
                    }
                    for (int h = 0; h < height; ++h) {
                        for (int k = 0; k < width; ++k) mask[static_cast<std::size_t>((j + h) * size + i + k)] = 0;
                    }

                    const int plane = slice + (sign > 0 ? 1 : 0);
                    const int corners[4][2] = {{i, j}, {i + width, j}, {i + width, j + height}, {i, j + height}};
                    const std::uint32_t gpuMaterial = remap ? (*remap)[material] : material;
                    VoxelVertex quad[4];
                    for (int c = 0; c < 4; ++c) {
                        int position[3];
                        position[d] = plane;
                        position[u] = corners[c][0];
                        position[v] = corners[c][1];
                        quad[c] = {static_cast<std::uint32_t>(position[0]) | (static_cast<std::uint32_t>(position[1]) << 6) |
                                       (static_cast<std::uint32_t>(position[2]) << 12) |
                                       (static_cast<std::uint32_t>(normal) << 18),
                                   gpuMaterial};
                    }
                    // Orden antihorario visto desde fuera de la cara.
                    if (sign > 0) mesh.vertices.insert(mesh.vertices.end(), {quad[0], quad[1], quad[2], quad[3]});
                    else mesh.vertices.insert(mesh.vertices.end(), {quad[0], quad[3], quad[2], quad[1]});
                    i += width;
                }
            }
        }
    }
    return mesh;
}

}
