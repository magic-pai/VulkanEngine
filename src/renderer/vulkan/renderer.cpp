#include "renderer/vulkan/renderer.h"

#include "renderer/vulkan/ibl_generator.h"
#include "renderer/vulkan/command_buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/compute_pipeline.h"
#include "renderer/vulkan/depth_buffer.h"
#include "renderer/vulkan/descriptor_set_layout.h"
#include "renderer/vulkan/descriptor_sets.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/features/height_fog_feature.h"
#include "renderer/vulkan/features/post_process_feature.h"
#include "renderer/vulkan/features/reflection_probe_fallback_feature.h"
#include "renderer/vulkan/features/ssao_feature.h"
#include "renderer/vulkan/features/ssr_feature.h"
#include "renderer/vulkan/framebuffer.h"
#include "renderer/vulkan/frame_materials.h"
#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/gpu_timer.h"
#include "renderer/vulkan/graphics_pipeline.h"
#include "renderer/vulkan/imgui_layer.h"
#include "renderer/vulkan/instance_buffer.h"
#include "renderer/vulkan/local_shadow_atlas.h"
#include "renderer/vulkan/material.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/reflection_probe_resources.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "renderer/vulkan/render_targets.h"
#include "renderer/vulkan/render_pass.h"
#include "renderer/vulkan/shadow_cascade_atlas.h"
#include "renderer/vulkan/shadow_framebuffer.h"
#include "renderer/vulkan/shadow_map.h"
#include "renderer/vulkan/shadow_render_pass.h"
#include "renderer/vulkan/shadow_settings.h"
#include "renderer/vulkan/surface.h"
#include "renderer/vulkan/swapchain.h"
#include "renderer/vulkan/sync_objects.h"
#include "renderer/vulkan/uniform_buffer.h"
#include "scene/camera_2d.h"
#include "scene/camera_3d.h"
#include "scene/renderable_2d.h"
#include "scene/scene_2d.h"
#include "scene/scene_3d.h"
#include "scene/transform.h"
#include "platform/window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <limits>
#include <optional>
#include <memory>
#include <string>
#include <utility>

#include <imgui.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

namespace se {

static_assert(
    kRendererMaxFrameLocalLights == kMaxFrameLocalLights,
    "Renderer and GPU light-buffer local-light capacities must match"
);

FrameLightConstants FrameLightSet::Constants() const {
    glm::vec3 direction = primaryDirectional.direction;
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = glm::vec3(-0.45f, -0.82f, -0.35f);
    }
    direction = glm::normalize(direction);

    FrameLightConstants constants{};
    constants.directionalLight = glm::vec4(
        direction,
        std::max(primaryDirectional.intensity, 0.0f)
    );
    constants.ambientLight = glm::vec4(
        std::max(primaryDirectional.ambient, 0.0f),
        std::max(primaryDirectional.specular, 0.0f),
        0.0f,
        0.0f
    );
    return constants;
}

namespace {

constexpr f32 kShadowMinHalfExtent = 2.5f;
constexpr f32 kShadowPaddingRatio = 0.18f;
constexpr f32 kShadowDepthPadding = 4.0f;
constexpr f32 kLocalShadowNearPlane = 0.05f;

using FrameClock = std::chrono::steady_clock;

f32 ElapsedMilliseconds(FrameClock::time_point start, FrameClock::time_point end) {
    return std::chrono::duration<f32, std::milli>(end - start).count();
}

RendererDrawStats DrawStatsForQueues(
    std::span<const RenderCommand> mainCommands,
    std::span<const RenderCommand> overlayCommands,
    std::span<const RenderCommand> shadowCommands
) {
    auto triangleCount = [](std::span<const RenderCommand> commands) {
        u64 count = 0;
        for (const RenderCommand& command : commands) {
            if (command.mesh != nullptr) {
                count += static_cast<u64>(command.mesh->IndexCount() / 3);
            }
        }

        return count;
    };

    RendererDrawStats stats{};
    stats.mainDraws = static_cast<u32>(mainCommands.size());
    stats.overlayDraws = static_cast<u32>(overlayCommands.size());
    stats.shadowDraws = static_cast<u32>(shadowCommands.size());
    stats.mainTriangles = triangleCount(mainCommands);
    stats.overlayTriangles = triangleCount(overlayCommands);
    stats.shadowTriangles = triangleCount(shadowCommands);

    return stats;
}

u64 TriangleCountForCommand(const RenderCommand& command) {
    if (command.mesh == nullptr) {
        return 0;
    }

    return static_cast<u64>(command.mesh->IndexCount() / 3);
}

glm::vec3 CommandBoundsCenter(const RenderCommand& command) {
    if (command.worldBounds.valid) {
        return (command.worldBounds.min + command.worldBounds.max) * 0.5f;
    }

    return glm::vec3(command.model[3]);
}

glm::vec3 CameraPositionFromMatrices(const FrameMatrices* matrices) {
    if (matrices == nullptr) {
        return glm::vec3(0.0f);
    }

    const glm::mat4 invView = glm::inverse(matrices->view);
    return glm::vec3(invView[3]);
}

f32 DistanceSquaredToCamera(
    const RenderCommand& command,
    const glm::vec3& cameraPosition
) {
    const glm::vec3 delta = CommandBoundsCenter(command) - cameraPosition;
    return glm::dot(delta, delta);
}

MaterialRenderClass RenderClassForCommand(const RenderCommand& command) {
    const f32 alpha = std::clamp(
        command.materialPushConstants.materialBaseColorFactor.a,
        0.0f,
        1.0f
    );

    if (command.material == nullptr) {
        return alpha < 0.999f
            ? MaterialRenderClass::Transparent
            : MaterialRenderClass::DeferredOpaque;
    }

    const MaterialProperties& properties = command.material->Properties();
    if (properties.alphaMode == MaterialAlphaMode::Blend) {
        return MaterialRenderClass::Transparent;
    }
    if (properties.alphaMode != MaterialAlphaMode::Mask && alpha < 0.999f) {
        return MaterialRenderClass::Transparent;
    }

    return properties.renderClass;
}

f32 RenderClassValue(MaterialRenderClass renderClass) {
    return static_cast<f32>(static_cast<u32>(renderClass));
}

f32 AlphaModeValue(MaterialAlphaMode alphaMode) {
    return static_cast<f32>(static_cast<u32>(alphaMode));
}

void IncludePoint(glm::vec3 point, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    boundsMin.x = std::min(boundsMin.x, point.x);
    boundsMin.y = std::min(boundsMin.y, point.y);
    boundsMin.z = std::min(boundsMin.z, point.z);

    boundsMax.x = std::max(boundsMax.x, point.x);
    boundsMax.y = std::max(boundsMax.y, point.y);
    boundsMax.z = std::max(boundsMax.z, point.z);
}

u32 FloatBits(f32 value) {
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

u32 CeilDivU32(u32 value, u32 divisor) {
    if (divisor == 0) {
        return 0;
    }

    return (value + divisor - 1) / divisor;
}

u64 HashCombine(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

u64 HashMatrix(u64 seed, const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            seed = HashCombine(seed, FloatBits(matrix[column][row]));
        }
    }

    return seed;
}

void IncludeCommandBounds(
    const RenderCommand& command,
    glm::vec3& boundsMin,
    glm::vec3& boundsMax,
    bool& hasBounds
) {
    if (!command.worldBounds.valid) {
        return;
    }

    IncludePoint(command.worldBounds.min, boundsMin, boundsMax);
    IncludePoint(command.worldBounds.max, boundsMin, boundsMax);
    hasBounds = true;
}

void ExpandRangeAroundCenter(f32& minValue, f32& maxValue, f32 minHalfExtent) {
    const f32 center = (minValue + maxValue) * 0.5f;
    const f32 halfExtent = std::max((maxValue - minValue) * 0.5f, minHalfExtent);
    minValue = center - halfExtent;
    maxValue = center + halfExtent;
}

glm::vec3 NormalizedDirectionalLightDirection(const FrameLightSet& lights) {
    glm::vec3 lightDirection = lights.primaryDirectional.direction;
    if (glm::dot(lightDirection, lightDirection) <= 0.0001f) {
        lightDirection = glm::vec3(-0.45f, -0.82f, -0.35f);
    }

    return glm::normalize(lightDirection);
}

bool CameraDepthRangeFromMatrices(
    const FrameMatrices* matrices,
    f32& nearDepth,
    f32& farDepth
) {
    if (matrices == nullptr) {
        return false;
    }

    const glm::mat4 inverseView = glm::inverse(matrices->view);
    const glm::mat4 inverseViewProjection =
        glm::inverse(matrices->proj * matrices->view);
    const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);

    auto unproject = [&](f32 x, f32 y, f32 z) {
        const glm::vec4 worldH = inverseViewProjection * glm::vec4(x, y, z, 1.0f);
        if (std::abs(worldH.w) <= 0.000001f) {
            return cameraPosition;
        }

        return glm::vec3(worldH) / worldH.w;
    };

    glm::vec3 nearCenter{ 0.0f };
    glm::vec3 farCenter{ 0.0f };
    for (const f32 x : { -1.0f, 1.0f }) {
        for (const f32 y : { -1.0f, 1.0f }) {
            nearCenter += unproject(x, y, 0.0f);
            farCenter += unproject(x, y, 1.0f);
        }
    }
    nearCenter *= 0.25f;
    farCenter *= 0.25f;

    glm::vec3 forward = farCenter - nearCenter;
    if (glm::dot(forward, forward) <= 0.000001f) {
        forward = -glm::vec3(inverseView[2]);
    }
    if (glm::dot(forward, forward) <= 0.000001f) {
        return false;
    }
    forward = glm::normalize(forward);

    nearDepth = glm::dot(nearCenter - cameraPosition, forward);
    farDepth = glm::dot(farCenter - cameraPosition, forward);
    if (!std::isfinite(nearDepth) || !std::isfinite(farDepth)) {
        return false;
    }
    if (nearDepth <= 0.0f || farDepth <= nearDepth + 0.001f) {
        return false;
    }

    return true;
}

bool CameraSliceCornersFromMatrices(
    const FrameMatrices& matrices,
    f32 nearDepth,
    f32 farDepth,
    std::array<glm::vec3, 8>& corners
) {
    const glm::mat4 inverseView = glm::inverse(matrices.view);
    const glm::mat4 inverseViewProjection =
        glm::inverse(matrices.proj * matrices.view);
    const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);

    auto unproject = [&](f32 x, f32 y, f32 z) {
        const glm::vec4 worldH = inverseViewProjection * glm::vec4(x, y, z, 1.0f);
        if (std::abs(worldH.w) <= 0.000001f) {
            return cameraPosition;
        }

        return glm::vec3(worldH) / worldH.w;
    };

    glm::vec3 farCenter{ 0.0f };
    std::array<glm::vec3, 4> farPlaneCorners{};
    u32 cornerIndex = 0;
    for (const f32 x : { -1.0f, 1.0f }) {
        for (const f32 y : { -1.0f, 1.0f }) {
            farPlaneCorners[cornerIndex] = unproject(x, y, 1.0f);
            farCenter += farPlaneCorners[cornerIndex];
            ++cornerIndex;
        }
    }
    farCenter *= 0.25f;

    glm::vec3 forward = farCenter - cameraPosition;
    if (glm::dot(forward, forward) <= 0.000001f) {
        forward = -glm::vec3(inverseView[2]);
    }
    if (glm::dot(forward, forward) <= 0.000001f) {
        return false;
    }
    forward = glm::normalize(forward);

    for (u32 index = 0; index < 4; ++index) {
        const glm::vec3 ray = farPlaneCorners[index] - cameraPosition;
        const f32 forwardDistance = glm::dot(ray, forward);
        if (std::abs(forwardDistance) <= 0.000001f) {
            return false;
        }

        corners[index] = cameraPosition + ray * (nearDepth / forwardDistance);
        corners[index + 4] = cameraPosition + ray * (farDepth / forwardDistance);
    }

    return true;
}

glm::mat4 LightViewProjectionForCascade(
    std::span<const RenderCommand> renderCommands,
    const FrameLightSet& lights,
    const std::array<glm::vec3, 8>& frustumCorners,
    bool stableSnappingEnabled,
    u32 mapSize,
    f32* texelWorldSize
) {
    const glm::vec3 lightDirection = NormalizedDirectionalLightDirection(lights);
    glm::vec3 cascadeBoundsMin{ std::numeric_limits<f32>::max() };
    glm::vec3 cascadeBoundsMax{ std::numeric_limits<f32>::lowest() };
    for (const glm::vec3& worldPoint : frustumCorners) {
        IncludePoint(worldPoint, cascadeBoundsMin, cascadeBoundsMax);
    }

    const glm::vec3 center = (cascadeBoundsMin + cascadeBoundsMax) * 0.5f;
    const glm::vec3 cascadeExtent = cascadeBoundsMax - cascadeBoundsMin;
    const f32 cascadeRadius = std::max(
        glm::length(cascadeExtent) * 0.5f,
        kShadowMinHalfExtent
    );

    const glm::vec3 lightForward = glm::normalize(lightDirection);
    const glm::vec3 eye = center - lightForward * (cascadeRadius + kShadowDepthPadding);
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };
    if (std::abs(glm::dot(lightForward, up)) > 0.95f) {
        up = { 0.0f, 0.0f, 1.0f };
    }

    const glm::mat4 view = glm::lookAt(eye, center, up);
    glm::vec3 lightBoundsMin{ std::numeric_limits<f32>::max() };
    glm::vec3 lightBoundsMax{ std::numeric_limits<f32>::lowest() };
    for (const glm::vec3& worldPoint : frustumCorners) {
        IncludePoint(
            glm::vec3(view * glm::vec4(worldPoint, 1.0f)),
            lightBoundsMin,
            lightBoundsMax
        );
    }

    for (const RenderCommand& command : renderCommands) {
        if (!command.worldBounds.valid) {
            continue;
        }

        for (const glm::vec3& worldPoint : command.worldBounds.corners) {
            const glm::vec3 lightPoint =
                glm::vec3(view * glm::vec4(worldPoint, 1.0f));
            lightBoundsMin.z = std::min(lightBoundsMin.z, lightPoint.z);
            lightBoundsMax.z = std::max(lightBoundsMax.z, lightPoint.z);
        }
    }

    const f32 xPadding = std::max(
        (lightBoundsMax.x - lightBoundsMin.x) * kShadowPaddingRatio,
        0.35f
    );
    const f32 yPadding = std::max(
        (lightBoundsMax.y - lightBoundsMin.y) * kShadowPaddingRatio,
        0.35f
    );
    lightBoundsMin.x -= xPadding;
    lightBoundsMax.x += xPadding;
    lightBoundsMin.y -= yPadding;
    lightBoundsMax.y += yPadding;
    lightBoundsMin.z -= kShadowDepthPadding;
    lightBoundsMax.z += kShadowDepthPadding;

    ExpandRangeAroundCenter(lightBoundsMin.x, lightBoundsMax.x, kShadowMinHalfExtent);
    ExpandRangeAroundCenter(lightBoundsMin.y, lightBoundsMax.y, kShadowMinHalfExtent);
    if (stableSnappingEnabled) {
        const f32 halfExtent = std::max(
            (lightBoundsMax.x - lightBoundsMin.x) * 0.5f,
            (lightBoundsMax.y - lightBoundsMin.y) * 0.5f
        );
        const f32 centerX = (lightBoundsMin.x + lightBoundsMax.x) * 0.5f;
        const f32 centerY = (lightBoundsMin.y + lightBoundsMax.y) * 0.5f;
        lightBoundsMin.x = centerX - halfExtent;
        lightBoundsMax.x = centerX + halfExtent;
        lightBoundsMin.y = centerY - halfExtent;
        lightBoundsMax.y = centerY + halfExtent;
    }

    f32 texelSize = 0.0f;
    if (mapSize > 0) {
        texelSize = (lightBoundsMax.x - lightBoundsMin.x) /
            static_cast<f32>(mapSize);
    }
    if (stableSnappingEnabled && texelSize > 0.0f) {
        const f32 width = lightBoundsMax.x - lightBoundsMin.x;
        const f32 height = lightBoundsMax.y - lightBoundsMin.y;
        lightBoundsMin.x = std::floor(lightBoundsMin.x / texelSize) * texelSize;
        lightBoundsMin.y = std::floor(lightBoundsMin.y / texelSize) * texelSize;
        lightBoundsMax.x = lightBoundsMin.x + width;
        lightBoundsMax.y = lightBoundsMin.y + height;
    }
    if (texelWorldSize != nullptr) {
        *texelWorldSize = texelSize;
    }

    const f32 nearPlane = std::max(0.01f, -lightBoundsMax.z);
    const f32 farPlane = std::max(nearPlane + 0.1f, -lightBoundsMin.z);
    glm::mat4 projection = glm::ortho(
        lightBoundsMin.x,
        lightBoundsMax.x,
        lightBoundsMin.y,
        lightBoundsMax.y,
        nearPlane,
        farPlane
    );
    projection[1][1] *= -1.0f;
    return projection * view;
}

RendererShadowCascadeStats ShadowCascadeStatsFor(
    const DirectionalShadowCascadeSet& cascades
) {
    RendererShadowCascadeStats stats{};
    stats.configuredCount = cascades.configuredCount;
    stats.activeCount = cascades.activeCount;
    stats.stableSnappingEnabled = cascades.stableSnappingEnabled ? 1u : 0u;
    stats.splitLambda = cascades.splitLambda;
    stats.maxDistance = cascades.maxDistance;
    stats.nearDepth = cascades.nearDepth;
    stats.farDepth = cascades.farDepth;
    for (u32 index = 0; index < std::min<u32>(cascades.activeCount, kMaxDirectionalShadowCascades); ++index) {
        stats.splitDepths[index] = cascades.cascades[index].splitDepth;
        stats.texelWorldSizes[index] = cascades.cascades[index].texelWorldSize;
    }

    return stats;
}

struct LocalShadowTileBudget {
    u32 shadowableLightCount = 0;
    u32 pointLightCount = 0;
    u32 spotLightCount = 0;
    u32 pointFaceTiles = 0;
    u32 spotTiles = 0;
    u32 requestedTiles = 0;
};

u32 LocalShadowAtlasTileSizeFor(const VulkanShadowSettings& settings) {
    if (settings.quality == VulkanShadowQuality::Low) {
        return 512u;
    }
    if (settings.quality == VulkanShadowQuality::Off) {
        return 512u;
    }

    return std::max<u32>(std::min<u32>(settings.mapSize / 2u, 1024u), 512u);
}

u32 LocalShadowAtlasTileCapacityFor(const VulkanShadowSettings& settings) {
    switch (settings.quality) {
    case VulkanShadowQuality::Off:
        return 8u;
    case VulkanShadowQuality::Low:
        return 12u;
    case VulkanShadowQuality::Medium:
        return 24u;
    case VulkanShadowQuality::High:
    case VulkanShadowQuality::Ultra:
        return 32u;
    }

    return 24u;
}

LocalShadowTileBudget LocalShadowTileBudgetFor(const FrameLightSet& lights) {
    LocalShadowTileBudget budget{};
    const u32 localCount = std::min<u32>(
        lights.localCount,
        static_cast<u32>(lights.localLights.size())
    );

    for (u32 index = 0; index < localCount; ++index) {
        const RendererLocalLight& light = lights.localLights[index];
        if (light.kind == RendererLightKind::Point) {
            ++budget.shadowableLightCount;
            ++budget.pointLightCount;
            budget.pointFaceTiles += 6u;
            budget.requestedTiles += 6u;
            continue;
        }

        if (light.kind == RendererLightKind::Spot) {
            ++budget.shadowableLightCount;
            ++budget.spotLightCount;
            ++budget.spotTiles;
            ++budget.requestedTiles;
        }
    }

    return budget;
}

glm::mat4 PerspectiveProjection(f32 verticalFovRadians, f32 aspectRatio, f32 nearPlane, f32 farPlane) {
    glm::mat4 projection = glm::perspective(
        verticalFovRadians,
        aspectRatio,
        nearPlane,
        std::max(farPlane, nearPlane + 0.1f)
    );
    projection[1][1] *= -1.0f;
    return projection;
}

glm::mat4 LocalShadowViewProjection(
    glm::vec3 position,
    glm::vec3 direction,
    glm::vec3 up,
    f32 verticalFovRadians,
    f32 farPlane
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, -1.0f, 0.0f };
    }
    direction = glm::normalize(direction);
    if (glm::dot(up, up) <= 0.0001f ||
        std::abs(glm::dot(glm::normalize(up), direction)) > 0.98f) {
        up = std::abs(direction.y) > 0.95f
            ? glm::vec3{ 0.0f, 0.0f, 1.0f }
            : glm::vec3{ 0.0f, 1.0f, 0.0f };
    }
    up = glm::normalize(up);

    return PerspectiveProjection(
        verticalFovRadians,
        1.0f,
        kLocalShadowNearPlane,
        farPlane
    ) * glm::lookAt(position, position + direction, up);
}

void AddLocalShadowTile(
    LocalShadowTileSet& tileSet,
    const glm::mat4& viewProjection,
    u32 localLightIndex,
    u32 faceIndex,
    RendererLightKind lightKind,
    u64 cacheKey,
    bool cacheReusable
) {
    ++tileSet.requestedCount;
    if (tileSet.assignedCount >= tileSet.tileCapacity ||
        tileSet.assignedCount >= tileSet.tiles.size()) {
        ++tileSet.droppedCount;
        return;
    }

    LocalShadowTile& tile = tileSet.tiles[tileSet.assignedCount];
    tile.viewProjection = viewProjection;
    tile.tileIndex = tileSet.assignedCount;
    tile.localLightIndex = localLightIndex;
    tile.faceIndex = faceIndex;
    tile.lightKind = static_cast<u32>(lightKind);
    tile.cacheKey = cacheKey;
    tile.cacheReusable = cacheReusable;
    tileSet.cacheKeys[tileSet.assignedCount] = cacheKey;
    if (cacheReusable) {
        ++tileSet.cacheEligibleTiles;
        ++tileSet.cacheHitTiles;
    } else {
        ++tileSet.cacheMissTiles;
    }
    ++tileSet.assignedCount;
}

u64 LocalShadowTileCacheKey(
    const RendererLocalLight& light,
    u32 localLightIndex,
    u32 faceIndex,
    u64 casterSignature
) {
    u64 hash = 0x6f4d1f5bb9e6d9c5ull;
    hash = HashCombine(hash, static_cast<u64>(localLightIndex));
    hash = HashCombine(hash, static_cast<u64>(light.kind));
    hash = HashCombine(hash, static_cast<u64>(faceIndex));
    hash = HashCombine(hash, FloatBits(light.position.x));
    hash = HashCombine(hash, FloatBits(light.position.y));
    hash = HashCombine(hash, FloatBits(light.position.z));
    hash = HashCombine(hash, FloatBits(light.radius));
    hash = HashCombine(hash, FloatBits(light.direction.x));
    hash = HashCombine(hash, FloatBits(light.direction.y));
    hash = HashCombine(hash, FloatBits(light.direction.z));
    hash = HashCombine(hash, FloatBits(light.innerConeCos));
    hash = HashCombine(hash, FloatBits(light.outerConeCos));
    hash = HashCombine(hash, casterSignature);
    return hash;
}

bool LocalShadowTileCacheReusable(
    const std::array<u64, kMaxLocalShadowTiles>& previousKeys,
    u32 previousKeyCount,
    u64 cacheKey,
    u32 tileIndex
) {
    if (tileIndex >= previousKeyCount || tileIndex >= previousKeys.size()) {
        return false;
    }

    return previousKeys[tileIndex] == cacheKey;
}

bool SphereIntersectsAabb(
    glm::vec3 center,
    f32 radius,
    const RenderBounds& bounds
) {
    if (!bounds.valid) {
        return true;
    }

    const glm::vec3 closest = glm::clamp(center, bounds.min, bounds.max);
    const glm::vec3 delta = closest - center;
    return glm::dot(delta, delta) <= radius * radius;
}

glm::vec3 BoundsCenter(const RenderBounds& bounds) {
    return (bounds.min + bounds.max) * 0.5f;
}

f32 BoundsRadius(const RenderBounds& bounds) {
    if (!bounds.valid) {
        return std::numeric_limits<f32>::max();
    }

    return glm::length(bounds.max - bounds.min) * 0.5f;
}

bool PointLightFaceMaySeeBounds(
    const RendererLocalLight& light,
    glm::vec3 faceDirection,
    const RenderBounds& bounds
) {
    if (!bounds.valid) {
        return true;
    }
    if (!SphereIntersectsAabb(light.position, std::max(light.radius, kLocalShadowNearPlane), bounds)) {
        return false;
    }

    const glm::vec3 toBounds = BoundsCenter(bounds) - light.position;
    return glm::dot(toBounds, faceDirection) + BoundsRadius(bounds) >= 0.0f;
}

u64 HashShadowCommand(u64 signature, const RenderCommand& command) {
    signature = HashCombine(
        signature,
        static_cast<u64>(reinterpret_cast<std::uintptr_t>(command.mesh))
    );
    signature = HashCombine(
        signature,
        static_cast<u64>(reinterpret_cast<std::uintptr_t>(command.material))
    );
    signature = HashCombine(signature, static_cast<u64>(command.meshSortKey));
    signature = HashCombine(signature, static_cast<u64>(command.materialSortKey));
    signature = HashCombine(signature, static_cast<u64>(command.drawOrder));
    signature = HashCombine(signature, command.castShadow ? 1ull : 0ull);
    signature = HashMatrix(signature, command.model);
    if (command.worldBounds.valid) {
        signature = HashCombine(signature, 1ull);
        signature = HashCombine(signature, FloatBits(command.worldBounds.min.x));
        signature = HashCombine(signature, FloatBits(command.worldBounds.min.y));
        signature = HashCombine(signature, FloatBits(command.worldBounds.min.z));
        signature = HashCombine(signature, FloatBits(command.worldBounds.max.x));
        signature = HashCombine(signature, FloatBits(command.worldBounds.max.y));
        signature = HashCombine(signature, FloatBits(command.worldBounds.max.z));
    } else {
        signature = HashCombine(signature, 0ull);
    }

    return signature;
}

u64 LocalShadowCasterSignature(
    std::span<const RenderCommand> shadowCommands,
    const RendererLocalLight& light,
    const glm::vec3* pointFaceDirection = nullptr
) {
    u64 signature = 0x35f0d5a8936a1c21ull;
    u32 relevantCount = 0;
    const f32 influenceRadius = std::max(light.radius, kLocalShadowNearPlane);
    for (const RenderCommand& command : shadowCommands) {
        if (!command.castShadow) {
            continue;
        }
        const bool relevant = pointFaceDirection != nullptr
            ? PointLightFaceMaySeeBounds(light, *pointFaceDirection, command.worldBounds)
            : SphereIntersectsAabb(light.position, influenceRadius, command.worldBounds);
        if (!relevant) {
            continue;
        }

        signature = HashShadowCommand(signature, command);
        ++relevantCount;
    }

    return HashCombine(signature, static_cast<u64>(relevantCount));
}

std::string ReadEnvironmentString(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
        return {};
    }

    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

bool GpuTimestampsEnabledFromEnvironment() {
    const std::string value = ReadEnvironmentString("SE_ENABLE_GPU_TIMESTAMPS");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

struct RenderFeatureFrameGraphAppendData {
    const VulkanRenderFeatureRegistry* registry = nullptr;
    const VulkanRenderFeatureContext* renderer = nullptr;
    const RendererStats* stats = nullptr;
};

void AppendRenderFeaturesToCurrentFrameGraph(
    RenderFrameGraphPlan& plan,
    RenderFramePassKind stage,
    const void* userData
) {
    const auto* data =
        static_cast<const RenderFeatureFrameGraphAppendData*>(userData);
    if (data == nullptr ||
        data->registry == nullptr ||
        data->renderer == nullptr ||
        data->stats == nullptr) {
        return;
    }

    data->registry->AppendFrameGraph(
        VulkanRenderFeatureFrameGraphContext{
            plan,
            *data->renderer,
            *data->stats,
            stage == RenderFramePassKind::PostProcess
                ? VulkanRenderFeatureFrameGraphStage::PostProcess
                : VulkanRenderFeatureFrameGraphStage::Lighting
        }
    );
}

bool EnvironmentFlagEnabled(const char* name) {
    const std::string value = ReadEnvironmentString(name);
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

std::optional<bool> EnvironmentFlagOverride(const char* name) {
    const std::string value = ReadEnvironmentString(name);
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES") {
        return true;
    }
    if (value == "0" ||
        value == "false" ||
        value == "FALSE" ||
        value == "off" ||
        value == "OFF" ||
        value == "no" ||
        value == "NO") {
        return false;
    }

    return std::nullopt;
}

std::optional<RendererReflectionProbeCaptureSource>
ReflectionProbeCaptureSourceOverrideFromEnvironment() {
    const std::string value =
        ReadEnvironmentString("SE_REFLECTION_PROBE_CAPTURE_SOURCE");
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "none" || value == "None" || value == "NONE" ||
        value == "off" || value == "OFF" || value == "0") {
        return RendererReflectionProbeCaptureSource::None;
    }
    if (value == "builtin" || value == "built_in" ||
        value == "procedural" || value == "BuiltInProcedural" ||
        value == "1") {
        return RendererReflectionProbeCaptureSource::BuiltInProcedural;
    }
    if (value == "authored" || value == "authored_cubemap" ||
        value == "AuthoredCubemap" || value == "2") {
        return RendererReflectionProbeCaptureSource::AuthoredCubemap;
    }
    if (value == "captured" || value == "captured_scene" ||
        value == "CapturedScene" || value == "3") {
        return RendererReflectionProbeCaptureSource::CapturedScene;
    }

    return std::nullopt;
}

std::optional<f32> EnvironmentFloatOverride(const char* name) {
    const std::string value = ReadEnvironmentString(name);
    if (value.empty()) {
        return std::nullopt;
    }

    char* end = nullptr;
    const f32 parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str()) {
        return std::nullopt;
    }

    return parsed;
}

bool WeightedTranslucencyAlphaReferenceEnabled() {
    return EnvironmentFlagEnabled("SE_WBOIT_REFERENCE_ALPHA") ||
        EnvironmentFlagEnabled("SE_WEIGHTED_TRANSLUCENCY_ALPHA_REFERENCE");
}

RendererLocalLight PointLocalLight(
    glm::vec3 position,
    f32 radius,
    glm::vec3 color,
    f32 intensity
) {
    RendererLocalLight light{};
    light.kind = RendererLightKind::Point;
    light.position = position;
    light.radius = radius;
    light.color = color;
    light.intensity = intensity;
    return light;
}

