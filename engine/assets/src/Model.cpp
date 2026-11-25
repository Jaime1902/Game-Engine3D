#include "ge3d/Model.hpp"

#include "ge3d/SceneIO.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace ge3d {

namespace {

constexpr std::size_t maxVertices = 2'000'000;

// Lectura de archivos con rutas UTF-8 (cgltf usa fopen, que en Windows no las admite).
cgltf_result readFile(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size, void** data) {
    std::ifstream file(pathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!file) return cgltf_result_file_not_found;
    const auto length = static_cast<std::size_t>(file.tellg());
    void* buffer = std::malloc(length ? length : 1);
    if (!buffer) return cgltf_result_out_of_memory;
    file.seekg(0);
    file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(length));
    if (!file) {
        std::free(buffer);
        return cgltf_result_io_error;
    }
    *size = length;
    *data = buffer;
    return cgltf_result_success;
}

void releaseFile(const cgltf_memory_options*, const cgltf_file_options*, void* data) {
    std::free(data);
}

const char* describe(cgltf_result result) {
    switch (result) {
    case cgltf_result_data_too_short: return "archivo incompleto";
    case cgltf_result_unknown_format: return "no es glTF";
    case cgltf_result_invalid_json: return "JSON invalido";
    case cgltf_result_invalid_gltf: return "glTF invalido";
    case cgltf_result_file_not_found: return "no se encuentra un archivo que usa (por ejemplo, el .bin)";
    case cgltf_result_io_error: return "error de lectura";
    case cgltf_result_out_of_memory: return "sin memoria";
    default: return "error desconocido";
    }
}

std::uint32_t packColor(const float* c) {
    const auto channel = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return channel(c[0]) | (channel(c[1]) << 8) | (channel(c[2]) << 16) | (channel(c[3]) << 24);
}

// Matriz 3x3 normal (inversa transpuesta) de una matriz 4x4 en columnas.
void normalMatrix(const float* m, float* out) {
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const float inv = std::abs(det) > 1e-12f ? 1.0f / det : 0.0f;
    // Cofactores (inversa transpuesta = cofactores / det), en filas.
    out[0] = (e * i - f * h) * inv;
    out[1] = -(d * i - f * g) * inv;
    out[2] = (d * h - e * g) * inv;
    out[3] = -(b * i - c * h) * inv;
    out[4] = (a * i - c * g) * inv;
    out[5] = -(a * h - b * g) * inv;
    out[6] = (b * f - c * e) * inv;
    out[7] = -(a * f - c * d) * inv;
    out[8] = (a * e - b * d) * inv;
}

Image decodeTexture(const cgltf_image* image, const std::filesystem::path& folder, const cgltf_options& options) {
    if (image->buffer_view) {
        const auto* bytes = cgltf_buffer_view_data(image->buffer_view);
        return decodeImage(bytes, image->buffer_view->size);
    }
    if (!image->uri) throw std::runtime_error("textura sin datos");
    const std::string uri = image->uri;
    if (uri.rfind("data:", 0) == 0) {
        const auto comma = uri.find(";base64,");
        if (comma == std::string::npos) throw std::runtime_error("textura con URI de datos no soportada");
        const std::string base64 = uri.substr(comma + 8);
        void* data = nullptr;
        const cgltf_size size = base64.size() / 4 * 3 - (base64.ends_with("==") ? 2 : base64.ends_with("=") ? 1 : 0);
        if (cgltf_load_buffer_base64(&options, size, base64.c_str(), &data) != cgltf_result_success) {
            throw std::runtime_error("textura en base64 invalida");
        }
        try {
            auto decoded = decodeImage(static_cast<const std::uint8_t*>(data), size);
            std::free(data);
            return decoded;
        } catch (...) {
            std::free(data);
            throw;
        }
    }
    std::string relative = uri;
    relative.resize(cgltf_decode_uri(relative.data()));
    if (relative.find("..") != std::string::npos) throw std::runtime_error("textura fuera de la carpeta del modelo: " + relative);
    std::ifstream file(folder / pathFromUtf8(relative), std::ios::binary);
    if (!file) throw std::runtime_error("no se encuentra la textura " + relative);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return decodeImage(bytes.data(), bytes.size());
}

}

bool isValidModelPath(const std::string& path) {
    return !path.empty() && path.size() < 260 && path.find("..") == std::string::npos && path.front() != '/' &&
           path.front() != '\\' && path.find(':') == std::string::npos && (path.ends_with(".gltf") || path.ends_with(".glb"));
}

