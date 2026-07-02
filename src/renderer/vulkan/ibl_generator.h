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

constexpr u32 kIblBrdfLutSize = 128;
constexpr u32 kIblIrradianceFaceSize = 32;
constexpr u32 kIblPrefilteredFaceSize = 256;
constexpr u32 kIblPrefilteredMipCount = 5;
constexpr VkFormat kIblBrdfLutFormat = VK_FORMAT_R16G16_SFLOAT;
constexpr VkFormat kIblEnvironmentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr u32 kReflectionProbeCubemapFaceSize = 64;
constexpr u32 kReflectionProbeCubemapMipCount = 5;
constexpr VkFormat kReflectionProbeCubemapFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

void GenerateIblTextures(const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice, const VulkanCommandPool& commandPool,
    std::unique_ptr<VulkanImage>& brdfImage, std::unique_ptr<VulkanImage>& irradianceImage,
    std::unique_ptr<VulkanImage>& prefilteredImage,
    VkImageView& irradianceView, VkImageView& prefilteredView, VkSampler& sampler);

void GenerateReflectionProbeCubemap(const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice, const VulkanCommandPool& commandPool,
    std::unique_ptr<VulkanImage>& cubemapImage, VkImageView& cubemapView);
}
