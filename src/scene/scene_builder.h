#pragma once

// Runtime scene-building tool.
//
// The builder creates ordinary Scene3D renderables that reference already
// registered primitive mesh ids and one runtime-created VulkanMaterial per
// object. Every mutation goes through the normal Renderable3D / Transform3D /
// MaterialProperties model so the existing RenderQueue, material descriptor,
// and shading paths consume it without an editor-specific render path.
//
// The builder never inspects scene names, object ordinals, or known transforms
// to decide renderer behavior. It only owns the objects it created itself.

#include "core.h"
#include "renderer/vulkan/material.h"
#include "scene/camera_3d.h"
#include "scene/scene_3d.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <functional>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace se {

class Renderable3D;
class VulkanCommandPool;
class VulkanDevice;
class VulkanMaterialLibrary;
class VulkanPhysicalDevice;
class VulkanRenderResources2D;

enum class SceneBuilderPrimitive : u32 {
    Cube = 0,
    Plane = 1,
    Sphere = 2,
    Cone = 3,
    Lvjuren = 4,
    Building1 = 5,
    Building2 = 6,
    Building3 = 7,
    Building4 = 8,
    Car1 = 9,
    Car2 = 10
};

inline constexpr u32 kSceneBuilderPrimitiveCount = 11;
inline constexpr u32 kSceneBuilderMaxReflectionProbes = 4;

// Reasons a create request can be refused. Reported instead of silently
// falling back to a different mesh or material.
enum class SceneBuilderCreateFailure : u32 {
    None = 0,
    MeshNotRegistered = 1,
    ObjectLimitReached = 2,
    MaterialIdCollision = 3,
    UnknownPrimitiveName = 4,
    UnknownModifier = 5,
    LightLimitReached = 6,
    ReflectionProbeLimitReached = 7,
    ImportedAssetUnavailable = 8,
    ImportedAssetLoadFailed = 9
};

