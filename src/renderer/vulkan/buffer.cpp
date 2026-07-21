#include "renderer/vulkan/buffer.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/upload_batch.h"

#include <cstring>

namespace se {

VulkanBuffer::VulkanBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memoryProperties,
    VkMemoryAllocateFlags allocationFlags
) : m_Device(device.Handle()),
    m_Size(size),
    m_Usage(usage),
    m_MemoryProperties(memoryProperties) {
    SE_ASSERT(size > 0, "Vulkan buffer size must be greater than zero");

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device.Handle(), &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan buffer");
    }

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device.Handle(), m_Buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = physicalDevice.FindMemoryType(
        memoryRequirements.memoryTypeBits,
        memoryProperties
    );
    VkMemoryAllocateFlagsInfo allocateFlagsInfo{};
    if (allocationFlags != 0) {
        allocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocateFlagsInfo.flags = allocationFlags;
        allocateInfo.pNext = &allocateFlagsInfo;
    }

    if (vkAllocateMemory(device.Handle(), &allocateInfo, nullptr, &m_Memory) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to allocate Vulkan buffer memory");
    }

    if (vkBindBufferMemory(device.Handle(), m_Buffer, m_Memory, 0) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to bind Vulkan buffer memory");
    }
}

VulkanBuffer::~VulkanBuffer() {
    Release();
}

VkBuffer VulkanBuffer::Handle() const {
    return m_Buffer;
}

VkDeviceSize VulkanBuffer::Size() const {
    return m_Size;
}

VkBufferUsageFlags VulkanBuffer::Usage() const {
    return m_Usage;
}

VkDeviceAddress VulkanBuffer::DeviceAddress() const {
    SE_ASSERT(
        (m_Usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0,
        "Vulkan buffer was not created for device-address access"
    );
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = m_Buffer;
    return vkGetBufferDeviceAddress(m_Device, &addressInfo);
}

void VulkanBuffer::Upload(std::span<const std::byte> data) const {
    SE_ASSERT(data.size_bytes() <= m_Size, "Upload data is larger than Vulkan buffer");
    SE_ASSERT(
        (m_MemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0,
        "Vulkan buffer memory is not host visible"
    );

    void* mappedMemory = nullptr;
    if (vkMapMemory(m_Device, m_Memory, 0, data.size_bytes(), 0, &mappedMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map Vulkan buffer memory");
    }

    std::memcpy(mappedMemory, data.data(), data.size_bytes());
    vkUnmapMemory(m_Device, m_Memory);
}

void VulkanBuffer::Download(std::span<std::byte> data) const {
    SE_ASSERT(data.size_bytes() <= m_Size, "Download destination is larger than Vulkan buffer");
    SE_ASSERT(
        (m_MemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0,
        "Vulkan buffer memory is not host visible"
    );

    void* mappedMemory = nullptr;
    if (vkMapMemory(m_Device, m_Memory, 0, data.size_bytes(), 0, &mappedMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map Vulkan buffer memory for download");
    }

    std::memcpy(data.data(), mappedMemory, data.size_bytes());
    vkUnmapMemory(m_Device, m_Memory);
}

void VulkanBuffer::Copy(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    VkBuffer source,
    VkBuffer destination,
    VkDeviceSize size,
    VulkanUploadBatch* uploadBatch,
    VkAccessFlags destinationAccessMask,
    VkPipelineStageFlags destinationStageMask
) {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (uploadBatch != nullptr) {
        commandBuffer = uploadBatch->CommandBuffer();
    } else {
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandPool = commandPool.Handle();
        allocateInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device.Handle(), &allocateInfo, &commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate Vulkan copy command buffer");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
            throw std::runtime_error("Failed to begin Vulkan copy command buffer");
        }
    }

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;

    vkCmdCopyBuffer(commandBuffer, source, destination, 1, &copyRegion);

    VkBufferMemoryBarrier bufferBarrier{};
    bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bufferBarrier.dstAccessMask = destinationAccessMask;
    bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarrier.buffer = destination;
    bufferBarrier.offset = 0;
    bufferBarrier.size = size;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        destinationStageMask,
        0,
        0,
        nullptr,
        1,
        &bufferBarrier,
        0,
        nullptr
    );

    if (uploadBatch != nullptr) {
        return;
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
        throw std::runtime_error("Failed to record Vulkan copy command buffer");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(device.GraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
        throw std::runtime_error("Failed to submit Vulkan buffer copy");
    }

    vkQueueWaitIdle(device.GraphicsQueue());
    vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
}

void VulkanBuffer::Release() {
    if (m_Buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_Device, m_Buffer, nullptr);
        m_Buffer = VK_NULL_HANDLE;
    }

    if (m_Memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_Memory, nullptr);
        m_Memory = VK_NULL_HANDLE;
    }

    m_Size = 0;
    m_Usage = 0;
    m_MemoryProperties = 0;
}

}
