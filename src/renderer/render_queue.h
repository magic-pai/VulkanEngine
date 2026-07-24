#pragma once

#include "core.h"
#include "renderer/vulkan/vulkan_common.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace se {

class Renderable2D;
class Renderable3D;
class VulkanMaterial;
class VulkanMesh;
class VulkanRenderResources2D;

struct FrustumPlane {
    glm::vec3 normal{ 0.0f };
    f32 distance = 0.0f;
};

struct Frustum {
    std::array<FrustumPlane, 6> planes{};

    static Frustum FromViewProjection(const glm::mat4& viewProjection);
    bool IntersectsAabb(const glm::vec3& boundsMin, const glm::vec3& boundsMax) const;
};

struct RenderQueueCullingStats {
    u32 visible = 0;
    u32 culled = 0;
};

struct RenderQueueCacheStats {
    u32 boundsCacheHits = 0;
    u32 boundsCacheMisses = 0;
    u32 commandCacheHits = 0;
    u32 commandCacheMisses = 0;
    u32 visibilityCacheHits = 0;
    u32 visibilityCacheMisses = 0;
    u32 queueCacheHits = 0;
    u32 queueCacheMisses = 0;
};

struct RenderQueueLodOptions {
    bool enabled = false;
    glm::vec3 cameraPosition{ 0.0f };
    f32 screenHeight = 1080.0f;
    f32 fovYRadians = 1.0472f;
    f32 targetPixelError = 1.0f;
    f32 hysteresisRatio = 0.15f;
    f32 lod0ScreenFraction = 0.65f;
};

struct RenderQueueLodStats {
    u32 enabled = 0;
    u32 eligibleCommands = 0;
    u32 selectedCommands = 0;
    u32 reducedCommands = 0;
    u32 transitionCount = 0;
    u32 skinnedExcludedCommands = 0;
    std::array<u32, 4> levelCounts{};
    u64 sourceTriangles = 0;
    u64 renderedTriangles = 0;
    u64 savedTriangles = 0;
    u32 residentChainCount = 0;
    u32 residentLevelCount = 0;
    u64 sourceVertexBytes = 0;
    u64 sourceIndexBytes = 0;
    u64 residentVertexBytes = 0;
    u64 residentIndexBytes = 0;
    u64 extraVertexBytes = 0;
    u64 extraIndexBytes = 0;
    f32 minScreenFraction = 0.0f;
    f32 maxScreenFraction = 0.0f;
    f32 maxSelectedErrorPixels = 0.0f;
    f32 targetPixelError = 1.0f;
};

struct RenderQueueBuildOptions {
    const Frustum* frustum = nullptr;
    RenderQueueCullingStats* cullingStats = nullptr;
    RenderQueueCacheStats* cacheStats = nullptr;
    RenderQueueLodStats* lodStats = nullptr;
    RenderQueueLodOptions lodOptions{};
    bool shadowCastersOnly = false;
    const void* sceneIdentity = nullptr;
    u64 sceneMembershipRevision = 0;
    u64 sceneRenderRevision = 0;
    bool useSceneRevisions = false;
};

struct RenderBounds {
    glm::vec3 min{ 0.0f };
    glm::vec3 max{ 0.0f };
    std::array<glm::vec3, 8> corners{};
    bool valid = false;
};

