#include "renderer/vulkan/fidelityfx_sssr_adapter.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/image.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/render_targets.h"

#include <algorithm>
#include <limits>
#include <string>

namespace se {

namespace {

#include "../../../thirdParty/fidelityfx_sssr/data/blue_noise_tables_128x128_1spp.inl"

static_assert(sizeof(int) == sizeof(u32), "FFX blue-noise tables require 32-bit int storage");
static_assert(
    sizeof(sobol_256spp_256d) / sizeof(sobol_256spp_256d[0]) ==
        VulkanFfxSssrBlueNoiseResources::kSobolEntryCount,
    "FFX blue-noise Sobol table size must match the vendor shader contract"
);
static_assert(
    sizeof(rankingTile) / sizeof(rankingTile[0]) ==
        VulkanFfxSssrBlueNoiseResources::kTileEntryCount,
    "FFX blue-noise ranking table size must match the vendor shader contract"
);
static_assert(
    sizeof(scramblingTile) / sizeof(scramblingTile[0]) ==
        VulkanFfxSssrBlueNoiseResources::kTileEntryCount,
    "FFX blue-noise scrambling table size must match the vendor shader contract"
);

VkBufferView CreateR32UintBufferView(
    const VulkanDevice& device,
    VkBuffer buffer,
    VkDeviceSize range,
    const char* errorMessage
) {
    VkBufferViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    viewInfo.buffer = buffer;
    viewInfo.format = VK_FORMAT_R32_UINT;
    viewInfo.offset = 0;
    viewInfo.range = range;
    VkBufferView view = VK_NULL_HANDLE;
    if (vkCreateBufferView(device.Handle(), &viewInfo, nullptr, &view) !=
        VK_SUCCESS) {
        throw std::runtime_error(errorMessage);
    }
    return view;
}

std::unique_ptr<VulkanBuffer> CreateFfxBlueNoiseTableBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const int* table,
    u32 entryCount
) {
    const VkDeviceSize byteSize =
        static_cast<VkDeviceSize>(entryCount) * sizeof(u32);
    auto buffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        byteSize,
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    buffer->Upload(
        std::as_bytes(std::span<const int>(table, static_cast<std::size_t>(entryCount)))
    );
    return buffer;
}

bool FfxExtentsDiffer(const VkExtent2D& left, const VkExtent2D& right) {
    return left.width != right.width || left.height != right.height;
}

} // namespace

VulkanFfxSssrConstantsDescriptorSetLayout::
VulkanFfxSssrConstantsDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanFfxSssrConstantsDescriptorSetLayout::
~VulkanFfxSssrConstantsDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout
VulkanFfxSssrConstantsDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanFfxSssrConstantsDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanFfxSssrConstantsDescriptorSetLayout::CreateDescriptorSetLayout(
    const VulkanDevice& device
) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0u;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1u;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = 1u;
    createInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(
            device.Handle(),
            &createInfo,
            nullptr,
            &m_DescriptorSetLayout
        ) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create Vulkan FidelityFX SSSR constants descriptor set layout"
        );
    }
}

VulkanFfxSssrConstantsResources::VulkanFfxSssrConstantsResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanFfxSssrConstantsDescriptorSetLayout& descriptorSetLayout,
    std::size_t count
) : m_Device(device.Handle()) {
    CreateResources(device, physicalDevice, descriptorSetLayout, count);
}

VulkanFfxSssrConstantsResources::~VulkanFfxSssrConstantsResources() {
    Release();
}

VkDescriptorSet VulkanFfxSssrConstantsResources::Handle(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DescriptorSets.size(),
        "FidelityFX SSSR constants descriptor image index is out of range"
    );
    return m_DescriptorSets[imageIndex];
}

std::size_t VulkanFfxSssrConstantsResources::Count() const {
    return m_DescriptorSets.size();
}

u64 VulkanFfxSssrConstantsResources::TotalMemoryBytes() const {
    return static_cast<u64>(Count()) * sizeof(FfxSssrConstants);
}

void VulkanFfxSssrConstantsResources::Update(
    std::size_t imageIndex,
    const FfxSssrConstants& constants
) const {
    SE_ASSERT(
        imageIndex < m_Buffers.size(),
        "FidelityFX SSSR constants buffer image index is out of range"
    );
    m_Buffers[imageIndex]->Upload(
        std::as_bytes(std::span<const FfxSssrConstants>(&constants, 1u))
    );
}

void VulkanFfxSssrConstantsResources::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanFfxSssrConstantsDescriptorSetLayout& descriptorSetLayout,
    std::size_t count
) {
    Release();
    m_Device = device.Handle();
    CreateResources(device, physicalDevice, descriptorSetLayout, count);
}

void VulkanFfxSssrConstantsResources::Release() {
    m_DescriptorSets.clear();
    m_Buffers.clear();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanFfxSssrConstantsResources::CreateResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanFfxSssrConstantsDescriptorSetLayout& descriptorSetLayout,
    std::size_t count
) {
    SE_ASSERT(count > 0, "FidelityFX SSSR constants resource count must be greater than zero");

    constexpr VkMemoryPropertyFlags memoryProperties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    m_Buffers.reserve(count);
    const FfxSssrConstants zeroConstants{};
    for (std::size_t index = 0; index < count; ++index) {
        auto buffer = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            sizeof(FfxSssrConstants),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            memoryProperties
        );
        buffer->Upload(
            std::as_bytes(std::span<const FfxSssrConstants>(&zeroConstants, 1u))
        );
        m_Buffers.push_back(std::move(buffer));
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<u32>(count);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1u;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<u32>(count);
    if (vkCreateDescriptorPool(
            device.Handle(),
            &poolInfo,
            nullptr,
            &m_DescriptorPool
        ) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create Vulkan FidelityFX SSSR constants descriptor pool"
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
            "Failed to allocate Vulkan FidelityFX SSSR constants descriptor sets"
        );
    }

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_Buffers[imageIndex]->Handle();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(FfxSssrConstants);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_DescriptorSets[imageIndex];
        write.dstBinding = 0u;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1u;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device.Handle(), 1u, &write, 0u, nullptr);
    }
}

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

VkBufferView VulkanFfxSssrPrepareIndirectArgsResources::RayCounterBufferView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_RayCounterBufferViews.size(),
        "FidelityFX SSSR ray-counter buffer-view image index is out of range"
    );
    return m_RayCounterBufferViews[imageIndex];
}

std::array<u32, 4>
VulkanFfxSssrPrepareIndirectArgsResources::RayCounterValues(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_RayCounterBuffers.size(),
        "FidelityFX SSSR ray-counter readback image index is out of range"
    );
    std::array<u32, 4> values{};
    m_RayCounterBuffers[imageIndex]->Download(
        std::as_writable_bytes(std::span<u32>(values))
    );
    return values;
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
        VkBufferView rayCounterView = CreateR32UintBufferView(
            device,
            rayCounter->Handle(),
            kRayCounterBufferSize,
            "Failed to create Vulkan FidelityFX SSSR ray-counter buffer view"
        );
        VkBufferView indirectArgsView = VK_NULL_HANDLE;
        try {
            indirectArgsView = CreateR32UintBufferView(
                device,
                indirectArgs->Handle(),
                kIndirectArgsBufferSize,
                "Failed to create Vulkan FidelityFX SSSR indirect-args buffer view"
            );
        } catch (...) {
            vkDestroyBufferView(device.Handle(), rayCounterView, nullptr);
            throw;
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

VulkanFfxSssrBlueNoiseDescriptorSetLayout::
VulkanFfxSssrBlueNoiseDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanFfxSssrBlueNoiseDescriptorSetLayout::
~VulkanFfxSssrBlueNoiseDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout
VulkanFfxSssrBlueNoiseDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanFfxSssrBlueNoiseDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanFfxSssrBlueNoiseDescriptorSetLayout::CreateDescriptorSetLayout(
    const VulkanDevice& device
) {
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    auto setBinding = [&](u32 binding, VkDescriptorType type) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = type;
        bindings[binding].descriptorCount = 1u;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[binding].pImmutableSamplers = nullptr;
    };
    setBinding(0u, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    setBinding(1u, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    setBinding(2u, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    setBinding(3u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

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
            "Failed to create Vulkan FidelityFX SSSR blue-noise descriptor set layout"
        );
    }
}

VulkanFfxSssrBlueNoiseResources::VulkanFfxSssrBlueNoiseResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrBlueNoiseDescriptorSetLayout& descriptorSetLayout,
    std::size_t count
) : m_Device(device.Handle()) {
    CreateResources(device, physicalDevice, commandPool, descriptorSetLayout, count);
}

VulkanFfxSssrBlueNoiseResources::~VulkanFfxSssrBlueNoiseResources() {
    Release();
}

VkDescriptorSet VulkanFfxSssrBlueNoiseResources::Handle(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DescriptorSets.size(),
        "FidelityFX SSSR blue-noise descriptor image index is out of range"
    );
    return m_DescriptorSets[imageIndex];
}

