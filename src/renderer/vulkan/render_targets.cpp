#include "renderer/vulkan/render_targets.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/swapchain.h"

#include <array>
#include <cstddef>

namespace se {

namespace {

std::array<u8, 4> LutTexel(u32 red, u32 green, u32 blue, u32 size) {
    const u32 denominator = std::max(size - 1, 1u);
    return {
        static_cast<u8>((red * 255u + denominator / 2u) / denominator),
        static_cast<u8>((green * 255u + denominator / 2u) / denominator),
        static_cast<u8>((blue * 255u + denominator / 2u) / denominator),
        255u
    };
}

std::vector<std::byte> BuildNeutralLutStrip(u32 size) {
    const u32 width = size * size;
    const u32 height = size;
    std::vector<std::byte> texels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u
    );

    for (u32 blue = 0; blue < size; ++blue) {
        for (u32 green = 0; green < size; ++green) {
            for (u32 red = 0; red < size; ++red) {
                const u32 x = blue * size + red;
                const u32 y = green;
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * 4u;
                const std::array<u8, 4> texel = LutTexel(red, green, blue, size);
                texels[offset + 0] = static_cast<std::byte>(texel[0]);
                texels[offset + 1] = static_cast<std::byte>(texel[1]);
                texels[offset + 2] = static_cast<std::byte>(texel[2]);
                texels[offset + 3] = static_cast<std::byte>(texel[3]);
            }
        }
    }

    return texels;
}

} // namespace

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

VulkanWeightedTranslucencyRenderPass::VulkanWeightedTranslucencyRenderPass(
    const VulkanDevice& device,
    const VulkanSceneRenderTargets& renderTargets
) : m_Device(device.Handle()) {
    CreateRenderPass(device, renderTargets);
}

VulkanWeightedTranslucencyRenderPass::~VulkanWeightedTranslucencyRenderPass() {
    Release();
}

VkRenderPass VulkanWeightedTranslucencyRenderPass::Handle() const {
    return m_RenderPass;
}

void VulkanWeightedTranslucencyRenderPass::Recreate(
    const VulkanDevice& device,
    const VulkanSceneRenderTargets& renderTargets
) {
    Release();
    m_Device = device.Handle();
    CreateRenderPass(device, renderTargets);
}

void VulkanWeightedTranslucencyRenderPass::CreateRenderPass(
    const VulkanDevice& device,
    const VulkanSceneRenderTargets& renderTargets
) {
    std::array<VkAttachmentDescription, 3> attachments{};
    attachments[0].format = renderTargets.WeightedTranslucencyAccumFormat();
    attachments[1].format = renderTargets.WeightedTranslucencyRevealageFormat();
    for (std::size_t index = 0; index < 2; ++index) {
        VkAttachmentDescription& attachment = attachments[index];
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    attachments[2].format = renderTargets.SceneDepthFormat();
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentReference, 2> colorAttachmentReferences{};
    for (std::size_t index = 0; index < colorAttachmentReferences.size(); ++index) {
        colorAttachmentReferences[index].attachment = static_cast<u32>(index);
        colorAttachmentReferences[index].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkAttachmentReference depthAttachmentReference{};
    depthAttachmentReference.attachment = 2;
    depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

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
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<u32>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device.Handle(), &createInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan weighted translucency render pass");
    }
}

void VulkanWeightedTranslucencyRenderPass::Release() {
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

VulkanBloomRenderPass::VulkanBloomRenderPass(
    const VulkanDevice& device,
    VkFormat bloomFormat,
    bool loadExistingColor
) : m_Device(device.Handle()) {
    CreateRenderPass(device, bloomFormat, loadExistingColor);
}

VulkanBloomRenderPass::~VulkanBloomRenderPass() {
    Release();
}

VkRenderPass VulkanBloomRenderPass::Handle() const {
    return m_RenderPass;
}

void VulkanBloomRenderPass::Recreate(
    const VulkanDevice& device,
    VkFormat bloomFormat,
    bool loadExistingColor
) {
    Release();
    m_Device = device.Handle();
    CreateRenderPass(device, bloomFormat, loadExistingColor);
}

void VulkanBloomRenderPass::CreateRenderPass(
    const VulkanDevice& device,
    VkFormat bloomFormat,
    bool loadExistingColor
) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = bloomFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp =
        loadExistingColor ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = loadExistingColor
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentReference{};
    colorAttachmentReference.attachment = 0;
    colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentReference;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = loadExistingColor
        ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = loadExistingColor
        ? VK_ACCESS_SHADER_READ_BIT
        : 0;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        (loadExistingColor ? VK_ACCESS_COLOR_ATTACHMENT_READ_BIT : 0);

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = static_cast<u32>(dependencies.size());
    createInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device.Handle(), &createInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan bloom render pass");
    }
}

