#pragma once

#include "renderer/vulkan/image.h"
#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanUploadBatch;

struct VulkanTexturePixels {
    std::span<const u8> rgba;
    u32 width = 0;
    u32 height = 0;
};

struct VulkanEncodedTextureBytes {
    std::span<const u8> bytes;
};

class VulkanTexture2D {
public:
    VulkanTexture2D(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string path,
        bool srgb = true,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    VulkanTexture2D(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes encoded,
        bool srgb = true,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    VulkanTexture2D(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels pixels,
        bool srgb = true,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );

    ~VulkanTexture2D();

    SE_DISABLE_COPY(VulkanTexture2D);
    SE_DISABLE_MOVE(VulkanTexture2D);

    VkImageView View() const;
    VkImageLayout Layout() const;
    VkExtent2D Extent() const;
    u32 MipLevels() const;

private:
    void CreateTextureImage(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        const std::string& path,
        bool srgb,
        bool generateMipmaps,
        bool flipVertically,
        VulkanUploadBatch* uploadBatch
    );
    void CreateTextureImage(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes encoded,
        bool srgb,
        bool generateMipmaps,
        bool flipVertically,
        VulkanUploadBatch* uploadBatch
    );
    void CreateTextureImage(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels pixels,
        bool srgb,
        bool generateMipmaps,
        bool flipVertically,
        VulkanUploadBatch* uploadBatch
    );

private:
    VulkanImage m_Image;
    VkImageLayout m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

}
