#pragma once

#include "GpuContext.hpp"
#include "ge3d/Model.hpp"
#include "ge3d/Scene.hpp"
#include <glm/glm.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Dibuja los componentes Modelo. Cada modelo (archivo) se sube una vez a la GPU y lo comparten todas las
// entidades que lo usan. Las texturas de todos los modelos van a un Texture2DArray de 128 x 128 (cada
// textura se reescala con nearest: pensado para texturas pequeñas de pixel art), en el binding 2 del
// set de la escena.
class ModelRenderer {
public:
    static constexpr std::uint32_t textureSize = 128;
    static constexpr std::uint32_t maxTextures = 256;

    void init(const GpuContext& context);
    void destroy();
    // Sube lo que falte de los modelos de la escena. Se llama con la GPU libre de este fotograma; si el
    // array de texturas cambió (se rehízo esperando a la GPU) devuelve true y hay que reescribir los
    // descriptores.
    bool prepare(const ge3d::Scene& scene, ge3d::ModelLibrary& library);
    // Dibuja los modelos activos visibles. hidden: entidad (con sus hijos) que no se dibuja (el jugador en
    // primera persona). El pipeline de modelos y los descriptores ya deben estar enlazados.
    void draw(VkCommandBuffer command, VkPipelineLayout layout, const glm::mat4& viewProj, const ge3d::Scene& scene,
              ge3d::ModelLibrary& library, const std::function<glm::mat4(const ge3d::Entity&)>& worldMatrix,
              ge3d::EntityId hidden);

    [[nodiscard]] VkImageView textureView() const { return textures_.view; }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }
    struct Stats {
        int models{};
        int drawn{};
        std::size_t triangles{};
    };
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    struct Part {
        std::uint32_t firstIndex{};
        std::uint32_t indexCount{};
        std::int32_t layer{-1};
        std::uint32_t baseColor{0xFFFFFFFFu};
    };
    struct GpuModel {
        const ge3d::ModelData* data{};
        GpuBuffer vertices;
        GpuBuffer indices;
        std::vector<Part> parts;
        std::vector<int> textureLayers;   // capa del array de cada textura del modelo
    };
    struct TextureArray {
        VkImage image{};
        VkImageView view{};
        VmaAllocation allocation{};
        std::uint32_t layers{};
    };
    void upload(GpuModel& model, const ge3d::ModelData& data);
    void rebuildTextures();
    void release(GpuModel& model);

    GpuContext context_{};
    VkSampler sampler_{};
    TextureArray textures_;
    std::map<std::string, GpuModel> models_;
    std::vector<const ge3d::Image*> textureSources_;   // en orden de capa (la 0 es blanca)
    std::vector<std::shared_ptr<const ge3d::ModelData>> keepAlive_;
    Stats stats_;
};