// Editable state of one builder object. Read it, mutate it, apply it back.
struct SceneBuilderObjectEdit {
    glm::vec3 position{ 0.0f };
    glm::vec3 rotationDegrees{ 0.0f };
    glm::vec3 scale{ 1.0f };
    bool castShadow = true;
    glm::vec4 baseColor{ 0.82f, 0.82f, 0.84f, 1.0f };
    f32 metallic = 0.0f;
    f32 roughness = 0.5f;
    glm::vec3 emissive{ 0.0f };
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    f32 alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct SceneBuilderObject {
    u64 renderIdentity = 0;
    SceneBuilderPrimitive primitive = SceneBuilderPrimitive::Cube;
    std::string name;
    std::string meshId;
    std::string materialId;
    // Empty for built-in primitives. Imported assets may own multiple scene
    // renderables but expose one editor entity and one root identity.
    std::string assetPath;
    std::vector<u64> memberRenderIdentities;
    Renderable3D* renderable = nullptr;
    VulkanMaterial* material = nullptr;
};

// Result returned by the runtime importer seam. SceneBuilder remains
// independent from importer implementation details and only adopts the
// ordinary Scene3D renderables that the importer created.
struct SceneBuilderImportedAsset {
    bool loaded = false;
    std::string message;
    bool materialResourcesChanged = false;
    std::vector<u64> renderIdentities;
};

struct SceneBuilderCityLayoutResult {
    bool applied = false;
    bool failed = false;
    u32 createdObjectCount = 0;
    u32 buildingCount = 0;
    u32 carCount = 0;
};

// Builder-owned identity for a light stored by Scene3D. The type is retained
// here for UI/document routing; Scene3D remains the source of light values.
// Directional lights also retain an editor-only gizmo anchor because their
// physical model has direction but no finite world-space position.
struct SceneBuilderLight {
    u64 lightIdentity = 0;
    SceneLightKind kind = SceneLightKind::Point;
    glm::vec3 gizmoPosition{ 0.0f };
};

// The builder's local reflection probe is ordinary scene data. Keeping its
// full state here makes capture placement and update policy editable and
// serializable with the rest of the scene.
struct SceneBuilderReflectionProbeEdit {
    std::string name = "Scene Builder Reflection Probe";
    // Stored as `capturePosition` in scene documents. The historical member
    // name remains so existing editor call sites keep their capture semantics.
    glm::vec3 center{ 0.0f, 1.2f, 0.0f };
    f32 radius = 8.0f;
    glm::vec3 boxCenter{ 0.0f, 1.2f, 0.0f };
    glm::vec3 boxExtents{ 8.0f, 5.0f, 8.0f };
    glm::vec3 color{ 1.0f };
    f32 intensity = 1.0f;
    f32 blendStrength = 1.0f;
    f32 falloff = 1.5f;
    bool enabled = true;
    ReflectionProbeCaptureSource captureSource =
        ReflectionProbeCaptureSource::CapturedScene;
    std::string captureAssetId;
    ReflectionProbeRefreshPolicy refreshPolicy =
        ReflectionProbeRefreshPolicy::Static;
    std::vector<u64> captureExcludedRenderableIdentities;
};

// Passive Debug/observability record. No readback, no image scanning.
struct SceneBuilderStats {
    u32 available = 0;
    u32 primitiveAvailabilityMask = 0;
    u32 objectCount = 0;
    u32 createdObjectCount = 0;
    u32 destroyedObjectCount = 0;
    u32 lightCount = 0;
    u32 createdLightCount = 0;
    u32 destroyedLightCount = 0;
    u32 liveMaterialCount = 0;
    u32 materialLibraryCount = 0;
    u32 frameMaterialBudget = 0;
    u32 sceneRenderableCount = 0;
    u64 selectedIdentity = 0;
    u64 selectedLightIdentity = 0;
    u32 selectedPrimitive = 0;
    u64 editRevision = 0;
    u32 transformEditCount = 0;
    u32 materialEditCount = 0;
    u32 lightEditCount = 0;
    u32 lightIconOverlayCount = 0;
    u32 lightIconHitTestCount = 0;
    u32 lightIconSelectionCount = 0;
    u32 renameCount = 0;
    // Counts selection changes imported from Scene3D (for example, a viewport
    // mouse pick) rather than changes made by the object combo box.
    u32 selectionSyncCount = 0;
    // Counts successful deletes requested through the editor shortcut path.
    u32 selectionShortcutDeleteCount = 0;
    // Analytic primitive ray picks used only by the Scene Builder viewport.
    u32 selectionRayQueryCount = 0;
    u32 selectionRayHitCount = 0;
    u32 materialDescriptorRefreshCount = 0;
    u32 lastCreateFailure = 0;
    u32 createFailureCount = 0;
    u32 environmentIblEnabled = 1;
    u32 environmentSkyboxEnabled = 0;
    f32 environmentDiffuseIntensity = 1.0f;
    f32 environmentSpecularIntensity = 1.0f;
    f32 environmentHorizonBlend = 0.22f;
    f32 environmentSkyboxIntensity = 1.0f;
    f32 environmentSkyboxBlur = 0.0f;
    u32 environmentLightingAsset = 0;
    u32 reflectionProbeCount = 0;
    u32 reflectionProbeEditCount = 0;
    u32 reflectionProbeCapturedSceneCount = 0;
    u32 reflectionProbeStaticRefreshCount = 0;
    u32 reflectionProbeExcludedRenderableCount = 0;
    i32 selectedReflectionProbeIndex = -1;
    // Set when at least one builder object needs the transparent scene path,
    // which the renderer keeps disabled until its temporal contract exists.
    u32 blendObjectCount = 0;
    u32 selfTestRan = 0;
    u32 selfTestPassed = 0;
    u32 selfTestFailedCheckMask = 0;
    std::array<u32, kSceneBuilderPrimitiveCount> primitiveCounts{};
    std::array<u32, 4> lightCounts{};
};

class SceneBuilder {
public:
    // Invoked after the builder adds a material to the render resources so the
    // owner can refresh material descriptor sets through the renderer.
    using MaterialsChangedCallback = std::function<void()>;
    using ImportedAssetCreator = std::function<SceneBuilderImportedAsset(
        SceneBuilderPrimitive primitive,
        const SceneBuilderObjectEdit& initialEdit
    )>;

