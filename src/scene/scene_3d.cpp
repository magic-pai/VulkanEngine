#include "scene/scene_3d.h"

#include "scene/renderable_3d.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>

namespace se {

namespace {

constexpr f32 kRayEpsilon = 0.000001f;
constexpr f32 kLocalBoundsMin = -0.5f;
constexpr f32 kLocalBoundsMax = 0.5f;

bool IsFinite(const glm::vec3& value) {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool LightEditIsFinite(const SceneLightEdit& edit) {
    return IsFinite(edit.position) &&
        IsFinite(edit.direction) &&
        IsFinite(edit.color) &&
        std::isfinite(edit.intensity) &&
        std::isfinite(edit.radius) &&
        std::isfinite(edit.sourceRadius) &&
        std::isfinite(edit.innerConeDegrees) &&
        std::isfinite(edit.outerConeDegrees) &&
        std::isfinite(edit.width) &&
        std::isfinite(edit.height) &&
        std::isfinite(edit.ambient) &&
        std::isfinite(edit.specular) &&
        std::isfinite(edit.angularRadiusRadians);
}

glm::vec3 NormalizedLightDirection(glm::vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        return { 0.0f, -1.0f, 0.0f };
    }

    return glm::normalize(direction);
}

const char* DefaultLightName(SceneLightKind kind) {
    switch (kind) {
    case SceneLightKind::Directional:
        return "Directional Light";
    case SceneLightKind::Point:
        return "Point Light";
    case SceneLightKind::Spot:
        return "Spot Light";
    case SceneLightKind::Rect:
        return "Rect Light";
    }

    return "Light";
}

void SanitizeLightEdit(SceneLightEdit& edit) {
    if (edit.name.empty()) {
        edit.name = DefaultLightName(edit.kind);
    }
    edit.direction = NormalizedLightDirection(edit.direction);
    edit.color = glm::max(edit.color, glm::vec3(0.0f));
    edit.intensity = std::max(edit.intensity, 0.0f);
    edit.radius = std::max(edit.radius, 0.0f);
    edit.sourceRadius = std::max(edit.sourceRadius, 0.0f);
    edit.outerConeDegrees = std::clamp(edit.outerConeDegrees, 0.1f, 89.0f);
    edit.innerConeDegrees = std::clamp(
        edit.innerConeDegrees,
        0.05f,
        edit.outerConeDegrees
    );
    edit.width = std::max(edit.width, 0.0f);
    edit.height = std::max(edit.height, 0.0f);
    edit.ambient = std::max(edit.ambient, 0.0f);
    edit.specular = std::max(edit.specular, 0.0f);
    edit.angularRadiusRadians = std::clamp(edit.angularRadiusRadians, 0.0f, 0.05f);
}

bool SameLightEdit(const SceneLightEdit& left, const SceneLightEdit& right) {
    constexpr f32 epsilon = 0.000001f;
    const auto sameFloat = [epsilon](f32 a, f32 b) {
        return std::abs(a - b) <= epsilon;
    };
    const auto sameVec3 = [&sameFloat](const glm::vec3& a, const glm::vec3& b) {
        return sameFloat(a.x, b.x) &&
            sameFloat(a.y, b.y) &&
            sameFloat(a.z, b.z);
    };

    return left.kind == right.kind &&
        left.name == right.name &&
        left.enabled == right.enabled &&
        sameVec3(left.position, right.position) &&
        sameVec3(left.direction, right.direction) &&
        sameVec3(left.color, right.color) &&
        sameFloat(left.intensity, right.intensity) &&
        sameFloat(left.radius, right.radius) &&
        sameFloat(left.sourceRadius, right.sourceRadius) &&
        sameFloat(left.innerConeDegrees, right.innerConeDegrees) &&
        sameFloat(left.outerConeDegrees, right.outerConeDegrees) &&
        sameFloat(left.width, right.width) &&
        sameFloat(left.height, right.height) &&
        sameFloat(left.ambient, right.ambient) &&
        sameFloat(left.specular, right.specular) &&
        sameFloat(left.angularRadiusRadians, right.angularRadiusRadians);
}

bool SameEnvironment(
    const SceneEnvironment3D& left,
    const SceneEnvironment3D& right
) {
    constexpr f32 epsilon = 0.000001f;
    return left.iblEnabled == right.iblEnabled &&
        std::abs(left.diffuseIntensity - right.diffuseIntensity) <= epsilon &&
        std::abs(left.specularIntensity - right.specularIntensity) <= epsilon &&
        std::abs(left.horizonBlend - right.horizonBlend) <= epsilon &&
        left.skyboxEnabled == right.skyboxEnabled &&
        std::abs(left.skyboxIntensity - right.skyboxIntensity) <= epsilon &&
        std::abs(left.skyboxBlur - right.skyboxBlur) <= epsilon &&
        left.lightingAsset == right.lightingAsset;
}

SceneEnvironment3D SanitizedEnvironment(SceneEnvironment3D environment) {
    environment.diffuseIntensity = std::isfinite(environment.diffuseIntensity)
        ? std::clamp(environment.diffuseIntensity, 0.0f, 4.0f)
        : 1.0f;
    environment.specularIntensity = std::isfinite(environment.specularIntensity)
        ? std::clamp(environment.specularIntensity, 0.0f, 4.0f)
        : 1.0f;
    environment.horizonBlend = std::isfinite(environment.horizonBlend)
        ? std::clamp(environment.horizonBlend, 0.0f, 1.0f)
        : 0.22f;
    environment.skyboxIntensity = std::isfinite(environment.skyboxIntensity)
        ? std::clamp(environment.skyboxIntensity, 0.0f, 4.0f)
        : 1.0f;
    environment.skyboxBlur = std::isfinite(environment.skyboxBlur)
        ? std::clamp(environment.skyboxBlur, 0.0f, 8.0f)
        : 0.0f;
    return environment;
}

void WrapDegrees(f32& degrees) {
    while (degrees > 180.0f) {
        degrees -= 360.0f;
    }

    while (degrees < -180.0f) {
        degrees += 360.0f;
    }
}

bool IntersectUnitBounds(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    f32& hitDistance
) {
    f32 nearHit = 0.0f;
    f32 farHit = std::numeric_limits<f32>::max();

    for (int axis = 0; axis < 3; ++axis) {
        const f32 origin = rayOrigin[axis];
        const f32 direction = rayDirection[axis];

        if (std::abs(direction) <= kRayEpsilon) {
            if (origin < kLocalBoundsMin || origin > kLocalBoundsMax) {
                return false;
            }

            continue;
        }

        f32 axisNear = (kLocalBoundsMin - origin) / direction;
        f32 axisFar = (kLocalBoundsMax - origin) / direction;
        if (axisNear > axisFar) {
            std::swap(axisNear, axisFar);
        }

        nearHit = std::max(nearHit, axisNear);
        farHit = std::min(farHit, axisFar);

        if (nearHit > farHit) {
            return false;
        }
    }

    hitDistance = nearHit;
    return farHit >= 0.0f;
}

}

Scene3D::Scene3D() = default;

Scene3D::~Scene3D() = default;

Renderable3D& Scene3D::CreateRenderable(
    std::string name,
    std::string meshId,
    std::string materialId
) {
    Renderable3D& renderable = m_Storage.CreateRenderable(
        std::move(name),
        std::move(meshId),
        std::move(materialId)
    );
    renderable.SetRenderChangeCallback([this]() {
        MarkRenderChanged();
    });
    MarkMembershipChanged();

    return renderable;
}

PointLight3D& Scene3D::CreatePointLight(
    std::string name,
    glm::vec3 position,
    f32 radius,
    glm::vec3 color,
    f32 intensity,
    f32 sourceRadius
) {
    m_PointLights.push_back(PointLight3D{
        std::move(name),
        position,
        std::max(radius, 0.0f),
        glm::max(color, glm::vec3(0.0f)),
        std::max(intensity, 0.0f),
        std::max(sourceRadius, 0.0f),
        true,
        AllocateLightIdentity()
    });
    MarkLightsChanged();

    return m_PointLights.back();
}

SpotLight3D& Scene3D::CreateSpotLight(
    std::string name,
    glm::vec3 position,
    glm::vec3 direction,
    f32 radius,
    glm::vec3 color,
    f32 intensity,
    f32 innerConeDegrees,
    f32 outerConeDegrees,
    f32 sourceRadius
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, -1.0f, 0.0f };
    } else {
        direction = glm::normalize(direction);
    }

    outerConeDegrees = std::clamp(outerConeDegrees, 0.1f, 89.0f);
    innerConeDegrees = std::clamp(innerConeDegrees, 0.05f, outerConeDegrees);

    m_SpotLights.push_back(SpotLight3D{
        std::move(name),
        position,
        direction,
        std::max(radius, 0.0f),
        glm::max(color, glm::vec3(0.0f)),
        std::max(intensity, 0.0f),
        innerConeDegrees,
        outerConeDegrees,
        std::max(sourceRadius, 0.0f),
        true,
        AllocateLightIdentity()
    });
    MarkLightsChanged();

    return m_SpotLights.back();
}