VkImage VulkanFfxSssrBlueNoiseResources::BlueNoiseImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_BlueNoiseImages.size(),
        "FidelityFX SSSR blue-noise image index is out of range"
    );
    return m_BlueNoiseImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrBlueNoiseResources::BlueNoiseView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_BlueNoiseImages.size(),
        "FidelityFX SSSR blue-noise image-view index is out of range"
    );
    return m_BlueNoiseImages[imageIndex]->View();
}

std::size_t VulkanFfxSssrBlueNoiseResources::Count() const {
    return m_DescriptorSets.size();
}

VkExtent2D VulkanFfxSssrBlueNoiseResources::Extent() const {
    return m_Extent;
}

u32 VulkanFfxSssrBlueNoiseResources::GroupCountX() const {
    return (m_Extent.width + 7u) / 8u;
}

u32 VulkanFfxSssrBlueNoiseResources::GroupCountY() const {
    return (m_Extent.height + 7u) / 8u;
}

u32 VulkanFfxSssrBlueNoiseResources::SobolEntryCount() const {
    return kSobolEntryCount;
}

u32 VulkanFfxSssrBlueNoiseResources::RankingTileEntryCount() const {
    return kTileEntryCount;
}

u32 VulkanFfxSssrBlueNoiseResources::ScramblingTileEntryCount() const {
    return kTileEntryCount;
}

u64 VulkanFfxSssrBlueNoiseResources::TotalMemoryBytes() const {
    const u64 tableBytes =
        (static_cast<u64>(kSobolEntryCount) + 2ull * kTileEntryCount) *
        sizeof(u32);
    const u64 imageBytes =
        static_cast<u64>(Count()) *
        static_cast<u64>(m_Extent.width) *
        static_cast<u64>(m_Extent.height) *
        sizeof(f32) * 2ull;
    return tableBytes + imageBytes;
}

void VulkanFfxSssrBlueNoiseResources::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrBlueNoiseDescriptorSetLayout& descriptorSetLayout,
    std::size_t count
) {
    Release();
    m_Device = device.Handle();
    CreateResources(device, physicalDevice, commandPool, descriptorSetLayout, count);
}

void VulkanFfxSssrBlueNoiseResources::Release() {
    m_DescriptorSets.clear();
    m_BlueNoiseImages.clear();
    if (m_ScramblingTileBufferView != VK_NULL_HANDLE) {
        vkDestroyBufferView(m_Device, m_ScramblingTileBufferView, nullptr);
        m_ScramblingTileBufferView = VK_NULL_HANDLE;
    }
    if (m_RankingTileBufferView != VK_NULL_HANDLE) {
        vkDestroyBufferView(m_Device, m_RankingTileBufferView, nullptr);
        m_RankingTileBufferView = VK_NULL_HANDLE;
    }
    if (m_SobolBufferView != VK_NULL_HANDLE) {
        vkDestroyBufferView(m_Device, m_SobolBufferView, nullptr);
        m_SobolBufferView = VK_NULL_HANDLE;
    }
    m_ScramblingTileBuffer.reset();
    m_RankingTileBuffer.reset();
    m_SobolBuffer.reset();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    m_Extent = { kTextureSize, kTextureSize };
}

void VulkanFfxSssrBlueNoiseResources::CreateResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrBlueNoiseDescriptorSetLayout& descriptorSetLayout,
    std::size_t count
) {
    SE_ASSERT(count > 0, "FidelityFX SSSR blue-noise resource count must be greater than zero");

    m_Extent = { kTextureSize, kTextureSize };
    m_SobolBuffer = CreateFfxBlueNoiseTableBuffer(
        device,
        physicalDevice,
        sobol_256spp_256d,
        kSobolEntryCount
    );
    m_RankingTileBuffer = CreateFfxBlueNoiseTableBuffer(
        device,
        physicalDevice,
        rankingTile,
        kTileEntryCount
    );
    m_ScramblingTileBuffer = CreateFfxBlueNoiseTableBuffer(
        device,
        physicalDevice,
        scramblingTile,
        kTileEntryCount
    );
    m_SobolBufferView = CreateR32UintBufferView(
        device,
        m_SobolBuffer->Handle(),
        static_cast<VkDeviceSize>(kSobolEntryCount) * sizeof(u32),
        "Failed to create Vulkan FidelityFX SSSR Sobol buffer view"
    );
    m_RankingTileBufferView = CreateR32UintBufferView(
        device,
        m_RankingTileBuffer->Handle(),
        static_cast<VkDeviceSize>(kTileEntryCount) * sizeof(u32),
        "Failed to create Vulkan FidelityFX SSSR ranking-tile buffer view"
    );
    m_ScramblingTileBufferView = CreateR32UintBufferView(
        device,
        m_ScramblingTileBuffer->Handle(),
        static_cast<VkDeviceSize>(kTileEntryCount) * sizeof(u32),
        "Failed to create Vulkan FidelityFX SSSR scrambling-tile buffer view"
    );

    m_BlueNoiseImages.reserve(count);
    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        auto blueNoise = std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            m_Extent,
            VK_FORMAT_R32G32_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        blueNoise->TransitionLayout(
            device,
            commandPool,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL
        );
        m_BlueNoiseImages.push_back(std::move(blueNoise));
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[0].descriptorCount = static_cast<u32>(count * 3u);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = static_cast<u32>(count);

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
            "Failed to create Vulkan FidelityFX SSSR blue-noise descriptor pool"
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
            "Failed to allocate Vulkan FidelityFX SSSR blue-noise descriptor sets"
        );
    }

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        std::array<VkBufferView, 3> bufferViews{
            m_SobolBufferView,
            m_RankingTileBufferView,
            m_ScramblingTileBufferView
        };

        VkDescriptorImageInfo storageImage{};
        storageImage.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImage.imageView = m_BlueNoiseImages[imageIndex]->View();

        std::array<VkWriteDescriptorSet, 4> writes{};
        for (u32 binding = 0u; binding < 3u; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType =
                VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            writes[binding].descriptorCount = 1u;
            writes[binding].pTexelBufferView = &bufferViews[binding];
        }
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = m_DescriptorSets[imageIndex];
        writes[3].dstBinding = 3u;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[3].descriptorCount = 1u;
        writes[3].pImageInfo = &storageImage;

        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(writes.size()),
            writes.data(),
            0u,
            nullptr
        );
    }
}

VulkanFfxSssrClassifyTilesDescriptorSetLayout::
VulkanFfxSssrClassifyTilesDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanFfxSssrClassifyTilesDescriptorSetLayout::
~VulkanFfxSssrClassifyTilesDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout
VulkanFfxSssrClassifyTilesDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanFfxSssrClassifyTilesDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanFfxSssrClassifyTilesDescriptorSetLayout::CreateDescriptorSetLayout(
    const VulkanDevice& device
) {
    std::array<VkDescriptorSetLayoutBinding, 11> bindings{};
    auto setBinding = [&](u32 binding, VkDescriptorType type) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = type;
        bindings[binding].descriptorCount = 1u;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[binding].pImmutableSamplers = nullptr;
    };

    setBinding(0u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    setBinding(1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    setBinding(2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    setBinding(3u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    setBinding(4u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    setBinding(5u, VK_DESCRIPTOR_TYPE_SAMPLER);
    setBinding(6u, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
    setBinding(7u, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
    setBinding(8u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    setBinding(9u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    setBinding(10u, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);

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
            "Failed to create Vulkan FidelityFX SSSR classify-tiles descriptor set layout"
        );
    }
}

VulkanFfxSssrClassifyTilesResources::VulkanFfxSssrClassifyTilesResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrClassifyTilesDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkImageView environmentMapView,
    VkSampler environmentMapSampler
) : m_Device(device.Handle()) {
    CreateResources(
        device,
        physicalDevice,
        commandPool,
        descriptorSetLayout,
        prepareResources,
        renderTargets,
        environmentMapView,
        environmentMapSampler
    );
}

VulkanFfxSssrClassifyTilesResources::~VulkanFfxSssrClassifyTilesResources() {
    Release();
}

VkDescriptorSet VulkanFfxSssrClassifyTilesResources::Handle(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DescriptorSets.size(),
        "FidelityFX SSSR classify-tiles descriptor image index is out of range"
    );
    return m_DescriptorSets[imageIndex];
}

VkBuffer VulkanFfxSssrClassifyTilesResources::RayListBuffer(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_RayListBuffers.size(),
        "FidelityFX SSSR ray-list image index is out of range"
    );
    return m_RayListBuffers[imageIndex]->Handle();
}

VkBufferView VulkanFfxSssrClassifyTilesResources::RayListBufferView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_RayListBufferViews.size(),
        "FidelityFX SSSR ray-list buffer-view image index is out of range"
    );
    return m_RayListBufferViews[imageIndex];
}

VkBuffer VulkanFfxSssrClassifyTilesResources::DenoiserTileListBuffer(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DenoiserTileListBuffers.size(),
        "FidelityFX SSSR denoiser tile-list image index is out of range"
    );
    return m_DenoiserTileListBuffers[imageIndex]->Handle();
}

