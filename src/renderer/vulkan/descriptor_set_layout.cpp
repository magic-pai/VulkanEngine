#include "renderer/vulkan/descriptor_set_layout.h"

#include "renderer/vulkan/device.h"

namespace se {

VkDescriptorSetLayoutBinding BonePaletteDescriptorSetLayoutBinding() {
    VkDescriptorSetLayoutBinding paletteBinding{};
    paletteBinding.binding = kBonePaletteDescriptorBinding;
    paletteBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    paletteBinding.descriptorCount = 1;
    paletteBinding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    paletteBinding.pImmutableSamplers = nullptr;

    return paletteBinding;
}

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout VulkanDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanDescriptorSetLayout::CreateDescriptorSetLayout(const VulkanDevice& device) {
    VkDescriptorSetLayoutBinding uniformBufferLayoutBinding{};
    uniformBufferLayoutBinding.binding = 0;
    uniformBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBufferLayoutBinding.descriptorCount = 1;
    uniformBufferLayoutBinding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    uniformBufferLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding lightBufferLayoutBinding{};
    lightBufferLayoutBinding.binding = 1;
    lightBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightBufferLayoutBinding.descriptorCount = 1;
    lightBufferLayoutBinding.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    lightBufferLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding materialBufferLayoutBinding{};
    materialBufferLayoutBinding.binding = 2;
    materialBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialBufferLayoutBinding.descriptorCount = 1;
    materialBufferLayoutBinding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    materialBufferLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding lightTileDiagnosticsLayoutBinding{};
    lightTileDiagnosticsLayoutBinding.binding = 3;
    lightTileDiagnosticsLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightTileDiagnosticsLayoutBinding.descriptorCount = 1;
    lightTileDiagnosticsLayoutBinding.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    lightTileDiagnosticsLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding directionalShadowCascadeLayoutBinding{};
    directionalShadowCascadeLayoutBinding.binding = 4;
    directionalShadowCascadeLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    directionalShadowCascadeLayoutBinding.descriptorCount = 1;
    directionalShadowCascadeLayoutBinding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    directionalShadowCascadeLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding localShadowLayoutBinding{};
    localShadowLayoutBinding.binding = 5;
    localShadowLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    localShadowLayoutBinding.descriptorCount = 1;
    localShadowLayoutBinding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    localShadowLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding brdfLutLayoutBinding{};
    brdfLutLayoutBinding.binding = 6;
    brdfLutLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    brdfLutLayoutBinding.descriptorCount = 1;
    brdfLutLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding irradianceMapBinding{};
    irradianceMapBinding.binding = 7;
    irradianceMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    irradianceMapBinding.descriptorCount = 1;
    irradianceMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding prefilteredMapBinding{};
    prefilteredMapBinding.binding = 8;
    prefilteredMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    prefilteredMapBinding.descriptorCount = 1;
    prefilteredMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding probeGridBinding{};
    probeGridBinding.binding = 9;
    probeGridBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    probeGridBinding.descriptorCount = 1;
    probeGridBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding autoExposureBinding{};
    autoExposureBinding.binding = 10;
    autoExposureBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    autoExposureBinding.descriptorCount = 1;
    autoExposureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    autoExposureBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding localReflectionProbeMapBinding{};
    localReflectionProbeMapBinding.binding = 11;
    localReflectionProbeMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    localReflectionProbeMapBinding.descriptorCount = 4;
    localReflectionProbeMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    localReflectionProbeMapBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding visibleSkyboxTextureBinding{};
    visibleSkyboxTextureBinding.binding = 12;
    visibleSkyboxTextureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    visibleSkyboxTextureBinding.descriptorCount = 1;
    visibleSkyboxTextureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    visibleSkyboxTextureBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding localReflectionProbeIrradianceMapBinding{};
    localReflectionProbeIrradianceMapBinding.binding = 13;
    localReflectionProbeIrradianceMapBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    localReflectionProbeIrradianceMapBinding.descriptorCount = 4;
    localReflectionProbeIrradianceMapBinding.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT;
    localReflectionProbeIrradianceMapBinding.pImmutableSamplers = nullptr;

    const std::array<VkDescriptorSetLayoutBinding, 14> bindings = {
        uniformBufferLayoutBinding,
        lightBufferLayoutBinding,
        materialBufferLayoutBinding,
        lightTileDiagnosticsLayoutBinding,
        directionalShadowCascadeLayoutBinding,
        localShadowLayoutBinding,
        brdfLutLayoutBinding,
        irradianceMapBinding,
        prefilteredMapBinding,
        probeGridBinding,
        autoExposureBinding,
        localReflectionProbeMapBinding,
        visibleSkyboxTextureBinding,
        localReflectionProbeIrradianceMapBinding
    };

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
        throw std::runtime_error("Failed to create Vulkan frame descriptor set layout");
    }
}

