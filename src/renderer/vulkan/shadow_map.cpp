#include "renderer/vulkan/shadow_map.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/depth_buffer.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

namespace se {

VulkanShadowMap::VulkanShadowMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::size_t imageCount,
    u32 size
) : m_Device(device.Handle()) {
    Recreate(device, physicalDevice, commandPool, imageCount, size);
}

VulkanShadowMap::~VulkanShadowMap() {
    Release();
}

VkExtent2D VulkanShadowMap::Extent() const {
    return m_Extent;
}

VkFormat VulkanShadowMap::Format() const {
    return m_Format;
}

VkImageView VulkanShadowMap::View() const {
    return View(0);
}

VkImageView VulkanShadowMap::View(std::size_t index) const {
    SE_ASSERT(index < m_Images.size(), "Shadow image index is out of range");
    return m_Images[index]->View();
}

VkSampler VulkanShadowMap::Sampler() const {
    return m_Sampler;
}

VkImageLayout VulkanShadowMap::Layout() const {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
}

std::size_t VulkanShadowMap::Count() const {
    return m_Images.size();
}

void VulkanShadowMap::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::size_t imageCount,
    u32 size
) {
    SE_ASSERT(imageCount > 0, "Shadow map image count must be greater than zero");
    SE_ASSERT(size > 0, "Shadow map size must be greater than zero");

    Release();
    m_Device = device.Handle();
    m_Extent = { size, size };
    m_Format = FindDepthFormat(physicalDevice);
    m_Images.reserve(imageCount);
    for (std::size_t index = 0; index < imageCount; ++index) {
        m_Images.push_back(std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            m_Extent,
            m_Format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        ));
    }
    InitializeImageLayouts(device, commandPool);
    CreateSampler(device, physicalDevice);
}

void VulkanShadowMap::Release() {
    if (m_Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device, m_Sampler, nullptr);
        m_Sampler = VK_NULL_HANDLE;
    }
    m_Images.clear();
    m_Format = VK_FORMAT_UNDEFINED;
}

VkFormat VulkanShadowMap::FindDepthFormat(const VulkanPhysicalDevice& physicalDevice) const {
    return VulkanDepthBuffer::FindDepthFormat(physicalDevice);
}

void VulkanShadowMap::CreateSampler(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice
) {
    const VkPhysicalDeviceFeatures& features = physicalDevice.Features();
    const VkPhysicalDeviceProperties& properties = physicalDevice.Properties();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = features.samplerAnisotropy
        ? properties.limits.maxSamplerAnisotropy
        : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device.Handle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow map sampler");
    }
}

void VulkanShadowMap::InitializeImageLayouts(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool
) const {
    for (const std::unique_ptr<VulkanImage>& image : m_Images) {
        image->TransitionLayout(
            device,
            commandPool,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            1,
            nullptr,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );
    }
}

}
