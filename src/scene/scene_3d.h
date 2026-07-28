#pragma once

#include "core.h"
#include "scene/renderable_3d.h"
#include "scene/scene_storage.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace se {

inline constexpr f32 kDefaultDirectionalLightAngularRadiusRadians = 0.00464258f;

enum class SceneLightKind : u32 {
    Directional = 0u,
    Point = 1u,
    Spot = 2u,
    Rect = 3u
};

// A type-tagged editable light state. Scene3D owns the canonical light
// storage; tools use this value to avoid retaining pointers into light arrays.
struct SceneLightEdit {
    SceneLightKind kind = SceneLightKind::Point;
    std::string name;
    bool enabled = true;
    glm::vec3 position{ 0.0f };
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    glm::vec3 color{ 1.0f };
    f32 intensity = 1.0f;
    f32 radius = 1.0f;
    f32 sourceRadius = 0.05f;
    f32 innerConeDegrees = 18.0f;
    f32 outerConeDegrees = 28.0f;
    f32 width = 1.0f;
    f32 height = 1.0f;
    f32 ambient = 0.22f;
    f32 specular = 1.0f;
    f32 angularRadiusRadians = kDefaultDirectionalLightAngularRadiusRadians;
};

struct DirectionalLight3D {
    std::string name;
    glm::vec3 direction{ -0.45f, -0.82f, -0.35f };
    f32 intensity = 0.78f;
    f32 ambient = 0.22f;
    f32 specular = 0.24f;
    f32 angularRadiusRadians = kDefaultDirectionalLightAngularRadiusRadians;
    bool enabled = true;
    u64 identity = 0;
};

struct PointLight3D {
    std::string name;
    glm::vec3 position{ 0.0f };
    f32 radius = 1.0f;
    glm::vec3 color{ 1.0f };
    f32 intensity = 1.0f;
    f32 sourceRadius = 0.05f;
    bool enabled = true;
    u64 identity = 0;
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
    f32 sourceRadius = 0.05f;
    bool enabled = true;
    u64 identity = 0;
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
    f32 specular = 1.0f;
    bool enabled = true;
    u64 identity = 0;
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
    // Cubemap capture origin. This is deliberately independent from the
    // parallax proxy center below.
    glm::vec3 center{ 0.0f, 1.2f, 0.0f };
    f32 radius = 5.5f;
    // Center of the finite box used for local-probe influence and parallax.
    glm::vec3 boxCenter{ 0.0f, 1.2f, 0.0f };
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
    // Stable render identities excluded only while this probe captures the
    // scene. Unlike ReflectionCaptureVisible this does not hide an object
    // from other reflection probes.
    std::vector<u64> captureExcludedRenderableIdentities;
};

enum class SceneEnvironmentLightingAsset : u32 {
    RendererDefault = 0,
    StudioPanorama = 1
};

// Scene-owned controls for global environment maps and the visible skybox.
// The lighting asset has a GPU-resource lifetime; scalar controls remain
// per-frame shading state.
struct SceneEnvironment3D {
    bool iblEnabled = true;
    f32 diffuseIntensity = 1.0f;
    f32 specularIntensity = 1.0f;
    f32 horizonBlend = 0.22f;
    bool skyboxEnabled = false;
    f32 skyboxIntensity = 1.0f;
    f32 skyboxBlur = 0.0f;
    SceneEnvironmentLightingAsset lightingAsset =
        SceneEnvironmentLightingAsset::RendererDefault;
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
        f32 intensity,
        f32 sourceRadius = 0.05f
    );
    SpotLight3D& CreateSpotLight(
        std::string name,
        glm::vec3 position,
        glm::vec3 direction,
        f32 radius,
        glm::vec3 color,
        f32 intensity,
        f32 innerConeDegrees,
        f32 outerConeDegrees,
        f32 sourceRadius = 0.05f
    );
    RectLight3D& CreateRectLight(
        std::string name,
        glm::vec3 position,
        glm::vec3 direction,
        f32 width,
        f32 height,
        f32 radius,
        glm::vec3 color,
        f32 intensity,
        f32 specular = 1.0f
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
    bool UpdateReflectionProbe(
        std::size_t index,
        const ReflectionProbe3D& probe
    );
    bool DestroyReflectionProbe(std::size_t index);
    DirectionalLight3D& SetPrimaryDirectionalLight(
        std::string name,
        glm::vec3 direction,
        f32 intensity,
        f32 ambient,
        f32 specular,
        f32 angularRadiusRadians = kDefaultDirectionalLightAngularRadiusRadians
    );
    // The environment module is the single scene-facing seam for global IBL
    // and the visible skybox. Direct authored lights remain independent.
    const SceneEnvironment3D& Environment() const;
    bool EnvironmentAuthored() const;
    void SetEnvironment(const SceneEnvironment3D& environment);
    bool EnvironmentIblEnabled() const;
    void SetEnvironmentIblEnabled(bool enabled);
    // Stable light identities make dynamic editor mutations independent of
    // vector ordering. Existing typed creation methods remain available for
    // authored scenes; these operations are the tool-facing seam.
    u64 CreateLight(SceneLightKind kind);
    bool ReadLightEdit(u64 lightIdentity, SceneLightEdit& edit) const;
    bool ApplyLightEdit(u64 lightIdentity, const SceneLightEdit& edit);
    bool DestroyLight(u64 lightIdentity);
    // Removes one renderable and bumps the membership revision so cached
    // render queues cannot reuse commands for destroyed scene objects.
    bool DestroyRenderable(const Renderable3D& renderable);
    bool DestroyRenderableByIdentity(u64 renderIdentity);
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
    // Stable identity lookup. Vector order changes whenever a renderable is
    // created or destroyed, so tools must not treat an index as an identity.
    Renderable3D* FindRenderableByIdentity(u64 renderIdentity);
    const Renderable3D* FindRenderableByIdentity(u64 renderIdentity) const;
    bool SelectRenderableByIdentity(u64 renderIdentity);
    bool SelectAlongRay(const glm::vec3& origin, const glm::vec3& direction);
    u64 MembershipRevision() const;
    u64 RenderRevision() const;
    u64 LightRevision() const;

private:
    u64 AllocateLightIdentity();
    void MarkMembershipChanged();
    void MarkRenderChanged();
    void MarkLightsChanged();

    SceneStorage<Renderable3D> m_Storage;
    std::optional<DirectionalLight3D> m_PrimaryDirectionalLight;
    std::vector<PointLight3D> m_PointLights;
    std::vector<SpotLight3D> m_SpotLights;
    std::vector<RectLight3D> m_RectLights;
    std::vector<ReflectionProbe3D> m_ReflectionProbes;
    SceneEnvironment3D m_Environment;
    bool m_EnvironmentAuthored = false;
    u64 m_MembershipRevision = 1;
    u64 m_RenderRevision = 1;
    u64 m_LightRevision = 1;
    u64 m_NextLightIdentity = 1;
};

}
