#include "renderer/vulkan/descriptor_sets.h"

#include "renderer/vulkan/descriptor_set_layout.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/material.h"
#include "renderer/vulkan/render_targets.h"
#include "renderer/vulkan/sampler.h"
#include "renderer/vulkan/shadow_cascade_atlas.h"
#include "renderer/vulkan/local_shadow_atlas.h"
#include "renderer/vulkan/shadow_map.h"
#include "renderer/vulkan/uniform_buffer.h"

#include <algorithm>

namespace se {

VulkanDescriptorSets::VulkanDescriptorSets(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& descriptorSetLayout,
    const VulkanUniformBuffer& uniformBuffer,
    const VulkanLightBuffer& lightBuffer,
    const VulkanLightTileDiagnosticsBuffer& lightTileDiagnosticsBuffer,
    const VulkanMaterialBuffer& materialBuffer,
    const VulkanProbeGridBuffer& probeGridBuffer,
    const VulkanDirectionalShadowCascadeBuffer& directionalShadowCascadeBuffer,
    const VulkanLocalShadowBuffer& localShadowBuffer,
    const VulkanAutoExposureBuffer& autoExposureBuffer
) : m_Device(device.Handle()) {
    try {
        CreateDescriptorPool(device, uniformBuffer.Count());
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            uniformBuffer,
            lightBuffer,
            lightTileDiagnosticsBuffer,
            materialBuffer,
            probeGridBuffer,
            directionalShadowCascadeBuffer,
            localShadowBuffer,
            autoExposureBuffer
        );
    } catch (...) {
        Release();
        throw;
    }
}

VulkanDescriptorSets::~VulkanDescriptorSets() {
    Release();
}

VkDescriptorSet VulkanDescriptorSets::Handle(std::size_t index) const {
    SE_ASSERT(index < m_DescriptorSets.size(), "Descriptor set index is out of range");
    return m_DescriptorSets[index];
}

std::size_t VulkanDescriptorSets::Count() const {
    return m_DescriptorSets.size();
}

void VulkanDescriptorSets::Recreate(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& descriptorSetLayout,
    const VulkanUniformBuffer& uniformBuffer,
    const VulkanLightBuffer& lightBuffer,
    const VulkanLightTileDiagnosticsBuffer& lightTileDiagnosticsBuffer,
    const VulkanMaterialBuffer& materialBuffer,
    const VulkanProbeGridBuffer& probeGridBuffer,
    const VulkanDirectionalShadowCascadeBuffer& directionalShadowCascadeBuffer,
    const VulkanLocalShadowBuffer& localShadowBuffer,
    const VulkanAutoExposureBuffer& autoExposureBuffer
) {
    Release();
    m_Device = device.Handle();

    try {
        CreateDescriptorPool(device, uniformBuffer.Count());
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            uniformBuffer,
            lightBuffer,
            lightTileDiagnosticsBuffer,
            materialBuffer,
            probeGridBuffer,
            directionalShadowCascadeBuffer,
            localShadowBuffer,
            autoExposureBuffer
        );
    } catch (...) {
        Release();
        throw;
    }
}

void VulkanDescriptorSets::Release() {
    m_DescriptorSets.clear();

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanDescriptorSets::CreateDescriptorPool(
    const VulkanDevice& device,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Descriptor set count must be greater than zero");

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<u32>(count);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = static_cast<u32>(count * 8); // 8 storage bindings
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = static_cast<u32>(
        count * kFrameDescriptorCombinedImageSamplerCount
    );

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(count);

    if (vkCreateDescriptorPool(device.Handle(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan descriptor pool");
    }
}

void VulkanDescriptorSets::CreateDescriptorSets(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& descriptorSetLayout,
    const VulkanUniformBuffer& uniformBuffer,
    const VulkanLightBuffer& lightBuffer,
    const VulkanLightTileDiagnosticsBuffer& lightTileDiagnosticsBuffer,
    const VulkanMaterialBuffer& materialBuffer,
    const VulkanProbeGridBuffer& probeGridBuffer,
    const VulkanDirectionalShadowCascadeBuffer& directionalShadowCascadeBuffer,
    const VulkanLocalShadowBuffer& localShadowBuffer,
    const VulkanAutoExposureBuffer& autoExposureBuffer
) {
    const std::size_t count = uniformBuffer.Count();
    SE_ASSERT(lightBuffer.Count() == count, "Light buffer count must match frame uniform buffer count");
    SE_ASSERT(
        lightTileDiagnosticsBuffer.Count() == count,
        "Light tile diagnostics buffer count must match frame uniform buffer count"
    );
    SE_ASSERT(materialBuffer.Count() == count, "Material buffer count must match frame uniform buffer count");
    SE_ASSERT(probeGridBuffer.Count() == count, "Probe grid buffer count must match frame uniform buffer count");
    SE_ASSERT(
        directionalShadowCascadeBuffer.Count() == count,
        "Directional shadow cascade buffer count must match frame uniform buffer count"
    );
    SE_ASSERT(
        localShadowBuffer.Count() == count,
        "Local shadow buffer count must match frame uniform buffer count"
    );
    SE_ASSERT(
        autoExposureBuffer.Count() == count,
        "Auto exposure buffer count must match frame uniform buffer count"
    );
    std::vector<VkDescriptorSetLayout> layouts(count, descriptorSetLayout.Handle());
    m_DescriptorSets.resize(count);

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_DescriptorPool;
    allocateInfo.descriptorSetCount = static_cast<u32>(count);
    allocateInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device.Handle(), &allocateInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to allocate Vulkan descriptor sets");
    }

    for (std::size_t index = 0; index < count; ++index) {
        const VkDescriptorBufferInfo frameBufferInfo = uniformBuffer.DescriptorInfo(index);
        const VkDescriptorBufferInfo lightBufferInfo = lightBuffer.DescriptorInfo(index);
        const VkDescriptorBufferInfo lightTileDiagnosticsBufferInfo =
            lightTileDiagnosticsBuffer.DescriptorInfo(index);
        const VkDescriptorBufferInfo materialBufferInfo = materialBuffer.DescriptorInfo(index);
        const VkDescriptorBufferInfo probeGridBufferInfo = probeGridBuffer.DescriptorInfo(index);
        const VkDescriptorBufferInfo directionalShadowCascadeBufferInfo =
            directionalShadowCascadeBuffer.DescriptorInfo(index);
        const VkDescriptorBufferInfo localShadowBufferInfo =
            localShadowBuffer.DescriptorInfo(index);
        const VkDescriptorBufferInfo autoExposureBufferInfo =
            autoExposureBuffer.DescriptorInfo(index);

        std::array<VkWriteDescriptorSet, 8> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_DescriptorSets[index];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &frameBufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_DescriptorSets[index];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &lightBufferInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = m_DescriptorSets[index];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pBufferInfo = &materialBufferInfo;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = m_DescriptorSets[index];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &lightTileDiagnosticsBufferInfo;

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = m_DescriptorSets[index];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pBufferInfo = &directionalShadowCascadeBufferInfo;

        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = m_DescriptorSets[index];
        descriptorWrites[5].dstBinding = 5;
        descriptorWrites[5].dstArrayElement = 0;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[5].descriptorCount = 1;
        descriptorWrites[5].pBufferInfo = &localShadowBufferInfo;

        descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[6].dstSet = m_DescriptorSets[index];
        descriptorWrites[6].dstBinding = 9;
        descriptorWrites[6].dstArrayElement = 0;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pBufferInfo = &probeGridBufferInfo;

        descriptorWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[7].dstSet = m_DescriptorSets[index];
        descriptorWrites[7].dstBinding = 10;
        descriptorWrites[7].dstArrayElement = 0;
        descriptorWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[7].descriptorCount = 1;
        descriptorWrites[7].pBufferInfo = &autoExposureBufferInfo;

        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(descriptorWrites.size()),
            descriptorWrites.data(),
            0,
            nullptr
        );
    }
}

VulkanMaterialDescriptorSets::VulkanMaterialDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    std::span<const VulkanMaterial*> materials,
    const VulkanShadowMap* shadowMap,
    const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas,
    const VulkanLocalShadowAtlas* localShadowAtlas
) : m_Device(device.Handle()) {
    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            materials,
            shadowMap,
            cascadeAtlas,
            localShadowAtlas
        );
    } catch (...) {
        Release();
        throw;
    }
}

