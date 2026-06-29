#include "renderer/vulkan/material.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

#include <utility>

namespace se {

VulkanMaterial::VulkanMaterial(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string albedoTexturePath,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) : m_AlbedoTexture(
        device,
        physicalDevice,
        commandPool,
        std::move(albedoTexturePath),
        true,
        generateMipmaps,
        flipVertically,
        uploadBatch
    ),
    m_Sampler(
        device,
        physicalDevice,
        m_AlbedoTexture.MipLevels()
    ) {
}

VulkanMaterial::VulkanMaterial(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels albedoTexturePixels,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) : m_AlbedoTexture(
        device,
        physicalDevice,
        commandPool,
        albedoTexturePixels,
        true,
        generateMipmaps,
        flipVertically,
        uploadBatch
    ),
    m_Sampler(
        device,
        physicalDevice,
        m_AlbedoTexture.MipLevels()
    ) {
}

VulkanMaterial::VulkanMaterial(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes albedoTextureBytes,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) : m_AlbedoTexture(
        device,
        physicalDevice,
        commandPool,
        albedoTextureBytes,
        true,
        generateMipmaps,
        flipVertically,
        uploadBatch
    ),
    m_Sampler(
        device,
        physicalDevice,
        m_AlbedoTexture.MipLevels()
    ) {
}

const VulkanTexture2D& VulkanMaterial::AlbedoTexture() const {
    return m_AlbedoTexture;
}

const VulkanTexture2D& VulkanMaterial::ColorMapTexture() const {
    if (m_ColorMapTexture != nullptr) {
        return *m_ColorMapTexture;
    }

    return m_AlbedoTexture;
}

const VulkanTexture2D& VulkanMaterial::NormalTexture() const {
    if (m_NormalTexture != nullptr) {
        return *m_NormalTexture;
    }

    return m_AlbedoTexture;
}

const VulkanTexture2D& VulkanMaterial::OcclusionTexture() const {
    if (m_OcclusionTexture != nullptr) {
        return *m_OcclusionTexture;
    }

    return m_AlbedoTexture;
}

const VulkanTexture2D& VulkanMaterial::EmissiveTexture() const {
    if (m_EmissiveTexture != nullptr) {
        return *m_EmissiveTexture;
    }

    return m_AlbedoTexture;
}

const VulkanTexture2D& VulkanMaterial::OpacityTexture() const {
    if (m_OpacityTexture != nullptr) {
        return *m_OpacityTexture;
    }

    return m_AlbedoTexture;
}

const VulkanTexture2D& VulkanMaterial::SpecularTexture() const {
    if (m_SpecularTexture != nullptr) {
        return *m_SpecularTexture;
    }

    return m_AlbedoTexture;
}

const VulkanTexture2D& VulkanMaterial::ClearcoatTexture() const {
    if (m_ClearcoatTexture != nullptr) {
        return *m_ClearcoatTexture;
    }

    return m_AlbedoTexture;
}

const VulkanTexture2D& VulkanMaterial::TransmissionTexture() const {
    if (m_TransmissionTexture != nullptr) {
        return *m_TransmissionTexture;
    }

    return m_AlbedoTexture;
}

const VulkanTexture2D& VulkanMaterial::ClearcoatRoughnessTexture() const {
    if (m_ClearcoatRoughnessTexture != nullptr) {
        return *m_ClearcoatRoughnessTexture;
    }

    return m_AlbedoTexture;
}

const VulkanCubemap* VulkanMaterial::SkyboxCubemap() const {
    return m_SkyboxCubemap.get();
}

const VulkanSampler& VulkanMaterial::Sampler() const {
    return m_Sampler;
}

MaterialProperties& VulkanMaterial::Properties() {
    return m_Properties;
}

const MaterialProperties& VulkanMaterial::Properties() const {
    return m_Properties;
}

void VulkanMaterial::SetColorMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string colorMapPath,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_ColorMapTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        std::move(colorMapPath),
        srgb,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetColorMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels colorMapPixels,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_ColorMapTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        colorMapPixels,
        srgb,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetColorMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes colorMapBytes,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_ColorMapTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        colorMapBytes,
        srgb,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetNormalMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string normalMapPath,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_NormalTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        std::move(normalMapPath),
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetNormalMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels normalMapPixels,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_NormalTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        normalMapPixels,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetNormalMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes normalMapBytes,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_NormalTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        normalMapBytes,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetOcclusionMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string occlusionMapPath,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_OcclusionTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        std::move(occlusionMapPath),
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetOcclusionMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels occlusionMapPixels,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_OcclusionTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        occlusionMapPixels,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetOcclusionMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes occlusionMapBytes,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_OcclusionTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        occlusionMapBytes,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetEmissiveMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string emissiveMapPath,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_EmissiveTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        std::move(emissiveMapPath),
        true,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetEmissiveMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels emissiveMapPixels,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_EmissiveTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        emissiveMapPixels,
        true,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetEmissiveMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes emissiveMapBytes,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_EmissiveTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        emissiveMapBytes,
        true,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetOpacityMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string opacityMapPath,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_OpacityTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        std::move(opacityMapPath),
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetOpacityMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels opacityMapPixels,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_OpacityTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        opacityMapPixels,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetOpacityMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes opacityMapBytes,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_OpacityTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        opacityMapBytes,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetSpecularMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string specularMapPath,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_SpecularTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        std::move(specularMapPath),
        srgb,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetSpecularMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels specularMapPixels,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_SpecularTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        specularMapPixels,
        srgb,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetSpecularMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes specularMapBytes,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_SpecularTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        specularMapBytes,
        srgb,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetClearcoatMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string clearcoatMapPath,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_ClearcoatTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        std::move(clearcoatMapPath),
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetClearcoatMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels clearcoatMapPixels,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_ClearcoatTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        clearcoatMapPixels,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetClearcoatMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes clearcoatMapBytes,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_ClearcoatTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        clearcoatMapBytes,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetTransmissionMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string transmissionMapPath,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_TransmissionTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        std::move(transmissionMapPath),
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetTransmissionMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels transmissionMapPixels,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_TransmissionTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        transmissionMapPixels,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetTransmissionMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes transmissionMapBytes,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_TransmissionTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        transmissionMapBytes,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetClearcoatRoughnessMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string clearcoatRoughnessMapPath,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_ClearcoatRoughnessTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        std::move(clearcoatRoughnessMapPath),
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetClearcoatRoughnessMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels clearcoatRoughnessMapPixels,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_ClearcoatRoughnessTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        clearcoatRoughnessMapPixels,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetClearcoatRoughnessMap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes clearcoatRoughnessMapBytes,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    m_ClearcoatRoughnessTexture = std::make_unique<VulkanTexture2D>(
        device,
        physicalDevice,
        commandPool,
        clearcoatRoughnessMapBytes,
        false,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

void VulkanMaterial::SetSkyboxCubemap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string cubemapDirectory
) {
    m_SkyboxCubemap = std::make_unique<VulkanCubemap>(
        device,
        physicalDevice,
        commandPool,
        std::move(cubemapDirectory)
    );
    m_Sampler.Recreate(device, physicalDevice, m_SkyboxCubemap->MipLevels());
}

}