    // The material library already owns the device/physical-device/command-pool
    // references needed to create a material, so the builder does not duplicate
    // them.
    SceneBuilder(
        VulkanMaterialLibrary& materialLibrary,
        VulkanRenderResources2D& renderResources,
        Scene3D& scene
    );

    SE_DISABLE_COPY(SceneBuilder);
    SE_DISABLE_MOVE(SceneBuilder);

    void SetMaterialsChangedCallback(MaterialsChangedCallback callback);
    void SetImportedAssetCreator(ImportedAssetCreator callback);

    bool Available() const;
    bool PrimitiveAvailable(SceneBuilderPrimitive primitive) const;

    u64 CreatePrimitive(SceneBuilderPrimitive primitive);
    // Creates objects from a comma-separated primitive spec. Each entry may
    // carry colon-separated modifiers, e.g. "cube:noshadow,plane".
    //
    // Data-driven so a headless lane can compose a scene without ImGui input.
    // Returns the number of objects created. Unknown primitive names and
    // unknown modifiers are counted as refused creates rather than silently
    // skipped, so a typo in a validation lane cannot look like success.
    //
    // Supported modifiers:
    //   noshadow - creates the object with cast shadow disabled.
    u32 CreateFromSpec(std::string_view spec);
    // Saves/loads builder-owned primitive/PBR/light state together with the
    // active viewport camera state. The document is intentionally local and
    // versioned.
    bool SaveToFile(
        const std::filesystem::path& path,
        const Camera3DState& cameraState
    );
    bool LoadFromFile(
        const std::filesystem::path& path,
        std::optional<Camera3DState>& cameraState
    );
    bool SaveDefaultDocument(const Camera3DState& cameraState);
    bool LoadDefaultDocument(std::optional<Camera3DState>& cameraState);
    // Seeds the default city once. The persisted layout id prevents later
    // editor saves from being overwritten or repopulated at every startup.
    SceneBuilderCityLayoutResult BootstrapDefaultCityLayout();
    bool HasDefaultCityLayout() const;
    const std::string& LastDocumentStatus() const;
    bool LastDocumentOperationFailed() const;
    static std::filesystem::path DefaultDocumentPath();
    bool DestroyObject(u64 renderIdentity);
    // Deletes only the currently selected builder object. This is the single
    // deletion path used by the Delete shortcut so foreign scene objects can
    // never be removed by the editor.
    bool DeleteSelectedObject();
    // Deletes the selected light when present, otherwise the selected object.
    // This is the editor's single keyboard deletion operation.
    bool DeleteSelectedEntity();
    bool RenameObject(u64 renderIdentity, std::string name);

    bool ReadObjectEdit(u64 renderIdentity, SceneBuilderObjectEdit& edit) const;
    bool ApplyObjectEdit(u64 renderIdentity, const SceneBuilderObjectEdit& edit);

    u64 CreateLight(SceneLightKind kind);
    bool DestroyLight(u64 lightIdentity);
    bool ReadLightEdit(u64 lightIdentity, SceneLightEdit& edit) const;
    bool ApplyLightEdit(u64 lightIdentity, const SceneLightEdit& edit);
    SceneEnvironment3D Environment() const;
    void SetEnvironment(const SceneEnvironment3D& environment);
    bool EnvironmentIblEnabled() const;
    void SetEnvironmentIblEnabled(bool enabled);
    u32 ReflectionProbeCount() const;
    i32 SelectedReflectionProbeIndex() const;
    bool SelectReflectionProbe(u32 index);
    bool CreateReflectionProbe();
    bool DuplicateReflectionProbe(u32 index);
    bool DestroyReflectionProbe(u32 index);
    bool ReadReflectionProbeEdit(
        u32 index,
        SceneBuilderReflectionProbeEdit& edit
    ) const;
    bool ApplyReflectionProbeEdit(
        u32 index,
        const SceneBuilderReflectionProbeEdit& edit
    );
    // Compatibility convenience for existing single-probe callers. New editor
    // code addresses its selected probe explicitly by index.
    bool ReadReflectionProbeEdit(SceneBuilderReflectionProbeEdit& edit) const;
    bool ApplyReflectionProbeEdit(const SceneBuilderReflectionProbeEdit& edit);