VulkanMaterialDescriptorSets::~VulkanMaterialDescriptorSets() {
    Release();
}

VkDescriptorSet VulkanMaterialDescriptorSets::Handle(const VulkanMaterial& material) const {
    return Handle(material, 0);
}

VkDescriptorSet VulkanMaterialDescriptorSets::Handle(
    const VulkanMaterial& material,
    std::size_t imageIndex
) const {
    const auto found = std::find(m_Materials.begin(), m_Materials.end(), &material);
    SE_ASSERT(found != m_Materials.end(), "Material does not have a Vulkan descriptor set");
    SE_ASSERT(imageIndex < m_SetsPerMaterial, "Material descriptor image index is out of range");

    const std::size_t materialIndex =
        static_cast<std::size_t>(std::distance(m_Materials.begin(), found));
    const std::size_t descriptorIndex = materialIndex * m_SetsPerMaterial + imageIndex;
    SE_ASSERT(descriptorIndex < m_DescriptorSets.size(), "Material descriptor set index is out of range");
    return m_DescriptorSets[descriptorIndex];
}

std::size_t VulkanMaterialDescriptorSets::Count() const {
    return m_DescriptorSets.size();
}

void VulkanMaterialDescriptorSets::Recreate(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    std::span<const VulkanMaterial*> materials,
    const VulkanShadowMap* shadowMap,
    const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas,
    const VulkanLocalShadowAtlas* localShadowAtlas
) {
    Release();
    m_Device = device.Handle();

    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            materials,
            shadowMap,
            cascadeAtlas,
            localShadowAtlas
        );
    } catch (...) {
        Release();
        throw;
    }
}