void VulkanBloomRenderPass::Release() {
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

VkImageView VulkanSceneRenderTargets::WeightedTranslucencyAccumView(std::size_t index) const {
    SE_ASSERT(
        index < m_WeightedTranslucencyAccumImages.size(),
        "Weighted translucency accum index is out of range"
    );
    return m_WeightedTranslucencyAccumImages[index]->View();
}

VkImageView VulkanSceneRenderTargets::WeightedTranslucencyRevealageView(std::size_t index) const {
    SE_ASSERT(
        index < m_WeightedTranslucencyRevealageImages.size(),
        "Weighted translucency revealage index is out of range"
    );
    return m_WeightedTranslucencyRevealageImages[index]->View();
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

VkFormat VulkanSceneRenderTargets::WeightedTranslucencyAccumFormat() const {
    return kWeightedTranslucencyAccumFormat;
}

VkFormat VulkanSceneRenderTargets::WeightedTranslucencyRevealageFormat() const {
    return kWeightedTranslucencyRevealageFormat;
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
        kWeightedTranslucencyAccumFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_WeightedTranslucencyAccumImages
    );
    CreateImageArray(
        device,
        physicalDevice,
        count,
        kWeightedTranslucencyRevealageFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_WeightedTranslucencyRevealageImages
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
    m_WeightedTranslucencyAccumImages.clear();
    m_WeightedTranslucencyRevealageImages.clear();
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

VulkanBloomPyramid::VulkanBloomPyramid(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanSwapchain& swapchain
) {
    Recreate(device, physicalDevice, swapchain);
}

VulkanBloomPyramid::~VulkanBloomPyramid() {
    Release();
}

VkImageView VulkanBloomPyramid::BloomMipView(
    std::size_t imageIndex,
    u32 mipIndex
) const {
    SE_ASSERT(mipIndex < m_BloomMipImages.size(), "Bloom mip index is out of range");
    SE_ASSERT(
        imageIndex < m_BloomMipImages[mipIndex].size(),
        "Bloom image index is out of range"
    );
    return m_BloomMipImages[mipIndex][imageIndex]->View();
}

VkFormat VulkanBloomPyramid::BloomFormat() const {
    return kBloomFormat;
}

VkExtent2D VulkanBloomPyramid::MipExtent(u32 mipIndex) const {
    SE_ASSERT(mipIndex < m_MipExtents.size(), "Bloom mip extent index is out of range");
    return m_MipExtents[mipIndex];
}

std::size_t VulkanBloomPyramid::Count() const {
    return m_BloomMipImages.empty() ? 0 : m_BloomMipImages.front().size();
}

u32 VulkanBloomPyramid::MipCount() const {
    return static_cast<u32>(m_BloomMipImages.size());
}

void VulkanBloomPyramid::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanSwapchain& swapchain
) {
    Release();
    CreateImages(
        device,
        physicalDevice,
        swapchain.Images().size(),
        swapchain.Extent()
    );
}

void VulkanBloomPyramid::Release() {
    m_BloomMipImages.clear();
    m_MipExtents = {};
}

void VulkanBloomPyramid::CreateImages(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count,
    VkExtent2D swapchainExtent
) {
    m_BloomMipImages.resize(kBloomPyramidMipCount);

    u32 mipWidth = std::max(swapchainExtent.width / 2, 1u);
    u32 mipHeight = std::max(swapchainExtent.height / 2, 1u);
    for (u32 mipIndex = 0; mipIndex < kBloomPyramidMipCount; ++mipIndex) {
        VkExtent2D mipExtent{ mipWidth, mipHeight };
        m_MipExtents[mipIndex] = mipExtent;
        std::vector<std::unique_ptr<VulkanImage>>& mipImages =
            m_BloomMipImages[mipIndex];
        mipImages.reserve(count);
        for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
            mipImages.push_back(std::make_unique<VulkanImage>(
                device,
                physicalDevice,
                mipExtent,
                kBloomFormat,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT
            ));
        }
        mipWidth = std::max(mipWidth / 2, 1u);
        mipHeight = std::max(mipHeight / 2, 1u);
    }
}

VulkanBloomFramebuffer::VulkanBloomFramebuffer(
    const VulkanDevice& device,
    const VulkanBloomRenderPass& renderPass,
    const VulkanBloomPyramid& bloomPyramid
) : m_Device(device.Handle()) {
    CreateFramebuffers(device, renderPass, bloomPyramid);
}

VulkanBloomFramebuffer::~VulkanBloomFramebuffer() {
    Release();
}

VkFramebuffer VulkanBloomFramebuffer::Handle(
    std::size_t imageIndex,
    u32 mipIndex
) const {
    SE_ASSERT(mipIndex < m_Framebuffers.size(), "Bloom framebuffer mip index is out of range");
    SE_ASSERT(
        imageIndex < m_Framebuffers[mipIndex].size(),
        "Bloom framebuffer image index is out of range"
    );
    return m_Framebuffers[mipIndex][imageIndex];
}

VkExtent2D VulkanBloomFramebuffer::MipExtent(u32 mipIndex) const {
    SE_ASSERT(mipIndex < m_MipExtents.size(), "Bloom framebuffer mip extent index is out of range");
    return m_MipExtents[mipIndex];
}

std::size_t VulkanBloomFramebuffer::Count() const {
    return m_Framebuffers.empty() ? 0 : m_Framebuffers.front().size();
}

u32 VulkanBloomFramebuffer::MipCount() const {
    return static_cast<u32>(m_Framebuffers.size());
}

void VulkanBloomFramebuffer::Recreate(
    const VulkanDevice& device,
    const VulkanBloomRenderPass& renderPass,
    const VulkanBloomPyramid& bloomPyramid
) {
    Release();
    m_Device = device.Handle();
    CreateFramebuffers(device, renderPass, bloomPyramid);
}

void VulkanBloomFramebuffer::CreateFramebuffers(
    const VulkanDevice& device,
    const VulkanBloomRenderPass& renderPass,
    const VulkanBloomPyramid& bloomPyramid
) {
    const u32 mipCount = bloomPyramid.MipCount();
    m_Framebuffers.resize(mipCount);
    for (u32 mipIndex = 0; mipIndex < mipCount; ++mipIndex) {
        const VkExtent2D mipExtent = bloomPyramid.MipExtent(mipIndex);
        m_MipExtents[mipIndex] = mipExtent;
        std::vector<VkFramebuffer>& mipFramebuffers = m_Framebuffers[mipIndex];
        mipFramebuffers.resize(bloomPyramid.Count());

        for (std::size_t imageIndex = 0; imageIndex < bloomPyramid.Count(); ++imageIndex) {
            const VkImageView attachment =
                bloomPyramid.BloomMipView(imageIndex, mipIndex);

            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = renderPass.Handle();
            createInfo.attachmentCount = 1;
            createInfo.pAttachments = &attachment;
            createInfo.width = mipExtent.width;
            createInfo.height = mipExtent.height;
            createInfo.layers = 1;

            if (vkCreateFramebuffer(
                    device.Handle(),
                    &createInfo,
                    nullptr,
                    &mipFramebuffers[imageIndex]
                ) != VK_SUCCESS) {
                Release();
                throw std::runtime_error("Failed to create Vulkan bloom framebuffer");
            }
        }
    }
}

void VulkanBloomFramebuffer::Release() {
    for (std::vector<VkFramebuffer>& mipFramebuffers : m_Framebuffers) {
        for (VkFramebuffer framebuffer : mipFramebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
            }
        }
    }

    m_Framebuffers.clear();
    m_MipExtents = {};
}

