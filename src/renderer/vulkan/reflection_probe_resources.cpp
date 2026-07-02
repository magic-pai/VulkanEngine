#include "renderer/vulkan/reflection_probe_resources.h"

#include "renderer/vulkan/ibl_generator.h"
#include "renderer/vulkan/image.h"

namespace se {

void VulkanReflectionProbeResources::CreateBuiltInProcedural(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool
) {
    GenerateReflectionProbeCubemap(
        device,
        physicalDevice,
        commandPool,
        m_BuiltInCubemapImage,
        m_BuiltInCubemapView
    );
}

void VulkanReflectionProbeResources::Release() {
    m_BuiltInCubemapImage.reset();
    m_BuiltInCubemapView = VK_NULL_HANDLE;
    m_DescriptorSetsBound = 0;
}

bool VulkanReflectionProbeResources::BuiltInProceduralReady(
    VkSampler sampler
) const {
    return m_BuiltInCubemapImage != nullptr &&
        m_BuiltInCubemapView != VK_NULL_HANDLE &&
        sampler != VK_NULL_HANDLE;
}

VkImageView VulkanReflectionProbeResources::DescriptorViewFor(
    VkImageView fallbackView,
    VkSampler sampler
) const {
    return BuiltInProceduralReady(sampler) ? m_BuiltInCubemapView : fallbackView;
}

VkImageView VulkanReflectionProbeResources::BuiltInView() const {
    return m_BuiltInCubemapView;
}

u32 VulkanReflectionProbeResources::FaceSize() const {
    return m_BuiltInCubemapImage != nullptr
        ? m_BuiltInCubemapImage->Extent().width
        : 0u;
}

u32 VulkanReflectionProbeResources::MipCount() const {
    return m_BuiltInCubemapImage != nullptr
        ? m_BuiltInCubemapImage->MipLevels()
        : 0u;
}

VkFormat VulkanReflectionProbeResources::Format() const {
    return m_BuiltInCubemapImage != nullptr
        ? m_BuiltInCubemapImage->Format()
        : VK_FORMAT_UNDEFINED;
}

void VulkanReflectionProbeResources::SetDescriptorSetsBound(u32 count) {
    m_DescriptorSetsBound = count;
}

u32 VulkanReflectionProbeResources::DescriptorSetsBound() const {
    return m_DescriptorSetsBound;
}

}
