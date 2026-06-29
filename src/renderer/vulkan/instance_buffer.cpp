#include "renderer/vulkan/instance_buffer.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

namespace se {

namespace {

std::span<const std::byte> AsBytes(std::span<const Instance3D> instances) {
    return std::as_bytes(instances);
}

std::size_t NextCapacity(std::size_t required) {
    std::size_t capacity = 1;
    while (capacity < required) {
        capacity *= 2;
    }
    return capacity;
}

}

VulkanInstanceBuffer::VulkanInstanceBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t frameCount
) {
    Recreate(device, physicalDevice, frameCount);
}

VulkanInstanceBuffer::~VulkanInstanceBuffer() {
    Release();
}

void VulkanInstanceBuffer::Update(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t frameIndex,
    std::span<const Instance3D> instances
) {
    SE_ASSERT(frameIndex < m_Buffers.size(), "Instance buffer frame index is out of range");
    if (instances.empty()) {
        m_Counts[frameIndex] = 0;
        return;
    }

    EnsureCapacity(device, physicalDevice, frameIndex, instances.size());
    m_Buffers[frameIndex]->Upload(AsBytes(instances));
    m_Counts[frameIndex] = instances.size();
}

void VulkanInstanceBuffer::Bind(VkCommandBuffer commandBuffer, std::size_t frameIndex) const {
    const VkBuffer vertexBuffers[] = {
        Handle(frameIndex)
    };
    const VkDeviceSize offsets[] = {
        0
    };

    vkCmdBindVertexBuffers(commandBuffer, 1, 1, vertexBuffers, offsets);
}

void VulkanInstanceBuffer::Recreate(
    const VulkanDevice&,
    const VulkanPhysicalDevice&,
    std::size_t frameCount
) {
    Release();
    SE_ASSERT(frameCount > 0, "Instance buffer frame count must be greater than zero");
    m_Buffers.resize(frameCount);
    m_Capacities.assign(frameCount, 0);
    m_Counts.assign(frameCount, 0);
}

void VulkanInstanceBuffer::Release() {
    m_Buffers.clear();
    m_Capacities.clear();
    m_Counts.clear();
}

VkBuffer VulkanInstanceBuffer::Handle(std::size_t frameIndex) const {
    SE_ASSERT(frameIndex < m_Buffers.size(), "Instance buffer frame index is out of range");
    SE_ASSERT(m_Buffers[frameIndex] != nullptr, "Instance buffer was not allocated");
    return m_Buffers[frameIndex]->Handle();
}

void VulkanInstanceBuffer::EnsureCapacity(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t frameIndex,
    std::size_t instanceCount
) {
    if (m_Buffers[frameIndex] != nullptr && m_Capacities[frameIndex] >= instanceCount) {
        return;
    }

    const std::size_t capacity = NextCapacity(instanceCount);
    const VkDeviceSize bufferSize =
        static_cast<VkDeviceSize>(sizeof(Instance3D) * capacity);
    m_Buffers[frameIndex] = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    m_Capacities[frameIndex] = capacity;
}

}
