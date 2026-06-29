#include "renderer/vulkan/render_targets.h"

#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/swapchain.h"

namespace se {

VulkanHdrRenderPass::VulkanHdrRenderPass(
    const VulkanDevice& device,
    VkFormat hdrColorFormat
) : m_Device(device.Handle()) {
    CreateRenderPass(device, hdrColorFormat);
}

VulkanHdrRenderPass::~VulkanHdrRenderPass() {
    Release();
}

VkRenderPass VulkanHdrRenderPass::Handle() const {
    return m_RenderPass;
}

void VulkanHdrRenderPass::Recreate(
    const VulkanDevice& device,
    VkFormat hdrColorFormat
) {
    Release();
    m_Device = device.Handle();
    CreateRenderPass(device, hdrColorFormat);
}

void VulkanHdrRenderPass::CreateRenderPass(
    const VulkanDevice& device,
    VkFormat hdrColorFormat
) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = hdrColorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentReference{};
    colorAttachmentReference.attachment = 0;
    colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device.Handle(), &createInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan HDR render pass");
    }
}

void VulkanHdrRenderPass::Release() {
    if (m_RenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
        m_RenderPass = VK_NULL_HANDLE;
    }
}

VulkanGBufferRenderPass::VulkanGBufferRenderPass(
    const VulkanDevice& device,
    const VulkanSceneRenderTargets& renderTargets
) : m_Device(device.Handle()) {
    CreateRenderPass(device, renderTargets);
}

VulkanGBufferRenderPass::~VulkanGBufferRenderPass() {
    Release();
}

VkRenderPass VulkanGBufferRenderPass::Handle() const {
    return m_RenderPass;
}

void VulkanGBufferRenderPass::Recreate(
    const VulkanDevice& device,
    const VulkanSceneRenderTargets& renderTargets
) {
    Release();
    m_Device = device.Handle();
    CreateRenderPass(device, renderTargets);
}

