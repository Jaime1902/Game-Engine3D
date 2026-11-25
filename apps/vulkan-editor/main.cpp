#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
#include "EditorLog.hpp"
#include "EditorView.hpp"
#include "MaterialTextures.hpp"
#include "RenderTypes.hpp"
#include "TerrainRenderer.hpp"
#include "ge3d/Materials.hpp"
#include "ge3d/Primitives.hpp"
#include "ge3d/SceneIO.hpp"
#include <chrono>
#include <filesystem>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>
#include <cstddef>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(operation);
    }
}

// --- Capas de validación -------------------------------------------------------------------
// Activas por defecto en Debug. GE3D_VALIDATION=0 las desactiva y GE3D_VALIDATION=1 las fuerza en Release.

constexpr const char* validationLayer = "VK_LAYER_KHRONOS_validation";
std::atomic<int> validationErrors{0};
std::atomic<int> validationWarnings{0};

VKAPI_ATTR VkBool32 VKAPI_CALL onValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
    ++(error ? validationErrors : validationWarnings);
    // Se muestra en la Consola del editor y también en la terminal.
    editorLog().add(error ? LogLevel::error : LogLevel::warning,
                    std::string(error ? "[Vulkan error] " : "[Vulkan aviso] ") +
                        (data && data->pMessage ? data->pMessage : "(sin mensaje)"));
    return VK_FALSE;
}

bool validationRequested() {
    if (const char* value = std::getenv("GE3D_VALIDATION")) return std::string_view(value) != "0";
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

bool hasInstanceLayer(const char* name) {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) return false;
    return std::any_of(layers.begin(), layers.end(),
                       [&](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, name) == 0; });
}

VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = onValidationMessage;
    return info;
}


// La escena se dibuja a una resolución interna (la vista dividida por el tamaño del píxel) en imágenes
// con capacidad de sobra, para no recrearlas cada vez que cambia el tamaño del panel.
constexpr VkExtent2D initialSceneCapacity{1280, 768};
constexpr VkFormat sceneFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat normalFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

struct RenderImage {
    VkImage image{};
    VkImageView view{};
    VmaAllocation allocation{};
};

void createRenderImage(VkDevice device, VmaAllocator allocator, VkFormat format, VkExtent2D extent,
                       VkImageUsageFlags usage, VkImageAspectFlags aspect, RenderImage& target) {
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {extent.width, extent.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = usage;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    check(vmaCreateImage(allocator, &image, &allocation, &target.image, &target.allocation, nullptr),
          "Allocate scene image");

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = target.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, 1, 0, 1};
    check(vkCreateImageView(device, &view, nullptr, &target.view), "Create scene image view");
}

void transition(VkCommandBuffer command, VkImage image, VkImageAspectFlags aspect,
                VkImageLayout oldLayout, VkImageLayout newLayout,
                VkPipelineStageFlags2 sourceStage, VkAccessFlags2 sourceAccess,
                VkPipelineStageFlags2 destinationStage, VkAccessFlags2 destinationAccess) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = destinationStage;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

bool supportsFormat(VkPhysicalDevice gpu, VkFormat format, VkFormatFeatureFlags required) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(gpu, format, &properties);
    return (properties.optimalTilingFeatures & required) == required;
}

std::vector<std::uint32_t> readShader(const char* name) {
    const std::string path = std::string(GE3D_SHADER_DIR) + "/" + name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open shader: " + path);
    const auto size = file.tellg();
    if (size <= 0 || size % 4 != 0) throw std::runtime_error("Invalid SPIR-V shader: " + path);
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), size);
    if (!file) throw std::runtime_error("Cannot read shader: " + path);
    return words;
}

VkShaderModule createShaderModule(VkDevice device, const char* name) {
    const auto words = readShader(name);
    VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create.codeSize = words.size() * sizeof(std::uint32_t);
    create.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &create, nullptr, &module), "Create shader module");
    return module;
}


// Vértice de piezas y cuadrícula. Pieza: color.x = número de cara. Cuadrícula: color de la línea.
struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
};

// Índices de material de las 6 caras empaquetados de dos en dos (16 bits cada uno).
glm::uvec4 packFaceMaterials(const ge3d::ShapeComponent& shape, const MaterialTextures& textures) {
    std::uint32_t index[6];
    for (std::size_t face = 0; face < 6; ++face) index[face] = textures.gpuIndex(shape.materials[face]) & 0xFFFFu;
    return {index[0] | (index[1] << 16), index[2] | (index[3] << 16), index[4] | (index[5] << 16), 0u};
}

// Semiancho de la cuadrícula base en líneas. Se recoloca y se escala cada fotograma según la cámara.
constexpr int gridHalfLines = 50;

struct MeshRange {
    std::uint32_t firstIndex{};
    std::uint32_t count{};
};

struct SceneGeometry {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    MeshRange box;
    MeshRange ramp;
    std::uint32_t gridFirst{};
    std::uint32_t gridCount{};
};

SceneGeometry makeGeometry() {
    SceneGeometry geometry;
    // Cada vértice guarda su número de cara y la normal de la cara; material, textura y luz los pone el shader.
    const auto append = [&](ge3d::Primitive primitive) {
        auto mesh = ge3d::makePrimitive(primitive);
        MeshRange range{static_cast<std::uint32_t>(geometry.indices.size()), 0};
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> faceVertices;
        const auto point = [&](std::uint32_t index) {
            const auto p = mesh.positions[index];
            return glm::vec3{p.x, p.y, p.z};
        };
        glm::vec3 center{0.0f};
        for (std::uint32_t i = 0; i < mesh.positions.size(); ++i) center += point(i);
        center /= static_cast<float>(mesh.positions.size());
        for (const auto& triangle : mesh.triangles) {
            const auto& t = triangle.indices;
            glm::vec3 normal = glm::normalize(glm::cross(point(t[1]) - point(t[0]), point(t[2]) - point(t[0])));
            // Hacia fuera de la pieza: del centro de la pieza hacia el centro del triángulo.
            const glm::vec3 middle = (point(t[0]) + point(t[1]) + point(t[2])) / 3.0f;
            if (glm::dot(normal, middle - center) < 0) normal = -normal;
            for (const auto index : triangle.indices) {
                const auto [it, inserted] = faceVertices.try_emplace(
                    std::pair{triangle.face, index}, static_cast<std::uint32_t>(geometry.vertices.size()));
                if (inserted) {
                    geometry.vertices.push_back({point(index), {static_cast<float>(triangle.face), 0.0f, 0.0f}, normal});
                }
                geometry.indices.push_back(it->second);
            }
        }
        range.count = static_cast<std::uint32_t>(geometry.indices.size()) - range.firstIndex;
        return range;
    };
    geometry.box = append(ge3d::Primitive::box);
    geometry.ramp = append(ge3d::Primitive::ramp);
    geometry.gridFirst = static_cast<std::uint32_t>(geometry.vertices.size());
    const auto line = [&](glm::vec3 a, glm::vec3 b, glm::vec3 color) {
        geometry.vertices.push_back({a, color, {0, 1, 0}});
        geometry.vertices.push_back({b, color, {0, 1, 0}});
    };
    // Los ejes rojo/azul los colorea el shader según la posición en el mundo.
    constexpr float extent = static_cast<float>(gridHalfLines);
    const glm::vec3 minor{0.32f, 0.34f, 0.37f};
    for (int i = -gridHalfLines; i <= gridHalfLines; ++i) {
        const float coordinate = static_cast<float>(i);
        line({coordinate, 0, -extent}, {coordinate, 0, extent}, minor);
        line({-extent, 0, coordinate}, {extent, 0, coordinate}, minor);
    }
    geometry.gridCount = static_cast<std::uint32_t>(geometry.vertices.size()) - geometry.gridFirst;
    return geometry;
}