RectLight3D& Scene3D::CreateRectLight(
    std::string name,
    glm::vec3 position,
    glm::vec3 direction,
    f32 width,
    f32 height,
    f32 radius,
    glm::vec3 color,
    f32 intensity,
    f32 specular
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, -1.0f, 0.0f };
    } else {
        direction = glm::normalize(direction);
    }

    m_RectLights.push_back(RectLight3D{
        std::move(name),
        position,
        direction,
        std::max(width, 0.0f),
        std::max(height, 0.0f),
        std::max(radius, 0.0f),
        glm::max(color, glm::vec3(0.0f)),
        std::max(intensity, 0.0f),
        std::clamp(specular, 0.0f, 1.0f),
        true,
        AllocateLightIdentity()
    });
    MarkLightsChanged();

    return m_RectLights.back();
}

ReflectionProbe3D& Scene3D::CreateReflectionProbe(
    std::string name,
    glm::vec3 center,
    f32 radius,
    glm::vec3 boxExtents,
    glm::vec3 color,
    f32 intensity,
    f32 blendStrength,
    f32 falloff,
    ReflectionProbeCaptureSource captureSource,
    std::string captureAssetId,
    ReflectionProbeRefreshPolicy refreshPolicy
) {
    m_ReflectionProbes.push_back(ReflectionProbe3D{
        std::move(name),
        center,
        std::clamp(radius, 0.01f, 256.0f),
        center,
        glm::max(boxExtents, glm::vec3(0.01f)),
        glm::max(color, glm::vec3(0.0f)),
        std::clamp(intensity, 0.0f, 4.0f),
        std::clamp(blendStrength, 0.0f, 1.0f),
        std::clamp(falloff, 0.25f, 8.0f),
        true,
        captureSource,
        std::move(captureAssetId),
        refreshPolicy
    });
    MarkLightsChanged();

    return m_ReflectionProbes.back();
}