void VulkanMaterialDescriptorSets::Release() {
    m_Materials.clear();
    m_DescriptorSets.clear();
    m_SetsPerMaterial = 1;

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanMaterialDescriptorSets::CreateDescriptorPool(
    const VulkanDevice& device,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Material descriptor set count must be greater than zero");

    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = static_cast<u32>(
        count * kMaterialDescriptorCombinedImageSamplerCount
    );

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(count);

    if (vkCreateDescriptorPool(device.Handle(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan material descriptor pool");
    }
}

void VulkanMaterialDescriptorSets::CreateDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    std::span<const VulkanMaterial*> materials,
    const VulkanShadowMap* shadowMap,
    const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas,
    const VulkanLocalShadowAtlas* localShadowAtlas
) {
    for (const VulkanMaterial* material : materials) {
        SE_ASSERT(material != nullptr, "2D render resources contain a null material");
        if (std::find(m_Materials.begin(), m_Materials.end(), material) == m_Materials.end()) {
            m_Materials.push_back(material);
        }
    }

    if (cascadeAtlas != nullptr) {
        m_SetsPerMaterial = cascadeAtlas->Count();
    } else {
        m_SetsPerMaterial = shadowMap != nullptr ? shadowMap->Count() : 1;
    }
    SE_ASSERT(m_SetsPerMaterial > 0, "Material descriptors need at least one set per material");

    CreateDescriptorPool(device, m_Materials.size() * m_SetsPerMaterial);

    const std::size_t descriptorSetCount = m_Materials.size() * m_SetsPerMaterial;
    std::vector<VkDescriptorSetLayout> layouts(descriptorSetCount, descriptorSetLayout.Handle());
    m_DescriptorSets.resize(descriptorSetCount);

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_DescriptorPool;
    allocateInfo.descriptorSetCount = static_cast<u32>(m_DescriptorSets.size());
    allocateInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device.Handle(), &allocateInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to allocate Vulkan material descriptor sets");
    }

    for (std::size_t materialIndex = 0; materialIndex < m_Materials.size(); ++materialIndex) {
        const VulkanMaterial& material = *m_Materials[materialIndex];

        VkDescriptorImageInfo albedoImageInfo{};
        albedoImageInfo.imageLayout = material.AlbedoTexture().Layout();
        albedoImageInfo.imageView = material.AlbedoTexture().View();
        albedoImageInfo.sampler = material.Sampler().Handle();

        VkDescriptorImageInfo colorMapImageInfo{};
        colorMapImageInfo.imageLayout = material.ColorMapTexture().Layout();
        colorMapImageInfo.imageView = material.ColorMapTexture().View();
        colorMapImageInfo.sampler = material.Sampler().Handle();

        VkDescriptorImageInfo cubemapImageInfo{};
        const VulkanCubemap* skyboxCubemap = material.SkyboxCubemap();
        if (skyboxCubemap != nullptr) {
            cubemapImageInfo.imageLayout = skyboxCubemap->Layout();
            cubemapImageInfo.imageView = skyboxCubemap->View();
            cubemapImageInfo.sampler = material.Sampler().Handle();
        } else {
            cubemapImageInfo.imageLayout = material.AlbedoTexture().Layout();
            cubemapImageInfo.imageView = material.AlbedoTexture().View();
            cubemapImageInfo.sampler = material.Sampler().Handle();
        }

        VkDescriptorImageInfo normalImageInfo{};
        normalImageInfo.imageLayout = material.NormalTexture().Layout();
        normalImageInfo.imageView = material.NormalTexture().View();
        normalImageInfo.sampler = material.Sampler().Handle();

        VkDescriptorImageInfo occlusionImageInfo{};
        occlusionImageInfo.imageLayout = material.OcclusionTexture().Layout();
        occlusionImageInfo.imageView = material.OcclusionTexture().View();
        occlusionImageInfo.sampler = material.Sampler().Handle();

        VkDescriptorImageInfo emissiveImageInfo{};
        emissiveImageInfo.imageLayout = material.EmissiveTexture().Layout();
        emissiveImageInfo.imageView = material.EmissiveTexture().View();
        emissiveImageInfo.sampler = material.Sampler().Handle();

        VkDescriptorImageInfo opacityImageInfo{};
        opacityImageInfo.imageLayout = material.OpacityTexture().Layout();
        opacityImageInfo.imageView = material.OpacityTexture().View();
        opacityImageInfo.sampler = material.Sampler().Handle();

        VkDescriptorImageInfo specularImageInfo{};
        specularImageInfo.imageLayout = material.SpecularTexture().Layout();
        specularImageInfo.imageView = material.SpecularTexture().View();
        specularImageInfo.sampler = material.Sampler().Handle();

        VkDescriptorImageInfo clearcoatImageInfo{};
        clearcoatImageInfo.imageLayout = material.ClearcoatTexture().Layout();
        clearcoatImageInfo.imageView = material.ClearcoatTexture().View();
        clearcoatImageInfo.sampler = material.Sampler().Handle();

        VkDescriptorImageInfo transmissionImageInfo{};
        transmissionImageInfo.imageLayout = material.TransmissionTexture().Layout();
        transmissionImageInfo.imageView = material.TransmissionTexture().View();
        transmissionImageInfo.sampler = material.Sampler().Handle();

        VkDescriptorImageInfo clearcoatRoughnessImageInfo{};
        clearcoatRoughnessImageInfo.imageLayout = material.ClearcoatRoughnessTexture().Layout();
        clearcoatRoughnessImageInfo.imageView = material.ClearcoatRoughnessTexture().View();
        clearcoatRoughnessImageInfo.sampler = material.Sampler().Handle();

        for (std::size_t imageIndex = 0; imageIndex < m_SetsPerMaterial; ++imageIndex) {
            const std::size_t descriptorIndex = materialIndex * m_SetsPerMaterial + imageIndex;

            VkDescriptorImageInfo shadowImageInfo{};
            VkDescriptorImageInfo shadowRawDepthImageInfo{};
            if (cascadeAtlas != nullptr) {
                shadowImageInfo.imageLayout = cascadeAtlas->Layout();
                shadowImageInfo.imageView = cascadeAtlas->View(imageIndex);
                shadowImageInfo.sampler = cascadeAtlas->Sampler();
                shadowRawDepthImageInfo = shadowImageInfo;
                shadowRawDepthImageInfo.sampler = cascadeAtlas->RawDepthSampler();
            } else if (shadowMap != nullptr) {
                shadowImageInfo.imageLayout = shadowMap->Layout();
                shadowImageInfo.imageView = shadowMap->View(imageIndex);
                shadowImageInfo.sampler = shadowMap->Sampler();
                shadowRawDepthImageInfo = shadowImageInfo;
                shadowRawDepthImageInfo.sampler = shadowMap->RawDepthSampler();
            } else {
                shadowImageInfo.imageLayout = material.AlbedoTexture().Layout();
                shadowImageInfo.imageView = material.AlbedoTexture().View();
                shadowImageInfo.sampler = material.Sampler().Handle();
                shadowRawDepthImageInfo = shadowImageInfo;
            }

            VkDescriptorImageInfo localShadowImageInfo{};
            VkDescriptorImageInfo localShadowRawDepthImageInfo{};
            if (localShadowAtlas != nullptr) {
                localShadowImageInfo.imageLayout = localShadowAtlas->Layout();
                localShadowImageInfo.imageView = localShadowAtlas->View(imageIndex);
                localShadowImageInfo.sampler = localShadowAtlas->ComparisonSampler();
                localShadowRawDepthImageInfo = localShadowImageInfo;
                localShadowRawDepthImageInfo.sampler = localShadowAtlas->Sampler();
            } else {
                localShadowImageInfo = shadowImageInfo;
                localShadowRawDepthImageInfo = shadowRawDepthImageInfo;
            }

            std::array<VkWriteDescriptorSet, 19> descriptorWrites{};
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pImageInfo = &albedoImageInfo;

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &colorMapImageInfo;

            descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[2].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[2].dstBinding = 2;
            descriptorWrites[2].dstArrayElement = 0;
            descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[2].descriptorCount = 1;
            descriptorWrites[2].pImageInfo = &cubemapImageInfo;

            descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[3].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[3].dstBinding = 3;
            descriptorWrites[3].dstArrayElement = 0;
            descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[3].descriptorCount = 1;
            descriptorWrites[3].pImageInfo = &normalImageInfo;

            descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[4].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[4].dstBinding = 4;
            descriptorWrites[4].dstArrayElement = 0;
            descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[4].descriptorCount = 1;
            descriptorWrites[4].pImageInfo = &occlusionImageInfo;

            descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[5].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[5].dstBinding = 5;
            descriptorWrites[5].dstArrayElement = 0;
            descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[5].descriptorCount = 1;
            descriptorWrites[5].pImageInfo = &emissiveImageInfo;

            descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[6].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[6].dstBinding = 6;
            descriptorWrites[6].dstArrayElement = 0;
            descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[6].descriptorCount = 1;
            descriptorWrites[6].pImageInfo = &shadowImageInfo;

            descriptorWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[7].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[7].dstBinding = 7;
            descriptorWrites[7].dstArrayElement = 0;
            descriptorWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[7].descriptorCount = 1;
            descriptorWrites[7].pImageInfo = &opacityImageInfo;

            descriptorWrites[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[8].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[8].dstBinding = 8;
            descriptorWrites[8].dstArrayElement = 0;
            descriptorWrites[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[8].descriptorCount = 1;
            descriptorWrites[8].pImageInfo = &specularImageInfo;

            descriptorWrites[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[9].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[9].dstBinding = 9;
            descriptorWrites[9].dstArrayElement = 0;
            descriptorWrites[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[9].descriptorCount = 1;
            descriptorWrites[9].pImageInfo = &clearcoatImageInfo;

            descriptorWrites[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[10].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[10].dstBinding = 10;
            descriptorWrites[10].dstArrayElement = 0;
            descriptorWrites[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[10].descriptorCount = 1;
            descriptorWrites[10].pImageInfo = &transmissionImageInfo;

            descriptorWrites[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[11].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[11].dstBinding = 11;
            descriptorWrites[11].dstArrayElement = 0;
            descriptorWrites[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[11].descriptorCount = 1;
            descriptorWrites[11].pImageInfo = &clearcoatRoughnessImageInfo;

            descriptorWrites[12].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[12].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[12].dstBinding = 12;
            descriptorWrites[12].dstArrayElement = 0;
            descriptorWrites[12].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[12].descriptorCount = 1;
            descriptorWrites[12].pImageInfo = &localShadowImageInfo;

            descriptorWrites[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[13].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[13].dstBinding = 13;
            descriptorWrites[13].dstArrayElement = 0;
            descriptorWrites[13].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[13].descriptorCount = 1;
            descriptorWrites[13].pImageInfo = &shadowRawDepthImageInfo;

            descriptorWrites[14].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[14].dstSet = m_DescriptorSets[descriptorIndex];
            descriptorWrites[14].dstBinding = 14;
            descriptorWrites[14].dstArrayElement = 0;
            descriptorWrites[14].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[14].descriptorCount = 1;
            descriptorWrites[14].pImageInfo = &localShadowRawDepthImageInfo;

            // Material/forward paths do not consume the deferred-only SSR bindings,
            // but every shared-layout descriptor set still receives a valid fallback.
            for (std::size_t binding = 15; binding < descriptorWrites.size(); ++binding) {
                descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[binding].dstSet = m_DescriptorSets[descriptorIndex];
                descriptorWrites[binding].dstBinding = static_cast<u32>(binding);
                descriptorWrites[binding].dstArrayElement = 0;
                descriptorWrites[binding].descriptorType =
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrites[binding].descriptorCount = 1;
                descriptorWrites[binding].pImageInfo = &albedoImageInfo;
            }

            vkUpdateDescriptorSets(
                device.Handle(),
                static_cast<u32>(descriptorWrites.size()),
                descriptorWrites.data(),
                0,
                nullptr
            );
        }
    }
}

VulkanGBufferDescriptorSets::VulkanGBufferDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanSampler& sampler,
    const VulkanShadowMap* shadowMap,
    const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas,
    const VulkanLocalShadowAtlas* localShadowAtlas,
    const VulkanDepthPyramid* depthPyramid
) : m_Device(device.Handle()) {
    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            renderTargets,
            sampler,
            shadowMap,
            cascadeAtlas,
            localShadowAtlas,
            depthPyramid
        );
    } catch (...) {
        Release();
        throw;
    }
}

VulkanGBufferDescriptorSets::~VulkanGBufferDescriptorSets() {
    Release();
}

VkDescriptorSet VulkanGBufferDescriptorSets::Handle(std::size_t index) const {
    SE_ASSERT(index < m_DescriptorSets.size(), "GBuffer descriptor set index is out of range");
    return m_DescriptorSets[index];
}

std::size_t VulkanGBufferDescriptorSets::Count() const {
    return m_DescriptorSets.size();
}

bool VulkanGBufferDescriptorSets::UpdateSsrSceneColorHistory(
    const VulkanDevice& device,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanSampler& sampler,
    std::size_t descriptorIndex,
    std::size_t historyImageIndex,
    VkImageView resolvedReflectionOverride,
    VkImageLayout resolvedReflectionLayout
) {
    if (descriptorIndex >= m_DescriptorSets.size() ||
        historyImageIndex >= renderTargets.Count()) {
        return false;
    }

    VkDescriptorImageInfo historyColorInfo{};
    historyColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    historyColorInfo.imageView = renderTargets.TemporalHistoryColorView(historyImageIndex);
    historyColorInfo.sampler = sampler.Handle();

    VkDescriptorImageInfo resolvedInfo{};
    resolvedInfo.imageLayout = resolvedReflectionLayout;
    resolvedInfo.imageView = resolvedReflectionOverride != VK_NULL_HANDLE
        ? resolvedReflectionOverride
        : renderTargets.SsrResolvedView(historyImageIndex);
    resolvedInfo.sampler = sampler.Handle();

    VkDescriptorImageInfo metadataInfo{};
    metadataInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    metadataInfo.imageView = renderTargets.SsrHistoryMetadataView(historyImageIndex);
    metadataInfo.sampler = sampler.Handle();

    std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
    const std::array<VkDescriptorImageInfo*, 3> imageInfos = {
        &historyColorInfo,
        &resolvedInfo,
        &metadataInfo
    };
    const std::array<u32, 3> bindings = { 16u, 17u, 18u };
    for (std::size_t index = 0; index < descriptorWrites.size(); ++index) {
        descriptorWrites[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[index].dstSet = m_DescriptorSets[descriptorIndex];
        descriptorWrites[index].dstBinding = bindings[index];
        descriptorWrites[index].dstArrayElement = 0;
        descriptorWrites[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[index].descriptorCount = 1;
        descriptorWrites[index].pImageInfo = imageInfos[index];
    }
    vkUpdateDescriptorSets(
        device.Handle(),
        static_cast<u32>(descriptorWrites.size()),
        descriptorWrites.data(),
        0,
        nullptr
    );
    return true;
}

void VulkanGBufferDescriptorSets::Recreate(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanSampler& sampler,
    const VulkanShadowMap* shadowMap,
    const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas,
    const VulkanLocalShadowAtlas* localShadowAtlas,
    const VulkanDepthPyramid* depthPyramid
) {
    Release();
    m_Device = device.Handle();

    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            renderTargets,
            sampler,
            shadowMap,
            cascadeAtlas,
            localShadowAtlas,
            depthPyramid
        );
    } catch (...) {
        Release();
        throw;
    }
}

void VulkanGBufferDescriptorSets::Release() {
    m_DescriptorSets.clear();

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanGBufferDescriptorSets::CreateDescriptorPool(
    const VulkanDevice& device,
    std::size_t count
) {
    SE_ASSERT(count > 0, "GBuffer descriptor set count must be greater than zero");

    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = static_cast<u32>(
        count * kMaterialDescriptorCombinedImageSamplerCount
    );

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(count);

    if (vkCreateDescriptorPool(device.Handle(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan GBuffer descriptor pool");
    }
}

void VulkanGBufferDescriptorSets::CreateDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanSampler& sampler,
    const VulkanShadowMap* shadowMap,
    const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas,
    const VulkanLocalShadowAtlas* localShadowAtlas,
    const VulkanDepthPyramid* depthPyramid
) {
    const std::size_t count = renderTargets.Count();
    CreateDescriptorPool(device, count);

    std::vector<VkDescriptorSetLayout> layouts(count, descriptorSetLayout.Handle());
    m_DescriptorSets.resize(count);

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_DescriptorPool;
    allocateInfo.descriptorSetCount = static_cast<u32>(m_DescriptorSets.size());
    allocateInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device.Handle(), &allocateInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to allocate Vulkan GBuffer descriptor sets");
    }

    for (std::size_t index = 0; index < count; ++index) {
        std::array<
            VkDescriptorImageInfo,
            kMaterialDescriptorCombinedImageSamplerCount
        > imageInfos{};
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[0].imageView = renderTargets.GBufferAlbedoView(index);
        imageInfos[0].sampler = sampler.Handle();
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].imageView = renderTargets.GBufferNormalRoughnessView(index);
        imageInfos[1].sampler = sampler.Handle();
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[2].imageView = renderTargets.GBufferMaterialView(index);
        imageInfos[2].sampler = sampler.Handle();
        imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[3].imageView = renderTargets.VelocityView(index);
        imageInfos[3].sampler = sampler.Handle();
        imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfos[4].imageView = renderTargets.SceneDepthView(index);
        imageInfos[4].sampler = sampler.Handle();
        imageInfos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[5].imageView = renderTargets.GBufferEmissiveView(index);
        imageInfos[5].sampler = sampler.Handle();
        if (cascadeAtlas != nullptr) {
            imageInfos[6].imageLayout = cascadeAtlas->Layout();
            imageInfos[6].imageView = cascadeAtlas->View(index);
            imageInfos[6].sampler = cascadeAtlas->Sampler();
        } else if (shadowMap != nullptr) {
            imageInfos[6].imageLayout = shadowMap->Layout();
            imageInfos[6].imageView = shadowMap->View(index);
            imageInfos[6].sampler = shadowMap->Sampler();
        } else {
            imageInfos[6] = imageInfos[0];
        }
        imageInfos[7].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[7].imageView = renderTargets.GBufferMaterialAuxView(index);
        imageInfos[7].sampler = sampler.Handle();
        imageInfos[8] = imageInfos[0];
        imageInfos[9] = imageInfos[0];
        imageInfos[10] = imageInfos[0];
        imageInfos[11] = imageInfos[0];
        if (localShadowAtlas != nullptr) {
            imageInfos[12].imageLayout = localShadowAtlas->Layout();
            imageInfos[12].imageView = localShadowAtlas->View(index);
            imageInfos[12].sampler = localShadowAtlas->ComparisonSampler();
            imageInfos[14] = imageInfos[12];
            imageInfos[14].sampler = localShadowAtlas->Sampler();
        } else {
            imageInfos[12] = imageInfos[6];
        }

        if (cascadeAtlas != nullptr) {
            imageInfos[13].imageLayout = cascadeAtlas->Layout();
            imageInfos[13].imageView = cascadeAtlas->View(index);
            imageInfos[13].sampler = cascadeAtlas->RawDepthSampler();
        } else if (shadowMap != nullptr) {
            imageInfos[13].imageLayout = shadowMap->Layout();
            imageInfos[13].imageView = shadowMap->View(index);
            imageInfos[13].sampler = shadowMap->RawDepthSampler();
        } else {
            imageInfos[13] = imageInfos[0];
        }
        if (localShadowAtlas == nullptr) {
            imageInfos[14] = imageInfos[13];
        }
        if (depthPyramid != nullptr) {
            imageInfos[15].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageInfos[15].imageView = depthPyramid->View(index);
            imageInfos[15].sampler = sampler.Handle();
        } else {
            imageInfos[15] = imageInfos[0];
        }
        imageInfos[16].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[16].imageView = renderTargets.TemporalHistoryColorView(index);
        imageInfos[16].sampler = sampler.Handle();
        imageInfos[17].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[17].imageView = renderTargets.SsrResolvedView(index);
        imageInfos[17].sampler = sampler.Handle();
        imageInfos[18].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[18].imageView = renderTargets.SsrHistoryMetadataView(index);
        imageInfos[18].sampler = sampler.Handle();

        std::array<
            VkWriteDescriptorSet,
            kMaterialDescriptorCombinedImageSamplerCount
        > descriptorWrites{};
        for (std::size_t binding = 0; binding < descriptorWrites.size(); ++binding) {
            descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[binding].dstSet = m_DescriptorSets[index];
            descriptorWrites[binding].dstBinding = static_cast<u32>(binding);
            descriptorWrites[binding].dstArrayElement = 0;
            descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[binding].descriptorCount = 1;
            descriptorWrites[binding].pImageInfo = &imageInfos[binding];
        }

        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(descriptorWrites.size()),
            descriptorWrites.data(),
            0,
            nullptr
        );
    }
}

VulkanHiZDescriptorSets::VulkanHiZDescriptorSets(
    const VulkanDevice& device,
    const VulkanHiZDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanDepthPyramid& depthPyramid,
    const VulkanSampler& sampler
) : m_Device(device.Handle()) {
    CreateDescriptorSets(
        device,
        descriptorSetLayout,
        renderTargets,
        depthPyramid,
        sampler
    );
}

VulkanHiZDescriptorSets::~VulkanHiZDescriptorSets() {
    Release();
}

VkDescriptorSet VulkanHiZDescriptorSets::Handle(
    std::size_t imageIndex,
    u32 mipIndex
) const {
    SE_ASSERT(imageIndex < m_Count, "Hi-Z descriptor image index is out of range");
    SE_ASSERT(mipIndex < m_MipCount, "Hi-Z descriptor mip index is out of range");
    return m_DescriptorSets[imageIndex * m_MipCount + mipIndex];
}

std::size_t VulkanHiZDescriptorSets::Count() const {
    return m_Count;
}

u32 VulkanHiZDescriptorSets::MipCount() const {
    return m_MipCount;
}

void VulkanHiZDescriptorSets::Recreate(
    const VulkanDevice& device,
    const VulkanHiZDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanDepthPyramid& depthPyramid,
    const VulkanSampler& sampler
) {
    Release();
    m_Device = device.Handle();
    CreateDescriptorSets(
        device,
        descriptorSetLayout,
        renderTargets,
        depthPyramid,
        sampler
    );
}

void VulkanHiZDescriptorSets::Release() {
    m_DescriptorSets.clear();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    m_Count = 0;
    m_MipCount = 0;
}

void VulkanHiZDescriptorSets::CreateDescriptorSets(
    const VulkanDevice& device,
    const VulkanHiZDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanDepthPyramid& depthPyramid,
    const VulkanSampler& sampler
) {
    SE_ASSERT(
        renderTargets.Count() == depthPyramid.Count(),
        "Hi-Z source and pyramid image counts must match"
    );
    m_Count = depthPyramid.Count();
    m_MipCount = depthPyramid.MipCount();
    const std::size_t setCount = m_Count * m_MipCount;

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = static_cast<u32>(setCount);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = static_cast<u32>(setCount);
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(setCount);
    if (vkCreateDescriptorPool(
            device.Handle(),
            &poolInfo,
            nullptr,
            &m_DescriptorPool
        ) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan Hi-Z descriptor pool");
    }

    std::vector<VkDescriptorSetLayout> layouts(
        setCount,
        descriptorSetLayout.Handle()
    );
    m_DescriptorSets.resize(setCount);
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_DescriptorPool;
    allocateInfo.descriptorSetCount = static_cast<u32>(setCount);
    allocateInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(
            device.Handle(),
            &allocateInfo,
            m_DescriptorSets.data()
        ) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan Hi-Z descriptor sets");
    }

    for (std::size_t imageIndex = 0; imageIndex < m_Count; ++imageIndex) {
        for (u32 mipIndex = 0; mipIndex < m_MipCount; ++mipIndex) {
            VkDescriptorImageInfo sourceInfo{};
            sourceInfo.imageLayout = mipIndex == 0
                ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_GENERAL;
            sourceInfo.imageView = mipIndex == 0
                ? renderTargets.SceneDepthView(imageIndex)
                : depthPyramid.MipView(imageIndex, mipIndex - 1);
            sourceInfo.sampler = sampler.Handle();

            VkDescriptorImageInfo destinationInfo{};
            destinationInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            destinationInfo.imageView = depthPyramid.MipView(imageIndex, mipIndex);

            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = Handle(imageIndex, mipIndex);
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &sourceInfo;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = Handle(imageIndex, mipIndex);
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &destinationInfo;
            vkUpdateDescriptorSets(
                device.Handle(),
                static_cast<u32>(writes.size()),
                writes.data(),
                0,
                nullptr
            );
        }
    }
}

VulkanSsrReconstructionDescriptorSets::VulkanSsrReconstructionDescriptorSets(
    const VulkanDevice& device,
    const VulkanSsrReconstructionDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanDepthPyramid& depthPyramid,
    const VulkanSampler& sampler
) : m_Device(device.Handle()) {
    CreateDescriptorSets(
        device,
        descriptorSetLayout,
        renderTargets,
        depthPyramid,
        sampler
    );
}

VulkanSsrReconstructionDescriptorSets::~VulkanSsrReconstructionDescriptorSets() {
    Release();
}

VkDescriptorSet VulkanSsrReconstructionDescriptorSets::Handle(
    std::size_t imageIndex
) const {
    SE_ASSERT(
        imageIndex < m_DescriptorSets.size(),
        "SSR reconstruction descriptor image index is out of range"
    );
    return m_DescriptorSets[imageIndex];
}

std::size_t VulkanSsrReconstructionDescriptorSets::Count() const {
    return m_DescriptorSets.size();
}

void VulkanSsrReconstructionDescriptorSets::Recreate(
    const VulkanDevice& device,
    const VulkanSsrReconstructionDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanDepthPyramid& depthPyramid,
    const VulkanSampler& sampler
) {
    Release();
    m_Device = device.Handle();
    CreateDescriptorSets(
        device,
        descriptorSetLayout,
        renderTargets,
        depthPyramid,
        sampler
    );
}

void VulkanSsrReconstructionDescriptorSets::Release() {
    m_DescriptorSets.clear();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanSsrReconstructionDescriptorSets::CreateDescriptorSets(
    const VulkanDevice& device,
    const VulkanSsrReconstructionDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanDepthPyramid& depthPyramid,
    const VulkanSampler& sampler
) {
    SE_ASSERT(
        renderTargets.Count() == depthPyramid.Count(),
        "SSR reconstruction targets and depth pyramid counts must match"
    );
    const std::size_t count = renderTargets.Count();

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = static_cast<u32>(count * 12u);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = static_cast<u32>(count * 4u);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(count);
    if (vkCreateDescriptorPool(
            device.Handle(),
            &poolInfo,
            nullptr,
            &m_DescriptorPool
        ) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create Vulkan SSR reconstruction descriptor pool"
        );
    }

    std::vector<VkDescriptorSetLayout> layouts(
        count,
        descriptorSetLayout.Handle()
    );
    m_DescriptorSets.resize(count);
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_DescriptorPool;
    allocateInfo.descriptorSetCount = static_cast<u32>(count);
    allocateInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(
            device.Handle(),
            &allocateInfo,
            m_DescriptorSets.data()
        ) != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to allocate Vulkan SSR reconstruction descriptor sets"
        );
    }

    for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
        const std::size_t historySourceIndex = count > 1
            ? (imageIndex + 1u) % count
            : imageIndex;
        std::array<VkDescriptorImageInfo, 16> imageInfos{};
        auto sampled = [&](u32 binding, VkImageView view, VkImageLayout layout) {
            imageInfos[binding].imageLayout = layout;
            imageInfos[binding].imageView = view;
            imageInfos[binding].sampler = sampler.Handle();
        };
        sampled(0, renderTargets.SceneDepthView(imageIndex),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        sampled(1, renderTargets.GBufferNormalRoughnessView(imageIndex),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(2, renderTargets.GBufferAlbedoView(imageIndex),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(3, renderTargets.GBufferMaterialView(imageIndex),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(4, renderTargets.GBufferEmissiveView(imageIndex),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(5, renderTargets.VelocityView(imageIndex),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(6, depthPyramid.View(imageIndex), VK_IMAGE_LAYOUT_GENERAL);
        sampled(7, renderTargets.HdrSceneColorView(imageIndex),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sampled(8, renderTargets.SsrRawView(imageIndex), VK_IMAGE_LAYOUT_GENERAL);
        sampled(9, renderTargets.SsrHistoryColorView(historySourceIndex),
            VK_IMAGE_LAYOUT_GENERAL);
        sampled(10, renderTargets.SsrHistoryMetadataView(historySourceIndex),
            VK_IMAGE_LAYOUT_GENERAL);

        imageInfos[11].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[11].imageView = renderTargets.SsrRawView(imageIndex);
        imageInfos[12].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[12].imageView = renderTargets.SsrHistoryColorView(imageIndex);
        imageInfos[13].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[13].imageView = renderTargets.SsrHistoryMetadataView(imageIndex);
        sampled(14, renderTargets.SsrHistoryColorView(imageIndex),
            VK_IMAGE_LAYOUT_GENERAL);
        imageInfos[15].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[15].imageView = renderTargets.SsrResolvedView(imageIndex);

        std::array<VkWriteDescriptorSet, 16> writes{};
        for (u32 binding = 0; binding < writes.size(); ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_DescriptorSets[imageIndex];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType = binding <= 10 || binding == 14
                ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[binding].descriptorCount = 1;
            writes[binding].pImageInfo = &imageInfos[binding];
        }
        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(writes.size()),
            writes.data(),
            0,
            nullptr
        );
    }
}

VulkanHdrDescriptorSets::VulkanHdrDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanBloomPyramid* bloomPyramid,
    const VulkanColorGradingLut* colorGradingLut,
    const VulkanSampler& sampler,
    bool useTemporalUpscaleOutputSource
) : m_Device(device.Handle()) {
    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            renderTargets,
            bloomPyramid,
            colorGradingLut,
            sampler,
            useTemporalUpscaleOutputSource
        );
    } catch (...) {
        Release();
        throw;
    }
}

VulkanHdrDescriptorSets::~VulkanHdrDescriptorSets() {
    Release();
}

VkDescriptorSet VulkanHdrDescriptorSets::Handle(std::size_t index) const {
    SE_ASSERT(index < m_DescriptorSets.size(), "HDR descriptor set index is out of range");
    return m_DescriptorSets[index];
}

std::size_t VulkanHdrDescriptorSets::Count() const {
    return m_DescriptorSets.size();
}

void VulkanHdrDescriptorSets::Recreate(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanBloomPyramid* bloomPyramid,
    const VulkanColorGradingLut* colorGradingLut,
    const VulkanSampler& sampler,
    bool useTemporalUpscaleOutputSource
) {
    Release();
    m_Device = device.Handle();

    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            renderTargets,
            bloomPyramid,
            colorGradingLut,
            sampler,
            useTemporalUpscaleOutputSource
        );
    } catch (...) {
        Release();
        throw;
    }
}

void VulkanHdrDescriptorSets::Release() {
    m_DescriptorSets.clear();

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanHdrDescriptorSets::CreateDescriptorPool(
    const VulkanDevice& device,
    std::size_t count
) {
    SE_ASSERT(count > 0, "HDR descriptor set count must be greater than zero");

    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = static_cast<u32>(
        count * kMaterialDescriptorCombinedImageSamplerCount
    );

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(count);

    if (vkCreateDescriptorPool(device.Handle(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan HDR descriptor pool");
    }
}

void VulkanHdrDescriptorSets::CreateDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanBloomPyramid* bloomPyramid,
    const VulkanColorGradingLut* colorGradingLut,
    const VulkanSampler& sampler,
    bool useTemporalUpscaleOutputSource
) {
    const std::size_t count = renderTargets.Count();
    CreateDescriptorPool(device, count);

    std::vector<VkDescriptorSetLayout> layouts(count, descriptorSetLayout.Handle());
    m_DescriptorSets.resize(count);

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_DescriptorPool;
    allocateInfo.descriptorSetCount = static_cast<u32>(m_DescriptorSets.size());
    allocateInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device.Handle(), &allocateInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to allocate Vulkan HDR descriptor sets");
    }

    for (std::size_t index = 0; index < count; ++index) {
        VkDescriptorImageInfo hdrImageInfo{};
        hdrImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        hdrImageInfo.imageView = useTemporalUpscaleOutputSource
            ? renderTargets.TemporalUpscaleOutputView(index)
            : renderTargets.HdrSceneColorAttachmentView(index);
        hdrImageInfo.sampler = sampler.Handle();

        VkDescriptorImageInfo bloomImageInfo = hdrImageInfo;
        if (bloomPyramid != nullptr &&
            bloomPyramid->Count() == count &&
            bloomPyramid->MipCount() > 0) {
            bloomImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            bloomImageInfo.imageView = bloomPyramid->BloomMipView(index, 0);
            bloomImageInfo.sampler = sampler.Handle();
        }

        VkDescriptorImageInfo colorGradingLutInfo = hdrImageInfo;
        if (colorGradingLut != nullptr &&
            colorGradingLut->Uploaded() &&
            colorGradingLut->View() != VK_NULL_HANDLE) {
            colorGradingLutInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            colorGradingLutInfo.imageView = colorGradingLut->View();
            colorGradingLutInfo.sampler = sampler.Handle();
        }

        VkDescriptorImageInfo temporalHistoryColorInfo = hdrImageInfo;
        temporalHistoryColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        temporalHistoryColorInfo.imageView = renderTargets.TemporalHistoryColorView(index);
        temporalHistoryColorInfo.sampler = sampler.Handle();

        VkDescriptorImageInfo velocityInfo = hdrImageInfo;
        velocityInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        velocityInfo.imageView = renderTargets.VelocityView(index);
        velocityInfo.sampler = sampler.Handle();

        VkDescriptorImageInfo sceneDepthInfo = hdrImageInfo;
        sceneDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        sceneDepthInfo.imageView = renderTargets.SceneDepthView(index);
        sceneDepthInfo.sampler = sampler.Handle();

        std::array<VkWriteDescriptorSet, 13> descriptorWrites{};
        for (std::size_t binding = 0; binding < descriptorWrites.size(); ++binding) {
            descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[binding].dstSet = m_DescriptorSets[index];
            descriptorWrites[binding].dstBinding = static_cast<u32>(binding);
            descriptorWrites[binding].dstArrayElement = 0;
            descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[binding].descriptorCount = 1;
            if (binding == 1) {
                descriptorWrites[binding].pImageInfo = &bloomImageInfo;
            } else if (binding == 2) {
                descriptorWrites[binding].pImageInfo = &colorGradingLutInfo;
            } else if (binding == 3) {
                descriptorWrites[binding].pImageInfo = &temporalHistoryColorInfo;
            } else if (binding == 4) {
                descriptorWrites[binding].pImageInfo = &velocityInfo;
            } else if (binding == 5) {
                descriptorWrites[binding].pImageInfo = &sceneDepthInfo;
            } else {
                descriptorWrites[binding].pImageInfo = &hdrImageInfo;
            }
        }

        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(descriptorWrites.size()),
            descriptorWrites.data(),
            0,
            nullptr
        );
    }
}

VulkanBloomDescriptorSets::VulkanBloomDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanBloomPyramid& bloomPyramid,
    const VulkanSampler& sampler,
    bool useTemporalUpscaleOutputSource
) : m_Device(device.Handle()) {
    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            renderTargets,
            bloomPyramid,
            sampler,
            useTemporalUpscaleOutputSource
        );
    } catch (...) {
        Release();
        throw;
    }
}

