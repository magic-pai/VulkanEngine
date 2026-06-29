#pragma once

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanUploadBatch;

class VulkanIndexBuffer {
public:
    using Index = u32;

    VulkanIndexBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::vector<Index> indices,
        VulkanUploadBatch* uploadBatch = nullptr
    );

    ~VulkanIndexBuffer();

    SE_DISABLE_COPY(VulkanIndexBuffer);
    SE_DISABLE_MOVE(VulkanIndexBuffer);

    VkBuffer Handle() const;
    u32 IndexCount() const;
    VkIndexType IndexType() const;

private:
    void CreateIndexBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::span<const Index> indices,
        VulkanUploadBatch* uploadBatch
    );

private:
    std::unique_ptr<VulkanBuffer> m_Buffer;
    u32 m_IndexCount = 0;
};

}
