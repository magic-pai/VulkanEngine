#include "instance.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace se {

namespace {

#if defined(NDEBUG)
    constexpr bool kEnableValidationLayers = false;
#else
    constexpr bool kEnableValidationLayers = true;
#endif

    const std::vector<const char*> kValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    std::string ReadEnvironmentValue(const char* name) {
#if defined(_MSC_VER)
        char* buffer = nullptr;
        size_t size = 0;
        if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
            return {};
        }

        std::string value{buffer};
        std::free(buffer);
        return value;
#else
        const char* value = std::getenv(name);
        return value ? std::string{value} : std::string{};
#endif
    }

    bool IsTruthyEnvironmentFlag(const char* name) {
        const std::string value = ReadEnvironmentValue(name);
        return value == "1" ||
            value == "true" ||
            value == "TRUE" ||
            value == "on" ||
            value == "ON";
    }

    bool ContainsText(std::string_view text, std::string_view needle) {
        return text.find(needle) != std::string_view::npos;
    }

    std::string ExtractBetween(
        std::string_view text,
        std::string_view begin,
        std::string_view end,
        size_t startOffset = 0
    ) {
        const size_t beginPos = text.find(begin, startOffset);
        if (beginPos == std::string_view::npos) {
            return {};
        }

        const size_t valueBegin = beginPos + begin.size();
        const size_t endPos = text.find(end, valueBegin);
        if (endPos == std::string_view::npos) {
            return {};
        }

        return std::string{text.substr(valueBegin, endPos - valueBegin)};
    }

    bool IsKnownNgxInternalDlssLayoutWarning(std::string_view message) {
        return ContainsText(message, "vkQueueSubmit()") &&
            ContainsText(message, "expects VkImage ") &&
            ContainsText(message, "[nv.ngx.dlss.") &&
            ContainsText(message, "VK_IMAGE_LAYOUT_GENERAL--instead") &&
            ContainsText(message, "VK_IMAGE_LAYOUT_UNDEFINED") &&
            ContainsText(message, "VUID-vkCmdDraw-None-09600");
    }

    bool SuppressKnownNgxInternalDlssLayoutWarning(std::string_view message) {
        if (!IsTruthyEnvironmentFlag(
                "SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT"
            )) {
            return false;
        }
        if (!IsKnownNgxInternalDlssLayoutWarning(message)) {
            return false;
        }

        const size_t imagePrefix = message.find("expects VkImage ");
        const std::string image =
            imagePrefix == std::string_view::npos ?
                std::string{} :
                ExtractBetween(message, "VkImage ", "[", imagePrefix);
        const size_t resourceBegin =
            imagePrefix == std::string_view::npos ?
                std::string_view::npos :
                message.find("[nv.ngx.dlss.", imagePrefix);
        const std::string resource =
            resourceBegin == std::string_view::npos ?
                std::string{} :
                ExtractBetween(message, "[", "]", resourceBegin);
        std::cout << "SelfEngineVkSuppressedNgxInternalLayout"
                  << " image=" << (image.empty() ? "unknown" : image)
                  << " resource=" << (resource.empty() ? "unknown" : resource)
                  << " policy=SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT"
                  << std::endl;
        return true;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData
    ) {
        (void)messageSeverity;
        (void)messageType;
        (void)userData;

        const std::string_view message =
            callbackData && callbackData->pMessage ?
                std::string_view{callbackData->pMessage} :
                std::string_view{};
        if (SuppressKnownNgxInternalDlssLayoutWarning(message)) {
            return VK_FALSE;
        }

        std::cerr << "[Vulkan Validation] " << message << std::endl;
        return VK_FALSE;
    }

    void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = DebugCallback;
        createInfo.pUserData = nullptr;
    }

    VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
        const VkAllocationCallbacks* allocator,
        VkDebugUtilsMessengerEXT* debugMessenger
    ) {
        const auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")
        );

        if (function) {
            return function(instance, createInfo, allocator, debugMessenger);
        }

        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    void DestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        const VkAllocationCallbacks* allocator
    ) {
        const auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")
        );

        if (function) {
            function(instance, debugMessenger, allocator);
        }
    }

    bool IsValidationLayerSupported(const char* layerName) {
        u32 layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const VkLayerProperties& layerProperties : availableLayers) {
            if (std::strcmp(layerName, layerProperties.layerName) == 0) {
                return true;
            }
        }

        return false;
    }

    std::vector<const char*> GetRequiredExtensions() {
        u32 glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        if (!glfwExtensions) {
            throw std::runtime_error("GLFW failed to provide Vulkan instance extensions");
        }

        std::vector<const char*> extensions(
            glfwExtensions,
            glfwExtensions + glfwExtensionCount
        );

        if (kEnableValidationLayers) {
            AppendUniqueExtension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        for (const char* extensionName :
            EnabledOptionalDlssVulkanInstanceExtensions()) {
            AppendUniqueExtension(extensions, extensionName);
        }

        return extensions;
    }

}

VulkanInstance::VulkanInstance() {
    if (!glfwVulkanSupported()) {
        throw std::runtime_error("GLFW reports that Vulkan is not supported");
    }

    if (kEnableValidationLayers) {
        for (const char* layerName : kValidationLayers) {
            if (!IsValidationLayerSupported(layerName)) {
                throw std::runtime_error("Required Vulkan validation layer is not available");
            }
        }
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SelfEngine Application";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "SelfEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions = GetRequiredExtensions();

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

    if (kEnableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<u32>(kValidationLayers.size());
        createInfo.ppEnabledLayerNames = kValidationLayers.data();

        PopulateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.ppEnabledLayerNames = nullptr;
        createInfo.pNext = nullptr;
    }

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    if (kEnableValidationLayers) {
        VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo{};
        PopulateDebugMessengerCreateInfo(messengerCreateInfo);

        if (CreateDebugUtilsMessengerEXT(m_Instance, &messengerCreateInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS) {
            vkDestroyInstance(m_Instance, nullptr);
            m_Instance = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to create Vulkan debug messenger");
        }
    }
}

VulkanInstance::~VulkanInstance() {
    if (m_DebugMessenger != VK_NULL_HANDLE) {
        DestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
        m_DebugMessenger = VK_NULL_HANDLE;
    }

    if (m_Instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;
    }
}

VkInstance VulkanInstance::Handle() const {
    return m_Instance;
}

}
