#include "renderer/vulkan/fidelityfx_sssr_adapter.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

namespace se {

VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout::
VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout::
~VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout
VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout::
CreateDescriptorSetLayout(const VulkanDevice& device) {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (u32 binding = 0; binding < bindings.size(); ++binding) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[binding].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<u32>(bindings.size());
    createInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(
            device.Handle(),
            &createInfo,
            nullptr,
            &m_DescriptorSetLayout
        ) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create Vulkan FidelityFX SSSR prepare-args descriptor set layout"
        );
    }
}

VulkanFfxSssrPrepareIndirectArgsResources::
VulkanFfxSssrPrepareIndirectArgsResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout&
        descriptorSetLayout,
    std::size_t count
) : m_Device(device.Handle()) {
    CreateResources(device, physicalDevice, descriptorSetLayout, count);
}

VulkanFfxSssrPrepareIndirectArgsResources::
~VulkanFfxSssrPrepareIndirectArgsResources() {
    Release();
}

VkDescriptorSet VulkanFfxSssrPrepareIndirectArgsResources::Handle(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DescriptorSets.size(),
        "FidelityFX SSSR prepare-args descriptor image index is out of range"
    );
    return m_DescriptorSets[imageIndex];
}

VkBuffer VulkanFfxSssrPrepareIndirectArgsResources::RayCounterBuffer(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_RayCounterBuffers.size(),
        "FidelityFX SSSR ray-counter image index is out of range"
    );
    return m_RayCounterBuffers[imageIndex]->Handle();
}

VkBuffer VulkanFfxSssrPrepareIndirectArgsResources::IndirectArgsBuffer(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_IndirectArgsBuffers.size(),
        "FidelityFX SSSR indirect-args image index is out of range"
    );
    return m_IndirectArgsBuffers[imageIndex]->Handle();
}

std::size_t VulkanFfxSssrPrepareIndirectArgsResources::Count() const {
    return m_DescriptorSets.size();
}

VkDeviceSize
VulkanFfxSssrPrepareIndirectArgsResources::RayCounterBufferSize() const {
    return kRayCounterBufferSize;
}

VkDeviceSize
VulkanFfxSssrPrepareIndirectArgsResources::IndirectArgsBufferSize() const {
    return kIndirectArgsBufferSize;
}

u64 VulkanFfxSssrPrepareIndirectArgsResources::TotalMemoryBytes() const {
    return static_cast<u64>(Count()) *
        static_cast<u64>(kRayCounterBufferSize + kIndirectArgsBufferSize);
}

void VulkanFfxSssrPrepareIndirectArgsResources::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout&
        descriptorSetLayout,
    std::size_t count
) {
    Release();
    m_Device = device.Handle();
    CreateResources(device, physicalDevice, descriptorSetLayout, count);
}

