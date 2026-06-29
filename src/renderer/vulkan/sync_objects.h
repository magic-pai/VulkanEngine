#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;

class VulkanSyncObjects {
public:
    static constexpr std::size_t kMaxFramesInFlight = 2;

    VulkanSyncObjects(const VulkanDevice& device, std::size_t swapchainImageCount);
    ~VulkanSyncObjects();

    SE_DISABLE_COPY(VulkanSyncObjects);
    SE_DISABLE_MOVE(VulkanSyncObjects);

    VkSemaphore ImageAvailableSemaphore(std::size_t frameIndex) const;
    VkSemaphore RenderFinishedSemaphore(std::size_t imageIndex) const;
    VkFence InFlightFence(std::size_t frameIndex) const;
    VkFence ImageInFlightFence(std::size_t imageIndex) const;

    void MarkImageInFlight(std::size_t imageIndex, VkFence fence);
    void RecreateSwapchainSyncObjects(std::size_t swapchainImageCount);

private:
    void CreateFrameSyncObjects(const VulkanDevice& device);
    void CreateSwapchainSyncObjects(std::size_t swapchainImageCount);
    void CleanupFrameSyncObjects();
    void CleanupSwapchainSyncObjects();
    void Cleanup();

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    std::vector<VkSemaphore> m_ImageAvailableSemaphores;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;
    std::vector<VkFence> m_InFlightFences;
    std::vector<VkFence> m_ImagesInFlight;
};

}
