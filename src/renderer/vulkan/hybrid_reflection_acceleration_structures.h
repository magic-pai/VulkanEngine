#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;
class VulkanMaterial;
class VulkanPhysicalDevice;
struct RenderCommand;
struct RendererHybridReflectionStats;

inline constexpr u32 kMaxHybridReflectionInstances = 4096u;

struct alignas(8) HybridReflectionInstanceMetadata {
    std::array<u32, 2> vertexAddress{};
    std::array<u32, 2> indexAddress{};
    u32 vertexCount = 0u;
    u32 indexCount = 0u;
    u32 vertexStride = 0u;
    u32 materialIndex = 0u;
    u32 submissionIndex = 0u;
    u32 reflectionAuditObjectId = 0u;
};

static_assert(sizeof(HybridReflectionInstanceMetadata) == 40u);
static_assert(offsetof(HybridReflectionInstanceMetadata, vertexAddress) == 0u);
static_assert(offsetof(HybridReflectionInstanceMetadata, indexAddress) == 8u);
static_assert(offsetof(HybridReflectionInstanceMetadata, vertexCount) == 16u);
static_assert(offsetof(HybridReflectionInstanceMetadata, indexCount) == 20u);
static_assert(offsetof(HybridReflectionInstanceMetadata, vertexStride) == 24u);
static_assert(offsetof(HybridReflectionInstanceMetadata, materialIndex) == 28u);
static_assert(offsetof(HybridReflectionInstanceMetadata, submissionIndex) == 32u);
static_assert(offsetof(HybridReflectionInstanceMetadata, reflectionAuditObjectId) == 36u);

struct HybridReflectionInstanceAuditRecord {
    u32 tlasIndex = std::numeric_limits<u32>::max();
    u32 submissionIndex = 0u;
    u32 materialIndex = 0u;
    u32 instanceFlags = 0u;
    u32 inTlas = 0u;
    u32 exclusionReason = 0u;
    u32 reflectionAuditObjectId = 0u;
    u64 renderIdentity = 0u;
    u64 meshIdentity = 0u;
    u64 materialIdentity = 0u;
    std::string renderableName;
    std::array<f32, 16> model{};
    std::array<f32, 3> boundsMin{};
    std::array<f32, 3> boundsMax{};
    std::array<f32, 4> baseColor{};
    std::array<f32, 4> emissive{};
    u32 vertexCount = 0u;
    u32 indexCount = 0u;
    u32 vertexStride = 0u;
    u32 alphaMode = 0u;
    u32 renderClass = 0u;
    u32 doubleSided = 0u;
    u32 castShadow = 0u;
    u32 reflectionCaptureVisible = 0u;
    f32 metallic = 0.0f;
    f32 roughness = 1.0f;
    f32 textureMix = 0.0f;
    f32 modelDeterminant = 1.0f;
};

class VulkanHybridReflectionAccelerationStructures {
public:
    VulkanHybridReflectionAccelerationStructures(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        u32 frameCount,
        const std::string& skinningShaderPath
    );
    ~VulkanHybridReflectionAccelerationStructures();

    SE_DISABLE_COPY(VulkanHybridReflectionAccelerationStructures);
    SE_DISABLE_MOVE(VulkanHybridReflectionAccelerationStructures);

    void PrepareFrame(
        u32 frameIndex,
        std::span<const RenderCommand> renderCommands,
        RendererHybridReflectionStats& stats
    );
    void RecordBuilds(
        VkCommandBuffer commandBuffer,
        u32 frameIndex,
        RendererHybridReflectionStats& stats
    );

    VkAccelerationStructureKHR TopLevelHandle(u32 frameIndex) const;
    std::span<const HybridReflectionInstanceMetadata> InstanceMetadata(
        u32 frameIndex
    ) const;
    std::span<const VulkanMaterial* const> InstanceMaterials(
        u32 frameIndex
    ) const;
    u32 FindInstanceIndexBySubmissionIndex(
        u32 frameIndex,
        u32 submissionIndex
    ) const;
    std::span<const HybridReflectionInstanceAuditRecord> InstanceAuditRecords(
        u32 frameIndex
    ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

}
