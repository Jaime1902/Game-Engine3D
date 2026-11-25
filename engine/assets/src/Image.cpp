#include "ge3d/Materials.hpp"
#include "ge3d/SceneIO.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

// Implementación de stb (dominio público / MIT) solo en esta unidad de compilación.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image.h>
#include <stb_image_write.h>

namespace ge3d {

Image loadPng(const std::filesystem::path& path) {
    // Se lee el archivo a memoria con std::ifstream para admitir rutas UTF-8 en Windows.
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("No se puede abrir " + pathToUtf8(path));
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) throw std::runtime_error("PNG invalido " + pathToUtf8(path) + ": " + stbi_failure_reason());
    Image image{width, height, {pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4}};
    stbi_image_free(pixels);
    return image;
}

Image decodeImage(const std::uint8_t* data, std::size_t size) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) throw std::runtime_error(std::string("imagen invalida: ") + stbi_failure_reason());
    Image image{width, height, {pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4}};
    stbi_image_free(pixels);
    return image;
}

void savePng(const std::filesystem::path& path, const Image& image) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4) {
        throw std::invalid_argument("savePng: imagen invalida");
    }
    std::vector<unsigned char> encoded;
    const auto write = [](void* context, void* data, int size) {
        auto* out = static_cast<std::vector<unsigned char>*>(context);
        const auto* begin = static_cast<unsigned char*>(data);
        out->insert(out->end(), begin, begin + size);
    };
    if (!stbi_write_png_to_func(write, &encoded, image.width, image.height, 4, image.rgba.data(), image.width * 4)) {
        throw std::runtime_error("No se pudo codificar " + pathToUtf8(path));
    }
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    if (!file) throw std::runtime_error("No se pudo escribir " + pathToUtf8(path));
}

}
