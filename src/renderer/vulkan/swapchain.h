#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class Window;
class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanSurface;

class VulkanSwapchain {
public:
    VulkanSwapchain(
        const Window& window,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanDevice& device,
        const VulkanSurface& surface
    );

    ~VulkanSwapchain();

    SE_DISABLE_COPY(VulkanSwapchain);
    SE_DISABLE_MOVE(VulkanSwapchain);

    VkSwapchainKHR Handle() const;
    VkFormat ImageFormat() const;
    VkExtent2D Extent() const;
    bool TransferSourceSupported() const;

    const std::vector<VkImage>& Images() const;
    const std::vector<VkImageView>& ImageViews() const;

    void Recreate(
        const Window& window,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanDevice& device,
        const VulkanSurface& surface
    );
    void Release();

private:
    void CreateSwapchain(
        const Window& window,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanDevice& device,
        const VulkanSurface& surface
    );

    void CreateImageViews(const VulkanDevice& device);
private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;

    std::vector<VkImage> m_Images;
    std::vector<VkImageView> m_ImageViews;

    VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_Extent{};
    bool m_TransferSourceSupported = false;
};

}
