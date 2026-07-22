#include "renderer/vulkan/swapchain.h"

#include "platform/window.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/surface.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>

namespace se {

namespace {

VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const VkSurfaceFormatKHR& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats.front();
}

std::string ReadSwapchainEnvironmentString(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
        return {};
    }

    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

std::string LowerAscii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text;
}

bool PresentModeAvailable(
    const std::vector<VkPresentModeKHR>& availablePresentModes,
    VkPresentModeKHR requestedMode
) {
    return std::find(
        availablePresentModes.begin(),
        availablePresentModes.end(),
        requestedMode
    ) != availablePresentModes.end();
}

VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    const std::string requestedMode =
        LowerAscii(ReadSwapchainEnvironmentString("SE_SWAPCHAIN_PRESENT_MODE"));
    if ((requestedMode == "fifo" || requestedMode == "vsync") &&
        PresentModeAvailable(availablePresentModes, VK_PRESENT_MODE_FIFO_KHR)) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    if ((requestedMode == "immediate" || requestedMode == "no-vsync") &&
        PresentModeAvailable(availablePresentModes, VK_PRESENT_MODE_IMMEDIATE_KHR)) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    if (requestedMode == "mailbox" &&
        PresentModeAvailable(availablePresentModes, VK_PRESENT_MODE_MAILBOX_KHR)) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }

    for (VkPresentModeKHR availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, const Window& window) {
    if (capabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent{
        static_cast<u32>(window.GetWidth()),
        static_cast<u32>(window.GetHeight())
    };

    actualExtent.width = std::clamp(
        actualExtent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
    );

    actualExtent.height = std::clamp(
        actualExtent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
    );

    return actualExtent;
}

}

VulkanSwapchain::VulkanSwapchain(
    const Window& window,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanDevice& device,
    const VulkanSurface& surface
) : m_Device(device.Handle()) {
    CreateSwapchain(window, physicalDevice, device, surface);
    CreateImageViews(device);
}

VulkanSwapchain::~VulkanSwapchain() {
    Release();
}

VkSwapchainKHR VulkanSwapchain::Handle() const {
    return m_Swapchain;
}

VkFormat VulkanSwapchain::ImageFormat() const {
    return m_ImageFormat;
}

VkExtent2D VulkanSwapchain::Extent() const {
    return m_Extent;
}

bool VulkanSwapchain::TransferSourceSupported() const {
    return m_TransferSourceSupported;
}

const std::vector<VkImage>& VulkanSwapchain::Images() const {
    return m_Images;
}

const std::vector<VkImageView>& VulkanSwapchain::ImageViews() const {
    return m_ImageViews;
}

void VulkanSwapchain::Recreate(
    const Window& window,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanDevice& device,
    const VulkanSurface& surface
) {
    Release();
    CreateSwapchain(window, physicalDevice, device, surface);
    CreateImageViews(device);
}

void VulkanSwapchain::CreateSwapchain(
    const Window& window,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanDevice& device,
    const VulkanSurface& surface
) {
    const SwapchainSupportDetails swapchainSupport = physicalDevice.QuerySwapchainSupport(surface);

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(swapchainSupport.formats);
    const VkPresentModeKHR presentMode = ChoosePresentMode(swapchainSupport.presentModes);
    const VkExtent2D extent = ChooseExtent(swapchainSupport.capabilities, window);

    u32 imageCount = swapchainSupport.capabilities.minImageCount + 1;
    if (swapchainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapchainSupport.capabilities.maxImageCount) {
        imageCount = swapchainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface.Handle();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    m_TransferSourceSupported =
        (swapchainSupport.capabilities.supportedUsageFlags &
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
    if (m_TransferSourceSupported) {
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    const QueueFamilyIndices& indices = physicalDevice.QueueFamilies();
    const u32 queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device.Handle(), &createInfo, nullptr, &m_Swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan swapchain");
    }

    vkGetSwapchainImagesKHR(device.Handle(), m_Swapchain, &imageCount, nullptr);
    m_Images.resize(imageCount);
    vkGetSwapchainImagesKHR(device.Handle(), m_Swapchain, &imageCount, m_Images.data());

    m_ImageFormat = surfaceFormat.format;
    m_Extent = extent;
}

void VulkanSwapchain::CreateImageViews(const VulkanDevice& device) {
    m_ImageViews.resize(m_Images.size());

    for (std::size_t index = 0; index < m_Images.size(); ++index) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_Images[index];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_ImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.Handle(), &createInfo, nullptr, &m_ImageViews[index]) != VK_SUCCESS) {
            Release();
            throw std::runtime_error("Failed to create Vulkan swapchain image view");
        }
    }
}

void VulkanSwapchain::Release() {
    for (VkImageView imageView : m_ImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, imageView, nullptr);
        }
    }
    m_ImageViews.clear();
    m_Images.clear();

    if (m_Swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
    }

    m_ImageFormat = VK_FORMAT_UNDEFINED;
    m_Extent = {};
    m_TransferSourceSupported = false;
}

}