VkBufferView VulkanFfxSssrClassifyTilesResources::DenoiserTileListBufferView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DenoiserTileListBufferViews.size(),
        "FidelityFX SSSR denoiser tile-list buffer-view image index is out of range"
    );
    return m_DenoiserTileListBufferViews[imageIndex];
}

VkImage VulkanFfxSssrClassifyTilesResources::IntersectionOutputImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_IntersectionOutputImages.size(),
        "FidelityFX SSSR intersection-output image index is out of range"
    );
    return m_IntersectionOutputImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrClassifyTilesResources::IntersectionOutputView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_IntersectionOutputImages.size(),
        "FidelityFX SSSR intersection-output image-view index is out of range"
    );
    return m_IntersectionOutputImages[imageIndex]->View();
}

VkImage VulkanFfxSssrClassifyTilesResources::ExtractedRoughnessImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_ExtractedRoughnessImages.size(),
        "FidelityFX SSSR extracted-roughness image index is out of range"
    );
    return m_ExtractedRoughnessImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrClassifyTilesResources::ExtractedRoughnessView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_ExtractedRoughnessImages.size(),
        "FidelityFX SSSR extracted-roughness image-view index is out of range"
    );
    return m_ExtractedRoughnessImages[imageIndex]->View();
}

std::size_t VulkanFfxSssrClassifyTilesResources::Count() const {
    return m_DescriptorSets.size();
}

VkExtent2D VulkanFfxSssrClassifyTilesResources::Extent() const {
    return m_Extent;
}

u32 VulkanFfxSssrClassifyTilesResources::GroupCountX() const {
    return m_GroupCountX;
}

u32 VulkanFfxSssrClassifyTilesResources::GroupCountY() const {
    return m_GroupCountY;
}

u32 VulkanFfxSssrClassifyTilesResources::RayListCapacity() const {
    return m_RayListCapacity;
}

u32 VulkanFfxSssrClassifyTilesResources::DenoiserTileListCapacity() const {
    return m_DenoiserTileListCapacity;
}

VkDeviceSize VulkanFfxSssrClassifyTilesResources::RayListBufferSize() const {
    return m_RayListBufferSize;
}

VkDeviceSize
VulkanFfxSssrClassifyTilesResources::DenoiserTileListBufferSize() const {
    return m_DenoiserTileListBufferSize;
}

u64 VulkanFfxSssrClassifyTilesResources::TotalMemoryBytes() const {
    const u64 pixelCount =
        static_cast<u64>(m_Extent.width) * static_cast<u64>(m_Extent.height);
    const u64 imageBytes = pixelCount * (sizeof(f32) * 4u + sizeof(f32) * 2u);
    return static_cast<u64>(Count()) *
        (static_cast<u64>(m_RayListBufferSize) +
            static_cast<u64>(m_DenoiserTileListBufferSize) +
            imageBytes);
}

void VulkanFfxSssrClassifyTilesResources::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrClassifyTilesDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkImageView environmentMapView,
    VkSampler environmentMapSampler
) {
    Release();
    m_Device = device.Handle();
    CreateResources(
        device,
        physicalDevice,
        commandPool,
        descriptorSetLayout,
        prepareResources,
        renderTargets,
        environmentMapView,
        environmentMapSampler
    );
}

void VulkanFfxSssrClassifyTilesResources::Release() {
    m_DescriptorSets.clear();
    m_VarianceHistoryImages.clear();
    m_ExtractedRoughnessImages.clear();
    m_IntersectionOutputImages.clear();
    for (VkBufferView view : m_DenoiserTileListBufferViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyBufferView(m_Device, view, nullptr);
        }
    }
    m_DenoiserTileListBufferViews.clear();
    for (VkBufferView view : m_RayListBufferViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyBufferView(m_Device, view, nullptr);
        }
    }
    m_RayListBufferViews.clear();
    m_DenoiserTileListBuffers.clear();
    m_RayListBuffers.clear();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    m_Extent = {};
    m_GroupCountX = 0u;
    m_GroupCountY = 0u;
    m_RayListCapacity = 0u;
    m_DenoiserTileListCapacity = 0u;
    m_RayListBufferSize = 0;
    m_DenoiserTileListBufferSize = 0;
}