RendererLocalLight SpotLocalLight(
    glm::vec3 position,
    glm::vec3 direction,
    f32 radius,
    glm::vec3 color,
    f32 intensity,
    f32 innerConeDegrees,
    f32 outerConeDegrees
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, -1.0f, 0.0f };
    } else {
        direction = glm::normalize(direction);
    }

    outerConeDegrees = std::clamp(outerConeDegrees, 0.1f, 89.0f);
    innerConeDegrees = std::clamp(innerConeDegrees, 0.05f, outerConeDegrees);

    RendererLocalLight light{};
    light.kind = RendererLightKind::Spot;
    light.position = position;
    light.radius = radius;
    light.color = color;
    light.intensity = intensity;
    light.direction = direction;
    light.innerConeCos = std::cos(glm::radians(innerConeDegrees));
    light.outerConeCos = std::cos(glm::radians(outerConeDegrees));
    return light;
}

RendererLocalLight RectLocalLight(
    glm::vec3 position,
    glm::vec3 direction,
    f32 width,
    f32 height,
    f32 radius,
    glm::vec3 color,
    f32 intensity
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, -1.0f, 0.0f };
    } else {
        direction = glm::normalize(direction);
    }

    RendererLocalLight light{};
    light.kind = RendererLightKind::Rect;
    light.position = position;
    light.radius = radius;
    light.color = color;
    light.intensity = intensity;
    light.direction = direction;
    light.width = std::max(width, 0.0f);
    light.height = std::max(height, 0.0f);
    return light;
}

RendererReflectionProbe ClampReflectionProbe(RendererReflectionProbe probe) {
    probe.radius = std::clamp(probe.radius, 0.01f, 256.0f);
    probe.boxExtents = glm::max(probe.boxExtents, glm::vec3(0.01f));
    probe.color = glm::max(probe.color, glm::vec3(0.0f));
    probe.intensity = std::clamp(probe.intensity, 0.0f, 4.0f);
    probe.blendStrength = std::clamp(probe.blendStrength, 0.0f, 1.0f);
    probe.falloff = std::clamp(probe.falloff, 0.25f, 8.0f);
    return probe;
}

bool ReflectionProbeBoxProjectionEnabled(const RendererReflectionProbe& probe) {
    return probe.sceneOwned &&
        probe.boxExtents.x > 0.01f &&
        probe.boxExtents.y > 0.01f &&
        probe.boxExtents.z > 0.01f;
}

f32 ReflectionProbeBoxWeight(
    const RendererReflectionProbe& probe,
    glm::vec3 position
) {
    if (!ReflectionProbeBoxProjectionEnabled(probe)) {
        return 1.0f;
    }

    const glm::vec3 extents = glm::max(probe.boxExtents, glm::vec3(0.01f));
    const glm::vec3 normalized =
        glm::abs(position - probe.center) / extents;
    const f32 maxAxis =
        std::max(normalized.x, std::max(normalized.y, normalized.z));
    if (maxAxis <= 1.0f) {
        return 1.0f;
    }

    return 1.0f / (1.0f + (maxAxis - 1.0f) * 4.0f);
}

f32 SmoothStep(f32 edge0, f32 edge1, f32 value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0f : 1.0f;
    }
    const f32 t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

f32 ReflectionProbeInfluenceWeight(
    const RendererReflectionProbe& probe,
    glm::vec3 position
) {
    if (!probe.enabled ||
        probe.radius <= 0.001f ||
        probe.intensity <= 0.0001f ||
        probe.blendStrength <= 0.0001f) {
        return 0.0f;
    }

    const f32 radius = std::max(probe.radius, 0.001f);
    const f32 normalizedDistance = glm::length(position - probe.center) / radius;
    const f32 falloff = std::clamp(probe.falloff, 0.25f, 8.0f);
    f32 influence =
        std::pow(std::clamp(1.0f - normalizedDistance, 0.0f, 1.0f), falloff);
    if (ReflectionProbeBoxProjectionEnabled(probe)) {
        const glm::vec3 extents = glm::max(probe.boxExtents, glm::vec3(0.001f));
        const glm::vec3 normalizedBox =
            glm::abs(position - probe.center) / extents;
        const f32 maxAxis =
            std::max(normalizedBox.x, std::max(normalizedBox.y, normalizedBox.z));
        influence *= 1.0f - SmoothStep(1.0f, 1.25f, maxAxis);
    }

    return influence * std::clamp(probe.blendStrength, 0.0f, 1.0f);
}

f32 ReflectionProbeSelectionScore(
    const RendererReflectionProbe& probe,
    glm::vec3 position
) {
    const f32 radius = std::max(probe.radius, 0.001f);
    const f32 distance = glm::length(position - probe.center);
    const f32 normalizedDistance = distance / radius;
    const f32 falloff = std::clamp(probe.falloff, 0.25f, 8.0f);
    const f32 sphereInfluence =
        std::pow(std::clamp(1.0f - normalizedDistance, 0.0f, 1.0f), falloff);
    const f32 proximity = 1.0f / (1.0f + normalizedDistance);
    const f32 boxWeight = ReflectionProbeBoxWeight(probe, position);
    return (sphereInfluence * 3.0f + proximity * 0.35f) *
        std::max(boxWeight, 0.05f) *
        std::max(probe.intensity, 0.0f) *
        std::max(probe.blendStrength, 0.0f);
}

RendererReflectionProbeCaptureSource RendererCaptureSource(
    ReflectionProbeCaptureSource source
) {
    switch (source) {
    case ReflectionProbeCaptureSource::None:
        return RendererReflectionProbeCaptureSource::None;
    case ReflectionProbeCaptureSource::BuiltInProcedural:
        return RendererReflectionProbeCaptureSource::BuiltInProcedural;
    case ReflectionProbeCaptureSource::AuthoredCubemap:
        return RendererReflectionProbeCaptureSource::AuthoredCubemap;
    case ReflectionProbeCaptureSource::CapturedScene:
        return RendererReflectionProbeCaptureSource::CapturedScene;
    }

    return RendererReflectionProbeCaptureSource::None;
}

RendererReflectionProbeCaptureFallbackReason CaptureFallbackReasonFor(
    RendererReflectionProbeCaptureSource source,
    bool resourceReady
) {
    if (resourceReady) {
        return RendererReflectionProbeCaptureFallbackReason::None;
    }

    switch (source) {
    case RendererReflectionProbeCaptureSource::None:
        return RendererReflectionProbeCaptureFallbackReason::SourceDisabled;
    case RendererReflectionProbeCaptureSource::BuiltInProcedural:
        return RendererReflectionProbeCaptureFallbackReason::BuiltInResourceUnavailable;
    case RendererReflectionProbeCaptureSource::AuthoredCubemap:
        return RendererReflectionProbeCaptureFallbackReason::AuthoredCubemapNotLoaded;
    case RendererReflectionProbeCaptureSource::CapturedScene:
        return RendererReflectionProbeCaptureFallbackReason::CapturedSceneNotImplemented;
    }

    return RendererReflectionProbeCaptureFallbackReason::SourceDisabled;
}

bool ReflectionProbeCaptureResourceReady(
    RendererReflectionProbeCaptureSource source,
    bool builtInCubemapReady
) {
    return source == RendererReflectionProbeCaptureSource::BuiltInProcedural &&
        builtInCubemapReady;
}

void ResetFrameReflectionProbeCaptureDiagnostics(FrameReflectionProbeSet& probes) {
    probes.selectedCaptureSlots.fill(-1);
    probes.selectedCaptureResourceReady.fill(false);
    probes.selectedCaptureDescriptorBound.fill(false);
    probes.selectedCaptureFallbackReasons.fill(
        RendererReflectionProbeCaptureFallbackReason::NoActiveSceneProbe
    );
}

void SetSelectedReflectionProbeCaptureDiagnostics(
    FrameReflectionProbeSet& probes,
    u32 selectedIndex,
    const RendererReflectionProbe& probe,
    bool cubemapSamplingEnabled,
    bool builtInCubemapReady,
    u32 descriptorSetsBound
) {
    if (selectedIndex >= probes.selectedCaptureSlots.size()) {
        return;
    }

    const bool resourceReady =
        ReflectionProbeCaptureResourceReady(probe.captureSource, builtInCubemapReady);
    const bool descriptorBound = resourceReady && descriptorSetsBound > 0u;
    const RendererReflectionProbeCaptureFallbackReason fallbackReason =
        cubemapSamplingEnabled
            ? CaptureFallbackReasonFor(probe.captureSource, resourceReady)
            : RendererReflectionProbeCaptureFallbackReason::CubemapSamplingDisabled;
    const u32 bit = 1u << selectedIndex;

    probes.selectedCaptureSlots[selectedIndex] = resourceReady ? 0 : -1;
    probes.selectedCaptureResourceReady[selectedIndex] = resourceReady;
    probes.selectedCaptureDescriptorBound[selectedIndex] = descriptorBound;
    probes.selectedCaptureFallbackReasons[selectedIndex] = fallbackReason;
    if (resourceReady) {
        if (probes.selectedCaptureSlotCount == 0u) {
            ++probes.selectedCaptureSlotCount;
        }
        ++probes.selectedCaptureResourceReadyCount;
        probes.selectedCaptureReadyMask |= bit;
    } else {
        ++probes.selectedCaptureFallbackCount;
        probes.selectedCaptureFallbackMask |= bit;
    }
    if (cubemapSamplingEnabled && descriptorBound) {
        ++probes.selectedCubemapSamplingCount;
        probes.selectedCubemapSamplingMask |= bit;
    }
}

RendererReflectionProbe SceneReflectionProbe(
    const ReflectionProbe3D& source,
    i32 sceneIndex
) {
    RendererReflectionProbe probe{};
    probe.center = source.center;
    probe.radius = source.radius;
    probe.boxExtents = source.boxExtents;
    probe.color = source.color;
    probe.intensity = source.intensity;
    probe.blendStrength = source.blendStrength;
    probe.falloff = source.falloff;
    probe.enabled = source.enabled;
    probe.sceneOwned = true;
    probe.sceneIndex = sceneIndex;
    probe.captureSource = RendererCaptureSource(source.captureSource);
    return ClampReflectionProbe(probe);
}

RendererReflectionProbe SettingsReflectionProbe(
    const VulkanShadowSettings& settings
) {
    RendererReflectionProbe probe{};
    probe.center = {
        settings.localReflectionProbeCenterX,
        settings.localReflectionProbeCenterY,
        settings.localReflectionProbeCenterZ
    };
    probe.radius = settings.localReflectionProbeRadius;
    probe.boxExtents = glm::vec3(settings.localReflectionProbeRadius);
    probe.color = {
        settings.localReflectionProbeColorR,
        settings.localReflectionProbeColorG,
        settings.localReflectionProbeColorB
    };
    probe.intensity = settings.localReflectionProbeIntensity;
    probe.blendStrength = settings.localReflectionProbeBlendStrength;
    probe.falloff = settings.localReflectionProbeFalloff;
    probe.enabled = settings.localReflectionProbeEnabled;
    probe.sceneOwned = false;
    probe.captureSource = RendererReflectionProbeCaptureSource::None;
    return ClampReflectionProbe(probe);
}

bool ReflectionProbeContributes(const RendererReflectionProbe& probe) {
    return probe.enabled &&
        probe.radius > 0.001f &&
        probe.intensity > 0.0001f &&
        probe.blendStrength > 0.0001f;
}

void AddDebugLocalLights(FrameLightSet& lights) {
    if (!EnvironmentFlagEnabled("SE_DEBUG_LOCAL_LIGHTS")) {
        return;
    }

    const std::array<RendererLocalLight, 3> debugLights{
        PointLocalLight(
            glm::vec3(-2.2f, 1.1f, -1.8f),
            4.8f,
            glm::vec3(1.0f, 0.38f, 0.22f),
            4.2f
        ),
        PointLocalLight(
            glm::vec3(1.8f, 1.0f, 0.9f),
            4.5f,
            glm::vec3(0.24f, 0.48f, 1.0f),
            3.8f
        ),
        PointLocalLight(
            glm::vec3(0.1f, 1.6f, 2.8f),
            5.2f,
            glm::vec3(0.36f, 1.0f, 0.44f),
            3.2f
        )
    };

    if (lights.localCount >= lights.localLights.size()) {
        return;
    }

    const u32 availableSlots =
        static_cast<u32>(lights.localLights.size() - lights.localCount);
    const u32 copyCount = std::min<u32>(
        availableSlots,
        static_cast<u32>(debugLights.size())
    );
    for (u32 index = 0; index < copyCount; ++index) {
        lights.localLights[lights.localCount + index] = debugLights[index];
    }
    lights.localCount += copyCount;
}

void WriteFrameReflectionProbeStats(
    const FrameReflectionProbeSet& frameProbes,
    RendererReflectionProbeStats& stats
) {
    const RendererReflectionProbe& localProbe = frameProbes.localProbe;
    stats.sceneProbeCount = frameProbes.sceneProbeCount;
    stats.activeProbeCount = frameProbes.activeLocalProbeCount;
    stats.sceneEligibleProbeCount = frameProbes.eligibleSceneProbeCount;
    stats.selectedProbeCount = frameProbes.selectedProbeCount;
    stats.blendedProbeCount = frameProbes.blendedProbeCount;
    stats.selectedCaptureSlotCount = frameProbes.selectedCaptureSlotCount;
    stats.selectedCaptureResourceReadyCount =
        frameProbes.selectedCaptureResourceReadyCount;
    stats.selectedCaptureFallbackCount =
        frameProbes.selectedCaptureFallbackCount;
    stats.selectedCubemapSamplingCount =
        frameProbes.selectedCubemapSamplingCount;
    stats.selectedCaptureReadyMask = frameProbes.selectedCaptureReadyMask;
    stats.selectedCaptureFallbackMask = frameProbes.selectedCaptureFallbackMask;
    stats.selectedCubemapSamplingMask = frameProbes.selectedCubemapSamplingMask;
    stats.droppedProbeCount = frameProbes.droppedSceneProbeCount;
    stats.selectedProbeIndex = frameProbes.selectedSceneProbeIndex;
    stats.selectedProbeIndices.fill(-1);
    stats.selectedCaptureSlots.fill(-1);
    stats.selectedCaptureSourceTypes.fill(0u);
    stats.selectedCaptureFallbackReasons.fill(
        static_cast<u32>(
            RendererReflectionProbeCaptureFallbackReason::NoActiveSceneProbe
        )
    );
    const u32 selectedProbeCount = std::min<u32>(
        frameProbes.selectedProbeCount,
        static_cast<u32>(stats.selectedProbeIndices.size())
    );
    for (u32 index = 0; index < selectedProbeCount; ++index) {
        stats.selectedProbeIndices[index] =
            frameProbes.selectedProbes[index].sceneIndex;
        stats.selectedCaptureSlots[index] =
            frameProbes.selectedCaptureSlots[index];
        stats.selectedCaptureSourceTypes[index] =
            static_cast<u32>(frameProbes.selectedProbes[index].captureSource);
        stats.selectedCaptureFallbackReasons[index] =
            static_cast<u32>(frameProbes.selectedCaptureFallbackReasons[index]);
    }
    stats.maxBlendWeight = frameProbes.maxBlendWeight;
    stats.totalBlendWeight = frameProbes.totalBlendWeight;
    stats.multiBlendEnabled = frameProbes.multiBlendEnabled ? 1u : 0u;
    stats.localEnabled =
        frameProbes.activeLocalProbeCount > 0 && ReflectionProbeContributes(localProbe)
            ? 1u
            : 0u;
    stats.localSceneOwned =
        stats.localEnabled > 0 && localProbe.sceneOwned ? 1u : 0u;
    stats.localRadius = localProbe.radius;
    stats.localBoxExtentX = localProbe.boxExtents.x;
    stats.localBoxExtentY = localProbe.boxExtents.y;
    stats.localBoxExtentZ = localProbe.boxExtents.z;
    stats.localIntensity = localProbe.intensity;
    stats.localBlendStrength = localProbe.blendStrength;
    stats.localFalloff = localProbe.falloff;
    stats.captureSourceType =
        static_cast<u32>(frameProbes.captureSource);
    stats.captureResourceReady =
        frameProbes.captureResourceReady ? 1u : 0u;
    stats.captureFallbackReason =
        static_cast<u32>(frameProbes.captureFallbackReason);
    stats.captureDescriptorBound =
        frameProbes.captureDescriptorBound ? 1u : 0u;
    stats.boxProjectionEnabled =
        frameProbes.boxProjectionEnabled ? 1u : 0u;
    stats.influenceMode = frameProbes.influenceMode;
    stats.parallaxCorrectionEnabled =
        frameProbes.parallaxCorrectionEnabled ? 1u : 0u;
}

void PopulateReflectionProbeUniforms(
    const FrameReflectionProbeSet& reflectionProbes,
    bool cubemapSamplingEnabled,
    UniformBufferObject& uniformData
) {
    const RendererReflectionProbe& localProbe = reflectionProbes.localProbe;
    const bool localReflectionProbeApplied =
        reflectionProbes.fallbackEnabled &&
        reflectionProbes.activeLocalProbeCount > 0 &&
        ReflectionProbeContributes(localProbe);
    const bool localReflectionProbeCubemapApplied =
        localReflectionProbeApplied &&
        cubemapSamplingEnabled &&
        reflectionProbes.captureResourceReady;
    uniformData.localReflectionProbePositionRadius = glm::vec4(
        localProbe.center,
        localProbe.radius
    );
    uniformData.localReflectionProbeControls = glm::vec4(
        localReflectionProbeApplied ? 1.0f : 0.0f,
        localProbe.intensity,
        localProbe.blendStrength,
        localProbe.falloff
    );
    uniformData.localReflectionProbeColor = glm::vec4(
        glm::clamp(localProbe.color, glm::vec3(0.0f), glm::vec3(4.0f)),
        localReflectionProbeCubemapApplied ? 1.0f : 0.0f
    );
    uniformData.localReflectionProbeBoxExtentsProjection = glm::vec4(
        glm::max(localProbe.boxExtents, glm::vec3(0.01f)),
        ReflectionProbeBoxProjectionEnabled(localProbe) ? 1.0f : 0.0f
    );

    const u32 selectedProbeCount = std::min<u32>(
        reflectionProbes.selectedProbeCount,
        static_cast<u32>(kMaxFrameReflectionProbes)
    );
    for (u32 index = 0; index < selectedProbeCount; ++index) {
        const RendererReflectionProbe& probe =
            reflectionProbes.selectedProbes[index];
        const bool probeApplied =
            reflectionProbes.fallbackEnabled && ReflectionProbeContributes(probe);
        const bool probeCubemapApplied =
            probeApplied &&
            cubemapSamplingEnabled &&
            reflectionProbes.selectedCaptureDescriptorBound[index];
        uniformData.reflectionProbePositionRadius[index] = glm::vec4(
            probe.center,
            probe.radius
        );
        uniformData.reflectionProbeControlsArray[index] = glm::vec4(
            probeApplied ? 1.0f : 0.0f,
            probe.intensity,
            probe.blendStrength,
            probe.falloff
        );
        uniformData.reflectionProbeColorArray[index] = glm::vec4(
            glm::clamp(probe.color, glm::vec3(0.0f), glm::vec3(4.0f)),
            probeCubemapApplied ? 1.0f : 0.0f
        );
        uniformData.reflectionProbeBoxExtentsProjectionArray[index] = glm::vec4(
            glm::max(probe.boxExtents, glm::vec3(0.01f)),
            ReflectionProbeBoxProjectionEnabled(probe) ? 1.0f : 0.0f
        );
    }
    uniformData.reflectionProbeBlendControls = glm::vec4(
        static_cast<f32>(selectedProbeCount),
        reflectionProbes.multiBlendEnabled ? 1.0f : 0.0f,
        reflectionProbes.maxBlendWeight,
        reflectionProbes.totalBlendWeight
    );
}

struct ScreenTileBounds {
    u32 minX = 0;
    u32 minY = 0;
    u32 maxX = 0;
    u32 maxY = 0;
    bool valid = false;
    bool conservative = false;
};

ScreenTileBounds FullScreenTileBounds(u32 tileCountX, u32 tileCountY) {
    ScreenTileBounds bounds{};
    if (tileCountX == 0 || tileCountY == 0) {
        return bounds;
    }

    bounds.maxX = tileCountX - 1;
    bounds.maxY = tileCountY - 1;
    bounds.valid = true;
    bounds.conservative = true;
    return bounds;
}

