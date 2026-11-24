#include <vulkan/vulkan.h>
#include <cstdint>
#include <iostream>

int main() {
    const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    std::uint32_t version = VK_API_VERSION_1_0;
    if (enumerateVersion && enumerateVersion(&version) != VK_SUCCESS) {
        std::cerr << "Cannot query Vulkan loader version\n";
        return 1;
    }
    std::cout << "Vulkan loader: " << VK_API_VERSION_MAJOR(version) << '.'
              << VK_API_VERSION_MINOR(version) << '.' << VK_API_VERSION_PATCH(version) << '\n';
    return version >= VK_API_VERSION_1_3 ? 0 : 1;
}