void VulkanFfxSssrClassifyTilesResources::CreateResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrClassifyTilesDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkImageView environmentMapView,
    VkSampler environmentMapSampler
) {
    const std::size_t count = renderTargets.Count();
    SE_ASSERT(count > 0, "FidelityFX SSSR classify resource count must be greater than zero");
    SE_ASSERT(
        prepareResources.Count() == count,
        "FidelityFX SSSR classify resources must match prepare resources"
    );
    SE_ASSERT(
        environmentMapView != VK_NULL_HANDLE &&
            environmentMapSampler != VK_NULL_HANDLE,
        "FidelityFX SSSR classify resources require a valid environment map"
    );

    m_Extent = renderTargets.Extent();
    if (m_Extent.width == 0u || m_Extent.height == 0u) {
        throw std::runtime_error(
            "FidelityFX SSSR classify resources require a non-empty render extent"
        );
    }

    const u64 pixelCount =
        static_cast<u64>(m_Extent.width) * static_cast<u64>(m_Extent.height);
    if (pixelCount > std::numeric_limits<u32>::max()) {
        throw std::runtime_error(
            "FidelityFX SSSR classify render extent exceeds 32-bit ray-list capacity"
        );
    }
    m_GroupCountX = (m_Extent.width + 7u) / 8u;
    m_GroupCountY = (m_Extent.height + 7u) / 8u;
    m_RayListCapacity = std::max(1u, static_cast<u32>(pixelCount));
    m_DenoiserTileListCapacity = std::max(1u, m_GroupCountX * m_GroupCountY);
    m_RayListBufferSize =
        static_cast<VkDeviceSize>(m_RayListCapacity) * sizeof(u32);
    m_DenoiserTileListBufferSize =
        static_cast<VkDeviceSize>(m_DenoiserTileListCapacity) * sizeof(u32);

    const VkBufferUsageFlags listUsage =
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    constexpr VkMemoryPropertyFlags listMemory =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    m_RayListBuffers.reserve(count);
    m_DenoiserTileListBuffers.reserve(count);
    m_RayListBufferViews.reserve(count);
    m_DenoiserTileListBufferViews.reserve(count);
    m_IntersectionOutputImages.reserve(count);
    m_ExtractedRoughnessImages.reserve(count);
    m_VarianceHistoryImages.reserve(count);

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        auto rayList = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            m_RayListBufferSize,
            listUsage,
            listMemory
        );
        auto denoiserTileList = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            m_DenoiserTileListBufferSize,
            listUsage,
            listMemory
        );
        VkBufferView rayListView = CreateR32UintBufferView(
            device,
            rayList->Handle(),
            m_RayListBufferSize,
            "Failed to create Vulkan FidelityFX SSSR ray-list buffer view"
        );
        VkBufferView denoiserTileListView = VK_NULL_HANDLE;
        try {
            denoiserTileListView = CreateR32UintBufferView(
                device,
                denoiserTileList->Handle(),
                m_DenoiserTileListBufferSize,
                "Failed to create Vulkan FidelityFX SSSR denoiser tile-list buffer view"
            );
        } catch (...) {
            vkDestroyBufferView(device.Handle(), rayListView, nullptr);
            throw;
        }

        auto intersectionOutput = std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            m_Extent,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        intersectionOutput->TransitionLayout(
            device,
            commandPool,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL
        );

        auto extractedRoughness = std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            m_Extent,
            VK_FORMAT_R32_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        extractedRoughness->TransitionLayout(
            device,
            commandPool,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL
        );

        auto varianceHistory = std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            m_Extent,
            VK_FORMAT_R32_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        varianceHistory->TransitionLayout(
            device,
            commandPool,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL
        );

        m_RayListBuffers.push_back(std::move(rayList));
        m_DenoiserTileListBuffers.push_back(std::move(denoiserTileList));
        m_RayListBufferViews.push_back(rayListView);
        m_DenoiserTileListBufferViews.push_back(denoiserTileListView);
        m_IntersectionOutputImages.push_back(std::move(intersectionOutput));
        m_ExtractedRoughnessImages.push_back(std::move(extractedRoughness));
        m_VarianceHistoryImages.push_back(std::move(varianceHistory));
    }

    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[0].descriptorCount = static_cast<u32>(count * 5u);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<u32>(count);
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    poolSizes[2].descriptorCount = static_cast<u32>(count * 3u);
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[3].descriptorCount = static_cast<u32>(count * 2u);

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
            "Failed to create Vulkan FidelityFX SSSR classify-tiles descriptor pool"
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
            "Failed to allocate Vulkan FidelityFX SSSR classify-tiles descriptor sets"
        );
    }

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        std::array<VkDescriptorImageInfo, 5> sampledImages{};
        sampledImages[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImages[0].imageView =
            renderTargets.GBufferNormalRoughnessView(imageIndex);
        sampledImages[1].imageLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        sampledImages[1].imageView = renderTargets.SceneDepthView(imageIndex);
        sampledImages[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[2].imageView = m_VarianceHistoryImages[imageIndex]->View();
        sampledImages[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImages[3].imageView =
            renderTargets.GBufferNormalRoughnessView(imageIndex);
        sampledImages[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImages[4].imageView = environmentMapView;

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = environmentMapSampler;

        std::array<VkBufferView, 3> texelBufferViews{
            m_RayListBufferViews[imageIndex],
            prepareResources.RayCounterBufferView(imageIndex),
            m_DenoiserTileListBufferViews[imageIndex]
        };

        std::array<VkDescriptorImageInfo, 2> storageImages{};
        storageImages[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[0].imageView =
            m_IntersectionOutputImages[imageIndex]->View();
        storageImages[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[1].imageView =
            m_ExtractedRoughnessImages[imageIndex]->View();

        std::array<VkWriteDescriptorSet, 11> writes{};
        for (u32 binding = 0u; binding <= 4u; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[binding].descriptorCount = 1u;
            writes[binding].pImageInfo = &sampledImages[binding];
        }

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = m_DescriptorSets[imageIndex];
        writes[5].dstBinding = 5u;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[5].descriptorCount = 1u;
        writes[5].pImageInfo = &samplerInfo;

        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = m_DescriptorSets[imageIndex];
        writes[6].dstBinding = 6u;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        writes[6].descriptorCount = 1u;
        writes[6].pTexelBufferView = &texelBufferViews[0];

        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = m_DescriptorSets[imageIndex];
        writes[7].dstBinding = 7u;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        writes[7].descriptorCount = 1u;
        writes[7].pTexelBufferView = &texelBufferViews[1];

        writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet = m_DescriptorSets[imageIndex];
        writes[8].dstBinding = 8u;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[8].descriptorCount = 1u;
        writes[8].pImageInfo = &storageImages[0];

        writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[9].dstSet = m_DescriptorSets[imageIndex];
        writes[9].dstBinding = 9u;
        writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[9].descriptorCount = 1u;
        writes[9].pImageInfo = &storageImages[1];

        writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[10].dstSet = m_DescriptorSets[imageIndex];
        writes[10].dstBinding = 10u;
        writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        writes[10].descriptorCount = 1u;
        writes[10].pTexelBufferView = &texelBufferViews[2];

        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(writes.size()),
            writes.data(),
            0u,
            nullptr
        );
    }
}

VulkanFfxSssrIntersectDescriptorSetLayout::
VulkanFfxSssrIntersectDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanFfxSssrIntersectDescriptorSetLayout::
~VulkanFfxSssrIntersectDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout VulkanFfxSssrIntersectDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanFfxSssrIntersectDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanFfxSssrIntersectDescriptorSetLayout::CreateDescriptorSetLayout(
    const VulkanDevice& device
) {
    std::array<VkDescriptorSetLayoutBinding, 10> bindings{};
    auto setBinding = [&](u32 binding, VkDescriptorType type) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = type;
        bindings[binding].descriptorCount = 1u;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[binding].pImmutableSamplers = nullptr;
    };
    for (u32 binding = 0u; binding <= 5u; ++binding) {
        setBinding(binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    }
    setBinding(6u, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    setBinding(7u, VK_DESCRIPTOR_TYPE_SAMPLER);
    setBinding(8u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    setBinding(9u, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);

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
            "Failed to create Vulkan FidelityFX SSSR intersect descriptor set layout"
        );
    }
}

VulkanFfxSssrIntersectResources::VulkanFfxSssrIntersectResources(
    const VulkanDevice& device,
    const VulkanFfxSssrIntersectDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
    const VulkanFfxSssrBlueNoiseResources& blueNoiseResources,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanDepthPyramid& depthPyramid,
    VkImageView environmentMapView,
    VkSampler environmentMapSampler
) : m_Device(device.Handle()) {
    CreateResources(
        device,
        descriptorSetLayout,
        classifyResources,
        prepareResources,
        blueNoiseResources,
        renderTargets,
        depthPyramid,
        environmentMapView,
        environmentMapSampler
    );
}

VulkanFfxSssrIntersectResources::~VulkanFfxSssrIntersectResources() {
    Release();
}

VkDescriptorSet VulkanFfxSssrIntersectResources::Handle(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DescriptorSets.size(),
        "FidelityFX SSSR intersect descriptor image index is out of range"
    );
    return m_DescriptorSets[imageIndex];
}

std::size_t VulkanFfxSssrIntersectResources::Count() const {
    return m_DescriptorSets.size();
}

VkExtent2D VulkanFfxSssrIntersectResources::Extent() const {
    return m_Extent;
}

u32 VulkanFfxSssrIntersectResources::DepthPyramidMipCount() const {
    return m_DepthPyramidMipCount;
}

void VulkanFfxSssrIntersectResources::Recreate(
    const VulkanDevice& device,
    const VulkanFfxSssrIntersectDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
    const VulkanFfxSssrBlueNoiseResources& blueNoiseResources,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanDepthPyramid& depthPyramid,
    VkImageView environmentMapView,
    VkSampler environmentMapSampler
) {
    Release();
    m_Device = device.Handle();
    CreateResources(
        device,
        descriptorSetLayout,
        classifyResources,
        prepareResources,
        blueNoiseResources,
        renderTargets,
        depthPyramid,
        environmentMapView,
        environmentMapSampler
    );
}

void VulkanFfxSssrIntersectResources::Release() {
    m_DescriptorSets.clear();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    m_Extent = {};
    m_DepthPyramidMipCount = 0u;
}

void VulkanFfxSssrIntersectResources::CreateResources(
    const VulkanDevice& device,
    const VulkanFfxSssrIntersectDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
    const VulkanFfxSssrBlueNoiseResources& blueNoiseResources,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanDepthPyramid& depthPyramid,
    VkImageView environmentMapView,
    VkSampler environmentMapSampler
) {
    const std::size_t count = renderTargets.Count();
    SE_ASSERT(count > 0, "FidelityFX SSSR intersect resource count must be greater than zero");
    SE_ASSERT(
        classifyResources.Count() == count &&
            prepareResources.Count() == count &&
            blueNoiseResources.Count() == count &&
            depthPyramid.Count() == count,
        "FidelityFX SSSR intersect resources require matching per-frame producers"
    );
    SE_ASSERT(
        environmentMapView != VK_NULL_HANDLE &&
            environmentMapSampler != VK_NULL_HANDLE,
        "FidelityFX SSSR intersect resources require a valid environment map"
    );

    m_Extent = renderTargets.Extent();
    if (m_Extent.width == 0u ||
        m_Extent.height == 0u ||
        depthPyramid.MipCount() == 0u ||
        FfxExtentsDiffer(m_Extent, classifyResources.Extent()) ||
        FfxExtentsDiffer(m_Extent, depthPyramid.Extent())) {
        throw std::runtime_error(
            "FidelityFX SSSR intersect resources require matching render, classify, and depth-pyramid extents"
        );
    }
    m_DepthPyramidMipCount = depthPyramid.MipCount();

    std::array<VkDescriptorPoolSize, 5> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[0].descriptorCount = static_cast<u32>(count * 6u);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[1].descriptorCount = static_cast<u32>(count);
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[2].descriptorCount = static_cast<u32>(count);
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[3].descriptorCount = static_cast<u32>(count);
    poolSizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    poolSizes[4].descriptorCount = static_cast<u32>(count);

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
            "Failed to create Vulkan FidelityFX SSSR intersect descriptor pool"
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
            "Failed to allocate Vulkan FidelityFX SSSR intersect descriptor sets"
        );
    }

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        std::array<VkDescriptorImageInfo, 6> sampledImages{};
        sampledImages[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImages[0].imageView =
            renderTargets.HdrSceneColorAttachmentView(imageIndex);
        sampledImages[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[1].imageView = depthPyramid.View(imageIndex);
        sampledImages[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImages[2].imageView =
            renderTargets.GBufferNormalRoughnessView(imageIndex);
        sampledImages[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[3].imageView =
            classifyResources.ExtractedRoughnessView(imageIndex);
        sampledImages[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImages[4].imageView = environmentMapView;
        sampledImages[5].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[5].imageView = blueNoiseResources.BlueNoiseView(imageIndex);

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = environmentMapSampler;

        VkBufferView rayListBufferView =
            classifyResources.RayListBufferView(imageIndex);
        VkBufferView rayCounterBufferView =
            prepareResources.RayCounterBufferView(imageIndex);

        VkDescriptorImageInfo intersectionOutput{};
        intersectionOutput.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        intersectionOutput.imageView =
            classifyResources.IntersectionOutputView(imageIndex);

        std::array<VkWriteDescriptorSet, 10> writes{};
        for (u32 binding = 0u; binding <= 5u; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[binding].descriptorCount = 1u;
            writes[binding].pImageInfo = &sampledImages[binding];
        }
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = m_DescriptorSets[imageIndex];
        writes[6].dstBinding = 6u;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        writes[6].descriptorCount = 1u;
        writes[6].pTexelBufferView = &rayListBufferView;

        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = m_DescriptorSets[imageIndex];
        writes[7].dstBinding = 7u;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[7].descriptorCount = 1u;
        writes[7].pImageInfo = &samplerInfo;

        writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet = m_DescriptorSets[imageIndex];
        writes[8].dstBinding = 8u;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[8].descriptorCount = 1u;
        writes[8].pImageInfo = &intersectionOutput;

        writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[9].dstSet = m_DescriptorSets[imageIndex];
        writes[9].dstBinding = 9u;
        writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        writes[9].descriptorCount = 1u;
        writes[9].pTexelBufferView = &rayCounterBufferView;

        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(writes.size()),
            writes.data(),
            0u,
            nullptr
        );
    }
}

VulkanFfxSssrReprojectDescriptorSetLayout::
VulkanFfxSssrReprojectDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanFfxSssrReprojectDescriptorSetLayout::
~VulkanFfxSssrReprojectDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout VulkanFfxSssrReprojectDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanFfxSssrReprojectDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanFfxSssrReprojectDescriptorSetLayout::CreateDescriptorSetLayout(
    const VulkanDevice& device
) {
    std::array<VkDescriptorSetLayoutBinding, 19> bindings{};
    auto setBinding = [&](u32 binding, VkDescriptorType type) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = type;
        bindings[binding].descriptorCount = 1u;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[binding].pImmutableSamplers = nullptr;
    };
    for (u32 binding = 0u; binding <= 12u; ++binding) {
        setBinding(binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    }
    setBinding(13u, VK_DESCRIPTOR_TYPE_SAMPLER);
    for (u32 binding = 14u; binding <= 17u; ++binding) {
        setBinding(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }
    setBinding(18u, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);

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
            "Failed to create Vulkan FidelityFX SSSR reproject descriptor set layout"
        );
    }
}

VulkanFfxSssrReprojectResources::VulkanFfxSssrReprojectResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrReprojectDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrBlueNoiseResources& blueNoiseResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkSampler linearSampler
) : m_Device(device.Handle()) {
    CreateResources(
        device,
        physicalDevice,
        commandPool,
        descriptorSetLayout,
        classifyResources,
        blueNoiseResources,
        renderTargets,
        linearSampler
    );
}

VulkanFfxSssrReprojectResources::~VulkanFfxSssrReprojectResources() {
    Release();
}

VkDescriptorSet VulkanFfxSssrReprojectResources::Handle(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DescriptorSets.size(),
        "FidelityFX SSSR reproject descriptor image index is out of range"
    );
    return m_DescriptorSets[imageIndex];
}

