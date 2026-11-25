#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ge3d {

// Paleta indexada del proyecto. Las caras guardan un índice, no un color: cambiar la paleta
// recolorea todo de forma coherente, como pide el estilo pixel art (docs/guidelines.md).
struct PaletteColor {
    const char* name;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

using PaletteIndex = std::uint8_t;

// Paleta propia de 24 colores en rampas: grises, maderas y tierras, tejados, amarillos de farol,
// verdes, aguas y azules de cielo y noche, pensada para el pueblo al atardecer.
inline constexpr std::array<PaletteColor, 24> defaultPalette{{
    {"Negro", 0x1b, 0x1b, 0x23},
    {"Pizarra", 0x3b, 0x3f, 0x4f},
    {"Piedra", 0x6b, 0x6e, 0x7a},
    {"Gris claro", 0xa4, 0xa7, 0xb0},
    {"Blanco", 0xe8, 0xe6, 0xdf},
    {"Madera oscura", 0x5a, 0x3a, 0x2a},
    {"Madera", 0x8a, 0x5a, 0x3a},
    {"Arena", 0xc9, 0x9a, 0x5b},
    {"Naranja", 0xeb, 0x9e, 0x4f},
    {"Ladrillo", 0xa8, 0x46, 0x3c},
    {"Teja", 0xc8, 0x55, 0x3d},
    {"Rosa", 0xe0, 0x8a, 0x8a},
    {"Amarillo", 0xf2, 0xc8, 0x4b},
    {"Paja", 0xe6, 0xd1, 0x8a},
    {"Musgo", 0x4f, 0x6b, 0x3a},
    {"Hierba", 0x6f, 0x9a, 0x45},
    {"Lima", 0xa9, 0xc7, 0x5a},
    {"Verde agua", 0x66, 0xb5, 0xa8},
    {"Turquesa", 0x3f, 0x8a, 0x8c},
    {"Azul noche", 0x2b, 0x3a, 0x67},
    {"Azul", 0x3f, 0x6f, 0xb0},
    {"Cielo", 0x7f, 0xb2, 0xe0},
    {"Lavanda", 0x8f, 0x7f, 0xb8},
    {"Ciruela", 0x5c, 0x3d, 0x6e},
}};

inline constexpr std::size_t paletteSize = defaultPalette.size();
inline constexpr PaletteIndex paletteOrange = 8;
inline constexpr PaletteIndex paletteSeaGreen = 17;

}
