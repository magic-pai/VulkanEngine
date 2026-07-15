#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <memory>

namespace se {

class VulkanPhysicalDevice;
class VulkanPipelineCache;

class VulkanDevice {
public:
    VulkanDevice(const VulkanPhysicalDevice& physicalDevice);
    ~VulkanDevice();

    SE_DISABLE_COPY(VulkanDevice);
    SE_DISABLE_MOVE(VulkanDevice);

    VkDevice Handle() const;
    VkQueue GraphicsQueue() const;
    VkQueue PresentQueue() const;
    VkPipelineCache PipelineCacheHandle() const;
    void SavePipelineCache() const;

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_PresentQueue = VK_NULL_HANDLE;
    std::unique_ptr<VulkanPipelineCache> m_PipelineCache;
};

}