VulkanBloomDescriptorSets::~VulkanBloomDescriptorSets() {
    Release();
}

VkDescriptorSet VulkanBloomDescriptorSets::DownsampleHandle(
    std::size_t imageIndex,
    u32 mipIndex
) const {
    const std::size_t descriptorIndex = DescriptorIndex(imageIndex, mipIndex);
    SE_ASSERT(
        descriptorIndex < m_DownsampleDescriptorSets.size(),
        "Bloom downsample descriptor index is out of range"
    );
    return m_DownsampleDescriptorSets[descriptorIndex];
}

VkDescriptorSet VulkanBloomDescriptorSets::UpsampleHandle(
    std::size_t imageIndex,
    u32 mipIndex
) const {
    const std::size_t descriptorIndex = DescriptorIndex(imageIndex, mipIndex);
    SE_ASSERT(
        descriptorIndex < m_UpsampleDescriptorSets.size(),
        "Bloom upsample descriptor index is out of range"
    );
    return m_UpsampleDescriptorSets[descriptorIndex];
}

std::size_t VulkanBloomDescriptorSets::Count() const {
    return m_Count;
}

u32 VulkanBloomDescriptorSets::MipCount() const {
    return m_MipCount;
}

void VulkanBloomDescriptorSets::Recreate(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanBloomPyramid& bloomPyramid,
    const VulkanSampler& sampler,
    bool useTemporalUpscaleOutputSource
) {
    Release();
    m_Device = device.Handle();

    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            renderTargets,
            bloomPyramid,
            sampler,
            useTemporalUpscaleOutputSource
        );
    } catch (...) {
        Release();
        throw;
    }
}

