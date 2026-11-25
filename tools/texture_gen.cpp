// Genera el juego de texturas pixel art inicial (16 x 16) y el catálogo de materiales.
// Uso: ge3d_texture_gen <carpeta Content> [--force]
// No sobrescribe archivos existentes salvo con --force: las texturas son del proyecto y se pueden
// retocar o sustituir con cualquier editor de imágenes.
#include "ge3d/Materials.hpp"
#include "ge3d/SceneIO.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int size = 16;

struct Color {
    std::uint8_t r, g, b;
};

constexpr Color hex(std::uint32_t value) {
    return {static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)};
}

// Generador pseudoaleatorio determinista: las texturas salen iguales en cualquier equipo.
class Random {
public:
    explicit Random(std::uint32_t seed) : state_(seed * 2654435761u + 1u) {}
    std::uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    float unit() { return static_cast<float>(next() >> 8) / static_cast<float>(1u << 24); }
    int range(int count) { return static_cast<int>(next() % static_cast<std::uint32_t>(count)); }

private:
    std::uint32_t state_;
};

class Canvas {
public:
    explicit Canvas(Color fill) : pixels_(size * size, fill) {}
    void set(int x, int y, Color color) { pixels_[static_cast<std::size_t>(((y % size + size) % size) * size + ((x % size + size) % size))] = color; }
    [[nodiscard]] Color get(int x, int y) const { return pixels_[static_cast<std::size_t>(((y % size + size) % size) * size + ((x % size + size) % size))]; }
    // Moteado: cada píxel toma una de las variantes con su probabilidad.
    void speckle(Random& random, Color color, float chance) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                if (random.unit() < chance) set(x, y, color);
            }
        }
    }
    [[nodiscard]] ge3d::Image image() const {
        ge3d::Image image{size, size, {}};
        for (const auto& pixel : pixels_) image.rgba.insert(image.rgba.end(), {pixel.r, pixel.g, pixel.b, 255});
        return image;
    }

private:
    std::vector<Color> pixels_;
};

Canvas grass(std::uint32_t seed) {
    Random random(seed);
    Canvas canvas(hex(0x6f9a45));
    canvas.speckle(random, hex(0x5c8a3a), 0.30f);
    canvas.speckle(random, hex(0x4f6b3a), 0.12f);
    canvas.speckle(random, hex(0x8fb456), 0.10f);
    // Matas: un píxel claro sobre uno oscuro.
    for (int i = 0; i < 10; ++i) {
        const int x = random.range(size), y = random.range(size);
        canvas.set(x, y, hex(0xa9c75a));
        canvas.set(x, y + 1, hex(0x4f6b3a));
    }
    return canvas;
}

Canvas dirt(std::uint32_t seed) {
    Random random(seed);
    Canvas canvas(hex(0x7a5236));
    canvas.speckle(random, hex(0x6a4630), 0.30f);
    canvas.speckle(random, hex(0x8c6040), 0.15f);
    canvas.speckle(random, hex(0x5a3a2a), 0.08f);
    for (int i = 0; i < 4; ++i) canvas.set(random.range(size), random.range(size), hex(0x8f8a84));  // piedrecitas
    return canvas;
}

Canvas grassSide() {
    Random random(41);
    Canvas canvas = dirt(17);
    // Borde de césped irregular arriba.
    for (int x = 0; x < size; ++x) {
        const int depth = 2 + random.range(3);
        for (int y = 0; y < depth; ++y) {
            const float pick = random.unit();
            canvas.set(x, y, pick < 0.2f ? hex(0x8fb456) : pick < 0.45f ? hex(0x5c8a3a) : hex(0x6f9a45));
        }
        canvas.set(x, depth, hex(0x4f6b3a));
    }
    return canvas;
}

Canvas stone() {
    Random random(7);
    Canvas canvas(hex(0x6b6e7a));
    canvas.speckle(random, hex(0x5e616c), 0.30f);
    canvas.speckle(random, hex(0x7c7f8a), 0.18f);
    // Grietas cortas.
    for (int crack = 0; crack < 3; ++crack) {
        int x = random.range(size), y = random.range(size);
        for (int step = 0; step < 4 + random.range(4); ++step) {
            canvas.set(x, y, hex(0x45474f));
            x += random.range(3) - 1;
            y += 1;
        }
    }
    return canvas;
}

