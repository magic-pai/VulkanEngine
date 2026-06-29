#pragma once

#include "vulkan_common.h"

namespace se {
    class VulkanInstance {
    public:
        VulkanInstance();
        ~VulkanInstance();

        SE_DISABLE_COPY(VulkanInstance);
        SE_DISABLE_MOVE(VulkanInstance);

        VkInstance Handle() const;

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    };
}
