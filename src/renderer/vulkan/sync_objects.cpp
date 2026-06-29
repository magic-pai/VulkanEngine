#include "renderer/vulkan/sync_objects.h"

#include "renderer/vulkan/device.h"

namespace se {

VulkanSyncObjects::VulkanSyncObjects(const VulkanDevice& device, std::size_t swapchainImageCount)
    : m_Device(device.Handle()) {
    CreateFrameSyncObjects(device);
    CreateSwapchainSyncObjects(swapchainImageCount);
}

VulkanSyncObjects::~VulkanSyncObjects() {
    Cleanup();
}

VkSemaphore VulkanSyncObjects::ImageAvailableSemaphore(std::size_t frameIndex) const {
    SE_ASSERT(frameIndex < m_ImageAvailableSemaphores.size(), "Frame index is out of range");
    return m_ImageAvailableSemaphores[frameIndex];
}

VkSemaphore VulkanSyncObjects::RenderFinishedSemaphore(std::size_t imageIndex) const {
    SE_ASSERT(imageIndex < m_RenderFinishedSemaphores.size(), "Swapchain image index is out of range");
    return m_RenderFinishedSemaphores[imageIndex];
}

VkFence VulkanSyncObjects::InFlightFence(std::size_t frameIndex) const {
    SE_ASSERT(frameIndex < m_InFlightFences.size(), "Frame index is out of range");
    return m_InFlightFences[frameIndex];
}

VkFence VulkanSyncObjects::ImageInFlightFence(std::size_t imageIndex) const {
    SE_ASSERT(imageIndex < m_ImagesInFlight.size(), "Swapchain image index is out of range");
    return m_ImagesInFlight[imageIndex];
}

void VulkanSyncObjects::MarkImageInFlight(std::size_t imageIndex, VkFence fence) {
    SE_ASSERT(imageIndex < m_ImagesInFlight.size(), "Swapchain image index is out of range");
    m_ImagesInFlight[imageIndex] = fence;
}

void VulkanSyncObjects::RecreateSwapchainSyncObjects(std::size_t swapchainImageCount) {
    CleanupSwapchainSyncObjects();
    CreateSwapchainSyncObjects(swapchainImageCount);
}

void VulkanSyncObjects::CreateFrameSyncObjects(const VulkanDevice& device) {
    m_ImageAvailableSemaphores.resize(kMaxFramesInFlight);
    m_InFlightFences.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (std::size_t index = 0; index < kMaxFramesInFlight; ++index) {
        if (vkCreateSemaphore(device.Handle(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[index]) != VK_SUCCESS ||
            vkCreateFence(device.Handle(), &fenceInfo, nullptr, &m_InFlightFences[index]) != VK_SUCCESS) {
            CleanupFrameSyncObjects();
            throw std::runtime_error("Failed to create Vulkan frame synchronization objects");
        }
    }
}

void VulkanSyncObjects::CreateSwapchainSyncObjects(std::size_t swapchainImageCount) {
    SE_ASSERT(swapchainImageCount > 0, "Swapchain image count must be greater than zero");

    m_RenderFinishedSemaphores.resize(swapchainImageCount);
    m_ImagesInFlight.resize(swapchainImageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (std::size_t index = 0; index < swapchainImageCount; ++index) {
        if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[index]) != VK_SUCCESS) {
            CleanupSwapchainSyncObjects();
            throw std::runtime_error("Failed to create Vulkan swapchain synchronization objects");
        }
    }
}

void VulkanSyncObjects::CleanupFrameSyncObjects() {
    for (VkFence fence : m_InFlightFences) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_Device, fence, nullptr);
        }
    }

    for (VkSemaphore semaphore : m_ImageAvailableSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_Device, semaphore, nullptr);
        }
    }

    m_InFlightFences.clear();
    m_ImageAvailableSemaphores.clear();
}

void VulkanSyncObjects::CleanupSwapchainSyncObjects() {
    for (VkSemaphore semaphore : m_RenderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_Device, semaphore, nullptr);
        }
    }

    m_RenderFinishedSemaphores.clear();
    m_ImagesInFlight.clear();
}

void VulkanSyncObjects::Cleanup() {
    CleanupSwapchainSyncObjects();
    CleanupFrameSyncObjects();
}

}
