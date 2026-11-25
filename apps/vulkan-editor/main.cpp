#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
#include "EditorLog.hpp"
#include "EditorView.hpp"
#include "ge3d/Primitives.hpp"
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


constexpr VkExtent2D sceneExtent{320, 180};
constexpr VkFormat sceneFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

struct RenderImage {
    VkImage image{};
    VkImageView view{};
    VmaAllocation allocation{};
};

void createRenderImage(VkDevice device, VmaAllocator allocator, VkFormat format,
                       VkImageUsageFlags usage, VkImageAspectFlags aspect, RenderImage& target) {
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {sceneExtent.width, sceneExtent.height, 1};
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


struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

// Debe coincidir con ScenePush en shaders/scene.slang.
struct ScenePush {
    glm::mat4 transform{1.0f};
    glm::vec4 fade{0.0f};
    glm::vec4 grid{0.0f};
};
static_assert(sizeof(ScenePush) == 96, "ScenePush debe ocupar 96 bytes (limite garantizado: 128)");
constexpr VkShaderStageFlags pushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

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
    const auto append = [&](ge3d::Primitive primitive, glm::vec3 base) {
        auto mesh = ge3d::makePrimitive(primitive);
        MeshRange range{static_cast<std::uint32_t>(geometry.indices.size()), 0};
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> faceVertices;
        constexpr float light[] = {0.48f, 1.0f, 0.57f, 0.82f, 0.70f, 0.88f};
        for (const auto& triangle : mesh.triangles) {
            for (const auto index : triangle.indices) {
                const auto [it, inserted] = faceVertices.try_emplace(
                    std::pair{triangle.face, index}, static_cast<std::uint32_t>(geometry.vertices.size()));
                if (inserted) {
                    const auto position = mesh.positions[index];
                    geometry.vertices.push_back({{position.x, position.y, position.z}, base * light[triangle.face]});
                }
                geometry.indices.push_back(it->second);
            }
        }
        range.count = static_cast<std::uint32_t>(geometry.indices.size()) - range.firstIndex;
        return range;
    };
    geometry.box = append(ge3d::Primitive::box, {0.92f, 0.62f, 0.31f});
    geometry.ramp = append(ge3d::Primitive::ramp, {0.40f, 0.71f, 0.66f});
    geometry.gridFirst = static_cast<std::uint32_t>(geometry.vertices.size());
    const auto line = [&](glm::vec3 a, glm::vec3 b, glm::vec3 color) {
        geometry.vertices.push_back({a, color});
        geometry.vertices.push_back({b, color});
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

VkPipeline createScenePipeline(VkDevice device, VkFormat colorFormat, VkPipelineLayout& layout,
                               VkPrimitiveTopology topology) {
    VkShaderModule vertex = createShaderModule(device, "scene.vertex.spv");
    VkShaderModule fragment = VK_NULL_HANDLE;
    try {
        fragment = createShaderModule(device, "scene.fragment.spv");
        VkPushConstantRange push{};
        push.stageFlags = pushStages;
        push.offset = 0;
        push.size = sizeof(ScenePush);
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &push;
        if (!layout) check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout), "Create pipeline layout");

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
        const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attributes[] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}
        };
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 2;
        vertexInput.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = topology;
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
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkFormat colorFormats[] = {colorFormat};
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = colorFormats;
        rendering.depthAttachmentFormat = depthFormat;
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