VkImage VulkanFfxSssrReprojectResources::ReprojectedRadianceImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_ReprojectedRadianceImages.size(),
        "FidelityFX SSSR reprojected-radiance image index is out of range"
    );
    return m_ReprojectedRadianceImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrReprojectResources::ReprojectedRadianceView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_ReprojectedRadianceImages.size(),
        "FidelityFX SSSR reprojected-radiance view image index is out of range"
    );
    return m_ReprojectedRadianceImages[imageIndex]->View();
}

VkImage VulkanFfxSssrReprojectResources::AverageRadianceImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_AverageRadianceImages.size(),
        "FidelityFX SSSR average-radiance image index is out of range"
    );
    return m_AverageRadianceImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrReprojectResources::AverageRadianceView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_AverageRadianceImages.size(),
        "FidelityFX SSSR average-radiance view image index is out of range"
    );
    return m_AverageRadianceImages[imageIndex]->View();
}

VkImage VulkanFfxSssrReprojectResources::VarianceImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_VarianceImages.size(),
        "FidelityFX SSSR variance image index is out of range"
    );
    return m_VarianceImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrReprojectResources::VarianceView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_VarianceImages.size(),
        "FidelityFX SSSR variance view image index is out of range"
    );
    return m_VarianceImages[imageIndex]->View();
}

VkImage VulkanFfxSssrReprojectResources::SampleCountImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_SampleCountImages.size(),
        "FidelityFX SSSR sample-count image index is out of range"
    );
    return m_SampleCountImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrReprojectResources::SampleCountView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_SampleCountImages.size(),
        "FidelityFX SSSR sample-count view image index is out of range"
    );
    return m_SampleCountImages[imageIndex]->View();
}

VkImage VulkanFfxSssrReprojectResources::RadianceHistoryImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_RadianceHistoryImages.size(),
        "FidelityFX SSSR radiance-history image index is out of range"
    );
    return m_RadianceHistoryImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrReprojectResources::RadianceHistoryView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_RadianceHistoryImages.size(),
        "FidelityFX SSSR radiance-history view image index is out of range"
    );
    return m_RadianceHistoryImages[imageIndex]->View();
}

VkImage VulkanFfxSssrReprojectResources::AverageRadianceHistoryImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_AverageRadianceHistoryImages.size(),
        "FidelityFX SSSR average-radiance-history image index is out of range"
    );
    return m_AverageRadianceHistoryImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrReprojectResources::AverageRadianceHistoryView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_AverageRadianceHistoryImages.size(),
        "FidelityFX SSSR average-radiance-history view image index is out of range"
    );
    return m_AverageRadianceHistoryImages[imageIndex]->View();
}

VkImage VulkanFfxSssrReprojectResources::VarianceHistoryImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_VarianceHistoryImages.size(),
        "FidelityFX SSSR variance-history image index is out of range"
    );
    return m_VarianceHistoryImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrReprojectResources::VarianceHistoryView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_VarianceHistoryImages.size(),
        "FidelityFX SSSR variance-history view image index is out of range"
    );
    return m_VarianceHistoryImages[imageIndex]->View();
}