Canvas cobble() {
    Random random(11);
    const Color mortar = hex(0x3b3f4f);
    Canvas canvas(mortar);
    const Color shades[] = {hex(0x8f929c), hex(0x7f828d), hex(0x9ea1aa)};
    // Piedras de 6x4 en filas desplazadas, con luz arriba-izquierda y sombra abajo-derecha.
    for (int row = 0; row < 4; ++row) {
        const int offset = row % 2 ? 3 : 0;
        for (int column = -1; column < 3; ++column) {
            const int x0 = column * 6 + offset, y0 = row * 4;
            const Color base = shades[random.range(3)];
            for (int y = 0; y < 3; ++y) {
                for (int x = 0; x < 5; ++x) {
                    const bool corner = (x == 0 || x == 4) && (y == 0 || y == 2);
                    if (corner) continue;
                    Color color = base;
                    if (y == 0 || x == 0) color = hex(0xb4b6bd);
                    if (y == 2 || x == 4) color = hex(0x60636e);
                    canvas.set(x0 + x, y0 + y, color);
                }
            }
        }
    }
    return canvas;
}

Canvas sand() {
    Random random(23);
    Canvas canvas(hex(0xd9b77a));
    canvas.speckle(random, hex(0xc99a5b), 0.18f);
    canvas.speckle(random, hex(0xe6d18a), 0.18f);
    canvas.speckle(random, hex(0xb88a52), 0.04f);
    return canvas;
}

Canvas planks() {
    Random random(5);
    Canvas canvas(hex(0x8a5a3a));
    const Color tones[] = {hex(0x8a5a3a), hex(0x96643f), hex(0x7e5234)};
    for (int plank = 0; plank < 4; ++plank) {
        const Color tone = tones[random.range(3)];
        const int seam = random.range(size);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < size; ++x) {
                Color color = tone;
                if (y == 3) color = hex(0x5a3a2a);                       // junta entre tablones
                else if (x == seam) color = hex(0x5a3a2a);               // testa del tablón
                else if (random.unit() < 0.12f) color = hex(0x744a2f);   // veta
                else if (y == 0 && random.unit() < 0.3f) color = hex(0xa6744a);
                canvas.set(x, plank * 4 + y, color);
            }
        }
        canvas.set(seam + 2, plank * 4 + 1, hex(0x3b2a20));  // clavo
    }
    return canvas;
}

Canvas brick() {
    Random random(13);
    const Color mortar = hex(0xc9bfae);
    Canvas canvas(mortar);
    const Color tones[] = {hex(0xa8463c), hex(0xb4513f), hex(0x983c34)};
    for (int row = 0; row < 4; ++row) {
        const int offset = row % 2 ? 4 : 0;
        for (int column = -1; column < 2; ++column) {
            const Color base = tones[random.range(3)];
            for (int y = 0; y < 3; ++y) {
                for (int x = 0; x < 7; ++x) {
                    Color color = base;
                    if (y == 0 && random.unit() < 0.5f) color = hex(0xc06048);
                    if (y == 2) color = hex(0x84342e);
                    canvas.set(column * 8 + offset + x, row * 4 + y, color);
                }
            }
        }
    }
    return canvas;
}

Canvas roof() {
    Random random(19);
    Canvas canvas(hex(0xc8553d));
    // Tejas en filas de 4 px: borde inferior redondeado y oscuro.
    for (int row = 0; row < 4; ++row) {
        const int offset = row % 2 ? 2 : 0;
        for (int x = 0; x < size; ++x) {
            const int local = (x + offset) % 4;
            const int y = row * 4;
            canvas.set(x, y, local == 0 ? hex(0x8a3326) : hex(0xd8684a));
            canvas.set(x, y + 1, local == 0 ? hex(0x8a3326) : hex(0xc8553d));
            canvas.set(x, y + 2, local == 0 ? hex(0x8a3326) : (random.unit() < 0.2f ? hex(0xb84a35) : hex(0xc8553d)));
            canvas.set(x, y + 3, local == 1 || local == 2 ? hex(0x9a3a2a) : hex(0x7a2c22));
        }
    }
    return canvas;
}

