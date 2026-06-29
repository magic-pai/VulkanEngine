#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanCommandPool;
class VulkanUploadBatch;

class VulkanImage {
public:
    VulkanImage() = default;
    VulkanImage(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        VkExtent2D extent,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags memoryProperties,
        VkImageAspectFlags aspectFlags,
        u32 mipLevels = 1,
        u32 arrayLayers = 1,
        VkImageCreateFlags imageFlags = 0,
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D
    );

    ~VulkanImage();

    SE_DISABLE_COPY(VulkanImage);
    SE_DISABLE_MOVE(VulkanImage);

    VkImage Handle() const;
    VkImageView View() const;
    VkFormat Format() const;
    VkExtent2D Extent() const;
    u32 MipLevels() const;

    void TransitionLayout(
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        u32 mipLevels = 1,
        VulkanUploadBatch* uploadBatch = nullptr,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
    ) const;
    void CopyFromBuffer(
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool,
        VkBuffer sourceBuffer,
        u32 arrayLayers,
        VulkanUploadBatch* uploadBatch = nullptr
    ) const;
    void CopyFromBuffer(
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool,
        VkBuffer sourceBuffer,
        VulkanUploadBatch* uploadBatch = nullptr
    ) const;
    void GenerateMipmaps(
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool,
        VulkanUploadBatch* uploadBatch = nullptr
    ) const;
    void GenerateMipmaps(
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool,
        u32 arrayLayers,
        VulkanUploadBatch* uploadBatch = nullptr
    ) const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        VkExtent2D extent,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags memoryProperties,
        VkImageAspectFlags aspectFlags,
        u32 mipLevels = 1,
        u32 arrayLayers = 1,
        VkImageCreateFlags imageFlags = 0,
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D
    );
    void Release();

private:
    void CreateImage(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        VkExtent2D extent,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags memoryProperties,
        u32 mipLevels,
        u32 arrayLayers,
        VkImageCreateFlags imageFlags
    );
    void CreateImageView(
        const VulkanDevice& device,
        VkFormat format,
        VkImageAspectFlags aspectFlags,
        u32 mipLevels,
        u32 arrayLayers,
        VkImageViewType viewType
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_Extent{};
    u32 m_MipLevels = 1;
    u32 m_ArrayLayers = 1;
};

}