void VulkanBloomDescriptorSets::Release() {
    m_DownsampleDescriptorSets.clear();
    m_UpsampleDescriptorSets.clear();
    m_Count = 0;
    m_MipCount = 0;

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanBloomDescriptorSets::CreateDescriptorPool(
    const VulkanDevice& device,
    std::size_t swapchainCount,
    u32 mipCount
) {
    SE_ASSERT(
        swapchainCount > 0,
        "Bloom descriptor swapchain count must be greater than zero"
    );
    SE_ASSERT(mipCount > 0, "Bloom descriptor mip count must be greater than zero");

    const std::size_t setCount = swapchainCount * mipCount * 2;
    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = static_cast<u32>(
        setCount * kMaterialDescriptorCombinedImageSamplerCount
    );

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(setCount);

    if (vkCreateDescriptorPool(
            device.Handle(),
            &poolInfo,
            nullptr,
            &m_DescriptorPool
        ) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan bloom descriptor pool");
    }
}

void VulkanBloomDescriptorSets::CreateDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanBloomPyramid& bloomPyramid,
    const VulkanSampler& sampler,
    bool useTemporalUpscaleOutputSource
) {
    m_Count = renderTargets.Count();
    m_MipCount = bloomPyramid.MipCount();
    SE_ASSERT(
        bloomPyramid.Count() == m_Count,
        "Bloom pyramid image count must match scene render target count"
    );
    CreateDescriptorPool(device, m_Count, m_MipCount);

    const std::size_t descriptorSetCount = m_Count * m_MipCount;
    std::vector<VkDescriptorSetLayout> layouts(
        descriptorSetCount,
        descriptorSetLayout.Handle()
    );
    m_DownsampleDescriptorSets.resize(descriptorSetCount);
    m_UpsampleDescriptorSets.resize(descriptorSetCount);

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_DescriptorPool;
    allocateInfo.descriptorSetCount = static_cast<u32>(descriptorSetCount);
    allocateInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(
            device.Handle(),
            &allocateInfo,
            m_DownsampleDescriptorSets.data()
        ) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to allocate Vulkan bloom downsample descriptor sets");
    }
    if (vkAllocateDescriptorSets(
            device.Handle(),
            &allocateInfo,
            m_UpsampleDescriptorSets.data()
        ) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to allocate Vulkan bloom upsample descriptor sets");
    }

    for (std::size_t imageIndex = 0; imageIndex < m_Count; ++imageIndex) {
        for (u32 mipIndex = 0; mipIndex < m_MipCount; ++mipIndex) {
            VkDescriptorImageInfo downsampleSourceInfo{};
            downsampleSourceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            downsampleSourceInfo.imageView = mipIndex == 0
                ? (useTemporalUpscaleOutputSource
                    ? renderTargets.TemporalUpscaleOutputView(imageIndex)
                    : renderTargets.HdrSceneColorAttachmentView(imageIndex))
                : bloomPyramid.BloomMipView(imageIndex, mipIndex - 1);
            downsampleSourceInfo.sampler = sampler.Handle();

            VkDescriptorImageInfo currentMipInfo{};
            currentMipInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            currentMipInfo.imageView = bloomPyramid.BloomMipView(imageIndex, mipIndex);
            currentMipInfo.sampler = sampler.Handle();

            VkDescriptorImageInfo lowerMipInfo = currentMipInfo;
            if (mipIndex + 1 < m_MipCount) {
                lowerMipInfo.imageView =
                    bloomPyramid.BloomMipView(imageIndex, mipIndex + 1);
            }

            std::array<VkDescriptorImageInfo, 13> downsampleInfos{};
            downsampleInfos.fill(downsampleSourceInfo);
            downsampleInfos[1] = currentMipInfo;
            downsampleInfos[2] = lowerMipInfo;

            std::array<VkDescriptorImageInfo, 13> upsampleInfos{};
            upsampleInfos.fill(currentMipInfo);
            upsampleInfos[1] = currentMipInfo;
            upsampleInfos[2] = lowerMipInfo;

            std::array<VkWriteDescriptorSet, 13> descriptorWrites{};
            for (std::size_t binding = 0; binding < descriptorWrites.size(); ++binding) {
                descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[binding].dstBinding = static_cast<u32>(binding);
                descriptorWrites[binding].dstArrayElement = 0;
                descriptorWrites[binding].descriptorType =
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrites[binding].descriptorCount = 1;
            }

            const std::size_t descriptorIndex =
                DescriptorIndex(imageIndex, mipIndex);
            for (std::size_t binding = 0; binding < descriptorWrites.size(); ++binding) {
                descriptorWrites[binding].dstSet =
                    m_DownsampleDescriptorSets[descriptorIndex];
                descriptorWrites[binding].pImageInfo = &downsampleInfos[binding];
            }
            vkUpdateDescriptorSets(
                device.Handle(),
                static_cast<u32>(descriptorWrites.size()),
                descriptorWrites.data(),
                0,
                nullptr
            );

            for (std::size_t binding = 0; binding < descriptorWrites.size(); ++binding) {
                descriptorWrites[binding].dstSet =
                    m_UpsampleDescriptorSets[descriptorIndex];
                descriptorWrites[binding].pImageInfo = &upsampleInfos[binding];
            }
            vkUpdateDescriptorSets(
                device.Handle(),
                static_cast<u32>(descriptorWrites.size()),
                descriptorWrites.data(),
                0,
                nullptr
            );
        }
    }
}

