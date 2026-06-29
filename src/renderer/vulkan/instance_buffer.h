#pragma once

#include "renderer/vulkan/vertex.h"
#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanBuffer;
class VulkanDevice;
class VulkanPhysicalDevice;

class VulkanInstanceBuffer {
public:
    VulkanInstanceBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t frameCount
    );
    ~VulkanInstanceBuffer();

    SE_DISABLE_COPY(VulkanInstanceBuffer);
    SE_DISABLE_MOVE(VulkanInstanceBuffer);

    void Update(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t frameIndex,
        std::span<const Instance3D> instances
    );
    void Bind(VkCommandBuffer commandBuffer, std::size_t frameIndex) const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t frameCount
    );
    void Release();

private:
    VkBuffer Handle(std::size_t frameIndex) const;
    void EnsureCapacity(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t frameIndex,
        std::size_t instanceCount
    );

private:
    std::vector<std::unique_ptr<VulkanBuffer>> m_Buffers;
    std::vector<std::size_t> m_Capacities;
    std::vector<std::size_t> m_Counts;
};

}