void run() {
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
    RenderImage sceneDepths[2]{};
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
            if (uiRenderer) ImGui_ImplVulkan_Shutdown();
            if (sceneSampler) vkDestroySampler(device, sceneSampler, nullptr);
            for (const auto& buffer : {vertices, indices, staging}) {
                if (buffer.handle) vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
            }
            if (gridPipeline) vkDestroyPipeline(device, gridPipeline, nullptr);
            if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
            destroySwapchain();
            if (cubePipeline) vkDestroyPipeline(device, cubePipeline, nullptr);
            if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            for (int i = 0; i < 2; ++i) {
                for (const auto* target : {&sceneColors[i], &sceneDepths[i]}) {
                    if (target->view) vkDestroyImageView(device, target->view, nullptr);
                    if (target->image) vmaDestroyImage(allocator, target->image, target->allocation);
                }
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
            VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features2.pNext = &features;
            vkGetPhysicalDeviceFeatures2(candidate, &features2);
            const auto candidateQueues = findQueues(candidate, surface);
            const auto support = querySwapchain(candidate, surface);
            if (features.dynamicRendering && features.synchronization2 && candidateQueues.complete() &&
                !support.formats.empty() && !support.modes.empty() &&
                (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
                supportsFormat(candidate, sceneFormat, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) &&
                supportsFormat(candidate, depthFormat, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
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
        for (int i = 0; i < 2; ++i) {
            createRenderImage(device, allocator, sceneFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, sceneColors[i]);
            createRenderImage(device, allocator, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT, sceneDepths[i]);
        }
        cubePipeline = createScenePipeline(device, sceneFormat, pipelineLayout, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        gridPipeline = createScenePipeline(device, sceneFormat, pipelineLayout, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        const auto geometry = makeGeometry();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        uiContext = true;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::GetIO().IniFilename = nullptr;
        float contentScaleX = 1.0f;
        float contentScaleY = 1.0f;
        glfwGetWindowContentScale(window, &contentScaleX, &contentScaleY);
        EditorView editor(contentScaleX);
        uiPlatform = ImGui_ImplGlfw_InitForVulkan(window, true);
        if (!uiPlatform) throw std::runtime_error("Initialize GLFW editor backend");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = VK_FILTER_NEAREST;
        sampler.minFilter = VK_FILTER_NEAREST;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(device, &sampler, nullptr, &sceneSampler), "Create pixel art sampler");
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
                info.DescriptorPoolSize = 8;
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
                    sceneTextures[i] = ImGui_ImplVulkan_AddTexture(sceneSampler, sceneColors[i].view,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
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
        std::cout << "Vulkan pixel art preview: 320 x 180\n";
        while (!editor.quitApproved()) {
            glfwPollEvents();
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
            const auto& depth = sceneDepths[frame];
            const auto destination = images[imageIndex];
            check(vkResetCommandBuffer(command, 0), "Reset command buffer");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            check(vkBeginCommandBuffer(command, &begin), "Begin command buffer");
            transition(command, color.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            transition(command, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE, 0,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachment.imageView = color.view;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue.color = {{0.19f, 0.21f, 0.24f, 1.0f}};
            VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depthAttachment.imageView = depth.view;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.clearValue.depthStencil.depth = 1.0f;

            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            rendering.renderArea = {{0, 0}, sceneExtent};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &colorAttachment;
            rendering.pDepthAttachment = &depthAttachment;
            vkCmdBeginRendering(command, &rendering);
            const VkViewport viewport{0, 0, static_cast<float>(sceneExtent.width),
                static_cast<float>(sceneExtent.height), 0, 1};
            const VkRect2D scissor{{0, 0}, sceneExtent};
            vkCmdSetViewport(command, 0, 1, &viewport);
            vkCmdSetScissor(command, 0, 1, &scissor);

            const auto camera = editor.renderCamera();
            const VkDeviceSize vertexOffset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &vertices.handle, &vertexOffset);
            vkCmdBindIndexBuffer(command, indices.handle, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, cubePipeline);
            for (const auto& entity : editor.scene().entities()) {
                ScenePush push;
                push.transform = camera * EditorView::modelMatrix(entity.transform);
                const auto mesh = entity.primitive == ge3d::Primitive::box ? geometry.box : geometry.ramp;
                vkCmdPushConstants(command, pipelineLayout, pushStages, 0, sizeof(push), &push);
                vkCmdDrawIndexed(command, mesh.count, 1, mesh.firstIndex, 0, 0);
            }
            if (editor.showGrid()) {
                // Cuadrícula "infinita": se recoloca bajo el punto que mira la cámara, ajustada a su
                // separación, y se desvanece con tramado antes de llegar al borde de la malla base.
                const auto grid = editor.gridView();
                const float limit = static_cast<float>(gridHalfLines) * 0.95f;
                ScenePush push;
                push.transform = camera *
                    glm::translate(glm::mat4{1.0f}, {grid.offset.x, 0.01f, grid.offset.y}) *
                    glm::scale(glm::mat4{1.0f}, {grid.spacing, 1.0f, grid.spacing});
                push.fade = {(grid.focus - grid.offset) / grid.spacing,
                             std::min(grid.fadeRadius / grid.spacing, limit), 0.0f};
                push.grid = {grid.offset.x, grid.offset.y, grid.spacing, 1.0f};
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, gridPipeline);
                vkCmdPushConstants(command, pipelineLayout, pushStages, 0, sizeof(push), &push);
                vkCmdDraw(command, geometry.gridCount, 1, geometry.gridFirst, 0);
            }
            vkCmdEndRendering(command);

            transition(command, color.image, VK_IMAGE_ASPECT_COLOR_BIT,
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

int main() {
    try { run(); }
    catch (const std::exception& error) { std::cerr << "Vulkan editor: " << error.what() << '\n'; return 1; }
}
