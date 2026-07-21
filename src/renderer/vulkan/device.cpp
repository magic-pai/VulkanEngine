#include "renderer/vulkan/device.h"

#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/pipeline_cache.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace se {

bool VulkanRayTracingCapabilities::RayQueryExtensionsReady() const {
    return bufferDeviceAddressExtensionSupported &&
        deferredHostOperationsExtensionSupported &&
        accelerationStructureExtensionSupported &&
        rayQueryExtensionSupported;
}

bool VulkanRayTracingCapabilities::RayQueryFeaturesReady() const {
    return bufferDeviceAddressFeatureSupported &&
        shaderInt64FeatureSupported &&
        accelerationStructureFeatureSupported &&
        rayQueryFeatureSupported;
}

bool VulkanRayTracingCapabilities::RayQueryHardwareReady() const {
    return RayQueryExtensionsReady() && RayQueryFeaturesReady();
}

namespace {

std::string ReadDeviceEnvironmentString(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
        return {};
    }

    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

bool DeviceShutdownTraceEnabled() {
    const std::string value = ReadDeviceEnvironmentString("SE_SHUTDOWN_TRACE");
    const std::string deviceValue =
        value.empty() ? ReadDeviceEnvironmentString("SE_DEVICE_SHUTDOWN_TRACE") : value;
    return deviceValue == "1" ||
        deviceValue == "true" ||
        deviceValue == "TRUE" ||
        deviceValue == "on" ||
        deviceValue == "ON" ||
        deviceValue == "yes" ||
        deviceValue == "YES";
}

f32 DeviceElapsedMilliseconds(std::chrono::steady_clock::time_point startTime) {
    return std::chrono::duration<f32, std::milli>(
        std::chrono::steady_clock::now() - startTime
    ).count();
}

}

