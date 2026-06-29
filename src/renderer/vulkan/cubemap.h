#pragma once

#include "renderer/vulkan/image.h"
#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;

class VulkanCubemap {
public:
    VulkanCubemap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string directory
    );

    ~VulkanCubemap();

    SE_DISABLE_COPY(VulkanCubemap);
    SE_DISABLE_MOVE(VulkanCubemap);

    VkImageView View() const;
    VkImageLayout Layout() const;
    VkExtent2D Extent() const;
    u32 MipLevels() const;

private:
    void CreateCubemapImage(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        const std::string& directory
    );

private:
    VulkanImage m_Image;
    VkImageLayout m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

}
