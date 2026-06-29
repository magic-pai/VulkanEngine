#include "renderer/vulkan/command_pool.h"

#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

namespace se {

VulkanCommandPool::VulkanCommandPool(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice
) : m_Device(device.Handle()) {
    CreateCommandPool(device, physicalDevice);
}

VulkanCommandPool::~VulkanCommandPool() {
    Cleanup();
}

VkCommandPool VulkanCommandPool::Handle() const {
    return m_CommandPool;
}

void VulkanCommandPool::CreateCommandPool(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice
) {
    const QueueFamilyIndices& indices = physicalDevice.QueueFamilies();
    SE_ASSERT(indices.graphicsFamily.has_value(), "Graphics queue family is missing");

    VkCommandPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    createInfo.queueFamilyIndex = indices.graphicsFamily.value();

    if (vkCreateCommandPool(device.Handle(), &createInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan command pool");
    }
}

void VulkanCommandPool::Cleanup() {
    if (m_CommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        m_CommandPool = VK_NULL_HANDLE;
    }
}

}