VkImage VulkanFfxSssrReprojectResources::SampleCountHistoryImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_SampleCountHistoryImages.size(),
        "FidelityFX SSSR sample-count-history image index is out of range"
    );
    return m_SampleCountHistoryImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrReprojectResources::SampleCountHistoryView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_SampleCountHistoryImages.size(),
        "FidelityFX SSSR sample-count-history view image index is out of range"
    );
    return m_SampleCountHistoryImages[imageIndex]->View();
}

std::size_t VulkanFfxSssrReprojectResources::Count() const {
    return m_DescriptorSets.size();
}

VkExtent2D VulkanFfxSssrReprojectResources::Extent() const {
    return m_Extent;
}

VkExtent2D VulkanFfxSssrReprojectResources::AverageExtent() const {
    return m_AverageExtent;
}

u64 VulkanFfxSssrReprojectResources::TotalMemoryBytes() const {
    const u64 fullPixels =
        static_cast<u64>(m_Extent.width) * static_cast<u64>(m_Extent.height);
    const u64 averagePixels =
        static_cast<u64>(m_AverageExtent.width) *
        static_cast<u64>(m_AverageExtent.height);
    const u64 fullBytes =
        fullPixels *
        (sizeof(f32) * 4ull * 2ull + sizeof(f32) * 4ull);
    const u64 averageBytes =
        averagePixels * sizeof(f32) * 4ull * 2ull;
    return static_cast<u64>(Count()) * (fullBytes + averageBytes);
}

void VulkanFfxSssrReprojectResources::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrReprojectDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrBlueNoiseResources& blueNoiseResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkSampler linearSampler
) {
    Release();
    m_Device = device.Handle();
    CreateResources(
        device,
        physicalDevice,
        commandPool,
        descriptorSetLayout,
        classifyResources,
        blueNoiseResources,
        renderTargets,
        linearSampler
    );
}

void VulkanFfxSssrReprojectResources::Release() {
    m_DescriptorSets.clear();
    m_SampleCountImages.clear();
    m_VarianceImages.clear();
    m_AverageRadianceImages.clear();
    m_ReprojectedRadianceImages.clear();
    m_SampleCountHistoryImages.clear();
    m_VarianceHistoryImages.clear();
    m_AverageRadianceHistoryImages.clear();
    m_RadianceHistoryImages.clear();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    m_Extent = {};
    m_AverageExtent = {};
}

void VulkanFfxSssrReprojectResources::CreateResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrReprojectDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrBlueNoiseResources& blueNoiseResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkSampler linearSampler
) {
    const std::size_t count = renderTargets.Count();
    SE_ASSERT(count > 0, "FidelityFX SSSR reproject resource count must be greater than zero");
    SE_ASSERT(
        classifyResources.Count() == count &&
            blueNoiseResources.Count() == count,
        "FidelityFX SSSR reproject resources require matching per-frame producers"
    );
    SE_ASSERT(
        linearSampler != VK_NULL_HANDLE,
        "FidelityFX SSSR reproject resources require a valid linear sampler"
    );

    m_Extent = renderTargets.Extent();
    if (m_Extent.width == 0u ||
        m_Extent.height == 0u ||
        FfxExtentsDiffer(m_Extent, classifyResources.Extent())) {
        throw std::runtime_error(
            "FidelityFX SSSR reproject resources require matching render and classify extents"
        );
    }
    m_AverageExtent = {
        std::max(1u, classifyResources.GroupCountX()),
        std::max(1u, classifyResources.GroupCountY())
    };

    const VkImageUsageFlags imageUsage =
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto makeImage = [&](
        VkExtent2D extent,
        VkFormat format,
        const char* errorPrefix
    ) {
        auto image = std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            extent,
            format,
            VK_IMAGE_TILING_OPTIMAL,
            imageUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        try {
            image->TransitionLayout(
                device,
                commandPool,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL
            );
        } catch (const std::exception& exception) {
            throw std::runtime_error(
                std::string(errorPrefix) + ": " + exception.what()
            );
        }
        return image;
    };

    m_RadianceHistoryImages.reserve(count);
    m_AverageRadianceHistoryImages.reserve(count);
    m_VarianceHistoryImages.reserve(count);
    m_SampleCountHistoryImages.reserve(count);
    m_ReprojectedRadianceImages.reserve(count);
    m_AverageRadianceImages.reserve(count);
    m_VarianceImages.reserve(count);
    m_SampleCountImages.reserve(count);

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        m_RadianceHistoryImages.push_back(makeImage(
            m_Extent,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            "Failed to initialize FidelityFX SSSR radiance-history image"
        ));
        m_AverageRadianceHistoryImages.push_back(makeImage(
            m_AverageExtent,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            "Failed to initialize FidelityFX SSSR average-radiance-history image"
        ));
        m_VarianceHistoryImages.push_back(makeImage(
            m_Extent,
            VK_FORMAT_R32_SFLOAT,
            "Failed to initialize FidelityFX SSSR variance-history image"
        ));
        m_SampleCountHistoryImages.push_back(makeImage(
            m_Extent,
            VK_FORMAT_R32_SFLOAT,
            "Failed to initialize FidelityFX SSSR sample-count-history image"
        ));
        m_ReprojectedRadianceImages.push_back(makeImage(
            m_Extent,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            "Failed to initialize FidelityFX SSSR reprojected-radiance image"
        ));
        m_AverageRadianceImages.push_back(makeImage(
            m_AverageExtent,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            "Failed to initialize FidelityFX SSSR average-radiance image"
        ));
        m_VarianceImages.push_back(makeImage(
            m_Extent,
            VK_FORMAT_R32_SFLOAT,
            "Failed to initialize FidelityFX SSSR variance image"
        ));
        m_SampleCountImages.push_back(makeImage(
            m_Extent,
            VK_FORMAT_R32_SFLOAT,
            "Failed to initialize FidelityFX SSSR sample-count image"
        ));
    }

    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[0].descriptorCount = static_cast<u32>(count * 13u);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<u32>(count);
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[2].descriptorCount = static_cast<u32>(count * 4u);
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[3].descriptorCount = static_cast<u32>(count);

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
            "Failed to create Vulkan FidelityFX SSSR reproject descriptor pool"
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
            "Failed to allocate Vulkan FidelityFX SSSR reproject descriptor sets"
        );
    }

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        std::array<VkDescriptorImageInfo, 13> sampledImages{};
        sampledImages[0].imageLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        sampledImages[0].imageView = renderTargets.SceneDepthView(imageIndex);
        sampledImages[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[1].imageView =
            classifyResources.ExtractedRoughnessView(imageIndex);
        sampledImages[2].imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImages[2].imageView =
            renderTargets.GBufferNormalRoughnessView(imageIndex);
        sampledImages[3] = sampledImages[0];
        sampledImages[4] = sampledImages[1];
        sampledImages[5] = sampledImages[2];
        sampledImages[6].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[6].imageView =
            classifyResources.IntersectionOutputView(imageIndex);
        sampledImages[7].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[7].imageView =
            m_RadianceHistoryImages[imageIndex]->View();
        sampledImages[8].imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImages[8].imageView = renderTargets.VelocityView(imageIndex);
        sampledImages[9].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[9].imageView =
            m_AverageRadianceHistoryImages[imageIndex]->View();
        sampledImages[10].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[10].imageView =
            m_VarianceHistoryImages[imageIndex]->View();
        sampledImages[11].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[11].imageView =
            m_SampleCountHistoryImages[imageIndex]->View();
        sampledImages[12].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[12].imageView =
            blueNoiseResources.BlueNoiseView(imageIndex);

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = linearSampler;

        std::array<VkDescriptorImageInfo, 4> storageImages{};
        storageImages[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[0].imageView =
            m_ReprojectedRadianceImages[imageIndex]->View();
        storageImages[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[1].imageView =
            m_AverageRadianceImages[imageIndex]->View();
        storageImages[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[2].imageView =
            m_VarianceImages[imageIndex]->View();
        storageImages[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[3].imageView =
            m_SampleCountImages[imageIndex]->View();

        VkBufferView denoiserTileListView =
            classifyResources.DenoiserTileListBufferView(imageIndex);

        std::array<VkWriteDescriptorSet, 19> writes{};
        for (u32 binding = 0u; binding <= 12u; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[binding].descriptorCount = 1u;
            writes[binding].pImageInfo = &sampledImages[binding];
        }
        writes[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[13].dstSet = m_DescriptorSets[imageIndex];
        writes[13].dstBinding = 13u;
        writes[13].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[13].descriptorCount = 1u;
        writes[13].pImageInfo = &samplerInfo;

        for (u32 binding = 14u; binding <= 17u; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[binding].descriptorCount = 1u;
            writes[binding].pImageInfo = &storageImages[binding - 14u];
        }
        writes[18].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[18].dstSet = m_DescriptorSets[imageIndex];
        writes[18].dstBinding = 18u;
        writes[18].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        writes[18].descriptorCount = 1u;
        writes[18].pTexelBufferView = &denoiserTileListView;

        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(writes.size()),
            writes.data(),
            0u,
            nullptr
        );
    }
}

VulkanFfxSssrPrefilterDescriptorSetLayout::
VulkanFfxSssrPrefilterDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanFfxSssrPrefilterDescriptorSetLayout::
~VulkanFfxSssrPrefilterDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout VulkanFfxSssrPrefilterDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanFfxSssrPrefilterDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanFfxSssrPrefilterDescriptorSetLayout::CreateDescriptorSetLayout(
    const VulkanDevice& device
) {
    std::array<VkDescriptorSetLayoutBinding, 12> bindings{};
    auto setBinding = [&](u32 binding, VkDescriptorType type) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = type;
        bindings[binding].descriptorCount = 1u;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[binding].pImmutableSamplers = nullptr;
    };
    for (u32 binding = 0u; binding <= 6u; ++binding) {
        setBinding(binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    }
    setBinding(7u, VK_DESCRIPTOR_TYPE_SAMPLER);
    for (u32 binding = 8u; binding <= 10u; ++binding) {
        setBinding(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }
    setBinding(11u, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);

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
            "Failed to create Vulkan FidelityFX SSSR prefilter descriptor set layout"
        );
    }
}

VulkanFfxSssrPrefilterResources::VulkanFfxSssrPrefilterResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrPrefilterDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrReprojectResources& reprojectResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkSampler linearSampler
) : m_Device(device.Handle()) {
    CreateResources(
        device,
        physicalDevice,
        commandPool,
        descriptorSetLayout,
        classifyResources,
        reprojectResources,
        renderTargets,
        linearSampler
    );
}

VulkanFfxSssrPrefilterResources::~VulkanFfxSssrPrefilterResources() {
    Release();
}

VkDescriptorSet VulkanFfxSssrPrefilterResources::Handle(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DescriptorSets.size(),
        "FidelityFX SSSR prefilter descriptor image index is out of range"
    );
    return m_DescriptorSets[imageIndex];
}

