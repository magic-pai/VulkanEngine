#include "renderer/vulkan/image.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/upload_batch.h"

namespace se {

namespace {

VkDeviceSize BytesPerTexel(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R16G16_SFLOAT:
        return 4;
    default:
        return 4;
    }
}

VkCommandBuffer BeginSingleTimeCommands(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool
) {
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool = commandPool.Handle();
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device.Handle(), &allocateInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan image command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
        throw std::runtime_error("Failed to begin Vulkan image command buffer");
    }

    return commandBuffer;
}

void EndSingleTimeCommands(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    VkCommandBuffer commandBuffer
) {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
        throw std::runtime_error("Failed to record Vulkan image command buffer");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(device.GraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
        throw std::runtime_error("Failed to submit Vulkan image command buffer");
    }

    vkQueueWaitIdle(device.GraphicsQueue());
    vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
}

}

VulkanImage::VulkanImage(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    VkExtent2D extent,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags memoryProperties,
    VkImageAspectFlags aspectFlags,
    u32 mipLevels,
    u32 arrayLayers,
    VkImageCreateFlags imageFlags,
    VkImageViewType viewType
) : m_Device(device.Handle()) {
    Recreate(
        device,
        physicalDevice,
        extent,
        format,
        tiling,
        usage,
        memoryProperties,
        aspectFlags,
        mipLevels,
        arrayLayers,
        imageFlags,
        viewType
    );
}

VulkanImage::~VulkanImage() {
    Release();
}

VkImage VulkanImage::Handle() const {
    return m_Image;
}

VkImageView VulkanImage::View() const {
    return m_ImageView;
}

VkFormat VulkanImage::Format() const {
    return m_Format;
}

VkExtent2D VulkanImage::Extent() const {
    return m_Extent;
}

u32 VulkanImage::MipLevels() const {
    return m_MipLevels;
}

void VulkanImage::TransitionLayout(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    u32 mipLevels,
    VulkanUploadBatch* uploadBatch,
    VkImageAspectFlags aspectMask
) const {
    const bool batched = uploadBatch != nullptr;
    VkCommandBuffer commandBuffer = batched
        ? uploadBatch->CommandBuffer()
        : BeginSingleTimeCommands(device, commandPool);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = m_ArrayLayers;

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        if (!batched) {
            vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
        } else {
            vkEndCommandBuffer(commandBuffer);
        }
        throw std::runtime_error("Unsupported Vulkan image layout transition");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage,
        destinationStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );

    if (!batched) {
        EndSingleTimeCommands(device, commandPool, commandBuffer);
    }
}

void VulkanImage::CopyFromBuffer(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    VkBuffer sourceBuffer,
    VulkanUploadBatch* uploadBatch
) const {
    CopyFromBuffer(device, commandPool, sourceBuffer, 1, uploadBatch);
}

void VulkanImage::CopyFromBuffer(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    VkBuffer sourceBuffer,
    u32 arrayLayers,
    VulkanUploadBatch* uploadBatch
) const {
    SE_ASSERT(arrayLayers > 0 && arrayLayers <= m_ArrayLayers, "Image copy array layer count is invalid");
    const bool batched = uploadBatch != nullptr;
    VkCommandBuffer commandBuffer = batched
        ? uploadBatch->CommandBuffer()
        : BeginSingleTimeCommands(device, commandPool);

    std::vector<VkBufferImageCopy> regions;
    regions.reserve(arrayLayers);

    const VkDeviceSize layerSize =
        static_cast<VkDeviceSize>(m_Extent.width) *
        static_cast<VkDeviceSize>(m_Extent.height) *
        BytesPerTexel(m_Format);

    for (u32 layer = 0; layer < arrayLayers; ++layer) {
        VkBufferImageCopy region{};
        region.bufferOffset = layerSize * layer;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = {
            m_Extent.width,
            m_Extent.height,
            1
        };
        regions.push_back(region);
    }

    vkCmdCopyBufferToImage(
        commandBuffer,
        sourceBuffer,
        m_Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<u32>(regions.size()),
        regions.data()
    );

    if (!batched) {
        EndSingleTimeCommands(device, commandPool, commandBuffer);
    }
}

void VulkanImage::GenerateMipmaps(
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    VulkanUploadBatch* uploadBatch
) const {
    GenerateMipmaps(physicalDevice, device, commandPool, m_ArrayLayers, uploadBatch);
}