bool Scene3D::UpdateReflectionProbe(
    std::size_t index,
    const ReflectionProbe3D& probe
) {
    if (index >= m_ReflectionProbes.size() || probe.name.empty()) {
        return false;
    }

    ReflectionProbe3D sanitized = probe;
    sanitized.radius = std::clamp(sanitized.radius, 0.01f, 256.0f);
    sanitized.boxExtents = glm::max(sanitized.boxExtents, glm::vec3(0.01f));
    sanitized.color = glm::max(sanitized.color, glm::vec3(0.0f));
    sanitized.intensity = std::clamp(sanitized.intensity, 0.0f, 4.0f);
    sanitized.blendStrength = std::clamp(sanitized.blendStrength, 0.0f, 1.0f);
    sanitized.falloff = std::clamp(sanitized.falloff, 0.25f, 8.0f);
    auto& excludedIdentities = sanitized.captureExcludedRenderableIdentities;
    excludedIdentities.erase(
        std::remove(excludedIdentities.begin(), excludedIdentities.end(), 0u),
        excludedIdentities.end()
    );
    std::sort(excludedIdentities.begin(), excludedIdentities.end());
    excludedIdentities.erase(
        std::unique(excludedIdentities.begin(), excludedIdentities.end()),
        excludedIdentities.end()
    );

    m_ReflectionProbes[index] = std::move(sanitized);
    MarkLightsChanged();
    return true;
}

bool Scene3D::DestroyReflectionProbe(std::size_t index) {
    if (index >= m_ReflectionProbes.size()) {
        return false;
    }

    m_ReflectionProbes.erase(m_ReflectionProbes.begin() +
        static_cast<std::ptrdiff_t>(index));
    MarkLightsChanged();
    return true;
}

