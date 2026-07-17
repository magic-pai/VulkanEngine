#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <glm/mat4x4.hpp>

namespace se {

class VulkanCommandPool;
class VulkanDepthBuffer;
class VulkanDescriptorSets;
class VulkanDevice;
class VulkanDlssMaskFramebuffer;
class VulkanDlssMaskRenderPass;
class VulkanFramebuffer;
class VulkanForwardResidualVelocityFramebuffer;
class VulkanForwardResidualVelocityRenderPass;
class VulkanGBufferFramebuffer;
class VulkanGBufferDescriptorSets;
class VulkanGBufferRenderPass;
class VulkanGpuTimer;
class VulkanGraphicsPipeline;
class VulkanHdrFramebuffer;
class VulkanHdrDescriptorSets;
class VulkanHdrRenderPass;
class VulkanImGuiLayer;
class VulkanInstanceBuffer;
class VulkanMaterialDescriptorSets;
class VulkanComputePipeline;
class VulkanBloomDescriptorSets;
class VulkanBloomFramebuffer;
class VulkanBloomRenderPass;
class VulkanRenderPass;
class VulkanRenderResources2D;
class VulkanSceneRenderTargets;
class VulkanShadowFramebuffer;
class VulkanShadowRenderPass;
class VulkanSwapchain;
class VulkanWeightedTranslucencyFramebuffer;
class VulkanWeightedTranslucencyDescriptorSets;
class VulkanWeightedTranslucencyRenderPass;
struct RenderCommand;
struct RenderInstanceBatch;
struct RendererBindStats;
struct RenderFrameGraphPlan;
struct FrameMaterialSet;
struct DirectionalShadowCascadeSet;
struct LocalShadowTileSet;
struct FrameTemporalState;
struct FrameTemporalUpscaleState;
struct TemporalUpscalerEvaluateStatus;

enum class TemporalUpscalePostSourceFallbackReason : u32 {
    None = 0,
    Disabled = 1,
    CompositeUnavailable = 2,
    EvaluateOutputUnavailable = 3,
    DescriptorUnavailable = 4
};

struct TemporalUpscalePostSourceStatus {
    u32 requested = 0;
    u32 active = 0;
    TemporalUpscalePostSourceFallbackReason fallbackReason =
        TemporalUpscalePostSourceFallbackReason::Disabled;
};

enum class TemporalHistoryColorCopySource : u32 {
    HdrSceneColor = 0,
    TaaResolvedColor = 1
};

class VulkanDescriptorSets;
class VulkanGraphicsPipeline;
class VulkanMaterialDescriptorSets;

struct ReflectionCaptureDrawStats {
    u32 drawCount = 0;
    u32 materialBindCount = 0;
    u32 meshBindCount = 0;
    u32 pushConstantUpdateCount = 0;
    u64 pushConstantByteCount = 0;
};

struct ReflectionCaptureDirectionalShadowDrawStats {
    u32 passCount = 0;
    u32 drawCount = 0;
    u32 meshBindCount = 0;
    u32 bonePaletteDescriptorBindCount = 0;
    u32 bonePaletteFallbackDescriptorBindCount = 0;
    u32 pushConstantUpdateCount = 0;
    u64 pushConstantByteCount = 0;
};

struct ReflectionCaptureLocalShadowDrawStats {
    u32 tilePassCount = 0;
    u32 drawCount = 0;
    u32 meshBindCount = 0;
    u32 bonePaletteDescriptorBindCount = 0;
    u32 bonePaletteFallbackDescriptorBindCount = 0;
    u32 pushConstantUpdateCount = 0;
    u64 pushConstantByteCount = 0;
};

struct ShadowDepthBiasControls {
    bool enabled = false;
    f32 constantFactor = 0.0f;
    f32 clamp = 0.0f;
    f32 slopeFactor = 0.0f;
};

// Shared forward-material path for a one-face reflection probe capture pass.
ReflectionCaptureDrawStats RecordReflectionCaptureCommands(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedGraphicsPipeline,
    const VulkanDescriptorSets& descriptorSets,
    const VulkanMaterialDescriptorSets& materialDescriptorSets,
    const FrameMaterialSet& frameMaterials,
    std::span<const RenderCommand> renderCommands,
    const VkExtent2D& extent,
    std::size_t imageIndex
);

// Records one full-size directional depth map for an offscreen reflection
// capture. It deliberately does not use the main camera's CSM atlas.
ReflectionCaptureDirectionalShadowDrawStats
RecordReflectionCaptureDirectionalShadow(
    VkCommandBuffer commandBuffer,
    const VulkanShadowRenderPass& shadowRenderPass,
    const VulkanGraphicsPipeline& shadowGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedShadowGraphicsPipeline,
    const VulkanShadowFramebuffer& shadowFramebuffer,
    const VulkanDescriptorSets& shadowDescriptorSets,
    std::span<const RenderCommand> shadowRenderCommands,
    std::size_t imageIndex,
    const glm::mat4& lightViewProjection,
    const ShadowDepthBiasControls& shadowDepthBias,
    VkDescriptorSet bonePaletteFallbackDescriptorSet,
    u32 bonePaletteFallbackDescriptorReady
);

// Records a freshly cleared local-light atlas for a reflection-capture face.
// This path deliberately never consumes the main camera's tile cache.
ReflectionCaptureLocalShadowDrawStats
RecordReflectionCaptureLocalShadows(
    VkCommandBuffer commandBuffer,
    const VulkanShadowRenderPass& shadowRenderPass,
    const VulkanGraphicsPipeline& shadowGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedShadowGraphicsPipeline,
    const VulkanShadowFramebuffer& localShadowFramebuffer,
    const VulkanDescriptorSets& shadowDescriptorSets,
    const LocalShadowTileSet& localShadowTiles,
    std::span<const std::span<const RenderCommand>> localShadowTileRenderCommands,
    std::size_t imageIndex,
    const ShadowDepthBiasControls& shadowDepthBias,
    VkDescriptorSet bonePaletteFallbackDescriptorSet,
    u32 bonePaletteFallbackDescriptorReady
);

class VulkanCommandBuffer {
public:
    VulkanCommandBuffer(
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool,
        const VulkanFramebuffer& framebuffer
    );