VulkanDevice::VulkanDevice(const VulkanPhysicalDevice& physicalDevice) {
    const QueueFamilyIndices& indices = physicalDevice.QueueFamilies();
    SE_ASSERT(indices.IsComplete(), "Physical device queue family indices are incomplete");

    const f32 queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

    const std::set<u32> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    for (u32 queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = physicalDevice.Features().samplerAnisotropy;
    deviceFeatures.independentBlend = physicalDevice.Features().independentBlend;
    deviceFeatures.depthBiasClamp = physicalDevice.Features().depthBiasClamp;

    const std::vector<const char*> enabledExtensions =
        EnabledVulkanDeviceExtensionsForPhysicalDevice(physicalDevice.Handle());
    const std::set<std::string> availableExtensions =
        AvailableVulkanDeviceExtensionNames(physicalDevice.Handle());
    auto extensionAvailable = [&availableExtensions](const char* name) {
        return availableExtensions.find(name) != availableExtensions.end();
    };
    m_RayTracingCapabilities.bufferDeviceAddressExtensionSupported =
        extensionAvailable(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    m_RayTracingCapabilities.deferredHostOperationsExtensionSupported =
        extensionAvailable(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    m_RayTracingCapabilities.accelerationStructureExtensionSupported =
        extensionAvailable(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    m_RayTracingCapabilities.rayQueryExtensionSupported =
        extensionAvailable(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    m_RayTracingCapabilities.rayTracingPipelineExtensionSupported =
        extensionAvailable(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

    VkPhysicalDeviceBufferDeviceAddressFeatures supportedBufferDeviceAddress{};
    supportedBufferDeviceAddress.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR supportedAccelerationStructure{};
    supportedAccelerationStructure.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceRayQueryFeaturesKHR supportedRayQuery{};
    supportedRayQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR supportedRayTracingPipeline{};
    supportedRayTracingPipeline.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    supportedBufferDeviceAddress.pNext = &supportedAccelerationStructure;
    supportedAccelerationStructure.pNext = &supportedRayQuery;
    supportedRayQuery.pNext = &supportedRayTracingPipeline;
    VkPhysicalDeviceFeatures2 supportedFeatures{};
    supportedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supportedFeatures.pNext = &supportedBufferDeviceAddress;
    vkGetPhysicalDeviceFeatures2(physicalDevice.Handle(), &supportedFeatures);

    m_RayTracingCapabilities.bufferDeviceAddressFeatureSupported =
        supportedBufferDeviceAddress.bufferDeviceAddress == VK_TRUE;
    m_RayTracingCapabilities.shaderInt64FeatureSupported =
        supportedFeatures.features.shaderInt64 == VK_TRUE;
    m_RayTracingCapabilities.accelerationStructureFeatureSupported =
        supportedAccelerationStructure.accelerationStructure == VK_TRUE;
    m_RayTracingCapabilities.rayQueryFeatureSupported =
        supportedRayQuery.rayQuery == VK_TRUE;
    m_RayTracingCapabilities.rayTracingPipelineFeatureSupported =
        supportedRayTracingPipeline.rayTracingPipeline == VK_TRUE;

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    if (IsExtensionEnabled(
        enabledExtensions,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
    )) {
        bufferDeviceAddressFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddressFeatures.bufferDeviceAddress =
            supportedBufferDeviceAddress.bufferDeviceAddress;
    }

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
    const bool rayQueryExtensionsEnabled =
        IsExtensionEnabled(
            enabledExtensions,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
        ) &&
        IsExtensionEnabled(
            enabledExtensions,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME
        ) &&
        IsExtensionEnabled(enabledExtensions, VK_KHR_RAY_QUERY_EXTENSION_NAME);
    if (rayQueryExtensionsEnabled &&
        bufferDeviceAddressFeatures.bufferDeviceAddress == VK_TRUE &&
        supportedFeatures.features.shaderInt64 == VK_TRUE &&
        supportedAccelerationStructure.accelerationStructure == VK_TRUE &&
        supportedRayQuery.rayQuery == VK_TRUE) {
        deviceFeatures.shaderInt64 = VK_TRUE;
        accelerationStructureFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        accelerationStructureFeatures.accelerationStructure = VK_TRUE;
        rayQueryFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rayQueryFeatures.rayQuery = VK_TRUE;
        bufferDeviceAddressFeatures.pNext = &accelerationStructureFeatures;
        accelerationStructureFeatures.pNext = &rayQueryFeatures;
        m_RayTracingCapabilities.shaderInt64DeviceEnabled = true;
        m_RayTracingCapabilities.rayQueryDeviceEnabled = true;
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext =
        bufferDeviceAddressFeatures.sType != 0
            ? &bufferDeviceAddressFeatures
            : nullptr;
    createInfo.queueCreateInfoCount = static_cast<u32>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount =
        static_cast<u32>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    const VkResult result = vkCreateDevice(
        physicalDevice.Handle(),
        &createInfo,
        nullptr,
        &m_Device
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device");
    }

    vkGetDeviceQueue(m_Device, indices.graphicsFamily.value(), 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, indices.presentFamily.value(), 0, &m_PresentQueue);
    m_PipelineCache = std::make_unique<VulkanPipelineCache>(
        m_Device,
        physicalDevice.Properties()
    );
}

VulkanDevice::~VulkanDevice() {
    const bool traceShutdown = DeviceShutdownTraceEnabled();
    const auto destructorStartTime = std::chrono::steady_clock::now();
    if (traceShutdown) {
        std::cout << "[shutdown] device destroy_begin +0ms" << std::endl;
    }
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }
    if (traceShutdown) {
        std::cout << "[shutdown] device wait_idle +"
            << DeviceElapsedMilliseconds(destructorStartTime) << "ms"
            << std::endl;
    }
    m_PipelineCache.reset();
    if (traceShutdown) {
        std::cout << "[shutdown] device pipeline_cache_reset +"
            << DeviceElapsedMilliseconds(destructorStartTime) << "ms"
            << std::endl;
    }
    if (m_Device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
    }
    if (traceShutdown) {
        std::cout << "[shutdown] device destroy_device +"
            << DeviceElapsedMilliseconds(destructorStartTime) << "ms"
            << std::endl;
    }
}

VkDevice VulkanDevice::Handle() const {
    return m_Device;
}

VkQueue VulkanDevice::GraphicsQueue() const {
    return m_GraphicsQueue;
}

VkQueue VulkanDevice::PresentQueue() const {
    return m_PresentQueue;
}

VkPipelineCache VulkanDevice::PipelineCacheHandle() const {
    return m_PipelineCache != nullptr ? m_PipelineCache->Handle() : VK_NULL_HANDLE;
}

const VulkanRayTracingCapabilities& VulkanDevice::RayTracingCapabilities() const {
    return m_RayTracingCapabilities;
}

void VulkanDevice::SavePipelineCache() const {
    if (m_PipelineCache != nullptr) {
        m_PipelineCache->Save();
    }
}

}
