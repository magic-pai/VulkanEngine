#include "renderer/vulkan/shadow_framebuffer.h"

#include "renderer/vulkan/device.h"
#include "renderer/vulkan/local_shadow_atlas.h"
#include "renderer/vulkan/shadow_cascade_atlas.h"
#include "renderer/vulkan/shadow_map.h"
#include "renderer/vulkan/shadow_render_pass.h"

namespace se {

VulkanShadowFramebuffer::VulkanShadowFramebuffer(
    const VulkanDevice& device,
    const VulkanShadowRenderPass& renderPass,
    const VulkanShadowMap& shadowMap
) : m_Device(device.Handle()) {
    CreateFramebuffer(device, renderPass, shadowMap);
}

VulkanShadowFramebuffer::VulkanShadowFramebuffer(
    const VulkanDevice& device,
    const VulkanShadowRenderPass& renderPass,
    const VulkanDirectionalShadowCascadeAtlas& atlas
) : m_Device(device.Handle()) {
    CreateFramebuffer(device, renderPass, atlas);
}

VulkanShadowFramebuffer::VulkanShadowFramebuffer(
    const VulkanDevice& device,
    const VulkanShadowRenderPass& renderPass,
    const VulkanLocalShadowAtlas& atlas
) : m_Device(device.Handle()) {
    CreateFramebuffer(device, renderPass, atlas);
}

VulkanShadowFramebuffer::~VulkanShadowFramebuffer() {
    Release();
}

VkFramebuffer VulkanShadowFramebuffer::Handle() const {
    return Handle(0);
}

VkFramebuffer VulkanShadowFramebuffer::Handle(std::size_t index) const {
    SE_ASSERT(index < m_Framebuffers.size(), "Shadow framebuffer index is out of range");
    return m_Framebuffers[index];
}

VkExtent2D VulkanShadowFramebuffer::Extent() const {
    return m_Extent;
}

std::size_t VulkanShadowFramebuffer::Count() const {
    return m_Framebuffers.size();
}

void VulkanShadowFramebuffer::Recreate(
    const VulkanDevice& device,
    const VulkanShadowRenderPass& renderPass,
    const VulkanShadowMap& shadowMap
) {
    Release();
    m_Device = device.Handle();
    CreateFramebuffer(device, renderPass, shadowMap);
}

void VulkanShadowFramebuffer::Recreate(
    const VulkanDevice& device,
    const VulkanShadowRenderPass& renderPass,
    const VulkanDirectionalShadowCascadeAtlas& atlas
) {
    Release();
    m_Device = device.Handle();
    CreateFramebuffer(device, renderPass, atlas);
}

void VulkanShadowFramebuffer::Recreate(
    const VulkanDevice& device,
    const VulkanShadowRenderPass& renderPass,
    const VulkanLocalShadowAtlas& atlas
) {
    Release();
    m_Device = device.Handle();
    CreateFramebuffer(device, renderPass, atlas);
}

void VulkanShadowFramebuffer::Release() {
    for (VkFramebuffer framebuffer : m_Framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
        }
    }
    m_Framebuffers.clear();
}

void VulkanShadowFramebuffer::CreateFramebuffer(
    const VulkanDevice& device,
    const VulkanShadowRenderPass& renderPass,
    const VulkanShadowMap& shadowMap
) {
    m_Extent = shadowMap.Extent();
    m_Framebuffers.resize(shadowMap.Count(), VK_NULL_HANDLE);

    for (std::size_t index = 0; index < m_Framebuffers.size(); ++index) {
        const VkImageView attachment = shadowMap.View(index);

        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass.Handle();
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &attachment;
        createInfo.width = m_Extent.width;
        createInfo.height = m_Extent.height;
        createInfo.layers = 1;

        if (vkCreateFramebuffer(device.Handle(), &createInfo, nullptr, &m_Framebuffers[index]) != VK_SUCCESS) {
            Release();
            throw std::runtime_error("Failed to create Vulkan shadow framebuffer");
        }
    }
}

void VulkanShadowFramebuffer::CreateFramebuffer(
    const VulkanDevice& device,
    const VulkanShadowRenderPass& renderPass,
    const VulkanDirectionalShadowCascadeAtlas& atlas
) {
    m_Extent = atlas.Extent();
    m_Framebuffers.resize(atlas.Count(), VK_NULL_HANDLE);

    for (std::size_t index = 0; index < m_Framebuffers.size(); ++index) {
        const VkImageView attachment = atlas.View(index);

        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass.Handle();
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &attachment;
        createInfo.width = m_Extent.width;
        createInfo.height = m_Extent.height;
        createInfo.layers = 1;

        if (vkCreateFramebuffer(device.Handle(), &createInfo, nullptr, &m_Framebuffers[index]) != VK_SUCCESS) {
            Release();
            throw std::runtime_error("Failed to create Vulkan directional shadow cascade atlas framebuffer");
        }
    }
}

void VulkanShadowFramebuffer::CreateFramebuffer(
    const VulkanDevice& device,
    const VulkanShadowRenderPass& renderPass,
    const VulkanLocalShadowAtlas& atlas
) {
    m_Extent = atlas.Extent();
    m_Framebuffers.resize(atlas.Count(), VK_NULL_HANDLE);

    for (std::size_t index = 0; index < m_Framebuffers.size(); ++index) {
        const VkImageView attachment = atlas.View(index);

        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass.Handle();
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &attachment;
        createInfo.width = m_Extent.width;
        createInfo.height = m_Extent.height;
        createInfo.layers = 1;

        if (vkCreateFramebuffer(device.Handle(), &createInfo, nullptr, &m_Framebuffers[index]) != VK_SUCCESS) {
            Release();
            throw std::runtime_error("Failed to create Vulkan local shadow atlas framebuffer");
        }
    }
}

}
