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
    poolSizes[2].descriptorCount = static_cast<u32>(count * 6);

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
        const VkDescriptorBufferInfo directionalShadowCascadeBufferInfo =
            directionalShadowCascadeBuffer.DescriptorInfo(index);
        const VkDescriptorBufferInfo localShadowBufferInfo =
            localShadowBuffer.DescriptorInfo(index);
        const VkDescriptorBufferInfo autoExposureBufferInfo =
            autoExposureBuffer.DescriptorInfo(index);

        std::array<VkWriteDescriptorSet, 7> descriptorWrites{};
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
        descriptorWrites[6].dstBinding = 10;
        descriptorWrites[6].dstArrayElement = 0;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pBufferInfo = &autoExposureBufferInfo;

        // Binding 9 (probe grid): placeholder, updated when probe grid buffer is active
        { VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
          w.dstSet=m_DescriptorSets[index]; w.dstBinding=9;
          w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.descriptorCount=1;
          w.pBufferInfo=&localShadowBufferInfo;
          vkUpdateDescriptorSets(device.Handle(), 1, &w, 0, nullptr); }

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
    poolSizes[0].descriptorCount = static_cast<u32>(count * 13);

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
            if (cascadeAtlas != nullptr) {
                shadowImageInfo.imageLayout = cascadeAtlas->Layout();
                shadowImageInfo.imageView = cascadeAtlas->View(imageIndex);
                shadowImageInfo.sampler = cascadeAtlas->Sampler();
            } else if (shadowMap != nullptr) {
                shadowImageInfo.imageLayout = shadowMap->Layout();
                shadowImageInfo.imageView = shadowMap->View(imageIndex);
                shadowImageInfo.sampler = shadowMap->Sampler();
            } else {
                shadowImageInfo.imageLayout = material.AlbedoTexture().Layout();
                shadowImageInfo.imageView = material.AlbedoTexture().View();
                shadowImageInfo.sampler = material.Sampler().Handle();
            }

            VkDescriptorImageInfo localShadowImageInfo{};
            if (localShadowAtlas != nullptr) {
                localShadowImageInfo.imageLayout = localShadowAtlas->Layout();
                localShadowImageInfo.imageView = localShadowAtlas->View(imageIndex);
                localShadowImageInfo.sampler = localShadowAtlas->Sampler();
            } else {
                localShadowImageInfo.imageLayout = material.AlbedoTexture().Layout();
                localShadowImageInfo.imageView = material.AlbedoTexture().View();
                localShadowImageInfo.sampler = material.Sampler().Handle();
            }

            std::array<VkWriteDescriptorSet, 13> descriptorWrites{};
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
    const VulkanLocalShadowAtlas* localShadowAtlas
) : m_Device(device.Handle()) {
    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            renderTargets,
            sampler,
            shadowMap,
            cascadeAtlas,
            localShadowAtlas
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

void VulkanGBufferDescriptorSets::Recreate(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanSampler& sampler,
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
            renderTargets,
            sampler,
            shadowMap,
            cascadeAtlas,
            localShadowAtlas
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
    poolSizes[0].descriptorCount = static_cast<u32>(count * 13);

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
    const VulkanLocalShadowAtlas* localShadowAtlas
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
        std::array<VkDescriptorImageInfo, 13> imageInfos{};
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
        imageInfos[7] = imageInfos[0];
        imageInfos[8] = imageInfos[0];
        imageInfos[9] = imageInfos[0];
        imageInfos[10] = imageInfos[0];
        imageInfos[11] = imageInfos[0];
        if (localShadowAtlas != nullptr) {
            imageInfos[12].imageLayout = localShadowAtlas->Layout();
            imageInfos[12].imageView = localShadowAtlas->View(index);
            imageInfos[12].sampler = localShadowAtlas->Sampler();
        } else {
            imageInfos[12] = imageInfos[0];
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

VulkanHdrDescriptorSets::VulkanHdrDescriptorSets(
    const VulkanDevice& device,
    const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanBloomPyramid* bloomPyramid,
    const VulkanColorGradingLut* colorGradingLut,
    const VulkanSampler& sampler
) : m_Device(device.Handle()) {
    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            renderTargets,
            bloomPyramid,
            colorGradingLut,
            sampler
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
    const VulkanSampler& sampler
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
            sampler
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
    poolSizes[0].descriptorCount = static_cast<u32>(count * 13);

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
        throw std::runtime_error("Failed to allocate Vulkan HDR descriptor sets");
    }

    for (std::size_t index = 0; index < count; ++index) {
        VkDescriptorImageInfo hdrImageInfo{};
        hdrImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        hdrImageInfo.imageView = renderTargets.HdrSceneColorView(index);
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
    const VulkanSampler& sampler
) : m_Device(device.Handle()) {
    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            renderTargets,
            bloomPyramid,
            sampler
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
    const VulkanSampler& sampler
) {
    Release();
    m_Device = device.Handle();

    try {
        CreateDescriptorSets(
            device,
            descriptorSetLayout,
            renderTargets,
            bloomPyramid,
            sampler
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
    poolSizes[0].descriptorCount = static_cast<u32>(setCount * 13);

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
    const VulkanSampler& sampler
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
                ? renderTargets.HdrSceneColorView(imageIndex)
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
    poolSizes[0].descriptorCount = static_cast<u32>(count * 13);

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
        imageInfos[0].imageView = renderTargets.HdrSceneColorView(index);
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
