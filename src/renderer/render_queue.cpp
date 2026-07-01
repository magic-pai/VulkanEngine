#include "renderer/render_queue.h"

#include "renderer/vulkan/material.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/mesh_lod.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "scene/renderable_2d.h"
#include "scene/renderable_3d.h"
#include "scene/transform.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace se {

namespace {

FrustumPlane NormalizedPlane(glm::vec4 plane) {
    const f32 normalLength = glm::length(glm::vec3(plane));
    if (normalLength <= 0.000001f) {
        return FrustumPlane{};
    }

    return FrustumPlane{
        glm::vec3(plane) / normalLength,
        plane.w / normalLength
    };
}

void IncludePoint(glm::vec3 point, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    boundsMin = glm::min(boundsMin, point);
    boundsMax = glm::max(boundsMax, point);
}

u32 FloatBits(f32 value) {
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

u64 HashCombine(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

u64 FrustumSignature(const Frustum* frustum) {
    if (frustum == nullptr) {
        return 0x6eed0e9da4d94a4full;
    }

    u64 signature = 0xcbf29ce484222325ull;
    for (const FrustumPlane& plane : frustum->planes) {
        signature = HashCombine(signature, FloatBits(plane.normal.x));
        signature = HashCombine(signature, FloatBits(plane.normal.y));
        signature = HashCombine(signature, FloatBits(plane.normal.z));
        signature = HashCombine(signature, FloatBits(plane.distance));
    }

    return signature;
}

RenderBounds TransformAabb(
    const VulkanMesh& mesh,
    const glm::mat4& model
) {
    RenderBounds bounds{};
    bounds.min = glm::vec3(std::numeric_limits<f32>::max());
    bounds.max = glm::vec3(std::numeric_limits<f32>::lowest());

    const glm::vec3 localMin = mesh.BoundsMin();
    const glm::vec3 localMax = mesh.BoundsMax();
    u32 cornerIndex = 0;
    for (u32 x = 0; x < 2; ++x) {
        for (u32 y = 0; y < 2; ++y) {
            for (u32 z = 0; z < 2; ++z) {
                const glm::vec3 localPoint{
                    x == 0 ? localMin.x : localMax.x,
                    y == 0 ? localMin.y : localMax.y,
                    z == 0 ? localMin.z : localMax.z
                };
                const glm::vec3 worldPoint = glm::vec3(model * glm::vec4(localPoint, 1.0f));
                bounds.corners[cornerIndex] = worldPoint;
                ++cornerIndex;
                IncludePoint(worldPoint, bounds.min, bounds.max);
            }
        }
    }

    bounds.valid = true;
    return bounds;
}

RenderMaterialPushConstants MaterialPushConstantsFor(const VulkanMaterial& material) {
    const MaterialProperties& properties = material.Properties();

    RenderMaterialPushConstants constants{};
    constants.materialBaseColorFactor = glm::vec4(
        properties.baseColorFactor[0],
        properties.baseColorFactor[1],
        properties.baseColorFactor[2],
        properties.baseColorFactor[3]
    );
    constants.materialControls = glm::vec4(
        properties.textureMix,
        properties.viewControls[0],
        properties.viewControls[1],
        properties.viewControls[2]
    );
    constants.materialCustom = glm::vec4(
        properties.custom[0],
        properties.custom[1],
        properties.custom[2],
        properties.custom[3]
    );
    constants.cameraControls = glm::vec4(
        properties.cameraControls[0],
        properties.cameraControls[1],
        properties.cameraControls[2],
        properties.cameraControls[3]
    );
    constants.cameraPosition = glm::vec4(
        properties.cameraPosition[0],
        properties.cameraPosition[1],
        properties.cameraPosition[2],
        properties.cameraPosition[3]
    );
    constants.cameraDirection = glm::vec4(
        properties.cameraDirection[0],
        properties.cameraDirection[1],
        properties.cameraDirection[2],
        properties.cameraDirection[3]
    );

    return constants;
}

glm::mat4 HighlightModelMatrix(const Transform2D& transform) {
    constexpr f32 kOutlineScale = 1.04f;

    Transform3D outlineTransform = transform.AsTransform3D();
    glm::vec3 outlineScale = outlineTransform.Scale();
    outlineScale.x *= kOutlineScale;
    outlineScale.y *= kOutlineScale;
    outlineTransform.SetScale(outlineScale);

    return outlineTransform.Matrix();
}

RenderCommand CommandForRenderable(
    const VulkanRenderResources2D& renderResources,
    const Renderable2D& renderable,
    const glm::mat4& model,
    const glm::vec4& tint
) {
    RenderCommand command{};
    command.mesh = &renderResources.Mesh(renderable.MeshId());
    command.material = &renderResources.Material(renderable.MaterialId());
    command.model = model;
    command.tint = tint;
    command.drawOrder = renderable.DrawOrder();
    command.materialPushConstants = MaterialPushConstantsFor(*command.material);
    command.castShadow = false;

    return command;
}

RenderCommand CommandForRenderable(
    const VulkanMesh& mesh,
    const VulkanMaterial& material,
    const Renderable3D& renderable,
    const glm::mat4& model,
    const RenderBounds& worldBounds
) {
    RenderCommand command{};
    command.mesh = &mesh;
    command.material = &material;
    command.model = model;
    command.tint = glm::vec4(0.0f);
    command.drawOrder = renderable.DrawOrder();
    command.worldBounds = worldBounds;
    command.castShadow = renderable.CastShadow();

    return command;
}

}

Frustum Frustum::FromViewProjection(const glm::mat4& viewProjection) {
    Frustum frustum{};
    const glm::mat4 m = glm::transpose(viewProjection);
    frustum.planes[0] = NormalizedPlane(m[3] + m[0]);
    frustum.planes[1] = NormalizedPlane(m[3] - m[0]);
    frustum.planes[2] = NormalizedPlane(m[3] + m[1]);
    frustum.planes[3] = NormalizedPlane(m[3] - m[1]);
    frustum.planes[4] = NormalizedPlane(m[3] + m[2]);
    frustum.planes[5] = NormalizedPlane(m[3] - m[2]);

    return frustum;
}

bool Frustum::IntersectsAabb(
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax
) const {
    for (const FrustumPlane& plane : planes) {
        const glm::vec3 positiveVertex{
            plane.normal.x >= 0.0f ? boundsMax.x : boundsMin.x,
            plane.normal.y >= 0.0f ? boundsMax.y : boundsMin.y,
            plane.normal.z >= 0.0f ? boundsMax.z : boundsMin.z
        };

        if (glm::dot(plane.normal, positiveVertex) + plane.distance < 0.0f) {
            return false;
        }
    }

    return true;
}

void RenderQueue::Clear() {
    m_Commands.clear();
    m_NextSubmissionIndex = 0;
}

void RenderQueue::Submit(RenderCommand command) {
    SE_ASSERT(command.mesh != nullptr, "RenderCommand must reference a mesh");
    SE_ASSERT(command.material != nullptr, "RenderCommand must reference a material");

    command.materialSortKey = reinterpret_cast<std::uintptr_t>(command.material);
    command.meshSortKey = reinterpret_cast<std::uintptr_t>(command.mesh);
    command.submissionIndex = m_NextSubmissionIndex;
    ++m_NextSubmissionIndex;
    m_Commands.push_back(command);
}

void RenderQueue::BuildFromScene2D(
    const VulkanRenderResources2D& renderResources,
    std::span<Renderable2D* const> renderables,
    const Renderable2D* selectedRenderable
) {
    Clear();
    m_Commands.reserve(renderables.size() + 1);

    for (const Renderable2D* renderable : renderables) {
        SE_ASSERT(renderable != nullptr, "Scene contains a null renderable");

        if (renderable == selectedRenderable && renderable->HighlightEnabled()) {
            Submit(CommandForRenderable(
                renderResources,
                *renderable,
                HighlightModelMatrix(renderable->Transform()),
                glm::vec4(1.0f, 0.82f, 0.18f, 1.0f)
            ));
        }

        Submit(CommandForRenderable(
            renderResources,
            *renderable,
            renderable->Transform().Matrix(),
            glm::vec4(0.0f)
        ));
    }

    SortForDraw(false);
}

void RenderQueue::BuildFromScene3D(
    const VulkanRenderResources2D& renderResources,
    std::span<Renderable3D* const> renderables,
    const Renderable3D* selectedRenderable,
    RenderQueueBuildOptions options
) {
    (void)selectedRenderable;

    if (renderables.empty()) {
        Clear();
        m_3DCommandCache.clear();
        m_3DSceneCache.valid = false;
        if (options.cullingStats != nullptr) {
            *options.cullingStats = RenderQueueCullingStats{};
        }
        if (options.cacheStats != nullptr) {
            *options.cacheStats = RenderQueueCacheStats{};
        }
        return;
    } else if (m_3DCommandCache.size() > renderables.size() * 4) {
        m_3DCommandCache.clear();
        m_3DSceneCache.valid = false;
    }
    if (options.cullingStats != nullptr) {
        *options.cullingStats = RenderQueueCullingStats{};
    }
    if (options.cacheStats != nullptr) {
        *options.cacheStats = RenderQueueCacheStats{};
    }

    RenderQueueCullingStats cullingStats{};
    const u64 frustumSignature = FrustumSignature(options.frustum);
    const u64 sceneQueueSignature = Scene3DQueueSignature(
        renderables,
        options
    );
    if (TryReuseScene3DQueue(sceneQueueSignature, options)) {
        return;
    }
    if (options.cacheStats != nullptr) {
        ++options.cacheStats->queueCacheMisses;
    }

    Clear();
    m_Commands.reserve(renderables.size());

    u32 visibilityCandidates = 0;
    for (const Renderable3D* renderable : renderables) {
        SE_ASSERT(renderable != nullptr, "Scene3D contains a null renderable");

        if (options.shadowCastersOnly && !renderable->CastShadow()) {
            ++cullingStats.culled;
            continue;
        }

        RenderCommand command = CommandForRenderable3D(
            renderResources,
            *renderable,
            options.cacheStats
        );
        bool visible = true;
        if (options.frustum != nullptr && command.worldBounds.valid) {
            ++visibilityCandidates;
            if (TryCachedVisibility(*renderable, frustumSignature, visible)) {
                if (options.cacheStats != nullptr) {
                    ++options.cacheStats->visibilityCacheHits;
                }
            } else {
                visible = options.frustum->IntersectsAabb(
                    command.worldBounds.min,
                    command.worldBounds.max
                );
                StoreCachedVisibility(*renderable, frustumSignature, visible);
                if (options.cacheStats != nullptr) {
                    ++options.cacheStats->visibilityCacheMisses;
                }
            }
        }

        if (visible) {
            command.materialPushConstants = MaterialPushConstantsFor(*command.material);
            // LOD selection based on screen-space size
            if (options.lodOptions.enabled && command.worldBounds.valid) {
                glm::vec3 extent = command.worldBounds.max - command.worldBounds.min;
                f32 radius = glm::length(extent) * 0.5f;
                glm::vec3 center = (command.worldBounds.min + command.worldBounds.max) * 0.5f;
                f32 dist = glm::distance(center, options.lodOptions.cameraPosition);
                command.lodScreenFraction = MeshLodGenerator::ComputeScreenFraction(
                    radius, dist, options.lodOptions.screenHeight, options.lodOptions.fovYRadians);
                command.lodLevel = MeshLodGenerator::SelectLod(command.lodScreenFraction,
                    MeshLodChain{}, 0);
            }
            Submit(command);
            ++cullingStats.visible;
        } else {
            ++cullingStats.culled;
        }
    }

    SortForDraw(true);
    if (options.cullingStats != nullptr) {
        *options.cullingStats = cullingStats;
    }
    StoreScene3DQueue(
        sceneQueueSignature,
        cullingStats,
        static_cast<u32>(renderables.size()),
        visibilityCandidates
    );
}

void RenderQueue::BuildShadowCastersFrom(
    const RenderQueue& sourceQueue,
    RenderQueueCullingStats* cullingStats
) {
    Clear();
    m_Commands.reserve(sourceQueue.Count());
    if (cullingStats != nullptr) {
        *cullingStats = RenderQueueCullingStats{};
    }

    for (const RenderCommand& command : sourceQueue.Commands()) {
        if (!command.castShadow) {
            if (cullingStats != nullptr) {
                ++cullingStats->culled;
            }
            continue;
        }

        Submit(command);
        if (cullingStats != nullptr) {
            ++cullingStats->visible;
        }
    }

    SortForDraw(true);
}

void RenderQueue::SortForDraw(bool optimizeStateChanges) {
    if (!optimizeStateChanges) {
        std::sort(
            m_Commands.begin(),
            m_Commands.end(),
            [](const RenderCommand& lhs, const RenderCommand& rhs) {
                if (lhs.drawOrder != rhs.drawOrder) {
                    return lhs.drawOrder < rhs.drawOrder;
                }

                return lhs.submissionIndex < rhs.submissionIndex;
            }
        );
        return;
    }

    std::sort(
        m_Commands.begin(),
        m_Commands.end(),
        [](const RenderCommand& lhs, const RenderCommand& rhs) {
            if (lhs.drawOrder != rhs.drawOrder) {
                return lhs.drawOrder < rhs.drawOrder;
            }

            if (lhs.materialSortKey != rhs.materialSortKey) {
                return lhs.materialSortKey < rhs.materialSortKey;
            }

            if (lhs.meshSortKey != rhs.meshSortKey) {
                return lhs.meshSortKey < rhs.meshSortKey;
            }

            return lhs.submissionIndex < rhs.submissionIndex;
        }
    );
}

std::span<const RenderCommand> RenderQueue::Commands() const {
    return std::span<const RenderCommand>(m_Commands.data(), m_Commands.size());
}

bool RenderQueue::Empty() const {
    return m_Commands.empty();
}

RenderCommand RenderQueue::CommandForRenderable3D(
    const VulkanRenderResources2D& renderResources,
    const Renderable3D& renderable,
    RenderQueueCacheStats* cacheStats
) {
    const Transform3D& transform = renderable.Transform();
    const u64 transformVersion = transform.MatrixVersion();
    const u64 renderStateVersion = renderable.RenderStateVersion();
    const u64 renderableIdentity = renderable.RenderIdentity();

    const auto found = m_3DCommandCache.find(&renderable);
    if (found != m_3DCommandCache.end()) {
        const CachedRenderable3DCommand& cached = found->second;
        if (cached.renderableIdentity == renderableIdentity &&
            cached.renderStateVersion == renderStateVersion &&
            cached.transformVersion == transformVersion &&
            cached.command.mesh != nullptr &&
            cached.command.material != nullptr &&
            cached.command.worldBounds.valid) {
            RenderCommand command = cached.command;
            if (cacheStats != nullptr) {
                ++cacheStats->commandCacheHits;
                ++cacheStats->boundsCacheHits;
            }

            return command;
        }
    }

    const VulkanMesh& mesh = renderResources.Mesh(renderable.MeshId());
    const VulkanMaterial& material = renderResources.Material(renderable.MaterialId());
    const glm::mat4 model = transform.Matrix();
    RenderCommand command = CommandForRenderable(
        mesh,
        material,
        renderable,
        model,
        WorldBoundsFor(mesh, model)
    );

    CachedRenderable3DCommand cached{};
    cached.renderableIdentity = renderableIdentity;
    cached.renderStateVersion = renderStateVersion;
    cached.transformVersion = transformVersion;
    cached.command = command;
    if (found == m_3DCommandCache.end()) {
        m_3DCommandCache.emplace(&renderable, cached);
    } else {
        found->second = cached;
    }

    if (cacheStats != nullptr) {
        ++cacheStats->commandCacheMisses;
        ++cacheStats->boundsCacheMisses;
    }

    return command;
}

RenderBounds RenderQueue::WorldBoundsFor(
    const VulkanMesh& mesh,
    const glm::mat4& model
) const {
    return TransformAabb(mesh, model);
}

bool RenderQueue::TryCachedVisibility(
    const Renderable3D& renderable,
    u64 frustumSignature,
    bool& visible
) const {
    const auto found = m_3DCommandCache.find(&renderable);
    if (found == m_3DCommandCache.end()) {
        return false;
    }

    const CachedRenderable3DCommand& cached = found->second;
    if (!cached.visibilityValid ||
        cached.visibilityFrustumSignature != frustumSignature) {
        return false;
    }

    visible = cached.visible;
    return true;
}

void RenderQueue::StoreCachedVisibility(
    const Renderable3D& renderable,
    u64 frustumSignature,
    bool visible
) {
    const auto found = m_3DCommandCache.find(&renderable);
    if (found == m_3DCommandCache.end()) {
        return;
    }

    CachedRenderable3DCommand& cached = found->second;
    cached.visibilityFrustumSignature = frustumSignature;
    cached.visibilityValid = true;
    cached.visible = visible;
}

u64 RenderQueue::Scene3DQueueSignature(
    std::span<Renderable3D* const> renderables,
    RenderQueueBuildOptions options
) const {
    u64 signature = 0x97e4935cc6f0b3d1ull;
    signature = HashCombine(signature, FrustumSignature(options.frustum));
    signature = HashCombine(signature, options.shadowCastersOnly ? 1ull : 0ull);

    if (options.useSceneRevisions && options.sceneIdentity != nullptr) {
        signature = HashCombine(
            signature,
            static_cast<u64>(reinterpret_cast<std::uintptr_t>(options.sceneIdentity))
        );
        signature = HashCombine(signature, options.sceneMembershipRevision);
        signature = HashCombine(signature, options.sceneRenderRevision);
        return signature;
    }

    signature = HashCombine(signature, static_cast<u64>(renderables.size()));

    for (const Renderable3D* renderable : renderables) {
        signature = HashCombine(
            signature,
            static_cast<u64>(reinterpret_cast<std::uintptr_t>(renderable))
        );
        if (renderable == nullptr) {
            continue;
        }

        signature = HashCombine(signature, renderable->RenderIdentity());
        signature = HashCombine(signature, renderable->RenderStateVersion());
        signature = HashCombine(signature, renderable->Transform().MatrixVersion());
    }

    return signature;
}

bool RenderQueue::TryReuseScene3DQueue(
    u64 signature,
    RenderQueueBuildOptions options
) {
    if (!m_3DSceneCache.valid || m_3DSceneCache.signature != signature) {
        return false;
    }

    if (m_Commands.size() != m_3DSceneCache.commands.size()) {
        m_Commands = m_3DSceneCache.commands;
    }
    m_NextSubmissionIndex = m_Commands.size();
    Refresh3DMaterialPushConstants();

    if (options.cullingStats != nullptr) {
        *options.cullingStats = m_3DSceneCache.cullingStats;
    }
    if (options.cacheStats != nullptr) {
        ++options.cacheStats->queueCacheHits;
        options.cacheStats->commandCacheHits += m_3DSceneCache.scannedRenderables;
        options.cacheStats->boundsCacheHits += m_3DSceneCache.scannedRenderables;
        options.cacheStats->visibilityCacheHits += m_3DSceneCache.visibilityCandidates;
    }

    return true;
}

void RenderQueue::StoreScene3DQueue(
    u64 signature,
    const RenderQueueCullingStats& cullingStats,
    u32 scannedRenderables,
    u32 visibilityCandidates
) {
    m_3DSceneCache.signature = signature;
    m_3DSceneCache.commands = m_Commands;
    m_3DSceneCache.cullingStats = cullingStats;
    m_3DSceneCache.scannedRenderables = scannedRenderables;
    m_3DSceneCache.visibilityCandidates = visibilityCandidates;
    m_3DSceneCache.valid = true;
}

void RenderQueue::Refresh3DMaterialPushConstants() {
    for (RenderCommand& command : m_Commands) {
        SE_ASSERT(command.material != nullptr, "Cached 3D render command lost its material");
        command.materialPushConstants = MaterialPushConstantsFor(*command.material);
    }
}

std::size_t RenderQueue::Count() const {
    return m_Commands.size();
}

}
