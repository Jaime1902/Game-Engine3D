#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

#include <functional>
#include <stdexcept>

// Lo mínimo que comparten los módulos de GPU del editor.
struct GpuContext {
    VkDevice device{};
    VmaAllocator allocator{};
    VkQueue queue{};
    VkCommandPool commandPool{};
};

inline void vkCheck(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(operation);
}

// Ejecuta comandos una sola vez y espera a que terminen (cargas fuera del bucle de fotogramas).
void submitOnce(const GpuContext& context, const std::function<void(VkCommandBuffer)>& record);

struct GpuBuffer {
    VkBuffer handle{};
    VmaAllocation allocation{};
    void* mapped{};
};

GpuBuffer createGpuBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
void destroyGpuBuffer(VmaAllocator allocator, GpuBuffer& buffer);