std::size_t VulkanBloomDescriptorSets::DescriptorIndex(
    std::size_t imageIndex,
    u32 mipIndex
) const {
    SE_ASSERT(imageIndex < m_Count, "Bloom descriptor image index is out of range");
    SE_ASSERT(mipIndex < m_MipCount, "Bloom descriptor mip index is out of range");
    return imageIndex * m_MipCount + mipIndex;
}

VulkanWeightedTranslucencyDescriptorSets::VulkanWeightedTranslucencyDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanSampler& sampler
) : m_Device(device.Handle()) {
    try {
        CreateDescriptorSets(device, descriptorSetLayout, renderTargets, sampler);
    } catch (...) {
        Release();
        throw;
    }
}

VulkanWeightedTranslucencyDescriptorSets::~VulkanWeightedTranslucencyDescriptorSets() {
    Release();
}

VkDescriptorSet VulkanWeightedTranslucencyDescriptorSets::Handle(std::size_t index) const {
    SE_ASSERT(index < m_DescriptorSets.size(), "Weighted translucency descriptor set index is out of range");
    return m_DescriptorSets[index];
}

std::size_t VulkanWeightedTranslucencyDescriptorSets::Count() const {
    return m_DescriptorSets.size();
}

void VulkanWeightedTranslucencyDescriptorSets::Recreate(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanSampler& sampler
) {
    Release();
    m_Device = device.Handle();

    try {
        CreateDescriptorSets(device, descriptorSetLayout, renderTargets, sampler);
    } catch (...) {
        Release();
        throw;
    }
}

