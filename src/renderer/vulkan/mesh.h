#pragma once

#include "renderer/vulkan/index_buffer.h"
#include "renderer/vulkan/vertex_buffer.h"
#include "renderer/vulkan/vulkan_common.h"

#include <glm/vec3.hpp>

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanUploadBatch;

class VulkanMesh {
public:
    using Index = u32;

    VulkanMesh(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::vector<Vertex> vertices,
        std::vector<Index> indices,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    VulkanMesh(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::vector<Vertex3D> vertices,
        std::vector<Index> indices,
        VulkanUploadBatch* uploadBatch = nullptr
    );

    ~VulkanMesh() = default;

    SE_DISABLE_COPY(VulkanMesh);
    SE_DISABLE_MOVE(VulkanMesh);

    void Bind(VkCommandBuffer commandBuffer) const;
    u32 IndexCount() const;
    const glm::vec3& BoundsMin() const;
    const glm::vec3& BoundsMax() const;

private:
    template <typename TVertex>
    void CalculateBounds(std::span<const TVertex> vertices);

private:
    VulkanVertexBuffer m_VertexBuffer;
    VulkanIndexBuffer m_IndexBuffer;
    glm::vec3 m_BoundsMin{ -0.5f };
    glm::vec3 m_BoundsMax{ 0.5f };
};

}
