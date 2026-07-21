#include "renderer/vulkan/vertex_buffer.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/upload_batch.h"

namespace se {

namespace {

std::span<const std::byte> AsBytes(std::span<const Vertex> vertices) {
    return std::as_bytes(vertices);
}

std::span<const std::byte> AsBytes(std::span<const Vertex3D> vertices) {
    return std::as_bytes(vertices);
}

}

VulkanVertexBuffer::VulkanVertexBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::span<const Vertex> vertices,
    VulkanUploadBatch* uploadBatch,
    bool accelerationStructureInput
) {
    CreateVertexBuffer(
        device,
        physicalDevice,
        commandPool,
        AsBytes(vertices),
        static_cast<u32>(vertices.size()),
        uploadBatch,
        accelerationStructureInput
    );
}

VulkanVertexBuffer::VulkanVertexBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::span<const Vertex3D> vertices,
    VulkanUploadBatch* uploadBatch,
    bool accelerationStructureInput
) {
    CreateVertexBuffer(
        device,
        physicalDevice,
        commandPool,
        AsBytes(vertices),
        static_cast<u32>(vertices.size()),
        uploadBatch,
        accelerationStructureInput
    );
}

VulkanVertexBuffer::~VulkanVertexBuffer() = default;

VkBuffer VulkanVertexBuffer::Handle() const {
    SE_ASSERT(m_Buffer != nullptr, "Vertex buffer has not been created");
    return m_Buffer->Handle();
}

u32 VulkanVertexBuffer::VertexCount() const {
    return m_VertexCount;
}

VkDeviceAddress VulkanVertexBuffer::DeviceAddress() const {
    return m_DeviceAddressReady && m_Buffer != nullptr
        ? m_Buffer->DeviceAddress()
        : 0;
}

void VulkanVertexBuffer::CreateVertexBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::span<const std::byte> vertexBytes,
    u32 vertexCount,
    VulkanUploadBatch* uploadBatch,
    bool accelerationStructureInput
) {
    SE_ASSERT(vertexCount > 0, "Vertex buffer must contain at least one vertex");
    SE_ASSERT(!vertexBytes.empty(), "Vertex buffer byte span must not be empty");

    m_VertexCount = vertexCount;
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(vertexBytes.size_bytes());
    m_DeviceAddressReady = accelerationStructureInput &&
        device.RayTracingCapabilities().rayQueryDeviceEnabled;

    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer->Upload(vertexBytes);

    VkBufferUsageFlags vertexUsage =
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VkMemoryAllocateFlags allocationFlags = 0;
    if (m_DeviceAddressReady) {
        vertexUsage |=
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        allocationFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }
    m_Buffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        bufferSize,
        vertexUsage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        allocationFlags
    );

    VkAccessFlags destinationAccess = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
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