ModelData loadModel(const std::filesystem::path& path) {
    const auto name = pathToUtf8(path.filename());
    cgltf_options options{};
    options.file.read = readFile;
    options.file.release = releaseFile;
    cgltf_data* data = nullptr;
    const auto utf8 = pathToUtf8(path);
    cgltf_result result = cgltf_parse_file(&options, utf8.c_str(), &data);
    if (result == cgltf_result_success) result = cgltf_load_buffers(&options, data, utf8.c_str());
    if (result != cgltf_result_success) {
        if (data) cgltf_free(data);
        throw std::runtime_error(name + ": " + describe(result));
    }
    struct Guard {
        cgltf_data* data;
        ~Guard() { cgltf_free(data); }
    } guard{data};

    ModelData model;
    // Materiales y texturas.
    std::map<const cgltf_image*, int> imageIndex;
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const auto& source = data->materials[i];
        ModelMaterial material;
        material.name = source.name ? source.name : "material " + std::to_string(i);
        if (source.has_pbr_metallic_roughness) {
            const auto& pbr = source.pbr_metallic_roughness;
            std::copy(std::begin(pbr.base_color_factor), std::end(pbr.base_color_factor), material.baseColor.begin());
            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                const auto* image = pbr.base_color_texture.texture->image;
                if (const auto found = imageIndex.find(image); found != imageIndex.end()) {
                    material.texture = found->second;
                } else {
                    try {
                        model.textures.push_back(decodeTexture(image, path.parent_path(), options));
                        material.texture = static_cast<int>(model.textures.size()) - 1;
                        imageIndex[image] = material.texture;
                    } catch (const std::exception& error) {
                        throw std::runtime_error(name + ": " + error.what());
                    }
                }
            }
        }
        model.materials.push_back(std::move(material));
    }
    const auto materialIndex = [&](const cgltf_material* material) {
        return material ? static_cast<int>(material - data->materials) : -1;
    };

    // Mallas de todos los nodos (de la escena por defecto, o de todas), con su transformación combinada.
    const auto addNode = [&](const cgltf_node* node) {
        if (!node->mesh) return;
        float world[16];
        cgltf_node_transform_world(node, world);
        float normal[9];
        normalMatrix(world, normal);
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            const auto& primitive = node->mesh->primitives[p];
            if (primitive.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *positions = nullptr, *normals = nullptr, *uvs = nullptr, *colors = nullptr;
            for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
                const auto& attribute = primitive.attributes[a];
                if (attribute.type == cgltf_attribute_type_position) positions = attribute.data;
                else if (attribute.type == cgltf_attribute_type_normal) normals = attribute.data;
                else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) uvs = attribute.data;
                else if (attribute.type == cgltf_attribute_type_color && attribute.index == 0) colors = attribute.data;
            }
            if (!positions) continue;
            const auto base = static_cast<std::uint32_t>(model.vertices.size());
            if (model.vertices.size() + positions->count > maxVertices) throw std::runtime_error(name + ": demasiados vertices (maximo 2 millones)");
            for (cgltf_size v = 0; v < positions->count; ++v) {
                float p3[3] = {0, 0, 0}, n3[3] = {0, 1, 0}, t2[2] = {0, 0}, c4[4] = {1, 1, 1, 1};
                cgltf_accessor_read_float(positions, v, p3, 3);
                if (normals) cgltf_accessor_read_float(normals, v, n3, 3);
                if (uvs) cgltf_accessor_read_float(uvs, v, t2, 2);
                if (colors) cgltf_accessor_read_float(colors, v, c4, colors->type == cgltf_type_vec3 ? 3 : 4);
                ModelVertex vertex{};
                // Al mundo del modelo y giro de 180 grados en Y (+Z de glTF -> -Z del motor).
                const float x = world[0] * p3[0] + world[4] * p3[1] + world[8] * p3[2] + world[12];
                const float y = world[1] * p3[0] + world[5] * p3[1] + world[9] * p3[2] + world[13];
                const float z = world[2] * p3[0] + world[6] * p3[1] + world[10] * p3[2] + world[14];
                vertex.position[0] = -x;
                vertex.position[1] = y;
                vertex.position[2] = -z;
                float nx = normal[0] * n3[0] + normal[1] * n3[1] + normal[2] * n3[2];
                float ny = normal[3] * n3[0] + normal[4] * n3[1] + normal[5] * n3[2];
                float nz = normal[6] * n3[0] + normal[7] * n3[1] + normal[8] * n3[2];
                const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (length > 1e-8f) {
                    nx /= length;
                    ny /= length;
                    nz /= length;
                }
                vertex.normal[0] = -nx;
                vertex.normal[1] = ny;
                vertex.normal[2] = -nz;
                vertex.uv[0] = t2[0];
                vertex.uv[1] = t2[1];
                if (colors && colors->type == cgltf_type_vec3) c4[3] = 1.0f;
                vertex.color = packColor(c4);
                model.vertices.push_back(vertex);
            }
            ModelPart part;
            part.firstIndex = static_cast<std::uint32_t>(model.indices.size());
            part.material = materialIndex(primitive.material);
            if (primitive.indices) {
                for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
                    model.indices.push_back(base + static_cast<std::uint32_t>(cgltf_accessor_read_index(primitive.indices, i)));
                }
            } else {
                for (cgltf_size i = 0; i < positions->count; ++i) model.indices.push_back(base + static_cast<std::uint32_t>(i));
            }
            part.indexCount = static_cast<std::uint32_t>(model.indices.size()) - part.firstIndex;
            // Sin normales en el archivo: caras planas (estilo low poly).
            if (!normals) {
                for (std::uint32_t i = part.firstIndex; i + 2 < part.firstIndex + part.indexCount; i += 3) {
                    auto& a = model.vertices[model.indices[i]];
                    auto& b = model.vertices[model.indices[i + 1]];
                    auto& c = model.vertices[model.indices[i + 2]];
                    const float ux = b.position[0] - a.position[0], uy = b.position[1] - a.position[1], uz = b.position[2] - a.position[2];
                    const float vx = c.position[0] - a.position[0], vy = c.position[1] - a.position[1], vz = c.position[2] - a.position[2];
                    float n[3] = {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
                    const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    if (l > 1e-12f) {
                        for (auto& value : n) value /= l;
                    }
                    for (auto* vertex : {&a, &b, &c}) std::copy(n, n + 3, vertex->normal);
                }
            }
            // El giro de 180 grados conserva el sentido de las caras (es una rotación): no hay que invertirlas.
            if (part.indexCount > 0) model.parts.push_back(part);
        }
    };
    const auto visit = [&](auto& self, const cgltf_node* node) -> void {
        addNode(node);
        for (cgltf_size c = 0; c < node->children_count; ++c) self(self, node->children[c]);
    };
    if (data->scene) {
        for (cgltf_size n = 0; n < data->scene->nodes_count; ++n) visit(visit, data->scene->nodes[n]);
    } else {
        for (cgltf_size n = 0; n < data->nodes_count; ++n) {
            if (!data->nodes[n].parent) visit(visit, &data->nodes[n]);
        }
    }
    if (model.vertices.empty()) throw std::runtime_error(name + ": el archivo no tiene mallas de triangulos");
    model.boundsMin = {model.vertices[0].position[0], model.vertices[0].position[1], model.vertices[0].position[2]};
    model.boundsMax = model.boundsMin;
    for (const auto& vertex : model.vertices) {
        model.boundsMin = {std::min(model.boundsMin.x, vertex.position[0]), std::min(model.boundsMin.y, vertex.position[1]),
                           std::min(model.boundsMin.z, vertex.position[2])};
        model.boundsMax = {std::max(model.boundsMax.x, vertex.position[0]), std::max(model.boundsMax.y, vertex.position[1]),
                           std::max(model.boundsMax.z, vertex.position[2])};
    }
    return model;
}

