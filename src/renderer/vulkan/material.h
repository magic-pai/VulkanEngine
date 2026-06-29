#pragma once

#include "renderer/vulkan/cubemap.h"
#include "renderer/vulkan/sampler.h"
#include "renderer/vulkan/texture_2d.h"
#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanUploadBatch;

enum class MaterialRenderClass : u32 {
    DeferredOpaque = 0,
    Transparent = 1,
    ForwardSpecial = 2
};

enum class MaterialAlphaMode : u32 {
    Opaque = 0,
    Mask = 1,
    Blend = 2
};

inline constexpr f32 kMaterialTextureFlagNormal = 8.0f;
inline constexpr f32 kMaterialTextureFlagOcclusion = 16.0f;
inline constexpr f32 kMaterialTextureFlagEmissive = 32.0f;
inline constexpr f32 kMaterialTextureFlagOpacity = 64.0f;
inline constexpr f32 kMaterialTextureFlagSpecular = 128.0f;
inline constexpr f32 kMaterialTextureFlagClearcoat = 256.0f;
inline constexpr f32 kMaterialTextureFlagTransmission = 512.0f;
inline constexpr f32 kMaterialTextureFlagClearcoatRoughness = 1024.0f;

struct MaterialProperties {
    std::array<f32, 4> baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<f32, 4> custom{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<f32, 4> pbrFactors{ 1.0f, 1.0f, 0.0f, 0.0f };
    std::array<f32, 4> emissiveFactor{ 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<f32, 4> specularFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<f32, 4> uvTransform{ 0.0f, 0.0f, 1.0f, 1.0f };
    std::array<f32, 4> uvControls{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<f32, 4> viewControls{ 0.0f, 0.0f, 15.0f, 0.0f };
    std::array<f32, 4> cameraControls{ 15.0f, 1.0f, 0.0f, 0.0f };
    std::array<f32, 4> cameraPosition{ 0.0f, 0.0f, 15.0f, 0.0f };
    std::array<f32, 4> cameraDirection{ 0.0f, 0.0f, -1.0f, 0.0f };
    f32 textureMix = 1.0f;
    f32 alphaCutoff = 0.5f;
    f32 clearcoatFactor = 0.0f;
    f32 clearcoatRoughness = 0.0f;
    f32 transmissionFactor = 0.0f;
    f32 volumeThicknessFactor = 0.0f;
    f32 volumeAttenuationDistance = 0.0f;
    std::array<f32, 4> volumeAttenuationColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool doubleSided = false;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    MaterialRenderClass renderClass = MaterialRenderClass::DeferredOpaque;
};

class VulkanMaterial {
public:
    VulkanMaterial(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string albedoTexturePath,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    VulkanMaterial(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels albedoTexturePixels,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    VulkanMaterial(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes albedoTextureBytes,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );

    ~VulkanMaterial() = default;

    SE_DISABLE_COPY(VulkanMaterial);
    SE_DISABLE_MOVE(VulkanMaterial);

    const VulkanTexture2D& AlbedoTexture() const;
    const VulkanTexture2D& ColorMapTexture() const;
    const VulkanTexture2D& NormalTexture() const;
    const VulkanTexture2D& OcclusionTexture() const;
    const VulkanTexture2D& EmissiveTexture() const;
    const VulkanTexture2D& OpacityTexture() const;
    const VulkanTexture2D& SpecularTexture() const;
    const VulkanTexture2D& ClearcoatTexture() const;
    const VulkanTexture2D& TransmissionTexture() const;
    const VulkanTexture2D& ClearcoatRoughnessTexture() const;
    const VulkanCubemap* SkyboxCubemap() const;
    const VulkanSampler& Sampler() const;
    MaterialProperties& Properties();
    const MaterialProperties& Properties() const;

    void SetColorMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string colorMapPath,
        bool srgb = true,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetColorMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels colorMapPixels,
        bool srgb = true,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetColorMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes colorMapBytes,
        bool srgb = true,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetNormalMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string normalMapPath,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetNormalMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels normalMapPixels,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetNormalMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes normalMapBytes,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetOcclusionMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string occlusionMapPath,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetOcclusionMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels occlusionMapPixels,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetOcclusionMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes occlusionMapBytes,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetEmissiveMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string emissiveMapPath,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetEmissiveMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels emissiveMapPixels,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetEmissiveMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes emissiveMapBytes,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetOpacityMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string opacityMapPath,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetOpacityMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels opacityMapPixels,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetOpacityMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes opacityMapBytes,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetSpecularMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string specularMapPath,
        bool srgb = true,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetSpecularMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels specularMapPixels,
        bool srgb = true,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetSpecularMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes specularMapBytes,
        bool srgb = true,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetClearcoatMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string clearcoatMapPath,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetClearcoatMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels clearcoatMapPixels,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetClearcoatMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes clearcoatMapBytes,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetTransmissionMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string transmissionMapPath,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetTransmissionMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels transmissionMapPixels,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetTransmissionMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes transmissionMapBytes,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetClearcoatRoughnessMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string clearcoatRoughnessMapPath,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetClearcoatRoughnessMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanTexturePixels clearcoatRoughnessMapPixels,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetClearcoatRoughnessMap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanEncodedTextureBytes clearcoatRoughnessMapBytes,
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    void SetSkyboxCubemap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string cubemapDirectory
    );

private:
    VulkanTexture2D m_AlbedoTexture;
    std::unique_ptr<VulkanTexture2D> m_ColorMapTexture;
    std::unique_ptr<VulkanTexture2D> m_NormalTexture;
    std::unique_ptr<VulkanTexture2D> m_OcclusionTexture;
    std::unique_ptr<VulkanTexture2D> m_EmissiveTexture;
    std::unique_ptr<VulkanTexture2D> m_OpacityTexture;
    std::unique_ptr<VulkanTexture2D> m_SpecularTexture;
    std::unique_ptr<VulkanTexture2D> m_ClearcoatTexture;
    std::unique_ptr<VulkanTexture2D> m_TransmissionTexture;
    std::unique_ptr<VulkanTexture2D> m_ClearcoatRoughnessTexture;
    std::unique_ptr<VulkanCubemap> m_SkyboxCubemap;
    VulkanSampler m_Sampler;
    MaterialProperties m_Properties;
};

}