VkImage VulkanFfxSssrPrefilterResources::RadianceImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_RadianceImages.size(),
        "FidelityFX SSSR prefilter radiance image index is out of range"
    );
    return m_RadianceImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrPrefilterResources::RadianceView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_RadianceImages.size(),
        "FidelityFX SSSR prefilter radiance view image index is out of range"
    );
    return m_RadianceImages[imageIndex]->View();
}

VkImage VulkanFfxSssrPrefilterResources::VarianceImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_VarianceImages.size(),
        "FidelityFX SSSR prefilter variance image index is out of range"
    );
    return m_VarianceImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrPrefilterResources::VarianceView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_VarianceImages.size(),
        "FidelityFX SSSR prefilter variance view image index is out of range"
    );
    return m_VarianceImages[imageIndex]->View();
}

VkImage VulkanFfxSssrPrefilterResources::SampleCountImage(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_SampleCountImages.size(),
        "FidelityFX SSSR prefilter sample-count image index is out of range"
    );
    return m_SampleCountImages[imageIndex]->Handle();
}

VkImageView VulkanFfxSssrPrefilterResources::SampleCountView(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_SampleCountImages.size(),
        "FidelityFX SSSR prefilter sample-count view image index is out of range"
    );
    return m_SampleCountImages[imageIndex]->View();
}

std::size_t VulkanFfxSssrPrefilterResources::Count() const {
    return m_DescriptorSets.size();
}

VkExtent2D VulkanFfxSssrPrefilterResources::Extent() const {
    return m_Extent;
}

u64 VulkanFfxSssrPrefilterResources::TotalMemoryBytes() const {
    const u64 pixels =
        static_cast<u64>(m_Extent.width) * static_cast<u64>(m_Extent.height);
    const u64 bytesPerFrame =
        pixels * (sizeof(f32) * 4ull + sizeof(f32) * 2ull);
    return static_cast<u64>(Count()) * bytesPerFrame;
}

void VulkanFfxSssrPrefilterResources::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrPrefilterDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrReprojectResources& reprojectResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkSampler linearSampler
) {
    Release();
    m_Device = device.Handle();
    CreateResources(
        device,
        physicalDevice,
        commandPool,
        descriptorSetLayout,
        classifyResources,
        reprojectResources,
        renderTargets,
        linearSampler
    );
}

void VulkanFfxSssrPrefilterResources::Release() {
    m_DescriptorSets.clear();
    m_SampleCountImages.clear();
    m_VarianceImages.clear();
    m_RadianceImages.clear();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    m_Extent = {};
}