ScreenTileBounds ProjectLightSphereToTiles(
    const GpuLocalLightRecord& light,
    const FrameMatrices* matrices,
    const VkExtent2D& extent,
    u32 tileSize,
    u32 tileCountX,
    u32 tileCountY
) {
    if (matrices == nullptr || extent.width == 0 || extent.height == 0) {
        return FullScreenTileBounds(tileCountX, tileCountY);
    }

    const glm::vec3 center = glm::vec3(light.positionRadius);
    const f32 radius = std::max(light.positionRadius.w, 0.001f);
    const glm::mat4 viewProjection = matrices->proj * matrices->view;
    const std::array<glm::vec3, 6> offsets{
        glm::vec3(radius, 0.0f, 0.0f),
        glm::vec3(-radius, 0.0f, 0.0f),
        glm::vec3(0.0f, radius, 0.0f),
        glm::vec3(0.0f, -radius, 0.0f),
        glm::vec3(0.0f, 0.0f, radius),
        glm::vec3(0.0f, 0.0f, -radius)
    };

    glm::vec2 minPixel{ std::numeric_limits<f32>::max() };
    glm::vec2 maxPixel{ std::numeric_limits<f32>::lowest() };
    bool projectedAny = false;
    bool touchesClipBoundary = false;
    const auto includeProjectedPoint = [&](const glm::vec3& point) {
        const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
        if (std::abs(clip.w) <= 0.0001f) {
            touchesClipBoundary = true;
            return;
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (clip.w <= 0.0f || ndc.z < 0.0f || ndc.z > 1.0f) {
            touchesClipBoundary = true;
        }

        const f32 pixelX = (ndc.x * 0.5f + 0.5f) * static_cast<f32>(extent.width);
        const f32 pixelY = (ndc.y * 0.5f + 0.5f) * static_cast<f32>(extent.height);
        minPixel = glm::min(minPixel, glm::vec2(pixelX, pixelY));
        maxPixel = glm::max(maxPixel, glm::vec2(pixelX, pixelY));
        projectedAny = true;
    };

    includeProjectedPoint(center);
    for (const glm::vec3& offset : offsets) {
        includeProjectedPoint(center + offset);
    }

    if (!projectedAny || touchesClipBoundary) {
        return FullScreenTileBounds(tileCountX, tileCountY);
    }

    if (maxPixel.x < 0.0f || maxPixel.y < 0.0f ||
        minPixel.x > static_cast<f32>(extent.width) ||
        minPixel.y > static_cast<f32>(extent.height)) {
        return {};
    }

    if (minPixel.x > maxPixel.x || minPixel.y > maxPixel.y) {
        return {};
    }

    minPixel = glm::clamp(
        minPixel,
        glm::vec2(0.0f),
        glm::vec2(
            static_cast<f32>(extent.width),
            static_cast<f32>(extent.height)
        )
    );
    maxPixel = glm::clamp(
        maxPixel,
        glm::vec2(0.0f),
        glm::vec2(
            static_cast<f32>(extent.width),
            static_cast<f32>(extent.height)
        )
    );

    ScreenTileBounds bounds{};
    bounds.minX = std::min<u32>(
        tileCountX - 1,
        static_cast<u32>(std::floor(minPixel.x / static_cast<f32>(tileSize)))
    );
    bounds.minY = std::min<u32>(
        tileCountY - 1,
        static_cast<u32>(std::floor(minPixel.y / static_cast<f32>(tileSize)))
    );
    bounds.maxX = std::min<u32>(
        tileCountX - 1,
        static_cast<u32>(std::floor(std::max(maxPixel.x - 1.0f, 0.0f) /
            static_cast<f32>(tileSize)))
    );
    bounds.maxY = std::min<u32>(
        tileCountY - 1,
        static_cast<u32>(std::floor(std::max(maxPixel.y - 1.0f, 0.0f) /
            static_cast<f32>(tileSize)))
    );
    bounds.valid = bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY;
    return bounds;
}

FrameLightTileStats PopulateLightTileAssignments(
    LightBufferObject& lightData,
    std::size_t localCount,
    const VkExtent2D& extent,
    const FrameMatrices* matrices
) {
    const u32 tileSize = static_cast<u32>(kLightTileSize);
    const u32 tileCountX = CeilDivU32(extent.width, tileSize);
    const u32 tileCountY = CeilDivU32(extent.height, tileSize);
    const u64 requestedTileCount =
        static_cast<u64>(tileCountX) * static_cast<u64>(tileCountY);
    const bool canUseTileAssignments =
        requestedTileCount > 0 &&
        requestedTileCount <= kMaxFrameLightTiles &&
        localCount <= kMaxFrameLocalLights;

    FrameLightTileStats stats{};
    stats.tileSize = tileSize;
    stats.tileCountX = tileCountX;
    stats.tileCountY = tileCountY;
    stats.tileCount = requestedTileCount > std::numeric_limits<u32>::max()
        ? std::numeric_limits<u32>::max()
        : static_cast<u32>(requestedTileCount);
    stats.assignmentCapacity = stats.tileCount > std::numeric_limits<u32>::max() /
        static_cast<u32>(kMaxFrameLightsPerTile)
        ? std::numeric_limits<u32>::max()
        : stats.tileCount * static_cast<u32>(kMaxFrameLightsPerTile);
    stats.overflowCapacity = static_cast<u32>(kMaxFrameLightTileOverflowIndices);

    if (!canUseTileAssignments) {
        stats.fallbackCount = localCount > 0 ? 1 : 0;
        lightData.tileInfo = glm::vec4(
            static_cast<f32>(tileSize),
            0.0f,
            0.0f,
            static_cast<f32>(kMaxFrameLightsPerTile)
        );
        return stats;
    }

    lightData.lightCounts.w = 1.0f;
    lightData.tileInfo = glm::vec4(
        static_cast<f32>(tileSize),
        static_cast<f32>(tileCountX),
        static_cast<f32>(tileCountY),
        static_cast<f32>(kMaxFrameLightsPerTile)
    );

    const u32 localLightCount = std::min<u32>(
        static_cast<u32>(localCount),
        static_cast<u32>(kMaxFrameLocalLights)
    );
    const u32 groupsPerTile = static_cast<u32>(kLightIndexGroupsPerTile);
    std::vector<std::array<u32, kMaxFrameLightsPerTile>> tileAssignments(stats.tileCount);
    std::vector<std::vector<u32>> tileOverflowAssignments(stats.tileCount);
    std::vector<u32> tileAssignmentCounts(stats.tileCount, 0u);
    std::vector<u32> tileRawCandidateCounts(stats.tileCount, 0u);
    for (u32 localLightIndex = 0; localLightIndex < localLightCount; ++localLightIndex) {
        const ScreenTileBounds bounds = ProjectLightSphereToTiles(
            lightData.localLights[localLightIndex],
            matrices,
            extent,
            tileSize,
            tileCountX,
            tileCountY
        );
        if (!bounds.valid) {
            continue;
        }
        if (bounds.conservative) {
            ++stats.fallbackCount;
        }

        for (u32 tileY = bounds.minY; tileY <= bounds.maxY; ++tileY) {
            for (u32 tileX = bounds.minX; tileX <= bounds.maxX; ++tileX) {
                const u32 tileIndex = tileY * tileCountX + tileX;
                u32& assignmentCount = tileAssignmentCounts[tileIndex];
                ++tileRawCandidateCounts[tileIndex];
                if (assignmentCount >= kMaxFrameLightsPerTile) {
                    tileOverflowAssignments[tileIndex].push_back(localLightIndex);
                    continue;
                }

                tileAssignments[tileIndex][assignmentCount] = localLightIndex;
                ++assignmentCount;
                ++stats.assignments;
            }
        }
    }

    std::vector<u32> tileOverflowOffsets(stats.tileCount, 0u);
    std::vector<u32> tileOverflowCounts(stats.tileCount, 0u);
    u32 overflowCursor = 0;
    for (u32 tileIndex = 0; tileIndex < stats.tileCount; ++tileIndex) {
        const std::vector<u32>& overflowAssignments = tileOverflowAssignments[tileIndex];
        tileOverflowOffsets[tileIndex] = overflowCursor;
        const u32 remainingOverflowCapacity =
            overflowCursor >= kMaxFrameLightTileOverflowIndices
                ? 0u
                : static_cast<u32>(kMaxFrameLightTileOverflowIndices) - overflowCursor;
        const u32 overflowCount = std::min<u32>(
            static_cast<u32>(overflowAssignments.size()),
            remainingOverflowCapacity
        );
        tileOverflowCounts[tileIndex] = overflowCount;
        if (overflowCount > 0) {
            ++stats.overflowTileCount;
        }
        stats.overflowAssignments += overflowCount;
        const u32 droppedOverflowCount =
            static_cast<u32>(overflowAssignments.size()) - overflowCount;
        stats.overflowDropped += droppedOverflowCount;
        stats.fallbackCount += droppedOverflowCount;
        for (u32 overflowIndex = 0; overflowIndex < overflowCount; ++overflowIndex) {
            const u32 absoluteIndex = overflowCursor + overflowIndex;
            lightData.tileOverflowLightIndices[absoluteIndex] =
                overflowAssignments[overflowIndex];
        }
        overflowCursor += overflowCount;
    }

    for (u32 tileIndex = 0; tileIndex < stats.tileCount; ++tileIndex) {
        const u32 groupOffset = tileIndex * groupsPerTile;
        const u32 assignmentCount = tileAssignmentCounts[tileIndex];
        const u32 rawCandidateCount = tileRawCandidateCounts[tileIndex];
        const u32 overflowCount = tileOverflowCounts[tileIndex];
        const u32 droppedOverflowCount = rawCandidateCount -
            std::min(rawCandidateCount, assignmentCount + overflowCount);
        lightData.lightTiles[tileIndex].offsetCount =
            glm::uvec4(
                groupOffset,
                assignmentCount,
                rawCandidateCount,
                rawCandidateCount > kMaxFrameLightsPerTile ? 1u : 0u
            );
        lightData.lightTiles[tileIndex].overflowOffsetCount =
            glm::uvec4(
                tileOverflowOffsets[tileIndex],
                overflowCount,
                droppedOverflowCount,
                overflowCount > 0u ? 1u : 0u
            );

        for (u32 groupIndex = 0; groupIndex < groupsPerTile; ++groupIndex) {
            glm::uvec4 packedIndices{ 0u };
            for (u32 component = 0; component < 4; ++component) {
                const u32 assignmentIndex = groupIndex * 4u + component;
                if (assignmentIndex < assignmentCount) {
                    packedIndices[component] =
                        tileAssignments[tileIndex][assignmentIndex];
                }
            }
            lightData.tileLightIndexGroups[groupOffset + groupIndex] = packedIndices;
        }
    }

    return stats;
}

void AddScenePointLights(
    const Scene3D* scene,
    FrameLightSet& lights
) {
    if (scene == nullptr) {
        return;
    }

    for (const PointLight3D& pointLight : scene->PointLights()) {
        if (!pointLight.enabled ||
            lights.localCount >= lights.localLights.size() ||
            pointLight.radius <= 0.001f ||
            pointLight.intensity <= 0.0f) {
            continue;
        }

        lights.localLights[lights.localCount] = PointLocalLight(
            pointLight.position,
            pointLight.radius,
            glm::max(pointLight.color, glm::vec3(0.0f)),
            pointLight.intensity
        );
        ++lights.localCount;
    }
}

void AddSceneSpotLights(
    const Scene3D* scene,
    FrameLightSet& lights
) {
    if (scene == nullptr) {
        return;
    }

    for (const SpotLight3D& spotLight : scene->SpotLights()) {
        if (!spotLight.enabled ||
            lights.localCount >= lights.localLights.size() ||
            spotLight.radius <= 0.001f ||
            spotLight.intensity <= 0.0f) {
            continue;
        }

        lights.localLights[lights.localCount] = SpotLocalLight(
            spotLight.position,
            spotLight.direction,
            spotLight.radius,
            glm::max(spotLight.color, glm::vec3(0.0f)),
            spotLight.intensity,
            spotLight.innerConeDegrees,
            spotLight.outerConeDegrees
        );
        ++lights.localCount;
    }
}

void AddSceneRectLights(
    const Scene3D* scene,
    FrameLightSet& lights
) {
    if (scene == nullptr) {
        return;
    }

    for (const RectLight3D& rectLight : scene->RectLights()) {
        if (!rectLight.enabled ||
            lights.localCount >= lights.localLights.size() ||
            rectLight.radius <= 0.001f ||
            rectLight.intensity <= 0.0f ||
            rectLight.width <= 0.001f ||
            rectLight.height <= 0.001f) {
            continue;
        }

        lights.localLights[lights.localCount] = RectLocalLight(
            rectLight.position,
            rectLight.direction,
            rectLight.width,
            rectLight.height,
            rectLight.radius,
            glm::max(rectLight.color, glm::vec3(0.0f)),
            rectLight.intensity
        );
        ++lights.localCount;
        ++lights.rectCount;
    }
}

bool ApplySceneDirectionalLight(
    const Scene3D* scene,
    FrameLightSet& lights
) {
    if (scene == nullptr) {
        return false;
    }

    const DirectionalLight3D* directionalLight = scene->PrimaryDirectionalLight();
    if (directionalLight == nullptr || !directionalLight->enabled) {
        return false;
    }

    glm::vec3 direction = directionalLight->direction;
    if (glm::dot(direction, direction) <= 0.0001f) {
        return false;
    }

    lights.primaryDirectional.direction = glm::normalize(direction);
    lights.primaryDirectional.intensity = std::max(directionalLight->intensity, 0.0f);
    lights.primaryDirectional.ambient = std::max(directionalLight->ambient, 0.0f);
    lights.primaryDirectional.specular = std::max(directionalLight->specular, 0.0f);
    lights.directionalCount = 1;
    return true;
}

bool ApplyMaterialDirectionalFallback(
    std::span<const RenderCommand> renderCommands,
    FrameLightSet& lights
) {
    for (const RenderCommand& command : renderCommands) {
        const glm::vec3 candidate{
            command.materialPushConstants.materialCustom.x,
            command.materialPushConstants.materialCustom.y,
            command.materialPushConstants.materialCustom.z
        };
        if (glm::dot(candidate, candidate) <= 0.0001f) {
            continue;
        }

        lights.primaryDirectional.direction = glm::normalize(candidate);
        lights.primaryDirectional.intensity =
            std::max(command.materialPushConstants.materialControls.y, 0.0f);
        lights.primaryDirectional.ambient =
            std::max(command.materialPushConstants.materialCustom.w, 0.0f);
        lights.primaryDirectional.specular =
            std::max(command.materialPushConstants.materialControls.z, 0.0f);
        lights.directionalCount = 1;
        return true;
    }

    return false;
}

int GBufferDebugViewIndex(ForwardDebugView view) {
    switch (view) {
    case ForwardDebugView::GBufferAlbedo:
        return 0;
    case ForwardDebugView::GBufferNormal:
        return 1;
    case ForwardDebugView::GBufferRoughness:
        return 2;
    case ForwardDebugView::GBufferMetallic:
        return 3;
    case ForwardDebugView::GBufferMaterialId:
        return 4;
    case ForwardDebugView::GBufferDepth:
        return 5;
    case ForwardDebugView::GBufferEmissive:
        return 6;
    case ForwardDebugView::GBufferVelocity:
        return 7;
    case ForwardDebugView::DeferredShadow:
        return 8;
    case ForwardDebugView::ShadowCascade:
        return 9;
    default:
        return -1;
    }
}

int DeferredPbrDebugViewIndex(ForwardDebugView view) {
    switch (view) {
    case ForwardDebugView::DeferredDirect:
        return 1;
    case ForwardDebugView::DeferredAmbient:
        return 2;
    case ForwardDebugView::DeferredSpecular:
        return 3;
    case ForwardDebugView::DeferredLightComplexity:
        return 4;
    case ForwardDebugView::DeferredTileOccupancy:
        return 5;
    case ForwardDebugView::DeferredMaterialTable:
        return 6;
    case ForwardDebugView::LocalShadowAtlas:
        return 7;
    case ForwardDebugView::LocalShadowVisibility:
        return 8;
    case ForwardDebugView::ContactShadow:
        return 9;
    case ForwardDebugView::LocalShadowFace:
        return 10;
    case ForwardDebugView::Ssao:
        return 11;
    case ForwardDebugView::Ssr:
        return 12;
    case ForwardDebugView::ReflectionProbe:
        return 13;
    case ForwardDebugView::HeightFog:
        return 14;
    default:
        return 0;
    }
}

int WeightedTranslucencyDebugViewIndex(ForwardDebugView view) {
    switch (view) {
    case ForwardDebugView::WeightedTranslucencyAccum:
        return 1;
    case ForwardDebugView::WeightedTranslucencyRevealage:
        return 2;
    case ForwardDebugView::WeightedTranslucencyWeight:
        return 3;
    default:
        return 0;
    }
}

bool UsesDeferredHdrComposite(ForwardDebugView view) {
    return view == ForwardDebugView::DeferredHdr ||
        view == ForwardDebugView::DeferredDirect ||
        view == ForwardDebugView::DeferredAmbient ||
        view == ForwardDebugView::DeferredSpecular ||
        view == ForwardDebugView::DeferredLightComplexity ||
        view == ForwardDebugView::DeferredTileOccupancy ||
        view == ForwardDebugView::DeferredMaterialTable ||
        view == ForwardDebugView::LocalShadowAtlas ||
        view == ForwardDebugView::LocalShadowVisibility ||
        view == ForwardDebugView::ContactShadow ||
        view == ForwardDebugView::LocalShadowFace ||
        view == ForwardDebugView::Ssao ||
        view == ForwardDebugView::Ssr ||
        view == ForwardDebugView::ReflectionProbe ||
        view == ForwardDebugView::HeightFog ||
        view == ForwardDebugView::Bloom ||
        view == ForwardDebugView::ColorGrading ||
        view == ForwardDebugView::ToneMapping ||
        view == ForwardDebugView::AutoExposure ||
        view == ForwardDebugView::Sharpening ||
        view == ForwardDebugView::WeightedTranslucencyAccum ||
        view == ForwardDebugView::WeightedTranslucencyRevealage ||
        view == ForwardDebugView::WeightedTranslucencyWeight;
}

std::optional<ForwardDebugView> ForwardDebugViewFromEnvironment() {
    const std::string value = ReadEnvironmentString("SE_RENDER_VIEW");
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "deferred-hdr" || value == "DeferredHDR" || value == "deferred_hdr") {
        return ForwardDebugView::DeferredHdr;
    }
    if (value == "deferred-shadow" || value == "DeferredShadow" || value == "deferred_shadow") {
        return ForwardDebugView::DeferredShadow;
    }
    if (value == "shadow-cascade" ||
        value == "ShadowCascade" ||
        value == "shadow_cascade" ||
        value == "csm-cascade" ||
        value == "csm_cascade" ||
        value == "cascade-debug" ||
        value == "cascade_debug") {
        return ForwardDebugView::ShadowCascade;
    }
    if (value == "deferred-direct" || value == "DeferredDirect" || value == "deferred_direct") {
        return ForwardDebugView::DeferredDirect;
    }
    if (value == "deferred-ambient" || value == "DeferredAmbient" || value == "deferred_ambient") {
        return ForwardDebugView::DeferredAmbient;
    }
    if (value == "deferred-specular" || value == "DeferredSpecular" || value == "deferred_specular") {
        return ForwardDebugView::DeferredSpecular;
    }
    if (value == "forward-light-complexity" ||
        value == "ForwardLightComplexity" ||
        value == "forward_light_complexity" ||
        value == "forward-plus-light-complexity" ||
        value == "forward_plus_light_complexity") {
        return ForwardDebugView::ForwardLightComplexity;
    }
    if (value == "deferred-light-complexity" ||
        value == "DeferredLightComplexity" ||
        value == "deferred_light_complexity" ||
        value == "light-complexity" ||
        value == "light_complexity") {
        return ForwardDebugView::DeferredLightComplexity;
    }
    if (value == "deferred-material-table" ||
        value == "DeferredMaterialTable" ||
        value == "deferred_material_table" ||
        value == "material-table" ||
        value == "material_table") {
        return ForwardDebugView::DeferredMaterialTable;
    }
    if (value == "deferred-tile-occupancy" ||
        value == "DeferredTileOccupancy" ||
        value == "deferred_tile_occupancy" ||
        value == "tile-occupancy" ||
        value == "tile_occupancy") {
        return ForwardDebugView::DeferredTileOccupancy;
    }
    if (value == "local-shadow-atlas" ||
        value == "LocalShadowAtlas" ||
        value == "local_shadow_atlas" ||
        value == "local-shadow" ||
        value == "local_shadow" ||
        value == "local-shadow-debug" ||
        value == "local_shadow_debug") {
        return ForwardDebugView::LocalShadowAtlas;
    }
    if (value == "local-shadow-visibility" ||
        value == "LocalShadowVisibility" ||
        value == "local_shadow_visibility" ||
        value == "local-shadow-resolve" ||
        value == "local_shadow_resolve" ||
        value == "local-shadow-visibility-debug" ||
        value == "local_shadow_visibility_debug") {
        return ForwardDebugView::LocalShadowVisibility;
    }
    if (value == "contact-shadow" ||
        value == "ContactShadow" ||
        value == "contact_shadow" ||
        value == "deferred-contact-shadow" ||
        value == "deferred_contact_shadow" ||
        value == "contact-shadow-debug" ||
        value == "contact_shadow_debug") {
        return ForwardDebugView::ContactShadow;
    }
    if (value == "local-shadow-face" ||
        value == "LocalShadowFace" ||
        value == "local_shadow_face" ||
        value == "point-shadow-face" ||
        value == "point_shadow_face" ||
        value == "local-shadow-seam" ||
        value == "local_shadow_seam" ||
        value == "point-shadow-seam" ||
        value == "point_shadow_seam") {
        return ForwardDebugView::LocalShadowFace;
    }
    if (value == "ssao" ||
        value == "SSAO" ||
        value == "screen-space-ao" ||
        value == "screen_space_ao" ||
        value == "ambient-occlusion" ||
        value == "ambient_occlusion") {
        return ForwardDebugView::Ssao;
    }
    if (value == "ssr" ||
        value == "SSR" ||
        value == "screen-space-reflection" ||
        value == "screen_space_reflection" ||
        value == "screen-space-reflections" ||
        value == "screen_space_reflections" ||
        value == "reflection-debug" ||
        value == "reflection_debug") {
        return ForwardDebugView::Ssr;
    }
    if (value == "reflection-probe" ||
        value == "reflection_probe" ||
        value == "ReflectionProbe" ||
        value == "global-reflection-probe" ||
        value == "global_reflection_probe" ||
        value == "reflection-fallback" ||
        value == "reflection_fallback") {
        return ForwardDebugView::ReflectionProbe;
    }
    if (value == "height-fog" ||
        value == "height_fog" ||
        value == "HeightFog" ||
        value == "distance-fog" ||
        value == "distance_fog" ||
        value == "fog") {
        return ForwardDebugView::HeightFog;
    }
    if (value == "bloom" ||
        value == "Bloom" ||
        value == "bloom-debug" ||
        value == "bloom_debug") {
        return ForwardDebugView::Bloom;
    }
    if (value == "color-grading" ||
        value == "color_grading" ||
        value == "color-grade" ||
        value == "color_grade" ||
        value == "grading" ||
        value == "ColorGrading") {
        return ForwardDebugView::ColorGrading;
    }
    if (value == "tone-map" ||
        value == "tone_map" ||
        value == "tonemap" ||
        value == "tone-mapping" ||
        value == "tone_mapping" ||
        value == "ToneMapping") {
        return ForwardDebugView::ToneMapping;
    }
    if (value == "auto-exposure" ||
        value == "auto_exposure" ||
        value == "autoexposure" ||
        value == "AutoExposure") {
        return ForwardDebugView::AutoExposure;
    }
    if (value == "sharpening" ||
        value == "sharpen" ||
        value == "sharpness" ||
        value == "Sharpening") {
        return ForwardDebugView::Sharpening;
    }
    if (value == "wboit-accum" ||
        value == "wboit_accum" ||
        value == "weighted-translucency-accum" ||
        value == "weighted_translucency_accum" ||
        value == "WeightedTranslucencyAccum") {
        return ForwardDebugView::WeightedTranslucencyAccum;
    }
    if (value == "wboit-revealage" ||
        value == "wboit_revealage" ||
        value == "weighted-translucency-revealage" ||
        value == "weighted_translucency_revealage" ||
        value == "WeightedTranslucencyRevealage") {
        return ForwardDebugView::WeightedTranslucencyRevealage;
    }
    if (value == "wboit-weight" ||
        value == "wboit_weight" ||
        value == "weighted-translucency-weight" ||
        value == "weighted_translucency_weight" ||
        value == "WeightedTranslucencyWeight") {
        return ForwardDebugView::WeightedTranslucencyWeight;
    }
    if (value == "gbuffer-albedo" || value == "GBufferAlbedo" || value == "gbuffer_albedo") {
        return ForwardDebugView::GBufferAlbedo;
    }
    if (value == "gbuffer-normal" || value == "GBufferNormal" || value == "gbuffer_normal") {
        return ForwardDebugView::GBufferNormal;
    }
    if (value == "gbuffer-roughness" || value == "GBufferRoughness" || value == "gbuffer_roughness") {
        return ForwardDebugView::GBufferRoughness;
    }
    if (value == "gbuffer-metallic" || value == "GBufferMetallic" || value == "gbuffer_metallic") {
        return ForwardDebugView::GBufferMetallic;
    }
    if (value == "gbuffer-material-id" ||
        value == "GBufferMaterialId" ||
        value == "gbuffer_material_id" ||
        value == "material-id" ||
        value == "material_id") {
        return ForwardDebugView::GBufferMaterialId;
    }
    if (value == "gbuffer-depth" || value == "GBufferDepth" || value == "gbuffer_depth") {
        return ForwardDebugView::GBufferDepth;
    }
    if (value == "gbuffer-emissive" || value == "GBufferEmissive" || value == "gbuffer_emissive") {
        return ForwardDebugView::GBufferEmissive;
    }
    if (value == "gbuffer-velocity" || value == "GBufferVelocity" || value == "gbuffer_velocity") {
        return ForwardDebugView::GBufferVelocity;
    }
    if (value == "lit" || value == "Lit") {
        return ForwardDebugView::Lit;
    }
    if (value == "albedo" || value == "Albedo") {
        return ForwardDebugView::Albedo;
    }
    if (value == "normal" || value == "Normal") {
        return ForwardDebugView::Normal;
    }
    if (value == "roughness" || value == "Roughness") {
        return ForwardDebugView::Roughness;
    }
    if (value == "metallic" || value == "Metallic") {
        return ForwardDebugView::Metallic;
    }
    if (value == "occlusion" || value == "Occlusion") {
        return ForwardDebugView::Occlusion;
    }
    if (value == "shadow" || value == "Shadow") {
        return ForwardDebugView::Shadow;
    }
    if (value == "light-depth" || value == "LightSpaceDepth") {
        return ForwardDebugView::LightSpaceDepth;
    }

    return std::nullopt;
}

std::optional<u32> ToneMapModeFromEnvironment() {
    std::string value = ReadEnvironmentString("SE_TONEMAP_MODE");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_TONE_MAP_MODE");
    }
    if (value.empty()) {
        value = ReadEnvironmentString("SE_TONEMAP");
    }
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "aces" || value == "ACES" || value == "0") {
        return 0u;
    }
    if (value == "reinhard" || value == "Reinhard" || value == "1") {
        return 1u;
    }
    if (value == "linear" ||
        value == "Linear" ||
        value == "linear-clamp" ||
        value == "linear_clamp" ||
        value == "off" ||
        value == "2") {
        return 2u;
    }

    return std::nullopt;
}

bool MaterialFlagEnabled(f32 flags, f32 bit) {
    return std::fmod(std::floor(flags / bit), 2.0f) > 0.5f;
}

std::optional<VulkanShadowQuality> ShadowQualityFromEnvironment() {
    const std::string value = ReadEnvironmentString("SE_SHADOW_QUALITY");
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "off" || value == "Off" || value == "OFF" || value == "0") {
        return VulkanShadowQuality::Off;
    }
    if (value == "low" || value == "Low" || value == "LOW" || value == "1") {
        return VulkanShadowQuality::Low;
    }
    if (value == "medium" || value == "Medium" || value == "MEDIUM" || value == "2") {
        return VulkanShadowQuality::Medium;
    }
    if (value == "high" || value == "High" || value == "HIGH" || value == "3") {
        return VulkanShadowQuality::High;
    }
    if (value == "ultra" || value == "Ultra" || value == "ULTRA" || value == "4") {
        return VulkanShadowQuality::Ultra;
    }

    return std::nullopt;
}

}

VulkanRenderer::VulkanRenderer(
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
) : m_Window(window),
    m_Device(device),
    m_PhysicalDevice(physicalDevice),
    m_Surface(surface),
    m_Instance(instance),
    m_CommandPool(commandPool),
    m_Scene(scene),
    m_Camera(camera),
    m_RenderResources(renderResources),
    m_PipelineSpec(std::move(pipelineSpec)) {
    m_RenderFeatures.Add(std::make_unique<VulkanSsaoFeature>());
    m_RenderFeatures.Add(std::make_unique<VulkanSsrFeature>());
    m_RenderFeatures.Add(std::make_unique<VulkanReflectionProbeFallbackFeature>());
    m_RenderFeatures.Add(std::make_unique<VulkanHeightFogFeature>());
    m_RenderFeatures.Add(std::make_unique<VulkanPostProcessFeature>());
    ApplyEnvironmentRenderSettings();
    if (m_Scene != nullptr) {
        SE_ASSERT(!m_Scene->Empty(), "VulkanRenderer requires at least one renderable in the 2D scene");
    }
    ValidateSceneResources();
    CreateSwapchainResources();
}

VulkanRenderer::~VulkanRenderer() {
    WaitIdle();

    m_GpuTimer.reset();
    m_CommandBuffer.reset();
    m_InstanceBuffer.reset();
    m_Framebuffer.reset();
    m_BloomUpsamplePipeline.reset();
    m_BloomDownsamplePipeline.reset();
    m_OverlayGraphicsPipeline.reset();
    m_InstancedGraphicsPipeline.reset();
    m_DoubleSidedInstancedGraphicsPipeline.reset();
    m_GBufferDebugPipeline.reset();
    m_HdrCompositePipeline.reset();
    m_WeightedTranslucencyResolvePipeline.reset();
    m_DeferredLightingPipeline.reset();
    m_LightTileCullComputePipeline.reset();
    m_LightClusterCullComputePipeline.reset();
    m_AutoExposureComputePipeline.reset();
    m_GBufferGraphicsPipeline.reset();
    m_DoubleSidedGBufferGraphicsPipeline.reset();
    m_DepthPrefillGraphicsPipeline.reset();
    m_DoubleSidedDepthPrefillGraphicsPipeline.reset();
    m_WeightedTranslucencyGraphicsPipeline.reset();
    m_DoubleSidedWeightedTranslucencyGraphicsPipeline.reset();
    m_ForwardResidualGraphicsPipeline.reset();
    m_DoubleSidedForwardResidualGraphicsPipeline.reset();
    m_ShadowGraphicsPipeline.reset();
    m_DoubleSidedShadowGraphicsPipeline.reset();
    m_GraphicsPipeline.reset();
    m_DoubleSidedGraphicsPipeline.reset();
    m_ImGuiLayer.reset();
    m_DepthLoadRenderPass.reset();
    m_RenderPass.reset();
    m_GBufferFramebuffer.reset();
    m_GBufferRenderPass.reset();
    m_WeightedTranslucencyFramebuffer.reset();
    m_WeightedTranslucencyRenderPass.reset();
    m_HdrFramebuffer.reset();
    m_HdrRenderPass.reset();
    m_DirectionalShadowCascadeFramebuffer.reset();
    m_LocalShadowFramebuffer.reset();
    m_ShadowFramebuffer.reset();
    m_ShadowRenderPass.reset();
    m_LocalShadowAtlas.reset();
    m_DirectionalShadowCascadeAtlas.reset();
    m_ShadowMap.reset();
    m_BloomUpsampleFramebuffer.reset();
    m_BloomDownsampleFramebuffer.reset();
    m_BloomUpsampleRenderPass.reset();
    m_BloomDownsampleRenderPass.reset();
    m_HdrDescriptorSets.reset();
    m_BloomDescriptorSets.reset();
    m_WeightedTranslucencyDescriptorSets.reset();
    m_GBufferDescriptorSets.reset();
    m_SceneTargetSampler.reset();
    m_ColorGradingLut.reset();
    m_BloomPyramid.reset();
    m_SceneRenderTargets.reset();
    if (m_IblSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device.Handle(), m_IblSampler, nullptr);
        m_IblSampler = VK_NULL_HANDLE;
    }
    m_ReflectionProbeResources.Release();
    m_IblPrefilteredImage.reset();
    m_IblIrradianceImage.reset();
    m_IblBrdfImage.reset();
    m_IblPrefilteredView = VK_NULL_HANDLE;
    m_IblIrradianceView = VK_NULL_HANDLE;
    m_DepthBuffer.reset();
    m_MaterialDescriptorSets.reset();
    m_OverlayDescriptorSets.reset();
    m_DescriptorSets.reset();
    m_OverlayUniformBuffer.reset();
    m_LightBuffer.reset();
    m_LightTileDiagnosticsBuffer.reset();
    m_AutoExposureBuffer.reset();
    m_MaterialBuffer.reset();
    m_DirectionalShadowCascadeBuffer.reset();
    m_LocalShadowBuffer.reset();
    m_UniformBuffer.reset();
    m_MaterialDescriptorSetLayout.reset();
    m_DescriptorSetLayout.reset();
    m_SyncObjects.reset();
    m_Swapchain.reset();
}

