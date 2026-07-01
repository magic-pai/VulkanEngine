#pragma once

#include "renderer/vulkan/vulkan_common.h"
#include "renderer/vulkan/pipeline_spec.h"
#include "renderer/vulkan/render_debug_settings.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/shadow_settings.h"
#include "renderer/vulkan/vertex.h"
#include "renderer/render_queue.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <array>
#include <functional>

namespace se {

class VulkanCommandBuffer;
class VulkanCommandPool;
class VulkanComputePipeline;
class VulkanDepthBuffer;
class VulkanDescriptorSetLayout;
class VulkanDescriptorSets;
class VulkanDevice;
class VulkanFramebuffer;
class VulkanGpuTimer;
class VulkanGBufferDescriptorSets;
class VulkanGraphicsPipeline;
class VulkanHdrDescriptorSets;
class VulkanWeightedTranslucencyDescriptorSets;
class VulkanImGuiLayer;
class VulkanInstanceBuffer;
class VulkanLocalShadowAtlas;
class VulkanMaterialDescriptorSetLayout;
class VulkanMaterialDescriptorSets;
class VulkanPhysicalDevice;
class VulkanRenderPass;
class VulkanRenderResources2D;
class VulkanSampler;
class VulkanGBufferFramebuffer;
class VulkanGBufferRenderPass;
class VulkanHdrFramebuffer;
class VulkanHdrRenderPass;
class VulkanSceneRenderTargets;
class VulkanWeightedTranslucencyFramebuffer;
class VulkanWeightedTranslucencyRenderPass;
class VulkanShadowFramebuffer;
class VulkanDirectionalShadowCascadeAtlas;
class VulkanShadowMap;
class VulkanShadowRenderPass;
class VulkanSurface;
class VulkanSwapchain;
class VulkanSyncObjects;
class VulkanLightBuffer;
class VulkanLightTileDiagnosticsBuffer;
class VulkanMaterialBuffer;
class VulkanDirectionalShadowCascadeBuffer;
class VulkanLocalShadowBuffer;
class VulkanUniformBuffer;
class Camera2D;
class Camera3D;
class Scene2D;
class Scene3D;
class Window;
struct FrameMaterialSet;

struct FrameMatrices {
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
};

struct FrameLightConstants {
    glm::vec4 directionalLight{ -0.45f, -0.82f, -0.35f, 0.78f };
    glm::vec4 ambientLight{ 0.22f, 0.24f, 0.0f, 0.0f };
};

enum class RendererLightKind : u32 {
    Directional = 0,
    Point = 1,
    Spot = 2,
    Rect = 3
};

struct RendererDirectionalLight {
    RendererLightKind kind = RendererLightKind::Directional;
    glm::vec3 direction{ -0.45f, -0.82f, -0.35f };
    f32 intensity = 0.78f;
    f32 ambient = 0.22f;
    f32 specular = 0.24f;
};

struct RendererLocalLight {
    RendererLightKind kind = RendererLightKind::Point;
    glm::vec3 position{ 0.0f };
    f32 radius = 1.0f;
    glm::vec3 color{ 1.0f };
    f32 intensity = 1.0f;
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    f32 innerConeCos = 1.0f;
    f32 outerConeCos = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
};

inline constexpr std::size_t kRendererMaxFrameLocalLights = 64;

struct FrameLightSet {
    RendererDirectionalLight primaryDirectional{};
    std::array<RendererLocalLight, kRendererMaxFrameLocalLights> localLights{};
    u32 directionalCount = 1;
    u32 localCount = 0;
    u32 rectCount = 0;

