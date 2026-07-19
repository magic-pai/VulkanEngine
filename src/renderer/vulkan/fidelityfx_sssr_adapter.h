#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanBuffer;
class VulkanDevice;
class VulkanPhysicalDevice;

class VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout {
public:
    explicit VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout(
        const VulkanDevice& device
    );
    ~VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout();

    SE_DISABLE_COPY(VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout);
    SE_DISABLE_MOVE(VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout);

    VkDescriptorSetLayout Handle() const;
    void Release();

private:
    void CreateDescriptorSetLayout(const VulkanDevice& device);

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
};

class VulkanFfxSssrPrepareIndirectArgsResources {
public:
    VulkanFfxSssrPrepareIndirectArgsResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout&
            descriptorSetLayout,
        std::size_t count
    );
    ~VulkanFfxSssrPrepareIndirectArgsResources();

    SE_DISABLE_COPY(VulkanFfxSssrPrepareIndirectArgsResources);
    SE_DISABLE_MOVE(VulkanFfxSssrPrepareIndirectArgsResources);

    VkDescriptorSet Handle(std::size_t imageIndex) const;
    VkBuffer RayCounterBuffer(std::size_t imageIndex) const;
    VkBuffer IndirectArgsBuffer(std::size_t imageIndex) const;
    std::size_t Count() const;
    VkDeviceSize RayCounterBufferSize() const;
    VkDeviceSize IndirectArgsBufferSize() const;
    u64 TotalMemoryBytes() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout&
            descriptorSetLayout,
        std::size_t count
    );
    void Release();

    static constexpr VkDeviceSize kRayCounterBufferSize = sizeof(u32) * 4u;
    static constexpr VkDeviceSize kIndirectArgsBufferSize = sizeof(u32) * 6u;

private:
    void CreateResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout&
            descriptorSetLayout,
        std::size_t count
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<VulkanBuffer>> m_RayCounterBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> m_IndirectArgsBuffers;
    std::vector<VkBufferView> m_RayCounterBufferViews;
    std::vector<VkBufferView> m_IndirectArgsBufferViews;
    std::vector<VkDescriptorSet> m_DescriptorSets;
};

}
