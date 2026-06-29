#include "renderer/vulkan/depth_buffer.h"

#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/swapchain.h"

namespace se {

VulkanDepthBuffer::VulkanDepthBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanSwapchain& swapchain
) {
    Recreate(device, physicalDevice, swapchain);
}

VulkanDepthBuffer::~VulkanDepthBuffer() {
    Release();
}

VkImage VulkanDepthBuffer::Image(std::size_t index) const {
    SE_ASSERT(index < m_Images.size(), "Depth image index is out of range");
    return m_Images[index]->Handle();
}

VkImageView VulkanDepthBuffer::View(std::size_t index) const {
    SE_ASSERT(index < m_Images.size(), "Depth image index is out of range");
    return m_Images[index]->View();
}

VkFormat VulkanDepthBuffer::Format() const {
    return m_Format;
}

std::size_t VulkanDepthBuffer::Count() const {
    return m_Images.size();
}

void VulkanDepthBuffer::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanSwapchain& swapchain
) {
    Release();

    m_Format = FindDepthFormat(physicalDevice);
    m_Images.reserve(swapchain.Images().size());

    for (std::size_t index = 0; index < swapchain.Images().size(); ++index) {
        m_Images.push_back(std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            swapchain.Extent(),
            m_Format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        ));
    }
}

void VulkanDepthBuffer::Release() {
    m_Images.clear();
    m_Format = VK_FORMAT_UNDEFINED;
}

VkFormat VulkanDepthBuffer::FindDepthFormat(const VulkanPhysicalDevice& physicalDevice) {
    constexpr std::array<VkFormat, 3> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    return FindSupportedFormat(
        physicalDevice,
        candidates,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

VkFormat VulkanDepthBuffer::FindSupportedFormat(
    const VulkanPhysicalDevice& physicalDevice,
    std::span<const VkFormat> candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features
) {
    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice.Handle(), format, &properties);

        const bool linearSupported =
            tiling == VK_IMAGE_TILING_LINEAR &&
            (properties.linearTilingFeatures & features) == features;
        const bool optimalSupported =
            tiling == VK_IMAGE_TILING_OPTIMAL &&
            (properties.optimalTilingFeatures & features) == features;

        if (linearSupported || optimalSupported) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported Vulkan image format");
}

}
