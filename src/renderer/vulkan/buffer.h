#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <cstddef>

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanUploadBatch;

class VulkanBuffer {
public:
    VulkanBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memoryProperties
    );

    ~VulkanBuffer();

    SE_DISABLE_COPY(VulkanBuffer);
    SE_DISABLE_MOVE(VulkanBuffer);

    VkBuffer Handle() const;
    VkDeviceSize Size() const;

    void Upload(std::span<const std::byte> data) const;
    void Download(std::span<std::byte> data) const;

    static void Copy(
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool,
        VkBuffer source,
        VkBuffer destination,
        VkDeviceSize size,
        VulkanUploadBatch* uploadBatch = nullptr
    );

private:
    void Release();

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkDeviceSize m_Size = 0;
    VkMemoryPropertyFlags m_MemoryProperties = 0;
};

}