struct Buffer {
    VkBuffer handle{};
    VmaAllocation allocation{};
};

void createBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage,
                  VmaAllocationCreateFlags flags, Buffer& buffer) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    VmaAllocationCreateInfo allocation{};
    allocation.flags = flags;
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    check(vmaCreateBuffer(allocator, &info, &allocation, &buffer.handle, &buffer.allocation, nullptr),
        "Allocate geometry buffer");
}

// Pipelines de la escena: piezas (triángulos), cuadrícula (líneas), vóxeles (vértice compacto),
// terreno de malla y la pasada de estilo pixel art (triángulo a pantalla completa, sin vértices).
enum class PipelineKind { piece, grid, voxel, height, water, post };

VkPipeline createScenePipeline(VkDevice device, VkPipelineLayout layout, PipelineKind kind) {
    const bool post = kind == PipelineKind::post;
    const char* vertexName = kind == PipelineKind::voxel ? "scene.voxel.spv"
                           : kind == PipelineKind::height ? "scene.height.spv"
                           : kind == PipelineKind::water ? "scene.water.spv"
                           : post ? "post.vertex.spv" : "scene.vertex.spv";
    VkShaderModule vertex = createShaderModule(device, vertexName);
    VkShaderModule fragment = VK_NULL_HANDLE;
    try {
        fragment = createShaderModule(device, post ? "post.fragment.spv" : "scene.fragment.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = "main";
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        const std::uint32_t stride = kind == PipelineKind::voxel ? static_cast<std::uint32_t>(sizeof(ge3d::VoxelVertex))
                                   : kind == PipelineKind::height ? static_cast<std::uint32_t>(sizeof(ge3d::HeightVertex))
                                   : kind == PipelineKind::water ? static_cast<std::uint32_t>(sizeof(ge3d::WaterVertex))
                                   : static_cast<std::uint32_t>(sizeof(Vertex));
        const VkVertexInputBindingDescription binding{0, stride, VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attributes[] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
            {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)}
        };
        const VkVertexInputAttributeDescription voxelAttribute{0, 0, VK_FORMAT_R32G32_UINT, 0};
        const VkVertexInputAttributeDescription heightAttributes[] = {
            {0, 0, VK_FORMAT_R32_SFLOAT, offsetof(ge3d::HeightVertex, height)},
            {1, 0, VK_FORMAT_R32_UINT, offsetof(ge3d::HeightVertex, normal)},
            {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ge3d::HeightVertex, weights)},
            {3, 0, VK_FORMAT_R32_UINT, offsetof(ge3d::HeightVertex, cell)}
        };
        const VkVertexInputAttributeDescription waterAttributes[] = {
            {0, 0, VK_FORMAT_R32_SFLOAT, offsetof(ge3d::WaterVertex, surface)},
            {1, 0, VK_FORMAT_R32_SFLOAT, offsetof(ge3d::WaterVertex, depth)},
            {2, 0, VK_FORMAT_R32_UINT, offsetof(ge3d::WaterVertex, normal)},
            {3, 0, VK_FORMAT_R32_UINT, offsetof(ge3d::WaterVertex, cell)}
        };
        if (!post) {
            vertexInput.vertexBindingDescriptionCount = 1;
            vertexInput.pVertexBindingDescriptions = &binding;
            vertexInput.vertexAttributeDescriptionCount = kind == PipelineKind::voxel ? 1
                                                        : (kind == PipelineKind::height || kind == PipelineKind::water) ? 4 : 3;
            vertexInput.pVertexAttributeDescriptions = kind == PipelineKind::voxel ? &voxelAttribute
                                                     : kind == PipelineKind::height ? heightAttributes
                                                     : kind == PipelineKind::water ? waterAttributes : attributes;
        }
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = kind == PipelineKind::grid ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        // Escena: color + normal (para los contornos). Post: solo color.
        VkPipelineColorBlendAttachmentState blendAttachments[2]{};
        for (auto& attachment : blendAttachments) {
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = post ? 1 : 2;
        blend.pAttachments = blendAttachments;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = post ? VK_FALSE : VK_TRUE;
        depth.depthWriteEnable = post ? VK_FALSE : VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkFormat colorFormats[] = {sceneFormat, normalFormat};
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = post ? 1 : 2;
        rendering.pColorAttachmentFormats = colorFormats;
        rendering.depthAttachmentFormat = post ? VK_FORMAT_UNDEFINED : depthFormat;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        if (result != VK_SUCCESS) {
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            check(result, "Create cube graphics pipeline");
        }
        vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
        return pipeline;
    } catch (...) {
        if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
        throw;
    }
}

struct QueueFamilies {
    std::uint32_t graphics = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t present = std::numeric_limits<std::uint32_t>::max();
    [[nodiscard]] bool complete() const { return graphics != UINT32_MAX && present != UINT32_MAX; }
};

QueueFamilies findQueues(VkPhysicalDevice device, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    QueueFamilies result;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) result.graphics = i;
        VkBool32 supported = VK_FALSE;
        check(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supported), "Query surface support");
        if (supported) result.present = i;
        if (result.complete()) break;
    }
    return result;
}

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> modes;
};

SwapchainSupport querySwapchain(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupport result;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &result.capabilities), "Query surface capabilities");
    std::uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr), "Query surface formats");
    result.formats.resize(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, result.formats.data()), "Read surface formats");
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr), "Query present modes");
    result.modes.resize(count);
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, result.modes.data()), "Read present modes");
    return result;
}