DirectionalLight3D& Scene3D::SetPrimaryDirectionalLight(
    std::string name,
    glm::vec3 direction,
    f32 intensity,
    f32 ambient,
    f32 specular,
    f32 angularRadiusRadians
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { -0.45f, -0.82f, -0.35f };
    } else {
        direction = glm::normalize(direction);
    }

    DirectionalLight3D nextLight{
        std::move(name),
        direction,
        std::max(intensity, 0.0f),
        std::max(ambient, 0.0f),
        std::max(specular, 0.0f),
        std::clamp(angularRadiusRadians, 0.0f, 0.05f),
        true,
        m_PrimaryDirectionalLight.has_value()
            ? m_PrimaryDirectionalLight->identity
            : AllocateLightIdentity()
    };

    const bool changed =
        !m_PrimaryDirectionalLight.has_value() ||
        m_PrimaryDirectionalLight->name != nextLight.name ||
        glm::length(m_PrimaryDirectionalLight->direction - nextLight.direction) > 0.0001f ||
        std::abs(m_PrimaryDirectionalLight->intensity - nextLight.intensity) > 0.0001f ||
        std::abs(m_PrimaryDirectionalLight->ambient - nextLight.ambient) > 0.0001f ||
        std::abs(m_PrimaryDirectionalLight->specular - nextLight.specular) > 0.0001f ||
        std::abs(
            m_PrimaryDirectionalLight->angularRadiusRadians -
            nextLight.angularRadiusRadians
        ) > 0.000001f ||
        m_PrimaryDirectionalLight->enabled != nextLight.enabled;

    m_PrimaryDirectionalLight = std::move(nextLight);
    if (changed) {
        MarkLightsChanged();
    }

    return *m_PrimaryDirectionalLight;
}

u64 Scene3D::CreateLight(SceneLightKind kind) {
    switch (kind) {
    case SceneLightKind::Directional:
        if (m_PrimaryDirectionalLight.has_value()) {
            return m_PrimaryDirectionalLight->identity;
        }
        return SetPrimaryDirectionalLight(
            DefaultLightName(kind),
            { -0.45f, -0.82f, -0.35f },
            2.35f,
            0.16f,
            0.32f
        ).identity;
    case SceneLightKind::Point:
        return CreatePointLight(
            DefaultLightName(kind),
            { 0.0f, 1.5f, 0.0f },
            5.0f,
            { 1.0f, 1.0f, 1.0f },
            4.0f
        ).identity;
    case SceneLightKind::Spot:
        return CreateSpotLight(
            DefaultLightName(kind),
            { 0.0f, 2.5f, 0.0f },
            { 0.0f, -1.0f, 0.0f },
            6.0f,
            { 1.0f, 1.0f, 1.0f },
            4.0f,
            18.0f,
            28.0f
        ).identity;
    case SceneLightKind::Rect:
        return CreateRectLight(
            DefaultLightName(kind),
            { 0.0f, 2.5f, 0.0f },
            { 0.0f, -1.0f, 0.0f },
            2.0f,
            1.0f,
            6.0f,
            { 1.0f, 1.0f, 1.0f },
            4.0f
        ).identity;
    }

    return 0u;
}

bool Scene3D::ReadLightEdit(u64 lightIdentity, SceneLightEdit& edit) const {
    if (lightIdentity == 0u) {
        return false;
    }

    if (m_PrimaryDirectionalLight.has_value() &&
        m_PrimaryDirectionalLight->identity == lightIdentity) {
        const DirectionalLight3D& light = *m_PrimaryDirectionalLight;
        edit = SceneLightEdit{};
        edit.kind = SceneLightKind::Directional;
        edit.name = light.name;
        edit.enabled = light.enabled;
        edit.direction = light.direction;
        edit.intensity = light.intensity;
        edit.ambient = light.ambient;
        edit.specular = light.specular;
        edit.angularRadiusRadians = light.angularRadiusRadians;
        return true;
    }

    for (const PointLight3D& light : m_PointLights) {
        if (light.identity != lightIdentity) {
            continue;
        }
        edit = SceneLightEdit{};
        edit.kind = SceneLightKind::Point;
        edit.name = light.name;
        edit.enabled = light.enabled;
        edit.position = light.position;
        edit.color = light.color;
        edit.intensity = light.intensity;
        edit.radius = light.radius;
        edit.sourceRadius = light.sourceRadius;
        return true;
    }

    for (const SpotLight3D& light : m_SpotLights) {
        if (light.identity != lightIdentity) {
            continue;
        }
        edit = SceneLightEdit{};
        edit.kind = SceneLightKind::Spot;
        edit.name = light.name;
        edit.enabled = light.enabled;
        edit.position = light.position;
        edit.direction = light.direction;
        edit.color = light.color;
        edit.intensity = light.intensity;
        edit.radius = light.radius;
        edit.sourceRadius = light.sourceRadius;
        edit.innerConeDegrees = light.innerConeDegrees;
        edit.outerConeDegrees = light.outerConeDegrees;
        return true;
    }

    for (const RectLight3D& light : m_RectLights) {
        if (light.identity != lightIdentity) {
            continue;
        }
        edit = SceneLightEdit{};
        edit.kind = SceneLightKind::Rect;
        edit.name = light.name;
        edit.enabled = light.enabled;
        edit.position = light.position;
        edit.direction = light.direction;
        edit.color = light.color;
        edit.intensity = light.intensity;
        edit.radius = light.radius;
        edit.width = light.width;
        edit.height = light.height;
        edit.specular = light.specular;
        return true;
    }

    return false;
}

