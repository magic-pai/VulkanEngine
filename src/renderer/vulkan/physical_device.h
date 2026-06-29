#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanInstance;
class VulkanSurface;

struct QueueFamilyIndices {
    std::optional<u32> graphicsFamily;
    std::optional<u32> presentFamily;

    bool IsComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanPhysicalDevice {
public:
    VulkanPhysicalDevice(const VulkanInstance& instance, const VulkanSurface& surface);

    SE_DISABLE_COPY(VulkanPhysicalDevice);
    SE_DISABLE_MOVE(VulkanPhysicalDevice);

    VkPhysicalDevice Handle() const;
    const QueueFamilyIndices& QueueFamilies() const;
    const SwapchainSupportDetails& SwapchainSupport() const;
    const VkPhysicalDeviceProperties& Properties() const;
    const VkPhysicalDeviceFeatures& Features() const;
    SwapchainSupportDetails QuerySwapchainSupport(const VulkanSurface& surface) const;
    u32 FindMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) const;
    bool SupportsLinearBlit(VkFormat format) const;

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    QueueFamilyIndices m_QueueFamilies;
    SwapchainSupportDetails m_SwapchainSupport;
    VkPhysicalDeviceProperties m_Properties{};
    VkPhysicalDeviceFeatures m_Features{};
};

}
