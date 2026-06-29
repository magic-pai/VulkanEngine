#pragma once

#include "vulkan_common.h"

namespace se {

class Window;
class VulkanInstance;

class VulkanSurface {
public:
    VulkanSurface(const VulkanInstance& instance, const Window& window);
    ~VulkanSurface();

    SE_DISABLE_COPY(VulkanSurface);
    SE_DISABLE_MOVE(VulkanSurface);

    VkSurfaceKHR Handle() const;

private:
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
};

}