#pragma once

#include "GpuContext.hpp"
#include "ge3d/Materials.hpp"
#include <imgui.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Material tal como lo lee el shader (FrameData::materials). Debe coincidir con scene.slang.
struct GpuMaterial {
    std::uint32_t top{};
    std::uint32_t side{};
    std::uint32_t bottom{};
    std::uint32_t tint{0xFFFFFF};
};

// Texturas del catálogo de materiales en un Texture2DArray con mipmaps (nearest, sin mezclar
// colores de la paleta entre niveles más allá del promedio 2x2). La capa 0 es una textura de
// "falta material" (cuadros magenta) que se usa para ids desconocidos o PNG que no cargan.
class MaterialTextures {
public:
    static constexpr std::size_t maxMaterials = 255;   // índice 0 reservado para "falta material"

    void load(const GpuContext& context, const ge3d::MaterialLibrary& library, const std::filesystem::path& contentRoot,
              std::vector<std::string>& errors);
    void destroy();

    // Índice para el shader (0 = falta material).
    [[nodiscard]] std::uint32_t gpuIndex(std::string_view materialId) const noexcept;
    [[nodiscard]] const std::array<GpuMaterial, 256>& table() const noexcept { return table_; }
    [[nodiscard]] VkImageView view() const noexcept { return view_; }
    [[nodiscard]] VkSampler sampler() const noexcept { return sampler_; }
    [[nodiscard]] int textureSize() const noexcept { return size_; }
    [[nodiscard]] int layerCount() const noexcept { return layers_; }

    // Miniaturas para la interfaz (una por material del catálogo, de su textura superior).
    // Se vuelven a registrar si el backend de Dear ImGui se reinicia.
    void registerThumbnails();
    void unregisterThumbnails();
    [[nodiscard]] const std::vector<ImTextureID>& thumbnails() const noexcept { return thumbnails_; }

private:
    GpuContext context_{};
    VkImage image_{};
    VmaAllocation allocation_{};
    VkImageView view_{};
    VkSampler sampler_{};
    VkSampler thumbnailSampler_{};
    std::vector<VkImageView> layerViews_;      // una vista 2D por capa
    std::vector<std::uint32_t> thumbnailLayers_;  // capa de cada material del catálogo
    std::vector<ImTextureID> thumbnails_;
    std::array<GpuMaterial, 256> table_{};
    std::vector<std::string> ids_;             // ids del catálogo, en orden (gpuIndex = posición + 1)
    int size_{16};
    int layers_{0};
};