    FrameLightConstants Constants() const;
};

struct FrameLightTileStats {
    u32 tileSize = 0;
    u32 tileCountX = 0;
    u32 tileCountY = 0;
    u32 tileCount = 0;
    u32 assignments = 0;
    u32 assignmentCapacity = 0;
    u32 overflowAssignments = 0;
    u32 overflowCapacity = 0;
    u32 overflowTileCount = 0;
    u32 overflowDropped = 0;
    u32 fallbackCount = 0;
};

struct FrameLightTileGpuReadbackStats {
    bool valid = false;
    u32 saturatedTileCount = 0;
    u32 maxRawCandidateCount = 0;
    u64 rawCandidateCountSum = 0;
    u32 overflowUsedTileCount = 0;
    u32 overflowDroppedTileCount = 0;
    u32 overflowStoredCount = 0;
    u32 overflowDroppedCount = 0;
};

struct DirectionalShadowCascade {
    glm::mat4 viewProjection{ 1.0f };
    f32 nearDepth = 0.0f;
    f32 farDepth = 0.0f;
    f32 splitDepth = 0.0f;
    f32 texelWorldSize = 0.0f;
};

struct DirectionalShadowCascadeSet {
    std::array<DirectionalShadowCascade, kMaxDirectionalShadowCascades> cascades{};
    u32 configuredCount = 0;
    u32 activeCount = 0;
    bool stableSnappingEnabled = false;
    f32 splitLambda = 0.0f;
    f32 maxDistance = 0.0f;
    f32 nearDepth = 0.0f;
    f32 farDepth = 0.0f;
};

inline constexpr std::size_t kMaxLocalShadowTiles = 64;

struct LocalShadowTile {
    glm::mat4 viewProjection{ 1.0f };
    u32 tileIndex = 0;
    u32 localLightIndex = 0;
    u32 faceIndex = 0;
    u32 lightKind = 0;
    u64 cacheKey = 0;
    bool cacheReusable = false;
};

struct LocalShadowTileSet {
    std::array<LocalShadowTile, kMaxLocalShadowTiles> tiles{};
    std::array<u64, kMaxLocalShadowTiles> cacheKeys{};
    u32 tileSize = 0;
    u32 tileColumns = 0;
    u32 tileRows = 0;
    u32 tileCapacity = 0;
    u32 requestedCount = 0;
    u32 assignedCount = 0;
    u32 droppedCount = 0;
    u32 pointLightCount = 0;
    u32 spotLightCount = 0;
    u32 pointFaceTiles = 0;
    u32 spotTiles = 0;
    u32 cacheEligibleTiles = 0;
    u32 cacheHitTiles = 0;
    u32 cacheMissTiles = 0;
    u32 cacheSkippedTiles = 0;
};

struct LocalShadowCacheState {
    std::array<u64, kMaxLocalShadowTiles> tileKeys{};
    u32 tileCount = 0;
    bool valid = false;
};

struct RenderQueueContext {
    const Frustum* frustum = nullptr;
    RenderQueueCullingStats* cullingStats = nullptr;
    RenderQueueCacheStats* cacheStats = nullptr;
    RenderQueue* shadowRenderQueue = nullptr;
    RenderQueueCullingStats* shadowCullingStats = nullptr;
};

class VulkanRenderer {
public:
    using FrameMatricesProvider = std::function<FrameMatrices(f32 aspectRatio)>;
    using RenderQueueBuilder = std::function<void(
        RenderQueue& renderQueue,
        const RenderQueueContext& context
    )>;

    VulkanRenderer(
        Window& window,
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanSurface& surface,
        VkInstance instance,
        const VulkanCommandPool& commandPool,
        Scene2D* scene,
        Camera2D* camera,
        const VulkanRenderResources2D& renderResources,
        PipelineSpec pipelineSpec
    );

    ~VulkanRenderer();

    SE_DISABLE_COPY(VulkanRenderer);
    SE_DISABLE_MOVE(VulkanRenderer);

