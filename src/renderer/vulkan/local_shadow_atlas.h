#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanImage;
class VulkanPhysicalDevice;

class VulkanLocalShadowAtlas {
public:
    VulkanLocalShadowAtlas(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::size_t imageCount,
        u32 tileSize,
        u32 tileCapacity
    );
    ~VulkanLocalShadowAtlas();

    SE_DISABLE_COPY(VulkanLocalShadowAtlas);
    SE_DISABLE_MOVE(VulkanLocalShadowAtlas);

    VkExtent2D Extent() const;
    VkFormat Format() const;
    VkImageView View(std::size_t index) const;
    VkSampler Sampler() const;
    VkSampler ComparisonSampler() const;
    VkImageLayout Layout() const;
    std::size_t Count() const;
    u32 TileSize() const;
    u32 TileCapacity() const;
    u32 TileColumns() const;
    u32 TileRows() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::size_t imageCount,
        u32 tileSize,
        u32 tileCapacity
    );
    void Release();

private:
    VkFormat FindDepthFormat(const VulkanPhysicalDevice& physicalDevice) const;
    void CreateSampler(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice
    );
    void InitializeImageLayouts(
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool
    ) const;

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<VulkanImage>> m_Images;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkSampler m_ComparisonSampler = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_Extent{};
    u32 m_TileSize = 0;
    u32 m_TileCapacity = 0;
    u32 m_TileColumns = 0;
    u32 m_TileRows = 0;
};

}
