#include "renderer/vulkan/index_buffer.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/upload_batch.h"

namespace se {

namespace {

std::span<const std::byte> AsBytes(std::span<const VulkanIndexBuffer::Index> indices) {
    return std::as_bytes(indices);
}

}

VulkanIndexBuffer::VulkanIndexBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::vector<Index> indices,
    VulkanUploadBatch* uploadBatch
) {
    CreateIndexBuffer(device, physicalDevice, commandPool, indices, uploadBatch);
}

VulkanIndexBuffer::~VulkanIndexBuffer() = default;

VkBuffer VulkanIndexBuffer::Handle() const {
    SE_ASSERT(m_Buffer != nullptr, "Index buffer has not been created");
    return m_Buffer->Handle();
}

u32 VulkanIndexBuffer::IndexCount() const {
    return m_IndexCount;
}

VkIndexType VulkanIndexBuffer::IndexType() const {
    return VK_INDEX_TYPE_UINT32;
}

void VulkanIndexBuffer::CreateIndexBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::span<const Index> indices,
    VulkanUploadBatch* uploadBatch
) {
    SE_ASSERT(!indices.empty(), "Index buffer must contain at least one index");

    m_IndexCount = static_cast<u32>(indices.size());
    const VkDeviceSize bufferSize = sizeof(Index) * indices.size();

    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer->Upload(AsBytes(indices));

    m_Buffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    VulkanBuffer::Copy(
        device,
        commandPool,
        stagingBuffer->Handle(),
        m_Buffer->Handle(),
        bufferSize,
        uploadBatch
    );
    if (uploadBatch != nullptr) {
        uploadBatch->KeepAlive(std::move(stagingBuffer));
    }
}

}