Canvas plaster() {
    Random random(29);
    Canvas canvas(hex(0xe8e2d4));
    canvas.speckle(random, hex(0xdcd4c2), 0.25f);
    canvas.speckle(random, hex(0xf2eee4), 0.10f);
    int x = random.range(size), y = 2;
    for (int step = 0; step < 5; ++step) canvas.set(x += random.range(3) - 1, y++, hex(0xbfb6a2));
    return canvas;
}

Canvas water() {
    Random random(31);
    Canvas canvas(hex(0x3f6fb0));
    canvas.speckle(random, hex(0x36629f), 0.35f);
    // Brillos horizontales cortos.
    for (int i = 0; i < 7; ++i) {
        const int x = random.range(size), y = random.range(size), length = 2 + random.range(3);
        for (int k = 0; k < length; ++k) canvas.set(x + k, y, k == 0 || k == length - 1 ? hex(0x5f8fcf) : hex(0x7fb2e0));
    }
    return canvas;
}

Canvas snow() {
    Random random(37);
    Canvas canvas(hex(0xeef2f6));
    canvas.speckle(random, hex(0xdbe4ee), 0.25f);
    canvas.speckle(random, hex(0xc6d4e4), 0.06f);
    canvas.speckle(random, hex(0xffffff), 0.08f);
    return canvas;
}

Canvas leaves() {
    Random random(43);
    Canvas canvas(hex(0x4f6b3a));
    canvas.speckle(random, hex(0x3f5a2e), 0.35f);
    canvas.speckle(random, hex(0x6f9a45), 0.20f);
    canvas.speckle(random, hex(0x2f4524), 0.10f);
    for (int i = 0; i < 6; ++i) canvas.set(random.range(size), random.range(size), hex(0x8fb456));
    return canvas;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Uso: ge3d_texture_gen <carpeta Content> [--force]\n";
        return 2;
    }
    namespace fs = std::filesystem;
    const auto content = ge3d::pathFromUtf8(argv[1]);
    const bool force = argc > 2 && std::string(argv[2]) == "--force";

    const std::vector<std::pair<std::string, std::function<Canvas()>>> textures = {
        {"cesped", [] { return grass(3); }}, {"cesped_lado", grassSide}, {"tierra", [] { return dirt(17); }},
        {"piedra", stone}, {"adoquin", cobble}, {"arena", sand}, {"tablones", planks}, {"ladrillo", brick},
        {"teja", roof}, {"yeso", plaster}, {"agua", water}, {"nieve", snow}, {"hojas", leaves},
    };
    int written = 0;
    for (const auto& [name, make] : textures) {
        const auto path = content / "textures" / (name + ".png");
        std::error_code error;
        if (!force && fs::exists(path, error)) continue;
        ge3d::savePng(path, make().image());
        ++written;
    }

    const auto libraryPath = content / "materials.json";
    std::error_code error;
    if (force || !fs::exists(libraryPath, error)) {
        ge3d::MaterialLibrary library;
        const auto add = [&](const char* id, const char* name, const char* side, const char* top = "", const char* bottom = "") {
            ge3d::MaterialDef material;
            material.id = id;
            material.name = name;
            material.side = std::string("textures/") + side + ".png";
            if (*top) material.top = std::string("textures/") + top + ".png";
            if (*bottom) material.bottom = std::string("textures/") + bottom + ".png";
            library.add(std::move(material));
        };
        add("cesped", "Cesped", "cesped_lado", "cesped", "tierra");
        add("tierra", "Tierra", "tierra");
        add("piedra", "Piedra", "piedra");
        add("adoquin", "Adoquin", "adoquin");
        add("arena", "Arena", "arena");
        add("tablones", "Tablones", "tablones");
        add("ladrillo", "Ladrillo", "ladrillo");
        add("teja", "Teja", "teja");
        add("yeso", "Yeso", "yeso");
        add("agua", "Agua", "agua");
        add("nieve", "Nieve", "nieve");
        add("hojas", "Hojas", "hojas");
        ge3d::writeTextFileAtomic(libraryPath, ge3d::serializeMaterialLibrary(library));
        ++written;
    }
    std::cout << "Texturas y catalogo: " << written << " archivos escritos en " << ge3d::pathToUtf8(content) << '\n';
    return 0;
}