VulkanMaterialDescriptorSetLayout::VulkanMaterialDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanMaterialDescriptorSetLayout::~VulkanMaterialDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout VulkanMaterialDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanMaterialDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanMaterialDescriptorSetLayout::CreateDescriptorSetLayout(const VulkanDevice& device) {
    VkDescriptorSetLayoutBinding albedoSamplerLayoutBinding{};
    albedoSamplerLayoutBinding.binding = 0;
    albedoSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    albedoSamplerLayoutBinding.descriptorCount = 1;
    albedoSamplerLayoutBinding.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    albedoSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding colorMapSamplerLayoutBinding{};
    colorMapSamplerLayoutBinding.binding = 1;
    colorMapSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    colorMapSamplerLayoutBinding.descriptorCount = 1;
    colorMapSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    colorMapSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding cubemapSamplerLayoutBinding{};
    cubemapSamplerLayoutBinding.binding = 2;
    cubemapSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    cubemapSamplerLayoutBinding.descriptorCount = 1;
    cubemapSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    cubemapSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding normalSamplerLayoutBinding{};
    normalSamplerLayoutBinding.binding = 3;
    normalSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalSamplerLayoutBinding.descriptorCount = 1;
    normalSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    normalSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding occlusionSamplerLayoutBinding{};
    occlusionSamplerLayoutBinding.binding = 4;
    occlusionSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    occlusionSamplerLayoutBinding.descriptorCount = 1;
    occlusionSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    occlusionSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding emissiveSamplerLayoutBinding{};
    emissiveSamplerLayoutBinding.binding = 5;
    emissiveSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    emissiveSamplerLayoutBinding.descriptorCount = 1;
    emissiveSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    emissiveSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding shadowSamplerLayoutBinding{};
    shadowSamplerLayoutBinding.binding = 6;
    shadowSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowSamplerLayoutBinding.descriptorCount = 1;
    shadowSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    shadowSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding opacitySamplerLayoutBinding{};
    opacitySamplerLayoutBinding.binding = 7;
    opacitySamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    opacitySamplerLayoutBinding.descriptorCount = 1;
    opacitySamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    opacitySamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding specularSamplerLayoutBinding{};
    specularSamplerLayoutBinding.binding = 8;
    specularSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    specularSamplerLayoutBinding.descriptorCount = 1;
    specularSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    specularSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding clearcoatSamplerLayoutBinding{};
    clearcoatSamplerLayoutBinding.binding = 9;
    clearcoatSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    clearcoatSamplerLayoutBinding.descriptorCount = 1;
    clearcoatSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    clearcoatSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding transmissionSamplerLayoutBinding{};
    transmissionSamplerLayoutBinding.binding = 10;
    transmissionSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    transmissionSamplerLayoutBinding.descriptorCount = 1;
    transmissionSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    transmissionSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding clearcoatRoughnessSamplerLayoutBinding{};
    clearcoatRoughnessSamplerLayoutBinding.binding = 11;
    clearcoatRoughnessSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    clearcoatRoughnessSamplerLayoutBinding.descriptorCount = 1;
    clearcoatRoughnessSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    clearcoatRoughnessSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding localShadowSamplerLayoutBinding{};
    localShadowSamplerLayoutBinding.binding = 12;
    localShadowSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    localShadowSamplerLayoutBinding.descriptorCount = 1;
    localShadowSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    localShadowSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding shadowRawDepthSamplerLayoutBinding{};
    shadowRawDepthSamplerLayoutBinding.binding = 13;
    shadowRawDepthSamplerLayoutBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowRawDepthSamplerLayoutBinding.descriptorCount = 1;
    shadowRawDepthSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    shadowRawDepthSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding localShadowRawDepthSamplerLayoutBinding{};
    localShadowRawDepthSamplerLayoutBinding.binding = 14;
    localShadowRawDepthSamplerLayoutBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    localShadowRawDepthSamplerLayoutBinding.descriptorCount = 1;
    localShadowRawDepthSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    localShadowRawDepthSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding ssrDepthPyramidSamplerLayoutBinding{};
    ssrDepthPyramidSamplerLayoutBinding.binding = 15;
    ssrDepthPyramidSamplerLayoutBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssrDepthPyramidSamplerLayoutBinding.descriptorCount = 1;
    ssrDepthPyramidSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ssrDepthPyramidSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding ssrSceneColorHistorySamplerLayoutBinding{};
    ssrSceneColorHistorySamplerLayoutBinding.binding = 16;
    ssrSceneColorHistorySamplerLayoutBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssrSceneColorHistorySamplerLayoutBinding.descriptorCount = 1;
    ssrSceneColorHistorySamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ssrSceneColorHistorySamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding ssrResolvedSamplerLayoutBinding{};
    ssrResolvedSamplerLayoutBinding.binding = 17;
    ssrResolvedSamplerLayoutBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssrResolvedSamplerLayoutBinding.descriptorCount = 1;
    ssrResolvedSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ssrResolvedSamplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding ssrHistoryMetadataSamplerLayoutBinding{};
    ssrHistoryMetadataSamplerLayoutBinding.binding = 18;
    ssrHistoryMetadataSamplerLayoutBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssrHistoryMetadataSamplerLayoutBinding.descriptorCount = 1;
    ssrHistoryMetadataSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ssrHistoryMetadataSamplerLayoutBinding.pImmutableSamplers = nullptr;

    const std::array<
        VkDescriptorSetLayoutBinding,
        kMaterialDescriptorCombinedImageSamplerCount
    > bindings = {
        albedoSamplerLayoutBinding,
        colorMapSamplerLayoutBinding,
        cubemapSamplerLayoutBinding,
        normalSamplerLayoutBinding,
        occlusionSamplerLayoutBinding,
        emissiveSamplerLayoutBinding,
        shadowSamplerLayoutBinding,
        opacitySamplerLayoutBinding,
        specularSamplerLayoutBinding,
        clearcoatSamplerLayoutBinding,
        transmissionSamplerLayoutBinding,
        clearcoatRoughnessSamplerLayoutBinding,
        localShadowSamplerLayoutBinding,
        shadowRawDepthSamplerLayoutBinding,
        localShadowRawDepthSamplerLayoutBinding,
        ssrDepthPyramidSamplerLayoutBinding,
        ssrSceneColorHistorySamplerLayoutBinding,
        ssrResolvedSamplerLayoutBinding,
        ssrHistoryMetadataSamplerLayoutBinding
    };

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
        throw std::runtime_error("Failed to create Vulkan material descriptor set layout");
    }
}