struct RenderMaterialPushConstants {
    alignas(16) glm::vec4 materialBaseColorFactor{ 1.0f };
    alignas(16) glm::vec4 materialControls{ 1.0f, 0.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 materialCustom{ 0.0f };
    alignas(16) glm::vec4 viewport{ 1.0f, 1.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 cameraControls{ 15.0f, 1.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 cameraPosition{ 0.0f, 0.0f, 15.0f, 0.0f };
    alignas(16) glm::vec4 cameraDirection{ 0.0f, 0.0f, -1.0f, 0.0f };
};

struct RenderCommand {
    const VulkanMesh* mesh = nullptr;
    const VulkanMaterial* material = nullptr;
    glm::mat4 model{ 1.0f };
    glm::mat4 previousModel{ 1.0f };
    glm::vec4 tint{ 0.0f };
    RenderBounds worldBounds{};
    RenderMaterialPushConstants materialPushConstants{};
    bool castShadow = false;
    bool reflectionCaptureVisible = true;
    u32 lodLevel = 0;
    f32 lodScreenFraction = 1.0f;
    i32 drawOrder = 0;
    std::uintptr_t materialSortKey = 0;
    std::uintptr_t meshSortKey = 0;
    std::size_t submissionIndex = 0;
    u64 renderableIdentity = 0u;
    u32 gpuOcclusionCandidateIndex = std::numeric_limits<u32>::max();
    u32 reflectionAuditObjectId = 0u;
    // 0 selects global IBL; 1..4 select a stable frame-local reflection probe.
    u32 reflectionProbeAssignmentCode = 0u;
    i32 reflectionProbeSceneIndex = -1;
#if !defined(NDEBUG)
    glm::vec3 reflectionProbeAnchor{ 0.0f };
    f32 reflectionProbeAssignmentWeight = 0.0f;
#endif
    std::string bonePaletteResourceId;
    u32 bonePaletteCurrentEntryCount = 0;
    u32 bonePalettePreviousEntryCount = 0;
    u32 bonePaletteChangedEntryCount = 0;
    u32 bonePaletteReady = 0;
    u64 bonePaletteRevision = 0u;
    const glm::mat4* bonePalettePreviousData = nullptr;
    const glm::mat4* bonePaletteCurrentData = nullptr;
    VkDescriptorSet bonePaletteDescriptorSet = VK_NULL_HANDLE;
    u32 bonePaletteDescriptorSetReady = 0;
    u32 bonePaletteDescriptorSetIndex = 0;
    u32 bonePaletteDescriptorBinding = 0;
    u32 bonePaletteDescriptorRangeBytes = 0;
    u32 skinnedWorldBoundsConservative = 0;
#if !defined(NDEBUG)
    std::string_view debugRenderableName;
#endif
};

struct RenderInstanceBatch {
    std::size_t firstCommandIndex = 0;
    u32 commandCount = 0;
    u32 firstInstance = 0;
};

class RenderQueue {
public:
    void Clear();
    void Submit(RenderCommand command);
    void BuildFromScene2D(
        const VulkanRenderResources2D& renderResources,
        std::span<Renderable2D* const> renderables,
        const Renderable2D* selectedRenderable
    );
    void BuildFromScene3D(
        const VulkanRenderResources2D& renderResources,
        std::span<Renderable3D* const> renderables,
        const Renderable3D* selectedRenderable = nullptr,
        RenderQueueBuildOptions options = {}
    );
    void BuildShadowCastersFrom(
        const RenderQueue& sourceQueue,
        RenderQueueCullingStats* cullingStats = nullptr
    );
    void SortForDraw(bool optimizeStateChanges = false);

    std::span<const RenderCommand> Commands() const;
    std::span<RenderCommand> MutableCommands();
    bool Empty() const;
    std::size_t Count() const;

private:
    struct CachedRenderable3DCommand {
        u64 renderableIdentity = 0;
        u64 renderStateVersion = 0;
        u64 transformVersion = 0;
        u64 visibilityFrustumSignature = 0;
        RenderCommand command{};
        bool visibilityValid = false;
        bool visible = true;
    };

    struct CachedScene3DQueue {
        u64 signature = 0;
        std::vector<RenderCommand> commands;
        RenderQueueCullingStats cullingStats{};
        RenderQueueLodStats lodStats{};
        u32 scannedRenderables = 0;
        u32 visibilityCandidates = 0;
        bool valid = false;
    };

    RenderCommand CommandForRenderable3D(
        const VulkanRenderResources2D& renderResources,
        const Renderable3D& renderable,
        RenderQueueCacheStats* cacheStats
    );

    RenderBounds WorldBoundsFor(
        const VulkanMesh& mesh,
        const glm::mat4& model
    ) const;
    bool TryCachedVisibility(
        const Renderable3D& renderable,
        u64 frustumSignature,
        bool& visible
    ) const;
    void StoreCachedVisibility(
        const Renderable3D& renderable,
        u64 frustumSignature,
        bool visible
    );
    u64 Scene3DQueueSignature(
        std::span<Renderable3D* const> renderables,
        RenderQueueBuildOptions options
    ) const;
    bool TryReuseScene3DQueue(
        u64 signature,
        const VulkanRenderResources2D& renderResources,
        RenderQueueBuildOptions options
    );
    void StoreScene3DQueue(
        u64 signature,
        const RenderQueueCullingStats& cullingStats,
        const RenderQueueLodStats& lodStats,
        u32 scannedRenderables,
        u32 visibilityCandidates
    );
    void Refresh3DDynamicCommandState(const VulkanRenderResources2D& renderResources);

    std::vector<RenderCommand> m_Commands;
    std::unordered_map<const Renderable3D*, CachedRenderable3DCommand> m_3DCommandCache;
    std::unordered_map<u64, u32> m_PreviousLodByRenderable;
    CachedScene3DQueue m_3DSceneCache;
    std::size_t m_NextSubmissionIndex = 0;
};

}