void VulkanRenderer::DrawFrame() {
    RendererStats frameStats{};
    ResetTransformMatrixRecalculationCount();
    const FrameClock::time_point frameStart = FrameClock::now();
    FrameClock::time_point sectionStart = frameStart;

    const VkFence currentFrameFence = m_SyncObjects->InFlightFence(m_CurrentFrame);
    const VkSemaphore imageAvailableSemaphore = m_SyncObjects->ImageAvailableSemaphore(m_CurrentFrame);

    vkWaitForFences(
        m_Device.Handle(),
        1,
        &currentFrameFence,
        VK_TRUE,
        std::numeric_limits<u64>::max()
    );

    u32 imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        m_Device.Handle(),
        m_Swapchain->Handle(),
        std::numeric_limits<u64>::max(),
        imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire Vulkan swapchain image");
    }

    const VkFence imageInFlightFence = m_SyncObjects->ImageInFlightFence(imageIndex);
    if (imageInFlightFence != VK_NULL_HANDLE) {
        vkWaitForFences(
            m_Device.Handle(),
            1,
            &imageInFlightFence,
            VK_TRUE,
            std::numeric_limits<u64>::max()
        );
    }
    if (m_GpuTimer != nullptr) {
        frameStats.gpu = m_GpuTimer->ReadFrameStats(imageIndex);
    }
    const FrameLightTileGpuReadbackStats lightTileGpuStats =
        ReadPreviousLightTileGpuStats(imageIndex);
    const FrameAutoExposureReadbackStats autoExposureStats =
        ReadPreviousAutoExposureStats(imageIndex);

    FrameClock::time_point sectionEnd = FrameClock::now();
    frameStats.cpu.waitAcquireMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    m_ImGuiLayer->BeginFrame(
        m_Scene,
        m_Camera,
        m_ImGuiScene3D,
        m_ImGuiCamera3D,
        &m_RenderResources,
        &m_LastStats,
        &m_RenderDebugSettings,
        &m_ShadowSettings
    );

    sectionEnd = FrameClock::now();
    frameStats.cpu.imguiMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    ApplyShadowMapSettings();

    HandleObjectPicking();

    sectionEnd = FrameClock::now();
    frameStats.cpu.pickingMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    const VkExtent2D extent = m_Swapchain->Extent();
    const f32 aspectRatio = static_cast<f32>(extent.width) / static_cast<f32>(extent.height);
    std::optional<FrameMatrices> mainFrameMatrices;
    if (m_FrameMatricesProvider) {
        mainFrameMatrices = m_FrameMatricesProvider(aspectRatio);
    } else if (m_Camera != nullptr) {
        mainFrameMatrices = FrameMatrices{
            m_Camera->ViewMatrix(),
            m_Camera->ProjectionMatrix(aspectRatio)
        };
    }
    std::optional<FrameMatrices> overlayFrameMatrices;
    if (m_OverlayCamera3D != nullptr) {
        overlayFrameMatrices = FrameMatrices{
            m_OverlayCamera3D->ViewMatrix(),
            m_OverlayCamera3D->ProjectionMatrix(aspectRatio)
        };
    }

    std::optional<Frustum> mainFrustum;
    if (m_RenderQueueBuilder && mainFrameMatrices.has_value()) {
        mainFrustum = Frustum::FromViewProjection(
            mainFrameMatrices->proj * mainFrameMatrices->view
        );
    }
    std::optional<Frustum> overlayFrustum;
    if (overlayFrameMatrices.has_value()) {
        overlayFrustum = Frustum::FromViewProjection(
            overlayFrameMatrices->proj * overlayFrameMatrices->view
        );
    }

    RenderQueueCullingStats mainCullingStats{};
    RenderQueueCullingStats overlayCullingStats{};
    RenderQueueCullingStats shadowCullingStats{};
    RenderQueueCacheStats mainCacheStats{};
    RenderQueueCacheStats overlayCacheStats{};
    const bool shadowPassEnabled = m_ShadowSettings.enabled &&
        m_ShadowSettings.strength > 0.001f;
    m_ShadowRenderQueue.Clear();
    if (m_RenderQueueBuilder) {
        m_RenderQueueBuilder(
            m_RenderQueue,
            RenderQueueContext{
                mainFrustum.has_value() ? &*mainFrustum : nullptr,
                &mainCullingStats,
                &mainCacheStats,
                shadowPassEnabled ? &m_ShadowRenderQueue : nullptr,
                shadowPassEnabled ? &shadowCullingStats : nullptr
            }
        );
    } else {
        SE_ASSERT(m_Scene != nullptr, "VulkanRenderer needs a render queue builder when no 2D scene is attached");
        m_RenderQueue.BuildFromScene2D(
            m_RenderResources,
            m_Scene->Renderables(),
            m_Scene->SelectedRenderable()
        );
    }
    if (m_OverlayScene3D != nullptr) {
        RenderQueueBuildOptions overlayBuildOptions{};
        overlayBuildOptions.frustum = overlayFrustum.has_value() ? &*overlayFrustum : nullptr;
        overlayBuildOptions.cullingStats = &overlayCullingStats;
        overlayBuildOptions.cacheStats = &overlayCacheStats;
        overlayBuildOptions.sceneIdentity = m_OverlayScene3D;
        overlayBuildOptions.sceneMembershipRevision = m_OverlayScene3D->MembershipRevision();
        overlayBuildOptions.sceneRenderRevision = m_OverlayScene3D->RenderRevision();
        overlayBuildOptions.useSceneRevisions = true;
        m_OverlayRenderQueue.BuildFromScene3D(
            m_RenderResources,
            m_OverlayScene3D->Renderables(),
            m_OverlayScene3D->SelectedRenderable(),
            overlayBuildOptions
        );
        if (shadowPassEnabled) {
            m_ShadowRenderQueue.BuildShadowCastersFrom(
                m_OverlayRenderQueue,
                &shadowCullingStats
            );
            shadowCullingStats.culled += overlayCullingStats.culled;
        }
    } else {
        m_OverlayRenderQueue.Clear();
    }
    sectionEnd = FrameClock::now();
    frameStats.cpu.queueBuildMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    const std::span<const RenderCommand> mainCommands = m_RenderQueue.Commands();
    const std::span<const RenderCommand> overlayCommands = m_OverlayRenderQueue.Commands();
    const std::span<const RenderCommand> shadowCommands = ShadowRenderCommands();
    const bool shadowSamplingEnabled = shadowPassEnabled && !shadowCommands.empty();
    const bool recordTransparentAlphaReference =
        WeightedTranslucencyAlphaReferenceEnabled();
    if (m_LocalShadowCacheStates.size() != m_Swapchain->Images().size()) {
        m_LocalShadowCacheStates.assign(m_Swapchain->Images().size(), LocalShadowCacheState{});
    }
    const LocalShadowCacheState* localShadowCacheState =
        imageIndex < m_LocalShadowCacheStates.size()
            ? &m_LocalShadowCacheStates[imageIndex]
            : nullptr;
    std::vector<RenderCommand> gBufferCommands;
    std::vector<RenderCommand> weightedTranslucencyCommands;
    std::vector<RenderCommand> forwardResidualCommands;
    const bool has3DMainPass =
        m_PipelineSpec.vertexLayout == VertexLayout::Vertex3D ||
        m_PipelineSpec.vertexLayout == VertexLayout::Vertex3DInstanced;
    const bool showDeferredHdr =
        UsesDeferredHdrComposite(m_RenderDebugSettings.forwardView);
    const bool hdrCompositeAvailable =
        showDeferredHdr &&
        m_HdrCompositePipeline != nullptr &&
        m_HdrDescriptorSets != nullptr;
    const FrameLightSet frameLightSet = BuildFrameLightSet(mainCommands);
    const FrameReflectionProbeSet frameReflectionProbes =
        BuildFrameReflectionProbeSet(
            mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr
        );
    const VulkanRenderFeatureContext renderFeatureContext{
        m_ShadowSettings,
        m_RenderDebugSettings,
        has3DMainPass,
        m_DeferredLightingPipeline != nullptr && m_GBufferDescriptorSets != nullptr,
        hdrCompositeAvailable,
        frameReflectionProbes.sceneProbeCount,
        frameReflectionProbes.activeLocalProbeCount,
        frameReflectionProbes.localProbe.sceneOwned,
        m_ShadowSettings.reflectionProbeCubemapEnabled &&
            frameReflectionProbes.fallbackEnabled &&
            frameReflectionProbes.localProbe.sceneOwned &&
            frameReflectionProbes.selectedCubemapSamplingCount > 0,
        static_cast<u32>(frameReflectionProbes.captureSource),
        static_cast<u32>(frameReflectionProbes.captureFallbackReason)
    };
    const FrameMaterialSet frameMaterialSet = has3DMainPass
        ? BuildFrameMaterialSet(mainCommands)
        : FrameMaterialSet{};
    const FrameLightConstants frameLights = frameLightSet.Constants();
    const glm::mat4 lightViewProjection = LightViewProjection(shadowCommands, frameLightSet);
    const DirectionalShadowCascadeSet directionalShadowCascades =
        BuildDirectionalShadowCascades(
            shadowCommands,
            frameLightSet,
            mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr,
            shadowSamplingEnabled
        );
    const LocalShadowTileSet localShadowTiles = BuildLocalShadowTiles(
        frameLightSet,
        shadowCommands,
        m_LocalShadowAtlas != nullptr ? m_LocalShadowAtlas->TileCapacity() : 0u,
        localShadowCacheState
    );
    UpdateUniformBuffer(
        imageIndex,
        mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr,
        lightViewProjection,
        frameLights,
        frameReflectionProbes,
        shadowSamplingEnabled
    );
    UpdateOverlayUniformBuffer(
        imageIndex,
        overlayFrameMatrices.has_value() ? &*overlayFrameMatrices : nullptr,
        lightViewProjection,
        frameLights,
        frameReflectionProbes,
        shadowSamplingEnabled
    );
    FrameLightTileStats lightTileStats{};
    UpdateLightBuffer(
        imageIndex,
        frameLightSet,
        extent,
        mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr,
        &lightTileStats
    );
    UpdateMaterialBuffer(imageIndex, frameMaterialSet);
    UpdateDirectionalShadowCascadeBuffer(
        imageIndex,
        directionalShadowCascades,
        lightViewProjection
    );
    UpdateLocalShadowBuffer(imageIndex, localShadowTiles);

    sectionEnd = FrameClock::now();
    frameStats.cpu.uniformUpdateMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    const VkSemaphore renderFinishedSemaphore = m_SyncObjects->RenderFinishedSemaphore(imageIndex);
    m_SyncObjects->MarkImageInFlight(imageIndex, currentFrameFence);
    vkResetFences(m_Device.Handle(), 1, &currentFrameFence);

    const int deferredPbrDebugView =
        DeferredPbrDebugViewIndex(m_RenderDebugSettings.forwardView);
    const int weightedTranslucencyDebugView =
        WeightedTranslucencyDebugViewIndex(m_RenderDebugSettings.forwardView);
    const int gBufferDebugView = GBufferDebugViewIndex(m_RenderDebugSettings.forwardView);
    const bool allowInstanceBatchCacheReuse =
        mainCacheStats.queueCacheHits > 0 &&
        mainCacheStats.queueCacheMisses == 0;
    const bool reusedInstanceBatches = BuildMainInstanceBatches(
        mainCommands,
        allowInstanceBatchCacheReuse
    );
    bool uploadedMainInstances = false;
    bool skippedMainInstanceUpload = false;
    if (m_InstanceBuffer != nullptr) {
        uploadedMainInstances = UploadMainInstancesIfNeeded(imageIndex);
        skippedMainInstanceUpload = !uploadedMainInstances;
    }
    frameStats.draw = DrawStatsForQueues(
        mainCommands,
        overlayCommands,
        shadowCommands
    );
    frameStats.shadowCascades = ShadowCascadeStatsFor(directionalShadowCascades);
    frameStats.shadowCascades.pcfKernelRadius =
        std::clamp<u32>(m_ShadowSettings.pcfKernelRadius, 0u, 2u);
    frameStats.shadowCascades.pcssStrength =
        std::clamp(m_ShadowSettings.pcssStrength, 0.0f, 1.0f);
    frameStats.shadowCascades.blendRatio =
        std::clamp(m_ShadowSettings.cascadeBlendRatio, 0.0f, 0.25f);
    frameStats.shadowCascades.fadeRatio =
        std::clamp(m_ShadowSettings.cascadeFadeRatio, 0.0f, 0.35f);
    frameStats.shadowCascades.contactShadowStrength =
        std::clamp(m_ShadowSettings.contactShadowStrength, 0.0f, 1.0f);
    frameStats.shadowCascades.contactShadowLength =
        std::clamp(m_ShadowSettings.contactShadowLength, 0.0f, 1.0f);
    frameStats.shadowCascades.contactShadowThickness =
        std::clamp(m_ShadowSettings.contactShadowThickness, 0.0f, 0.5f);
    frameStats.shadowCascades.contactShadowSteps =
        std::clamp<u32>(m_ShadowSettings.contactShadowSteps, 0u, 12u);
    frameStats.shadowCascades.contactShadowJitterStrength =
        std::clamp(m_ShadowSettings.contactShadowJitterStrength, 0.0f, 1.0f);
    frameStats.shadowCascades.contactShadowEdgeFadePixels =
        std::clamp(m_ShadowSettings.contactShadowEdgeFadePixels, 0.0f, 96.0f);
    m_RenderFeatures.WriteStats(
        VulkanRenderFeatureStatsContext{
            frameStats,
            renderFeatureContext
        }
    );
    WriteFrameReflectionProbeStats(
        frameReflectionProbes,
        frameStats.reflectionProbe
    );
    const bool recordAutoExposureCompute =
        frameStats.postProcess.autoExposureEnabled > 0 &&
        hdrCompositeAvailable &&
        m_AutoExposureComputePipeline != nullptr &&
        m_AutoExposureBuffer != nullptr &&
        m_HdrDescriptorSets != nullptr;
    frameStats.postProcess.autoExposureHistogramEnabled =
        recordAutoExposureCompute ? 1u : 0u;
    frameStats.postProcess.autoExposureHistoryValid =
        autoExposureStats.valid ? 1u : 0u;
    frameStats.postProcess.autoExposureGpuExposure =
        autoExposureStats.exposure;
    frameStats.postProcess.autoExposureGpuTargetExposure =
        autoExposureStats.targetExposure;
    frameStats.postProcess.autoExposureGpuAverageLuminance =
        autoExposureStats.averageLuminance;
    frameStats.postProcess.autoExposureFallbacks =
        frameStats.postProcess.autoExposureEnabled > 0 &&
            !recordAutoExposureCompute
            ? 1u
            : 0u;
    const bool recordBloomPyramid =
        frameStats.postProcess.bloomEnabled > 0 &&
        hdrCompositeAvailable &&
        m_BloomPyramid != nullptr &&
        m_BloomDescriptorSets != nullptr &&
        m_BloomDownsampleRenderPass != nullptr &&
        m_BloomUpsampleRenderPass != nullptr &&
        m_BloomDownsampleFramebuffer != nullptr &&
        m_BloomUpsampleFramebuffer != nullptr &&
        m_BloomDownsamplePipeline != nullptr &&
        m_BloomUpsamplePipeline != nullptr;
    frameStats.postProcess.bloomPyramidEnabled =
        recordBloomPyramid ? 1u : 0u;
    frameStats.postProcess.bloomPyramidMipCount =
        m_BloomPyramid != nullptr ? m_BloomPyramid->MipCount() : 0u;
    frameStats.postProcess.bloomPyramidFallbacks =
        frameStats.postProcess.bloomEnabled > 0 && !recordBloomPyramid ? 1u : 0u;
    const bool colorGradingLutReady =
        m_ColorGradingLut != nullptr && m_ColorGradingLut->Uploaded();
    frameStats.postProcess.colorGradingLutEnabled =
        frameStats.postProcess.colorGradingEnabled > 0 &&
            colorGradingLutReady &&
            frameStats.postProcess.colorGradingLutStrength > 0.0001f
            ? 1u
            : 0u;
    frameStats.postProcess.colorGradingLutSize =
        colorGradingLutReady ? m_ColorGradingLut->LutSize() : 0u;
    frameStats.postProcess.colorGradingLutFallbacks =
        frameStats.postProcess.colorGradingEnabled > 0 && !colorGradingLutReady ? 1u : 0u;
    const bool iblBrdfReady =
        m_IblBrdfImage != nullptr &&
        m_IblBrdfImage->View() != VK_NULL_HANDLE &&
        m_IblSampler != VK_NULL_HANDLE;
    const bool iblIrradianceReady =
        m_IblIrradianceImage != nullptr &&
        m_IblIrradianceView != VK_NULL_HANDLE &&
        m_IblSampler != VK_NULL_HANDLE;
    const bool iblPrefilteredReady =
        m_IblPrefilteredImage != nullptr &&
        m_IblPrefilteredView != VK_NULL_HANDLE &&
        m_IblSampler != VK_NULL_HANDLE;
    const bool iblReady =
        iblBrdfReady && iblIrradianceReady && iblPrefilteredReady;
    frameStats.ibl.brdfLutAllocated = iblBrdfReady ? 1u : 0u;
    frameStats.ibl.brdfLutSize =
        iblBrdfReady ? m_IblBrdfImage->Extent().width : 0u;
    frameStats.ibl.brdfLutFormat =
        iblBrdfReady ? m_IblBrdfImage->Format() : VK_FORMAT_UNDEFINED;
    frameStats.ibl.irradianceMapAllocated = iblIrradianceReady ? 1u : 0u;
    frameStats.ibl.irradianceFaceSize =
        iblIrradianceReady ? m_IblIrradianceImage->Extent().width : 0u;
    frameStats.ibl.irradianceFormat =
        iblIrradianceReady ? m_IblIrradianceImage->Format() : VK_FORMAT_UNDEFINED;
    frameStats.ibl.prefilteredMapAllocated = iblPrefilteredReady ? 1u : 0u;
    frameStats.ibl.prefilteredFaceSize =
        iblPrefilteredReady ? m_IblPrefilteredImage->Extent().width : 0u;
    frameStats.ibl.prefilteredMipCount =
        iblPrefilteredReady ? m_IblPrefilteredImage->MipLevels() : 0u;
    frameStats.ibl.prefilteredFormat =
        iblPrefilteredReady ? m_IblPrefilteredImage->Format() : VK_FORMAT_UNDEFINED;
    frameStats.ibl.descriptorSetsBound =
        iblReady && m_DescriptorSets != nullptr
            ? static_cast<u32>(m_DescriptorSets->Count())
            : 0u;
    frameStats.ibl.shaderIntegrationEnabled =
        iblReady && has3DMainPass ? 1u : 0u;
    const bool reflectionProbeCubemapReady = LocalReflectionProbeCubemapReady();
    frameStats.reflectionProbe.localCubemapAllocated =
        reflectionProbeCubemapReady ? 1u : 0u;
    frameStats.reflectionProbe.localCubemapFaceSize =
        reflectionProbeCubemapReady
            ? m_ReflectionProbeResources.FaceSize()
            : 0u;
    frameStats.reflectionProbe.localCubemapMipCount =
        reflectionProbeCubemapReady
            ? m_ReflectionProbeResources.MipCount()
            : 0u;
    frameStats.reflectionProbe.localCubemapFormat =
        reflectionProbeCubemapReady
            ? m_ReflectionProbeResources.Format()
            : VK_FORMAT_UNDEFINED;
    frameStats.reflectionProbe.localCubemapDescriptorSetsBound =
        reflectionProbeCubemapReady
            ? m_ReflectionProbeResources.DescriptorSetsBound()
            : 0u;
    frameStats.reflectionProbe.localCubemapShaderSamplingEnabled =
        m_ShadowSettings.reflectionProbeCubemapEnabled &&
            frameStats.reflectionProbe.captureResourceReady > 0 &&
            frameStats.reflectionProbe.localEnabled > 0 &&
            frameStats.reflectionProbe.localSceneOwned > 0 &&
            has3DMainPass
            ? 1u
            : 0u;
    frameStats.reflectionProbe.localCubemapSourceType =
        frameStats.reflectionProbe.captureSourceType;
    if (m_DirectionalShadowCascadeAtlas != nullptr) {
        const VkExtent2D cascadeAtlasExtent = m_DirectionalShadowCascadeAtlas->Extent();
        frameStats.shadowCascades.atlasAllocated = cascadeAtlasExtent.width > 0 ? 1u : 0u;
        frameStats.shadowCascades.atlasTileSize =
            m_DirectionalShadowCascadeAtlas->TileSize();
        frameStats.shadowCascades.atlasWidth = cascadeAtlasExtent.width;
        frameStats.shadowCascades.atlasHeight = cascadeAtlasExtent.height;
        frameStats.shadowCascades.atlasTileColumns =
            m_DirectionalShadowCascadeAtlas->TileColumns();
        frameStats.shadowCascades.atlasTileRows =
            m_DirectionalShadowCascadeAtlas->TileRows();
        frameStats.shadowCascades.atlasCascadeCapacity =
            m_DirectionalShadowCascadeAtlas->CascadeCapacity();
    }
    if (m_LocalShadowAtlas != nullptr) {
        const VkExtent2D localAtlasExtent = m_LocalShadowAtlas->Extent();
        frameStats.localShadowAtlas.allocated = localAtlasExtent.width > 0 ? 1u : 0u;
        frameStats.localShadowAtlas.tileSize = m_LocalShadowAtlas->TileSize();
        frameStats.localShadowAtlas.atlasWidth = localAtlasExtent.width;
        frameStats.localShadowAtlas.atlasHeight = localAtlasExtent.height;
        frameStats.localShadowAtlas.tileColumns = m_LocalShadowAtlas->TileColumns();
        frameStats.localShadowAtlas.tileRows = m_LocalShadowAtlas->TileRows();
        frameStats.localShadowAtlas.tileCapacity = m_LocalShadowAtlas->TileCapacity();
        frameStats.localShadowAtlas.shadowableLocalLights =
            localShadowTiles.pointLightCount + localShadowTiles.spotLightCount;
        frameStats.localShadowAtlas.pointLightCount = localShadowTiles.pointLightCount;
        frameStats.localShadowAtlas.spotLightCount = localShadowTiles.spotLightCount;
        frameStats.localShadowAtlas.pointFaceTiles = localShadowTiles.pointFaceTiles;
        frameStats.localShadowAtlas.spotTiles = localShadowTiles.spotTiles;
        frameStats.localShadowAtlas.requestedTiles = localShadowTiles.requestedCount;
        frameStats.localShadowAtlas.assignedTiles = localShadowTiles.assignedCount;
        frameStats.localShadowAtlas.droppedTiles = localShadowTiles.droppedCount;
        frameStats.localShadowAtlas.cacheEligibleTiles = localShadowTiles.cacheEligibleTiles;
        frameStats.localShadowAtlas.cacheHitTiles = localShadowTiles.cacheHitTiles;
        frameStats.localShadowAtlas.cacheMissTiles = localShadowTiles.cacheMissTiles;
        frameStats.localShadowAtlas.cacheSkippedTiles =
            localShadowTiles.cacheSkippedTiles;
        frameStats.localShadowAtlas.biasMin =
            std::clamp(m_ShadowSettings.localBiasMin, 0.0f, 0.02f);
        frameStats.localShadowAtlas.biasSlope =
            std::clamp(m_ShadowSettings.localBiasSlope, 0.0f, 0.05f);
        frameStats.localShadowAtlas.pcfRadius =
            std::clamp(m_ShadowSettings.localPcfRadius, 0.0f, 4.0f);
        frameStats.localShadowAtlas.pcfKernelRadius =
            std::clamp<u32>(m_ShadowSettings.localPcfKernelRadius, 0u, 2u);
        frameStats.localShadowAtlas.pcssStrength =
            std::clamp(m_ShadowSettings.localPcssStrength, 0.0f, 1.0f);
        frameStats.localShadowAtlas.faceBlendStrength =
            std::clamp(m_ShadowSettings.localFaceBlendStrength, 0.0f, 1.0f);
    }
    if (m_SceneRenderTargets != nullptr &&
        m_WeightedTranslucencyRenderPass != nullptr &&
        m_WeightedTranslucencyFramebuffer != nullptr) {
        const VkExtent2D weightedExtent =
            m_WeightedTranslucencyFramebuffer->Extent();
        frameStats.weightedTranslucency.allocated =
            weightedExtent.width > 0 && weightedExtent.height > 0 ? 1u : 0u;
        frameStats.weightedTranslucency.accumWidth = weightedExtent.width;
        frameStats.weightedTranslucency.accumHeight = weightedExtent.height;
        frameStats.weightedTranslucency.revealageWidth = weightedExtent.width;
        frameStats.weightedTranslucency.revealageHeight = weightedExtent.height;
        frameStats.weightedTranslucency.accumFormat =
            m_SceneRenderTargets->WeightedTranslucencyAccumFormat();
        frameStats.weightedTranslucency.revealageFormat =
            m_SceneRenderTargets->WeightedTranslucencyRevealageFormat();
        frameStats.weightedTranslucency.renderPassAllocated =
            m_WeightedTranslucencyRenderPass->Handle() != VK_NULL_HANDLE ? 1u : 0u;
        frameStats.weightedTranslucency.framebufferCount =
            static_cast<u32>(m_WeightedTranslucencyFramebuffer->Count());
    }
    if (has3DMainPass && m_GBufferGraphicsPipeline != nullptr) {
        BuildGBufferCommandList(
            mainCommands,
            gBufferCommands,
            weightedTranslucencyCommands,
            forwardResidualCommands,
            mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr,
            recordTransparentAlphaReference,
            frameStats.draw
        );
    }
    frameStats.binds.forwardResidualAlphaReferenceEnabled =
        recordTransparentAlphaReference ? 1u : 0u;
    if (recordTransparentAlphaReference) {
        const u32 weightedDraws =
            static_cast<u32>(weightedTranslucencyCommands.size());
        const u32 residualDraws =
            static_cast<u32>(forwardResidualCommands.size());
        frameStats.binds.weightedTranslucencyAlphaReferenceMismatchDraws =
            weightedDraws > residualDraws
                ? weightedDraws - residualDraws
                : residualDraws - weightedDraws;
    }
    frameStats.draw.mainVisible = mainCullingStats.visible;
    frameStats.draw.mainCulled = mainCullingStats.culled;
    frameStats.draw.overlayVisible = overlayCullingStats.visible;
    frameStats.draw.overlayCulled = overlayCullingStats.culled;
    frameStats.draw.shadowVisible = shadowCullingStats.visible;
    frameStats.draw.shadowCulled = shadowCullingStats.culled;
    frameStats.draw.mainBoundsCacheHits = mainCacheStats.boundsCacheHits;
    frameStats.draw.mainBoundsCacheMisses = mainCacheStats.boundsCacheMisses;
    frameStats.draw.mainCommandCacheHits = mainCacheStats.commandCacheHits;
    frameStats.draw.mainCommandCacheMisses = mainCacheStats.commandCacheMisses;
    frameStats.draw.mainVisibilityCacheHits = mainCacheStats.visibilityCacheHits;
    frameStats.draw.mainVisibilityCacheMisses = mainCacheStats.visibilityCacheMisses;
    frameStats.draw.mainQueueCacheHits = mainCacheStats.queueCacheHits;
    frameStats.draw.mainQueueCacheMisses = mainCacheStats.queueCacheMisses;
    if (reusedInstanceBatches) {
        ++frameStats.draw.mainInstanceBatchCacheHits;
    } else {
        ++frameStats.draw.mainInstanceBatchCacheMisses;
    }
    if (uploadedMainInstances) {
        ++frameStats.binds.mainInstanceBufferUploads;
    } else if (skippedMainInstanceUpload) {
        ++frameStats.binds.mainInstanceBufferUploadSkips;
    }
    ++frameStats.binds.frameLightConstantUpdates;
    ++frameStats.binds.frameLightBufferUpdates;
    frameStats.binds.frameLightTotalCount =
        frameLightSet.directionalCount + frameLightSet.localCount;
    frameStats.binds.frameDirectionalLightCount = frameLightSet.directionalCount;
    frameStats.binds.frameLocalLightCount = frameLightSet.localCount;
    frameStats.binds.frameRectLightCount = frameLightSet.rectCount;
    frameStats.binds.frameLightTileSize = lightTileStats.tileSize;
    frameStats.binds.frameLightTileCountX = lightTileStats.tileCountX;
    frameStats.binds.frameLightTileCountY = lightTileStats.tileCountY;
    frameStats.binds.frameLightTileCount = lightTileStats.tileCount;
    frameStats.binds.frameLightTileAssignments = lightTileStats.assignments;
    frameStats.binds.frameLightTileAssignmentCapacity =
        lightTileStats.assignmentCapacity;
    frameStats.binds.frameLightTileOverflowAssignments =
        lightTileStats.overflowAssignments;
    frameStats.binds.frameLightTileOverflowCapacity =
        lightTileStats.overflowCapacity;
    frameStats.binds.frameLightTileOverflowTiles =
        lightTileStats.overflowTileCount;
    frameStats.binds.frameLightTileOverflowDropped =
        lightTileStats.overflowDropped;
    frameStats.binds.frameLightTileAssignmentFallbacks =
        lightTileStats.fallbackCount;
    frameStats.binds.frameLightTileGpuReadbackValid =
        lightTileGpuStats.valid ? 1u : 0u;
    frameStats.binds.frameLightTileGpuSaturatedTiles =
        lightTileGpuStats.saturatedTileCount;
    frameStats.binds.frameLightTileGpuMaxCandidates =
        lightTileGpuStats.maxRawCandidateCount;
    frameStats.binds.frameLightTileGpuRawCandidates =
        lightTileGpuStats.rawCandidateCountSum;
    frameStats.binds.frameLightTileGpuOverflowTiles =
        lightTileGpuStats.overflowUsedTileCount;
    frameStats.binds.frameLightTileGpuOverflowDroppedTiles =
        lightTileGpuStats.overflowDroppedTileCount;
    frameStats.binds.frameLightTileGpuOverflowStored =
        lightTileGpuStats.overflowStoredCount;
    frameStats.binds.frameLightTileGpuOverflowDropped =
        lightTileGpuStats.overflowDroppedCount;
    if (m_MaterialBuffer != nullptr) {
        ++frameStats.binds.frameMaterialBufferUpdates;
    }
    if (m_DirectionalShadowCascadeBuffer != nullptr) {
        ++frameStats.binds.shadowCascadeBufferUpdates;
    }
    if (m_LocalShadowBuffer != nullptr) {
        ++frameStats.binds.localShadowBufferUpdates;
    }
    frameStats.binds.localShadowResolveEnabled =
        m_LocalShadowAtlas != nullptr &&
        m_LocalShadowBuffer != nullptr &&
        localShadowTiles.assignedCount > 0 &&
        shadowSamplingEnabled
            ? 1u
            : 0u;
    frameStats.binds.frameMaterialCount = frameMaterialSet.count;
    frameStats.binds.frameMaterialCapacity = static_cast<u32>(kMaxFrameMaterials);
    frameStats.binds.frameMaterialOverflowCount = frameMaterialSet.overflowCount;
    frameStats.binds.frameMaterialOpaqueCount = frameMaterialSet.opaqueCount;
    frameStats.binds.frameMaterialTransparentCount = frameMaterialSet.transparentCount;
    frameStats.binds.frameMaterialForwardSpecialCount = frameMaterialSet.forwardSpecialCount;
    frameStats.binds.frameMaterialEmissiveHintCount = frameMaterialSet.emissiveHintCount;
    frameStats.binds.frameMaterialSpecularHintCount = frameMaterialSet.specularHintCount;
    frameStats.binds.frameMaterialSpecularTextureCount =
        frameMaterialSet.specularTextureCount;
    frameStats.binds.frameMaterialAlphaMaskCount = frameMaterialSet.alphaMaskCount;
    frameStats.binds.frameMaterialAlphaBlendCount = frameMaterialSet.alphaBlendCount;
    frameStats.binds.frameMaterialUvTransformCount = frameMaterialSet.uvTransformCount;
    frameStats.binds.frameMaterialDoubleSidedCount = frameMaterialSet.doubleSidedCount;
    frameStats.binds.frameMaterialClearcoatCount = frameMaterialSet.clearcoatCount;
    frameStats.binds.frameMaterialClearcoatTextureCount =
        frameMaterialSet.clearcoatTextureCount;
    frameStats.binds.frameMaterialClearcoatRoughnessTextureCount =
        frameMaterialSet.clearcoatRoughnessTextureCount;
    frameStats.binds.frameMaterialTransmissionCount = frameMaterialSet.transmissionCount;
    frameStats.binds.frameMaterialTransmissionTextureCount =
        frameMaterialSet.transmissionTextureCount;
    frameStats.binds.frameMaterialVolumeCount = frameMaterialSet.volumeCount;
    frameStats.binds.frameMaterialOpacityTextureCount =
        frameMaterialSet.opacityTextureCount;
    frameStats.binds.frameMaterialTexturedCount = frameMaterialSet.texturedCount;
    frameStats.draw.overlayBoundsCacheHits = overlayCacheStats.boundsCacheHits;
    frameStats.draw.overlayBoundsCacheMisses = overlayCacheStats.boundsCacheMisses;
    frameStats.draw.overlayCommandCacheHits = overlayCacheStats.commandCacheHits;
    frameStats.draw.overlayCommandCacheMisses = overlayCacheStats.commandCacheMisses;
    frameStats.draw.overlayVisibilityCacheHits = overlayCacheStats.visibilityCacheHits;
    frameStats.draw.overlayVisibilityCacheMisses = overlayCacheStats.visibilityCacheMisses;
    frameStats.draw.overlayQueueCacheHits = overlayCacheStats.queueCacheHits;
    frameStats.draw.overlayQueueCacheMisses = overlayCacheStats.queueCacheMisses;
    frameStats.draw.mainInstancedDraws =
        static_cast<u32>(m_MainInstanceBatches.size());
    frameStats.draw.mainInstancedInstances =
        static_cast<u32>(m_MainInstances.size());
    frameStats.draw.matrixRecalculations = TransformMatrixRecalculationCount();
    const RenderFeatureFrameGraphAppendData renderFeatureFrameGraphAppendData{
        &m_RenderFeatures,
        &renderFeatureContext,
        &frameStats
    };
    frameStats.frameGraph = BuildCurrentVulkanFrameGraphPlan(
        CurrentVulkanFrameGraphInputs{
            shadowPassEnabled && !m_ShadowRenderQueue.Empty(),
            m_OverlayScene3D != nullptr && !overlayCommands.empty(),
            m_ImGuiLayer != nullptr,
            m_FrameMatricesProvider || m_ImGuiScene3D != nullptr || m_OverlayScene3D != nullptr,
            has3DMainPass || m_OverlayScene3D != nullptr,
            m_Swapchain->ImageFormat(),
            m_DepthBuffer->Format(),
            m_Swapchain->Extent(),
            static_cast<u32>(m_Swapchain->Images().size()),
            m_ShadowMap != nullptr ? m_ShadowMap->Extent().width : 0,
            m_DirectionalShadowCascadeAtlas != nullptr
                ? m_DirectionalShadowCascadeAtlas->Extent().width
                : 0,
            m_DirectionalShadowCascadeAtlas != nullptr
                ? m_DirectionalShadowCascadeAtlas->Extent().height
                : 0,
            m_DirectionalShadowCascadeAtlas != nullptr
                ? m_DirectionalShadowCascadeAtlas->TileSize()
                : 0,
            m_DirectionalShadowCascadeAtlas != nullptr
                ? m_DirectionalShadowCascadeAtlas->CascadeCapacity()
                : 0,
            m_LocalShadowAtlas != nullptr
                ? m_LocalShadowAtlas->Extent().width
                : 0,
            m_LocalShadowAtlas != nullptr
                ? m_LocalShadowAtlas->Extent().height
                : 0,
            m_LocalShadowAtlas != nullptr
                ? m_LocalShadowAtlas->TileSize()
                : 0,
            m_LocalShadowAtlas != nullptr
                ? m_LocalShadowAtlas->TileCapacity()
                : 0,
            m_LocalShadowAtlas != nullptr
                ? frameStats.localShadowAtlas.assignedTiles
                : 0,
            m_DirectionalShadowCascadeAtlas != nullptr
                ? directionalShadowCascades.activeCount
                : 0,
            directionalShadowCascades.activeCount,
            directionalShadowCascades.activeCount > 0,
            m_SceneRenderTargets != nullptr,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->HdrSceneColorFormat()
                : VK_FORMAT_UNDEFINED,
            m_HdrRenderPass != nullptr && m_HdrFramebuffer != nullptr,
            recordBloomPyramid,
            m_BloomPyramid != nullptr
                ? m_BloomPyramid->BloomFormat()
                : VK_FORMAT_UNDEFINED,
            m_BloomPyramid != nullptr ? m_BloomPyramid->MipCount() : 0,
            frameStats.postProcess.colorGradingLutEnabled > 0,
            m_ColorGradingLut != nullptr
                ? m_ColorGradingLut->Format()
                : VK_FORMAT_UNDEFINED,
            colorGradingLutReady ? m_ColorGradingLut->LutSize() : 0,
            frameStats.ibl.brdfLutAllocated > 0,
            frameStats.ibl.brdfLutFormat,
            frameStats.ibl.brdfLutSize,
            frameStats.ibl.irradianceMapAllocated > 0,
            frameStats.ibl.irradianceFormat,
            frameStats.ibl.irradianceFaceSize,
            frameStats.ibl.prefilteredMapAllocated > 0,
            frameStats.ibl.prefilteredFormat,
            frameStats.ibl.prefilteredFaceSize,
            frameStats.ibl.prefilteredMipCount,
            frameReflectionProbes.activeLocalProbeCount > 0 &&
                frameReflectionProbes.localProbe.sceneOwned,
            frameReflectionProbes.sceneProbeCount,
            frameReflectionProbes.activeLocalProbeCount > 0 &&
                frameReflectionProbes.localProbe.sceneOwned,
            frameReflectionProbes.activeLocalProbeCount > 0,
            frameStats.reflectionProbe.captureSourceType,
            frameStats.reflectionProbe.captureFallbackReason,
            frameStats.reflectionProbe.captureResourceReady > 0,
            frameStats.reflectionProbe.localCubemapFormat,
            frameStats.reflectionProbe.localCubemapFaceSize,
            frameStats.reflectionProbe.localCubemapMipCount,
            frameStats.postProcess.autoExposureHistogramEnabled > 0,
            frameStats.postProcess.autoExposureHistogramEnabled > 0 &&
                m_AutoExposureBuffer != nullptr,
            m_DeferredLightingPipeline != nullptr && m_GBufferDescriptorSets != nullptr,
            AppendRenderFeaturesToCurrentFrameGraph,
            &renderFeatureFrameGraphAppendData,
            has3DMainPass && m_LightTileCullComputePipeline != nullptr,
            gBufferDebugView >= 0 && m_GBufferDebugPipeline != nullptr && m_GBufferDescriptorSets != nullptr,
            frameStats.weightedTranslucency.allocated > 0,
            frameStats.weightedTranslucency.accumFormat,
            frameStats.weightedTranslucency.revealageFormat,
            frameStats.weightedTranslucency.renderPassAllocated > 0,
            frameStats.weightedTranslucency.framebufferCount,
            m_SceneRenderTargets != nullptr,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->SceneDepthFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->VelocityFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->GBufferAlbedoFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->GBufferNormalRoughnessFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->GBufferMaterialFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->GBufferEmissiveFormat()
                : VK_FORMAT_UNDEFINED,
            m_GBufferRenderPass != nullptr && m_GBufferFramebuffer != nullptr,
            has3DMainPass && m_GBufferGraphicsPipeline != nullptr,
            frameReflectionProbes.activeLocalProbeCount > 0 &&
                frameReflectionProbes.localProbe.sceneOwned,
            frameStats.reflectionProbe.selectedCaptureSlotCount,
            frameStats.reflectionProbe.selectedCaptureResourceReadyCount,
            frameStats.reflectionProbe.selectedCaptureFallbackCount
        }
    );

    constexpr u32 kLightTileCullComputeLocalSizeX = 8;
    constexpr u32 kLightTileCullComputeLocalSizeY = 8;
    const bool recordLightTileCullCompute =
        has3DMainPass &&
        m_LightTileCullComputePipeline != nullptr &&
        lightTileStats.tileCountX > 0 &&
        lightTileStats.tileCountY > 0;
    const u32 lightTileCullGroupCountX = recordLightTileCullCompute
        ? (lightTileStats.tileCountX + kLightTileCullComputeLocalSizeX - 1) /
            kLightTileCullComputeLocalSizeX
        : 0;
    const u32 lightTileCullGroupCountY = recordLightTileCullCompute
        ? (lightTileStats.tileCountY + kLightTileCullComputeLocalSizeY - 1) /
            kLightTileCullComputeLocalSizeY
        : 0;

    m_CommandBuffer->Record(
        imageIndex,
        *m_RenderPass,
        *m_GraphicsPipeline,
        m_DoubleSidedGraphicsPipeline.get(),
        *m_DescriptorSets,
        *m_MaterialDescriptorSets,
        m_RenderQueue.Commands(),
        *m_Framebuffer,
        m_DepthLoadRenderPass.get(),
        m_DepthLoadFramebuffer.get(),
        *m_Swapchain,
        m_ImGuiLayer.get(),
        m_ShadowRenderPass.get(),
        m_ShadowGraphicsPipeline.get(),
        m_DoubleSidedShadowGraphicsPipeline.get(),
        m_ShadowFramebuffer.get(),
        ShadowDescriptorSets(),
        shadowCommands,
        m_DirectionalShadowCascadeFramebuffer.get(),
        &directionalShadowCascades,
        m_LocalShadowFramebuffer.get(),
        &localShadowTiles,
        localShadowTiles.cacheSkippedTiles == localShadowTiles.assignedCount &&
            localShadowTiles.assignedCount > 0,
        m_HdrRenderPass.get(),
        m_HdrFramebuffer.get(),
        m_DeferredLightingPipeline.get(),
        m_DescriptorSets.get(),
        m_GBufferDescriptorSets.get(),
        deferredPbrDebugView,
        m_HdrCompositePipeline.get(),
        m_HdrDescriptorSets.get(),
        m_BloomDownsampleRenderPass.get(),
        m_BloomUpsampleRenderPass.get(),
        m_BloomDownsampleFramebuffer.get(),
        m_BloomUpsampleFramebuffer.get(),
        recordBloomPyramid ? m_BloomDownsamplePipeline.get() : nullptr,
        recordBloomPyramid ? m_BloomUpsamplePipeline.get() : nullptr,
        recordBloomPyramid ? m_BloomDescriptorSets.get() : nullptr,
        recordBloomPyramid,
        showDeferredHdr,
        m_RenderDebugSettings.forwardView == ForwardDebugView::Bloom,
        m_RenderDebugSettings.forwardView == ForwardDebugView::ToneMapping,
        m_RenderDebugSettings.forwardView == ForwardDebugView::AutoExposure,
        m_RenderDebugSettings.forwardView == ForwardDebugView::ColorGrading,
        m_RenderDebugSettings.forwardView == ForwardDebugView::Sharpening,
        m_GBufferDebugPipeline.get(),
        m_GBufferDescriptorSets.get(),
        gBufferDebugView,
        has3DMainPass ? m_DepthPrefillGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedDepthPrefillGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_SceneRenderTargets.get() : nullptr,
        has3DMainPass ? m_DepthBuffer.get() : nullptr,
        m_GBufferRenderPass.get(),
        m_GBufferFramebuffer.get(),
        m_WeightedTranslucencyRenderPass.get(),
        m_WeightedTranslucencyFramebuffer.get(),
        has3DMainPass ? m_WeightedTranslucencyGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedWeightedTranslucencyGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_WeightedTranslucencyResolvePipeline.get() : nullptr,
        m_WeightedTranslucencyDescriptorSets.get(),
        has3DMainPass
            ? std::span<const RenderCommand>(
                weightedTranslucencyCommands.data(),
                weightedTranslucencyCommands.size()
            )
            : std::span<const RenderCommand>{},
        weightedTranslucencyDebugView,
        has3DMainPass ? m_GBufferGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedGBufferGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DescriptorSets.get() : nullptr,
        has3DMainPass ? std::span<const RenderCommand>(gBufferCommands.data(), gBufferCommands.size()) : std::span<const RenderCommand>{},
        recordLightTileCullCompute ? m_LightTileCullComputePipeline.get() : nullptr,
        recordLightTileCullCompute ? m_DescriptorSets.get() : nullptr,
        lightTileCullGroupCountX,
        lightTileCullGroupCountY,
        4, // lightTileCullGroupCountZ (clustered: 4 depth slices)
        m_LightClusterCullComputePipeline.get(),
        recordAutoExposureCompute ? m_AutoExposureComputePipeline.get() : nullptr,
        recordAutoExposureCompute ? m_DescriptorSets.get() : nullptr,
        recordAutoExposureCompute ? m_HdrDescriptorSets.get() : nullptr,
        recordAutoExposureCompute,
        has3DMainPass ? m_ForwardResidualGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedForwardResidualGraphicsPipeline.get() : nullptr,
        has3DMainPass ? std::span<const RenderCommand>(forwardResidualCommands.data(), forwardResidualCommands.size()) : std::span<const RenderCommand>{},
        &frameMaterialSet,
        m_OverlayGraphicsPipeline.get(),
        m_OverlayDescriptorSets.get(),
        overlayCommands,
        m_InstancedGraphicsPipeline.get(),
        m_DoubleSidedInstancedGraphicsPipeline.get(),
        m_InstanceBuffer.get(),
        m_MainInstanceBatches,
        m_GpuTimer.get(),
        &frameStats.binds,
        &frameStats.frameGraph
    );
    frameStats.localShadowAtlas.recordedTilePasses =
        frameStats.binds.localShadowAtlasPasses;
    frameStats.localShadowAtlas.recordedDraws =
        frameStats.binds.localShadowAtlasDraws;
    frameStats.localShadowAtlas.recordedMeshBinds =
        frameStats.binds.localShadowAtlasMeshBinds;
    frameStats.weightedTranslucency.clearPasses =
        frameStats.binds.weightedTranslucencyClearPasses;
    frameStats.weightedTranslucency.draws =
        frameStats.binds.weightedTranslucencyDraws;
    frameStats.weightedTranslucency.sharedLightListDraws =
        frameStats.binds.weightedTranslucencySharedLightListDraws;
    frameStats.weightedTranslucency.shadowReadyDraws =
        frameStats.binds.weightedTranslucencyShadowReadyDraws;
    frameStats.weightedTranslucency.resolveDraws =
        frameStats.binds.weightedTranslucencyResolveDraws;
    if (imageIndex < m_LocalShadowCacheStates.size()) {
        LocalShadowCacheState& cacheState = m_LocalShadowCacheStates[imageIndex];
        cacheState.tileKeys = localShadowTiles.cacheKeys;
        cacheState.tileCount = localShadowTiles.assignedCount;
        cacheState.valid = localShadowTiles.assignedCount > 0;
    }
    if (imageIndex < m_LightTileGpuReadbackReady.size()) {
        m_LightTileGpuReadbackReady[imageIndex] = recordLightTileCullCompute;
    }

    sectionEnd = FrameClock::now();
    frameStats.cpu.commandRecordMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    const VkSemaphore waitSemaphores[] = {
        imageAvailableSemaphore
    };

    const VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    const VkCommandBuffer commandBuffers[] = {
        m_CommandBuffer->Handle(imageIndex)
    };

    const VkSemaphore signalSemaphores[] = {
        renderFinishedSemaphore
    };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandBuffers;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_Device.GraphicsQueue(), 1, &submitInfo, currentFrameFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit Vulkan draw command buffer");
    }
    if (m_GpuTimer != nullptr) {
        m_GpuTimer->MarkFrameSubmitted(imageIndex);
    }

    const VkSwapchainKHR swapchains[] = {
        m_Swapchain->Handle()
    };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(m_Device.PresentQueue(), &presentInfo);

    sectionEnd = FrameClock::now();
    frameStats.cpu.submitPresentMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    frameStats.cpu.totalFrameMs = ElapsedMilliseconds(frameStart, sectionEnd);
    m_LastStats = frameStats;

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        m_Window.WasResized()) {
        m_Window.ResetResizedFlag();
        RecreateSwapchain();
        return;
    }

    if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present Vulkan swapchain image");
    }

    m_CurrentFrame = (m_CurrentFrame + 1) % VulkanSyncObjects::kMaxFramesInFlight;
}