VulkanHiZDescriptorSetLayout::VulkanHiZDescriptorSetLayout(
    const VulkanDevice& device
) : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanHiZDescriptorSetLayout::~VulkanHiZDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout VulkanHiZDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanHiZDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanHiZDescriptorSetLayout::CreateDescriptorSetLayout(
    const VulkanDevice& device
) {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

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
        throw std::runtime_error("Failed to create Vulkan Hi-Z descriptor set layout");
    }
}

VulkanSsrReconstructionDescriptorSetLayout::
VulkanSsrReconstructionDescriptorSetLayout(const VulkanDevice& device)
    : m_Device(device.Handle()) {
    CreateDescriptorSetLayout(device);
}

VulkanSsrReconstructionDescriptorSetLayout::
~VulkanSsrReconstructionDescriptorSetLayout() {
    Release();
}

VkDescriptorSetLayout VulkanSsrReconstructionDescriptorSetLayout::Handle() const {
    return m_DescriptorSetLayout;
}

void VulkanSsrReconstructionDescriptorSetLayout::Release() {
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanSsrReconstructionDescriptorSetLayout::CreateDescriptorSetLayout(
    const VulkanDevice& device
) {
    std::array<VkDescriptorSetLayoutBinding, 16> bindings{};
    for (u32 binding = 0; binding <= 10; ++binding) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    for (u32 binding = 11; binding <= 13; ++binding) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bindings[14].binding = 14;
    bindings[14].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[14].descriptorCount = 1;
    bindings[14].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[15].binding = 15;
    bindings[15].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[15].descriptorCount = 1;
    bindings[15].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

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
            "Failed to create Vulkan SSR reconstruction descriptor set layout"
        );
    }
}

}