void VulkanWeightedTranslucencyDescriptorSets::Release() {
    m_DescriptorSets.clear();

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanWeightedTranslucencyDescriptorSets::CreateDescriptorPool(
    const VulkanDevice& device,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Weighted translucency descriptor set count must be greater than zero");

    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = static_cast<u32>(
        count * kMaterialDescriptorCombinedImageSamplerCount
    );

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(count);

    if (vkCreateDescriptorPool(device.Handle(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan weighted translucency descriptor pool");
    }
}

void VulkanWeightedTranslucencyDescriptorSets::CreateDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanSampler& sampler
) {
    const std::size_t count = renderTargets.Count();
    CreateDescriptorPool(device, count);

    std::vector<VkDescriptorSetLayout> layouts(count, descriptorSetLayout.Handle());
    m_DescriptorSets.resize(count);

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_DescriptorPool;
    allocateInfo.descriptorSetCount = static_cast<u32>(m_DescriptorSets.size());
    allocateInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device.Handle(), &allocateInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to allocate Vulkan weighted translucency descriptor sets");
    }

    for (std::size_t index = 0; index < count; ++index) {
        std::array<VkDescriptorImageInfo, 13> imageInfos{};
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[0].imageView = renderTargets.HdrSceneColorAttachmentView(index);
        imageInfos[0].sampler = sampler.Handle();
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].imageView = renderTargets.WeightedTranslucencyAccumView(index);
        imageInfos[1].sampler = sampler.Handle();
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[2].imageView = renderTargets.WeightedTranslucencyRevealageView(index);
        imageInfos[2].sampler = sampler.Handle();
        for (std::size_t binding = 3; binding < imageInfos.size(); ++binding) {
            imageInfos[binding] = imageInfos[0];
        }

        std::array<VkWriteDescriptorSet, 13> descriptorWrites{};
        for (std::size_t binding = 0; binding < descriptorWrites.size(); ++binding) {
            descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[binding].dstSet = m_DescriptorSets[index];
            descriptorWrites[binding].dstBinding = static_cast<u32>(binding);
            descriptorWrites[binding].dstArrayElement = 0;
            descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[binding].descriptorCount = 1;
            descriptorWrites[binding].pImageInfo = &imageInfos[binding];
        }

        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(descriptorWrites.size()),
            descriptorWrites.data(),
            0,
            nullptr
        );
    }
}

}