void VulkanRenderer::WaitIdle() const {
    vkDeviceWaitIdle(m_Device.Handle());
}

void VulkanRenderer::SetFrameMatricesProvider(FrameMatricesProvider provider) {
    m_FrameMatricesProvider = std::move(provider);
}

void VulkanRenderer::SetRenderQueueBuilder(RenderQueueBuilder builder) {
    m_RenderQueueBuilder = std::move(builder);
}

void VulkanRenderer::SetImGui3DContext(Scene3D* scene, Camera3D* camera) {
    m_MainScene3D = scene;
    m_ImGuiScene3D = scene;
    m_ImGuiCamera3D = camera;
}

void VulkanRenderer::SetOverlay3DContext(
    Scene3D* scene,
    Camera3D* camera,
    PipelineSpec pipelineSpec
) {
    m_OverlayScene3D = scene;
    m_OverlayCamera3D = camera;
    m_OverlayPipelineSpec = std::move(pipelineSpec);

    if (m_Swapchain == nullptr) {
        return;
    }

    WaitIdle();
    m_OverlayUniformBuffer = std::make_unique<VulkanUniformBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_OverlayDescriptorSets = std::make_unique<VulkanDescriptorSets>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_OverlayUniformBuffer,
        *m_LightBuffer,
        *m_LightTileDiagnosticsBuffer,
        *m_MaterialBuffer,
        *m_DirectionalShadowCascadeBuffer,
        *m_LocalShadowBuffer,
        *m_AutoExposureBuffer
    );
    m_ReflectionProbeResources.SetDescriptorSetsBound(
        UpdateEnvironmentDescriptorSets(m_DescriptorSets.get()) +
            UpdateEnvironmentDescriptorSets(m_OverlayDescriptorSets.get())
    );
    m_OverlayGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        *m_OverlayPipelineSpec
    );
}

void VulkanRenderer::RefreshMaterialDescriptors() {
    WaitIdle();
    std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
    m_MaterialDescriptorSets->Recreate(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        materials,
        m_ShadowMap.get(),
        m_DirectionalShadowCascadeAtlas.get(),
        m_LocalShadowAtlas.get()
    );
}

VulkanRenderDebugSettings& VulkanRenderer::RenderDebugSettings() {
    return m_RenderDebugSettings;
}

const VulkanRenderDebugSettings& VulkanRenderer::RenderDebugSettings() const {
    return m_RenderDebugSettings;
}

const RendererStats& VulkanRenderer::Stats() const {
    return m_LastStats;
}

VulkanShadowSettings& VulkanRenderer::ShadowSettings() {
    return m_ShadowSettings;
}

const VulkanShadowSettings& VulkanRenderer::ShadowSettings() const {
    return m_ShadowSettings;
}

void VulkanRenderer::ValidateSceneResources() const {
    if (m_Scene == nullptr) {
        return;
    }

    for (const Renderable2D* renderable : m_Scene->Renderables()) {
        SE_ASSERT(renderable != nullptr, "Scene contains a null renderable");
        SE_ASSERT(
            m_RenderResources.ContainsMesh(renderable->MeshId()),
            "Renderable2D references a mesh id that is not registered"
        );
        SE_ASSERT(
            m_RenderResources.ContainsMaterial(renderable->MaterialId()),
            "Renderable2D references a material id that is not registered"
        );
    }
}

void VulkanRenderer::CreateSwapchainResources() {
    m_Swapchain = std::make_unique<VulkanSwapchain>(m_Window, m_PhysicalDevice, m_Device, m_Surface);
    m_DescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device);
    m_MaterialDescriptorSetLayout = std::make_unique<VulkanMaterialDescriptorSetLayout>(m_Device);
    m_UniformBuffer = std::make_unique<VulkanUniformBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_LightBuffer = std::make_unique<VulkanLightBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_LightTileDiagnosticsBuffer = std::make_unique<VulkanLightTileDiagnosticsBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_AutoExposureBuffer = std::make_unique<VulkanAutoExposureBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_LightTileGpuReadbackReady.assign(m_Swapchain->Images().size(), false);
    m_MaterialBuffer = std::make_unique<VulkanMaterialBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_DirectionalShadowCascadeBuffer =
        std::make_unique<VulkanDirectionalShadowCascadeBuffer>(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    m_LocalShadowBuffer = std::make_unique<VulkanLocalShadowBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    GenerateIblTextures(m_Device, m_PhysicalDevice, m_CommandPool,
        m_IblBrdfImage, m_IblIrradianceImage, m_IblPrefilteredImage,
        m_IblIrradianceView, m_IblPrefilteredView, m_IblSampler);
    m_ReflectionProbeResources.CreateBuiltInProcedural(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool
    );
    m_DescriptorSets = std::make_unique<VulkanDescriptorSets>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_UniformBuffer,
        *m_LightBuffer,
        *m_LightTileDiagnosticsBuffer,
        *m_MaterialBuffer,
        *m_DirectionalShadowCascadeBuffer,
        *m_LocalShadowBuffer,
        *m_AutoExposureBuffer
    );
    m_ReflectionProbeResources.SetDescriptorSetsBound(
        UpdateEnvironmentDescriptorSets(m_DescriptorSets.get()) +
            UpdateEnvironmentDescriptorSets(m_OverlayDescriptorSets.get())
    );
    std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
    m_DepthBuffer = std::make_unique<VulkanDepthBuffer>(m_Device, m_PhysicalDevice, *m_Swapchain);
    m_SceneRenderTargets = std::make_unique<VulkanSceneRenderTargets>(
        m_Device,
        m_PhysicalDevice,
        *m_Swapchain
    );
    m_BloomPyramid = std::make_unique<VulkanBloomPyramid>(
        m_Device,
        m_PhysicalDevice,
        *m_Swapchain
    );
    m_ColorGradingLut = std::make_unique<VulkanColorGradingLut>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool
    );
    m_SceneTargetSampler = std::make_unique<VulkanSampler>(
        m_Device,
        m_PhysicalDevice,
        1
    );
    m_HdrRenderPass = std::make_unique<VulkanHdrRenderPass>(
        m_Device,
        m_SceneRenderTargets->HdrSceneColorFormat()
    );
    m_BloomDownsampleRenderPass = std::make_unique<VulkanBloomRenderPass>(
        m_Device,
        m_BloomPyramid->BloomFormat(),
        false
    );
    m_BloomUpsampleRenderPass = std::make_unique<VulkanBloomRenderPass>(
        m_Device,
        m_BloomPyramid->BloomFormat(),
        true
    );
    m_WeightedTranslucencyRenderPass =
        std::make_unique<VulkanWeightedTranslucencyRenderPass>(
            m_Device,
            *m_SceneRenderTargets
        );
    m_GBufferRenderPass = std::make_unique<VulkanGBufferRenderPass>(
        m_Device,
        *m_SceneRenderTargets
    );
    m_HdrFramebuffer = std::make_unique<VulkanHdrFramebuffer>(
        m_Device,
        *m_HdrRenderPass,
        *m_SceneRenderTargets
    );
    m_BloomDownsampleFramebuffer = std::make_unique<VulkanBloomFramebuffer>(
        m_Device,
        *m_BloomDownsampleRenderPass,
        *m_BloomPyramid
    );
    m_BloomUpsampleFramebuffer = std::make_unique<VulkanBloomFramebuffer>(
        m_Device,
        *m_BloomUpsampleRenderPass,
        *m_BloomPyramid
    );
    m_WeightedTranslucencyFramebuffer =
        std::make_unique<VulkanWeightedTranslucencyFramebuffer>(
            m_Device,
            *m_WeightedTranslucencyRenderPass,
            *m_SceneRenderTargets
        );
    m_GBufferFramebuffer = std::make_unique<VulkanGBufferFramebuffer>(
        m_Device,
        *m_GBufferRenderPass,
        *m_SceneRenderTargets
    );
    m_ShadowMap = std::make_unique<VulkanShadowMap>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        m_Swapchain->Images().size(),
        m_ShadowSettings.mapSize
    );
    m_DirectionalShadowCascadeAtlas =
        std::make_unique<VulkanDirectionalShadowCascadeAtlas>(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            m_ShadowSettings.mapSize
        );
    m_LocalShadowAtlas = std::make_unique<VulkanLocalShadowAtlas>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        m_Swapchain->Images().size(),
        LocalShadowAtlasTileSizeFor(m_ShadowSettings),
        LocalShadowAtlasTileCapacityFor(m_ShadowSettings)
    );
    ResetLocalShadowCacheStates();
    m_ShadowRenderPass = std::make_unique<VulkanShadowRenderPass>(
        m_Device,
        *m_ShadowMap
    );
    m_ShadowFramebuffer = std::make_unique<VulkanShadowFramebuffer>(
        m_Device,
        *m_ShadowRenderPass,
        *m_ShadowMap
    );
    m_DirectionalShadowCascadeFramebuffer =
        std::make_unique<VulkanShadowFramebuffer>(
            m_Device,
            *m_ShadowRenderPass,
            *m_DirectionalShadowCascadeAtlas
        );
    m_LocalShadowFramebuffer =
        std::make_unique<VulkanShadowFramebuffer>(
            m_Device,
            *m_ShadowRenderPass,
            *m_LocalShadowAtlas
        );
    m_MaterialDescriptorSets = std::make_unique<VulkanMaterialDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        materials,
        m_ShadowMap.get(),
        m_DirectionalShadowCascadeAtlas.get(),
        m_LocalShadowAtlas.get()
    );
    m_GBufferDescriptorSets = std::make_unique<VulkanGBufferDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        *m_SceneRenderTargets,
        *m_SceneTargetSampler,
        m_ShadowMap.get(),
        m_DirectionalShadowCascadeAtlas.get(),
        m_LocalShadowAtlas.get()
    );
    m_HdrDescriptorSets = std::make_unique<VulkanHdrDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        *m_SceneRenderTargets,
        m_BloomPyramid.get(),
        m_ColorGradingLut.get(),
        *m_SceneTargetSampler
    );
    m_BloomDescriptorSets = std::make_unique<VulkanBloomDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        *m_SceneRenderTargets,
        *m_BloomPyramid,
        *m_SceneTargetSampler
    );
    m_WeightedTranslucencyDescriptorSets =
        std::make_unique<VulkanWeightedTranslucencyDescriptorSets>(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SceneTargetSampler
        );
    m_RenderPass = std::make_unique<VulkanRenderPass>(m_Device, *m_Swapchain, *m_DepthBuffer);
    m_DepthLoadRenderPass = std::make_unique<VulkanRenderPass>(
        m_Device,
        *m_Swapchain,
        *m_DepthBuffer,
        true
    );
    m_ImGuiLayer = std::make_unique<VulkanImGuiLayer>(
        m_Window,
        m_Instance,
        m_PhysicalDevice,
        m_Device,
        *m_RenderPass,
        *m_Swapchain
    );
    m_GraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        m_PipelineSpec
    );
    m_DoubleSidedGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        PipelineSpec::DoubleSided(m_PipelineSpec)
    );
    if (m_PipelineSpec.vertexLayout == VertexLayout::Vertex3D ||
        m_PipelineSpec.vertexLayout == VertexLayout::Vertex3DInstanced) {
        const std::string depthPrefillShaderPath =
            std::string(SE_SHADER_DIR) + "/depth_prefill_3d.vert.spv";
        const PipelineSpec depthPrefillSpec =
            PipelineSpec::DepthPrefill3D(depthPrefillShaderPath);
        m_DepthPrefillGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            depthPrefillSpec
        );
        m_DoubleSidedDepthPrefillGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::DoubleSided(depthPrefillSpec)
        );
        const std::string weightedTranslucencyFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/weighted_translucency_3d.frag.spv";
        const PipelineSpec weightedTranslucencySpec =
            PipelineSpec::WeightedTranslucency3D(
                m_PipelineSpec.vertexShaderPath,
                weightedTranslucencyFragmentShaderPath
            );
        m_WeightedTranslucencyGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_WeightedTranslucencyRenderPass->Handle(),
                *m_Swapchain,
                weightedTranslucencySpec
            );
        m_DoubleSidedWeightedTranslucencyGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_WeightedTranslucencyRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(weightedTranslucencySpec)
            );
        const PipelineSpec forwardResidualSpec =
            PipelineSpec::ForwardResidual3D(
                m_PipelineSpec.vertexShaderPath,
                m_PipelineSpec.fragmentShaderPath
            );
        m_ForwardResidualGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            forwardResidualSpec
        );
        m_DoubleSidedForwardResidualGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::DoubleSided(forwardResidualSpec)
        );
    } else {
        m_DepthPrefillGraphicsPipeline.reset();
        m_DoubleSidedDepthPrefillGraphicsPipeline.reset();
        m_WeightedTranslucencyGraphicsPipeline.reset();
        m_DoubleSidedWeightedTranslucencyGraphicsPipeline.reset();
        m_ForwardResidualGraphicsPipeline.reset();
        m_DoubleSidedForwardResidualGraphicsPipeline.reset();
    }
    const std::string hdrCompositeVertexShaderPath =
        std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
    const std::string hdrCompositeFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/hdr_composite.frag.spv";
    const std::string bloomDownsampleFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/bloom_downsample.frag.spv";
    const std::string bloomUpsampleFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/bloom_upsample.frag.spv";
    m_BloomDownsamplePipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_BloomDownsampleRenderPass->Handle(),
        *m_Swapchain,
        PipelineSpec::BloomPyramid(
            hdrCompositeVertexShaderPath,
            bloomDownsampleFragmentShaderPath
        )
    );
    m_BloomUpsamplePipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_BloomUpsampleRenderPass->Handle(),
        *m_Swapchain,
        PipelineSpec::BloomUpsample(
            hdrCompositeVertexShaderPath,
            bloomUpsampleFragmentShaderPath
        )
    );
    m_HdrCompositePipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        PipelineSpec::HdrComposite(
            hdrCompositeVertexShaderPath,
            hdrCompositeFragmentShaderPath
        )
    );
    const std::string weightedTranslucencyResolveFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/weighted_translucency_resolve.frag.spv";
    m_WeightedTranslucencyResolvePipeline =
        std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_HdrRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::WeightedTranslucencyResolve(
                hdrCompositeVertexShaderPath,
                weightedTranslucencyResolveFragmentShaderPath
            )
        );
    const std::string gBufferDebugVertexShaderPath =
        std::string(SE_SHADER_DIR) + "/gbuffer_debug.vert.spv";
    const std::string gBufferDebugFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/gbuffer_debug.frag.spv";
    m_GBufferDebugPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        PipelineSpec::GBufferDebug(
            gBufferDebugVertexShaderPath,
            gBufferDebugFragmentShaderPath
        )
    );
    if (m_PipelineSpec.supportsInstancing && !m_PipelineSpec.instancedVertexShaderPath.empty()) {
        PipelineSpec instancedSpec = m_PipelineSpec;
        instancedSpec.vertexShaderPath = m_PipelineSpec.instancedVertexShaderPath;
        instancedSpec.vertexLayout = VertexLayout::Vertex3DInstanced;
        m_InstancedGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            instancedSpec
        );
        m_DoubleSidedInstancedGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::DoubleSided(instancedSpec)
        );
        m_InstanceBuffer = std::make_unique<VulkanInstanceBuffer>(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
        m_MainInstanceUploadSignatures.clear();
    } else {
        m_InstancedGraphicsPipeline.reset();
        m_DoubleSidedInstancedGraphicsPipeline.reset();
        m_InstanceBuffer.reset();
        m_MainInstanceUploadSignatures.clear();
    }
    if (m_PipelineSpec.vertexLayout == VertexLayout::Vertex3D ||
        m_PipelineSpec.vertexLayout == VertexLayout::Vertex3DInstanced) {
        const std::string gBufferVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_3d.vert.spv";
        const std::string gBufferFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_3d.frag.spv";
        const PipelineSpec gBufferSpec =
            PipelineSpec::GBuffer3D(
                gBufferVertexShaderPath,
                gBufferFragmentShaderPath
            );
        m_GBufferGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_GBufferRenderPass->Handle(),
            *m_Swapchain,
            gBufferSpec
        );
        m_DoubleSidedGBufferGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_GBufferRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::DoubleSided(gBufferSpec)
        );
    } else {
        m_GBufferGraphicsPipeline.reset();
        m_DoubleSidedGBufferGraphicsPipeline.reset();
    }
    const std::string deferredLightingVertexShaderPath =
        std::string(SE_SHADER_DIR) + "/deferred_lighting.vert.spv";
    const std::string deferredLightingFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/deferred_lighting.frag.spv";
    m_DeferredLightingPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_HdrRenderPass->Handle(),
        *m_Swapchain,
        PipelineSpec::DeferredLighting(
            deferredLightingVertexShaderPath,
            deferredLightingFragmentShaderPath
        )
    );
    if (m_PipelineSpec.vertexLayout == VertexLayout::Vertex3D ||
        m_PipelineSpec.vertexLayout == VertexLayout::Vertex3DInstanced) {
        const std::string lightTileCullShaderPath =
            std::string(SE_SHADER_DIR) + "/light_tile_cull.comp.spv";
        m_LightTileCullComputePipeline = std::make_unique<VulkanComputePipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            lightTileCullShaderPath
        );
        m_LightClusterCullComputePipeline = std::make_unique<VulkanComputePipeline>(
            m_Device, *m_DescriptorSetLayout,
            std::string(SE_SHADER_DIR) + "/light_cluster_cull.comp.spv");
    } else {
        m_LightTileCullComputePipeline.reset();
        m_LightClusterCullComputePipeline.reset();
    }
    m_AutoExposureComputePipeline = std::make_unique<VulkanComputePipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        std::string(SE_SHADER_DIR) + "/auto_exposure_histogram.comp.spv"
    );
    const std::string shadowShaderPath = std::string(SE_SHADER_DIR) + "/shadow_depth.vert.spv";
    const PipelineSpec shadowSpec =
        PipelineSpec::ShadowDepth(shadowShaderPath, m_ShadowMap->Extent());
    m_ShadowGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_ShadowRenderPass->Handle(),
        *m_Swapchain,
        shadowSpec
    );
    m_DoubleSidedShadowGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_ShadowRenderPass->Handle(),
        *m_Swapchain,
        PipelineSpec::DoubleSided(shadowSpec)
    );
    m_Framebuffer = std::make_unique<VulkanFramebuffer>(
        m_Device,
        *m_RenderPass,
        *m_DepthBuffer,
        *m_Swapchain
    );
    m_DepthLoadFramebuffer = std::make_unique<VulkanFramebuffer>(
        m_Device,
        *m_DepthLoadRenderPass,
        *m_DepthBuffer,
        *m_Swapchain
    );
    m_CommandBuffer = std::make_unique<VulkanCommandBuffer>(
        m_Device,
        m_CommandPool,
        *m_Framebuffer
    );
    if (GpuTimestampsEnabledFromEnvironment()) {
        m_GpuTimer = std::make_unique<VulkanGpuTimer>(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    } else {
        m_GpuTimer.reset();
    }
    m_SyncObjects = std::make_unique<VulkanSyncObjects>(m_Device, m_Swapchain->Images().size());
}