    ~VulkanCommandBuffer();

    SE_DISABLE_COPY(VulkanCommandBuffer);
    SE_DISABLE_MOVE(VulkanCommandBuffer);

    const std::vector<VkCommandBuffer>& Handles() const;
    VkCommandBuffer Handle(std::size_t index) const;
    void Record(
        std::size_t imageIndex,
        const VulkanRenderPass& renderPass,
        const VulkanGraphicsPipeline& graphicsPipeline,
        const VulkanGraphicsPipeline* doubleSidedGraphicsPipeline,
        const VulkanDescriptorSets& descriptorSets,
        const VulkanMaterialDescriptorSets& materialDescriptorSets,
        std::span<const RenderCommand> renderCommands,
        const VulkanFramebuffer& framebuffer,
        const VulkanRenderPass* depthLoadRenderPass,
        const VulkanFramebuffer* depthLoadFramebuffer,
        const VulkanSwapchain& swapchain,
        VulkanImGuiLayer* imguiLayer,
        const VulkanShadowRenderPass* shadowRenderPass = nullptr,
        const VulkanGraphicsPipeline* shadowGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* doubleSidedShadowGraphicsPipeline = nullptr,
        const VulkanShadowFramebuffer* shadowFramebuffer = nullptr,
        const VulkanDescriptorSets* shadowDescriptorSets = nullptr,
        std::span<const RenderCommand> shadowRenderCommands = {},
        const VulkanShadowFramebuffer* directionalShadowCascadeFramebuffer = nullptr,
        const DirectionalShadowCascadeSet* directionalShadowCascades = nullptr,
        const VulkanShadowFramebuffer* localShadowFramebuffer = nullptr,
        const LocalShadowTileSet* localShadowTiles = nullptr,
        std::span<const std::span<const RenderCommand>> directionalShadowCascadeRenderCommands = {},
        std::span<const std::span<const RenderCommand>> localShadowTileRenderCommands = {},
        ShadowDepthBiasControls shadowDepthBias = {},
        bool skipCachedLocalShadowTiles = false,
        const VulkanHdrRenderPass* hdrRenderPass = nullptr,
        const VulkanHdrFramebuffer* hdrFramebuffer = nullptr,
        const VulkanGraphicsPipeline* deferredLightingPipeline = nullptr,
        const VulkanDescriptorSets* deferredLightingFrameDescriptorSets = nullptr,
        const VulkanGBufferDescriptorSets* deferredLightingGBufferDescriptorSets = nullptr,
        int deferredPbrDebugView = 0,
        const VulkanGraphicsPipeline* hdrCompositePipeline = nullptr,
        const VulkanHdrDescriptorSets* hdrCompositeDescriptorSets = nullptr,
        const VulkanHdrDescriptorSets* temporalUpscaleHdrCompositeDescriptorSets = nullptr,
        const VulkanBloomRenderPass* bloomDownsampleRenderPass = nullptr,
        const VulkanBloomRenderPass* bloomUpsampleRenderPass = nullptr,
        const VulkanBloomFramebuffer* bloomDownsampleFramebuffer = nullptr,
        const VulkanBloomFramebuffer* bloomUpsampleFramebuffer = nullptr,
        const VulkanGraphicsPipeline* bloomDownsamplePipeline = nullptr,
        const VulkanGraphicsPipeline* bloomUpsamplePipeline = nullptr,
        const VulkanBloomDescriptorSets* bloomDescriptorSets = nullptr,
        const VulkanBloomDescriptorSets* temporalUpscaleBloomDescriptorSets = nullptr,
        bool recordBloomPyramid = false,
        bool useHdrCompositeAsMain = false,
        bool temporalUpscalePostSourceRequested = false,
        bool bloomDebugView = false,
        bool toneMappingDebugView = false,
        bool autoExposureDebugView = false,
        bool colorGradingDebugView = false,
        bool sharpeningDebugView = false,
        bool temporalHistoryColorInitialized = false,
        bool recordTemporalHistoryColorCopy = false,
        TemporalHistoryColorCopySource temporalHistoryColorCopySource =
            TemporalHistoryColorCopySource::HdrSceneColor,
        const VulkanHdrFramebuffer* taaResolveFramebuffer = nullptr,
        const VulkanGraphicsPipeline* taaResolvePipeline = nullptr,
        const FrameTemporalState* temporalState = nullptr,
        const FrameTemporalUpscaleState* temporalUpscaleState = nullptr,
        bool temporalUpscaleOutputInitialized = false,
        bool dlssMaskInputsInitialized = false,
        TemporalUpscalerEvaluateStatus* temporalUpscalerEvaluateStatus = nullptr,
        TemporalUpscalePostSourceStatus* temporalUpscalePostSourceStatus = nullptr,
        const VulkanGraphicsPipeline* gBufferDebugPipeline = nullptr,
        const VulkanGBufferDescriptorSets* gBufferDebugDescriptorSets = nullptr,
        int gBufferDebugView = -1,
        const VulkanGraphicsPipeline* depthPrefillGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* doubleSidedDepthPrefillGraphicsPipeline = nullptr,
        const VulkanSceneRenderTargets* sceneRenderTargets = nullptr,
        const VulkanDepthBuffer* swapchainDepthBuffer = nullptr,
        const VulkanGBufferRenderPass* gBufferRenderPass = nullptr,
        const VulkanGBufferFramebuffer* gBufferFramebuffer = nullptr,
        const VulkanForwardResidualVelocityRenderPass* forwardResidualVelocityRenderPass = nullptr,
        const VulkanForwardResidualVelocityFramebuffer* forwardResidualVelocityFramebuffer = nullptr,
        const VulkanGraphicsPipeline* forwardResidualVelocityGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* doubleSidedForwardResidualVelocityGraphicsPipeline = nullptr,
        std::span<const RenderCommand> forwardResidualVelocityRenderCommands = {},
        std::span<const RenderCommand> weightedTranslucencyVelocityRenderCommands = {},
        const VulkanDlssMaskRenderPass* dlssMaskRenderPass = nullptr,
        const VulkanDlssMaskFramebuffer* dlssMaskFramebuffer = nullptr,
        const VulkanGraphicsPipeline* dlssMaskGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* doubleSidedDlssMaskGraphicsPipeline = nullptr,
        std::span<const RenderCommand> dlssMaskWeightedTranslucencyRenderCommands = {},
        std::span<const RenderCommand> dlssMaskForwardResidualRenderCommands = {},
        const VulkanWeightedTranslucencyRenderPass* weightedTranslucencyRenderPass = nullptr,
        const VulkanWeightedTranslucencyFramebuffer* weightedTranslucencyFramebuffer = nullptr,
        const VulkanGraphicsPipeline* weightedTranslucencyGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* doubleSidedWeightedTranslucencyGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* weightedTranslucencyResolvePipeline = nullptr,
        const VulkanWeightedTranslucencyDescriptorSets* weightedTranslucencyDescriptorSets = nullptr,
        std::span<const RenderCommand> weightedTranslucencyRenderCommands = {},
        int weightedTranslucencyDebugView = 0,
        const VulkanGraphicsPipeline* gBufferGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* doubleSidedGBufferGraphicsPipeline = nullptr,
        const VulkanDescriptorSets* gBufferDescriptorSets = nullptr,
        std::span<const RenderCommand> gBufferRenderCommands = {},
        VkDescriptorSet gBufferBonePaletteFallbackDescriptorSet = VK_NULL_HANDLE,
        u32 gBufferBonePaletteFallbackDescriptorReady = 0,
        const VulkanComputePipeline* lightTileCullComputePipeline = nullptr,
        const VulkanDescriptorSets* lightTileCullDescriptorSets = nullptr,
        u32 lightTileCullGroupCountX = 0,
        u32 lightTileCullGroupCountY = 0,
        u32 lightTileCullGroupCountZ = 1,
        const VulkanComputePipeline* lightClusterCullComputePipeline = nullptr,
        const VulkanComputePipeline* autoExposureComputePipeline = nullptr,
        const VulkanDescriptorSets* autoExposureFrameDescriptorSets = nullptr,
        const VulkanHdrDescriptorSets* autoExposureHdrDescriptorSets = nullptr,
        bool recordAutoExposureCompute = false,
        const VulkanGraphicsPipeline* forwardResidualHdrGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* doubleSidedForwardResidualHdrGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* forwardResidualGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* doubleSidedForwardResidualGraphicsPipeline = nullptr,
        std::span<const RenderCommand> forwardResidualRenderCommands = {},
        const FrameMaterialSet* frameMaterials = nullptr,
        const VulkanGraphicsPipeline* overlayGraphicsPipeline = nullptr,
        const VulkanDescriptorSets* overlayDescriptorSets = nullptr,
        std::span<const RenderCommand> overlayRenderCommands = {},
        const VulkanGraphicsPipeline* instancedGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* doubleSidedInstancedGraphicsPipeline = nullptr,
        const VulkanInstanceBuffer* instanceBuffer = nullptr,
        std::span<const RenderInstanceBatch> instanceBatches = {},
        const VulkanGpuTimer* gpuTimer = nullptr,
        RendererBindStats* bindStats = nullptr,
        RenderFrameGraphPlan* frameGraph = nullptr,
        const VulkanComputePipeline* hizBuildPipeline = nullptr,
        const VulkanSceneRenderTargets* hizTargets = nullptr
    ) const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool,
        const VulkanFramebuffer& framebuffer
    );
    void Release();

private:
    void AllocateCommandBuffers(
        const VulkanDevice& device,
        const VulkanCommandPool& commandPool,
        std::size_t count
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_CommandBuffers;
};

}
