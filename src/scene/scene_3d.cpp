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
    f32 intensity
) {
    m_PointLights.push_back(PointLight3D{
        std::move(name),
        position,
        std::max(radius, 0.0f),
        glm::max(color, glm::vec3(0.0f)),
        std::max(intensity, 0.0f),
        true
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
    f32 outerConeDegrees
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
        true
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
    f32 intensity
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
        true
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

DirectionalLight3D& Scene3D::SetPrimaryDirectionalLight(
    std::string name,
    glm::vec3 direction,
    f32 intensity,
    f32 ambient,
    f32 specular
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
        true
    };

    const bool changed =
        !m_PrimaryDirectionalLight.has_value() ||
        m_PrimaryDirectionalLight->name != nextLight.name ||
        glm::length(m_PrimaryDirectionalLight->direction - nextLight.direction) > 0.0001f ||
        std::abs(m_PrimaryDirectionalLight->intensity - nextLight.intensity) > 0.0001f ||
        std::abs(m_PrimaryDirectionalLight->ambient - nextLight.ambient) > 0.0001f ||
        std::abs(m_PrimaryDirectionalLight->specular - nextLight.specular) > 0.0001f ||
        m_PrimaryDirectionalLight->enabled != nextLight.enabled;

    m_PrimaryDirectionalLight = std::move(nextLight);
    if (changed) {
        MarkLightsChanged();
    }

    return *m_PrimaryDirectionalLight;
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