    u64 SelectedIdentity() const;
    bool SelectObject(u64 renderIdentity);
    u64 SelectedLightIdentity() const;
    bool SelectLight(u64 lightIdentity);
    // Editor-only light-icon telemetry. This does not change scene state.
    void SetLightIconOverlayCount(u32 count);
    void RecordLightIconPick(bool selected);
    // Chooses the nearest actual primitive surface, rather than its bounds.
    bool SelectAlongRay(const glm::vec3& origin, const glm::vec3& direction);
    // Re-resolves the selection from the scene so external selection changes
    // (object picking, the existing ImGui picker) stay in sync.
    void SyncSelectionFromScene();

    const std::vector<SceneBuilderObject>& Objects() const;
    const SceneBuilderObject* FindObject(u64 renderIdentity) const;
    const std::vector<SceneBuilderLight>& Lights() const;
    const SceneBuilderLight* FindLight(u64 lightIdentity) const;
    SceneBuilderStats Stats() const;

    static std::string_view PrimitiveName(SceneBuilderPrimitive primitive);
    static std::string_view PrimitiveMeshId(SceneBuilderPrimitive primitive);
    static std::string_view PrimitiveAssetPath(SceneBuilderPrimitive primitive);

#if !defined(NDEBUG)
    // Deterministic create/mutate/destroy sequence used by the strict data
    // gate. Debug-only; never compiled into Release.
    bool RunSelfTest();
#endif

private:
    SceneBuilderObject* FindMutableObject(u64 renderIdentity);
    SceneBuilderLight* FindMutableLight(u64 lightIdentity);
    u64 CreateImportedAsset(SceneBuilderPrimitive primitive);
    void CreateFromSpecEntry(std::string_view entry, u32& createdCount);
    bool SetSelectedIdentity(u64 renderIdentity);
    bool SetSelectedLightIdentity(u64 lightIdentity);
    void AdoptPrimaryDirectionalLight();
    SceneBuilderReflectionProbeEdit LegacyReflectionProbeEdit() const;
    void RefreshMaterials();

    VulkanMaterialLibrary& m_MaterialLibrary;
    VulkanRenderResources2D& m_RenderResources;
    Scene3D& m_Scene;
    MaterialsChangedCallback m_MaterialsChanged;
    ImportedAssetCreator m_ImportedAssetCreator;
    std::vector<SceneBuilderObject> m_Objects;
    std::vector<SceneBuilderLight> m_Lights;
    u32 m_PrimitiveAvailabilityMask = 0;
    u32 m_NextObjectOrdinal = 1;
    u32 m_CreatedObjectCount = 0;
    u32 m_DestroyedObjectCount = 0;
    u32 m_CreatedLightCount = 0;
    u32 m_DestroyedLightCount = 0;
    u32 m_TransformEditCount = 0;
    u32 m_MaterialEditCount = 0;
    u32 m_LightEditCount = 0;
    u32 m_LightIconOverlayCount = 0;
    u32 m_LightIconHitTestCount = 0;
    u32 m_LightIconSelectionCount = 0;
    u32 m_ReflectionProbeEditCount = 0;
    u32 m_RenameCount = 0;
    u32 m_SelectionSyncCount = 0;
    u32 m_SelectionShortcutDeleteCount = 0;
    u32 m_SelectionRayQueryCount = 0;
    u32 m_SelectionRayHitCount = 0;
    u32 m_MaterialDescriptorRefreshCount = 0;
    u32 m_CreateFailureCount = 0;
    SceneBuilderCreateFailure m_LastCreateFailure = SceneBuilderCreateFailure::None;
    u64 m_SelectedIdentity = 0;
    u64 m_SelectedLightIdentity = 0;
    i32 m_SelectedReflectionProbeIndex = -1;
    u64 m_EditRevision = 0;
    u32 m_SelfTestRan = 0;
    u32 m_SelfTestPassed = 0;
    u32 m_SelfTestFailedCheckMask = 0;
    std::string m_LastDocumentStatus;
    std::string m_StartupLayoutId;
    bool m_LastDocumentOperationFailed = false;
};

}
