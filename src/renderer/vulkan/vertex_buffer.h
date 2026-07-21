#pragma once

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/vertex.h"
#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanUploadBatch;

class VulkanVertexBuffer {
public:
    VulkanVertexBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::span<const Vertex> vertices,
        VulkanUploadBatch* uploadBatch = nullptr,
        bool accelerationStructureInput = false
    );
    VulkanVertexBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::span<const Vertex3D> vertices,
        VulkanUploadBatch* uploadBatch = nullptr,
        bool accelerationStructureInput = true
    );

    ~VulkanVertexBuffer();

    SE_DISABLE_COPY(VulkanVertexBuffer);
    SE_DISABLE_MOVE(VulkanVertexBuffer);

    VkBuffer Handle() const;
    u32 VertexCount() const;
    VkDeviceAddress DeviceAddress() const;

private:
    void CreateVertexBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::span<const std::byte> vertexBytes,
        u32 vertexCount,
        VulkanUploadBatch* uploadBatch,
        bool accelerationStructureInput
    );

private:
    std::unique_ptr<VulkanBuffer> m_Buffer;
    u32 m_VertexCount = 0;
    bool m_DeviceAddressReady = false;
};

}