void VulkanRenderer::RecreateSwapchain() {
    m_Window.WaitForValidFramebufferSize();

    if (m_Window.ShouldClose() || m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0) {
        return;
    }

    m_Window.ResetResizedFlag();
    vkDeviceWaitIdle(m_Device.Handle());

    m_CommandBuffer->Release();
    if (m_GpuTimer != nullptr) {
        m_GpuTimer->Release();
    }
    m_Framebuffer->Release();
    if (m_DepthLoadFramebuffer != nullptr) {
        m_DepthLoadFramebuffer->Release();
    }
    if (m_OverlayGraphicsPipeline != nullptr) {
        m_OverlayGraphicsPipeline->Release();
    }
    if (m_InstancedGraphicsPipeline != nullptr) {
        m_InstancedGraphicsPipeline->Release();
    }
    if (m_DoubleSidedInstancedGraphicsPipeline != nullptr) {
        m_DoubleSidedInstancedGraphicsPipeline->Release();
    }
    if (m_GBufferDebugPipeline != nullptr) {
        m_GBufferDebugPipeline->Release();
    }
    if (m_BloomUpsamplePipeline != nullptr) {
        m_BloomUpsamplePipeline->Release();
    }
    if (m_BloomDownsamplePipeline != nullptr) {
        m_BloomDownsamplePipeline->Release();
    }
    if (m_WeightedTranslucencyResolvePipeline != nullptr) {
        m_WeightedTranslucencyResolvePipeline->Release();
    }
    if (m_DoubleSidedDepthPrefillGraphicsPipeline != nullptr) {
        m_DoubleSidedDepthPrefillGraphicsPipeline->Release();
    }
    if (m_DepthPrefillGraphicsPipeline != nullptr) {
        m_DepthPrefillGraphicsPipeline->Release();
    }
    if (m_DoubleSidedWeightedTranslucencyGraphicsPipeline != nullptr) {
        m_DoubleSidedWeightedTranslucencyGraphicsPipeline->Release();
    }
    if (m_WeightedTranslucencyGraphicsPipeline != nullptr) {
        m_WeightedTranslucencyGraphicsPipeline->Release();
    }
    if (m_DoubleSidedForwardResidualGraphicsPipeline != nullptr) {
        m_DoubleSidedForwardResidualGraphicsPipeline->Release();
    }
    if (m_ForwardResidualGraphicsPipeline != nullptr) {
        m_ForwardResidualGraphicsPipeline->Release();
    }
    if (m_HdrCompositePipeline != nullptr) {
        m_HdrCompositePipeline->Release();
    }
    if (m_DeferredLightingPipeline != nullptr) {
        m_DeferredLightingPipeline->Release();
    }
    if (m_LightTileCullComputePipeline != nullptr) {
        m_LightTileCullComputePipeline->Release();
    }
    if (m_AutoExposureComputePipeline != nullptr) {
        m_AutoExposureComputePipeline->Release();
    }
    if (m_GBufferGraphicsPipeline != nullptr) {
        m_GBufferGraphicsPipeline->Release();
    }
    if (m_DoubleSidedGBufferGraphicsPipeline != nullptr) {
        m_DoubleSidedGBufferGraphicsPipeline->Release();
    }
    if (m_ShadowGraphicsPipeline != nullptr) {
        m_ShadowGraphicsPipeline->Release();
    }
    if (m_DoubleSidedShadowGraphicsPipeline != nullptr) {
        m_DoubleSidedShadowGraphicsPipeline->Release();
    }
    m_GraphicsPipeline->Release();
    if (m_DoubleSidedGraphicsPipeline != nullptr) {
        m_DoubleSidedGraphicsPipeline->Release();
    }
    m_ImGuiLayer.reset();
    m_RenderPass->Release();
    if (m_DepthLoadRenderPass != nullptr) {
        m_DepthLoadRenderPass->Release();
    }
    if (m_GBufferFramebuffer != nullptr) {
        m_GBufferFramebuffer->Release();
    }
    if (m_GBufferRenderPass != nullptr) {
        m_GBufferRenderPass->Release();
    }
    if (m_HdrFramebuffer != nullptr) {
        m_HdrFramebuffer->Release();
    }
    if (m_HdrRenderPass != nullptr) {
        m_HdrRenderPass->Release();
    }
    if (m_BloomUpsampleFramebuffer != nullptr) {
        m_BloomUpsampleFramebuffer->Release();
    }
    if (m_BloomDownsampleFramebuffer != nullptr) {
        m_BloomDownsampleFramebuffer->Release();
    }
    if (m_BloomUpsampleRenderPass != nullptr) {
        m_BloomUpsampleRenderPass->Release();
    }
    if (m_BloomDownsampleRenderPass != nullptr) {
        m_BloomDownsampleRenderPass->Release();
    }
    if (m_WeightedTranslucencyFramebuffer != nullptr) {
        m_WeightedTranslucencyFramebuffer->Release();
    }
    if (m_WeightedTranslucencyRenderPass != nullptr) {
        m_WeightedTranslucencyRenderPass->Release();
    }
    if (m_ShadowFramebuffer != nullptr) {
        m_ShadowFramebuffer->Release();
    }
    if (m_DirectionalShadowCascadeFramebuffer != nullptr) {
        m_DirectionalShadowCascadeFramebuffer->Release();
    }
    if (m_LocalShadowFramebuffer != nullptr) {
        m_LocalShadowFramebuffer->Release();
    }
    if (m_DirectionalShadowCascadeAtlas != nullptr) {
        m_DirectionalShadowCascadeAtlas->Release();
    }
    if (m_LocalShadowAtlas != nullptr) {
        m_LocalShadowAtlas->Release();
    }
    m_DepthBuffer->Release();
    if (m_SceneRenderTargets != nullptr) {
        m_SceneRenderTargets->Release();
    }
    if (m_BloomPyramid != nullptr) {
        m_BloomPyramid->Release();
    }
    if (m_ColorGradingLut != nullptr) {
        m_ColorGradingLut->Release();
    }
    if (m_MaterialDescriptorSets != nullptr) {
        m_MaterialDescriptorSets->Release();
    }
    if (m_GBufferDescriptorSets != nullptr) {
        m_GBufferDescriptorSets->Release();
    }
    if (m_HdrDescriptorSets != nullptr) {
        m_HdrDescriptorSets->Release();
    }
    if (m_BloomDescriptorSets != nullptr) {
        m_BloomDescriptorSets->Release();
    }
    if (m_WeightedTranslucencyDescriptorSets != nullptr) {
        m_WeightedTranslucencyDescriptorSets->Release();
    }
    if (m_OverlayDescriptorSets != nullptr) {
        m_OverlayDescriptorSets->Release();
    }
    m_DescriptorSets->Release();
    if (m_OverlayUniformBuffer != nullptr) {
        m_OverlayUniformBuffer->Release();
    }
    m_UniformBuffer->Release();
    if (m_LightBuffer != nullptr) {
        m_LightBuffer->Release();
    }
    if (m_LightTileDiagnosticsBuffer != nullptr) {
        m_LightTileDiagnosticsBuffer->Release();
    }
    if (m_AutoExposureBuffer != nullptr) {
        m_AutoExposureBuffer->Release();
    }
    if (m_MaterialBuffer != nullptr) {
        m_MaterialBuffer->Release();
    }
    if (m_DirectionalShadowCascadeBuffer != nullptr) {
        m_DirectionalShadowCascadeBuffer->Release();
    }
    if (m_LocalShadowBuffer != nullptr) {
        m_LocalShadowBuffer->Release();
    }
    m_Swapchain->Release();

    m_Swapchain->Recreate(m_Window, m_PhysicalDevice, m_Device, m_Surface);
    m_UniformBuffer->Recreate(m_Device, m_PhysicalDevice, m_Swapchain->Images().size());
    if (m_LightBuffer != nullptr) {
        m_LightBuffer->Recreate(m_Device, m_PhysicalDevice, m_Swapchain->Images().size());
    }
    if (m_LightTileDiagnosticsBuffer != nullptr) {
        m_LightTileDiagnosticsBuffer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
    if (m_AutoExposureBuffer != nullptr) {
        m_AutoExposureBuffer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
    m_LightTileGpuReadbackReady.assign(m_Swapchain->Images().size(), false);
    if (m_MaterialBuffer != nullptr) {
        m_MaterialBuffer->Recreate(m_Device, m_PhysicalDevice, m_Swapchain->Images().size());
    }
    if (m_DirectionalShadowCascadeBuffer != nullptr) {
        m_DirectionalShadowCascadeBuffer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
    if (m_LocalShadowBuffer != nullptr) {
        m_LocalShadowBuffer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
    m_DescriptorSets->Recreate(
        m_Device,
        *m_DescriptorSetLayout,
        *m_UniformBuffer,
        *m_LightBuffer,
        *m_LightTileDiagnosticsBuffer,
        *m_MaterialBuffer,
        *m_DirectionalShadowCascadeBuffer,
        *m_LocalShadowBuffer,
        *m_AutoExposureBuffer
    );
    m_ReflectionProbeResources.SetDescriptorSetsBound(
        UpdateEnvironmentDescriptorSets(m_DescriptorSets.get())
    );
    m_SyncObjects->RecreateSwapchainSyncObjects(m_Swapchain->Images().size());
    m_DepthBuffer->Recreate(m_Device, m_PhysicalDevice, *m_Swapchain);
    if (m_SceneRenderTargets != nullptr) {
        m_SceneRenderTargets->Recreate(
            m_Device,
            m_PhysicalDevice,
            *m_Swapchain
        );
    }
    if (m_BloomPyramid != nullptr) {
        m_BloomPyramid->Recreate(
            m_Device,
            m_PhysicalDevice,
            *m_Swapchain
        );
    }
    if (m_ColorGradingLut != nullptr) {
        m_ColorGradingLut->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool
        );
    }
    if (m_HdrRenderPass != nullptr && m_SceneRenderTargets != nullptr) {
        m_HdrRenderPass->Recreate(
            m_Device,
            m_SceneRenderTargets->HdrSceneColorFormat()
        );
    }
    if (m_BloomDownsampleRenderPass != nullptr && m_BloomPyramid != nullptr) {
        m_BloomDownsampleRenderPass->Recreate(
            m_Device,
            m_BloomPyramid->BloomFormat(),
            false
        );
    }
    if (m_BloomUpsampleRenderPass != nullptr && m_BloomPyramid != nullptr) {
        m_BloomUpsampleRenderPass->Recreate(
            m_Device,
            m_BloomPyramid->BloomFormat(),
            true
        );
    }
    if (m_WeightedTranslucencyRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_WeightedTranslucencyRenderPass->Recreate(
            m_Device,
            *m_SceneRenderTargets
        );
    }
    if (m_GBufferRenderPass != nullptr && m_SceneRenderTargets != nullptr) {
        m_GBufferRenderPass->Recreate(
            m_Device,
            *m_SceneRenderTargets
        );
    }
    if (m_HdrFramebuffer != nullptr &&
        m_HdrRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_HdrFramebuffer->Recreate(
            m_Device,
            *m_HdrRenderPass,
            *m_SceneRenderTargets
        );
    }
    if (m_BloomDownsampleFramebuffer != nullptr &&
        m_BloomDownsampleRenderPass != nullptr &&
        m_BloomPyramid != nullptr) {
        m_BloomDownsampleFramebuffer->Recreate(
            m_Device,
            *m_BloomDownsampleRenderPass,
            *m_BloomPyramid
        );
    }
    if (m_BloomUpsampleFramebuffer != nullptr &&
        m_BloomUpsampleRenderPass != nullptr &&
        m_BloomPyramid != nullptr) {
        m_BloomUpsampleFramebuffer->Recreate(
            m_Device,
            *m_BloomUpsampleRenderPass,
            *m_BloomPyramid
        );
    }
    if (m_WeightedTranslucencyFramebuffer != nullptr &&
        m_WeightedTranslucencyRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_WeightedTranslucencyFramebuffer->Recreate(
            m_Device,
            *m_WeightedTranslucencyRenderPass,
            *m_SceneRenderTargets
        );
    }
    if (m_GBufferFramebuffer != nullptr &&
        m_GBufferRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_GBufferFramebuffer->Recreate(
            m_Device,
            *m_GBufferRenderPass,
            *m_SceneRenderTargets
        );
    }
    if (m_ShadowMap != nullptr) {
        m_ShadowMap->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            m_ShadowSettings.mapSize
        );
    }
    if (m_DirectionalShadowCascadeAtlas != nullptr) {
        m_DirectionalShadowCascadeAtlas->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            m_ShadowSettings.mapSize
        );
    }
    if (m_LocalShadowAtlas != nullptr) {
        m_LocalShadowAtlas->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            LocalShadowAtlasTileSizeFor(m_ShadowSettings),
            LocalShadowAtlasTileCapacityFor(m_ShadowSettings)
        );
    }
    ResetLocalShadowCacheStates();
    if (m_ShadowFramebuffer != nullptr && m_ShadowRenderPass != nullptr && m_ShadowMap != nullptr) {
        m_ShadowFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_ShadowMap
        );
    }
    if (m_DirectionalShadowCascadeFramebuffer != nullptr &&
        m_ShadowRenderPass != nullptr &&
        m_DirectionalShadowCascadeAtlas != nullptr) {
        m_DirectionalShadowCascadeFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_DirectionalShadowCascadeAtlas
        );
    }
    if (m_LocalShadowFramebuffer != nullptr &&
        m_ShadowRenderPass != nullptr &&
        m_LocalShadowAtlas != nullptr) {
        m_LocalShadowFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_LocalShadowAtlas
        );
    }
    std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
    m_MaterialDescriptorSets = std::make_unique<VulkanMaterialDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        materials,
        m_ShadowMap.get(),
        m_DirectionalShadowCascadeAtlas.get(),
        m_LocalShadowAtlas.get()
    );
    if (m_GBufferDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_GBufferDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SceneTargetSampler,
            m_ShadowMap.get(),
            m_DirectionalShadowCascadeAtlas.get(),
            m_LocalShadowAtlas.get()
        );
    }
    if (m_HdrDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_BloomPyramid != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_HdrDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            m_BloomPyramid.get(),
            m_ColorGradingLut.get(),
            *m_SceneTargetSampler
        );
    }
    if (m_BloomDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_BloomPyramid != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_BloomDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_BloomPyramid,
            *m_SceneTargetSampler
        );
    }
    if (m_WeightedTranslucencyDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_WeightedTranslucencyDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SceneTargetSampler
        );
    }
    m_RenderPass->Recreate(m_Device, *m_Swapchain, *m_DepthBuffer);
    if (m_DepthLoadRenderPass != nullptr) {
        m_DepthLoadRenderPass->Recreate(
            m_Device,
            *m_Swapchain,
            *m_DepthBuffer,
            true
        );
    }
    m_ImGuiLayer = std::make_unique<VulkanImGuiLayer>(
        m_Window,
        m_Instance,
        m_PhysicalDevice,
        m_Device,
        *m_RenderPass,
        *m_Swapchain
    );
    m_GraphicsPipeline->Recreate(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        m_PipelineSpec
    );
    if (m_DoubleSidedGraphicsPipeline != nullptr) {
        m_DoubleSidedGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::DoubleSided(m_PipelineSpec)
        );
    }
    if (m_ForwardResidualGraphicsPipeline != nullptr) {
        const PipelineSpec forwardResidualSpec =
            PipelineSpec::ForwardResidual3D(
                m_PipelineSpec.vertexShaderPath,
                m_PipelineSpec.fragmentShaderPath
            );
        m_ForwardResidualGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            forwardResidualSpec
        );
        if (m_DoubleSidedForwardResidualGraphicsPipeline != nullptr) {
            m_DoubleSidedForwardResidualGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                *m_RenderPass,
                *m_Swapchain,
                PipelineSpec::DoubleSided(forwardResidualSpec)
            );
        }
    }
    if (m_WeightedTranslucencyGraphicsPipeline != nullptr &&
        m_WeightedTranslucencyRenderPass != nullptr) {
        const std::string weightedTranslucencyFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/weighted_translucency_3d.frag.spv";
        const PipelineSpec weightedTranslucencySpec =
            PipelineSpec::WeightedTranslucency3D(
                m_PipelineSpec.vertexShaderPath,
                weightedTranslucencyFragmentShaderPath
            );
        m_WeightedTranslucencyGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_WeightedTranslucencyRenderPass->Handle(),
            *m_Swapchain,
            weightedTranslucencySpec
        );
        if (m_DoubleSidedWeightedTranslucencyGraphicsPipeline != nullptr) {
            m_DoubleSidedWeightedTranslucencyGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_WeightedTranslucencyRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(weightedTranslucencySpec)
            );
        }
    }
    if (m_DepthPrefillGraphicsPipeline != nullptr) {
        const std::string depthPrefillShaderPath =
            std::string(SE_SHADER_DIR) + "/depth_prefill_3d.vert.spv";
        const PipelineSpec depthPrefillSpec =
            PipelineSpec::DepthPrefill3D(depthPrefillShaderPath);
        m_DepthPrefillGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            depthPrefillSpec
        );
        if (m_DoubleSidedDepthPrefillGraphicsPipeline != nullptr) {
            m_DoubleSidedDepthPrefillGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                *m_RenderPass,
                *m_Swapchain,
                PipelineSpec::DoubleSided(depthPrefillSpec)
            );
        }
    }
    if (m_BloomDownsamplePipeline != nullptr &&
        m_BloomDownsampleRenderPass != nullptr) {
        const std::string hdrCompositeVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
        const std::string bloomDownsampleFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/bloom_downsample.frag.spv";
        m_BloomDownsamplePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_BloomDownsampleRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::BloomPyramid(
                hdrCompositeVertexShaderPath,
                bloomDownsampleFragmentShaderPath
            )
        );
    }
    if (m_BloomUpsamplePipeline != nullptr &&
        m_BloomUpsampleRenderPass != nullptr) {
        const std::string hdrCompositeVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
        const std::string bloomUpsampleFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/bloom_upsample.frag.spv";
        m_BloomUpsamplePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_BloomUpsampleRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::BloomUpsample(
                hdrCompositeVertexShaderPath,
                bloomUpsampleFragmentShaderPath
            )
        );
    }
    if (m_HdrCompositePipeline != nullptr) {
        const std::string hdrCompositeVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
        const std::string hdrCompositeFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.frag.spv";
        m_HdrCompositePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::HdrComposite(
                hdrCompositeVertexShaderPath,
                hdrCompositeFragmentShaderPath
            )
        );
    }
    if (m_WeightedTranslucencyResolvePipeline != nullptr &&
        m_HdrRenderPass != nullptr) {
        const std::string hdrCompositeVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
        const std::string weightedTranslucencyResolveFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/weighted_translucency_resolve.frag.spv";
        m_WeightedTranslucencyResolvePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_HdrRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::WeightedTranslucencyResolve(
                hdrCompositeVertexShaderPath,
                weightedTranslucencyResolveFragmentShaderPath
            )
        );
    }
    if (m_GBufferDebugPipeline != nullptr) {
        const std::string gBufferDebugVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_debug.vert.spv";
        const std::string gBufferDebugFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_debug.frag.spv";
        m_GBufferDebugPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::GBufferDebug(
                gBufferDebugVertexShaderPath,
                gBufferDebugFragmentShaderPath
            )
        );
    }
    if (m_InstancedGraphicsPipeline != nullptr) {
        PipelineSpec instancedSpec = m_PipelineSpec;
        instancedSpec.vertexShaderPath = m_PipelineSpec.instancedVertexShaderPath;
        instancedSpec.vertexLayout = VertexLayout::Vertex3DInstanced;
        m_InstancedGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            instancedSpec
        );
        if (m_DoubleSidedInstancedGraphicsPipeline != nullptr) {
            m_DoubleSidedInstancedGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                *m_RenderPass,
                *m_Swapchain,
                PipelineSpec::DoubleSided(instancedSpec)
            );
        }
        if (m_InstanceBuffer != nullptr) {
            m_InstanceBuffer->Recreate(
                m_Device,
                m_PhysicalDevice,
                m_Swapchain->Images().size()
            );
            m_MainInstanceUploadSignatures.clear();
        }
    }
    if (m_GBufferGraphicsPipeline != nullptr && m_GBufferRenderPass != nullptr) {
        const std::string gBufferVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_3d.vert.spv";
        const std::string gBufferFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_3d.frag.spv";
        const PipelineSpec gBufferSpec =
            PipelineSpec::GBuffer3D(
                gBufferVertexShaderPath,
                gBufferFragmentShaderPath
            );
        m_GBufferGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_GBufferRenderPass->Handle(),
            *m_Swapchain,
            gBufferSpec
        );
        if (m_DoubleSidedGBufferGraphicsPipeline != nullptr) {
            m_DoubleSidedGBufferGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_GBufferRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(gBufferSpec)
            );
        }
    }
    if (m_DeferredLightingPipeline != nullptr && m_HdrRenderPass != nullptr) {
        const std::string deferredLightingVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/deferred_lighting.vert.spv";
        const std::string deferredLightingFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/deferred_lighting.frag.spv";
        m_DeferredLightingPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_HdrRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::DeferredLighting(
                deferredLightingVertexShaderPath,
                deferredLightingFragmentShaderPath
            )
        );
    }
    if (m_LightTileCullComputePipeline != nullptr) {
        const std::string lightTileCullShaderPath =
            std::string(SE_SHADER_DIR) + "/light_tile_cull.comp.spv";
        m_LightTileCullComputePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            lightTileCullShaderPath
        );
    }
    if (m_AutoExposureComputePipeline != nullptr) {
        m_AutoExposureComputePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            std::string(SE_SHADER_DIR) + "/auto_exposure_histogram.comp.spv"
        );
    }
    if (m_ShadowGraphicsPipeline != nullptr && m_ShadowRenderPass != nullptr && m_ShadowMap != nullptr) {
        const std::string shadowShaderPath = std::string(SE_SHADER_DIR) + "/shadow_depth.vert.spv";
        const PipelineSpec shadowSpec =
            PipelineSpec::ShadowDepth(shadowShaderPath, m_ShadowMap->Extent());
        m_ShadowGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_ShadowRenderPass->Handle(),
            *m_Swapchain,
            shadowSpec
        );
        if (m_DoubleSidedShadowGraphicsPipeline != nullptr) {
            m_DoubleSidedShadowGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_ShadowRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(shadowSpec)
            );
        }
    }
    if (m_OverlayPipelineSpec.has_value()) {
        m_OverlayUniformBuffer = std::make_unique<VulkanUniformBuffer>(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
        m_OverlayDescriptorSets = std::make_unique<VulkanDescriptorSets>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_OverlayUniformBuffer,
            *m_LightBuffer,
            *m_LightTileDiagnosticsBuffer,
            *m_MaterialBuffer,
            *m_DirectionalShadowCascadeBuffer,
            *m_LocalShadowBuffer,
            *m_AutoExposureBuffer
        );
        m_ReflectionProbeResources.SetDescriptorSetsBound(
            m_ReflectionProbeResources.DescriptorSetsBound() +
                UpdateEnvironmentDescriptorSets(m_OverlayDescriptorSets.get())
        );
        m_OverlayGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            *m_OverlayPipelineSpec
        );
    }
    m_Framebuffer->Recreate(m_Device, *m_RenderPass, *m_DepthBuffer, *m_Swapchain);
    if (m_DepthLoadFramebuffer != nullptr && m_DepthLoadRenderPass != nullptr) {
        m_DepthLoadFramebuffer->Recreate(
            m_Device,
            *m_DepthLoadRenderPass,
            *m_DepthBuffer,
            *m_Swapchain
        );
    }
    m_CommandBuffer->Recreate(
        m_Device,
        m_CommandPool,
        *m_Framebuffer
    );
    if (m_GpuTimer != nullptr) {
        m_GpuTimer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
}

void VulkanRenderer::ApplyEnvironmentRenderSettings() {
    if (EnvironmentFlagEnabled("SE_FPS_FIRST")) {
        ApplyShadowQualityPreset(m_ShadowSettings, VulkanShadowQuality::Off);
    }

    const std::optional<VulkanShadowQuality> shadowQuality =
        ShadowQualityFromEnvironment();
    if (shadowQuality.has_value()) {
        ApplyShadowQualityPreset(m_ShadowSettings, *shadowQuality);
    }

    const std::optional<bool> reflectionProbeFallback =
        EnvironmentFlagOverride("SE_REFLECTION_PROBE_FALLBACK");
    if (reflectionProbeFallback.has_value()) {
        m_ShadowSettings.reflectionProbeFallbackEnabled = *reflectionProbeFallback;
    }
    const std::optional<bool> reflectionProbeCubemap =
        EnvironmentFlagOverride("SE_REFLECTION_PROBE_CUBEMAP");
    if (reflectionProbeCubemap.has_value()) {
        m_ShadowSettings.reflectionProbeCubemapEnabled = *reflectionProbeCubemap;
    }
    const std::optional<bool> localReflectionProbe =
        EnvironmentFlagOverride("SE_LOCAL_REFLECTION_PROBE");
    if (localReflectionProbe.has_value()) {
        m_ShadowSettings.localReflectionProbeEnabled = *localReflectionProbe;
    }
    const std::optional<bool> heightFog =
        EnvironmentFlagOverride("SE_HEIGHT_FOG");
    if (heightFog.has_value()) {
        m_ShadowSettings.heightFogEnabled = *heightFog;
    }
    const std::optional<bool> bloom = EnvironmentFlagOverride("SE_BLOOM");
    if (bloom.has_value()) {
        m_RenderDebugSettings.bloomEnabled = *bloom;
    }
    const std::optional<f32> bloomIntensity =
        EnvironmentFloatOverride("SE_BLOOM_INTENSITY");
    if (bloomIntensity.has_value()) {
        m_RenderDebugSettings.bloomIntensity = *bloomIntensity;
    }
    const std::optional<f32> bloomThreshold =
        EnvironmentFloatOverride("SE_BLOOM_THRESHOLD");
    if (bloomThreshold.has_value()) {
        m_RenderDebugSettings.bloomThreshold = *bloomThreshold;
    }
    const std::optional<f32> bloomRadius =
        EnvironmentFloatOverride("SE_BLOOM_RADIUS");
    if (bloomRadius.has_value()) {
        m_RenderDebugSettings.bloomRadiusPixels = *bloomRadius;
    }
    const std::optional<f32> exposure =
        EnvironmentFloatOverride("SE_EXPOSURE");
    if (exposure.has_value()) {
        m_RenderDebugSettings.exposure = *exposure;
    }
    const std::optional<u32> toneMapMode = ToneMapModeFromEnvironment();
    if (toneMapMode.has_value()) {
        m_RenderDebugSettings.toneMapMode = *toneMapMode;
    }
    const std::optional<f32> toneMapWhitePoint =
        EnvironmentFloatOverride("SE_TONEMAP_WHITE_POINT");
    if (toneMapWhitePoint.has_value()) {
        m_RenderDebugSettings.toneMapWhitePoint = *toneMapWhitePoint;
    }
    const std::optional<bool> autoExposure =
        EnvironmentFlagOverride("SE_AUTO_EXPOSURE");
    if (autoExposure.has_value()) {
        m_RenderDebugSettings.autoExposureEnabled = *autoExposure;
    }
    const std::optional<f32> autoExposureTarget =
        EnvironmentFloatOverride("SE_AUTO_EXPOSURE_TARGET");
    if (autoExposureTarget.has_value()) {
        m_RenderDebugSettings.autoExposureTargetLuminance = *autoExposureTarget;
    }
    const std::optional<f32> autoExposureMin =
        EnvironmentFloatOverride("SE_AUTO_EXPOSURE_MIN");
    if (autoExposureMin.has_value()) {
        m_RenderDebugSettings.autoExposureMin = *autoExposureMin;
    }
    const std::optional<f32> autoExposureMax =
        EnvironmentFloatOverride("SE_AUTO_EXPOSURE_MAX");
    if (autoExposureMax.has_value()) {
        m_RenderDebugSettings.autoExposureMax = *autoExposureMax;
    }
    const std::optional<f32> autoExposureAdaptation =
        EnvironmentFloatOverride("SE_AUTO_EXPOSURE_ADAPTATION");
    if (autoExposureAdaptation.has_value()) {
        m_RenderDebugSettings.autoExposureAdaptation = *autoExposureAdaptation;
    }
    const std::optional<bool> colorGrading =
        EnvironmentFlagOverride("SE_COLOR_GRADING");
    if (colorGrading.has_value()) {
        m_RenderDebugSettings.colorGradingEnabled = *colorGrading;
    }
    const std::optional<f32> colorGradingSaturation =
        EnvironmentFloatOverride("SE_COLOR_GRADING_SATURATION");
    if (colorGradingSaturation.has_value()) {
        m_RenderDebugSettings.colorGradingSaturation = *colorGradingSaturation;
    }
    const std::optional<f32> colorGradingContrast =
        EnvironmentFloatOverride("SE_COLOR_GRADING_CONTRAST");
    if (colorGradingContrast.has_value()) {
        m_RenderDebugSettings.colorGradingContrast = *colorGradingContrast;
    }
    const std::optional<f32> colorGradingGamma =
        EnvironmentFloatOverride("SE_COLOR_GRADING_GAMMA");
    if (colorGradingGamma.has_value()) {
        m_RenderDebugSettings.colorGradingGamma = *colorGradingGamma;
    }
    const std::optional<f32> colorGradingLutStrength =
        EnvironmentFloatOverride("SE_COLOR_GRADING_LUT_STRENGTH");
    if (colorGradingLutStrength.has_value()) {
        m_RenderDebugSettings.colorGradingLutStrength = *colorGradingLutStrength;
    }
    const std::optional<bool> sharpening =
        EnvironmentFlagOverride("SE_SHARPENING");
    if (sharpening.has_value()) {
        m_RenderDebugSettings.sharpeningEnabled = *sharpening;
    }
    const std::optional<f32> sharpeningStrength =
        EnvironmentFloatOverride("SE_SHARPENING_STRENGTH");
    if (sharpeningStrength.has_value()) {
        m_RenderDebugSettings.sharpeningStrength = *sharpeningStrength;
    }
    const std::optional<f32> sharpeningRadius =
        EnvironmentFloatOverride("SE_SHARPENING_RADIUS");
    if (sharpeningRadius.has_value()) {
        m_RenderDebugSettings.sharpeningRadiusPixels = *sharpeningRadius;
    }

    const std::optional<ForwardDebugView> forwardDebugView =
        ForwardDebugViewFromEnvironment();
    if (forwardDebugView.has_value()) {
        m_RenderDebugSettings.forwardView = *forwardDebugView;
    }
}

void VulkanRenderer::ApplyShadowMapSettings() {
    if (m_ShadowMap == nullptr ||
        m_ShadowFramebuffer == nullptr ||
        m_ShadowRenderPass == nullptr ||
        m_ShadowGraphicsPipeline == nullptr ||
        m_MaterialDescriptorSetLayout == nullptr ||
        m_MaterialDescriptorSets == nullptr) {
        return;
    }

    const VkExtent2D currentExtent = m_ShadowMap->Extent();
    if (currentExtent.width == m_ShadowSettings.mapSize &&
        currentExtent.height == m_ShadowSettings.mapSize) {
        return;
    }

    WaitIdle();
    m_ShadowGraphicsPipeline->Release();
    if (m_DoubleSidedShadowGraphicsPipeline != nullptr) {
        m_DoubleSidedShadowGraphicsPipeline->Release();
    }
    if (m_DirectionalShadowCascadeFramebuffer != nullptr) {
        m_DirectionalShadowCascadeFramebuffer->Release();
    }
    if (m_LocalShadowFramebuffer != nullptr) {
        m_LocalShadowFramebuffer->Release();
    }
    m_ShadowFramebuffer->Release();
    m_ShadowMap->Recreate(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        m_Swapchain->Images().size(),
        m_ShadowSettings.mapSize
    );
    if (m_DirectionalShadowCascadeAtlas != nullptr) {
        m_DirectionalShadowCascadeAtlas->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            m_ShadowSettings.mapSize
        );
    }
    if (m_LocalShadowAtlas != nullptr) {
        m_LocalShadowAtlas->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            LocalShadowAtlasTileSizeFor(m_ShadowSettings),
            LocalShadowAtlasTileCapacityFor(m_ShadowSettings)
        );
    }
    ResetLocalShadowCacheStates();
    m_ShadowFramebuffer->Recreate(
        m_Device,
        *m_ShadowRenderPass,
        *m_ShadowMap
    );
    if (m_DirectionalShadowCascadeFramebuffer != nullptr &&
        m_DirectionalShadowCascadeAtlas != nullptr) {
        m_DirectionalShadowCascadeFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_DirectionalShadowCascadeAtlas
        );
    }
    if (m_LocalShadowFramebuffer != nullptr &&
        m_LocalShadowAtlas != nullptr) {
        m_LocalShadowFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_LocalShadowAtlas
        );
    }

    std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
    m_MaterialDescriptorSets = std::make_unique<VulkanMaterialDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        materials,
        m_ShadowMap.get(),
        m_DirectionalShadowCascadeAtlas.get(),
        m_LocalShadowAtlas.get()
    );
    if (m_GBufferDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_GBufferDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SceneTargetSampler,
            m_ShadowMap.get(),
            m_DirectionalShadowCascadeAtlas.get(),
            m_LocalShadowAtlas.get()
        );
    }

    const std::string shadowShaderPath = std::string(SE_SHADER_DIR) + "/shadow_depth.vert.spv";
    const PipelineSpec shadowSpec =
        PipelineSpec::ShadowDepth(shadowShaderPath, m_ShadowMap->Extent());
    m_ShadowGraphicsPipeline->Recreate(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_ShadowRenderPass->Handle(),
        *m_Swapchain,
        shadowSpec
    );
    if (m_DoubleSidedShadowGraphicsPipeline != nullptr) {
        m_DoubleSidedShadowGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_ShadowRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::DoubleSided(shadowSpec)
        );
    }
}

