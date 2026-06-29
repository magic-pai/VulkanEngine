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
    VulkanUploadBatch* uploadBatch
) {
    CreateVertexBuffer(
        device,
        physicalDevice,
        commandPool,
        AsBytes(vertices),
        static_cast<u32>(vertices.size()),
        uploadBatch
    );
}

VulkanVertexBuffer::VulkanVertexBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::span<const Vertex3D> vertices,
    VulkanUploadBatch* uploadBatch
) {
    CreateVertexBuffer(
        device,
        physicalDevice,
        commandPool,
        AsBytes(vertices),
        static_cast<u32>(vertices.size()),
        uploadBatch
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

void VulkanVertexBuffer::CreateVertexBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::span<const std::byte> vertexBytes,
    u32 vertexCount,
    VulkanUploadBatch* uploadBatch
) {
    SE_ASSERT(vertexCount > 0, "Vertex buffer must contain at least one vertex");
    SE_ASSERT(!vertexBytes.empty(), "Vertex buffer byte span must not be empty");

    m_VertexCount = vertexCount;
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(vertexBytes.size_bytes());

    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer->Upload(vertexBytes);

    m_Buffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
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
