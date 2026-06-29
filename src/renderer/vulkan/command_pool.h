#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;
class VulkanPhysicalDevice;

class VulkanCommandPool {
public:
    VulkanCommandPool(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice
    );

    ~VulkanCommandPool();

    SE_DISABLE_COPY(VulkanCommandPool);
    SE_DISABLE_MOVE(VulkanCommandPool);

    VkCommandPool Handle() const;

private:
    void CreateCommandPool(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice
    );

    void Cleanup();

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
};

}