void VulkanRenderer::ResetLocalShadowCacheStates() {
    if (m_Swapchain == nullptr) {
        m_LocalShadowCacheStates.clear();
        return;
    }

    m_LocalShadowCacheStates.assign(
        m_Swapchain->Images().size(),
        LocalShadowCacheState{}
    );
}

void VulkanRenderer::HandleObjectPicking() {
    if (m_Scene == nullptr || m_Camera == nullptr) {
        return;
    }

    if (!m_Window.WasLeftMousePressed()) {
        return;
    }

    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        return;
    }

    m_Scene->SelectAtWorldPosition(CursorToWorldPosition(m_Swapchain->Extent()));
}

glm::vec2 VulkanRenderer::CursorToWorldPosition(const VkExtent2D& extent) const {
    SE_ASSERT(m_Camera != nullptr, "Cursor picking needs a 2D camera");

    const std::array<f64, 2> cursorPosition = m_Window.CursorPosition();
    const std::array<int, 2> windowSize = m_Window.WindowSize();
    const f32 cursorX = static_cast<f32>(cursorPosition[0]);
    const f32 cursorY = static_cast<f32>(cursorPosition[1]);
    const f32 windowWidth = static_cast<f32>(windowSize[0]);
    const f32 windowHeight = static_cast<f32>(windowSize[1]);
    const f32 framebufferWidth = static_cast<f32>(extent.width);
    const f32 framebufferHeight = static_cast<f32>(extent.height);
    const f32 aspectRatio = framebufferWidth / framebufferHeight;

    const f32 ndcX = (cursorX / windowWidth) * 2.0f - 1.0f;
    const f32 ndcY = (cursorY / windowHeight) * 2.0f - 1.0f;

    const glm::mat4 inverseViewProjection =
        glm::inverse(m_Camera->ProjectionMatrix(aspectRatio) * m_Camera->ViewMatrix());
    const glm::vec4 nearPositionH = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farPositionH = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 nearPosition = glm::vec3(nearPositionH) / nearPositionH.w;
    const glm::vec3 farPosition = glm::vec3(farPositionH) / farPositionH.w;
    const glm::vec3 rayDirection = farPosition - nearPosition;

    if (std::abs(rayDirection.z) <= std::numeric_limits<f32>::epsilon()) {
        return glm::vec2(nearPosition);
    }

    const f32 planeHit = -nearPosition.z / rayDirection.z;
    const glm::vec3 worldPosition = nearPosition + rayDirection * planeHit;

    return glm::vec2(worldPosition);
}

void VulkanRenderer::UpdateUniformBuffer(
    std::size_t imageIndex,
    const FrameMatrices* matrices,
    const glm::mat4& lightViewProjection,
    const FrameLightConstants& lights,
    const FrameReflectionProbeSet& reflectionProbes,
    bool shadowSamplingEnabled
) const {
    UniformBufferObject uniformData{};
    if (matrices != nullptr) {
        uniformData.view = matrices->view;
        uniformData.proj = matrices->proj;
        uniformData.invView = glm::inverse(matrices->view);
        uniformData.invProj = glm::inverse(matrices->proj);
    }
    uniformData.lightViewProj = lightViewProjection;
    uniformData.directionalLight = lights.directionalLight;
    uniformData.ambientLight = lights.ambientLight;
    uniformData.shadowControls = glm::vec4(
        shadowSamplingEnabled ? 1.0f : 0.0f,
        m_ShadowSettings.strength,
        m_ShadowSettings.biasMin,
        m_ShadowSettings.biasSlope
    );
    uniformData.shadowFiltering = glm::vec4(
        m_ShadowSettings.pcfRadius,
        m_ShadowSettings.ambientStrength,
        static_cast<f32>(static_cast<int>(m_RenderDebugSettings.forwardView)),
        m_RenderDebugSettings.exposure
    );
    uniformData.contactShadowControls = glm::vec4(
        std::clamp(m_ShadowSettings.contactShadowStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.contactShadowLength, 0.0f, 1.0f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.contactShadowSteps,
            0u,
            12u
        )),
        std::clamp(m_ShadowSettings.contactShadowThickness, 0.0f, 0.5f)
    );
    uniformData.contactShadowStabilityControls = glm::vec4(
        std::clamp(m_ShadowSettings.contactShadowJitterStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.contactShadowEdgeFadePixels, 0.0f, 96.0f),
        0.0f,
        0.0f
    );
    uniformData.ssaoControls = glm::vec4(
        std::clamp(m_ShadowSettings.ssaoStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.ssaoRadius, 0.0f, 8.0f),
        std::clamp(m_ShadowSettings.ssaoBias, 0.0f, 0.5f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.ssaoSampleCount,
            0u,
            16u
        ))
    );
    uniformData.ssrControls = glm::vec4(
        std::clamp(m_ShadowSettings.ssrStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.ssrRayLength, 0.0f, 64.0f),
        std::clamp(m_ShadowSettings.ssrThickness, 0.0f, 0.5f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.ssrStepCount,
            0u,
            32u
        ))
    );
    uniformData.reflectionProbeControls = glm::vec4(
        m_ShadowSettings.reflectionProbeFallbackEnabled ? 1.0f : 0.0f,
        std::clamp(m_ShadowSettings.reflectionProbeDiffuseIntensity, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.reflectionProbeSpecularIntensity, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.reflectionProbeHorizonBlend, 0.0f, 1.0f)
    );
    PopulateReflectionProbeUniforms(
        reflectionProbes,
        m_ShadowSettings.reflectionProbeCubemapEnabled,
        uniformData
    );
    const bool heightFogApplied =
        m_ShadowSettings.heightFogEnabled &&
        m_ShadowSettings.heightFogDensity > 0.0001f &&
        m_ShadowSettings.heightFogMaxOpacity > 0.0001f;
    uniformData.heightFogControls = glm::vec4(
        heightFogApplied ? 1.0f : 0.0f,
        std::clamp(m_ShadowSettings.heightFogDensity, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.heightFogHeightFalloff, 0.0f, 2.0f),
        std::clamp(m_ShadowSettings.heightFogStartDistance, 0.0f, 1000.0f)
    );
    uniformData.heightFogColor = glm::vec4(
        std::clamp(m_ShadowSettings.heightFogColorR, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogColorG, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogColorB, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogMaxOpacity, 0.0f, 1.0f)
    );
    const bool bloomApplied =
        m_RenderDebugSettings.bloomEnabled &&
        m_RenderDebugSettings.bloomIntensity > 0.0001f &&
        m_RenderDebugSettings.bloomRadiusPixels > 0.0001f;
    uniformData.postProcessControls = glm::vec4(
        bloomApplied ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.bloomIntensity, 0.0f, 4.0f),
        std::clamp(m_RenderDebugSettings.bloomThreshold, 0.0f, 16.0f),
        std::clamp(m_RenderDebugSettings.bloomRadiusPixels, 0.0f, 24.0f)
    );
    uniformData.colorGradingControls = glm::vec4(
        m_RenderDebugSettings.colorGradingEnabled ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.colorGradingSaturation, 0.0f, 2.5f),
        std::clamp(m_RenderDebugSettings.colorGradingContrast, 0.0f, 2.5f),
        std::clamp(m_RenderDebugSettings.colorGradingGamma, 0.25f, 4.0f)
    );
    const bool colorGradingLutReady =
        m_ColorGradingLut != nullptr && m_ColorGradingLut->Uploaded();
    const bool colorGradingLutApplied =
        m_RenderDebugSettings.colorGradingEnabled &&
        colorGradingLutReady &&
        m_RenderDebugSettings.colorGradingLutStrength > 0.0001f;
    uniformData.colorGradingLutControls = glm::vec4(
        colorGradingLutApplied
            ? std::clamp(m_RenderDebugSettings.colorGradingLutStrength, 0.0f, 1.0f)
            : 0.0f,
        static_cast<f32>(kColorGradingLutSize),
        colorGradingLutReady ? 1.0f : 0.0f,
        m_RenderDebugSettings.colorGradingEnabled && !colorGradingLutReady ? 1.0f : 0.0f
    );
    uniformData.toneMappingControls = glm::vec4(
        static_cast<f32>(std::clamp<u32>(m_RenderDebugSettings.toneMapMode, 0u, 2u)),
        std::clamp(m_RenderDebugSettings.exposure, 0.001f, 32.0f),
        std::clamp(m_RenderDebugSettings.toneMapWhitePoint, 0.1f, 64.0f),
        m_RenderDebugSettings.autoExposureEnabled ? 1.0f : 0.0f
    );
    const f32 autoExposureMin = std::clamp(m_RenderDebugSettings.autoExposureMin, 0.001f, 32.0f);
    uniformData.autoExposureControls = glm::vec4(
        std::clamp(m_RenderDebugSettings.autoExposureTargetLuminance, 0.001f, 4.0f),
        autoExposureMin,
        std::max(autoExposureMin, std::clamp(m_RenderDebugSettings.autoExposureMax, 0.001f, 32.0f)),
        std::clamp(m_RenderDebugSettings.autoExposureAdaptation, 0.0f, 1.0f)
    );
    const bool sharpeningApplied =
        m_RenderDebugSettings.sharpeningEnabled &&
        m_RenderDebugSettings.sharpeningStrength > 0.0001f &&
        m_RenderDebugSettings.sharpeningRadiusPixels > 0.0001f;
    uniformData.sharpeningControls = glm::vec4(
        sharpeningApplied ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.sharpeningStrength, 0.0f, 2.0f),
        std::clamp(m_RenderDebugSettings.sharpeningRadiusPixels, 0.0f, 4.0f),
        0.0f
    );

    m_UniformBuffer->Update(imageIndex, uniformData);
}

void VulkanRenderer::UpdateOverlayUniformBuffer(
    std::size_t imageIndex,
    const FrameMatrices* matrices,
    const glm::mat4& lightViewProjection,
    const FrameLightConstants& lights,
    const FrameReflectionProbeSet& reflectionProbes,
    bool shadowSamplingEnabled
) const {
    if (m_OverlayUniformBuffer == nullptr || matrices == nullptr) {
        return;
    }

    UniformBufferObject uniformData{};
    uniformData.view = matrices->view;
    uniformData.proj = matrices->proj;
    uniformData.invView = glm::inverse(matrices->view);
    uniformData.invProj = glm::inverse(matrices->proj);
    uniformData.lightViewProj = lightViewProjection;
    uniformData.directionalLight = lights.directionalLight;
    uniformData.ambientLight = lights.ambientLight;
    uniformData.shadowControls = glm::vec4(
        shadowSamplingEnabled ? 1.0f : 0.0f,
        m_ShadowSettings.strength,
        m_ShadowSettings.biasMin,
        m_ShadowSettings.biasSlope
    );
    uniformData.shadowFiltering = glm::vec4(
        m_ShadowSettings.pcfRadius,
        m_ShadowSettings.ambientStrength,
        static_cast<f32>(static_cast<int>(m_RenderDebugSettings.forwardView)),
        m_RenderDebugSettings.exposure
    );
    uniformData.contactShadowControls = glm::vec4(
        std::clamp(m_ShadowSettings.contactShadowStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.contactShadowLength, 0.0f, 1.0f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.contactShadowSteps,
            0u,
            12u
        )),
        std::clamp(m_ShadowSettings.contactShadowThickness, 0.0f, 0.5f)
    );
    uniformData.contactShadowStabilityControls = glm::vec4(
        std::clamp(m_ShadowSettings.contactShadowJitterStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.contactShadowEdgeFadePixels, 0.0f, 96.0f),
        0.0f,
        0.0f
    );
    uniformData.ssaoControls = glm::vec4(
        std::clamp(m_ShadowSettings.ssaoStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.ssaoRadius, 0.0f, 8.0f),
        std::clamp(m_ShadowSettings.ssaoBias, 0.0f, 0.5f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.ssaoSampleCount,
            0u,
            16u
        ))
    );
    uniformData.ssrControls = glm::vec4(
        std::clamp(m_ShadowSettings.ssrStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.ssrRayLength, 0.0f, 64.0f),
        std::clamp(m_ShadowSettings.ssrThickness, 0.0f, 0.5f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.ssrStepCount,
            0u,
            32u
        ))
    );
    uniformData.reflectionProbeControls = glm::vec4(
        m_ShadowSettings.reflectionProbeFallbackEnabled ? 1.0f : 0.0f,
        std::clamp(m_ShadowSettings.reflectionProbeDiffuseIntensity, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.reflectionProbeSpecularIntensity, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.reflectionProbeHorizonBlend, 0.0f, 1.0f)
    );
    PopulateReflectionProbeUniforms(
        reflectionProbes,
        m_ShadowSettings.reflectionProbeCubemapEnabled,
        uniformData
    );
    const bool heightFogApplied =
        m_ShadowSettings.heightFogEnabled &&
        m_ShadowSettings.heightFogDensity > 0.0001f &&
        m_ShadowSettings.heightFogMaxOpacity > 0.0001f;
    uniformData.heightFogControls = glm::vec4(
        heightFogApplied ? 1.0f : 0.0f,
        std::clamp(m_ShadowSettings.heightFogDensity, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.heightFogHeightFalloff, 0.0f, 2.0f),
        std::clamp(m_ShadowSettings.heightFogStartDistance, 0.0f, 1000.0f)
    );
    uniformData.heightFogColor = glm::vec4(
        std::clamp(m_ShadowSettings.heightFogColorR, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogColorG, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogColorB, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogMaxOpacity, 0.0f, 1.0f)
    );
    const bool bloomApplied =
        m_RenderDebugSettings.bloomEnabled &&
        m_RenderDebugSettings.bloomIntensity > 0.0001f &&
        m_RenderDebugSettings.bloomRadiusPixels > 0.0001f;
    uniformData.postProcessControls = glm::vec4(
        bloomApplied ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.bloomIntensity, 0.0f, 4.0f),
        std::clamp(m_RenderDebugSettings.bloomThreshold, 0.0f, 16.0f),
        std::clamp(m_RenderDebugSettings.bloomRadiusPixels, 0.0f, 24.0f)
    );
    uniformData.colorGradingControls = glm::vec4(
        m_RenderDebugSettings.colorGradingEnabled ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.colorGradingSaturation, 0.0f, 2.5f),
        std::clamp(m_RenderDebugSettings.colorGradingContrast, 0.0f, 2.5f),
        std::clamp(m_RenderDebugSettings.colorGradingGamma, 0.25f, 4.0f)
    );
    const bool colorGradingLutReady =
        m_ColorGradingLut != nullptr && m_ColorGradingLut->Uploaded();
    const bool colorGradingLutApplied =
        m_RenderDebugSettings.colorGradingEnabled &&
        colorGradingLutReady &&
        m_RenderDebugSettings.colorGradingLutStrength > 0.0001f;
    uniformData.colorGradingLutControls = glm::vec4(
        colorGradingLutApplied
            ? std::clamp(m_RenderDebugSettings.colorGradingLutStrength, 0.0f, 1.0f)
            : 0.0f,
        static_cast<f32>(kColorGradingLutSize),
        colorGradingLutReady ? 1.0f : 0.0f,
        m_RenderDebugSettings.colorGradingEnabled && !colorGradingLutReady ? 1.0f : 0.0f
    );
    uniformData.toneMappingControls = glm::vec4(
        static_cast<f32>(std::clamp<u32>(m_RenderDebugSettings.toneMapMode, 0u, 2u)),
        std::clamp(m_RenderDebugSettings.exposure, 0.001f, 32.0f),
        std::clamp(m_RenderDebugSettings.toneMapWhitePoint, 0.1f, 64.0f),
        m_RenderDebugSettings.autoExposureEnabled ? 1.0f : 0.0f
    );
    const f32 autoExposureMin = std::clamp(m_RenderDebugSettings.autoExposureMin, 0.001f, 32.0f);
    uniformData.autoExposureControls = glm::vec4(
        std::clamp(m_RenderDebugSettings.autoExposureTargetLuminance, 0.001f, 4.0f),
        autoExposureMin,
        std::max(autoExposureMin, std::clamp(m_RenderDebugSettings.autoExposureMax, 0.001f, 32.0f)),
        std::clamp(m_RenderDebugSettings.autoExposureAdaptation, 0.0f, 1.0f)
    );
    const bool sharpeningApplied =
        m_RenderDebugSettings.sharpeningEnabled &&
        m_RenderDebugSettings.sharpeningStrength > 0.0001f &&
        m_RenderDebugSettings.sharpeningRadiusPixels > 0.0001f;
    uniformData.sharpeningControls = glm::vec4(
        sharpeningApplied ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.sharpeningStrength, 0.0f, 2.0f),
        std::clamp(m_RenderDebugSettings.sharpeningRadiusPixels, 0.0f, 4.0f),
        0.0f
    );
    m_OverlayUniformBuffer->Update(imageIndex, uniformData);
}

void VulkanRenderer::UpdateLightBuffer(
    std::size_t imageIndex,
    const FrameLightSet& lights,
    const VkExtent2D& extent,
    const FrameMatrices* matrices,
    FrameLightTileStats* tileStats
) const {
    if (m_LightBuffer == nullptr) {
        return;
    }

    const FrameLightConstants constants = lights.Constants();
    auto lightData = std::make_unique<LightBufferObject>();
    lightData->directionalLight = constants.directionalLight;
    lightData->ambientLight = constants.ambientLight;
    lightData->lightCounts = glm::vec4(
        static_cast<f32>(lights.directionalCount + lights.localCount),
        static_cast<f32>(lights.directionalCount),
        static_cast<f32>(lights.localCount),
        0.0f
    );
    const std::size_t localCount = std::min<std::size_t>(
        lights.localCount,
        lightData->localLights.size()
    );
    for (std::size_t index = 0; index < localCount; ++index) {
        const RendererLocalLight& light = lights.localLights[index];
        GpuLocalLightRecord& record = lightData->localLights[index];
        record.positionRadius = glm::vec4(light.position, std::max(light.radius, 0.0f));
        record.colorIntensity = glm::vec4(
            glm::max(light.color, glm::vec3(0.0f)),
            std::max(light.intensity, 0.0f)
        );
        glm::vec3 direction = light.direction;
        if (glm::dot(direction, direction) <= 0.0001f) {
            direction = { 0.0f, -1.0f, 0.0f };
        } else {
            direction = glm::normalize(direction);
        }
        f32 localLightType = 0.0f;
        if (light.kind == RendererLightKind::Spot) {
            localLightType = 1.0f;
        } else if (light.kind == RendererLightKind::Rect) {
            localLightType = 2.0f;
        }
        record.directionType = glm::vec4(direction, localLightType);
        record.parameters = glm::vec4(
            std::clamp(light.innerConeCos, -1.0f, 1.0f),
            std::clamp(light.outerConeCos, -1.0f, 1.0f),
            std::max(light.width, 0.0f),
            std::max(light.height, 0.0f)
        );
    }
    const FrameLightTileStats populatedTileStats =
        PopulateLightTileAssignments(*lightData, localCount, extent, matrices);
    if (tileStats != nullptr) {
        *tileStats = populatedTileStats;
    }
    m_LightBuffer->Update(imageIndex, *lightData);
    if (m_LightTileDiagnosticsBuffer != nullptr) {
        m_LightTileDiagnosticsBuffer->Update(
            imageIndex,
            LightTileDiagnosticsBufferObject{}
        );
    }
}

FrameLightTileGpuReadbackStats VulkanRenderer::ReadPreviousLightTileGpuStats(
    std::size_t imageIndex
) const {
    FrameLightTileGpuReadbackStats stats{};
    if (m_LightTileDiagnosticsBuffer == nullptr ||
        imageIndex >= m_LightTileGpuReadbackReady.size() ||
        !m_LightTileGpuReadbackReady[imageIndex]) {
        return stats;
    }

    const LightTileDiagnosticsBufferObject diagnostics =
        m_LightTileDiagnosticsBuffer->Download(imageIndex);
    if (diagnostics.counters.w == 0u) {
        return stats;
    }

    stats.valid = true;
    stats.saturatedTileCount = diagnostics.counters.x;
    stats.maxRawCandidateCount = diagnostics.counters.y;
    stats.rawCandidateCountSum = diagnostics.counters.z;
    stats.overflowUsedTileCount = diagnostics.overflowCounters.x;
    stats.overflowDroppedTileCount = diagnostics.overflowCounters.y;
    stats.overflowDroppedCount = diagnostics.overflowCounters.w;
    stats.overflowStoredCount =
        diagnostics.overflowCounters.z >= diagnostics.overflowCounters.w
            ? diagnostics.overflowCounters.z - diagnostics.overflowCounters.w
            : 0u;

    return stats;
}

FrameAutoExposureReadbackStats VulkanRenderer::ReadPreviousAutoExposureStats(
    std::size_t imageIndex
) const {
    FrameAutoExposureReadbackStats stats{};
    if (m_AutoExposureBuffer == nullptr ||
        imageIndex >= m_AutoExposureBuffer->Count()) {
        return stats;
    }

    const AutoExposureBufferObject exposure =
        m_AutoExposureBuffer->Download(imageIndex);
    stats.valid = exposure.exposure.w > 0.5f;
    stats.exposure = std::max(exposure.exposure.x, 0.001f);
    stats.targetExposure = std::max(exposure.exposure.y, 0.001f);
    stats.averageLuminance = std::max(exposure.exposure.z, 0.001f);
    return stats;
}

void VulkanRenderer::UpdateMaterialBuffer(
    std::size_t imageIndex,
    const FrameMaterialSet& materials
) const {
    if (m_MaterialBuffer == nullptr) {
        return;
    }

    m_MaterialBuffer->Update(imageIndex, materials.materialData);
}

void VulkanRenderer::UpdateDirectionalShadowCascadeBuffer(
    std::size_t imageIndex,
    const DirectionalShadowCascadeSet& cascades,
    const glm::mat4& fallbackLightViewProjection
) const {
    if (m_DirectionalShadowCascadeBuffer == nullptr) {
        return;
    }

    DirectionalShadowCascadeBufferObject cascadeData{};
    cascadeData.cascadeInfo = glm::vec4(
        static_cast<f32>(cascades.activeCount),
        static_cast<f32>(cascades.configuredCount),
        cascades.stableSnappingEnabled ? 1.0f : 0.0f,
        cascades.splitLambda
    );
    cascadeData.cascadeBlendControls = glm::vec4(
        std::clamp(m_ShadowSettings.cascadeBlendRatio, 0.0f, 0.25f),
        std::clamp(m_ShadowSettings.cascadeFadeRatio, 0.0f, 0.35f),
        static_cast<f32>(std::clamp<u32>(m_ShadowSettings.pcfKernelRadius, 0u, 2u)),
        std::clamp(m_ShadowSettings.pcssStrength, 0.0f, 1.0f)
    );
    cascadeData.fallbackViewProjection = fallbackLightViewProjection;
    for (u32 index = 0; index < kMaxDirectionalShadowCascades; ++index) {
        cascadeData.viewProjections[index] = cascades.cascades[index].viewProjection;
        cascadeData.splitDepths[index] = cascades.cascades[index].splitDepth;
        cascadeData.texelWorldSizes[index] = cascades.cascades[index].texelWorldSize;
    }
    cascadeData.texelWorldSizes.w = cascades.maxDistance;

    m_DirectionalShadowCascadeBuffer->Update(imageIndex, cascadeData);
}

void VulkanRenderer::UpdateLocalShadowBuffer(
    std::size_t imageIndex,
    const LocalShadowTileSet& localShadowTiles
) const {
    if (m_LocalShadowBuffer == nullptr) {
        return;
    }

    LocalShadowBufferObject localShadowData{};
    localShadowData.atlasInfo = glm::uvec4(
        localShadowTiles.assignedCount,
        localShadowTiles.tileSize,
        localShadowTiles.tileColumns,
        localShadowTiles.tileRows
    );
    localShadowData.atlasInfo2 = glm::uvec4(
        localShadowTiles.tileCapacity,
        localShadowTiles.requestedCount,
        localShadowTiles.droppedCount,
        0u
    );
    localShadowData.filterControls = glm::vec4(
        std::clamp(m_ShadowSettings.localBiasMin, 0.0f, 0.02f),
        std::clamp(m_ShadowSettings.localBiasSlope, 0.0f, 0.05f),
        std::clamp(m_ShadowSettings.localPcfRadius, 0.0f, 4.0f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.localPcfKernelRadius,
            0u,
            2u
        ))
    );
    localShadowData.softShadowControls = glm::vec4(
        std::clamp(m_ShadowSettings.localPcssStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.localFaceBlendStrength, 0.0f, 1.0f),
        0.0f,
        0.0f
    );
    const u32 assignedCount = std::min<u32>(
        localShadowTiles.assignedCount,
        static_cast<u32>(localShadowData.tiles.size())
    );
    for (u32 index = 0; index < assignedCount; ++index) {
        const LocalShadowTile& tile = localShadowTiles.tiles[index];
        GpuLocalShadowTileRecord& record = localShadowData.tiles[index];
        record.viewProjection = tile.viewProjection;
        record.tileInfo = glm::uvec4(
            tile.tileIndex,
            tile.localLightIndex,
            tile.faceIndex,
            tile.lightKind
        );
        record.lightInfo = glm::vec4(0.0f);
    }

    m_LocalShadowBuffer->Update(imageIndex, localShadowData);
}

FrameLightSet VulkanRenderer::BuildFrameLightSet(std::span<const RenderCommand> renderCommands) const {
    FrameLightSet lights{};
    if (!ApplySceneDirectionalLight(m_MainScene3D, lights)) {
        (void)ApplyMaterialDirectionalFallback(renderCommands, lights);
    }

    AddScenePointLights(m_MainScene3D, lights);
    AddSceneSpotLights(m_MainScene3D, lights);
    AddSceneRectLights(m_MainScene3D, lights);
    AddDebugLocalLights(lights);
    return lights;
}

FrameReflectionProbeSet VulkanRenderer::BuildFrameReflectionProbeSet(
    const FrameMatrices* matrices
) const {
    FrameReflectionProbeSet probes{};
    ResetFrameReflectionProbeCaptureDiagnostics(probes);
    probes.fallbackEnabled = m_ShadowSettings.reflectionProbeFallbackEnabled;
    probes.influenceMode = 1u;
    const bool builtInCubemapReady = LocalReflectionProbeCubemapReady();
    const u32 reflectionProbeDescriptorSetsBound =
        m_ReflectionProbeResources.DescriptorSetsBound();
    std::span<const ReflectionProbe3D> sceneProbes{};
    if (m_MainScene3D != nullptr) {
        sceneProbes = m_MainScene3D->ReflectionProbes();
        probes.sceneProbeCount = static_cast<u32>(std::min<std::size_t>(
            sceneProbes.size(),
            std::numeric_limits<u32>::max()
        ));
    }
    if (!probes.fallbackEnabled) {
        probes.captureFallbackReason =
            RendererReflectionProbeCaptureFallbackReason::FallbackDisabled;
        return probes;
    }

    const std::optional<RendererReflectionProbeCaptureSource>
        captureSourceOverride = ReflectionProbeCaptureSourceOverrideFromEnvironment();

    if (!sceneProbes.empty()) {
        struct ReflectionProbeCandidate {
            RendererReflectionProbe probe{};
            f32 selectionScore = 0.0f;
            f32 blendWeight = 0.0f;
        };

        const glm::vec3 selectionPosition = CameraPositionFromMatrices(matrices);
        std::vector<ReflectionProbeCandidate> candidates;
        candidates.reserve(sceneProbes.size());

        for (std::size_t index = 0; index < sceneProbes.size(); ++index) {
            const ReflectionProbe3D& sceneProbe = sceneProbes[index];
            RendererReflectionProbe candidate = SceneReflectionProbe(
                sceneProbe,
                index <= static_cast<std::size_t>(std::numeric_limits<i32>::max())
                    ? static_cast<i32>(index)
                    : -1
            );
            if (!ReflectionProbeContributes(candidate)) {
                continue;
            }
            if (captureSourceOverride.has_value()) {
                candidate.captureSource = *captureSourceOverride;
            }

            ++probes.eligibleSceneProbeCount;
            candidates.push_back(ReflectionProbeCandidate{
                candidate,
                ReflectionProbeSelectionScore(candidate, selectionPosition),
                ReflectionProbeInfluenceWeight(candidate, selectionPosition)
            });
        }

        if (!candidates.empty()) {
            std::sort(
                candidates.begin(),
                candidates.end(),
                [](const ReflectionProbeCandidate& left,
                   const ReflectionProbeCandidate& right) {
                    if (left.selectionScore == right.selectionScore) {
                        return left.probe.sceneIndex < right.probe.sceneIndex;
                    }
                    return left.selectionScore > right.selectionScore;
                }
            );

            probes.selectedProbeCount = std::min<u32>(
                static_cast<u32>(candidates.size()),
                static_cast<u32>(kMaxFrameReflectionProbes)
            );
            probes.activeLocalProbeCount = probes.selectedProbeCount;
            probes.blendedProbeCount = probes.selectedProbeCount;
            probes.multiBlendEnabled = probes.selectedProbeCount > 0u;
            probes.droppedSceneProbeCount =
                probes.eligibleSceneProbeCount > probes.selectedProbeCount
                    ? probes.eligibleSceneProbeCount - probes.selectedProbeCount
                    : 0u;

            for (u32 index = 0; index < probes.selectedProbeCount; ++index) {
                const ReflectionProbeCandidate& selected = candidates[index];
                probes.selectedProbes[index] = selected.probe;
                probes.totalBlendWeight += selected.blendWeight;
                probes.maxBlendWeight =
                    std::max(probes.maxBlendWeight, selected.blendWeight);
                probes.boxProjectionEnabled =
                    probes.boxProjectionEnabled ||
                    ReflectionProbeBoxProjectionEnabled(selected.probe);
                SetSelectedReflectionProbeCaptureDiagnostics(
                    probes,
                    index,
                    selected.probe,
                    m_ShadowSettings.reflectionProbeCubemapEnabled,
                    builtInCubemapReady,
                    reflectionProbeDescriptorSetsBound
                );
            }

            probes.localProbe = probes.selectedProbes[0];
            probes.selectedSceneProbeIndex = probes.localProbe.sceneIndex;
            probes.parallaxCorrectionEnabled = probes.boxProjectionEnabled;
            probes.captureSource = probes.localProbe.captureSource;
            probes.captureResourceReady = probes.selectedCaptureResourceReady[0];
            probes.captureDescriptorBound = probes.selectedCaptureDescriptorBound[0];
            probes.captureFallbackReason =
                probes.selectedCaptureFallbackReasons[0];
            return probes;
        }
    }

    RendererReflectionProbe settingsProbe =
        SettingsReflectionProbe(m_ShadowSettings);
    if (ReflectionProbeContributes(settingsProbe)) {
        probes.localProbe = settingsProbe;
        probes.selectedProbes[0] = settingsProbe;
        probes.selectedProbeCount = 1;
        probes.activeLocalProbeCount = 1;
        probes.blendedProbeCount = 1;
        probes.multiBlendEnabled = true;
        probes.maxBlendWeight = settingsProbe.blendStrength;
        probes.totalBlendWeight = settingsProbe.blendStrength;
        SetSelectedReflectionProbeCaptureDiagnostics(
            probes,
            0u,
            settingsProbe,
            m_ShadowSettings.reflectionProbeCubemapEnabled,
            builtInCubemapReady,
            reflectionProbeDescriptorSetsBound
        );
        probes.captureSource = settingsProbe.captureSource;
        probes.captureResourceReady = probes.selectedCaptureResourceReady[0];
        probes.captureDescriptorBound = probes.selectedCaptureDescriptorBound[0];
        probes.captureFallbackReason =
            probes.selectedCaptureFallbackReasons[0];
    }
    return probes;
}