void VulkanImage::GenerateMipmaps(
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    u32 arrayLayers,
    VulkanUploadBatch* uploadBatch
) const {
    SE_ASSERT(arrayLayers > 0 && arrayLayers <= m_ArrayLayers, "Mipmap array layer count is invalid");

    if (!physicalDevice.SupportsLinearBlit(m_Format)) {
        throw std::runtime_error("Texture image format does not support linear blit mipmap generation");
    }

    const bool batched = uploadBatch != nullptr;
    VkCommandBuffer commandBuffer = batched
        ? uploadBatch->CommandBuffer()
        : BeginSingleTimeCommands(device, commandPool);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = m_Image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    for (u32 layer = 0; layer < arrayLayers; ++layer) {
        i32 mipWidth = static_cast<i32>(m_Extent.width);
        i32 mipHeight = static_cast<i32>(m_Extent.height);
        barrier.subresourceRange.baseArrayLayer = layer;

        for (u32 mipLevel = 1; mipLevel < m_MipLevels; ++mipLevel) {
            barrier.subresourceRange.baseMipLevel = mipLevel - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier
            );

            VkImageBlit blit{};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = mipLevel - 1;
            blit.srcSubresource.baseArrayLayer = layer;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = {
                mipWidth > 1 ? mipWidth / 2 : 1,
                mipHeight > 1 ? mipHeight / 2 : 1,
                1
            };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = mipLevel;
            blit.dstSubresource.baseArrayLayer = layer;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(
                commandBuffer,
                m_Image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                m_Image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &blit,
                VK_FILTER_LINEAR
            );

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier
            );

            if (mipWidth > 1) {
                mipWidth /= 2;
            }
            if (mipHeight > 1) {
                mipHeight /= 2;
            }
        }

        barrier.subresourceRange.baseMipLevel = m_MipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
    }

    if (!batched) {
        EndSingleTimeCommands(device, commandPool, commandBuffer);
    }
}

void VulkanImage::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    VkExtent2D extent,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags memoryProperties,
    VkImageAspectFlags aspectFlags,
    u32 mipLevels,
    u32 arrayLayers,
    VkImageCreateFlags imageFlags,
    VkImageViewType viewType
) {
    Release();
    m_Device = device.Handle();

    CreateImage(
        device,
        physicalDevice,
        extent,
        format,
        tiling,
        usage,
        memoryProperties,
        mipLevels,
        arrayLayers,
        imageFlags
    );
    CreateImageView(device, format, aspectFlags, mipLevels, arrayLayers, viewType);
}

void VulkanImage::Release() {
    if (m_ImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_ImageView, nullptr);
        m_ImageView = VK_NULL_HANDLE;
    }

    if (m_Image != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_Image, nullptr);
        m_Image = VK_NULL_HANDLE;
    }

    if (m_Memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_Memory, nullptr);
        m_Memory = VK_NULL_HANDLE;
    }

    m_Format = VK_FORMAT_UNDEFINED;
    m_Extent = {};
    m_MipLevels = 1;
    m_ArrayLayers = 1;
}

void VulkanImage::CreateImage(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    VkExtent2D extent,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags memoryProperties,
    u32 mipLevels,
    u32 arrayLayers,
    VkImageCreateFlags imageFlags
) {
    SE_ASSERT(extent.width > 0 && extent.height > 0, "Vulkan image extent must be valid");
    SE_ASSERT(mipLevels > 0, "Vulkan image mip levels must be greater than zero");
    SE_ASSERT(arrayLayers > 0, "Vulkan image array layers must be greater than zero");

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = imageFlags;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device.Handle(), &imageInfo, nullptr, &m_Image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan image");
    }

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(device.Handle(), m_Image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = physicalDevice.FindMemoryType(
        memoryRequirements.memoryTypeBits,
        memoryProperties
    );

    if (vkAllocateMemory(device.Handle(), &allocateInfo, nullptr, &m_Memory) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to allocate Vulkan image memory");
    }

    if (vkBindImageMemory(device.Handle(), m_Image, m_Memory, 0) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to bind Vulkan image memory");
    }

    m_Format = format;
    m_Extent = extent;
    m_MipLevels = mipLevels;
    m_ArrayLayers = arrayLayers;
}

void VulkanImage::CreateImageView(
    const VulkanDevice& device,
    VkFormat format,
    VkImageAspectFlags aspectFlags,
    u32 mipLevels,
    u32 arrayLayers,
    VkImageViewType viewType
) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = arrayLayers;

    if (vkCreateImageView(device.Handle(), &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to create Vulkan image view");
    }

}

}
