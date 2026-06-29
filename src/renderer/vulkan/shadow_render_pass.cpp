#include "renderer/vulkan/shadow_render_pass.h"

#include "renderer/vulkan/device.h"
#include "renderer/vulkan/shadow_map.h"

namespace se {

VulkanShadowRenderPass::VulkanShadowRenderPass(
    const VulkanDevice& device,
    const VulkanShadowMap& shadowMap
) : m_Device(device.Handle()) {
    CreateRenderPass(
        device,
        shadowMap,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_IMAGE_LAYOUT_UNDEFINED,
        m_RenderPass
    );
    CreateRenderPass(
        device,
        shadowMap,
        VK_ATTACHMENT_LOAD_OP_LOAD,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        m_LoadRenderPass
    );
}

VulkanShadowRenderPass::~VulkanShadowRenderPass() {
    Release();
}

VkRenderPass VulkanShadowRenderPass::Handle() const {
    return m_RenderPass;
}

VkRenderPass VulkanShadowRenderPass::LoadHandle() const {
    return m_LoadRenderPass;
}

void VulkanShadowRenderPass::Release() {
    if (m_RenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
        m_RenderPass = VK_NULL_HANDLE;
    }
    if (m_LoadRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_Device, m_LoadRenderPass, nullptr);
        m_LoadRenderPass = VK_NULL_HANDLE;
    }
}

void VulkanShadowRenderPass::CreateRenderPass(
    const VulkanDevice& device,
    const VulkanShadowMap& shadowMap,
    VkAttachmentLoadOp loadOp,
    VkImageLayout initialLayout,
    VkRenderPass& renderPass
) {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = shadowMap.Format();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = loadOp;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = initialLayout;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentReference{};
    depthAttachmentReference.attachment = 0;
    depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentReference;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &depthAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = static_cast<u32>(dependencies.size());
    createInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device.Handle(), &createInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow render pass");
    }
}

}