void VulkanGBufferRenderPass::CreateRenderPass(
    const VulkanDevice& device,
    const VulkanSceneRenderTargets& renderTargets
) {
    std::array<VkAttachmentDescription, 6> attachments{};
    attachments[0].format = renderTargets.GBufferAlbedoFormat();
    attachments[1].format = renderTargets.GBufferNormalRoughnessFormat();
    attachments[2].format = renderTargets.GBufferMaterialFormat();
    attachments[3].format = renderTargets.GBufferEmissiveFormat();
    attachments[4].format = renderTargets.VelocityFormat();
    for (std::size_t index = 0; index < 5; ++index) {
        attachments[index].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[index].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[index].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[index].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[index].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    attachments[5].format = renderTargets.SceneDepthFormat();
    attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[5].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[5].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[5].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentReference, 5> colorAttachmentReferences{};
    for (std::size_t index = 0; index < colorAttachmentReferences.size(); ++index) {
        colorAttachmentReferences[index].attachment = static_cast<u32>(index);
        colorAttachmentReferences[index].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkAttachmentReference depthAttachmentReference{};
    depthAttachmentReference.attachment = 5;
    depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<u32>(colorAttachmentReferences.size());
    subpass.pColorAttachments = colorAttachmentReferences.data();
    subpass.pDepthStencilAttachment = &depthAttachmentReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<u32>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device.Handle(), &createInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan GBuffer render pass");
    }
}

void VulkanGBufferRenderPass::Release() {
    if (m_RenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
        m_RenderPass = VK_NULL_HANDLE;
    }
}

VulkanSceneRenderTargets::VulkanSceneRenderTargets(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanSwapchain& swapchain
) {
    Recreate(device, physicalDevice, swapchain);
}

VulkanSceneRenderTargets::~VulkanSceneRenderTargets() {
    Release();
}

VkImageView VulkanSceneRenderTargets::HdrSceneColorView(std::size_t index) const {
    SE_ASSERT(index < m_HdrSceneColorImages.size(), "HDR scene color index is out of range");
    return m_HdrSceneColorImages[index]->View();
}

VkImage VulkanSceneRenderTargets::SceneDepthImage(std::size_t index) const {
    SE_ASSERT(index < m_SceneDepthImages.size(), "Scene depth index is out of range");
    return m_SceneDepthImages[index]->Handle();
}

VkImageView VulkanSceneRenderTargets::SceneDepthView(std::size_t index) const {
    SE_ASSERT(index < m_SceneDepthImages.size(), "Scene depth index is out of range");
    return m_SceneDepthImages[index]->View();
}

VkImageView VulkanSceneRenderTargets::VelocityView(std::size_t index) const {
    SE_ASSERT(index < m_VelocityImages.size(), "Velocity index is out of range");
    return m_VelocityImages[index]->View();
}

VkImageView VulkanSceneRenderTargets::GBufferAlbedoView(std::size_t index) const {
    SE_ASSERT(index < m_GBufferAlbedoImages.size(), "GBuffer albedo index is out of range");
    return m_GBufferAlbedoImages[index]->View();
}

VkImageView VulkanSceneRenderTargets::GBufferNormalRoughnessView(std::size_t index) const {
    SE_ASSERT(
        index < m_GBufferNormalRoughnessImages.size(),
        "GBuffer normal/roughness index is out of range"
    );
    return m_GBufferNormalRoughnessImages[index]->View();
}

VkImageView VulkanSceneRenderTargets::GBufferMaterialView(std::size_t index) const {
    SE_ASSERT(index < m_GBufferMaterialImages.size(), "GBuffer material index is out of range");
    return m_GBufferMaterialImages[index]->View();
}

VkImageView VulkanSceneRenderTargets::GBufferEmissiveView(std::size_t index) const {
    SE_ASSERT(index < m_GBufferEmissiveImages.size(), "GBuffer emissive index is out of range");
    return m_GBufferEmissiveImages[index]->View();
}

VkFormat VulkanSceneRenderTargets::HdrSceneColorFormat() const {
    return kHdrSceneColorFormat;
}

VkFormat VulkanSceneRenderTargets::SceneDepthFormat() const {
    return kSceneDepthFormat;
}

VkFormat VulkanSceneRenderTargets::VelocityFormat() const {
    return kVelocityFormat;
}

VkFormat VulkanSceneRenderTargets::GBufferAlbedoFormat() const {
    return kGBufferAlbedoFormat;
}

VkFormat VulkanSceneRenderTargets::GBufferNormalRoughnessFormat() const {
    return kGBufferNormalRoughnessFormat;
}

VkFormat VulkanSceneRenderTargets::GBufferMaterialFormat() const {
    return kGBufferMaterialFormat;
}

VkFormat VulkanSceneRenderTargets::GBufferEmissiveFormat() const {
    return kGBufferEmissiveFormat;
}

VkExtent2D VulkanSceneRenderTargets::Extent() const {
    return m_Extent;
}

std::size_t VulkanSceneRenderTargets::Count() const {
    return m_HdrSceneColorImages.size();
}

void VulkanSceneRenderTargets::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanSwapchain& swapchain
) {
    Release();
    m_Extent = swapchain.Extent();
    const std::size_t count = swapchain.Images().size();

    CreateImageArray(
        device,
        physicalDevice,
        count,
        kHdrSceneColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_HdrSceneColorImages
    );
    CreateImageArray(
        device,
        physicalDevice,
        count,
        kSceneDepthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        m_SceneDepthImages
    );
    CreateImageArray(
        device,
        physicalDevice,
        count,
        kVelocityFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_VelocityImages
    );
    CreateImageArray(
        device,
        physicalDevice,
        count,
        kGBufferAlbedoFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_GBufferAlbedoImages
    );
    CreateImageArray(
        device,
        physicalDevice,
        count,
        kGBufferNormalRoughnessFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_GBufferNormalRoughnessImages
    );
    CreateImageArray(
        device,
        physicalDevice,
        count,
        kGBufferMaterialFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_GBufferMaterialImages
    );
    CreateImageArray(
        device,
        physicalDevice,
        count,
        kGBufferEmissiveFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_GBufferEmissiveImages
    );
}

void VulkanSceneRenderTargets::Release() {
    m_HdrSceneColorImages.clear();
    m_SceneDepthImages.clear();
    m_VelocityImages.clear();
    m_GBufferAlbedoImages.clear();
    m_GBufferNormalRoughnessImages.clear();
    m_GBufferMaterialImages.clear();
    m_GBufferEmissiveImages.clear();
    m_Extent = {};
}

void VulkanSceneRenderTargets::CreateImageArray(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspectFlags,
    std::vector<std::unique_ptr<VulkanImage>>& images
) const {
    images.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        images.push_back(std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            m_Extent,
            format,
            VK_IMAGE_TILING_OPTIMAL,
            usage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            aspectFlags
        ));
    }
}

