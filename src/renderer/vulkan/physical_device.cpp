#include "renderer/vulkan/physical_device.h"

#include "renderer/vulkan/instance.h"
#include "renderer/vulkan/surface.h"

#include <set>

namespace se {

namespace {

QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (u32 index = 0; index < queueFamilyCount; ++index) {
        const VkQueueFamilyProperties& queueFamily = queueFamilies[index];

        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = index;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = index;
        }

        if (indices.IsComplete()) {
            break;
        }
    }

    return indices;
}

bool CheckDeviceExtensionSupport(VkPhysicalDevice device) {
    u32 extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(kDeviceExtensions.begin(), kDeviceExtensions.end());

    for (const VkExtensionProperties& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

SwapchainSupportDetails QuerySwapchainSupportDetails(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    u32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    u32 presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

bool IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
    const QueueFamilyIndices indices = FindQueueFamilies(device, surface);
    const bool extensionsSupported = CheckDeviceExtensionSupport(device);

    bool swapchainAdequate = false;
    if (extensionsSupported) {
        const SwapchainSupportDetails swapchainSupport = QuerySwapchainSupportDetails(device, surface);
        swapchainAdequate =
            !swapchainSupport.formats.empty() &&
            !swapchainSupport.presentModes.empty();
    }

    return indices.IsComplete() && extensionsSupported && swapchainAdequate;
}

void PrintDeviceName(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);

    std::cout << "Selected GPU: " << properties.deviceName << std::endl;
}

}

VulkanPhysicalDevice::VulkanPhysicalDevice(const VulkanInstance& instance, const VulkanSurface& surface) {
    u32 deviceCount = 0;
    vkEnumeratePhysicalDevices(instance.Handle(), &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance.Handle(), &deviceCount, devices.data());

    for (VkPhysicalDevice device : devices) {
        if (IsDeviceSuitable(device, surface.Handle())) {
            m_PhysicalDevice = device;
            m_QueueFamilies = FindQueueFamilies(device, surface.Handle());
            m_SwapchainSupport = QuerySwapchainSupportDetails(device, surface.Handle());
            break;
        }
    }

    if (m_PhysicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU");
    }

    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &m_Properties);
    vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &m_Features);

    PrintDeviceName(m_PhysicalDevice);
}

VkPhysicalDevice VulkanPhysicalDevice::Handle() const {
    return m_PhysicalDevice;
}

const QueueFamilyIndices& VulkanPhysicalDevice::QueueFamilies() const {
    return m_QueueFamilies;
}

const SwapchainSupportDetails& VulkanPhysicalDevice::SwapchainSupport() const {
    return m_SwapchainSupport;
}

const VkPhysicalDeviceProperties& VulkanPhysicalDevice::Properties() const {
    return m_Properties;
}

const VkPhysicalDeviceFeatures& VulkanPhysicalDevice::Features() const {
    return m_Features;
}

SwapchainSupportDetails VulkanPhysicalDevice::QuerySwapchainSupport(const VulkanSurface& surface) const {
    return QuerySwapchainSupportDetails(m_PhysicalDevice, surface.Handle());
}

u32 VulkanPhysicalDevice::FindMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties);

    for (u32 index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        const bool isSupported = (typeFilter & (1 << index)) != 0;
        const bool hasProperties =
            (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;

        if (isSupported && hasProperties) {
            return index;
        }
    }

    throw std::runtime_error("Failed to find suitable Vulkan memory type");
}

bool VulkanPhysicalDevice::SupportsLinearBlit(VkFormat format) const {
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &formatProperties);

    return (formatProperties.optimalTilingFeatures &
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

}