    void DrawFrame();
    void WaitIdle() const;
    void SetFrameMatricesProvider(FrameMatricesProvider provider);
    void SetRenderQueueBuilder(RenderQueueBuilder builder);
    void SetImGui3DContext(Scene3D* scene, Camera3D* camera);
    void SetOverlay3DContext(Scene3D* scene, Camera3D* camera, PipelineSpec pipelineSpec);
    void RefreshMaterialDescriptors();
    VulkanRenderDebugSettings& RenderDebugSettings();
    const VulkanRenderDebugSettings& RenderDebugSettings() const;
    const RendererStats& Stats() const;
    VulkanShadowSettings& ShadowSettings();
    const VulkanShadowSettings& ShadowSettings() const;

private:
    void ValidateSceneResources() const;
    void CreateSwapchainResources();
    void RecreateSwapchain();
    void ApplyEnvironmentRenderSettings();
    void ApplyShadowMapSettings();
    void ResetLocalShadowCacheStates();
    void HandleObjectPicking();
    glm::vec2 CursorToWorldPosition(const VkExtent2D& extent) const;
    void UpdateUniformBuffer(
        std::size_t imageIndex,
        const FrameMatrices* matrices,
        const glm::mat4& lightViewProjection,
        const FrameLightConstants& lights,
        bool shadowSamplingEnabled
    ) const;
    void UpdateOverlayUniformBuffer(
        std::size_t imageIndex,
        const FrameMatrices* matrices,
        const glm::mat4& lightViewProjection,
        const FrameLightConstants& lights,
        bool shadowSamplingEnabled
    ) const;
    void UpdateLightBuffer(
        std::size_t imageIndex,
        const FrameLightSet& lights,
        const VkExtent2D& extent,
        const FrameMatrices* matrices,
        FrameLightTileStats* tileStats
    ) const;
    FrameLightTileGpuReadbackStats ReadPreviousLightTileGpuStats(
        std::size_t imageIndex
    ) const;
    void UpdateMaterialBuffer(
        std::size_t imageIndex,
        const FrameMaterialSet& materials
    ) const;
    void UpdateDirectionalShadowCascadeBuffer(
        std::size_t imageIndex,
        const DirectionalShadowCascadeSet& cascades,
        const glm::mat4& fallbackLightViewProjection
    ) const;
    void UpdateLocalShadowBuffer(
        std::size_t imageIndex,
        const LocalShadowTileSet& localShadowTiles
    ) const;
    FrameLightSet BuildFrameLightSet(std::span<const RenderCommand> renderCommands) const;
    FrameMaterialSet BuildFrameMaterialSet(std::span<const RenderCommand> renderCommands) const;
    void BuildGBufferCommandList(
        std::span<const RenderCommand> renderCommands,
        std::vector<RenderCommand>& gBufferCommands,
        std::vector<RenderCommand>& weightedTranslucencyCommands,
        std::vector<RenderCommand>& forwardResidualCommands,
        const FrameMatrices* matrices,
        bool recordTransparentAlphaReference,
        RendererDrawStats& drawStats
    ) const;
    glm::mat4 LightViewProjection(
        std::span<const RenderCommand> renderCommands,
        const FrameLightSet& lights
    ) const;
    DirectionalShadowCascadeSet BuildDirectionalShadowCascades(
        std::span<const RenderCommand> renderCommands,
        const FrameLightSet& lights,
        const FrameMatrices* matrices,
        bool shadowSamplingEnabled
    ) const;
    LocalShadowTileSet BuildLocalShadowTiles(
        const FrameLightSet& lights,
        std::span<const RenderCommand> shadowCommands,
        u32 atlasTileCapacity,
        const LocalShadowCacheState* cacheState
    ) const;
    std::span<const RenderCommand> ShadowRenderCommands() const;
    const VulkanDescriptorSets* ShadowDescriptorSets() const;
    bool BuildMainInstanceBatches(
        std::span<const RenderCommand> commands,
        bool allowCacheReuse
    );
    bool UploadMainInstancesIfNeeded(std::size_t imageIndex);

private:
    Window& m_Window;
    const VulkanDevice& m_Device;
    const VulkanPhysicalDevice& m_PhysicalDevice;
    const VulkanSurface& m_Surface;
    VkInstance m_Instance = VK_NULL_HANDLE;
    const VulkanCommandPool& m_CommandPool;
    Scene2D* m_Scene = nullptr;
    Camera2D* m_Camera = nullptr;
    Scene3D* m_MainScene3D = nullptr;
    Scene3D* m_ImGuiScene3D = nullptr;
    Camera3D* m_ImGuiCamera3D = nullptr;
    Scene3D* m_OverlayScene3D = nullptr;
    Camera3D* m_OverlayCamera3D = nullptr;
    const VulkanRenderResources2D& m_RenderResources;
    PipelineSpec m_PipelineSpec;
    std::optional<PipelineSpec> m_OverlayPipelineSpec;
    FrameMatricesProvider m_FrameMatricesProvider;
    RenderQueueBuilder m_RenderQueueBuilder;
    RenderQueue m_RenderQueue;
    RenderQueue m_OverlayRenderQueue;
    RenderQueue m_ShadowRenderQueue;
    std::vector<LocalShadowCacheState> m_LocalShadowCacheStates;
    VulkanRenderDebugSettings m_RenderDebugSettings;
    VulkanShadowSettings m_ShadowSettings;
    RendererStats m_LastStats;
    std::size_t m_CurrentFrame = 0;