bool Scene3D::ApplyLightEdit(u64 lightIdentity, const SceneLightEdit& input) {
    SceneLightEdit current{};
    if (!ReadLightEdit(lightIdentity, current) ||
        current.kind != input.kind ||
        !LightEditIsFinite(input)) {
        return false;
    }

    SceneLightEdit next = input;
    SanitizeLightEdit(next);
    if (SameLightEdit(current, next)) {
        return true;
    }

    switch (next.kind) {
    case SceneLightKind::Directional:
        if (!m_PrimaryDirectionalLight.has_value() ||
            m_PrimaryDirectionalLight->identity != lightIdentity) {
            return false;
        }
        m_PrimaryDirectionalLight->name = std::move(next.name);
        m_PrimaryDirectionalLight->enabled = next.enabled;
        m_PrimaryDirectionalLight->direction = next.direction;
        m_PrimaryDirectionalLight->intensity = next.intensity;
        m_PrimaryDirectionalLight->ambient = next.ambient;
        m_PrimaryDirectionalLight->specular = next.specular;
        m_PrimaryDirectionalLight->angularRadiusRadians = next.angularRadiusRadians;
        break;
    case SceneLightKind::Point:
        for (PointLight3D& light : m_PointLights) {
            if (light.identity == lightIdentity) {
                light.name = std::move(next.name);
                light.enabled = next.enabled;
                light.position = next.position;
                light.color = next.color;
                light.intensity = next.intensity;
                light.radius = next.radius;
                light.sourceRadius = next.sourceRadius;
                MarkLightsChanged();
                return true;
            }
        }
        return false;
    case SceneLightKind::Spot:
        for (SpotLight3D& light : m_SpotLights) {
            if (light.identity == lightIdentity) {
                light.name = std::move(next.name);
                light.enabled = next.enabled;
                light.position = next.position;
                light.direction = next.direction;
                light.color = next.color;
                light.intensity = next.intensity;
                light.radius = next.radius;
                light.sourceRadius = next.sourceRadius;
                light.innerConeDegrees = next.innerConeDegrees;
                light.outerConeDegrees = next.outerConeDegrees;
                MarkLightsChanged();
                return true;
            }
        }
        return false;
    case SceneLightKind::Rect:
        for (RectLight3D& light : m_RectLights) {
            if (light.identity == lightIdentity) {
                light.name = std::move(next.name);
                light.enabled = next.enabled;
                light.position = next.position;
                light.direction = next.direction;
                light.color = next.color;
                light.intensity = next.intensity;
                light.radius = next.radius;
                light.width = next.width;
                light.height = next.height;
                light.specular = std::clamp(next.specular, 0.0f, 1.0f);
                MarkLightsChanged();
                return true;
            }
        }
        return false;
    }

    MarkLightsChanged();
    return true;
}

bool Scene3D::DestroyLight(u64 lightIdentity) {
    if (lightIdentity == 0u) {
        return false;
    }
    if (m_PrimaryDirectionalLight.has_value() &&
        m_PrimaryDirectionalLight->identity == lightIdentity) {
        m_PrimaryDirectionalLight.reset();
        MarkLightsChanged();
        return true;
    }

    const auto removeByIdentity = [lightIdentity](auto& lights) {
        const auto found = std::find_if(
            lights.begin(),
            lights.end(),
            [lightIdentity](const auto& light) {
                return light.identity == lightIdentity;
            }
        );
        if (found == lights.end()) {
            return false;
        }
        lights.erase(found);
        return true;
    };

    if (!removeByIdentity(m_PointLights) &&
        !removeByIdentity(m_SpotLights) &&
        !removeByIdentity(m_RectLights)) {
        return false;
    }

    MarkLightsChanged();
    return true;
}