FrameMaterialSet VulkanRenderer::BuildFrameMaterialSet(
    std::span<const RenderCommand> renderCommands
) const {
    FrameMaterialSet materials{};
    materials.materialData.materialCounts = glm::vec4(
        0.0f,
        static_cast<f32>(kMaxFrameMaterials),
        0.0f,
        0.0f
    );

    for (const RenderCommand& command : renderCommands) {
        if (command.material == nullptr) {
            continue;
        }
        if (materials.materialIds.find(command.material) != materials.materialIds.end()) {
            continue;
        }
        if (materials.count >= kMaxFrameMaterials) {
            ++materials.overflowCount;
            continue;
        }

        const u32 materialId = materials.count + 1;
        materials.materialIds.emplace(command.material, materialId);

        GpuMaterialRecord& record = materials.materialData.materials[materials.count];
        record.baseColorFactor = command.materialPushConstants.materialBaseColorFactor;
        record.materialControls = command.materialPushConstants.materialControls;
        record.materialControls.w = static_cast<f32>(materialId);
        const MaterialProperties& properties = command.material->Properties();
        record.materialCustom = glm::vec4(
            properties.volumeAttenuationColor[0],
            properties.volumeAttenuationColor[1],
            properties.volumeAttenuationColor[2],
            std::clamp(properties.transmissionFactor, 0.0f, 1.0f)
        );
        record.cameraControls = command.materialPushConstants.cameraControls;
        record.pbrFactors = glm::vec4(
            properties.pbrFactors[0],
            properties.pbrFactors[1],
            AlphaModeValue(properties.alphaMode),
            std::clamp(properties.alphaCutoff, 0.0f, 1.0f)
        );
        record.emissiveFactor = glm::vec4(
            properties.emissiveFactor[0],
            properties.emissiveFactor[1],
            properties.emissiveFactor[2],
            std::clamp(properties.clearcoatRoughness, 0.0f, 1.0f)
        );
        record.specularFactor = glm::vec4(
            properties.specularFactor[0],
            properties.specularFactor[1],
            properties.specularFactor[2],
            properties.specularFactor[3]
        );
        record.uvTransform = glm::vec4(
            properties.uvTransform[0],
            properties.uvTransform[1],
            properties.uvTransform[2],
            properties.uvTransform[3]
        );
        record.uvControls = glm::vec4(
            properties.uvControls[0],
            properties.uvControls[1],
            properties.doubleSided ? 1.0f : properties.uvControls[2],
            std::clamp(properties.clearcoatFactor, 0.0f, 1.0f)
        );
        record.volumeFactor = glm::vec4(
            std::clamp(properties.volumeThicknessFactor, 0.0f, 64.0f),
            std::clamp(properties.volumeAttenuationDistance, 0.0f, 1000000.0f),
            0.0f,
            properties.volumeThicknessFactor > 0.001f ? 1.0f : 0.0f
        );

        const f32 alpha = std::clamp(record.baseColorFactor.a, 0.0f, 1.0f);
        const f32 textureFlags = std::max(record.cameraControls.w, 0.0f);
        const bool hasAlbedoTexture = record.materialControls.x > 0.001f;
        const bool hasAuxTexture = record.cameraControls.z > 0.001f;
        const bool hasNormalTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagNormal);
        const bool hasOcclusionTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagOcclusion);
        const bool hasEmissiveTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagEmissive);
        const bool hasOpacityTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagOpacity);
        const bool hasSpecularTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagSpecular);
        const bool hasClearcoatTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagClearcoat);
        const bool hasTransmissionTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagTransmission);
        const bool hasClearcoatRoughnessTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagClearcoatRoughness);
        const f32 maxEmissiveFactor = std::max(
            record.emissiveFactor.r,
            std::max(record.emissiveFactor.g, record.emissiveFactor.b)
        );
        const bool hasEmissiveFactor = maxEmissiveFactor > 0.001f;
        const bool hasSpecularFactor =
            std::abs(record.specularFactor.r - 1.0f) > 0.001f ||
            std::abs(record.specularFactor.g - 1.0f) > 0.001f ||
            std::abs(record.specularFactor.b - 1.0f) > 0.001f ||
            std::abs(record.specularFactor.a - 1.0f) > 0.001f;
        const bool hasAnyTexture = hasAlbedoTexture ||
            hasAuxTexture ||
            hasNormalTexture ||
            hasOcclusionTexture ||
            hasEmissiveTexture ||
            hasOpacityTexture ||
            hasSpecularTexture ||
            hasClearcoatTexture ||
            hasTransmissionTexture ||
            hasClearcoatRoughnessTexture;
        const bool alphaMask = properties.alphaMode == MaterialAlphaMode::Mask;
        const bool alphaBlend = properties.alphaMode == MaterialAlphaMode::Blend;
        const bool hasUvTransform =
            record.uvControls.y > 0.5f ||
            std::abs(record.uvTransform.x) > 0.0001f ||
            std::abs(record.uvTransform.y) > 0.0001f ||
            std::abs(record.uvTransform.z - 1.0f) > 0.0001f ||
            std::abs(record.uvTransform.w - 1.0f) > 0.0001f ||
            std::abs(record.uvControls.x) > 0.0001f;
        const bool doubleSided = properties.doubleSided;
        const bool hasClearcoat = properties.clearcoatFactor > 0.001f;
        const bool hasTransmission = properties.transmissionFactor > 0.001f;
        const bool hasVolume = properties.volumeThicknessFactor > 0.001f;
        MaterialRenderClass materialRenderClass = command.material != nullptr
            ? properties.renderClass
            : MaterialRenderClass::DeferredOpaque;
        if (alphaBlend || (!alphaMask && alpha < 0.999f)) {
            materialRenderClass = MaterialRenderClass::Transparent;
        }
        const bool transparentCandidate =
            materialRenderClass == MaterialRenderClass::Transparent;
        const bool forwardSpecial =
            materialRenderClass == MaterialRenderClass::ForwardSpecial;
        const bool emissiveHint = hasEmissiveTexture || hasEmissiveFactor;
        const bool specularHint = hasSpecularTexture || hasSpecularFactor;
        record.materialFlags = glm::vec4(
            RenderClassValue(materialRenderClass),
            textureFlags,
            emissiveHint ? 1.0f : 0.0f,
            alpha
        );

        if (transparentCandidate) {
            ++materials.transparentCount;
        } else if (forwardSpecial) {
            ++materials.forwardSpecialCount;
        } else {
            ++materials.opaqueCount;
        }
        if (emissiveHint) {
            ++materials.emissiveHintCount;
        }
        if (specularHint) {
            ++materials.specularHintCount;
        }
        if (hasSpecularTexture) {
            ++materials.specularTextureCount;
        }
        if (alphaMask) {
            ++materials.alphaMaskCount;
        }
        if (alphaBlend) {
            ++materials.alphaBlendCount;
        }
        if (hasUvTransform) {
            ++materials.uvTransformCount;
        }
        if (doubleSided) {
            ++materials.doubleSidedCount;
        }
        if (hasClearcoat) {
            ++materials.clearcoatCount;
        }
        if (hasClearcoatTexture) {
            ++materials.clearcoatTextureCount;
        }
        if (hasClearcoatRoughnessTexture) {
            ++materials.clearcoatRoughnessTextureCount;
        }
        if (hasTransmission) {
            ++materials.transmissionCount;
        }
        if (hasTransmissionTexture) {
            ++materials.transmissionTextureCount;
        }
        if (hasVolume) {
            ++materials.volumeCount;
        }
        if (hasOpacityTexture) {
            ++materials.opacityTextureCount;
        }
        if (hasAnyTexture) {
            ++materials.texturedCount;
        }

        ++materials.count;
    }

    materials.materialData.materialCounts = glm::vec4(
        static_cast<f32>(materials.count),
        static_cast<f32>(kMaxFrameMaterials),
        static_cast<f32>(materials.overflowCount),
        static_cast<f32>(materials.transparentCount)
    );
    return materials;
}

void VulkanRenderer::BuildGBufferCommandList(
    std::span<const RenderCommand> renderCommands,
    std::vector<RenderCommand>& gBufferCommands,
    std::vector<RenderCommand>& weightedTranslucencyCommands,
    std::vector<RenderCommand>& forwardResidualCommands,
    const FrameMatrices* matrices,
    bool recordTransparentAlphaReference,
    RendererDrawStats& drawStats
) const {
    gBufferCommands.clear();
    gBufferCommands.reserve(renderCommands.size());
    weightedTranslucencyCommands.clear();
    weightedTranslucencyCommands.reserve(renderCommands.size());
    forwardResidualCommands.clear();
    forwardResidualCommands.reserve(renderCommands.size());

    for (const RenderCommand& command : renderCommands) {
        const u64 triangles = TriangleCountForCommand(command);
        switch (RenderClassForCommand(command)) {
        case MaterialRenderClass::Transparent:
            weightedTranslucencyCommands.push_back(command);
            ++drawStats.hybridForwardTransparentDraws;
            ++drawStats.hybridWeightedTranslucencyDraws;
            drawStats.hybridWeightedTranslucencyTriangles += triangles;
            if (recordTransparentAlphaReference) {
                ++drawStats.hybridForwardResidualDraws;
                drawStats.hybridForwardResidualTriangles += triangles;
            }
            break;
        case MaterialRenderClass::ForwardSpecial:
            forwardResidualCommands.push_back(command);
            ++drawStats.hybridForwardSpecialDraws;
            ++drawStats.hybridForwardResidualDraws;
            drawStats.hybridForwardResidualTriangles += triangles;
            break;
        case MaterialRenderClass::DeferredOpaque:
        default:
            gBufferCommands.push_back(command);
            ++drawStats.hybridDeferredOpaqueDraws;
            drawStats.hybridDeferredOpaqueTriangles += triangles;
            break;
        }
    }

    const glm::vec3 cameraPosition = CameraPositionFromMatrices(matrices);
    const bool hasCameraMatrices = matrices != nullptr;
    auto sortResidualCommands = [&](std::vector<RenderCommand>& commands) {
        std::vector<std::size_t> sortedIndices(commands.size());
        for (std::size_t index = 0; index < sortedIndices.size(); ++index) {
            sortedIndices[index] = index;
        }

        std::sort(
            sortedIndices.begin(),
            sortedIndices.end(),
            [&](std::size_t lhsIndex, std::size_t rhsIndex) {
                const RenderCommand& lhs = commands[lhsIndex];
                const RenderCommand& rhs = commands[rhsIndex];
                if (lhs.drawOrder != rhs.drawOrder) {
                    return lhs.drawOrder < rhs.drawOrder;
                }

                const MaterialRenderClass lhsClass = RenderClassForCommand(lhs);
                const MaterialRenderClass rhsClass = RenderClassForCommand(rhs);
                const bool lhsTransparent = lhsClass == MaterialRenderClass::Transparent;
                const bool rhsTransparent = rhsClass == MaterialRenderClass::Transparent;
                if (lhsTransparent != rhsTransparent) {
                    return lhsTransparent;
                }
                if (hasCameraMatrices && lhsTransparent && rhsTransparent) {
                    const f32 lhsDistance = DistanceSquaredToCamera(lhs, cameraPosition);
                    const f32 rhsDistance = DistanceSquaredToCamera(rhs, cameraPosition);
                    if (std::abs(lhsDistance - rhsDistance) > 0.0001f) {
                        return lhsDistance > rhsDistance;
                    }
                }

                return lhsIndex < rhsIndex;
            }
        );

        std::vector<RenderCommand> sortedCommands;
        sortedCommands.reserve(commands.size());
        for (std::size_t sortedIndex : sortedIndices) {
            sortedCommands.push_back(commands[sortedIndex]);
        }
        commands = std::move(sortedCommands);
    };

    if (!weightedTranslucencyCommands.empty()) {
        sortResidualCommands(weightedTranslucencyCommands);
        ++drawStats.hybridWeightedTranslucencySortOps;
        drawStats.hybridWeightedTranslucencySortedTransparentDraws =
            drawStats.hybridForwardTransparentDraws;
    }

    if (recordTransparentAlphaReference && !weightedTranslucencyCommands.empty()) {
        forwardResidualCommands.insert(
            forwardResidualCommands.begin(),
            weightedTranslucencyCommands.begin(),
            weightedTranslucencyCommands.end()
        );
    }

    if (!forwardResidualCommands.empty()) {
        sortResidualCommands(forwardResidualCommands);
        ++drawStats.hybridForwardResidualSortOps;
        drawStats.hybridForwardResidualSortedTransparentDraws =
            recordTransparentAlphaReference ? drawStats.hybridForwardTransparentDraws : 0u;
        drawStats.hybridForwardResidualStableSpecialDraws =
            drawStats.hybridForwardSpecialDraws;
    }

    drawStats.gBufferDraws = static_cast<u32>(gBufferCommands.size());
    drawStats.gBufferTriangles = drawStats.hybridDeferredOpaqueTriangles;
}

glm::mat4 VulkanRenderer::LightViewProjection(
    std::span<const RenderCommand> renderCommands,
    const FrameLightSet& lights
) const {
    const glm::vec3 lightDirection = NormalizedDirectionalLightDirection(lights);

    glm::vec3 worldBoundsMin{ std::numeric_limits<f32>::max() };
    glm::vec3 worldBoundsMax{ std::numeric_limits<f32>::lowest() };
    bool hasBounds = false;
    for (const RenderCommand& command : renderCommands) {
        IncludeCommandBounds(command, worldBoundsMin, worldBoundsMax, hasBounds);
    }

    if (!hasBounds) {
        worldBoundsMin = glm::vec3(-kShadowMinHalfExtent);
        worldBoundsMax = glm::vec3(kShadowMinHalfExtent);
    }

    const glm::vec3 center = (worldBoundsMin + worldBoundsMax) * 0.5f;
    const glm::vec3 sceneExtent = worldBoundsMax - worldBoundsMin;
    const f32 sceneRadius = std::max(glm::length(sceneExtent) * 0.5f, kShadowMinHalfExtent);

    const glm::vec3 lightForward = glm::normalize(lightDirection);
    const glm::vec3 eye = center - lightForward * (sceneRadius + kShadowDepthPadding);
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };
    if (std::abs(glm::dot(lightForward, up)) > 0.95f) {
        up = { 0.0f, 0.0f, 1.0f };
    }

    const glm::mat4 view = glm::lookAt(eye, center, up);
    glm::vec3 lightBoundsMin{ std::numeric_limits<f32>::max() };
    glm::vec3 lightBoundsMax{ std::numeric_limits<f32>::lowest() };
    for (const RenderCommand& command : renderCommands) {
        if (!command.worldBounds.valid) {
            continue;
        }

        for (const glm::vec3& worldPoint : command.worldBounds.corners) {
            IncludePoint(
                glm::vec3(view * glm::vec4(worldPoint, 1.0f)),
                lightBoundsMin,
                lightBoundsMax
            );
        }
    }

    if (!hasBounds) {
        lightBoundsMin = glm::vec3(-kShadowMinHalfExtent);
        lightBoundsMax = glm::vec3(kShadowMinHalfExtent);
    }

    const f32 xPadding = std::max(
        (lightBoundsMax.x - lightBoundsMin.x) * kShadowPaddingRatio,
        0.35f
    );
    const f32 yPadding = std::max(
        (lightBoundsMax.y - lightBoundsMin.y) * kShadowPaddingRatio,
        0.35f
    );
    lightBoundsMin.x -= xPadding;
    lightBoundsMax.x += xPadding;
    lightBoundsMin.y -= yPadding;
    lightBoundsMax.y += yPadding;
    lightBoundsMin.z -= kShadowDepthPadding;
    lightBoundsMax.z += kShadowDepthPadding;
    ExpandRangeAroundCenter(lightBoundsMin.x, lightBoundsMax.x, kShadowMinHalfExtent);
    ExpandRangeAroundCenter(lightBoundsMin.y, lightBoundsMax.y, kShadowMinHalfExtent);

    const f32 nearPlane = std::max(0.01f, -lightBoundsMax.z);
    const f32 farPlane = std::max(nearPlane + 0.1f, -lightBoundsMin.z);
    glm::mat4 projection = glm::ortho(
        lightBoundsMin.x,
        lightBoundsMax.x,
        lightBoundsMin.y,
        lightBoundsMax.y,
        nearPlane,
        farPlane
    );
    projection[1][1] *= -1.0f;
    return projection * view;
}

DirectionalShadowCascadeSet VulkanRenderer::BuildDirectionalShadowCascades(
    std::span<const RenderCommand> renderCommands,
    const FrameLightSet& lights,
    const FrameMatrices* matrices,
    bool shadowSamplingEnabled
) const {
    DirectionalShadowCascadeSet cascadeSet{};
    if (!m_ShadowSettings.enabled || !shadowSamplingEnabled) {
        return cascadeSet;
    }
    if (matrices == nullptr) {
        return cascadeSet;
    }

    const u32 requestedCount = m_ShadowSettings.cascadesEnabled
        ? m_ShadowSettings.cascadeCount
        : 1u;
    const u32 configuredCount = std::clamp<u32>(
        requestedCount,
        1u,
        static_cast<u32>(kMaxDirectionalShadowCascades)
    );
    cascadeSet.configuredCount = configuredCount;
    cascadeSet.stableSnappingEnabled = m_ShadowSettings.stableCascades;
    cascadeSet.splitLambda = std::clamp(m_ShadowSettings.cascadeSplitLambda, 0.0f, 1.0f);
    cascadeSet.maxDistance = std::max(m_ShadowSettings.cascadeMaxDistance, 0.0f);

    f32 nearDepth = 0.0f;
    f32 farDepth = 0.0f;
    if (!CameraDepthRangeFromMatrices(matrices, nearDepth, farDepth)) {
        return cascadeSet;
    }

    if (cascadeSet.maxDistance > nearDepth + 0.1f) {
        farDepth = std::min(farDepth, cascadeSet.maxDistance);
    }
    farDepth = std::max(farDepth, nearDepth + 0.1f);
    cascadeSet.nearDepth = nearDepth;
    cascadeSet.farDepth = farDepth;

    f32 previousSplit = nearDepth;
    for (u32 cascadeIndex = 0; cascadeIndex < configuredCount; ++cascadeIndex) {
        const f32 ratio =
            static_cast<f32>(cascadeIndex + 1) / static_cast<f32>(configuredCount);
        const f32 logSplit = nearDepth *
            std::pow(std::max(farDepth / nearDepth, 1.0f), ratio);
        const f32 uniformSplit = nearDepth + (farDepth - nearDepth) * ratio;
        const f32 splitDepth = cascadeIndex + 1 == configuredCount
            ? farDepth
            : cascadeSet.splitLambda * logSplit +
                (1.0f - cascadeSet.splitLambda) * uniformSplit;

        std::array<glm::vec3, 8> corners{};
        if (!CameraSliceCornersFromMatrices(*matrices, previousSplit, splitDepth, corners)) {
            break;
        }

        DirectionalShadowCascade& cascade = cascadeSet.cascades[cascadeIndex];
        cascade.nearDepth = previousSplit;
        cascade.farDepth = splitDepth;
        cascade.splitDepth = splitDepth;
        cascade.viewProjection = LightViewProjectionForCascade(
            renderCommands,
            lights,
            corners,
            cascadeSet.stableSnappingEnabled,
            std::max(m_ShadowSettings.mapSize, 1u),
            &cascade.texelWorldSize
        );
        ++cascadeSet.activeCount;
        previousSplit = splitDepth;
    }

    return cascadeSet;
}

LocalShadowTileSet VulkanRenderer::BuildLocalShadowTiles(
    const FrameLightSet& lights,
    std::span<const RenderCommand> shadowCommands,
    u32 atlasTileCapacity,
    const LocalShadowCacheState* cacheState
) const {
    LocalShadowTileSet tileSet{};
    tileSet.tileCapacity = std::min<u32>(
        atlasTileCapacity,
        static_cast<u32>(tileSet.tiles.size())
    );
    if (m_LocalShadowAtlas != nullptr) {
        tileSet.tileSize = m_LocalShadowAtlas->TileSize();
        tileSet.tileColumns = m_LocalShadowAtlas->TileColumns();
        tileSet.tileRows = m_LocalShadowAtlas->TileRows();
    }

    static constexpr std::array<glm::vec3, 6> kPointFaceDirections{
        glm::vec3{ 1.0f, 0.0f, 0.0f },
        glm::vec3{ -1.0f, 0.0f, 0.0f },
        glm::vec3{ 0.0f, 1.0f, 0.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, 0.0f, 1.0f },
        glm::vec3{ 0.0f, 0.0f, -1.0f }
    };
    static constexpr std::array<glm::vec3, 6> kPointFaceUps{
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, 0.0f, 1.0f },
        glm::vec3{ 0.0f, 0.0f, -1.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f }
    };

    const u32 localCount = std::min<u32>(
        lights.localCount,
        static_cast<u32>(lights.localLights.size())
    );
    for (u32 index = 0; index < localCount; ++index) {
        const RendererLocalLight& light = lights.localLights[index];
        const f32 farPlane = std::max(light.radius, kLocalShadowNearPlane + 0.1f);
        const bool allowCacheReuse = cacheState != nullptr && cacheState->valid;
        if (light.kind == RendererLightKind::Point) {
            ++tileSet.pointLightCount;
            tileSet.pointFaceTiles += 6u;
            for (std::size_t faceIndex = 0; faceIndex < kPointFaceDirections.size(); ++faceIndex) {
                const u64 casterSignature = LocalShadowCasterSignature(
                    shadowCommands,
                    light,
                    &kPointFaceDirections[faceIndex]
                );
                const u64 cacheKey = LocalShadowTileCacheKey(
                    light,
                    index,
                    static_cast<u32>(faceIndex),
                    casterSignature
                );
                AddLocalShadowTile(
                    tileSet,
                    LocalShadowViewProjection(
                        light.position,
                        kPointFaceDirections[faceIndex],
                        kPointFaceUps[faceIndex],
                        glm::radians(90.0f),
                        farPlane
                    ),
                    index,
                    static_cast<u32>(faceIndex),
                    light.kind,
                    cacheKey,
                    allowCacheReuse &&
                    cacheState != nullptr &&
                    LocalShadowTileCacheReusable(
                        cacheState->tileKeys,
                        cacheState->tileCount,
                        cacheKey,
                        tileSet.assignedCount
                    )
                );
            }
        } else if (light.kind == RendererLightKind::Spot) {
            ++tileSet.spotLightCount;
            ++tileSet.spotTiles;
            const u64 casterSignature = LocalShadowCasterSignature(shadowCommands, light);
            const f32 outerConeCos = std::clamp(light.outerConeCos, 0.0f, 0.999f);
            const f32 outerConeRadians = std::acos(outerConeCos);
            const f32 spotFov = std::clamp(
                outerConeRadians * 2.0f,
                glm::radians(5.0f),
                glm::radians(175.0f)
            );
            const u64 cacheKey = LocalShadowTileCacheKey(light, index, 0u, casterSignature);
            AddLocalShadowTile(
                tileSet,
                LocalShadowViewProjection(
                    light.position,
                    light.direction,
                    { 0.0f, 1.0f, 0.0f },
                    spotFov,
                    farPlane
                ),
                index,
                0u,
                light.kind,
                cacheKey,
                allowCacheReuse &&
                cacheState != nullptr &&
                LocalShadowTileCacheReusable(
                    cacheState->tileKeys,
                    cacheState->tileCount,
                    cacheKey,
                    tileSet.assignedCount
                )
            );
        }
    }

    if (tileSet.assignedCount > 0 && tileSet.cacheHitTiles > 0) {
        tileSet.cacheSkippedTiles = tileSet.cacheHitTiles;
    }

    return tileSet;
}

bool VulkanRenderer::LocalReflectionProbeCubemapReady() const {
    return m_ReflectionProbeResources.BuiltInProceduralReady(m_IblSampler);
}

u32 VulkanRenderer::UpdateEnvironmentDescriptorSets(
    VulkanDescriptorSets* descriptorSets
) const {
    if (descriptorSets == nullptr ||
        m_IblSampler == VK_NULL_HANDLE ||
        m_IblBrdfImage == nullptr ||
        m_IblBrdfImage->View() == VK_NULL_HANDLE ||
        m_IblIrradianceView == VK_NULL_HANDLE ||
        m_IblPrefilteredView == VK_NULL_HANDLE) {
        return 0;
    }

    const VkImageView localReflectionProbeView =
        m_ReflectionProbeResources.DescriptorViewFor(
            m_IblPrefilteredView,
            m_IblSampler
        );
    u32 localProbeDescriptorWrites = 0;
    for (std::size_t index = 0; index < descriptorSets->Count(); ++index) {
        VkDescriptorImageInfo brdfInfo{};
        brdfInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        brdfInfo.imageView = m_IblBrdfImage->View();
        brdfInfo.sampler = m_IblSampler;

        VkDescriptorImageInfo irradianceInfo{};
        irradianceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        irradianceInfo.imageView = m_IblIrradianceView;
        irradianceInfo.sampler = m_IblSampler;

        VkDescriptorImageInfo prefilteredInfo{};
        prefilteredInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        prefilteredInfo.imageView = m_IblPrefilteredView;
        prefilteredInfo.sampler = m_IblSampler;

        VkDescriptorImageInfo localReflectionProbeInfo{};
        localReflectionProbeInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        localReflectionProbeInfo.imageView = localReflectionProbeView;
        localReflectionProbeInfo.sampler = m_IblSampler;

        std::array<VkWriteDescriptorSet, 4> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets->Handle(index);
        descriptorWrites[0].dstBinding = 6;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &brdfInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets->Handle(index);
        descriptorWrites[1].dstBinding = 7;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &irradianceInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSets->Handle(index);
        descriptorWrites[2].dstBinding = 8;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &prefilteredInfo;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptorSets->Handle(index);
        descriptorWrites[3].dstBinding = 11;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pImageInfo = &localReflectionProbeInfo;

        vkUpdateDescriptorSets(
            m_Device.Handle(),
            static_cast<u32>(descriptorWrites.size()),
            descriptorWrites.data(),
            0,
            nullptr
        );
        if (LocalReflectionProbeCubemapReady()) {
            ++localProbeDescriptorWrites;
        }
    }

    return localProbeDescriptorWrites;
}

std::span<const RenderCommand> VulkanRenderer::ShadowRenderCommands() const {
    return m_ShadowRenderQueue.Commands();
}

const VulkanDescriptorSets* VulkanRenderer::ShadowDescriptorSets() const {
    if (m_OverlayScene3D != nullptr &&
        !m_ShadowRenderQueue.Empty() &&
        m_OverlayDescriptorSets != nullptr) {
        return m_OverlayDescriptorSets.get();
    }

    if (m_PipelineSpec.vertexLayout == VertexLayout::Vertex3D) {
        return m_DescriptorSets.get();
    }

    return nullptr;
}

bool VulkanRenderer::BuildMainInstanceBatches(
    std::span<const RenderCommand> commands,
    bool allowCacheReuse
) {
    if (allowCacheReuse && m_MainInstanceBatchesCacheValid) {
        return true;
    }

    m_MainInstances.clear();
    m_MainInstanceBatches.clear();
    m_MainInstanceSignature = 0x2fcd2adf1379a1b9ull;
    if (m_InstancedGraphicsPipeline == nullptr || m_InstanceBuffer == nullptr) {
        m_MainInstanceBatchesCacheValid = true;
        return false;
    }

    std::size_t commandIndex = 0;
    while (commandIndex < commands.size()) {
        const RenderCommand& firstCommand = commands[commandIndex];
        std::size_t endIndex = commandIndex + 1;
        while (endIndex < commands.size()) {
            const RenderCommand& candidate = commands[endIndex];
            if (candidate.mesh != firstCommand.mesh ||
                candidate.material != firstCommand.material ||
                candidate.drawOrder != firstCommand.drawOrder ||
                candidate.tint != firstCommand.tint) {
                break;
            }
            ++endIndex;
        }

        const std::size_t commandCount = endIndex - commandIndex;
        if (commandCount > 1) {
            RenderInstanceBatch batch{};
            batch.firstCommandIndex = commandIndex;
            batch.commandCount = static_cast<u32>(commandCount);
            batch.firstInstance = static_cast<u32>(m_MainInstances.size());
            m_MainInstanceBatches.push_back(batch);

            for (std::size_t index = commandIndex; index < endIndex; ++index) {
                m_MainInstances.push_back(Instance3D{ commands[index].model });
                m_MainInstanceSignature = HashMatrix(
                    m_MainInstanceSignature,
                    commands[index].model
                );
            }
        }

        commandIndex = endIndex;
    }

    m_MainInstanceSignature = HashCombine(
        m_MainInstanceSignature,
        static_cast<u64>(m_MainInstances.size())
    );
    m_MainInstanceSignature = HashCombine(
        m_MainInstanceSignature,
        static_cast<u64>(m_MainInstanceBatches.size())
    );
    m_MainInstanceBatchesCacheValid = true;
    return false;
}

bool VulkanRenderer::UploadMainInstancesIfNeeded(std::size_t imageIndex) {
    if (m_InstanceBuffer == nullptr) {
        return false;
    }

    if (m_MainInstanceUploadSignatures.size() <= imageIndex) {
        m_MainInstanceUploadSignatures.resize(imageIndex + 1, 0);
    }

    if (m_MainInstanceUploadSignatures[imageIndex] == m_MainInstanceSignature) {
        return false;
    }

    m_InstanceBuffer->Update(
        m_Device,
        m_PhysicalDevice,
        imageIndex,
        m_MainInstances
    );
    m_MainInstanceUploadSignatures[imageIndex] = m_MainInstanceSignature;
    return true;
}

}
