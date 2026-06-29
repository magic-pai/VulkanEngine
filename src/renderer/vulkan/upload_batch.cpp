#include "renderer/vulkan/upload_batch.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"

namespace se {

VulkanUploadBatch::VulkanUploadBatch(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool
) : m_Device(device), m_CommandPool(commandPool) {
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool = m_CommandPool.Handle();
    allocateInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(
        m_Device.Handle(),
        &allocateInfo,
        &m_CommandBuffer
    ) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan upload command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(m_CommandBuffer, &beginInfo) != VK_SUCCESS) {
        Cancel();
        throw std::runtime_error("Failed to begin Vulkan upload command buffer");
    }
}

VulkanUploadBatch::~VulkanUploadBatch() {
    if (!m_Closed) {
        Cancel();
    }
}

VkCommandBuffer VulkanUploadBatch::CommandBuffer() const {
    SE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "Upload batch command buffer is not active");
    return m_CommandBuffer;
}

void VulkanUploadBatch::KeepAlive(std::unique_ptr<VulkanBuffer> buffer) {
    if (buffer != nullptr) {
        m_StagingBuffers.push_back(std::move(buffer));
    }
}

void VulkanUploadBatch::Submit() {
    if (m_Closed) {
        return;
    }

    if (vkEndCommandBuffer(m_CommandBuffer) != VK_SUCCESS) {
        Cancel();
        throw std::runtime_error("Failed to record Vulkan upload command buffer");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_CommandBuffer;

    if (vkQueueSubmit(
        m_Device.GraphicsQueue(),
        1,
        &submitInfo,
        VK_NULL_HANDLE
    ) != VK_SUCCESS) {
        Cancel();
        throw std::runtime_error("Failed to submit Vulkan upload command buffer");
    }

    vkQueueWaitIdle(m_Device.GraphicsQueue());
    vkFreeCommandBuffers(
        m_Device.Handle(),
        m_CommandPool.Handle(),
        1,
        &m_CommandBuffer
    );
    m_CommandBuffer = VK_NULL_HANDLE;
    m_StagingBuffers.clear();
    m_Closed = true;
}

void VulkanUploadBatch::Cancel() {
    if (m_CommandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(
            m_Device.Handle(),
            m_CommandPool.Handle(),
            1,
            &m_CommandBuffer
        );
        m_CommandBuffer = VK_NULL_HANDLE;
    }

    m_StagingBuffers.clear();
    m_Closed = true;
}

}
