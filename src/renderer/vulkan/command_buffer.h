#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanCommandPool;
class VulkanDepthBuffer;
class VulkanDescriptorSets;
class VulkanDevice;
class VulkanFramebuffer;
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
        bool skipCachedLocalShadowTiles = false,
        const VulkanHdrRenderPass* hdrRenderPass = nullptr,
        const VulkanHdrFramebuffer* hdrFramebuffer = nullptr,
        const VulkanGraphicsPipeline* deferredLightingPipeline = nullptr,
        const VulkanDescriptorSets* deferredLightingFrameDescriptorSets = nullptr,
        const VulkanGBufferDescriptorSets* deferredLightingGBufferDescriptorSets = nullptr,
        int deferredPbrDebugView = 0,
        const VulkanGraphicsPipeline* hdrCompositePipeline = nullptr,
        const VulkanHdrDescriptorSets* hdrCompositeDescriptorSets = nullptr,
        const VulkanBloomRenderPass* bloomDownsampleRenderPass = nullptr,
        const VulkanBloomRenderPass* bloomUpsampleRenderPass = nullptr,
        const VulkanBloomFramebuffer* bloomDownsampleFramebuffer = nullptr,
        const VulkanBloomFramebuffer* bloomUpsampleFramebuffer = nullptr,
        const VulkanGraphicsPipeline* bloomDownsamplePipeline = nullptr,
        const VulkanGraphicsPipeline* bloomUpsamplePipeline = nullptr,
        const VulkanBloomDescriptorSets* bloomDescriptorSets = nullptr,
        bool recordBloomPyramid = false,
        bool useHdrCompositeAsMain = false,
        bool bloomDebugView = false,
        bool toneMappingDebugView = false,
        bool autoExposureDebugView = false,
        bool colorGradingDebugView = false,
        bool sharpeningDebugView = false,
        const VulkanGraphicsPipeline* gBufferDebugPipeline = nullptr,
        const VulkanGBufferDescriptorSets* gBufferDebugDescriptorSets = nullptr,
        int gBufferDebugView = -1,
        const VulkanGraphicsPipeline* depthPrefillGraphicsPipeline = nullptr,
        const VulkanGraphicsPipeline* doubleSidedDepthPrefillGraphicsPipeline = nullptr,
        const VulkanSceneRenderTargets* sceneRenderTargets = nullptr,
        const VulkanDepthBuffer* swapchainDepthBuffer = nullptr,
        const VulkanGBufferRenderPass* gBufferRenderPass = nullptr,
        const VulkanGBufferFramebuffer* gBufferFramebuffer = nullptr,
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
