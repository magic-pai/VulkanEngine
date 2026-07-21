#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanCommandPool;
class VulkanDepthPyramid;
class VulkanDevice;
class VulkanFfxSssrBlueNoiseResources;
class VulkanFfxSssrClassifyTilesResources;
class VulkanFfxSssrConstantsDescriptorSetLayout;
class VulkanFfxSssrPrepareIndirectArgsResources;
class VulkanMaterial;
class VulkanLightBuffer;
class VulkanPhysicalDevice;
class VulkanSceneRenderTargets;
struct HybridReflectionInstanceMetadata;
struct RendererHybridReflectionStats;

inline constexpr u32 kMaxHybridReflectionMaterials = 256u;

struct HybridReflectionRayQuerySettings {
    f32 maxRayDistance = 100.0f;
    f32 screenHitConfidenceThreshold = 0.75f;
    f32 originBiasMin = 0.002f;
    f32 originBiasScale = 0.00025f;
    f32 originBiasMax = 0.05f;
    u32 maxShadowedLocalLights = 8u;
    u32 rectangleShadowSampleCount = 4u;
};

struct HybridReflectionRayQueryDiagnostics {
    bool valid = false;
    u32 candidateRayCount = 0u;
    u32 screenHitAcceptedCount = 0u;
    u32 traceCount = 0u;
    u32 committedHitCount = 0u;
    u32 missCount = 0u;
    u32 invalidRayCount = 0u;
    u32 hitDistanceSumMillimeters = 0u;
    u32 hitDistanceMinMillimeters = 0u;
    u32 hitDistanceMaxMillimeters = 0u;
    u32 resultPixelWriteCount = 0u;
    u32 hitAttributeResolvedCount = 0u;
    u32 invalidInstanceCount = 0u;
    u32 invalidPrimitiveCount = 0u;
    u32 invalidVertexCount = 0u;
    u32 invalidBarycentricCount = 0u;
    u32 invalidAttributeValueCount = 0u;
    u32 materialResolvedCount = 0u;
    u32 materialFallbackCount = 0u;
    u32 positionMismatchCount = 0u;
    u32 positionErrorMaxMicrometers = 0u;
    u32 normalLengthMinPermille = 0u;
    u32 normalLengthMaxPermille = 0u;
    u32 barycentricSumMinPermille = 0u;
    u32 barycentricSumMaxPermille = 0u;
    u32 identityChecksum = 0u;
    u32 primitiveChecksum = 0u;
    u32 materialChecksum = 0u;
    u32 materialRecordResolvedCount = 0u;
    u32 materialRecordFallbackCount = 0u;
    u32 textureSampleResolvedCount = 0u;
    u32 textureSampleFallbackCount = 0u;
    u32 textureSampleInvalidCount = 0u;
    u32 finiteSampledColorCount = 0u;
    u32 sampleLodMinMillilevels = 0u;
    u32 sampleLodMaxMillilevels = 0u;
    u32 hitSurfacePayloadWriteCount = 0u;
    u32 hitSurfacePayloadChecksum = 0u;
    u32 hitSurfaceLuminanceMinMilliunits = 0u;
    u32 hitSurfaceLuminanceMaxMilliunits = 0u;
    u32 hitLightingResolvedCount = 0u;
    u32 hitLightingInvalidCount = 0u;
    u32 directionalLightEvaluationCount = 0u;
    u32 directionalLightContributionCount = 0u;
    u32 pointLightEvaluationCount = 0u;
    u32 pointLightContributionCount = 0u;
    u32 spotLightEvaluationCount = 0u;
    u32 spotLightContributionCount = 0u;
    u32 rectLightEvaluationCount = 0u;
    u32 rectLightContributionCount = 0u;
    u32 finiteDirectRadianceCount = 0u;
    u32 finiteIblRadianceCount = 0u;
    u32 finiteEmissiveRadianceCount = 0u;
    u32 finiteRadianceCount = 0u;
    u32 directLuminanceSumMilliunits = 0u;
    u32 iblLuminanceSumMilliunits = 0u;
    u32 emissiveLuminanceSumMilliunits = 0u;
    u32 radianceLuminanceMinMilliunits = 0u;
    u32 radianceLuminanceMaxMilliunits = 0u;
    u32 radianceChecksum = 0u;
    u32 shadowVisibilityResolvedCount = 0u;
    u32 shadowRayCount = 0u;
    u32 shadowVisibleCount = 0u;
    u32 shadowOccludedCount = 0u;
    u32 shadowInvalidCount = 0u;
    u32 directionalShadowRayCount = 0u;
    u32 pointShadowRayCount = 0u;
    u32 spotShadowRayCount = 0u;
    u32 rectShadowRayCount = 0u;
    u32 localShadowCandidateCount = 0u;
    u32 localShadowSelectedCount = 0u;
    u32 localShadowDroppedCount = 0u;
    u32 unshadowedDirectLuminanceSumMilliunits = 0u;
    u32 visibleDirectLuminanceSumMilliunits = 0u;
    u32 shadowSelfIntersectionCandidateCount = 0u;
    u32 shadowHitDistanceMinMillimeters = 0u;
    u32 shadowHitDistanceMaxMillimeters = 0u;
    u32 shadowVisibilityMinPermille = 0u;
    u32 shadowVisibilityMaxPermille = 0u;
    u32 localShadowDroppedLuminanceSumMilliunits = 0u;
};

class VulkanHybridReflectionRayQuery {
public:
    VulkanHybridReflectionRayQuery(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        const VulkanFfxSssrConstantsDescriptorSetLayout& constantsLayout,
        const VulkanFfxSssrClassifyTilesResources& classifyResources,
        const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
        const VulkanFfxSssrBlueNoiseResources& blueNoiseResources,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanDepthPyramid& depthPyramid,
        const VulkanLightBuffer& lightBuffer,
        VkImageView iblBrdfView,
        VkImageView iblIrradianceView,
        VkImageView iblPrefilteredView,
        VkSampler iblSampler,
        u32 iblPrefilteredMipCount,
        const std::string& computeShaderPath
    );
    ~VulkanHybridReflectionRayQuery();

    SE_DISABLE_COPY(VulkanHybridReflectionRayQuery);
    SE_DISABLE_MOVE(VulkanHybridReflectionRayQuery);

    void PrepareFrame(
        const VulkanDevice& device,
        u32 imageIndex,
        VkAccelerationStructureKHR topLevelAccelerationStructure,
        std::span<const HybridReflectionInstanceMetadata> instanceMetadata,
        std::span<const VulkanMaterial* const> instanceMaterials,
        bool enabled,
        bool hitAttributesEnabled,
        bool materialTexturesEnabled,
        bool hitLightingEnabled,
        bool shadowVisibilityEnabled,
        u32 directionalLightCount,
        u32 localLightCount,
        const HybridReflectionRayQuerySettings& settings,
        RendererHybridReflectionStats& stats
    );
    void Record(
        VkCommandBuffer commandBuffer,
        u32 imageIndex,
        VkDescriptorSet ffxConstantsDescriptorSet,
        VkBuffer indirectArgsBuffer,
        RendererHybridReflectionStats& stats
    );

    HybridReflectionRayQueryDiagnostics ReadDiagnostics(u32 imageIndex) const;
    std::size_t Count() const;
    VkExtent2D Extent() const;
    VkFormat ResultFormat() const;
    VkImage HitSurfaceImage(u32 imageIndex) const;
    VkImageView HitSurfaceView(u32 imageIndex) const;
    VkFormat HitSurfaceFormat() const;
    u64 TotalMemoryBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

}
