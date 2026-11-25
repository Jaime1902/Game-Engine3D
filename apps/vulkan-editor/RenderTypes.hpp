#pragma once

#include "MaterialTextures.hpp"
#include <glm/glm.hpp>

#include <array>
#include <cstdint>

// Debe coincidir con ScenePush en shaders/scene.slang (128 bytes: el mínimo garantizado por Vulkan).
struct ScenePush {
    glm::mat4 model{1.0f};
    glm::vec4 fade{0.0f};
    glm::vec4 grid{0.0f};     // w = modo: 1 cuadrícula, 2 pieza, 3 vóxeles, 4 terreno de malla
    glm::uvec4 data{0u};
    glm::vec4 extra{0.0f};
};
static_assert(sizeof(ScenePush) == 128, "ScenePush debe ocupar 128 bytes");

inline constexpr float modeGrid = 1.0f;
inline constexpr float modePiece = 2.0f;
inline constexpr float modeVoxel = 3.0f;
inline constexpr float modeHeightmap = 4.0f;
inline constexpr float modeWater = 5.0f;
inline constexpr float modeModel = 6.0f;
inline constexpr VkShaderStageFlags pushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// Debe coincidir con FrameData en shaders/scene.slang.
struct FrameData {
    glm::mat4 viewProj{1.0f};
    glm::vec4 sun{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 style{0.0f, 16.0f, 0.0f, 0.0f};   // x bandas de luz (0 = continua), y texeles por repetición, z tiempo
    glm::vec4 camera{0.0f};                     // xyz posición de la cámara
    std::array<GpuMaterial, 256> materials{};
};
static_assert(sizeof(FrameData) == 64 + 16 + 16 + 16 + 256 * 16, "FrameData no coincide con el shader");

// Debe coincidir con PostPush en shaders/post.slang.
struct PostPush {
    glm::vec4 outline{0.0f};  // x oscurecer silueta, y aclarar aristas, z umbral de silueta, w umbral de arista
    glm::vec4 camera{0.0f};   // x plano cercano, y plano lejano, z niveles por canal, w tramado
    glm::vec4 extent{0.0f};   // xy zona dibujada en píxeles
};