VulkanColorGradingLut::VulkanColorGradingLut(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool
) {
    Recreate(device, physicalDevice, commandPool);
}

VulkanColorGradingLut::~VulkanColorGradingLut() {
    Release();
}

VkImageView VulkanColorGradingLut::View() const {
    return m_Image != nullptr ? m_Image->View() : VK_NULL_HANDLE;
}

VkFormat VulkanColorGradingLut::Format() const {
    return kLutFormat;
}

u32 VulkanColorGradingLut::LutSize() const {
    return kColorGradingLutSize;
}

VkExtent2D VulkanColorGradingLut::Extent() const {
    return m_Extent;
}

bool VulkanColorGradingLut::Uploaded() const {
    return m_Uploaded && m_Image != nullptr;
}

void VulkanColorGradingLut::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool
) {
    Release();
    CreateNeutralLut(device, physicalDevice, commandPool);
}

void VulkanColorGradingLut::Release() {
    m_Image.reset();
    m_Extent = {};
    m_Uploaded = false;
}

void VulkanColorGradingLut::CreateNeutralLut(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool
) {
    m_Extent = {
        kColorGradingLutSize * kColorGradingLutSize,
        kColorGradingLutSize
    };
    const std::vector<std::byte> texels =
        BuildNeutralLutStrip(kColorGradingLutSize);
    VulkanBuffer stagingBuffer(
        device,
        physicalDevice,
        static_cast<VkDeviceSize>(texels.size()),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer.Upload(texels);

    m_Image = std::make_unique<VulkanImage>(
        device,
        physicalDevice,
        m_Extent,
        kLutFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    m_Image->TransitionLayout(
        device,
        commandPool,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    m_Image->CopyFromBuffer(device, commandPool, stagingBuffer.Handle());
    m_Image->TransitionLayout(
        device,
        commandPool,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    m_Uploaded = true;
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

VulkanWeightedTranslucencyFramebuffer::VulkanWeightedTranslucencyFramebuffer(
    const VulkanDevice& device,
    const VulkanWeightedTranslucencyRenderPass& renderPass,
    const VulkanSceneRenderTargets& renderTargets
) : m_Device(device.Handle()) {
    CreateFramebuffers(device, renderPass, renderTargets);
}

VulkanWeightedTranslucencyFramebuffer::~VulkanWeightedTranslucencyFramebuffer() {
    Release();
}

VkFramebuffer VulkanWeightedTranslucencyFramebuffer::Handle(std::size_t index) const {
    SE_ASSERT(index < m_Framebuffers.size(), "Weighted translucency framebuffer index is out of range");
    return m_Framebuffers[index];
}

VkExtent2D VulkanWeightedTranslucencyFramebuffer::Extent() const {
    return m_Extent;
}

std::size_t VulkanWeightedTranslucencyFramebuffer::Count() const {
    return m_Framebuffers.size();
}

void VulkanWeightedTranslucencyFramebuffer::Recreate(
    const VulkanDevice& device,
    const VulkanWeightedTranslucencyRenderPass& renderPass,
    const VulkanSceneRenderTargets& renderTargets
) {
    Release();
    m_Device = device.Handle();
    CreateFramebuffers(device, renderPass, renderTargets);
}

void VulkanWeightedTranslucencyFramebuffer::CreateFramebuffers(
    const VulkanDevice& device,
    const VulkanWeightedTranslucencyRenderPass& renderPass,
    const VulkanSceneRenderTargets& renderTargets
) {
    m_Extent = renderTargets.Extent();
    m_Framebuffers.resize(renderTargets.Count());

    for (std::size_t index = 0; index < renderTargets.Count(); ++index) {
        const std::array<VkImageView, 3> attachments = {
            renderTargets.WeightedTranslucencyAccumView(index),
            renderTargets.WeightedTranslucencyRevealageView(index),
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
            throw std::runtime_error("Failed to create Vulkan weighted translucency framebuffer");
        }
    }
}

void VulkanWeightedTranslucencyFramebuffer::Release() {
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
