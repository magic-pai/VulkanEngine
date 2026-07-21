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
class VulkanPhysicalDevice;
class VulkanSceneRenderTargets;
struct HybridReflectionInstanceMetadata;
struct RendererHybridReflectionStats;

struct HybridReflectionRayQuerySettings {
    f32 maxRayDistance = 100.0f;
    f32 screenHitConfidenceThreshold = 0.75f;
    f32 originBiasMin = 0.002f;
    f32 originBiasScale = 0.00025f;
    f32 originBiasMax = 0.05f;
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
        u32 instanceMaterialCount,
        bool enabled,
        bool hitAttributesEnabled,
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
    u64 TotalMemoryBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

}