    std::unique_ptr<VulkanSwapchain> m_Swapchain;
    std::unique_ptr<VulkanDescriptorSetLayout> m_DescriptorSetLayout;
    std::unique_ptr<VulkanMaterialDescriptorSetLayout> m_MaterialDescriptorSetLayout;
    std::unique_ptr<VulkanUniformBuffer> m_UniformBuffer;
    std::unique_ptr<VulkanUniformBuffer> m_OverlayUniformBuffer;
    std::unique_ptr<VulkanDescriptorSets> m_DescriptorSets;
    std::unique_ptr<VulkanDescriptorSets> m_OverlayDescriptorSets;
    std::unique_ptr<VulkanMaterialDescriptorSets> m_MaterialDescriptorSets;
    std::unique_ptr<VulkanGBufferDescriptorSets> m_GBufferDescriptorSets;
    std::unique_ptr<VulkanHdrDescriptorSets> m_HdrDescriptorSets;
    std::unique_ptr<VulkanWeightedTranslucencyDescriptorSets> m_WeightedTranslucencyDescriptorSets;
    std::unique_ptr<VulkanLightBuffer> m_LightBuffer;
    std::unique_ptr<VulkanLightTileDiagnosticsBuffer> m_LightTileDiagnosticsBuffer;
    std::unique_ptr<VulkanMaterialBuffer> m_MaterialBuffer;
    std::unique_ptr<VulkanDirectionalShadowCascadeBuffer> m_DirectionalShadowCascadeBuffer;
    std::unique_ptr<VulkanLocalShadowBuffer> m_LocalShadowBuffer;
    std::unique_ptr<VulkanSceneRenderTargets> m_SceneRenderTargets;
    std::unique_ptr<VulkanSampler> m_SceneTargetSampler;
    std::unique_ptr<VulkanGBufferRenderPass> m_GBufferRenderPass;
    std::unique_ptr<VulkanGBufferFramebuffer> m_GBufferFramebuffer;
    std::unique_ptr<VulkanHdrRenderPass> m_HdrRenderPass;
    std::unique_ptr<VulkanHdrFramebuffer> m_HdrFramebuffer;
    std::unique_ptr<VulkanWeightedTranslucencyRenderPass> m_WeightedTranslucencyRenderPass;
    std::unique_ptr<VulkanWeightedTranslucencyFramebuffer> m_WeightedTranslucencyFramebuffer;
    std::unique_ptr<VulkanDepthBuffer> m_DepthBuffer;
    std::unique_ptr<VulkanShadowMap> m_ShadowMap;
    std::unique_ptr<VulkanDirectionalShadowCascadeAtlas> m_DirectionalShadowCascadeAtlas;
    std::unique_ptr<VulkanLocalShadowAtlas> m_LocalShadowAtlas;
    std::unique_ptr<VulkanShadowRenderPass> m_ShadowRenderPass;
    std::unique_ptr<VulkanShadowFramebuffer> m_ShadowFramebuffer;
    std::unique_ptr<VulkanShadowFramebuffer> m_DirectionalShadowCascadeFramebuffer;
    std::unique_ptr<VulkanShadowFramebuffer> m_LocalShadowFramebuffer;
    std::unique_ptr<VulkanRenderPass> m_RenderPass;
    std::unique_ptr<VulkanRenderPass> m_DepthLoadRenderPass;
    std::unique_ptr<VulkanImGuiLayer> m_ImGuiLayer;
    std::unique_ptr<VulkanGraphicsPipeline> m_GraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_InstancedGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedInstancedGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_GBufferGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedGBufferGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DeferredLightingPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_HdrCompositePipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_GBufferDebugPipeline;
    std::unique_ptr<VulkanComputePipeline> m_LightTileCullComputePipeline;
    std::unique_ptr<VulkanComputePipeline> m_LightClusterCullComputePipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DepthPrefillGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedDepthPrefillGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_WeightedTranslucencyGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedWeightedTranslucencyGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_WeightedTranslucencyResolvePipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_ForwardResidualGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedForwardResidualGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_ShadowGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedShadowGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_OverlayGraphicsPipeline;
    std::unique_ptr<VulkanFramebuffer> m_Framebuffer;
    std::unique_ptr<VulkanFramebuffer> m_DepthLoadFramebuffer;
    std::unique_ptr<VulkanCommandBuffer> m_CommandBuffer;
    std::unique_ptr<VulkanInstanceBuffer> m_InstanceBuffer;
    std::unique_ptr<VulkanGpuTimer> m_GpuTimer;
    std::unique_ptr<VulkanSyncObjects> m_SyncObjects;
    std::vector<Instance3D> m_MainInstances;
    std::vector<RenderInstanceBatch> m_MainInstanceBatches;
    std::vector<u64> m_MainInstanceUploadSignatures;
    std::vector<bool> m_LightTileGpuReadbackReady;
    u64 m_MainInstanceSignature = 0;
    bool m_MainInstanceBatchesCacheValid = false;
};

}