void VulkanFfxSssrPrepareIndirectArgsResources::Release() {
    m_DescriptorSets.clear();
    for (VkBufferView bufferView : m_IndirectArgsBufferViews) {
        if (bufferView != VK_NULL_HANDLE) {
            vkDestroyBufferView(m_Device, bufferView, nullptr);
        }
    }
    m_IndirectArgsBufferViews.clear();
    for (VkBufferView bufferView : m_RayCounterBufferViews) {
        if (bufferView != VK_NULL_HANDLE) {
            vkDestroyBufferView(m_Device, bufferView, nullptr);
        }
    }
    m_RayCounterBufferViews.clear();
    m_IndirectArgsBuffers.clear();
    m_RayCounterBuffers.clear();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanFfxSssrPrepareIndirectArgsResources::CreateResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout&
        descriptorSetLayout,
    std::size_t count
) {
    SE_ASSERT(count > 0, "FidelityFX SSSR resource count must be greater than zero");

    constexpr VkMemoryPropertyFlags memoryProperties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkBufferUsageFlags rayCounterUsage =
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const VkBufferUsageFlags indirectArgsUsage =
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    m_RayCounterBuffers.reserve(count);
    m_IndirectArgsBuffers.reserve(count);
    m_RayCounterBufferViews.reserve(count);
    m_IndirectArgsBufferViews.reserve(count);
    const std::array<u32, 4> zeroRayCounter{};
    const std::array<u32, 6> zeroIndirectArgs{};
    for (std::size_t index = 0; index < count; ++index) {
        auto rayCounter = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            kRayCounterBufferSize,
            rayCounterUsage,
            memoryProperties
        );
        rayCounter->Upload(std::as_bytes(std::span<const u32>(zeroRayCounter)));
        auto indirectArgs = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            kIndirectArgsBufferSize,
            indirectArgsUsage,
            memoryProperties
        );
        indirectArgs->Upload(std::as_bytes(std::span<const u32>(zeroIndirectArgs)));
        VkBufferViewCreateInfo rayCounterViewInfo{};
        rayCounterViewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        rayCounterViewInfo.buffer = rayCounter->Handle();
        rayCounterViewInfo.format = VK_FORMAT_R32_UINT;
        rayCounterViewInfo.offset = 0;
        rayCounterViewInfo.range = kRayCounterBufferSize;
        VkBufferView rayCounterView = VK_NULL_HANDLE;
        if (vkCreateBufferView(
                device.Handle(),
                &rayCounterViewInfo,
                nullptr,
                &rayCounterView
            ) != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to create Vulkan FidelityFX SSSR ray-counter buffer view"
            );
        }

        VkBufferViewCreateInfo indirectArgsViewInfo{};
        indirectArgsViewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        indirectArgsViewInfo.buffer = indirectArgs->Handle();
        indirectArgsViewInfo.format = VK_FORMAT_R32_UINT;
        indirectArgsViewInfo.offset = 0;
        indirectArgsViewInfo.range = kIndirectArgsBufferSize;
        VkBufferView indirectArgsView = VK_NULL_HANDLE;
        if (vkCreateBufferView(
                device.Handle(),
                &indirectArgsViewInfo,
                nullptr,
                &indirectArgsView
            ) != VK_SUCCESS) {
            vkDestroyBufferView(device.Handle(), rayCounterView, nullptr);
            throw std::runtime_error(
                "Failed to create Vulkan FidelityFX SSSR indirect-args buffer view"
            );
        }
        m_RayCounterBuffers.push_back(std::move(rayCounter));
        m_IndirectArgsBuffers.push_back(std::move(indirectArgs));
        m_RayCounterBufferViews.push_back(rayCounterView);
        m_IndirectArgsBufferViews.push_back(indirectArgsView);
    }

    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    poolSizes[0].descriptorCount = static_cast<u32>(count * 2u);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(count);
    if (vkCreateDescriptorPool(
            device.Handle(),
            &poolInfo,
            nullptr,
            &m_DescriptorPool
        ) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create Vulkan FidelityFX SSSR prepare-args descriptor pool"
        );
    }

    std::vector<VkDescriptorSetLayout> layouts(
        count,
        descriptorSetLayout.Handle()
    );
    m_DescriptorSets.resize(count);
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_DescriptorPool;
    allocateInfo.descriptorSetCount = static_cast<u32>(count);
    allocateInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(
            device.Handle(),
            &allocateInfo,
            m_DescriptorSets.data()
        ) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to allocate Vulkan FidelityFX SSSR prepare-args descriptor sets"
        );
    }

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        std::array<VkBufferView, 2> bufferViews{
            m_RayCounterBufferViews[imageIndex],
            m_IndirectArgsBufferViews[imageIndex]
        };

        std::array<VkWriteDescriptorSet, 2> writes{};
        for (u32 binding = 0; binding < writes.size(); ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType =
                VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            writes[binding].descriptorCount = 1;
            writes[binding].pTexelBufferView = &bufferViews[binding];
        }
        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(writes.size()),
            writes.data(),
            0,
            nullptr
        );
    }
}

}