bool Scene3D::DestroyRenderable(const Renderable3D& renderable) {
    const u64 renderIdentity = renderable.RenderIdentity();
    if (!m_Storage.RemoveRenderable(renderable)) {
        return false;
    }

    bool probeExclusionsPruned = false;
    for (ReflectionProbe3D& probe : m_ReflectionProbes) {
        std::vector<u64>& excludedIdentities =
            probe.captureExcludedRenderableIdentities;
        const std::size_t previousCount = excludedIdentities.size();
        excludedIdentities.erase(
            std::remove(
                excludedIdentities.begin(),
                excludedIdentities.end(),
                renderIdentity
            ),
            excludedIdentities.end()
        );
        probeExclusionsPruned |= excludedIdentities.size() != previousCount;
    }
    if (probeExclusionsPruned) {
        MarkLightsChanged();
    }
    MarkMembershipChanged();
    return true;
}

bool Scene3D::DestroyRenderableByIdentity(u64 renderIdentity) {
    const Renderable3D* renderable = FindRenderableByIdentity(renderIdentity);
    if (renderable == nullptr) {
        return false;
    }

    return DestroyRenderable(*renderable);
}

void Scene3D::Clear() {
    m_Storage.Clear();
    m_PrimaryDirectionalLight.reset();
    m_PointLights.clear();
    m_SpotLights.clear();
    m_RectLights.clear();
    m_ReflectionProbes.clear();
    ++m_MembershipRevision;
    ++m_LightRevision;
    MarkRenderChanged();
}

const SceneEnvironment3D& Scene3D::Environment() const {
    return m_Environment;
}

bool Scene3D::EnvironmentAuthored() const {
    return m_EnvironmentAuthored;
}

void Scene3D::SetEnvironment(const SceneEnvironment3D& environment) {
    const SceneEnvironment3D sanitized = SanitizedEnvironment(environment);
    if (m_EnvironmentAuthored && SameEnvironment(m_Environment, sanitized)) {
        return;
    }

    m_Environment = sanitized;
    m_EnvironmentAuthored = true;
    MarkRenderChanged();
}

bool Scene3D::EnvironmentIblEnabled() const {
    return m_Environment.iblEnabled;
}

void Scene3D::SetEnvironmentIblEnabled(bool enabled) {
    SceneEnvironment3D environment = m_Environment;
    environment.iblEnabled = enabled;
    SetEnvironment(environment);
}

void Scene3D::Update(f32 deltaSeconds) {
    const f32 clampedDeltaSeconds = std::max(deltaSeconds, 0.0f);
    for (Renderable3D* renderable : m_Storage.Renderables()) {
        if (renderable == nullptr) {
            continue;
        }

        Transform3D& transform = renderable->Transform();
        if (!transform.AnimateRotation()) {
            continue;
        }

        glm::vec3 rotationDegrees =
            transform.RotationDegrees() +
            transform.RotationSpeedDegreesPerSecond() * clampedDeltaSeconds;
        WrapDegrees(rotationDegrees.x);
        WrapDegrees(rotationDegrees.y);
        WrapDegrees(rotationDegrees.z);
        transform.SetRotationDegrees(rotationDegrees);
    }
}

std::span<Renderable3D* const> Scene3D::Renderables() const {
    return m_Storage.Renderables();
}

bool Scene3D::MovePointLight(std::size_t index, glm::vec3 position) {
    if (index >= m_PointLights.size()) {
        return false;
    }

    PointLight3D& light = m_PointLights[index];
    if (glm::length(light.position - position) <= 0.0001f) {
        return true;
    }

    light.position = position;
    MarkLightsChanged();
    return true;
}

const DirectionalLight3D* Scene3D::PrimaryDirectionalLight() const {
    return m_PrimaryDirectionalLight.has_value() ? &*m_PrimaryDirectionalLight : nullptr;
}

std::span<const PointLight3D> Scene3D::PointLights() const {
    return std::span<const PointLight3D>(m_PointLights.data(), m_PointLights.size());
}

std::span<const SpotLight3D> Scene3D::SpotLights() const {
    return std::span<const SpotLight3D>(m_SpotLights.data(), m_SpotLights.size());
}

