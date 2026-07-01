#pragma once
#include "renderer/vulkan/vulkan_common.h"
#include <memory>
#include <vector>

namespace se {
class VulkanImage;
class VulkanBuffer;
class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;

void GenerateIblTextures(const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice, const VulkanCommandPool& commandPool,
    std::unique_ptr<VulkanImage>& brdfImage, std::unique_ptr<VulkanImage>& irradianceImage,
    std::unique_ptr<VulkanImage>& prefilteredImage,
    VkImageView& irradianceView, VkImageView& prefilteredView, VkSampler& sampler);

void RunGpuIblPrefilter(const VulkanDevice& device, const VulkanCommandPool& commandPool,
    VkImageView sourceCubemapView,
    VkImage irradianceImage, VkImageView irradianceView,
    VkImage prefilteredImage, VkImageView prefilteredView,
    u32 faceSize, u32 irrFaceSize, u32 mipCount);
}
