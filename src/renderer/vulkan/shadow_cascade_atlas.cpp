#include "renderer/vulkan/shadow_cascade_atlas.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/depth_buffer.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/image.h"
#include "renderer/vulkan/physical_device.h"

#include <algorithm>

namespace se {

namespace {

u32 CeilSqrt(u32 value) {
    u32 result = 1;
    while (result * result < value) {
        ++result;
    }
    return result;
}

}

VulkanDirectionalShadowCascadeAtlas::VulkanDirectionalShadowCascadeAtlas(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::size_t imageCount,
    u32 tileSize,
    u32 cascadeCapacity
) : m_Device(device.Handle()) {
    Recreate(device, physicalDevice, commandPool, imageCount, tileSize, cascadeCapacity);
}

VulkanDirectionalShadowCascadeAtlas::~VulkanDirectionalShadowCascadeAtlas() {
    Release();
}

VkExtent2D VulkanDirectionalShadowCascadeAtlas::Extent() const {
    return m_Extent;
}

VkFormat VulkanDirectionalShadowCascadeAtlas::Format() const {
    return m_Format;
}

VkImageView VulkanDirectionalShadowCascadeAtlas::View(std::size_t index) const {
    SE_ASSERT(index < m_Images.size(), "Directional shadow cascade atlas index is out of range");
    return m_Images[index]->View();
}

VkSampler VulkanDirectionalShadowCascadeAtlas::Sampler() const {
    return m_Sampler;
}

VkImageLayout VulkanDirectionalShadowCascadeAtlas::Layout() const {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
}

std::size_t VulkanDirectionalShadowCascadeAtlas::Count() const {
    return m_Images.size();
}

u32 VulkanDirectionalShadowCascadeAtlas::TileSize() const {
    return m_TileSize;
}

u32 VulkanDirectionalShadowCascadeAtlas::CascadeCapacity() const {
    return m_CascadeCapacity;
}

u32 VulkanDirectionalShadowCascadeAtlas::TileColumns() const {
    return m_TileColumns;
}

u32 VulkanDirectionalShadowCascadeAtlas::TileRows() const {
    return m_TileRows;
}

void VulkanDirectionalShadowCascadeAtlas::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::size_t imageCount,
    u32 tileSize,
    u32 cascadeCapacity
) {
    SE_ASSERT(imageCount > 0, "Directional shadow cascade atlas image count must be greater than zero");
    SE_ASSERT(tileSize > 0, "Directional shadow cascade atlas tile size must be greater than zero");
    SE_ASSERT(cascadeCapacity > 0, "Directional shadow cascade atlas capacity must be greater than zero");

    Release();
    m_Device = device.Handle();
    m_TileSize = tileSize;
    m_CascadeCapacity = cascadeCapacity;
    m_TileColumns = CeilSqrt(cascadeCapacity);
    m_TileRows = (cascadeCapacity + m_TileColumns - 1) / m_TileColumns;
    m_Extent = {
        m_TileSize * m_TileColumns,
        m_TileSize * m_TileRows
    };
    m_Format = FindDepthFormat(physicalDevice);

    m_Images.reserve(imageCount);
    for (std::size_t index = 0; index < imageCount; ++index) {
        m_Images.push_back(std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            m_Extent,
            m_Format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        ));
    }

    InitializeImageLayouts(device, commandPool);
    CreateSampler(device, physicalDevice);
}

void VulkanDirectionalShadowCascadeAtlas::Release() {
    if (m_Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device, m_Sampler, nullptr);
        m_Sampler = VK_NULL_HANDLE;
    }
    m_Images.clear();
    m_Format = VK_FORMAT_UNDEFINED;
    m_Extent = {};
    m_TileSize = 0;
    m_CascadeCapacity = 0;
    m_TileColumns = 0;
    m_TileRows = 0;
}

VkFormat VulkanDirectionalShadowCascadeAtlas::FindDepthFormat(
    const VulkanPhysicalDevice& physicalDevice
) const {
    return VulkanDepthBuffer::FindDepthFormat(physicalDevice);
}

void VulkanDirectionalShadowCascadeAtlas::CreateSampler(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice
) {
    const VkPhysicalDeviceFeatures& features = physicalDevice.Features();
    const VkPhysicalDeviceProperties& properties = physicalDevice.Properties();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = features.samplerAnisotropy
        ? properties.limits.maxSamplerAnisotropy
        : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device.Handle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan directional shadow cascade atlas sampler");
    }
}

void VulkanDirectionalShadowCascadeAtlas::InitializeImageLayouts(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool
) const {
    for (const std::unique_ptr<VulkanImage>& image : m_Images) {
        image->TransitionLayout(
            device,
            commandPool,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            1,
            nullptr,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );
    }
}

}
