#include "renderer/vulkan/surface.h"

#include "platform/window.h"
#include "renderer/vulkan/instance.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace se {

VulkanSurface::VulkanSurface(const VulkanInstance& instance, const Window& window)
    : m_Instance(instance.Handle()) {
    const VkResult result = glfwCreateWindowSurface(
        m_Instance,
        window.NativeHandle(),
        nullptr,
        &m_Surface
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }
}

VulkanSurface::~VulkanSurface() {
    if (m_Surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        m_Surface = VK_NULL_HANDLE;
    }
}

VkSurfaceKHR VulkanSurface::Handle() const {
    return m_Surface;
}

}