// --- Biblioteca -------------------------------------------------------------------------------------

ModelLibrary::ModelLibrary(std::filesystem::path contentRoot) : root_(std::move(contentRoot)) {}

void ModelLibrary::setRoot(std::filesystem::path contentRoot) {
    root_ = std::move(contentRoot);
    entries_.clear();
    ++revision_;
}

std::shared_ptr<const ModelData> ModelLibrary::get(const std::string& path) {
    if (!isValidModelPath(path)) return nullptr;
    const auto file = root_ / pathFromUtf8(path);
    std::error_code error;
    const auto time = std::filesystem::last_write_time(file, error);
    auto& entry = entries_[path];
    if (error) {
        entry.data.reset();
        entry.error = "No se encuentra " + path;
        return nullptr;
    }
    if (entry.time == time && (entry.data || !entry.error.empty())) return entry.data;
    entry.time = time;
    try {
        entry.data = std::make_shared<ModelData>(loadModel(file));
        entry.error.clear();
    } catch (const std::exception& failure) {
        entry.data.reset();
        entry.error = failure.what();
    }
    ++revision_;
    return entry.data;
}

std::shared_ptr<const ModelData> ModelLibrary::cached(const std::string& path) {
    const auto it = entries_.find(path);
    if (it != entries_.end() && (it->second.data || !it->second.error.empty())) return it->second.data;
    return get(path);
}

void ModelLibrary::refresh() {
    std::vector<std::string> paths;
    for (const auto& [path, entry] : entries_) paths.push_back(path);
    for (const auto& path : paths) (void)get(path);
}

const std::string& ModelLibrary::lastError(const std::string& path) const {
    static const std::string none;
    const auto it = entries_.find(path);
    return it == entries_.end() ? none : it->second.error;
}

}
