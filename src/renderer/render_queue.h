#pragma once

#include "core.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <array>
#include <string>
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
    glm::vec3 cameraPosition{0.0f}; f32 screenHeight=1080.0f; f32 fovYRadians=1.0472f;
};

struct RenderQueueBuildOptions {
    const Frustum* frustum = nullptr;
    RenderQueueCullingStats* cullingStats = nullptr;
    RenderQueueCacheStats* cacheStats = nullptr;
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
    u32 lodLevel = 0;
    f32 lodScreenFraction = 1.0f;
    i32 drawOrder = 0;
    std::uintptr_t materialSortKey = 0;
    std::uintptr_t meshSortKey = 0;
    std::size_t submissionIndex = 0;
    std::string bonePaletteResourceId;
    u32 bonePaletteCurrentEntryCount = 0;
    u32 bonePalettePreviousEntryCount = 0;
    u32 bonePaletteChangedEntryCount = 0;
    u32 bonePaletteReady = 0;
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
        RenderQueueBuildOptions options
    );
    void StoreScene3DQueue(
        u64 signature,
        const RenderQueueCullingStats& cullingStats,
        u32 scannedRenderables,
        u32 visibilityCandidates
    );
    void Refresh3DMaterialPushConstants();

    std::vector<RenderCommand> m_Commands;
    std::unordered_map<const Renderable3D*, CachedRenderable3DCommand> m_3DCommandCache;
    CachedScene3DQueue m_3DSceneCache;
    std::size_t m_NextSubmissionIndex = 0;
};

}
