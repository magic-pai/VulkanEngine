#pragma once

#include "renderer/vulkan/image.h"

namespace se {

class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanSwapchain;

class VulkanDepthBuffer {
public:
    VulkanDepthBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanSwapchain& swapchain
    );

    ~VulkanDepthBuffer();

    SE_DISABLE_COPY(VulkanDepthBuffer);
    SE_DISABLE_MOVE(VulkanDepthBuffer);

    VkImage Image(std::size_t index) const;
    VkImageView View(std::size_t index) const;
    VkFormat Format() const;
    std::size_t Count() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanSwapchain& swapchain
    );
    void Release();

    static VkFormat FindDepthFormat(const VulkanPhysicalDevice& physicalDevice);

private:
    static VkFormat FindSupportedFormat(
        const VulkanPhysicalDevice& physicalDevice,
        std::span<const VkFormat> candidates,
        VkImageTiling tiling,
        VkFormatFeatureFlags features
    );

private:
    std::vector<std::unique_ptr<VulkanImage>> m_Images;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
};

}
