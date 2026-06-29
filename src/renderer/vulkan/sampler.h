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
        u32 mipLevels
    );
    ~VulkanSampler();

    SE_DISABLE_COPY(VulkanSampler);
    SE_DISABLE_MOVE(VulkanSampler);

    VkSampler Handle() const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        u32 mipLevels
    );
    void Release();

private:
    void CreateSampler(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        u32 mipLevels
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
};

}