void VulkanFfxSssrPrefilterResources::CreateResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrPrefilterDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrReprojectResources& reprojectResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkSampler linearSampler
) {
    const std::size_t count = renderTargets.Count();
    SE_ASSERT(
        count > 0,
        "FidelityFX SSSR prefilter resource count must be greater than zero"
    );
    SE_ASSERT(
        classifyResources.Count() == count &&
            reprojectResources.Count() == count,
        "FidelityFX SSSR prefilter resources require matching per-frame producers"
    );
    SE_ASSERT(
        linearSampler != VK_NULL_HANDLE,
        "FidelityFX SSSR prefilter resources require a valid linear sampler"
    );

    m_Extent = renderTargets.Extent();
    if (m_Extent.width == 0u ||
        m_Extent.height == 0u ||
        FfxExtentsDiffer(m_Extent, classifyResources.Extent()) ||
        FfxExtentsDiffer(m_Extent, reprojectResources.Extent())) {
        throw std::runtime_error(
            "FidelityFX SSSR prefilter resources require matching render, classify, and reproject extents"
        );
    }

    const VkImageUsageFlags imageUsage =
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto makeImage = [&](
        VkExtent2D extent,
        VkFormat format,
        const char* errorPrefix
    ) {
        auto image = std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            extent,
            format,
            VK_IMAGE_TILING_OPTIMAL,
            imageUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        try {
            image->TransitionLayout(
                device,
                commandPool,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL
            );
        } catch (const std::exception& exception) {
            throw std::runtime_error(
                std::string(errorPrefix) + ": " + exception.what()
            );
        }
        return image;
    };

    m_RadianceImages.reserve(count);
    m_VarianceImages.reserve(count);
    m_SampleCountImages.reserve(count);
    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        m_RadianceImages.push_back(makeImage(
            m_Extent,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            "Failed to initialize FidelityFX SSSR prefilter radiance image"
        ));
        m_VarianceImages.push_back(makeImage(
            m_Extent,
            VK_FORMAT_R32_SFLOAT,
            "Failed to initialize FidelityFX SSSR prefilter variance image"
        ));
        m_SampleCountImages.push_back(makeImage(
            m_Extent,
            VK_FORMAT_R32_SFLOAT,
            "Failed to initialize FidelityFX SSSR prefilter sample-count image"
        ));
    }

    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[0].descriptorCount = static_cast<u32>(count * 7u);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<u32>(count);
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[2].descriptorCount = static_cast<u32>(count * 3u);
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[3].descriptorCount = static_cast<u32>(count);

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
            "Failed to create Vulkan FidelityFX SSSR prefilter descriptor pool"
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
            "Failed to allocate Vulkan FidelityFX SSSR prefilter descriptor sets"
        );
    }

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        std::array<VkDescriptorImageInfo, 7> sampledImages{};
        sampledImages[0].imageLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        sampledImages[0].imageView = renderTargets.SceneDepthView(imageIndex);
        sampledImages[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[1].imageView =
            classifyResources.ExtractedRoughnessView(imageIndex);
        sampledImages[2].imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampledImages[2].imageView =
            renderTargets.GBufferNormalRoughnessView(imageIndex);
        sampledImages[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[3].imageView =
            reprojectResources.AverageRadianceView(imageIndex);
        sampledImages[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[4].imageView =
            classifyResources.IntersectionOutputView(imageIndex);
        sampledImages[5].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[5].imageView =
            reprojectResources.VarianceView(imageIndex);
        sampledImages[6].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[6].imageView =
            reprojectResources.SampleCountView(imageIndex);

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = linearSampler;

        std::array<VkDescriptorImageInfo, 3> storageImages{};
        storageImages[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[0].imageView = m_RadianceImages[imageIndex]->View();
        storageImages[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[1].imageView = m_VarianceImages[imageIndex]->View();
        storageImages[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[2].imageView = m_SampleCountImages[imageIndex]->View();

        VkBufferView denoiserTileListView =
            classifyResources.DenoiserTileListBufferView(imageIndex);

        std::array<VkWriteDescriptorSet, 12> writes{};
        for (u32 binding = 0u; binding <= 6u; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[binding].descriptorCount = 1u;
            writes[binding].pImageInfo = &sampledImages[binding];
        }
        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = m_DescriptorSets[imageIndex];
        writes[7].dstBinding = 7u;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[7].descriptorCount = 1u;
        writes[7].pImageInfo = &samplerInfo;

        for (u32 binding = 8u; binding <= 10u; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[binding].descriptorCount = 1u;
            writes[binding].pImageInfo = &storageImages[binding - 8u];
        }
        writes[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[11].dstSet = m_DescriptorSets[imageIndex];
        writes[11].dstBinding = 11u;
        writes[11].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        writes[11].descriptorCount = 1u;
        writes[11].pTexelBufferView = &denoiserTileListView;

        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(writes.size()),
            writes.data(),
            0u,
            nullptr
        );
    }
}

VulkanFfxSssrResolveTemporalDescriptorSetLayout::
VulkanFfxSssrResolveTemporalDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanFfxSssrResolveTemporalDescriptorSetLayout::
~VulkanFfxSssrResolveTemporalDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout
VulkanFfxSssrResolveTemporalDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanFfxSssrResolveTemporalDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void
VulkanFfxSssrResolveTemporalDescriptorSetLayout::CreateDescriptorSetLayout(
    const VulkanDevice& device
) {
    std::array<VkDescriptorSetLayoutBinding, 11> bindings{};
    auto setBinding = [&](u32 binding, VkDescriptorType type) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = type;
        bindings[binding].descriptorCount = 1u;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[binding].pImmutableSamplers = nullptr;
    };
    for (u32 binding = 0u; binding <= 5u; ++binding) {
        setBinding(binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    }
    setBinding(6u, VK_DESCRIPTOR_TYPE_SAMPLER);
    for (u32 binding = 7u; binding <= 9u; ++binding) {
        setBinding(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }
    setBinding(10u, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);

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
            "Failed to create Vulkan FidelityFX SSSR resolve-temporal descriptor set layout"
        );
    }
}

VulkanFfxSssrResolveTemporalResources::VulkanFfxSssrResolveTemporalResources(
    const VulkanDevice& device,
    const VulkanFfxSssrResolveTemporalDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrReprojectResources& reprojectResources,
    const VulkanFfxSssrPrefilterResources& prefilterResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkSampler linearSampler
) : m_Device(device.Handle()) {
    CreateResources(
        device,
        descriptorSetLayout,
        classifyResources,
        reprojectResources,
        prefilterResources,
        renderTargets,
        linearSampler
    );
}

VulkanFfxSssrResolveTemporalResources::
~VulkanFfxSssrResolveTemporalResources() {
    Release();
}

VkDescriptorSet VulkanFfxSssrResolveTemporalResources::Handle(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DescriptorSets.size(),
        "FidelityFX SSSR resolve-temporal descriptor image index is out of range"
    );
    return m_DescriptorSets[imageIndex];
}

std::size_t VulkanFfxSssrResolveTemporalResources::Count() const {
    return m_DescriptorSets.size();
}

VkExtent2D VulkanFfxSssrResolveTemporalResources::Extent() const {
    return m_Extent;
}

u64 VulkanFfxSssrResolveTemporalResources::TotalMemoryBytes() const {
    return 0ull;
}

void VulkanFfxSssrResolveTemporalResources::Recreate(
    const VulkanDevice& device,
    const VulkanFfxSssrResolveTemporalDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrReprojectResources& reprojectResources,
    const VulkanFfxSssrPrefilterResources& prefilterResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkSampler linearSampler
) {
    Release();
    m_Device = device.Handle();
    CreateResources(
        device,
        descriptorSetLayout,
        classifyResources,
        reprojectResources,
        prefilterResources,
        renderTargets,
        linearSampler
    );
}

void VulkanFfxSssrResolveTemporalResources::Release() {
    m_DescriptorSets.clear();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    m_Extent = {};
}

void VulkanFfxSssrResolveTemporalResources::CreateResources(
    const VulkanDevice& device,
    const VulkanFfxSssrResolveTemporalDescriptorSetLayout& descriptorSetLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrReprojectResources& reprojectResources,
    const VulkanFfxSssrPrefilterResources& prefilterResources,
    const VulkanSceneRenderTargets& renderTargets,
    VkSampler linearSampler
) {
    const std::size_t count = renderTargets.Count();
    SE_ASSERT(
        count > 0,
        "FidelityFX SSSR resolve-temporal resource count must be greater than zero"
    );
    SE_ASSERT(
        classifyResources.Count() == count &&
            reprojectResources.Count() == count &&
            prefilterResources.Count() == count,
        "FidelityFX SSSR resolve-temporal resources require matching per-frame producers"
    );
    SE_ASSERT(
        linearSampler != VK_NULL_HANDLE,
        "FidelityFX SSSR resolve-temporal resources require a valid linear sampler"
    );

    m_Extent = renderTargets.Extent();
    if (m_Extent.width == 0u ||
        m_Extent.height == 0u ||
        FfxExtentsDiffer(m_Extent, classifyResources.Extent()) ||
        FfxExtentsDiffer(m_Extent, reprojectResources.Extent()) ||
        FfxExtentsDiffer(m_Extent, prefilterResources.Extent())) {
        throw std::runtime_error(
            "FidelityFX SSSR resolve-temporal resources require matching render, classify, reproject, and prefilter extents"
        );
    }

    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[0].descriptorCount = static_cast<u32>(count * 6u);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<u32>(count);
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[2].descriptorCount = static_cast<u32>(count * 3u);
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[3].descriptorCount = static_cast<u32>(count);

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
            "Failed to create Vulkan FidelityFX SSSR resolve-temporal descriptor pool"
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
            "Failed to allocate Vulkan FidelityFX SSSR resolve-temporal descriptor sets"
        );
    }

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        std::array<VkDescriptorImageInfo, 6> sampledImages{};
        sampledImages[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[0].imageView =
            classifyResources.ExtractedRoughnessView(imageIndex);
        sampledImages[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[1].imageView =
            reprojectResources.AverageRadianceView(imageIndex);
        sampledImages[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[2].imageView = prefilterResources.RadianceView(imageIndex);
        sampledImages[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[3].imageView =
            reprojectResources.ReprojectedRadianceView(imageIndex);
        sampledImages[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[4].imageView = prefilterResources.VarianceView(imageIndex);
        sampledImages[5].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sampledImages[5].imageView =
            prefilterResources.SampleCountView(imageIndex);

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = linearSampler;

        std::array<VkDescriptorImageInfo, 3> storageImages{};
        storageImages[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[0].imageView =
            reprojectResources.RadianceHistoryView(imageIndex);
        storageImages[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[1].imageView =
            reprojectResources.VarianceHistoryView(imageIndex);
        storageImages[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageImages[2].imageView =
            reprojectResources.SampleCountHistoryView(imageIndex);

        VkBufferView denoiserTileListView =
            classifyResources.DenoiserTileListBufferView(imageIndex);

        std::array<VkWriteDescriptorSet, 11> writes{};
        for (u32 binding = 0u; binding <= 5u; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[binding].descriptorCount = 1u;
            writes[binding].pImageInfo = &sampledImages[binding];
        }
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = m_DescriptorSets[imageIndex];
        writes[6].dstBinding = 6u;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[6].descriptorCount = 1u;
        writes[6].pImageInfo = &samplerInfo;

        for (u32 binding = 7u; binding <= 9u; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[binding].descriptorCount = 1u;
            writes[binding].pImageInfo = &storageImages[binding - 7u];
        }
        writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[10].dstSet = m_DescriptorSets[imageIndex];
        writes[10].dstBinding = 10u;
        writes[10].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        writes[10].descriptorCount = 1u;
        writes[10].pTexelBufferView = &denoiserTileListView;

        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(writes.size()),
            writes.data(),
            0u,
            nullptr
        );
    }
}

}
