#pragma once

#include "core.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <type_traits>
#include <vulkan/vulkan.h>

namespace se {

inline const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

inline std::string ReadVulkanEnvironmentString(const char* name) {
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

inline std::string LowerAscii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))
        );
    }
    return value;
}

inline bool VulkanEnvironmentFlagEnabled(const char* name) {
    const std::string value = LowerAscii(ReadVulkanEnvironmentString(name));
    return value == "1" ||
        value == "true" ||
        value == "yes" ||
        value == "on";
}

inline bool DlssVulkanRequirementsRequestedFromEnvironment() {
    if (VulkanEnvironmentFlagEnabled("SE_ENABLE_DLSS_VULKAN_EXTENSIONS") ||
        VulkanEnvironmentFlagEnabled("SE_DLSS_VULKAN_EXTENSIONS")) {
        return true;
    }

    std::string provider = ReadVulkanEnvironmentString("SE_UPSCALER_PLUGIN");
    if (provider.empty()) {
        provider = ReadVulkanEnvironmentString("SE_TEMPORAL_UPSCALER_PLUGIN");
    }
    provider = LowerAscii(provider);
    return provider == "dlss" ||
        provider == "nvidia-dlss" ||
        provider == "nvidia_dlss" ||
        provider == "ngx";
}

inline void AppendUniqueExtension(
    std::vector<const char*>& extensions,
    const char* extensionName
) {
    const auto existing = std::find_if(
        extensions.begin(),
        extensions.end(),
        [extensionName](const char* current) {
            return std::strcmp(current, extensionName) == 0;
        }
    );
    if (existing == extensions.end()) {
        extensions.push_back(extensionName);
    }
}

inline std::set<std::string> AvailableVulkanInstanceExtensionNames() {
    u32 extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> extensionProperties(extensionCount);
    if (extensionCount != 0u) {
        vkEnumerateInstanceExtensionProperties(
            nullptr,
            &extensionCount,
            extensionProperties.data()
        );
    }

    std::set<std::string> names;
    for (const VkExtensionProperties& extension : extensionProperties) {
        names.insert(extension.extensionName);
    }
    return names;
}

inline std::set<std::string> AvailableVulkanDeviceExtensionNames(
    VkPhysicalDevice physicalDevice
) {
    u32 extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(
        physicalDevice,
        nullptr,
        &extensionCount,
        nullptr
    );

    std::vector<VkExtensionProperties> extensionProperties(extensionCount);
    if (extensionCount != 0u) {
        vkEnumerateDeviceExtensionProperties(
            physicalDevice,
            nullptr,
            &extensionCount,
            extensionProperties.data()
        );
    }

    std::set<std::string> names;
    for (const VkExtensionProperties& extension : extensionProperties) {
        names.insert(extension.extensionName);
    }
    return names;
}

inline std::vector<const char*> DlssVulkanInstanceExtensionRequirements() {
    if (!DlssVulkanRequirementsRequestedFromEnvironment()) {
        return {};
    }

    std::vector<const char*> extensions;
#if defined(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif
    return extensions;
}

inline std::vector<const char*> EnabledOptionalDlssVulkanInstanceExtensions() {
    std::vector<const char*> extensions;
    const std::set<std::string> availableNames =
        AvailableVulkanInstanceExtensionNames();
    for (const char* extensionName : DlssVulkanInstanceExtensionRequirements()) {
        if (availableNames.find(extensionName) != availableNames.end()) {
            AppendUniqueExtension(extensions, extensionName);
        }
    }
    return extensions;
}

inline std::vector<const char*> DlssVulkanDeviceExtensionRequirements() {
    if (!DlssVulkanRequirementsRequestedFromEnvironment()) {
        return {};
    }

    std::vector<const char*> extensions;
#if defined(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)
    extensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
#endif
#if defined(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)
    extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
#endif
#if defined(VK_NVX_BINARY_IMPORT_EXTENSION_NAME)
    extensions.push_back(VK_NVX_BINARY_IMPORT_EXTENSION_NAME);
#endif
#if defined(VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME)
    extensions.push_back(VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME);
#endif
    return extensions;
}

inline std::vector<const char*> EnabledVulkanDeviceExtensionsForPhysicalDevice(
    VkPhysicalDevice physicalDevice
) {
    std::vector<const char*> extensions = kDeviceExtensions;
    const std::set<std::string> availableNames =
        AvailableVulkanDeviceExtensionNames(physicalDevice);
    for (const char* extensionName : DlssVulkanDeviceExtensionRequirements()) {
        if (availableNames.find(extensionName) != availableNames.end()) {
            AppendUniqueExtension(extensions, extensionName);
        }
    }
    return extensions;
}

inline bool IsExtensionEnabled(
    const std::vector<const char*>& extensions,
    const char* extensionName
) {
    return std::any_of(
        extensions.begin(),
        extensions.end(),
        [extensionName](const char* current) {
            return std::strcmp(current, extensionName) == 0;
        }
    );
}

template <typename VulkanHandle>
inline std::uint64_t VulkanHandleValue(VulkanHandle handle) {
    if constexpr (std::is_pointer_v<VulkanHandle>) {
        return reinterpret_cast<std::uint64_t>(handle);
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}

template <typename VulkanHandle>
inline void SetVulkanDebugObjectName(
    VkDevice device,
    VkObjectType objectType,
    VulkanHandle handle,
    const char* name
) {
    if (device == VK_NULL_HANDLE ||
        handle == VK_NULL_HANDLE ||
        name == nullptr ||
        name[0] == '\0') {
        return;
    }

    const auto setName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT")
    );
    if (setName == nullptr) {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = objectType;
    nameInfo.objectHandle = VulkanHandleValue(handle);
    nameInfo.pObjectName = name;
    setName(device, &nameInfo);
}

inline void BeginVulkanDebugLabel(
    VkDevice device,
    VkCommandBuffer commandBuffer,
    const char* name,
    float red = 0.2f,
    float green = 0.45f,
    float blue = 1.0f,
    float alpha = 1.0f
) {
    if (device == VK_NULL_HANDLE ||
        commandBuffer == VK_NULL_HANDLE ||
        name == nullptr ||
        name[0] == '\0') {
        return;
    }

    const auto beginLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(device, "vkCmdBeginDebugUtilsLabelEXT")
    );
    if (beginLabel == nullptr) {
        return;
    }

    VkDebugUtilsLabelEXT label{};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name;
    label.color[0] = red;
    label.color[1] = green;
    label.color[2] = blue;
    label.color[3] = alpha;
    beginLabel(commandBuffer, &label);
}

inline void EndVulkanDebugLabel(
    VkDevice device,
    VkCommandBuffer commandBuffer
) {
    if (device == VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE) {
        return;
    }

    const auto endLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(device, "vkCmdEndDebugUtilsLabelEXT")
    );
    if (endLabel == nullptr) {
        return;
    }

    endLabel(commandBuffer);
}

}
