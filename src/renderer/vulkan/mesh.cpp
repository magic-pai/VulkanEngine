#include "renderer/vulkan/mesh.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

#include <algorithm>
#include <limits>

namespace se {

VulkanMesh::VulkanMesh(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::vector<Vertex> vertices,
    std::vector<u32> indices,
    VulkanUploadBatch* uploadBatch
) : m_VertexBuffer(
        device,
        physicalDevice,
        commandPool,
        std::span<const Vertex>(vertices.data(), vertices.size()),
        uploadBatch,
        false
    ),
    m_IndexBuffer(
        device,
        physicalDevice,
        commandPool,
        std::move(indices),
        uploadBatch,
        false
    ) {
    m_Is3D = false;
    m_VertexStride = sizeof(Vertex);
    CalculateBounds(std::span<const Vertex>(vertices.data(), vertices.size()));
}

VulkanMesh::VulkanMesh(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::vector<Vertex3D> vertices,
    std::vector<u32> indices,
    VulkanUploadBatch* uploadBatch
) : m_VertexBuffer(
        device,
        physicalDevice,
        commandPool,
        std::span<const Vertex3D>(vertices.data(), vertices.size()),
        uploadBatch,
        true
    ),
    m_IndexBuffer(
        device,
        physicalDevice,
        commandPool,
        std::move(indices),
        uploadBatch,
        true
    ) {
    m_Is3D = true;
    m_VertexStride = sizeof(Vertex3D);
    CalculateBounds(std::span<const Vertex3D>(vertices.data(), vertices.size()));
}

void VulkanMesh::Bind(VkCommandBuffer commandBuffer) const {
    const VkBuffer vertexBuffers[] = {
        m_VertexBuffer.Handle()
    };
    const VkDeviceSize offsets[] = {
        0
    };

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(
        commandBuffer,
        m_IndexBuffer.Handle(),
        0,
        m_IndexBuffer.IndexType()
    );
}

bool VulkanMesh::Is3D() const {
    return m_Is3D;
}

bool VulkanMesh::AccelerationStructureInputReady() const {
    return m_Is3D &&
        m_VertexBuffer.DeviceAddress() != 0 &&
        m_IndexBuffer.DeviceAddress() != 0 &&
        m_VertexBuffer.VertexCount() >= 3u &&
        m_IndexBuffer.IndexCount() >= 3u;
}

VkDeviceAddress VulkanMesh::VertexDeviceAddress() const {
    return m_VertexBuffer.DeviceAddress();
}

VkDeviceAddress VulkanMesh::IndexDeviceAddress() const {
    return m_IndexBuffer.DeviceAddress();
}

u32 VulkanMesh::VertexCount() const {
    return m_VertexBuffer.VertexCount();
}

u32 VulkanMesh::IndexCount() const {
    return m_IndexBuffer.IndexCount();
}

u32 VulkanMesh::VertexStride() const {
    return m_VertexStride;
}

const glm::vec3& VulkanMesh::BoundsMin() const {
    return m_BoundsMin;
}

const glm::vec3& VulkanMesh::BoundsMax() const {
    return m_BoundsMax;
}

template <typename TVertex>
void VulkanMesh::CalculateBounds(std::span<const TVertex> vertices) {
    SE_ASSERT(!vertices.empty(), "Mesh bounds need at least one vertex");

    m_BoundsMin = glm::vec3(std::numeric_limits<f32>::max());
    m_BoundsMax = glm::vec3(std::numeric_limits<f32>::lowest());

    for (const TVertex& vertex : vertices) {
        glm::vec3 position{ 0.0f };
        position.x = vertex.position[0];
        position.y = vertex.position[1];
        if constexpr (std::tuple_size_v<decltype(vertex.position)> >= 3) {
            position.z = vertex.position[2];
        }

        m_BoundsMin.x = std::min(m_BoundsMin.x, position.x);
        m_BoundsMin.y = std::min(m_BoundsMin.y, position.y);
        m_BoundsMin.z = std::min(m_BoundsMin.z, position.z);

        m_BoundsMax.x = std::max(m_BoundsMax.x, position.x);
        m_BoundsMax.y = std::max(m_BoundsMax.y, position.y);
        m_BoundsMax.z = std::max(m_BoundsMax.z, position.z);
    }
}

}
