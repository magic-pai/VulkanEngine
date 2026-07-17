#pragma once

#include "core.h"
#include "scene/renderable_3d.h"
#include "scene/scene_storage.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace se {

inline constexpr f32 kDefaultDirectionalLightAngularRadiusRadians = 0.00464258f;

struct DirectionalLight3D {
    std::string name;
    glm::vec3 direction{ -0.45f, -0.82f, -0.35f };
    f32 intensity = 0.78f;
    f32 ambient = 0.22f;
    f32 specular = 0.24f;
    f32 angularRadiusRadians = kDefaultDirectionalLightAngularRadiusRadians;
    bool enabled = true;
};

struct PointLight3D {
    std::string name;
    glm::vec3 position{ 0.0f };
    f32 radius = 1.0f;
    glm::vec3 color{ 1.0f };
    f32 intensity = 1.0f;
    bool enabled = true;
};

struct SpotLight3D {
    std::string name;
    glm::vec3 position{ 0.0f };
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    f32 radius = 1.0f;
    glm::vec3 color{ 1.0f };
    f32 intensity = 1.0f;
    f32 innerConeDegrees = 18.0f;
    f32 outerConeDegrees = 28.0f;
    bool enabled = true;
};

struct RectLight3D {
    std::string name;
    glm::vec3 position{ 0.0f };
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    f32 width = 1.0f;
    f32 height = 1.0f;
    f32 radius = 1.0f;
    glm::vec3 color{ 1.0f };
    f32 intensity = 1.0f;
    bool enabled = true;
};

enum class ReflectionProbeCaptureSource : u32 {
    None = 0,
    BuiltInProcedural = 1,
    AuthoredCubemap = 2,
    CapturedScene = 3
};

enum class ReflectionProbeRefreshPolicy : u32 {
    Static = 0,
    FileSignature = 1,
    Forced = 2,
    SceneDirty = 3
};

struct ReflectionProbe3D {
    std::string name;
    glm::vec3 center{ 0.0f, 1.2f, 0.0f };
    f32 radius = 5.5f;
    glm::vec3 boxExtents{ 5.5f };
    glm::vec3 color{ 1.0f, 0.82f, 0.62f };
    f32 intensity = 1.25f;
    f32 blendStrength = 0.65f;
    f32 falloff = 2.0f;
    bool enabled = true;
    ReflectionProbeCaptureSource captureSource =
        ReflectionProbeCaptureSource::BuiltInProcedural;
    std::string captureAssetId;
    ReflectionProbeRefreshPolicy refreshPolicy =
        ReflectionProbeRefreshPolicy::Static;
};

class Scene3D {
public:
    Scene3D();
    ~Scene3D();

    Renderable3D& CreateRenderable(
        std::string name,
        std::string meshId,
        std::string materialId
    );
    PointLight3D& CreatePointLight(
        std::string name,
        glm::vec3 position,
        f32 radius,
        glm::vec3 color,
        f32 intensity
    );
    SpotLight3D& CreateSpotLight(
        std::string name,
        glm::vec3 position,
        glm::vec3 direction,
        f32 radius,
        glm::vec3 color,
        f32 intensity,
        f32 innerConeDegrees,
        f32 outerConeDegrees
    );
    RectLight3D& CreateRectLight(
        std::string name,
        glm::vec3 position,
        glm::vec3 direction,
        f32 width,
        f32 height,
        f32 radius,
        glm::vec3 color,
        f32 intensity
    );
    ReflectionProbe3D& CreateReflectionProbe(
        std::string name,
        glm::vec3 center,
        f32 radius,
        glm::vec3 boxExtents,
        glm::vec3 color,
        f32 intensity,
        f32 blendStrength,
        f32 falloff,
        ReflectionProbeCaptureSource captureSource =
            ReflectionProbeCaptureSource::BuiltInProcedural,
        std::string captureAssetId = {},
        ReflectionProbeRefreshPolicy refreshPolicy =
            ReflectionProbeRefreshPolicy::Static
    );
    DirectionalLight3D& SetPrimaryDirectionalLight(
        std::string name,
        glm::vec3 direction,
        f32 intensity,
        f32 ambient,
        f32 specular,
        f32 angularRadiusRadians = kDefaultDirectionalLightAngularRadiusRadians
    );
    void Clear();
    void Update(f32 deltaSeconds);
    bool MovePointLight(std::size_t index, glm::vec3 position);

    std::span<Renderable3D* const> Renderables() const;
    const DirectionalLight3D* PrimaryDirectionalLight() const;
    std::span<const PointLight3D> PointLights() const;
    std::span<const SpotLight3D> SpotLights() const;
    std::span<const RectLight3D> RectLights() const;
    std::span<const ReflectionProbe3D> ReflectionProbes() const;
    bool Empty() const;
    std::size_t Count() const;
    Renderable3D* SelectedRenderable();
    const Renderable3D* SelectedRenderable() const;
    std::size_t SelectedIndex() const;
    void SetSelectedIndex(std::size_t index);
    bool SelectAlongRay(const glm::vec3& origin, const glm::vec3& direction);
    u64 MembershipRevision() const;
    u64 RenderRevision() const;
    u64 LightRevision() const;

private:
    void MarkMembershipChanged();
    void MarkRenderChanged();
    void MarkLightsChanged();

    SceneStorage<Renderable3D> m_Storage;
    std::optional<DirectionalLight3D> m_PrimaryDirectionalLight;
    std::vector<PointLight3D> m_PointLights;
    std::vector<SpotLight3D> m_SpotLights;
    std::vector<RectLight3D> m_RectLights;
    std::vector<ReflectionProbe3D> m_ReflectionProbes;
    u64 m_MembershipRevision = 1;
    u64 m_RenderRevision = 1;
    u64 m_LightRevision = 1;
};

}
