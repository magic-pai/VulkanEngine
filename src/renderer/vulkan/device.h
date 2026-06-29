#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanPhysicalDevice;

class VulkanDevice {
public:
    VulkanDevice(const VulkanPhysicalDevice& physicalDevice);
    ~VulkanDevice();

    SE_DISABLE_COPY(VulkanDevice);
    SE_DISABLE_MOVE(VulkanDevice);

    VkDevice Handle() const;
    VkQueue GraphicsQueue() const;
    VkQueue PresentQueue() const;

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_PresentQueue = VK_NULL_HANDLE;
};

}