#include "renderer/vulkan/sampler.h"

#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

namespace se {

VulkanSampler::VulkanSampler(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    u32 mipLevels
)
    : m_Device(device.Handle()) {
    CreateSampler(device, physicalDevice, mipLevels);
}

VulkanSampler::~VulkanSampler() {
    Release();
}

VkSampler VulkanSampler::Handle() const {
    return m_Sampler;
}

void VulkanSampler::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    u32 mipLevels
) {
    Release();
    m_Device = device.Handle();
    CreateSampler(device, physicalDevice, mipLevels);
}

void VulkanSampler::Release() {
    if (m_Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device, m_Sampler, nullptr);
        m_Sampler = VK_NULL_HANDLE;
    }
}

void VulkanSampler::CreateSampler(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    u32 mipLevels
) {
    SE_ASSERT(mipLevels > 0, "Sampler mip levels must be greater than zero");

    const VkPhysicalDeviceFeatures& features = physicalDevice.Features();
    const VkPhysicalDeviceProperties& properties = physicalDevice.Properties();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = features.samplerAnisotropy ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = features.samplerAnisotropy
        ? properties.limits.maxSamplerAnisotropy
        : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<f32>(mipLevels - 1);

    if (vkCreateSampler(device.Handle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan texture sampler");
    }
}

}
