#include "renderer/vulkan/framebuffer.h"

#include "renderer/vulkan/depth_buffer.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/render_pass.h"
#include "renderer/vulkan/swapchain.h"

namespace se {

VulkanFramebuffer::VulkanFramebuffer(
    const VulkanDevice& device,
    const VulkanRenderPass& renderPass,
    const VulkanDepthBuffer& depthBuffer,
    const VulkanSwapchain& swapchain
) : m_Device(device.Handle()) {
    CreateFramebuffers(device, renderPass, depthBuffer, swapchain);
}

VulkanFramebuffer::~VulkanFramebuffer() {
    Release();
}

const std::vector<VkFramebuffer>& VulkanFramebuffer::Handles() const {
    return m_Framebuffers;
}

void VulkanFramebuffer::Recreate(
    const VulkanDevice& device,
    const VulkanRenderPass& renderPass,
    const VulkanDepthBuffer& depthBuffer,
    const VulkanSwapchain& swapchain
) {
    Release();
    CreateFramebuffers(device, renderPass, depthBuffer, swapchain);
}

void VulkanFramebuffer::CreateFramebuffers(
    const VulkanDevice& device,
    const VulkanRenderPass& renderPass,
    const VulkanDepthBuffer& depthBuffer,
    const VulkanSwapchain& swapchain
) {
    const std::vector<VkImageView>& imageViews = swapchain.ImageViews();
    const VkExtent2D extent = swapchain.Extent();

    SE_ASSERT(
        depthBuffer.Count() == imageViews.size(),
        "Depth image count must match swapchain image count"
    );

    m_Framebuffers.resize(imageViews.size());

    for (std::size_t index = 0; index < imageViews.size(); ++index) {
        const VkImageView attachments[] = {
            imageViews[index],
            depthBuffer.View(index)
        };

        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass.Handle();
        createInfo.attachmentCount = static_cast<u32>(std::size(attachments));
        createInfo.pAttachments = attachments;
        createInfo.width = extent.width;
        createInfo.height = extent.height;
        createInfo.layers = 1;

        if (vkCreateFramebuffer(device.Handle(), &createInfo, nullptr, &m_Framebuffers[index]) != VK_SUCCESS) {
            Release();
            throw std::runtime_error("Failed to create Vulkan framebuffer");
        }
    }
}

void VulkanFramebuffer::Release() {
    for (VkFramebuffer framebuffer : m_Framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
        }
    }

    m_Framebuffers.clear();
}

}