std::span<const RectLight3D> Scene3D::RectLights() const {
    return std::span<const RectLight3D>(m_RectLights.data(), m_RectLights.size());
}

std::span<const ReflectionProbe3D> Scene3D::ReflectionProbes() const {
    return std::span<const ReflectionProbe3D>(
        m_ReflectionProbes.data(),
        m_ReflectionProbes.size()
    );
}

bool Scene3D::Empty() const {
    return m_Storage.Empty();
}

std::size_t Scene3D::Count() const {
    return m_Storage.Count();
}

Renderable3D* Scene3D::SelectedRenderable() {
    return m_Storage.SelectedRenderable();
}

const Renderable3D* Scene3D::SelectedRenderable() const {
    return m_Storage.SelectedRenderable();
}

std::size_t Scene3D::SelectedIndex() const {
    return m_Storage.SelectedIndex();
}

void Scene3D::SetSelectedIndex(std::size_t index) {
    m_Storage.SetSelectedIndex(index);
}

Renderable3D* Scene3D::FindRenderableByIdentity(u64 renderIdentity) {
    if (renderIdentity == 0) {
        return nullptr;
    }

    for (Renderable3D* renderable : m_Storage.Renderables()) {
        if (renderable != nullptr && renderable->RenderIdentity() == renderIdentity) {
            return renderable;
        }
    }

    return nullptr;
}

const Renderable3D* Scene3D::FindRenderableByIdentity(u64 renderIdentity) const {
    if (renderIdentity == 0) {
        return nullptr;
    }

    for (const Renderable3D* renderable : m_Storage.Renderables()) {
        if (renderable != nullptr && renderable->RenderIdentity() == renderIdentity) {
            return renderable;
        }
    }

    return nullptr;
}

bool Scene3D::SelectRenderableByIdentity(u64 renderIdentity) {
    Renderable3D* renderable = FindRenderableByIdentity(renderIdentity);
    if (renderable == nullptr) {
        return false;
    }

    return m_Storage.SelectRenderable(*renderable);
}

bool Scene3D::SelectAlongRay(const glm::vec3& origin, const glm::vec3& direction) {
    if (glm::length(direction) <= kRayEpsilon) {
        return false;
    }

    const glm::vec3 normalizedDirection = glm::normalize(direction);
    Renderable3D* nearestRenderable = nullptr;
    f32 nearestDistance = std::numeric_limits<f32>::max();

    for (Renderable3D* renderable : m_Storage.Renderables()) {
        if (renderable == nullptr || !renderable->Pickable()) {
            continue;
        }

        const glm::mat4 model = renderable->Transform().Matrix();
        const glm::mat4 inverseModel = glm::inverse(model);
        const glm::vec3 localOrigin =
            glm::vec3(inverseModel * glm::vec4(origin, 1.0f));
        const glm::vec3 localRayEnd =
            glm::vec3(inverseModel * glm::vec4(origin + normalizedDirection, 1.0f));
        const glm::vec3 localDirection = localRayEnd - localOrigin;

        f32 hitDistance = 0.0f;
        if (!IntersectUnitBounds(localOrigin, localDirection, hitDistance)) {
            continue;
        }

        const glm::vec3 localHitPosition = localOrigin + localDirection * hitDistance;
        const glm::vec3 worldHitPosition =
            glm::vec3(model * glm::vec4(localHitPosition, 1.0f));
        const f32 worldDistance = glm::length(worldHitPosition - origin);

        if (worldDistance < nearestDistance) {
            nearestDistance = worldDistance;
            nearestRenderable = renderable;
        }
    }

    if (nearestRenderable == nullptr) {
        return false;
    }

    return m_Storage.SelectRenderable(*nearestRenderable);
}

u64 Scene3D::MembershipRevision() const {
    return m_MembershipRevision;
}

u64 Scene3D::RenderRevision() const {
    return m_RenderRevision;
}

u64 Scene3D::LightRevision() const {
    return m_LightRevision;
}

u64 Scene3D::AllocateLightIdentity() {
    if (m_NextLightIdentity == 0u) {
        m_NextLightIdentity = 1u;
    }

    return m_NextLightIdentity++;
}

void Scene3D::MarkMembershipChanged() {
    ++m_MembershipRevision;
    MarkRenderChanged();
}

void Scene3D::MarkRenderChanged() {
    ++m_RenderRevision;
}

void Scene3D::MarkLightsChanged() {
    ++m_LightRevision;
    MarkRenderChanged();
}

}