void run(const std::filesystem::path& initialScene) {
    if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Game-Engine3D | Editor de escenas", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("Window creation failed");
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    bool validation = validationRequested();
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline cubePipeline = VK_NULL_HANDLE;
    VkPipeline gridPipeline = VK_NULL_HANDLE;
    VkPipeline voxelPipeline = VK_NULL_HANDLE;
    VkPipeline heightPipeline = VK_NULL_HANDLE;
    VkPipeline waterPipeline = VK_NULL_HANDLE;
    VkPipeline postPipeline = VK_NULL_HANDLE;
    VkPipelineLayout postLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout postSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool postPool = VK_NULL_HANDLE;
    VkDescriptorSet postSets[2]{};
    VkDescriptorSetLayout sceneSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool scenePool = VK_NULL_HANDLE;
    VkDescriptorSet sceneSets[2]{};
    GpuBuffer frameUniforms[2]{};
    MaterialTextures textures;
    TerrainRenderer terrainRenderer;
    bool terrainReady = false;
    Buffer vertices, indices, staging;
    VkSampler sceneSampler{};
    VkDescriptorSet sceneTextures[2]{};
    std::vector<VkImageView> swapViews;
    bool uiContext = false;
    bool uiPlatform = false;
    bool uiRenderer = false;
    VkSemaphore imageReady[2]{};
    std::vector<VkSemaphore> renderDone;
    VkFence frameFence[2]{};
    VmaAllocator allocator{};
    RenderImage sceneColors[2]{};
    RenderImage sceneNormals[2]{};
    RenderImage sceneDepths[2]{};
    RenderImage finalColors[2]{};
    VkExtent2D sceneCapacity{};
    const auto destroySceneTargets = [&] {
        for (int i = 0; i < 2; ++i) {
            for (auto* target : {&sceneColors[i], &sceneNormals[i], &sceneDepths[i], &finalColors[i]}) {
                if (target->view) vkDestroyImageView(device, target->view, nullptr);
                if (target->image) vmaDestroyImage(allocator, target->image, target->allocation);
                *target = {};
            }
        }
    };
    const auto destroySwapchain = [&] {
        for (auto semaphore : renderDone) {
            if (semaphore) vkDestroySemaphore(device, semaphore, nullptr);
        }
        renderDone.clear();
        for (auto view : swapViews) {
            if (view) vkDestroyImageView(device, view, nullptr);
        }
        swapViews.clear();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    };
    const auto cleanup = [&] {
        if (device) {
            vkDeviceWaitIdle(device);
            if (uiRenderer) textures.unregisterThumbnails();
            if (uiRenderer) ImGui_ImplVulkan_Shutdown();
            textures.destroy();
            if (terrainReady) terrainRenderer.destroy();
            for (auto& uniforms : frameUniforms) destroyGpuBuffer(allocator, uniforms);
            if (scenePool) vkDestroyDescriptorPool(device, scenePool, nullptr);
            if (sceneSetLayout) vkDestroyDescriptorSetLayout(device, sceneSetLayout, nullptr);
            if (voxelPipeline) vkDestroyPipeline(device, voxelPipeline, nullptr);
            if (heightPipeline) vkDestroyPipeline(device, heightPipeline, nullptr);
            if (waterPipeline) vkDestroyPipeline(device, waterPipeline, nullptr);
            if (postPipeline) vkDestroyPipeline(device, postPipeline, nullptr);
            if (postLayout) vkDestroyPipelineLayout(device, postLayout, nullptr);
            if (postPool) vkDestroyDescriptorPool(device, postPool, nullptr);
            if (postSetLayout) vkDestroyDescriptorSetLayout(device, postSetLayout, nullptr);
            if (sceneSampler) vkDestroySampler(device, sceneSampler, nullptr);
            for (const auto& buffer : {vertices, indices, staging}) {
                if (buffer.handle) vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
            }
            if (gridPipeline) vkDestroyPipeline(device, gridPipeline, nullptr);
            if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
            destroySwapchain();
            if (cubePipeline) vkDestroyPipeline(device, cubePipeline, nullptr);
            if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            destroySceneTargets();
            for (int i = 0; i < 2; ++i) {
                if (imageReady[i]) vkDestroySemaphore(device, imageReady[i], nullptr);
                if (frameFence[i]) vkDestroyFence(device, frameFence[i], nullptr);
            }
            if (allocator) vmaDestroyAllocator(allocator);
            vkDestroyDevice(device, nullptr);
        }
        if (uiPlatform) ImGui_ImplGlfw_Shutdown();
        if (uiContext) ImGui::DestroyContext();
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (messenger) {
            const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyMessenger) destroyMessenger(instance, messenger, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        if (validation) {
            std::cout << "Validacion Vulkan: " << validationErrors << " errores, "
                      << validationWarnings << " avisos\n";
        }
    };
    try {
        std::uint32_t loaderVersion = VK_API_VERSION_1_0;
        const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
        if (enumerateVersion) check(enumerateVersion(&loaderVersion), "Query Vulkan loader version");
        if (loaderVersion < VK_API_VERSION_1_3) throw std::runtime_error("Vulkan 1.3 loader required");

        std::uint32_t extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (!extensions) throw std::runtime_error("GLFW could not load Vulkan surface extensions");
        if (validation && !hasInstanceLayer(validationLayer)) {
            std::cerr << "Capa de validacion no instalada (" << validationLayer << "); se continua sin ella\n";
            validation = false;
        }
        std::vector<const char*> instanceExtensions(extensions, extensions + extensionCount);
        if (validation) instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        const auto messengerInfo = messengerCreateInfo();

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Game-Engine3D";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create.pApplicationInfo = &app;
        create.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
        create.ppEnabledExtensionNames = instanceExtensions.data();
        if (validation) {
            // Encadenado también aquí para validar la creación y destrucción de la instancia.
            create.pNext = &messengerInfo;
            create.enabledLayerCount = 1;
            create.ppEnabledLayerNames = &validationLayer;
        }
        check(vkCreateInstance(&create, nullptr, &instance), "Create Vulkan instance");
        if (validation) {
            const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (!createMessenger) throw std::runtime_error("vkCreateDebugUtilsMessengerEXT no disponible");
            check(createMessenger(instance, &messengerInfo, nullptr, &messenger), "Create debug messenger");
            editorLog().add(LogLevel::info, "Validacion Vulkan activa");
        }
        check(glfwCreateWindowSurface(instance, window, nullptr, &surface), "Create GLFW Vulkan surface");

        std::uint32_t gpuCount = 0;
        check(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr), "Enumerate GPUs");
        if (gpuCount == 0) throw std::runtime_error("No Vulkan physical device found");
        std::vector<VkPhysicalDevice> gpus(gpuCount);
        check(vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data()), "Read GPUs");
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        QueueFamilies queues;
        for (auto candidate : gpus) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3) continue;
            VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
            features.pNext = &features11;
            VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features2.pNext = &features;
            vkGetPhysicalDeviceFeatures2(candidate, &features2);
            const auto candidateQueues = findQueues(candidate, surface);
            const auto support = querySwapchain(candidate, surface);
            if (features.dynamicRendering && features.synchronization2 && features11.shaderDrawParameters && candidateQueues.complete() &&
                !support.formats.empty() && !support.modes.empty() &&
                (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
                supportsFormat(candidate, sceneFormat, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) &&
                supportsFormat(candidate, depthFormat, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
                physical = candidate;
                queues = candidateQueues;
                break;
            }
        }
        if (physical == VK_NULL_HANDLE) throw std::runtime_error("No GPU supports Vulkan 1.3 rendering and presentation");

        const float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (auto family : {queues.graphics, queues.present}) {
            if (!queueInfos.empty() && queueInfos.front().queueFamilyIndex == family) continue;
            VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &priority;
            queueInfos.push_back(q);
        }
        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkPhysicalDeviceVulkan13Features enabled13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        enabled13.dynamicRendering = VK_TRUE;
        enabled13.synchronization2 = VK_TRUE;
        // SV_VertexID del triángulo a pantalla completa de la pasada de post.
        VkPhysicalDeviceVulkan11Features enabled11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        enabled11.shaderDrawParameters = VK_TRUE;
        enabled13.pNext = &enabled11;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.pNext = &enabled13;
        deviceInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "Create Vulkan device");
        VkQueue graphicsQueue{}, presentQueue{};
        vkGetDeviceQueue(device, queues.graphics, 0, &graphicsQueue);
        vkGetDeviceQueue(device, queues.present, 0, &presentQueue);


        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.instance = instance;
        allocatorInfo.physicalDevice = physical;
        allocatorInfo.device = device;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        check(vmaCreateAllocator(&allocatorInfo, &allocator), "Create Vulkan memory allocator");
        // Descriptores de la escena: datos del fotograma (cámara y materiales) y el array de texturas.
        const VkDescriptorSetLayoutBinding sceneBindings[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        };
        VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setLayoutInfo.bindingCount = 2;
        setLayoutInfo.pBindings = sceneBindings;
        check(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &sceneSetLayout), "Create scene set layout");
        VkPushConstantRange pushRange{pushStages, 0, sizeof(ScenePush)};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &sceneSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout), "Create pipeline layout");
        cubePipeline = createScenePipeline(device, pipelineLayout, PipelineKind::piece);
        gridPipeline = createScenePipeline(device, pipelineLayout, PipelineKind::grid);
        voxelPipeline = createScenePipeline(device, pipelineLayout, PipelineKind::voxel);
        heightPipeline = createScenePipeline(device, pipelineLayout, PipelineKind::height);
        waterPipeline = createScenePipeline(device, pipelineLayout, PipelineKind::water);

        // Pasada de estilo: lee color, normal y profundidad de la escena.
        VkDescriptorSetLayoutBinding postBindings[3]{};
        for (std::uint32_t i = 0; i < 3; ++i) {
            postBindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        }
        VkDescriptorSetLayoutCreateInfo postSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        postSetInfo.bindingCount = 3;
        postSetInfo.pBindings = postBindings;
        check(vkCreateDescriptorSetLayout(device, &postSetInfo, nullptr, &postSetLayout), "Create post set layout");
        const VkPushConstantRange postRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPush)};
        VkPipelineLayoutCreateInfo postLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        postLayoutInfo.setLayoutCount = 1;
        postLayoutInfo.pSetLayouts = &postSetLayout;
        postLayoutInfo.pushConstantRangeCount = 1;
        postLayoutInfo.pPushConstantRanges = &postRange;
        check(vkCreatePipelineLayout(device, &postLayoutInfo, nullptr, &postLayout), "Create post pipeline layout");
        postPipeline = createScenePipeline(device, postLayout, PipelineKind::post);
        const VkDescriptorPoolSize postPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6};
        VkDescriptorPoolCreateInfo postPoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        postPoolInfo.maxSets = 2;
        postPoolInfo.poolSizeCount = 1;
        postPoolInfo.pPoolSizes = &postPoolSize;
        check(vkCreateDescriptorPool(device, &postPoolInfo, nullptr, &postPool), "Create post descriptor pool");
        const VkDescriptorSetLayout postLayouts[] = {postSetLayout, postSetLayout};
        VkDescriptorSetAllocateInfo postAllocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        postAllocate.descriptorPool = postPool;
        postAllocate.descriptorSetCount = 2;
        postAllocate.pSetLayouts = postLayouts;
        check(vkAllocateDescriptorSets(device, &postAllocate, postSets), "Allocate post descriptor sets");

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
        };
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &scenePool), "Create scene descriptor pool");
        const VkDescriptorSetLayout setLayouts[] = {sceneSetLayout, sceneSetLayout};
        VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = scenePool;
        setInfo.descriptorSetCount = 2;
        setInfo.pSetLayouts = setLayouts;
        check(vkAllocateDescriptorSets(device, &setInfo, sceneSets), "Allocate scene descriptor sets");
        for (int i = 0; i < 2; ++i) {
            frameUniforms[i] = createGpuBuffer(allocator, sizeof(FrameData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        }
        const auto geometry = makeGeometry();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        uiContext = true;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::GetIO().IniFilename = nullptr;
        float contentScaleX = 1.0f;
        float contentScaleY = 1.0f;
        glfwGetWindowContentScale(window, &contentScaleX, &contentScaleY);
        EditorView editor(contentScaleX, initialScene);
        ge3d::MaterialLibrary library;
        std::uint64_t materialsVersion = 1;
        const auto contentRoot = ge3d::pathFromUtf8(GE3D_CONTENT_DIR);
        uiPlatform = ImGui_ImplGlfw_InitForVulkan(window, true);
        if (!uiPlatform) throw std::runtime_error("Initialize GLFW editor backend");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = VK_FILTER_NEAREST;
        sampler.minFilter = VK_FILTER_NEAREST;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(device, &sampler, nullptr, &sceneSampler), "Create pixel art sampler");

        // Imágenes de la escena con la capacidad indicada (redondeada hacia arriba para crecer pocas veces).
        // Se llama con la GPU parada. Vuelve a escribir los descriptores del post y las texturas de la vista.
        const auto createSceneTargets = [&](VkExtent2D requested) {
            if (sceneCapacity.width) check(vkDeviceWaitIdle(device), "Wait before resizing scene images");
            if (uiRenderer && sceneCapacity.width) {
                for (auto& texture : sceneTextures) ImGui_ImplVulkan_RemoveTexture(texture);
            }
            destroySceneTargets();
            const auto round = [](std::uint32_t value) { return (value + 255u) & ~255u; };
            sceneCapacity = {round(std::max(requested.width, sceneCapacity.width)),
                             round(std::max(requested.height, sceneCapacity.height))};
            for (int i = 0; i < 2; ++i) {
                createRenderImage(device, allocator, sceneFormat, sceneCapacity,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, sceneColors[i]);
                createRenderImage(device, allocator, normalFormat, sceneCapacity,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, sceneNormals[i]);
                createRenderImage(device, allocator, depthFormat, sceneCapacity,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, sceneDepths[i]);
                createRenderImage(device, allocator, sceneFormat, sceneCapacity,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, finalColors[i]);
                const VkDescriptorImageInfo images[3] = {
                    {sceneSampler, sceneColors[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                    {sceneSampler, sceneNormals[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                    {sceneSampler, sceneDepths[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                };
                VkWriteDescriptorSet writes[3]{};
                for (std::uint32_t b = 0; b < 3; ++b) {
                    writes[b] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    writes[b].dstSet = postSets[i];
                    writes[b].dstBinding = b;
                    writes[b].descriptorCount = 1;
                    writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[b].pImageInfo = &images[b];
                }
                vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
                if (uiRenderer) {
                    sceneTextures[i] = ImGui_ImplVulkan_AddTexture(sceneSampler, finalColors[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            }
        };
        createSceneTargets(initialSceneCapacity);
        VkFormat uiFormat = VK_FORMAT_UNDEFINED;
        std::uint32_t uiImageCount = 0;

        VkExtent2D extent{};
        std::vector<VkImage> images;
        const auto createSwapchain = [&](int width, int height) {
            const auto support = querySwapchain(physical, surface);
            const auto colorCompatible = [&](const VkSurfaceFormatKHR& candidate) {
                return candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                    (candidate.format == VK_FORMAT_B8G8R8A8_SRGB || candidate.format == VK_FORMAT_R8G8B8A8_SRGB ||
                     candidate.format == VK_FORMAT_B8G8R8A8_UNORM || candidate.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                    supportsFormat(physical, candidate.format, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
            };
            // UNORM: la escena pixel art y los colores de la interfaz ya están en espacio sRGB. Un swapchain
            // *_SRGB volvería a aplicar la gamma y aclararía todo (colores lavados y paleta alterada).
            auto format = std::find_if(support.formats.begin(), support.formats.end(), [&](const auto& candidate) {
                return colorCompatible(candidate) &&
                       (candidate.format == VK_FORMAT_B8G8R8A8_UNORM || candidate.format == VK_FORMAT_R8G8B8A8_UNORM);
            });
            if (format == support.formats.end()) {
                format = std::find_if(support.formats.begin(), support.formats.end(), colorCompatible);
            }
            if (format == support.formats.end()) throw std::runtime_error("No compatible swapchain color format");

            const auto& capabilities = support.capabilities;
            extent = capabilities.currentExtent;
            if (extent.width == UINT32_MAX) {
                extent.width = std::clamp(static_cast<std::uint32_t>(width),
                    capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
                extent.height = std::clamp(static_cast<std::uint32_t>(height),
                    capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            }
            if (extent.width == 0 || extent.height == 0) return false;

            if (swapchain) {
                check(vkDeviceWaitIdle(device), "Wait before recreating swapchain");
                destroySwapchain();
            }
            auto imageCount = capabilities.minImageCount + 1;
            if (capabilities.maxImageCount) imageCount = std::min(imageCount, capabilities.maxImageCount);

            VkSwapchainCreateInfoKHR swapInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
            swapInfo.surface = surface;
            swapInfo.minImageCount = imageCount;
            swapInfo.imageFormat = format->format;
            swapInfo.imageColorSpace = format->colorSpace;
            swapInfo.imageExtent = extent;
            swapInfo.imageArrayLayers = 1;
            swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            const std::uint32_t familyIndices[] = {queues.graphics, queues.present};
            swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (queues.graphics != queues.present) {
                swapInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                swapInfo.queueFamilyIndexCount = 2;
                swapInfo.pQueueFamilyIndices = familyIndices;
            }
            swapInfo.preTransform = capabilities.currentTransform;
            const auto alphaModes = capabilities.supportedCompositeAlpha;
            swapInfo.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(alphaModes & (0u - alphaModes));
            swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            swapInfo.clipped = VK_TRUE;
            check(vkCreateSwapchainKHR(device, &swapInfo, nullptr, &swapchain), "Create swapchain");

            std::uint32_t actualImages = 0;
            check(vkGetSwapchainImagesKHR(device, swapchain, &actualImages, nullptr), "Query swapchain images");
            images.resize(actualImages);
            check(vkGetSwapchainImagesKHR(device, swapchain, &actualImages, images.data()), "Read swapchain images");
            images.resize(actualImages);
            renderDone.resize(actualImages);
            swapViews.resize(actualImages);
            for (std::size_t i = 0; i < swapViews.size(); ++i) {
                VkImageViewCreateInfo imageView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                imageView.image = images[i];
                imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
                imageView.format = format->format;
                imageView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                check(vkCreateImageView(device, &imageView, nullptr, &swapViews[i]), "Create editor image view");
            }
            if (!uiRenderer || uiFormat != format->format || uiImageCount != actualImages) {
                if (uiRenderer) {
                    ImGui_ImplVulkan_Shutdown();
                    uiRenderer = false;
                }
                uiFormat = format->format;
                uiImageCount = actualImages;
                ImGui_ImplVulkan_InitInfo info{};
                info.ApiVersion = VK_API_VERSION_1_3;
                info.Instance = instance;
                info.PhysicalDevice = physical;
                info.Device = device;
                info.QueueFamily = queues.graphics;
                info.Queue = graphicsQueue;
                info.DescriptorPoolSize = 512;  // vista de escena + miniaturas de materiales
                info.MinImageCount = std::max(2u, capabilities.minImageCount);
                info.ImageCount = actualImages;
                info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
                info.UseDynamicRendering = true;
                info.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
                info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &uiFormat;
                info.CheckVkResultFn = [](VkResult result) { check(result, "Dear ImGui Vulkan operation"); };
                uiRenderer = ImGui_ImplVulkan_Init(&info);
                if (!uiRenderer) throw std::runtime_error("Initialize Vulkan editor backend");
                for (int i = 0; i < 2; ++i) {
                    sceneTextures[i] = ImGui_ImplVulkan_AddTexture(sceneSampler, finalColors[i].view,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
                // Las miniaturas de materiales viven en el pool del backend: se registran de nuevo.
                textures.registerThumbnails();
                editor.setMaterials(library, textures.thumbnails());
            }
            VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            for (auto& semaphore : renderDone) {
                check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore), "Create present semaphore");
            }
            return true;
        };
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex = queues.graphics;
        check(vkCreateCommandPool(device, &pool, nullptr, &commandPool), "Create command pool");
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = commandPool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 2;
        VkCommandBuffer commands[2]{};
        check(vkAllocateCommandBuffers(device, &alloc, commands), "Allocate command buffers");
        for (int i = 0; i < 2; ++i) {
            VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            check(vkCreateSemaphore(device, &sem, nullptr, &imageReady[i]), "Create acquire semaphore");
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(vkCreateFence(device, &fence, nullptr, &frameFence[i]), "Create frame fence");
        }

        // --- Materiales y texturas -------------------------------------------------------------
        // Se recargan solos al guardar materials.json o cualquier PNG que usen (se comprueba cada segundo).
        const GpuContext gpu{device, allocator, graphicsQueue, commandPool};
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> watched;
        const auto writeSceneSets = [&] {
            for (int i = 0; i < 2; ++i) {
                const VkDescriptorBufferInfo bufferInfo{frameUniforms[i].handle, 0, sizeof(FrameData)};
                const VkDescriptorImageInfo imageInfo{textures.sampler(), textures.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet writes[2]{};
                writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                writes[0].dstSet = sceneSets[i];
                writes[0].dstBinding = 0;
                writes[0].descriptorCount = 1;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[0].pBufferInfo = &bufferInfo;
                writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                writes[1].dstSet = sceneSets[i];
                writes[1].dstBinding = 1;
                writes[1].descriptorCount = 1;
                writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[1].pImageInfo = &imageInfo;
                vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
            }
        };
        const auto fileTime = [](const std::filesystem::path& path) {
            std::error_code error;
            const auto time = std::filesystem::last_write_time(path, error);
            return error ? std::filesystem::file_time_type{} : time;
        };
        const auto loadMaterials = [&](bool initial) {
            std::vector<std::string> errors;
            ge3d::MaterialLibrary loaded;
            try {
                loaded = ge3d::loadMaterialLibrary(contentRoot / "materials.json");
            } catch (const std::exception& error) {
                errors.push_back(std::string("Catalogo de materiales: ") + error.what() + " (se conserva el anterior)");
                loaded = library;
            }
            if (!initial) {
                check(vkDeviceWaitIdle(device), "Wait before reloading materials");
                if (uiRenderer) textures.unregisterThumbnails();
            }
            library = loaded;
            textures.load(gpu, library, contentRoot, errors);
            writeSceneSets();
            if (uiRenderer) textures.registerThumbnails();
            editor.setMaterials(library, textures.thumbnails());
            ++materialsVersion;
            for (const auto& error : errors) editorLog().add(LogLevel::warning, "Materiales: " + error);
            editorLog().add(LogLevel::info, (initial ? "Materiales: " : "Materiales recargados: ") +
                std::to_string(library.materials().size()) + " materiales, " + std::to_string(textures.layerCount()) +
                " texturas de " + std::to_string(textures.textureSize()) + " px");
            watched.clear();
            watched.emplace_back(contentRoot / "materials.json", fileTime(contentRoot / "materials.json"));
            for (const auto& material : library.materials()) {
                for (const auto* path : {&material.side, &material.top, &material.bottom}) {
                    if (path->empty()) continue;
                    const auto full = contentRoot / ge3d::pathFromUtf8(*path);
                    watched.emplace_back(full, fileTime(full));
                }
            }
        };
        loadMaterials(true);
        terrainRenderer.init(gpu);
        terrainReady = true;
        auto lastWatch = std::chrono::steady_clock::now();
        std::uint64_t frameNumber = 0;


        constexpr VkDeviceSize vertexCapacity = 4 * 1024 * 1024;
        constexpr VkDeviceSize indexCapacity = 2 * 1024 * 1024;
        constexpr VkDeviceSize stagingCapacity = 8 * 1024 * 1024;
        const VkDeviceSize vertexBytes = geometry.vertices.size() * sizeof(Vertex);
        const VkDeviceSize indexBytes = geometry.indices.size() * sizeof(std::uint32_t);
        if (vertexBytes > vertexCapacity || indexBytes > indexCapacity ||
            vertexBytes + indexBytes > stagingCapacity) {
            throw std::runtime_error("Starter geometry exceeds the initial geometry buffers");
        }
        createBuffer(allocator, vertexCapacity, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, vertices);
        createBuffer(allocator, indexCapacity, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0, indices);
        createBuffer(allocator, stagingCapacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, staging);
        check(vmaCopyMemoryToAllocation(allocator, geometry.vertices.data(), staging.allocation, 0, vertexBytes),
            "Stage vertex data");
        check(vmaCopyMemoryToAllocation(allocator, geometry.indices.data(), staging.allocation, vertexBytes, indexBytes),
            "Stage index data");

        VkCommandBufferBeginInfo uploadBegin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        uploadBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(commands[0], &uploadBegin), "Begin geometry upload");
        const VkBufferCopy vertexCopy{0, 0, vertexBytes};
        const VkBufferCopy indexCopy{vertexBytes, 0, indexBytes};
        vkCmdCopyBuffer(commands[0], staging.handle, vertices.handle, 1, &vertexCopy);
        vkCmdCopyBuffer(commands[0], staging.handle, indices.handle, 1, &indexCopy);
        VkMemoryBarrier2 uploadBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        uploadBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        uploadBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        uploadBarrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        uploadBarrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
        VkDependencyInfo uploadDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        uploadDependency.memoryBarrierCount = 1;
        uploadDependency.pMemoryBarriers = &uploadBarrier;
        vkCmdPipelineBarrier2(commands[0], &uploadDependency);
        check(vkEndCommandBuffer(commands[0]), "End geometry upload");
        VkSubmitInfo uploadSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        uploadSubmit.commandBufferCount = 1;
        uploadSubmit.pCommandBuffers = commands;
        check(vkResetFences(device, 1, frameFence), "Reset upload fence");
        check(vkQueueSubmit(graphicsQueue, 1, &uploadSubmit, frameFence[0]), "Submit geometry upload");

        std::size_t frame = 0;
        int lastWidth = 0;
        int lastHeight = 0;
        bool recreate = true;
        std::string windowTitle;
        bool mouseCaptured = false;
        double lastCursorX = 0, lastCursorY = 0;
        bool lastMouseButton = false;
        std::cout << "Vulkan pixel art preview\n";
        while (!editor.quitApproved()) {
            glfwPollEvents();
            if (std::chrono::steady_clock::now() - lastWatch > std::chrono::seconds(1)) {
                lastWatch = std::chrono::steady_clock::now();
                const bool changed = std::any_of(watched.begin(), watched.end(),
                    [&](const auto& entry) { return fileTime(entry.first) != entry.second; });
                if (changed) loadMaterials(false);
            }
            // Cerrar la ventana pasa por el editor: si hay cambios sin guardar pregunta antes.
            if (glfwWindowShouldClose(window)) {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                editor.requestQuit();
                if (editor.quitApproved()) break;
            }

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            if (width == 0 || height == 0 || glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
                glfwWaitEvents();
                continue;
            }
            if (recreate || width != lastWidth || height != lastHeight) {
                if (!createSwapchain(width, height)) {
                    glfwWaitEventsTimeout(0.05);
                    continue;
                }
                lastWidth = width;
                lastHeight = height;
                recreate = false;
            }

            check(vkWaitForFences(device, 1, &frameFence[frame], VK_TRUE, UINT64_MAX), "Wait frame fence");
            std::uint32_t imageIndex = 0;
            const auto acquired = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                imageReady[frame], VK_NULL_HANDLE, &imageIndex);
            if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
                recreate = true;
                continue;
            }
            if (acquired != VK_SUBOPTIMAL_KHR) check(acquired, "Acquire swapchain image");

            // La vista pidió en el fotograma anterior una resolución mayor que las imágenes: se agrandan
            // antes de empezar la interfaz (que guarda referencias a sus texturas).
            if (const auto wanted = editor.requestedSceneExtent();
                wanted.x > sceneCapacity.width || wanted.y > sceneCapacity.height) {
                createSceneTargets({wanted.x, wanted.y});
                editorLog().add(LogLevel::info, "Vista: imagenes de escena de " + std::to_string(sceneCapacity.width) + " x " +
                                                    std::to_string(sceneCapacity.height));
            }
            editor.setSceneCapacity({sceneCapacity.width, sceneCapacity.height});

            // Jugador en primera persona: el cursor desaparece y su movimiento se lee directamente de GLFW
            // (la interfaz deja de recibir el ratón mientras tanto).
            if (!glfwGetWindowAttrib(window, GLFW_FOCUSED)) editor.releaseMouse();
            const bool wantCapture = editor.wantsMouseCapture();
            if (wantCapture != mouseCaptured) {
                mouseCaptured = wantCapture;
                glfwSetInputMode(window, GLFW_CURSOR, mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
                if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, mouseCaptured ? GLFW_TRUE : GLFW_FALSE);
                glfwGetCursorPos(window, &lastCursorX, &lastCursorY);
                lastMouseButton = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                auto& flags = ImGui::GetIO().ConfigFlags;
                flags = mouseCaptured ? (flags | ImGuiConfigFlags_NoMouse) : (flags & ~ImGuiConfigFlags_NoMouse);
            }
            if (mouseCaptured) {
                double x = 0, y = 0;
                glfwGetCursorPos(window, &x, &y);
                const bool button = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                editor.setCapturedMouse(static_cast<float>(x - lastCursorX), static_cast<float>(y - lastCursorY), button && !lastMouseButton);
                lastCursorX = x;
                lastCursorY = y;
                lastMouseButton = button;
            }

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGuizmo::BeginFrame();
            editor.draw(reinterpret_cast<ImTextureID>(sceneTextures[frame]));
            ImGui::Render();
            if (auto title = editor.windowTitle(); title != windowTitle) {
                windowTitle = std::move(title);
                glfwSetWindowTitle(window, windowTitle.c_str());
            }

            const auto command = commands[frame];
            const auto& color = sceneColors[frame];
            const auto& normals = sceneNormals[frame];
            const auto& depth = sceneDepths[frame];
            const auto& finalColor = finalColors[frame];
            const auto sceneView = editor.sceneView();
            const auto& style = editor.renderStyle();
            const VkExtent2D sceneExtent{std::min(sceneView.extent.x, sceneCapacity.width),
                                         std::min(sceneView.extent.y, sceneCapacity.height)};
            const auto destination = images[imageIndex];
            check(vkResetCommandBuffer(command, 0), "Reset command buffer");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            check(vkBeginCommandBuffer(command, &begin), "Begin command buffer");

            // Terreno: remallado con presupuesto y copias a la GPU (fuera de la pasada de render).
            ++frameNumber;
            const std::uint64_t completedFrame = frameNumber > 2 ? frameNumber - 2 : 0;
            terrainRenderer.update(command, static_cast<int>(frame), frameNumber, completedFrame, editor.scene(),
                [&](const std::string& id) { return textures.gpuIndex(id); }, materialsVersion);

            // Datos del fotograma: cámara, sol y tabla de materiales.
            const auto camera = sceneView.viewProj;
            FrameData frameData;
            frameData.viewProj = camera;
            frameData.sun = glm::vec4(glm::normalize(glm::vec3{0.45f, 0.85f, 0.35f}), 0.0f);
            frameData.style = {static_cast<float>(style.lightBands), static_cast<float>(std::max(1, textures.textureSize())),
                               static_cast<float>(std::fmod(glfwGetTime(), 3600.0)), 0.0f};
            frameData.camera = glm::vec4(editor.cameraPosition(), 1.0f);
            frameData.materials = textures.table();
            std::memcpy(frameUniforms[frame].mapped, &frameData, sizeof(frameData));
            check(vmaFlushAllocation(allocator, frameUniforms[frame].allocation, 0, VK_WHOLE_SIZE), "Flush frame data");

            // Las imágenes de la escena vienen de ser leídas por el post del fotograma anterior en este slot.
            for (const auto* target : {&color, &normals}) {
                transition(command, target->image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            }
            transition(command, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo colorAttachments[2]{};
            colorAttachments[0] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachments[0].imageView = color.view;
            colorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachments[0].clearValue.color = {{0.19f, 0.21f, 0.24f, 1.0f}};
            colorAttachments[1] = colorAttachments[0];
            colorAttachments[1].imageView = normals.view;
            colorAttachments[1].clearValue.color = {{0.5f, 1.0f, 0.5f, 0.0f}};
            VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depthAttachment.imageView = depth.view;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil.depth = 1.0f;

            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            rendering.renderArea = {{0, 0}, sceneExtent};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 2;
            rendering.pColorAttachments = colorAttachments;
            rendering.pDepthAttachment = &depthAttachment;
            vkCmdBeginRendering(command, &rendering);
            const VkViewport viewport{0, 0, static_cast<float>(sceneExtent.width),
                static_cast<float>(sceneExtent.height), 0, 1};
            const VkRect2D scissor{{0, 0}, sceneExtent};
            vkCmdSetViewport(command, 0, 1, &viewport);
            vkCmdSetScissor(command, 0, 1, &scissor);

            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sceneSets[frame], 0, nullptr);
            const VkDeviceSize vertexOffset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &vertices.handle, &vertexOffset);
            vkCmdBindIndexBuffer(command, indices.handle, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, cubePipeline);
            for (const auto& entity : editor.scene().entities()) {
                if (!entity.shape || !entity.active) continue;
                ScenePush push;
                push.model = EditorView::modelMatrix(entity.transform);
                push.grid.w = modePiece;
                push.data = packFaceMaterials(*entity.shape, textures);
                const auto mesh = entity.shape->primitive == ge3d::Primitive::box ? geometry.box : geometry.ramp;
                vkCmdPushConstants(command, pipelineLayout, pushStages, 0, sizeof(push), &push);
                vkCmdDrawIndexed(command, mesh.count, 1, mesh.firstIndex, 0, 0);
            }
            // Terreno de vóxeles: chunks visibles desde la cámara.
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipeline);
            terrainRenderer.draw(command, pipelineLayout, camera, editor.scene(),
                                 [](const ge3d::Transform& transform) { return EditorView::modelMatrix(transform); });
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, heightPipeline);
            terrainRenderer.drawHeightmaps(command, pipelineLayout, camera, editor.scene(),
                                           [](const ge3d::Transform& transform) { return EditorView::modelMatrix(transform); },
                                           [&](const std::string& id) { return textures.gpuIndex(id); });
            // Agua después de todo lo opaco: su tramado deja ver el fondo ya dibujado.
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline);
            terrainRenderer.drawWater(command, pipelineLayout, camera, editor.scene(),
                                      [](const ge3d::Transform& transform) { return EditorView::modelMatrix(transform); },
                                      [&](const std::string& id) { return textures.gpuIndex(id); });
            const auto& terrainStats = terrainRenderer.stats();
            editor.setTerrainStats({terrainStats.terrains, terrainStats.chunks, terrainStats.visibleChunks,
                                    terrainStats.pendingChunks, terrainStats.quads, terrainStats.visibleQuads,
                                    static_cast<std::size_t>(terrainStats.gpuBytes)});
            vkCmdBindVertexBuffers(command, 0, 1, &vertices.handle, &vertexOffset);
            if (editor.showGrid()) {
                // Cuadrícula "infinita": se recoloca bajo el punto que mira la cámara, ajustada a su
                // separación, y se desvanece con tramado antes de llegar al borde de la malla base.
                const auto grid = editor.gridView();
                const float limit = static_cast<float>(gridHalfLines) * 0.95f;
                ScenePush push;
                push.model = glm::translate(glm::mat4{1.0f}, {grid.offset.x, 0.01f, grid.offset.y}) *
                             glm::scale(glm::mat4{1.0f}, {grid.spacing, 1.0f, grid.spacing});
                push.fade = {(grid.focus - grid.offset) / grid.spacing,
                             std::min(grid.fadeRadius / grid.spacing, limit), 0.0f};
                push.grid = {grid.offset.x, grid.offset.y, grid.spacing, modeGrid};
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, gridPipeline);
                vkCmdPushConstants(command, pipelineLayout, pushStages, 0, sizeof(push), &push);
                vkCmdDraw(command, geometry.gridCount, 1, geometry.gridFirst, 0);
            }
            vkCmdEndRendering(command);

            // --- Pasada de estilo pixel art: contornos, aristas y reducción de colores -------------
            for (const auto* target : {&color, &normals}) {
                transition(command, target->image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }
            transition(command, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            transition(command, finalColor.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            VkRenderingAttachmentInfo postAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            postAttachment.imageView = finalColor.view;
            postAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            postAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            postAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfo postRendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            postRendering.renderArea = {{0, 0}, sceneExtent};
            postRendering.layerCount = 1;
            postRendering.colorAttachmentCount = 1;
            postRendering.pColorAttachments = &postAttachment;
            vkCmdBeginRendering(command, &postRendering);
            vkCmdSetViewport(command, 0, 1, &viewport);
            vkCmdSetScissor(command, 0, 1, &scissor);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout, 0, 1, &postSets[frame], 0, nullptr);
            PostPush postPush;
            postPush.outline = {style.outlines ? style.outlineDarken : 0.0f, style.outlines ? style.edgeLighten : 0.0f,
                                style.outlineThreshold, style.edgeThreshold};
            postPush.camera = {sceneView.nearPlane, sceneView.farPlane, static_cast<float>(style.colorLevels), style.colorDither};
            postPush.extent = {static_cast<float>(sceneExtent.width), static_cast<float>(sceneExtent.height), 0.0f, 0.0f};
            vkCmdPushConstants(command, postLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(postPush), &postPush);
            vkCmdDraw(command, 3, 1, 0, 0);
            vkCmdEndRendering(command);

            transition(command, finalColor.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            transition(command, destination, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo uiAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            uiAttachment.imageView = swapViews[imageIndex];
            uiAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            uiAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            uiAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            uiAttachment.clearValue.color = {{0.015f, 0.022f, 0.033f, 1.0f}};
            VkRenderingInfo uiRendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            uiRendering.renderArea = {{0, 0}, extent};
            uiRendering.layerCount = 1;
            uiRendering.colorAttachmentCount = 1;
            uiRendering.pColorAttachments = &uiAttachment;
            vkCmdBeginRendering(command, &uiRendering);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
            vkCmdEndRendering(command);
            transition(command, destination, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE, 0);
            check(vkEndCommandBuffer(command), "End command buffer");

            const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &imageReady[frame];
            submit.pWaitDstStageMask = &waitStage;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &renderDone[imageIndex];
            check(vkResetFences(device, 1, &frameFence[frame]), "Reset frame fence");
            check(vkQueueSubmit(graphicsQueue, 1, &submit, frameFence[frame]), "Submit frame");

            VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores = &renderDone[imageIndex];
            present.swapchainCount = 1;
            present.pSwapchains = &swapchain;
            present.pImageIndices = &imageIndex;
            const auto presented = vkQueuePresentKHR(presentQueue, &present);
            if (presented != VK_ERROR_OUT_OF_DATE_KHR && presented != VK_SUBOPTIMAL_KHR) {
                check(presented, "Present frame");
            }
            recreate = presented != VK_SUCCESS || acquired == VK_SUBOPTIMAL_KHR;
            frame = (frame + 1) % 2;
        }
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
}

}

// Uso: ge3d_vulkan_editor [escena.scene.json]
int main(int argc, char** argv) {
    std::filesystem::path scene;
    if (argc > 1) scene = std::filesystem::absolute(ge3d::pathFromUtf8(argv[1]));
    try { run(scene); }
    catch (const std::exception& error) { std::cerr << "Vulkan editor: " << error.what() << '\n'; return 1; }
}
