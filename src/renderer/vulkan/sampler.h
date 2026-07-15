#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;
class VulkanPhysicalDevice;

class VulkanSampler {
public:
    VulkanSampler(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        u32 mipLevels,
        f32 mipLodBias = 0.0f
    );
    ~VulkanSampler();

    SE_DISABLE_COPY(VulkanSampler);
    SE_DISABLE_MOVE(VulkanSampler);

    VkSampler Handle() const;
    f32 MipLodBias() const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        u32 mipLevels,
        f32 mipLodBias = 0.0f
    );
    void Release();

private:
    void CreateSampler(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        u32 mipLevels,
        f32 mipLodBias
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    f32 m_MipLodBias = 0.0f;
};

}
