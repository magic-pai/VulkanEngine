#pragma once

#include "renderer/vulkan/image.h"
#include "renderer/vulkan/sampler.h"

namespace se {

class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanCommandPool;

class VulkanShadowMap {
public:
    static constexpr u32 kSize = 2048;

    VulkanShadowMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::size_t imageCount = 1,
        u32 size = kSize
    );

    ~VulkanShadowMap();

    SE_DISABLE_COPY(VulkanShadowMap);
    SE_DISABLE_MOVE(VulkanShadowMap);

    VkExtent2D Extent() const;
    VkFormat Format() const;
    VkImageView View() const;
    VkImageView View(std::size_t index) const;
    VkSampler Sampler() const;
    VkSampler RawDepthSampler() const;
    VkImageLayout Layout() const;
    std::size_t Count() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::size_t imageCount,
        u32 size = kSize
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
    VkSampler m_RawDepthSampler = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_Extent{ kSize, kSize };
};

}
