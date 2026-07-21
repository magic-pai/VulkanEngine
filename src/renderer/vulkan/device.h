#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <memory>

namespace se {

class VulkanPhysicalDevice;
class VulkanPipelineCache;

struct VulkanRayTracingCapabilities {
    bool bufferDeviceAddressExtensionSupported = false;
    bool deferredHostOperationsExtensionSupported = false;
    bool accelerationStructureExtensionSupported = false;
    bool rayQueryExtensionSupported = false;
    bool rayTracingPipelineExtensionSupported = false;
    bool bufferDeviceAddressFeatureSupported = false;
    bool accelerationStructureFeatureSupported = false;
    bool rayQueryFeatureSupported = false;
    bool rayTracingPipelineFeatureSupported = false;
    bool rayQueryDeviceEnabled = false;

    bool RayQueryExtensionsReady() const;
    bool RayQueryFeaturesReady() const;
    bool RayQueryHardwareReady() const;
};

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
    const VulkanRayTracingCapabilities& RayTracingCapabilities() const;
    void SavePipelineCache() const;

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_PresentQueue = VK_NULL_HANDLE;
    VulkanRayTracingCapabilities m_RayTracingCapabilities{};
    std::unique_ptr<VulkanPipelineCache> m_PipelineCache;
};

}
