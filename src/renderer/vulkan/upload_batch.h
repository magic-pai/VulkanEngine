#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanBuffer;
class VulkanCommandPool;
class VulkanDevice;

class VulkanUploadBatch {
public:
    VulkanUploadBatch(
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool
    );
    ~VulkanUploadBatch();

    SE_DISABLE_COPY(VulkanUploadBatch);
    SE_DISABLE_MOVE(VulkanUploadBatch);

    VkCommandBuffer CommandBuffer() const;
    void KeepAlive(std::unique_ptr<VulkanBuffer> buffer);
    void Submit();
    void Cancel();

private:
    const VulkanDevice& m_Device;
    const VulkanCommandPool& m_CommandPool;
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    bool m_Closed = false;
    std::vector<std::unique_ptr<VulkanBuffer>> m_StagingBuffers;
};

}