VulkanHdrFramebuffer::VulkanHdrFramebuffer(
    const VulkanDevice& device,
    const VulkanHdrRenderPass& renderPass,
    const VulkanSceneRenderTargets& renderTargets
) : m_Device(device.Handle()) {
    CreateFramebuffers(device, renderPass, renderTargets);
}

VulkanHdrFramebuffer::~VulkanHdrFramebuffer() {
    Release();
}

VkFramebuffer VulkanHdrFramebuffer::Handle(std::size_t index) const {
    SE_ASSERT(index < m_Framebuffers.size(), "HDR framebuffer index is out of range");
    return m_Framebuffers[index];
}

VkExtent2D VulkanHdrFramebuffer::Extent() const {
    return m_Extent;
}

std::size_t VulkanHdrFramebuffer::Count() const {
    return m_Framebuffers.size();
}

void VulkanHdrFramebuffer::Recreate(
    const VulkanDevice& device,
    const VulkanHdrRenderPass& renderPass,
    const VulkanSceneRenderTargets& renderTargets
) {
    Release();
    m_Device = device.Handle();
    CreateFramebuffers(device, renderPass, renderTargets);
}

void VulkanHdrFramebuffer::CreateFramebuffers(
    const VulkanDevice& device,
    const VulkanHdrRenderPass& renderPass,
    const VulkanSceneRenderTargets& renderTargets
) {
    m_Extent = renderTargets.Extent();
    m_Framebuffers.resize(renderTargets.Count());

    for (std::size_t index = 0; index < renderTargets.Count(); ++index) {
        const VkImageView attachment = renderTargets.HdrSceneColorView(index);

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
            throw std::runtime_error("Failed to create Vulkan HDR framebuffer");
        }
    }
}

void VulkanHdrFramebuffer::Release() {
    for (VkFramebuffer framebuffer : m_Framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
        }
    }

    m_Framebuffers.clear();
    m_Extent = {};
}

VulkanGBufferFramebuffer::VulkanGBufferFramebuffer(
    const VulkanDevice& device,
    const VulkanGBufferRenderPass& renderPass,
    const VulkanSceneRenderTargets& renderTargets
) : m_Device(device.Handle()) {
    CreateFramebuffers(device, renderPass, renderTargets);
}

VulkanGBufferFramebuffer::~VulkanGBufferFramebuffer() {
    Release();
}

VkFramebuffer VulkanGBufferFramebuffer::Handle(std::size_t index) const {
    SE_ASSERT(index < m_Framebuffers.size(), "GBuffer framebuffer index is out of range");
    return m_Framebuffers[index];
}

VkExtent2D VulkanGBufferFramebuffer::Extent() const {
    return m_Extent;
}

std::size_t VulkanGBufferFramebuffer::Count() const {
    return m_Framebuffers.size();
}

void VulkanGBufferFramebuffer::Recreate(
    const VulkanDevice& device,
    const VulkanGBufferRenderPass& renderPass,
    const VulkanSceneRenderTargets& renderTargets
) {
    Release();
    m_Device = device.Handle();
    CreateFramebuffers(device, renderPass, renderTargets);
}

void VulkanGBufferFramebuffer::CreateFramebuffers(
    const VulkanDevice& device,
    const VulkanGBufferRenderPass& renderPass,
    const VulkanSceneRenderTargets& renderTargets
) {
    m_Extent = renderTargets.Extent();
    m_Framebuffers.resize(renderTargets.Count());

    for (std::size_t index = 0; index < renderTargets.Count(); ++index) {
        const std::array<VkImageView, 6> attachments = {
            renderTargets.GBufferAlbedoView(index),
            renderTargets.GBufferNormalRoughnessView(index),
            renderTargets.GBufferMaterialView(index),
            renderTargets.GBufferEmissiveView(index),
            renderTargets.VelocityView(index),
            renderTargets.SceneDepthView(index)
        };

        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass.Handle();
        createInfo.attachmentCount = static_cast<u32>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.width = m_Extent.width;
        createInfo.height = m_Extent.height;
        createInfo.layers = 1;

        if (vkCreateFramebuffer(device.Handle(), &createInfo, nullptr, &m_Framebuffers[index]) != VK_SUCCESS) {
            Release();
            throw std::runtime_error("Failed to create Vulkan GBuffer framebuffer");
        }
    }
}

void VulkanGBufferFramebuffer::Release() {
    for (VkFramebuffer framebuffer : m_Framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
        }
    }

    m_Framebuffers.clear();
    m_Extent = {};
}

}
