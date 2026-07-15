#include "renderer/vulkan/device.h"

#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/pipeline_cache.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace se {

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

    const std::vector<const char*> enabledExtensions =
        EnabledVulkanDeviceExtensionsForPhysicalDevice(physicalDevice.Handle());
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    if (IsExtensionEnabled(
        enabledExtensions,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
    )) {
        VkPhysicalDeviceBufferDeviceAddressFeatures supportedBufferDeviceAddress{};
        supportedBufferDeviceAddress.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        VkPhysicalDeviceFeatures2 supportedFeatures{};
        supportedFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supportedFeatures.pNext = &supportedBufferDeviceAddress;
        vkGetPhysicalDeviceFeatures2(physicalDevice.Handle(), &supportedFeatures);

        bufferDeviceAddressFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddressFeatures.bufferDeviceAddress =
            supportedBufferDeviceAddress.bufferDeviceAddress;
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

void VulkanDevice::SavePipelineCache() const {
    if (m_PipelineCache != nullptr) {
        m_PipelineCache->Save();
    }
}

}
