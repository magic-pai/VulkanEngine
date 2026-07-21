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
    VulkanUploadBatch* uploadBatch,
    bool accelerationStructureInput
) {
    CreateIndexBuffer(
        device,
        physicalDevice,
        commandPool,
        indices,
        uploadBatch,
        accelerationStructureInput
    );
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

VkDeviceAddress VulkanIndexBuffer::DeviceAddress() const {
    return m_DeviceAddressReady && m_Buffer != nullptr
        ? m_Buffer->DeviceAddress()
        : 0;
}

void VulkanIndexBuffer::CreateIndexBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::span<const Index> indices,
    VulkanUploadBatch* uploadBatch,
    bool accelerationStructureInput
) {
    SE_ASSERT(!indices.empty(), "Index buffer must contain at least one index");

    m_IndexCount = static_cast<u32>(indices.size());
    const VkDeviceSize bufferSize = sizeof(Index) * indices.size();
    m_DeviceAddressReady = accelerationStructureInput &&
        device.RayTracingCapabilities().rayQueryDeviceEnabled;

    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer->Upload(AsBytes(indices));

    VkBufferUsageFlags indexUsage =
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    VkMemoryAllocateFlags allocationFlags = 0;
    if (m_DeviceAddressReady) {
        indexUsage |=
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        allocationFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }
    m_Buffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        bufferSize,
        indexUsage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        allocationFlags
    );

    VkAccessFlags destinationAccess = VK_ACCESS_INDEX_READ_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    if (m_DeviceAddressReady) {
        destinationAccess |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        destinationStage |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    }

    VulkanBuffer::Copy(
        device,
        commandPool,
        stagingBuffer->Handle(),
        m_Buffer->Handle(),
        bufferSize,
        uploadBatch,
        destinationAccess,
        destinationStage
    );
    if (uploadBatch != nullptr) {
        uploadBatch->KeepAlive(std::move(stagingBuffer));
    }
}

}
