#include "scene/scene_builder.h"

#include "renderer/vulkan/material_library.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/render_resources_2d.h"
#if defined(SE_ENABLE_SCENE_BUILDER_GIZMO)
#include "renderer/vulkan/scene_builder_gizmo.h"
#endif
#include "renderer/vulkan/uniform_buffer.h"
#include "scene/mesh_factory.h"
#include "scene/renderable_3d.h"
#include "scene/scene_3d.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>

namespace se {

namespace {

// One opaque white texel. Editor materials are factor-driven, so textureMix
// stays at zero and the albedo texture is only a legal descriptor binding.
constexpr std::array<u8, 4> kSceneBuilderWhiteTexel{ 255, 255, 255, 255 };

constexpr u32 kSceneBuilderMaxObjects = 128;
constexpr f32 kPickEpsilon = 0.00001f;
constexpr std::string_view kSceneBuilderDocumentFormat = "SelfEngineSceneBuilder";
// Version 3 adds the builder-owned light inventory, version 4 adds an
// editor-only directional-light gizmo anchor, version 5 adds the IBL gate,
// version 6 persists the complete scene environment, version 7 adds its
// lighting asset, version 8 makes the local reflection probe editable scene
// data, version 9 records probe-local capture exclusions, version 10
// separates the cubemap capture origin from the parallax proxy center, and
// version 11 replaces the singleton probe with an authored collection,
// version 12 persists imported editor asset identities, and version 13 records
// one-time startup layouts so later user edits are never repopulated.
// Older documents remain readable.
constexpr i32 kSceneBuilderDocumentVersion = 13;
constexpr i32 kSceneBuilderCameraDocumentVersion = 2;
constexpr i32 kSceneBuilderLightDocumentVersion = 3;
constexpr i32 kSceneBuilderDirectionalGizmoDocumentVersion = 4;
constexpr i32 kSceneBuilderEnvironmentIblDocumentVersion = 5;
constexpr i32 kSceneBuilderEnvironmentDocumentVersion = 6;
constexpr i32 kSceneBuilderEnvironmentLightingAssetDocumentVersion = 7;
constexpr i32 kSceneBuilderReflectionProbeDocumentVersion = 8;
constexpr i32 kSceneBuilderReflectionProbeExclusionDocumentVersion = 9;
constexpr i32 kSceneBuilderReflectionProbeSpatialDocumentVersion = 10;
constexpr i32 kSceneBuilderReflectionProbeCollectionDocumentVersion = 11;
constexpr i32 kSceneBuilderImportedAssetDocumentVersion = 12;
constexpr i32 kSceneBuilderStartupLayoutDocumentVersion = 13;
constexpr i32 kSceneBuilderLegacyDocumentVersion = 1;
constexpr std::string_view kSceneBuilderDefaultCityLayoutId = "default-city-v3";

struct SceneBuilderDocumentObject {
    SceneBuilderPrimitive primitive = SceneBuilderPrimitive::Cube;
    std::string name;
    std::string assetPath;
    SceneBuilderObjectEdit edit{};
};

struct SceneBuilderDocumentLight {
    SceneLightEdit edit{};
};

struct SceneBuilderDocument {
    std::vector<SceneBuilderDocumentObject> objects;
    std::vector<SceneBuilderDocumentLight> lights;
    // Version 3 made lights explicit document state. Earlier documents used
    // the Scene Builder startup key light implicitly.
    bool lightsAreExplicit = false;
    // Old documents did not serialize environment state, so defaults preserve
    // their established appearance during migration.
    SceneEnvironment3D environment{};
    std::vector<SceneBuilderReflectionProbeEdit> reflectionProbes;
    bool reflectionProbesAreExplicit = false;
    std::optional<Camera3DState> cameraState;
    std::string startupLayoutId;
};

struct PrimitiveDescriptor {
    std::string_view name;
    std::string_view meshId;
    glm::vec3 defaultScale;
};

constexpr std::array<PrimitiveDescriptor, kSceneBuilderPrimitiveCount>
kSceneBuilderPrimitives{ {
    { "Cube", "Cube", glm::vec3(1.0f, 1.0f, 1.0f) },
    { "Plane", "Plane", glm::vec3(4.0f, 1.0f, 4.0f) },
    { "Sphere", "Sphere", glm::vec3(1.0f, 1.0f, 1.0f) },
    { "Cone", "Cone", glm::vec3(1.0f, 1.0f, 1.0f) },
    { "Lvjuren", "", glm::vec3(1.0f, 1.0f, 1.0f) },
    { "Building1", "", glm::vec3(1.0f, 1.0f, 1.0f) },
    { "Building2", "", glm::vec3(1.0f, 1.0f, 1.0f) },
    { "Building3", "", glm::vec3(5.2704f) },
    { "Building4", "", glm::vec3(5.9778f) },
    { "Car1", "", glm::vec3(0.2878092f) },
    { "Car2", "", glm::vec3(0.2812f) }
} };

constexpr std::string_view kLvjurenAssetPath = "assets/models/lvjuren.glb";
constexpr std::string_view kBuilding1AssetPath =
    "assets/models/scene_builder/building1.glb";
constexpr std::string_view kBuilding2AssetPath =
    "assets/models/scene_builder/building2.glb";
constexpr std::string_view kBuilding3AssetPath =
    "assets/models/scene_builder/buiding3.glb";
constexpr std::string_view kBuilding4AssetPath =
    "assets/models/scene_builder/building4.glb";
constexpr std::string_view kCar1AssetPath =
    "assets/models/scene_builder/car1.glb";
constexpr std::string_view kCar2AssetPath =
    "assets/models/scene_builder/car2.glb";

constexpr std::string_view kLegacyBuilding1AssetPath = "assets/models/building1.glb";
constexpr std::string_view kLegacyBuilding2AssetPath = "assets/models/building2.glb";
constexpr std::string_view kLegacyBuilding3AssetPath = "assets/models/buiding3.glb";
constexpr std::string_view kLegacyBuilding4AssetPath = "assets/models/building4.glb";
constexpr std::string_view kLegacyCar1AssetPath = "assets/models/car1.glb";
constexpr std::string_view kLegacyCar2AssetPath = "assets/models/car2.glb";

bool IsImportedAsset(SceneBuilderPrimitive primitive) {
    switch (primitive) {
        case SceneBuilderPrimitive::Lvjuren:
        case SceneBuilderPrimitive::Building1:
        case SceneBuilderPrimitive::Building2:
        case SceneBuilderPrimitive::Building3:
        case SceneBuilderPrimitive::Building4:
        case SceneBuilderPrimitive::Car1:
        case SceneBuilderPrimitive::Car2:
            return true;
        default:
            return false;
    }
}

std::string_view AssetPathFor(SceneBuilderPrimitive primitive) {
    return SceneBuilder::PrimitiveAssetPath(primitive);
}

bool AssetPathMatchesPrimitive(
    SceneBuilderPrimitive primitive,
    std::string_view assetPath
) {
    if (assetPath == AssetPathFor(primitive)) {
        return true;
    }

    switch (primitive) {
        case SceneBuilderPrimitive::Building1:
            return assetPath == kLegacyBuilding1AssetPath;
        case SceneBuilderPrimitive::Building2:
            return assetPath == kLegacyBuilding2AssetPath;
        case SceneBuilderPrimitive::Building3:
            return assetPath == kLegacyBuilding3AssetPath;
        case SceneBuilderPrimitive::Building4:
            return assetPath == kLegacyBuilding4AssetPath;
        case SceneBuilderPrimitive::Car1:
            return assetPath == kLegacyCar1AssetPath;
        case SceneBuilderPrimitive::Car2:
            return assetPath == kLegacyCar2AssetPath;
        default:
            return false;
    }
}

struct SceneBuilderBounds {
    glm::vec3 min{ 0.0f };
    glm::vec3 max{ 0.0f };
    bool valid = false;
};

void ExpandSceneBuilderBounds(SceneBuilderBounds& bounds, const glm::vec3& point) {
    if (!bounds.valid) {
        bounds.min = point;
        bounds.max = point;
        bounds.valid = true;
        return;
    }

    bounds.min = glm::min(bounds.min, point);
    bounds.max = glm::max(bounds.max, point);
}

SceneBuilderBounds BoundsForBuilderObject(const SceneBuilderObject& object) {
    if (object.renderable == nullptr) {
        return {};
    }

    glm::vec3 localMin{ -0.5f };
    glm::vec3 localMax{ 0.5f };
    if (object.primitive == SceneBuilderPrimitive::Plane) {
        localMin.y = 0.0f;
        localMax.y = 0.0f;
    } else if (object.primitive == SceneBuilderPrimitive::Cone) {
        localMin = { -0.5f, 0.0f, -0.5f };
        localMax = { 0.5f, 1.0f, 0.5f };
    }

    const glm::mat4 model = object.renderable->Transform().Matrix();
    SceneBuilderBounds bounds{};
    for (u32 x = 0u; x < 2u; ++x) {
        for (u32 y = 0u; y < 2u; ++y) {
            for (u32 z = 0u; z < 2u; ++z) {
                const glm::vec3 localPoint{
                    x == 0u ? localMin.x : localMax.x,
                    y == 0u ? localMin.y : localMax.y,
                    z == 0u ? localMin.z : localMax.z
                };
                ExpandSceneBuilderBounds(
                    bounds,
                    glm::vec3(model * glm::vec4(localPoint, 1.0f))
                );
            }
        }
    }
    return bounds;
}

void EnsureSceneBuilderReflectionCaptureProbe(Scene3D& scene) {
    if (!scene.ReflectionProbes().empty()) {
        return;
    }

    // A new empty document starts with one ordinary scene-owned probe. Once
    // saved, its full state is restored from the document instead of relying
    // on this bootstrap value.
    scene.CreateReflectionProbe(
        "Scene Builder Reflection Probe",
        { 0.0f, 1.20f, 0.0f },
        8.0f,
        { 8.0f, 5.0f, 8.0f },
        { 1.0f, 1.0f, 1.0f },
        1.0f,
        1.0f,
        1.5f,
        ReflectionProbeCaptureSource::CapturedScene,
        {},
        ReflectionProbeRefreshPolicy::Static
    );
}

const PrimitiveDescriptor& DescriptorFor(SceneBuilderPrimitive primitive) {
    const u32 index = std::min(
        static_cast<u32>(primitive),
        kSceneBuilderPrimitiveCount - 1u
    );
    return kSceneBuilderPrimitives[index];
}

MaterialProperties DefaultEditorMaterialProperties() {
    MaterialProperties properties{};
    properties.baseColorFactor = { 0.82f, 0.82f, 0.84f, 1.0f };
    // Factor-only shading: no albedo/metallic-roughness texture contribution.
    properties.textureMix = 0.0f;
    // custom.xyz stays at the legacy forward key-light direction default and
    // custom.w is its ambient term; the deferred path ignores both.
    properties.custom = { -0.45f, -0.82f, -0.35f, 0.22f };
    properties.viewControls = { 1.0f, 0.35f, 24.0f, 0.0f };
    // cameraControls.x/.y are the scalar metallic/roughness inputs.
    // cameraControls.z is the texture-versus-scalar blend and must stay 0.
    properties.cameraControls = { 0.0f, 0.5f, 0.0f, 0.0f };
    properties.emissiveFactor = { 0.0f, 0.0f, 0.0f, 1.0f };
    properties.alphaMode = MaterialAlphaMode::Opaque;
    properties.renderClass = MaterialRenderClass::DeferredOpaque;
    properties.doubleSided = false;

    return properties;
}

std::string_view TrimSpecToken(std::string_view token) {
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
        token.remove_prefix(1);
    }
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
        token.remove_suffix(1);
    }

    return token;
}

std::string LowerSpecToken(std::string_view token) {
    std::string lowered(token);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );

    return lowered;
}

bool NearlyEqual(f32 left, f32 right) {
    return std::abs(left - right) <= 0.000001f;
}

bool NearlyEqual(const glm::vec3& left, const glm::vec3& right) {
    return NearlyEqual(left.x, right.x) &&
        NearlyEqual(left.y, right.y) &&
        NearlyEqual(left.z, right.z);
}

bool NearlyEqual(const glm::vec4& left, const glm::vec4& right) {
    return NearlyEqual(left.x, right.x) &&
        NearlyEqual(left.y, right.y) &&
        NearlyEqual(left.z, right.z) &&
        NearlyEqual(left.w, right.w);
}

const char* AlphaModeDocumentName(MaterialAlphaMode mode) {
    switch (mode) {
        case MaterialAlphaMode::Opaque:
            return "opaque";
        case MaterialAlphaMode::Mask:
            return "mask";
        case MaterialAlphaMode::Blend:
            return "blend";
    }

    return "opaque";
}

std::optional<MaterialAlphaMode> AlphaModeFromDocumentName(
    std::string_view name
) {
    if (name == "opaque") {
        return MaterialAlphaMode::Opaque;
    }
    if (name == "mask") {
        return MaterialAlphaMode::Mask;
    }
    if (name == "blend") {
        return MaterialAlphaMode::Blend;
    }

    return std::nullopt;
}

std::optional<SceneBuilderPrimitive> PrimitiveFromDocumentName(
    std::string_view name
) {
    for (u32 index = 0; index < kSceneBuilderPrimitiveCount; ++index) {
        const SceneBuilderPrimitive primitive =
            static_cast<SceneBuilderPrimitive>(index);
        if (DescriptorFor(primitive).name == name) {
            return primitive;
        }
    }

    return std::nullopt;
}

const char* LightKindDocumentName(SceneLightKind kind) {
    switch (kind) {
        case SceneLightKind::Directional:
            return "directional";
        case SceneLightKind::Point:
            return "point";
        case SceneLightKind::Spot:
            return "spot";
        case SceneLightKind::Rect:
            return "rect";
    }

    return "point";
}

std::optional<SceneLightKind> LightKindFromDocumentName(
    std::string_view name
) {
    if (name == "directional") {
        return SceneLightKind::Directional;
    }
    if (name == "point") {
        return SceneLightKind::Point;
    }
    if (name == "spot") {
        return SceneLightKind::Spot;
    }
    if (name == "rect") {
        return SceneLightKind::Rect;
    }

    return std::nullopt;
}

const char* ReflectionProbeCaptureSourceDocumentName(
    ReflectionProbeCaptureSource source
) {
    switch (source) {
        case ReflectionProbeCaptureSource::None:
            return "none";
        case ReflectionProbeCaptureSource::BuiltInProcedural:
            return "built-in-procedural";
        case ReflectionProbeCaptureSource::AuthoredCubemap:
            return "authored-cubemap";
        case ReflectionProbeCaptureSource::CapturedScene:
            return "captured-scene";
    }

    return "none";
}

std::optional<ReflectionProbeCaptureSource>
ReflectionProbeCaptureSourceFromDocumentName(std::string_view name) {
    if (name == "none") {
        return ReflectionProbeCaptureSource::None;
    }
    if (name == "built-in-procedural") {
        return ReflectionProbeCaptureSource::BuiltInProcedural;
    }
    if (name == "authored-cubemap") {
        return ReflectionProbeCaptureSource::AuthoredCubemap;
    }
    if (name == "captured-scene") {
        return ReflectionProbeCaptureSource::CapturedScene;
    }

    return std::nullopt;
}

const char* ReflectionProbeRefreshPolicyDocumentName(
    ReflectionProbeRefreshPolicy policy
) {
    switch (policy) {
        case ReflectionProbeRefreshPolicy::Static:
            return "static";
        case ReflectionProbeRefreshPolicy::FileSignature:
            return "file-signature";
        case ReflectionProbeRefreshPolicy::Forced:
            return "forced";
        case ReflectionProbeRefreshPolicy::SceneDirty:
            return "scene-dirty";
    }

    return "static";
}

std::optional<ReflectionProbeRefreshPolicy>
ReflectionProbeRefreshPolicyFromDocumentName(std::string_view name) {
    if (name == "static") {
        return ReflectionProbeRefreshPolicy::Static;
    }
    if (name == "file-signature") {
        return ReflectionProbeRefreshPolicy::FileSignature;
    }
    if (name == "forced") {
        return ReflectionProbeRefreshPolicy::Forced;
    }
    if (name == "scene-dirty") {
        return ReflectionProbeRefreshPolicy::SceneDirty;
    }

    return std::nullopt;
}

nlohmann::json JsonVec3(const glm::vec3& value) {
    return nlohmann::json::array({ value.x, value.y, value.z });
}

nlohmann::json JsonVec4(const glm::vec4& value) {
    return nlohmann::json::array({ value.x, value.y, value.z, value.w });
}

bool ReadJsonNumber(const nlohmann::json& value, f32& output) {
    if (!value.is_number()) {
        return false;
    }

    const f64 number = value.get<f64>();
    if (!std::isfinite(number) ||
        number < -static_cast<f64>(std::numeric_limits<f32>::max()) ||
        number > static_cast<f64>(std::numeric_limits<f32>::max())) {
        return false;
    }

    output = static_cast<f32>(number);
    return true;
}

bool ReadJsonVec3(const nlohmann::json& value, glm::vec3& output) {
    if (!value.is_array() || value.size() != 3u) {
        return false;
    }

    return ReadJsonNumber(value[0], output.x) &&
        ReadJsonNumber(value[1], output.y) &&
        ReadJsonNumber(value[2], output.z);
}

bool ReadJsonVec4(const nlohmann::json& value, glm::vec4& output) {
    if (!value.is_array() || value.size() != 4u) {
        return false;
    }

    return ReadJsonNumber(value[0], output.x) &&
        ReadJsonNumber(value[1], output.y) &&
        ReadJsonNumber(value[2], output.z) &&
        ReadJsonNumber(value[3], output.w);
}

bool DocumentEditIsInRange(const SceneBuilderObjectEdit& edit) {
    const auto unitRange = [](f32 value) {
        return value >= 0.0f && value <= 1.0f;
    };

    return edit.scale.x >= 0.001f &&
        edit.scale.y >= 0.001f &&
        edit.scale.z >= 0.001f &&
        unitRange(edit.baseColor.r) &&
        unitRange(edit.baseColor.g) &&
        unitRange(edit.baseColor.b) &&
        unitRange(edit.baseColor.a) &&
        unitRange(edit.metallic) &&
        edit.roughness >= 0.04f && edit.roughness <= 1.0f &&
        unitRange(edit.emissive.r) &&
        unitRange(edit.emissive.g) &&
        unitRange(edit.emissive.b) &&
        unitRange(edit.alphaCutoff);
}

bool DocumentCameraStateIsValid(const Camera3DState& state) {
    const auto finite = [](f32 value) {
        return std::isfinite(value);
    };

    return finite(state.position.x) &&
        finite(state.position.y) &&
        finite(state.position.z) &&
        finite(state.forward.x) &&
        finite(state.forward.y) &&
        finite(state.forward.z) &&
        finite(state.distance) &&
        finite(state.fovScale) &&
        glm::length(state.forward) > 0.0001f &&
        state.distance >= 0.0f &&
        state.fovScale >= 0.35f && state.fovScale <= 2.3f;
}

bool DocumentLightEditIsInRange(const SceneLightEdit& edit) {
    const auto finiteVector = [](const glm::vec3& value) {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    };
    const auto finiteScalar = [](f32 value) {
        return std::isfinite(value);
    };
    if (edit.name.empty() ||
        !finiteVector(edit.position) ||
        !finiteVector(edit.direction) ||
        !finiteVector(edit.color) ||
        !finiteScalar(edit.intensity) ||
        !finiteScalar(edit.radius) ||
        !finiteScalar(edit.sourceRadius) ||
        !finiteScalar(edit.innerConeDegrees) ||
        !finiteScalar(edit.outerConeDegrees) ||
        !finiteScalar(edit.width) ||
        !finiteScalar(edit.height) ||
        !finiteScalar(edit.ambient) ||
        !finiteScalar(edit.specular) ||
        !finiteScalar(edit.angularRadiusRadians) ||
        glm::dot(edit.direction, edit.direction) <= 0.0001f ||
        edit.intensity < 0.0f ||
        edit.radius < 0.0f ||
        edit.sourceRadius < 0.0f ||
        edit.width < 0.0f ||
        edit.height < 0.0f ||
        edit.ambient < 0.0f ||
        edit.specular < 0.0f ||
        edit.angularRadiusRadians < 0.0f ||
        edit.angularRadiusRadians > 0.05f ||
        edit.color.x < 0.0f ||
        edit.color.y < 0.0f ||
        edit.color.z < 0.0f) {
        return false;
    }

    if (edit.kind == SceneLightKind::Spot) {
        return edit.innerConeDegrees >= 0.05f &&
            edit.outerConeDegrees >= edit.innerConeDegrees &&
            edit.outerConeDegrees <= 89.0f;
    }
    if (edit.kind == SceneLightKind::Rect) {
        return edit.specular <= 1.0f;
    }

    return true;
}

bool DocumentReflectionProbeEditIsInRange(
    const SceneBuilderReflectionProbeEdit& edit
) {
    const auto finiteVector = [](const glm::vec3& value) {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    };
    const auto finiteScalar = [](f32 value) {
        return std::isfinite(value);
    };
    const bool captureSourceIsKnown =
        edit.captureSource == ReflectionProbeCaptureSource::None ||
        edit.captureSource == ReflectionProbeCaptureSource::BuiltInProcedural ||
        edit.captureSource == ReflectionProbeCaptureSource::AuthoredCubemap ||
        edit.captureSource == ReflectionProbeCaptureSource::CapturedScene;
    const bool refreshPolicyIsKnown =
        edit.refreshPolicy == ReflectionProbeRefreshPolicy::Static ||
        edit.refreshPolicy == ReflectionProbeRefreshPolicy::FileSignature ||
        edit.refreshPolicy == ReflectionProbeRefreshPolicy::Forced ||
        edit.refreshPolicy == ReflectionProbeRefreshPolicy::SceneDirty;
    if (edit.name.empty() ||
        !finiteVector(edit.center) ||
        !finiteVector(edit.boxCenter) ||
        !finiteVector(edit.boxExtents) ||
        !finiteVector(edit.color) ||
        !finiteScalar(edit.radius) ||
        !finiteScalar(edit.intensity) ||
        !finiteScalar(edit.blendStrength) ||
        !finiteScalar(edit.falloff) ||
        glm::any(glm::lessThan(edit.center, glm::vec3(-256.0f))) ||
        glm::any(glm::greaterThan(edit.center, glm::vec3(256.0f))) ||
        glm::any(glm::lessThan(edit.boxCenter, glm::vec3(-256.0f))) ||
        glm::any(glm::greaterThan(edit.boxCenter, glm::vec3(256.0f))) ||
        edit.radius < 0.01f || edit.radius > 256.0f ||
        edit.boxExtents.x < 0.01f || edit.boxExtents.x > 256.0f ||
        edit.boxExtents.y < 0.01f || edit.boxExtents.y > 256.0f ||
        edit.boxExtents.z < 0.01f || edit.boxExtents.z > 256.0f ||
        edit.color.x < 0.0f || edit.color.x > 4.0f ||
        edit.color.y < 0.0f || edit.color.y > 4.0f ||
        edit.color.z < 0.0f || edit.color.z > 4.0f ||
        edit.intensity < 0.0f || edit.intensity > 4.0f ||
        edit.blendStrength < 0.0f || edit.blendStrength > 1.0f ||
        edit.falloff < 0.25f || edit.falloff > 8.0f ||
        !captureSourceIsKnown || !refreshPolicyIsKnown ||
        edit.captureExcludedRenderableIdentities.size() >
            kSceneBuilderMaxObjects ||
        !std::all_of(
            edit.captureExcludedRenderableIdentities.begin(),
            edit.captureExcludedRenderableIdentities.end(),
            [](u64 identity) { return identity != 0u; }
        ) ||
        !std::is_sorted(
            edit.captureExcludedRenderableIdentities.begin(),
            edit.captureExcludedRenderableIdentities.end()
        ) ||
        std::adjacent_find(
            edit.captureExcludedRenderableIdentities.begin(),
            edit.captureExcludedRenderableIdentities.end()
        ) != edit.captureExcludedRenderableIdentities.end()) {
        return false;
    }

    // An authored source may be configured before an asset is chosen. The
    // renderer records that condition and keeps the normal IBL fallback active
    // until the asset becomes available, so the editor must not reject this
    // ordinary intermediate authoring state.
    return true;
}

bool ReadSceneBuilderDocument(
    const std::filesystem::path& path,
    SceneBuilderDocument& documentOutput,
    std::string& failure
) {
    std::ifstream input(path);
    if (!input) {
        failure = "Could not open scene document.";
        return false;
    }

    try {
        nlohmann::json document;
        input >> document;
        if (!document.is_object() ||
            document.value("format", std::string{}) !=
                std::string(kSceneBuilderDocumentFormat) ||
            !document.contains("objects") || !document["objects"].is_array()) {
            failure = "Unsupported scene document format or version.";
            return false;
        }

        const i32 version = document.value("version", 0);
        if (version != kSceneBuilderLegacyDocumentVersion &&
            version != kSceneBuilderCameraDocumentVersion &&
            version != kSceneBuilderLightDocumentVersion &&
            version != kSceneBuilderDirectionalGizmoDocumentVersion &&
            version != kSceneBuilderEnvironmentIblDocumentVersion &&
            version != kSceneBuilderEnvironmentDocumentVersion &&
            version != kSceneBuilderEnvironmentLightingAssetDocumentVersion &&
            version != kSceneBuilderReflectionProbeDocumentVersion &&
            version != kSceneBuilderReflectionProbeExclusionDocumentVersion &&
            version != kSceneBuilderReflectionProbeSpatialDocumentVersion &&
            version != kSceneBuilderReflectionProbeCollectionDocumentVersion &&
            version != kSceneBuilderImportedAssetDocumentVersion &&
            version != kSceneBuilderDocumentVersion) {
            failure = "Unsupported scene document format or version.";
            return false;
        }

        const nlohmann::json& documentObjects = document["objects"];
        if (documentObjects.size() > kSceneBuilderMaxObjects) {
            failure = "Scene document exceeds the object limit.";
            return false;
        }

        SceneBuilderDocument parsed{};
        if (version >= kSceneBuilderStartupLayoutDocumentVersion) {
            if (!document.contains("startupLayout") ||
                !document["startupLayout"].is_string()) {
                failure = "Scene document contains invalid startup layout state.";
                return false;
            }
            parsed.startupLayoutId =
                document["startupLayout"].get<std::string>();
            if (parsed.startupLayoutId.size() > 64u) {
                failure = "Scene document contains invalid startup layout state.";
                return false;
            }
        }
        if (version >= kSceneBuilderEnvironmentIblDocumentVersion) {
            if (!document.contains("environment") ||
                !document["environment"].is_object() ||
                !document["environment"].contains("iblEnabled") ||
                !document["environment"]["iblEnabled"].is_boolean()) {
                failure = "Scene document contains invalid environment state.";
                return false;
            }
            parsed.environment.iblEnabled =
                document["environment"]["iblEnabled"].get<bool>();
            if (version >= kSceneBuilderEnvironmentDocumentVersion) {
                const nlohmann::json& environment = document["environment"];
                if (!environment.contains("diffuseIntensity") ||
                    !environment.contains("specularIntensity") ||
                    !environment.contains("horizonBlend") ||
                    !environment.contains("skyboxEnabled") ||
                    !environment["skyboxEnabled"].is_boolean() ||
                    !environment.contains("skyboxIntensity") ||
                    !environment.contains("skyboxBlur") ||
                    !ReadJsonNumber(
                        environment["diffuseIntensity"],
                        parsed.environment.diffuseIntensity
                    ) ||
                    !ReadJsonNumber(
                        environment["specularIntensity"],
                        parsed.environment.specularIntensity
                    ) ||
                    !ReadJsonNumber(
                        environment["horizonBlend"],
                        parsed.environment.horizonBlend
                    ) ||
                    !ReadJsonNumber(
                        environment["skyboxIntensity"],
                        parsed.environment.skyboxIntensity
                    ) ||
                    !ReadJsonNumber(
                        environment["skyboxBlur"],
                        parsed.environment.skyboxBlur
                    )) {
                    failure = "Scene document contains invalid environment state.";
                    return false;
                }
                parsed.environment.skyboxEnabled =
                    environment["skyboxEnabled"].get<bool>();
                if (parsed.environment.diffuseIntensity < 0.0f ||
                    parsed.environment.diffuseIntensity > 4.0f ||
                    parsed.environment.specularIntensity < 0.0f ||
                    parsed.environment.specularIntensity > 4.0f ||
                    parsed.environment.horizonBlend < 0.0f ||
                    parsed.environment.horizonBlend > 1.0f ||
                    parsed.environment.skyboxIntensity < 0.0f ||
                    parsed.environment.skyboxIntensity > 4.0f ||
                    parsed.environment.skyboxBlur < 0.0f ||
                    parsed.environment.skyboxBlur > 8.0f) {
                    failure = "Scene document contains out-of-range environment state.";
                    return false;
                }
                if (version >= kSceneBuilderEnvironmentLightingAssetDocumentVersion) {
                    if (!environment.contains("lightingAsset") ||
                        (!environment["lightingAsset"].is_number_integer() &&
                            !environment["lightingAsset"].is_number_unsigned())) {
                        failure = "Scene document contains invalid environment asset.";
                        return false;
                    }
                    const i64 lightingAsset = environment["lightingAsset"].get<i64>();
                    if (lightingAsset < static_cast<i64>(
                            SceneEnvironmentLightingAsset::RendererDefault
                        ) ||
                        lightingAsset > static_cast<i64>(
                            SceneEnvironmentLightingAsset::StudioPanorama
                        )) {
                        failure = "Scene document contains unknown environment asset.";
                        return false;
                    }
                    parsed.environment.lightingAsset =
                        static_cast<SceneEnvironmentLightingAsset>(lightingAsset);
                }
            }
        }
        const auto parseReflectionProbe = [&failure](
            const nlohmann::json& source,
            bool spatialFields,
            bool exclusionFields,
            SceneBuilderReflectionProbeEdit& reflectionProbe
        ) {
            if (!source.is_object() ||
                !source.contains("name") || !source["name"].is_string() ||
                !source.contains("enabled") || !source["enabled"].is_boolean() ||
                !(spatialFields
                    ? source.contains("capturePosition")
                    : source.contains("center")) ||
                !source.contains("radius") ||
                !source.contains("boxExtents") ||
                !source.contains("color") ||
                !source.contains("intensity") ||
                !source.contains("blendStrength") ||
                !source.contains("falloff") ||
                !source.contains("captureSource") ||
                !source["captureSource"].is_string() ||
                !source.contains("captureAssetId") ||
                !source["captureAssetId"].is_string() ||
                !source.contains("refreshPolicy") ||
                !source["refreshPolicy"].is_string()) {
                failure = "Scene document contains incomplete reflection probe state.";
                return false;
            }

            reflectionProbe.name = source["name"].get<std::string>();
            reflectionProbe.enabled = source["enabled"].get<bool>();
            reflectionProbe.captureAssetId = source["captureAssetId"].get<std::string>();
            const nlohmann::json& capturePosition = spatialFields
                ? source["capturePosition"]
                : source["center"];
            if (!ReadJsonVec3(capturePosition, reflectionProbe.center) ||
                !ReadJsonNumber(source["radius"], reflectionProbe.radius) ||
                !ReadJsonVec3(source["boxExtents"], reflectionProbe.boxExtents) ||
                !ReadJsonVec3(source["color"], reflectionProbe.color) ||
                !ReadJsonNumber(source["intensity"], reflectionProbe.intensity) ||
                !ReadJsonNumber(source["blendStrength"], reflectionProbe.blendStrength) ||
                !ReadJsonNumber(source["falloff"], reflectionProbe.falloff)) {
                failure = "Scene document contains invalid reflection probe state.";
                return false;
            }
            reflectionProbe.boxCenter = reflectionProbe.center;
            if (spatialFields &&
                (!source.contains("boxCenter") ||
                    !ReadJsonVec3(source["boxCenter"], reflectionProbe.boxCenter))) {
                failure = "Scene document contains invalid reflection probe proxy state.";
                return false;
            }
            const std::optional<ReflectionProbeCaptureSource> captureSource =
                ReflectionProbeCaptureSourceFromDocumentName(
                    source["captureSource"].get<std::string>()
                );
            const std::optional<ReflectionProbeRefreshPolicy> refreshPolicy =
                ReflectionProbeRefreshPolicyFromDocumentName(
                    source["refreshPolicy"].get<std::string>()
                );
            if (!captureSource.has_value() || !refreshPolicy.has_value()) {
                failure = "Scene document contains unknown reflection probe state.";
                return false;
            }
            reflectionProbe.captureSource = *captureSource;
            reflectionProbe.refreshPolicy = *refreshPolicy;
            if (exclusionFields) {
                if (!source.contains("captureExcludedRenderableIdentities") ||
                    !source["captureExcludedRenderableIdentities"].is_array() ||
                    source["captureExcludedRenderableIdentities"].size() >
                        kSceneBuilderMaxObjects) {
                    failure = "Scene document contains invalid reflection probe exclusions.";
                    return false;
                }
                for (const nlohmann::json& identity :
                     source["captureExcludedRenderableIdentities"]) {
                    if (!identity.is_number_unsigned()) {
                        failure = "Scene document contains invalid reflection probe exclusions.";
                        return false;
                    }
                    reflectionProbe.captureExcludedRenderableIdentities.push_back(
                        identity.get<u64>()
                    );
                }
            }
            if (!DocumentReflectionProbeEditIsInRange(reflectionProbe)) {
                failure = "Scene document contains out-of-range reflection probe state.";
                return false;
            }
            return true;
        };
        if (version >= kSceneBuilderReflectionProbeCollectionDocumentVersion) {
            if (!document.contains("reflectionProbes") ||
                !document["reflectionProbes"].is_array() ||
                document["reflectionProbes"].size() >
                    kSceneBuilderMaxReflectionProbes) {
                failure = "Scene document contains invalid reflection probe collection.";
                return false;
            }
            parsed.reflectionProbesAreExplicit = true;
            parsed.reflectionProbes.reserve(document["reflectionProbes"].size());
            for (const nlohmann::json& source : document["reflectionProbes"]) {
                SceneBuilderReflectionProbeEdit reflectionProbe{};
                if (!parseReflectionProbe(source, true, true, reflectionProbe)) {
                    return false;
                }
                parsed.reflectionProbes.push_back(std::move(reflectionProbe));
            }
        } else if (version >= kSceneBuilderReflectionProbeDocumentVersion) {
            if (!document.contains("reflectionProbe")) {
                failure = "Scene document contains invalid reflection probe state.";
                return false;
            }
            SceneBuilderReflectionProbeEdit reflectionProbe{};
            if (!parseReflectionProbe(
                    document["reflectionProbe"],
                    version >= kSceneBuilderReflectionProbeSpatialDocumentVersion,
                    version >= kSceneBuilderReflectionProbeExclusionDocumentVersion,
                    reflectionProbe
                )) {
                return false;
            }
            parsed.reflectionProbes.push_back(std::move(reflectionProbe));
        }
        parsed.objects.reserve(documentObjects.size());
        for (const nlohmann::json& source : documentObjects) {
            if (!source.is_object() ||
                !source.contains("primitive") || !source["primitive"].is_string() ||
                !source.contains("name") || !source["name"].is_string() ||
                !source.contains("transform") || !source["transform"].is_object() ||
                !source.contains("material") || !source["material"].is_object() ||
                !source.contains("castShadow") || !source["castShadow"].is_boolean()) {
                failure = "Scene document contains an invalid object.";
                return false;
            }

            const std::optional<SceneBuilderPrimitive> primitive =
                PrimitiveFromDocumentName(source["primitive"].get<std::string>());
            if (!primitive.has_value()) {
                failure = "Scene document contains an unknown primitive.";
                return false;
            }

            const nlohmann::json& transform = source["transform"];
            const nlohmann::json& material = source["material"];
            if (!transform.contains("position") ||
                !transform.contains("rotationDegrees") ||
                !transform.contains("scale") ||
                !material.contains("baseColor") ||
                !material.contains("metallic") ||
                !material.contains("roughness") ||
                !material.contains("emissive") ||
                !material.contains("alphaMode") ||
                !material["alphaMode"].is_string() ||
                !material.contains("alphaCutoff") ||
                !material.contains("doubleSided") ||
                !material["doubleSided"].is_boolean()) {
                failure = "Scene document contains incomplete object state.";
                return false;
            }

            SceneBuilderDocumentObject object{};
            object.primitive = *primitive;
            object.name = source["name"].get<std::string>();
            if (version >= kSceneBuilderImportedAssetDocumentVersion) {
                if (!source.contains("assetPath") ||
                    !source["assetPath"].is_string()) {
                    failure = "Scene document contains invalid imported asset state.";
                    return false;
                }
                object.assetPath = source["assetPath"].get<std::string>();
            }
            if (IsImportedAsset(object.primitive) &&
                !AssetPathMatchesPrimitive(object.primitive, object.assetPath)) {
                failure = "Scene document contains an unsupported imported asset.";
                return false;
            }
            if (object.name.empty() ||
                !ReadJsonVec3(transform["position"], object.edit.position) ||
                !ReadJsonVec3(transform["rotationDegrees"], object.edit.rotationDegrees) ||
                !ReadJsonVec3(transform["scale"], object.edit.scale) ||
                !ReadJsonVec4(material["baseColor"], object.edit.baseColor) ||
                !ReadJsonNumber(material["metallic"], object.edit.metallic) ||
                !ReadJsonNumber(material["roughness"], object.edit.roughness) ||
                !ReadJsonVec3(material["emissive"], object.edit.emissive) ||
                !ReadJsonNumber(material["alphaCutoff"], object.edit.alphaCutoff)) {
                failure = "Scene document contains invalid numeric state.";
                return false;
            }

            const std::optional<MaterialAlphaMode> alphaMode =
                AlphaModeFromDocumentName(material["alphaMode"].get<std::string>());
            if (!alphaMode.has_value()) {
                failure = "Scene document contains an unknown alpha mode.";
                return false;
            }

            object.edit.alphaMode = *alphaMode;
            object.edit.doubleSided = material["doubleSided"].get<bool>();
            object.edit.castShadow = source["castShadow"].get<bool>();
            if (!DocumentEditIsInRange(object.edit)) {
                failure = "Scene document contains out-of-range object state.";
                return false;
            }
            parsed.objects.push_back(std::move(object));
        }

        if (version >= kSceneBuilderLightDocumentVersion) {
            if (!document.contains("lights") || !document["lights"].is_array()) {
                failure = "Scene document contains incomplete light state.";
                return false;
            }

            parsed.lightsAreExplicit = true;

            const nlohmann::json& documentLights = document["lights"];
            if (documentLights.size() > kMaxFrameLocalLights + 1u) {
                failure = "Scene document exceeds the light limit.";
                return false;
            }

            u32 directionalCount = 0u;
            u32 localLightCount = 0u;
            parsed.lights.reserve(documentLights.size());
            for (const nlohmann::json& source : documentLights) {
                if (!source.is_object() ||
                    !source.contains("type") || !source["type"].is_string() ||
                    !source.contains("name") || !source["name"].is_string() ||
                    !source.contains("enabled") || !source["enabled"].is_boolean()) {
                    failure = "Scene document contains an invalid light.";
                    return false;
                }

                const std::optional<SceneLightKind> kind =
                    LightKindFromDocumentName(source["type"].get<std::string>());
                if (!kind.has_value()) {
                    failure = "Scene document contains an unknown light type.";
                    return false;
                }

                SceneLightEdit edit{};
                edit.kind = *kind;
                edit.name = source["name"].get<std::string>();
                edit.enabled = source["enabled"].get<bool>();
                const auto readRequiredNumber = [&source](
                    std::string_view name,
                    f32& output
                ) {
                    const std::string key(name);
                    return source.contains(key) && ReadJsonNumber(source[key], output);
                };
                const auto readRequiredVec3 = [&source](
                    std::string_view name,
                    glm::vec3& output
                ) {
                    const std::string key(name);
                    return source.contains(key) && ReadJsonVec3(source[key], output);
                };

                bool valid = false;
                switch (edit.kind) {
                    case SceneLightKind::Directional:
                        valid = readRequiredVec3("direction", edit.direction) &&
                            readRequiredNumber("intensity", edit.intensity) &&
                            readRequiredNumber("ambient", edit.ambient) &&
                            readRequiredNumber("specular", edit.specular) &&
                            readRequiredNumber(
                                "angularRadiusRadians",
                                edit.angularRadiusRadians
                            );
                        if (version >= kSceneBuilderDirectionalGizmoDocumentVersion) {
                            valid = valid && readRequiredVec3(
                                "gizmoPosition",
                                edit.position
                            );
                        }
                        ++directionalCount;
                        break;
                    case SceneLightKind::Point:
                        valid = readRequiredVec3("position", edit.position) &&
                            readRequiredVec3("color", edit.color) &&
                            readRequiredNumber("intensity", edit.intensity) &&
                            readRequiredNumber("radius", edit.radius) &&
                            readRequiredNumber("sourceRadius", edit.sourceRadius);
                        ++localLightCount;
                        break;
                    case SceneLightKind::Spot:
                        valid = readRequiredVec3("position", edit.position) &&
                            readRequiredVec3("direction", edit.direction) &&
                            readRequiredVec3("color", edit.color) &&
                            readRequiredNumber("intensity", edit.intensity) &&
                            readRequiredNumber("radius", edit.radius) &&
                            readRequiredNumber("sourceRadius", edit.sourceRadius) &&
                            readRequiredNumber(
                                "innerConeDegrees",
                                edit.innerConeDegrees
                            ) &&
                            readRequiredNumber(
                                "outerConeDegrees",
                                edit.outerConeDegrees
                            );
                        ++localLightCount;
                        break;
                    case SceneLightKind::Rect:
                        valid = readRequiredVec3("position", edit.position) &&
                            readRequiredVec3("direction", edit.direction) &&
                            readRequiredVec3("color", edit.color) &&
                            readRequiredNumber("intensity", edit.intensity) &&
                            readRequiredNumber("radius", edit.radius) &&
                            readRequiredNumber("width", edit.width) &&
                            readRequiredNumber("height", edit.height) &&
                            readRequiredNumber("specular", edit.specular);
                        ++localLightCount;
                        break;
                }

                if (!valid || directionalCount > 1u ||
                    localLightCount > kMaxFrameLocalLights ||
                    !DocumentLightEditIsInRange(edit)) {
                    failure = "Scene document contains invalid light state.";
                    return false;
                }
                edit.direction = glm::normalize(edit.direction);
                parsed.lights.push_back(SceneBuilderDocumentLight{ std::move(edit) });
            }
        }

        if (version >= kSceneBuilderCameraDocumentVersion) {
            if (!document.contains("camera") || !document["camera"].is_object()) {
                failure = "Scene document contains incomplete camera state.";
                return false;
            }

            const nlohmann::json& camera = document["camera"];
            if (!camera.contains("position") ||
                !camera.contains("forward") ||
                !camera.contains("distance") ||
                !camera.contains("fovScale") ||
                !camera.contains("freeLookActive") ||
                !camera["freeLookActive"].is_boolean()) {
                failure = "Scene document contains incomplete camera state.";
                return false;
            }

            Camera3DState cameraState{};
            if (!ReadJsonVec3(camera["position"], cameraState.position) ||
                !ReadJsonVec3(camera["forward"], cameraState.forward) ||
                !ReadJsonNumber(camera["distance"], cameraState.distance) ||
                !ReadJsonNumber(camera["fovScale"], cameraState.fovScale)) {
                failure = "Scene document contains invalid camera state.";
                return false;
            }
            cameraState.freeLookActive = camera["freeLookActive"].get<bool>();
            if (!DocumentCameraStateIsValid(cameraState)) {
                failure = "Scene document contains out-of-range camera state.";
                return false;
            }
            parsed.cameraState = cameraState;
        }

        documentOutput = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        failure = "Scene document is not valid JSON.";
        return false;
    }
}

bool SameDocumentEdit(
    const SceneBuilderObjectEdit& left,
    const SceneBuilderObjectEdit& right
) {
    return NearlyEqual(left.position, right.position) &&
        NearlyEqual(left.rotationDegrees, right.rotationDegrees) &&
        NearlyEqual(left.scale, right.scale) &&
        left.castShadow == right.castShadow &&
        NearlyEqual(left.baseColor, right.baseColor) &&
        NearlyEqual(left.metallic, right.metallic) &&
        NearlyEqual(left.roughness, right.roughness) &&
        NearlyEqual(left.emissive, right.emissive) &&
        left.alphaMode == right.alphaMode &&
        NearlyEqual(left.alphaCutoff, right.alphaCutoff) &&
        left.doubleSided == right.doubleSided;
}

bool SameSceneEnvironment(
    const SceneEnvironment3D& left,
    const SceneEnvironment3D& right
) {
    return left.iblEnabled == right.iblEnabled &&
        NearlyEqual(left.diffuseIntensity, right.diffuseIntensity) &&
        NearlyEqual(left.specularIntensity, right.specularIntensity) &&
        NearlyEqual(left.horizonBlend, right.horizonBlend) &&
        left.skyboxEnabled == right.skyboxEnabled &&
        NearlyEqual(left.skyboxIntensity, right.skyboxIntensity) &&
        NearlyEqual(left.skyboxBlur, right.skyboxBlur) &&
        left.lightingAsset == right.lightingAsset;
}

bool SameReflectionProbeEdit(
    const SceneBuilderReflectionProbeEdit& left,
    const SceneBuilderReflectionProbeEdit& right
) {
    return left.name == right.name &&
        NearlyEqual(left.center, right.center) &&
        NearlyEqual(left.radius, right.radius) &&
        NearlyEqual(left.boxCenter, right.boxCenter) &&
        NearlyEqual(left.boxExtents, right.boxExtents) &&
        NearlyEqual(left.color, right.color) &&
        NearlyEqual(left.intensity, right.intensity) &&
        NearlyEqual(left.blendStrength, right.blendStrength) &&
        NearlyEqual(left.falloff, right.falloff) &&
        left.enabled == right.enabled &&
        left.captureSource == right.captureSource &&
        left.captureAssetId == right.captureAssetId &&
        left.refreshPolicy == right.refreshPolicy &&
        left.captureExcludedRenderableIdentities ==
            right.captureExcludedRenderableIdentities;
}

bool SameSceneLightEdit(
    const SceneLightEdit& left,
    const SceneLightEdit& right
) {
    return left.kind == right.kind &&
        left.name == right.name &&
        left.enabled == right.enabled &&
        NearlyEqual(left.position, right.position) &&
        NearlyEqual(left.direction, right.direction) &&
        NearlyEqual(left.color, right.color) &&
        NearlyEqual(left.intensity, right.intensity) &&
        NearlyEqual(left.radius, right.radius) &&
        NearlyEqual(left.sourceRadius, right.sourceRadius) &&
        NearlyEqual(left.innerConeDegrees, right.innerConeDegrees) &&
        NearlyEqual(left.outerConeDegrees, right.outerConeDegrees) &&
        NearlyEqual(left.width, right.width) &&
        NearlyEqual(left.height, right.height) &&
        NearlyEqual(left.ambient, right.ambient) &&
        NearlyEqual(left.specular, right.specular) &&
        NearlyEqual(left.angularRadiusRadians, right.angularRadiusRadians);
}

bool IntersectLocalAabb(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax,
    f32& hitDistance
) {
    f32 nearHit = -std::numeric_limits<f32>::max();
    f32 farHit = std::numeric_limits<f32>::max();

    for (i32 axis = 0; axis < 3; ++axis) {
        const f32 origin = rayOrigin[axis];
        const f32 direction = rayDirection[axis];
        if (std::abs(direction) <= kPickEpsilon) {
            if (origin < boundsMin[axis] || origin > boundsMax[axis]) {
                return false;
            }
            continue;
        }

        f32 axisNear = (boundsMin[axis] - origin) / direction;
        f32 axisFar = (boundsMax[axis] - origin) / direction;
        if (axisNear > axisFar) {
            std::swap(axisNear, axisFar);
        }
        nearHit = std::max(nearHit, axisNear);
        farHit = std::min(farHit, axisFar);
        if (nearHit > farHit) {
            return false;
        }
    }

    hitDistance = nearHit >= kPickEpsilon ? nearHit : farHit;
    return hitDistance >= kPickEpsilon;
}

bool IntersectLocalCube(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    f32& hitDistance
) {
    return IntersectLocalAabb(
        rayOrigin,
        rayDirection,
        glm::vec3(-0.5f),
        glm::vec3(0.5f),
        hitDistance
    );
}

bool IntersectLocalSphere(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    f32& hitDistance
) {
    constexpr f32 kRadius = 0.5f;
    const f32 a = glm::dot(rayDirection, rayDirection);
    const f32 b = 2.0f * glm::dot(rayOrigin, rayDirection);
    const f32 c = glm::dot(rayOrigin, rayOrigin) - kRadius * kRadius;
    const f32 discriminant = b * b - 4.0f * a * c;
    if (a <= kPickEpsilon || discriminant < 0.0f) {
        return false;
    }

    const f32 root = std::sqrt(discriminant);
    const f32 nearHit = (-b - root) / (2.0f * a);
    const f32 farHit = (-b + root) / (2.0f * a);
    hitDistance = nearHit >= kPickEpsilon ? nearHit : farHit;
    return hitDistance >= kPickEpsilon;
}

bool IntersectLocalPlane(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    f32& hitDistance
) {
    if (std::abs(rayDirection.y) <= kPickEpsilon) {
        return false;
    }

    const f32 candidate = -rayOrigin.y / rayDirection.y;
    if (candidate < kPickEpsilon) {
        return false;
    }

    const glm::vec3 hit = rayOrigin + rayDirection * candidate;
    if (std::abs(hit.x) > 0.5f || std::abs(hit.z) > 0.5f) {
        return false;
    }

    hitDistance = candidate;
    return true;
}

bool IntersectLocalCone(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    f32& hitDistance
) {
    // MeshFactory's cone has an apex at y=0 and a capped radius-0.5 base at y=1.
    f32 nearestHit = std::numeric_limits<f32>::max();
    const auto considerSideHit = [&](f32 candidate) {
        if (candidate < kPickEpsilon) {
            return;
        }
        const f32 y = rayOrigin.y + rayDirection.y * candidate;
        if (y >= 0.0f && y <= 1.0f) {
            nearestHit = std::min(nearestHit, candidate);
        }
    };

    const f32 a = rayDirection.x * rayDirection.x +
        rayDirection.z * rayDirection.z -
        0.25f * rayDirection.y * rayDirection.y;
    const f32 b = 2.0f * (rayOrigin.x * rayDirection.x +
        rayOrigin.z * rayDirection.z) -
        0.5f * rayOrigin.y * rayDirection.y;
    const f32 c = rayOrigin.x * rayOrigin.x + rayOrigin.z * rayOrigin.z -
        0.25f * rayOrigin.y * rayOrigin.y;
    if (std::abs(a) > kPickEpsilon) {
        const f32 discriminant = b * b - 4.0f * a * c;
        if (discriminant >= 0.0f) {
            const f32 root = std::sqrt(discriminant);
            considerSideHit((-b - root) / (2.0f * a));
            considerSideHit((-b + root) / (2.0f * a));
        }
    } else if (std::abs(b) > kPickEpsilon) {
        considerSideHit(-c / b);
    }

    if (std::abs(rayDirection.y) > kPickEpsilon) {
        const f32 capHit = (1.0f - rayOrigin.y) / rayDirection.y;
        if (capHit >= kPickEpsilon) {
            const glm::vec3 hit = rayOrigin + rayDirection * capHit;
            if (hit.x * hit.x + hit.z * hit.z <= 0.25f) {
                nearestHit = std::min(nearestHit, capHit);
            }
        }
    }

    if (nearestHit == std::numeric_limits<f32>::max()) {
        return false;
    }

    hitDistance = nearestHit;
    return true;
}

bool IntersectPrimitiveSurface(
    SceneBuilderPrimitive primitive,
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    f32& hitDistance
) {
    switch (primitive) {
        case SceneBuilderPrimitive::Cube:
            return IntersectLocalCube(rayOrigin, rayDirection, hitDistance);
        case SceneBuilderPrimitive::Plane:
            return IntersectLocalPlane(rayOrigin, rayDirection, hitDistance);
        case SceneBuilderPrimitive::Sphere:
            return IntersectLocalSphere(rayOrigin, rayDirection, hitDistance);
        case SceneBuilderPrimitive::Cone:
            return IntersectLocalCone(rayOrigin, rayDirection, hitDistance);
        case SceneBuilderPrimitive::Lvjuren:
        case SceneBuilderPrimitive::Building1:
        case SceneBuilderPrimitive::Building2:
        case SceneBuilderPrimitive::Building3:
        case SceneBuilderPrimitive::Building4:
        case SceneBuilderPrimitive::Car1:
        case SceneBuilderPrimitive::Car2:
            return false;
    }

    return false;
}

}

SceneBuilder::SceneBuilder(
    VulkanMaterialLibrary& materialLibrary,
    VulkanRenderResources2D& renderResources,
    Scene3D& scene
) : m_MaterialLibrary(materialLibrary),
    m_RenderResources(renderResources),
    m_Scene(scene) {
    for (u32 index = 0; index < kSceneBuilderPrimitiveCount; ++index) {
        if (!IsImportedAsset(static_cast<SceneBuilderPrimitive>(index)) &&
            m_RenderResources.ContainsMesh(kSceneBuilderPrimitives[index].meshId)) {
            m_PrimitiveAvailabilityMask |= (1u << index);
        }
    }
    // The Scene Builder lane creates its usable empty-state key light before
    // the builder itself. Adopt it so it follows the same edit/delete/save
    // contract as every light subsequently created by the editor.
    AdoptPrimaryDirectionalLight();
    EnsureSceneBuilderReflectionCaptureProbe(m_Scene);
}

void SceneBuilder::SetMaterialsChangedCallback(MaterialsChangedCallback callback) {
    m_MaterialsChanged = std::move(callback);
}

void SceneBuilder::SetImportedAssetCreator(ImportedAssetCreator callback) {
    m_ImportedAssetCreator = std::move(callback);
    for (u32 index = 0u; index < kSceneBuilderPrimitiveCount; ++index) {
        const SceneBuilderPrimitive primitive =
            static_cast<SceneBuilderPrimitive>(index);
        if (!IsImportedAsset(primitive)) {
            continue;
        }
        if (m_ImportedAssetCreator) {
            m_PrimitiveAvailabilityMask |= (1u << index);
        } else {
            m_PrimitiveAvailabilityMask &= ~(1u << index);
        }
    }
}

bool SceneBuilder::Available() const {
    return m_PrimitiveAvailabilityMask != 0;
}

bool SceneBuilder::PrimitiveAvailable(SceneBuilderPrimitive primitive) const {
    const u32 index = static_cast<u32>(primitive);
    if (index >= kSceneBuilderPrimitiveCount) {
        return false;
    }

    return (m_PrimitiveAvailabilityMask & (1u << index)) != 0u;
}

u64 SceneBuilder::CreatePrimitive(SceneBuilderPrimitive primitive) {
    if (IsImportedAsset(primitive)) {
        return CreateImportedAsset(primitive);
    }

    const PrimitiveDescriptor& descriptor = DescriptorFor(primitive);
    if (!PrimitiveAvailable(primitive)) {
        m_LastCreateFailure = SceneBuilderCreateFailure::MeshNotRegistered;
        ++m_CreateFailureCount;
        return 0;
    }
    if (m_Objects.size() >= kSceneBuilderMaxObjects) {
        m_LastCreateFailure = SceneBuilderCreateFailure::ObjectLimitReached;
        ++m_CreateFailureCount;
        return 0;
    }

    const u32 ordinal = m_NextObjectOrdinal;
    const std::string ordinalSuffix = std::to_string(ordinal);
    const std::string objectName =
        std::string(descriptor.name) + " " + ordinalSuffix;
    const std::string materialId =
        "SceneBuilderMaterial_" + ordinalSuffix;
    if (m_MaterialLibrary.Contains(materialId) ||
        m_RenderResources.ContainsMaterial(materialId)) {
        m_LastCreateFailure = SceneBuilderCreateFailure::MaterialIdCollision;
        ++m_CreateFailureCount;
        return 0;
    }

    VulkanMaterial& material = m_MaterialLibrary.Create(
        materialId,
        VulkanTexturePixels{
            std::span<const u8>(
                kSceneBuilderWhiteTexel.data(),
                kSceneBuilderWhiteTexel.size()
            ),
            1,
            1
        },
        DefaultEditorMaterialProperties(),
        false,
        false,
        nullptr
    );
    m_RenderResources.RegisterMaterial(materialId, material);

    Renderable3D& renderable = m_Scene.CreateRenderable(
        objectName,
        std::string(descriptor.meshId),
        materialId
    );
    renderable.Transform().SetPosition({ 0.0f, 0.0f, 0.0f });
    renderable.Transform().SetRotationDegrees({ 0.0f, 0.0f, 0.0f });
    renderable.Transform().SetScale(descriptor.defaultScale);
    // Editor objects are authored, not animated demo props.
    renderable.Transform().SetAnimateRotation(false);
    renderable.Transform().SetRotationSpeedDegreesPerSecond({ 0.0f, 0.0f, 0.0f });
    renderable.SetCastShadow(true);

    SceneBuilderObject object{};
    object.renderIdentity = renderable.RenderIdentity();
    object.primitive = primitive;
    object.name = objectName;
    object.meshId = std::string(descriptor.meshId);
    object.materialId = materialId;
    object.renderable = &renderable;
    object.material = &material;
    m_Objects.push_back(std::move(object));

    ++m_NextObjectOrdinal;
    ++m_CreatedObjectCount;
    m_LastCreateFailure = SceneBuilderCreateFailure::None;
    m_Scene.SelectRenderableByIdentity(renderable.RenderIdentity());
    SetSelectedIdentity(renderable.RenderIdentity());
    ++m_EditRevision;
    RefreshMaterials();

    return m_SelectedIdentity;
}

u64 SceneBuilder::CreateImportedAsset(SceneBuilderPrimitive primitive) {
    if (!IsImportedAsset(primitive) || !m_ImportedAssetCreator) {
        m_LastCreateFailure = SceneBuilderCreateFailure::ImportedAssetUnavailable;
        ++m_CreateFailureCount;
        return 0u;
    }
    if (m_Objects.size() >= kSceneBuilderMaxObjects) {
        m_LastCreateFailure = SceneBuilderCreateFailure::ObjectLimitReached;
        ++m_CreateFailureCount;
        return 0u;
    }

    SceneBuilderObjectEdit initialEdit{};
    const PrimitiveDescriptor& descriptor = DescriptorFor(primitive);
    initialEdit.scale = descriptor.defaultScale;
    const SceneBuilderImportedAsset imported = m_ImportedAssetCreator(
        primitive,
        initialEdit
    );
    if (!imported.loaded || imported.renderIdentities.empty()) {
        m_LastCreateFailure = SceneBuilderCreateFailure::ImportedAssetLoadFailed;
        ++m_CreateFailureCount;
        return 0u;
    }

    Renderable3D* rootRenderable = nullptr;
    for (const u64 identity : imported.renderIdentities) {
        Renderable3D* renderable = m_Scene.FindRenderableByIdentity(identity);
        if (identity == 0u || renderable == nullptr ||
            !m_RenderResources.ContainsMesh(renderable->MeshId()) ||
            !m_RenderResources.ContainsMaterial(renderable->MaterialId())) {
            m_LastCreateFailure = SceneBuilderCreateFailure::ImportedAssetLoadFailed;
            ++m_CreateFailureCount;
            return 0u;
        }
        if (rootRenderable == nullptr) {
            rootRenderable = renderable;
        }
    }
    if (rootRenderable == nullptr) {
        m_LastCreateFailure = SceneBuilderCreateFailure::ImportedAssetLoadFailed;
        ++m_CreateFailureCount;
        return 0u;
    }

    const u32 ordinal = m_NextObjectOrdinal;
    const std::string objectName = std::string(descriptor.name) + " " +
        std::to_string(ordinal);
    for (u32 partIndex = 0u;
         partIndex < static_cast<u32>(imported.renderIdentities.size());
         ++partIndex) {
        Renderable3D* renderable = m_Scene.FindRenderableByIdentity(
            imported.renderIdentities[partIndex]
        );
        if (renderable != nullptr) {
            renderable->SetName(partIndex == 0u
                ? objectName
                : objectName + " / Part " + std::to_string(partIndex + 1u));
        }
    }

    SceneBuilderObject object{};
    object.renderIdentity = rootRenderable->RenderIdentity();
    object.primitive = primitive;
    object.name = objectName;
    object.meshId = std::string(rootRenderable->MeshId());
    object.materialId = std::string(rootRenderable->MaterialId());
    object.assetPath = std::string(AssetPathFor(primitive));
    object.memberRenderIdentities = imported.renderIdentities;
    object.renderable = rootRenderable;
    object.material = &m_RenderResources.Material(rootRenderable->MaterialId());
    m_Objects.push_back(std::move(object));

    ++m_NextObjectOrdinal;
    ++m_CreatedObjectCount;
    m_LastCreateFailure = SceneBuilderCreateFailure::None;
    m_Scene.SelectRenderableByIdentity(rootRenderable->RenderIdentity());
    SetSelectedIdentity(rootRenderable->RenderIdentity());
    ++m_EditRevision;
    if (imported.materialResourcesChanged) {
        RefreshMaterials();
    }

    return m_SelectedIdentity;
}

u32 SceneBuilder::CreateFromSpec(std::string_view spec) {
    u32 createdCount = 0;
    std::size_t cursor = 0;
    while (cursor <= spec.size()) {
        const std::size_t separator = spec.find(',', cursor);
        const std::size_t end = separator == std::string_view::npos
            ? spec.size()
            : separator;
        std::string_view entry = spec.substr(cursor, end - cursor);

        if (!TrimSpecToken(entry).empty()) {
            CreateFromSpecEntry(entry, createdCount);
        }

        if (separator == std::string_view::npos) {
            break;
        }
        cursor = separator + 1;
    }

    return createdCount;
}

std::filesystem::path SceneBuilder::DefaultDocumentPath() {
    return std::filesystem::path(".selfengine") /
        "scene_builder" /
        "scene.json";
}

const std::string& SceneBuilder::LastDocumentStatus() const {
    return m_LastDocumentStatus;
}

bool SceneBuilder::LastDocumentOperationFailed() const {
    return m_LastDocumentOperationFailed;
}

bool SceneBuilder::SaveDefaultDocument(const Camera3DState& cameraState) {
    return SaveToFile(DefaultDocumentPath(), cameraState);
}

bool SceneBuilder::LoadDefaultDocument(
    std::optional<Camera3DState>& cameraState
) {
    return LoadFromFile(DefaultDocumentPath(), cameraState);
}

bool SceneBuilder::HasDefaultCityLayout() const {
    return m_StartupLayoutId == kSceneBuilderDefaultCityLayoutId;
}

SceneBuilderCityLayoutResult SceneBuilder::BootstrapDefaultCityLayout() {
    SceneBuilderCityLayoutResult result{};
    const auto countPrimitive = [this](SceneBuilderPrimitive primitive) {
        return static_cast<u32>(std::count_if(
            m_Objects.begin(),
            m_Objects.end(),
            [primitive](const SceneBuilderObject& object) {
                return object.primitive == primitive;
            }
        ));
    };
    const auto updateResultCounts = [&]() {
        result.buildingCount =
            countPrimitive(SceneBuilderPrimitive::Building1) +
            countPrimitive(SceneBuilderPrimitive::Building2) +
            countPrimitive(SceneBuilderPrimitive::Building3) +
            countPrimitive(SceneBuilderPrimitive::Building4);
        result.carCount =
            countPrimitive(SceneBuilderPrimitive::Car1) +
            countPrimitive(SceneBuilderPrimitive::Car2);
    };

    updateResultCounts();
    if (HasDefaultCityLayout()) {
        return result;
    }

    constexpr std::array<SceneBuilderPrimitive, 4> buildingPrimitives{
        SceneBuilderPrimitive::Building1,
        SceneBuilderPrimitive::Building2,
        SceneBuilderPrimitive::Building3,
        SceneBuilderPrimitive::Building4
    };
    constexpr std::array<SceneBuilderPrimitive, 2> carPrimitives{
        SceneBuilderPrimitive::Car1,
        SceneBuilderPrimitive::Car2
    };
    constexpr u32 kBuildingsPerAsset = 6u;
    constexpr u32 kCarsPerAsset = 5u;

    const auto ensureCount = [this, &result](
        SceneBuilderPrimitive primitive,
        u32 targetCount
    ) {
        u32 count = static_cast<u32>(std::count_if(
            m_Objects.begin(),
            m_Objects.end(),
            [primitive](const SceneBuilderObject& object) {
                return object.primitive == primitive;
            }
        ));
        while (count < targetCount) {
            if (CreatePrimitive(primitive) == 0u) {
                return false;
            }
            ++count;
            ++result.createdObjectCount;
        }
        return true;
    };

    for (const SceneBuilderPrimitive primitive : buildingPrimitives) {
        if (!ensureCount(primitive, kBuildingsPerAsset)) {
            result.failed = true;
            updateResultCounts();
            return result;
        }
    }
    for (const SceneBuilderPrimitive primitive : carPrimitives) {
        if (!ensureCount(primitive, kCarsPerAsset)) {
            result.failed = true;
            updateResultCounts();
            return result;
        }
    }

    u64 floorIdentity = 0u;
    for (const SceneBuilderObject& object : m_Objects) {
        if (object.primitive == SceneBuilderPrimitive::Cube &&
            LowerSpecToken(object.name) == "floor") {
            floorIdentity = object.renderIdentity;
            break;
        }
    }
    if (floorIdentity == 0u) {
        floorIdentity = CreatePrimitive(SceneBuilderPrimitive::Cube);
        if (floorIdentity == 0u) {
            result.failed = true;
            updateResultCounts();
            return result;
        }
        ++result.createdObjectCount;
        RenameObject(floorIdentity, "floor");
    }

    SceneBuilderObjectEdit floorEdit{};
    if (!ReadObjectEdit(floorIdentity, floorEdit)) {
        result.failed = true;
        updateResultCounts();
        return result;
    }
    floorEdit.position = { 0.0f, 0.0661892f, 0.0f };
    floorEdit.rotationDegrees = { 0.0f, 0.0f, 0.0f };
    floorEdit.scale = { 22.0f, 0.221f, 28.0f };
    floorEdit.castShadow = false;
    floorEdit.baseColor = { 0.075f, 0.082f, 0.095f, 1.0f };
    floorEdit.metallic = 0.05f;
    floorEdit.roughness = 0.88f;
    ApplyObjectEdit(floorIdentity, floorEdit);

    std::array<std::vector<u64>, buildingPrimitives.size()> buildingIdentities;
    std::array<std::vector<u64>, carPrimitives.size()> carIdentities;
    for (const SceneBuilderObject& object : m_Objects) {
        for (u32 index = 0u; index < buildingPrimitives.size(); ++index) {
            if (object.primitive == buildingPrimitives[index] &&
                buildingIdentities[index].size() < kBuildingsPerAsset) {
                buildingIdentities[index].push_back(object.renderIdentity);
            }
        }
        for (u32 index = 0u; index < carPrimitives.size(); ++index) {
            if (object.primitive == carPrimitives[index] &&
                carIdentities[index].size() < kCarsPerAsset) {
                carIdentities[index].push_back(object.renderIdentity);
            }
        }
    }

    constexpr std::array<f32, 4> buildingX{ -8.0f, -4.8f, 4.8f, 8.0f };
    constexpr std::array<f32, 6> buildingZ{
        -10.5f, -6.3f, -2.1f, 2.1f, 6.3f, 10.5f
    };
    constexpr std::array<f32, 4> buildingBaseY{
        1.2057081f, 1.1996406f, 5.7111530f, 6.3829565f
    };
    std::array<u32, buildingPrimitives.size()> nextBuilding{};
    for (u32 row = 0u; row < buildingZ.size(); ++row) {
        for (u32 column = 0u; column < buildingX.size(); ++column) {
            const u32 primitiveIndex = (row + column) % buildingPrimitives.size();
            const u32 instanceIndex = nextBuilding[primitiveIndex]++;
            const u64 identity = buildingIdentities[primitiveIndex][instanceIndex];
            SceneBuilderObjectEdit edit{};
            if (!ReadObjectEdit(identity, edit)) {
                result.failed = true;
                updateResultCounts();
                return result;
            }
            edit.position = {
                buildingX[column],
                buildingBaseY[primitiveIndex],
                buildingZ[row]
            };
            edit.rotationDegrees = {
                0.0f,
                buildingX[column] < 0.0f ? 90.0f : -90.0f,
                0.0f
            };
            edit.scale = DescriptorFor(buildingPrimitives[primitiveIndex]).defaultScale;
            ApplyObjectEdit(identity, edit);
            RenameObject(
                identity,
                "City " + std::string(PrimitiveName(buildingPrimitives[primitiveIndex])) +
                    " " + std::to_string(instanceIndex + 1u)
            );
        }
    }

    constexpr std::array<f32, 5> carZ{ -8.4f, -4.2f, 0.0f, 4.2f, 8.4f };
    constexpr std::array<f32, 2> carX{ -1.25f, 1.25f };
    constexpr std::array<f32, 2> carBaseY{ 0.3244606f, 0.2870443f };
    for (u32 primitiveIndex = 0u; primitiveIndex < carPrimitives.size(); ++primitiveIndex) {
        for (u32 instanceIndex = 0u; instanceIndex < kCarsPerAsset; ++instanceIndex) {
            const u64 identity = carIdentities[primitiveIndex][instanceIndex];
            SceneBuilderObjectEdit edit{};
            if (!ReadObjectEdit(identity, edit)) {
                result.failed = true;
                updateResultCounts();
                return result;
            }
            edit.position = {
                carX[primitiveIndex],
                carBaseY[primitiveIndex],
                carZ[instanceIndex]
            };
            edit.rotationDegrees = {
                0.0f,
                primitiveIndex == 0u ? 0.0f : 180.0f,
                0.0f
            };
            edit.scale = DescriptorFor(carPrimitives[primitiveIndex]).defaultScale;
            ApplyObjectEdit(identity, edit);
            RenameObject(
                identity,
                "City " + std::string(PrimitiveName(carPrimitives[primitiveIndex])) +
                    " " + std::to_string(instanceIndex + 1u)
            );
        }
    }

    m_StartupLayoutId = std::string(kSceneBuilderDefaultCityLayoutId);
    ++m_EditRevision;
    result.applied = true;
    updateResultCounts();
    return result;
}

bool SceneBuilder::SaveToFile(
    const std::filesystem::path& path,
    const Camera3DState& cameraState
) {
    if (!DocumentCameraStateIsValid(cameraState)) {
        m_LastDocumentStatus = "Save failed: camera state is invalid.";
        m_LastDocumentOperationFailed = true;
        return false;
    }

    const SceneEnvironment3D environment = Environment();
    nlohmann::json document{
        { "format", kSceneBuilderDocumentFormat },
        { "version", kSceneBuilderDocumentVersion },
        { "startupLayout", m_StartupLayoutId },
        { "camera", {
            { "position", JsonVec3(cameraState.position) },
            { "forward", JsonVec3(cameraState.forward) },
            { "distance", cameraState.distance },
            { "fovScale", cameraState.fovScale },
            { "freeLookActive", cameraState.freeLookActive }
        } },
        { "environment", {
            { "iblEnabled", environment.iblEnabled },
            { "diffuseIntensity", environment.diffuseIntensity },
            { "specularIntensity", environment.specularIntensity },
            { "horizonBlend", environment.horizonBlend },
            { "skyboxEnabled", environment.skyboxEnabled },
            { "skyboxIntensity", environment.skyboxIntensity },
            { "skyboxBlur", environment.skyboxBlur },
            { "lightingAsset", static_cast<u32>(environment.lightingAsset) }
        } },
        { "reflectionProbes", nlohmann::json::array() },
        { "objects", nlohmann::json::array() },
        { "lights", nlohmann::json::array() }
    };

    for (u32 index = 0u; index < ReflectionProbeCount(); ++index) {
        SceneBuilderReflectionProbeEdit reflectionProbe{};
        if (!ReadReflectionProbeEdit(index, reflectionProbe)) {
            m_LastDocumentStatus = "Save failed: reflection probe state is unavailable.";
            m_LastDocumentOperationFailed = true;
            return false;
        }

        document["reflectionProbes"].push_back({
            { "name", reflectionProbe.name },
            { "enabled", reflectionProbe.enabled },
            { "capturePosition", JsonVec3(reflectionProbe.center) },
            { "radius", reflectionProbe.radius },
            { "boxCenter", JsonVec3(reflectionProbe.boxCenter) },
            { "boxExtents", JsonVec3(reflectionProbe.boxExtents) },
            { "color", JsonVec3(reflectionProbe.color) },
            { "intensity", reflectionProbe.intensity },
            { "blendStrength", reflectionProbe.blendStrength },
            { "falloff", reflectionProbe.falloff },
            { "captureSource", ReflectionProbeCaptureSourceDocumentName(
                reflectionProbe.captureSource
            ) },
            { "captureAssetId", reflectionProbe.captureAssetId },
            { "refreshPolicy", ReflectionProbeRefreshPolicyDocumentName(
                reflectionProbe.refreshPolicy
            ) },
            { "captureExcludedRenderableIdentities",
                reflectionProbe.captureExcludedRenderableIdentities }
        });
    }

    for (const SceneBuilderObject& object : m_Objects) {
        SceneBuilderObjectEdit edit{};
        if (!ReadObjectEdit(object.renderIdentity, edit)) {
            m_LastDocumentStatus = "Save failed: object state is unavailable.";
            m_LastDocumentOperationFailed = true;
            return false;
        }

        document["objects"].push_back({
            { "primitive", PrimitiveName(object.primitive) },
            { "name", object.name },
            { "assetPath", object.assetPath },
            { "transform", {
                { "position", JsonVec3(edit.position) },
                { "rotationDegrees", JsonVec3(edit.rotationDegrees) },
                { "scale", JsonVec3(edit.scale) }
            } },
            { "material", {
                { "baseColor", JsonVec4(edit.baseColor) },
                { "metallic", edit.metallic },
                { "roughness", edit.roughness },
                { "emissive", JsonVec3(edit.emissive) },
                { "alphaMode", AlphaModeDocumentName(edit.alphaMode) },
                { "alphaCutoff", edit.alphaCutoff },
                { "doubleSided", edit.doubleSided }
            } },
            { "castShadow", edit.castShadow }
        });
    }

    for (const SceneBuilderLight& light : m_Lights) {
        SceneLightEdit edit{};
        if (!ReadLightEdit(light.lightIdentity, edit)) {
            m_LastDocumentStatus = "Save failed: light state is unavailable.";
            m_LastDocumentOperationFailed = true;
            return false;
        }

        nlohmann::json serializedLight{
            { "type", LightKindDocumentName(edit.kind) },
            { "name", edit.name },
            { "enabled", edit.enabled }
        };
        switch (edit.kind) {
            case SceneLightKind::Directional:
                serializedLight["direction"] = JsonVec3(edit.direction);
                serializedLight["gizmoPosition"] = JsonVec3(edit.position);
                serializedLight["intensity"] = edit.intensity;
                serializedLight["ambient"] = edit.ambient;
                serializedLight["specular"] = edit.specular;
                serializedLight["angularRadiusRadians"] =
                    edit.angularRadiusRadians;
                break;
            case SceneLightKind::Point:
                serializedLight["position"] = JsonVec3(edit.position);
                serializedLight["color"] = JsonVec3(edit.color);
                serializedLight["intensity"] = edit.intensity;
                serializedLight["radius"] = edit.radius;
                serializedLight["sourceRadius"] = edit.sourceRadius;
                break;
            case SceneLightKind::Spot:
                serializedLight["position"] = JsonVec3(edit.position);
                serializedLight["direction"] = JsonVec3(edit.direction);
                serializedLight["color"] = JsonVec3(edit.color);
                serializedLight["intensity"] = edit.intensity;
                serializedLight["radius"] = edit.radius;
                serializedLight["sourceRadius"] = edit.sourceRadius;
                serializedLight["innerConeDegrees"] = edit.innerConeDegrees;
                serializedLight["outerConeDegrees"] = edit.outerConeDegrees;
                break;
            case SceneLightKind::Rect:
                serializedLight["position"] = JsonVec3(edit.position);
                serializedLight["direction"] = JsonVec3(edit.direction);
                serializedLight["color"] = JsonVec3(edit.color);
                serializedLight["intensity"] = edit.intensity;
                serializedLight["radius"] = edit.radius;
                serializedLight["width"] = edit.width;
                serializedLight["height"] = edit.height;
                serializedLight["specular"] = edit.specular;
                break;
        }
        document["lights"].push_back(std::move(serializedLight));
    }

    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            m_LastDocumentStatus = "Save failed: could not create scene directory.";
            m_LastDocumentOperationFailed = true;
            return false;
        }
    }

    std::filesystem::path temporaryPath = path;
    temporaryPath += ".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::out | std::ios::trunc);
        if (!output) {
            m_LastDocumentStatus = "Save failed: could not write scene document.";
            m_LastDocumentOperationFailed = true;
            return false;
        }
        output << document.dump(2) << '\n';
        if (!output) {
            std::filesystem::remove(temporaryPath, error);
            m_LastDocumentStatus = "Save failed: scene document write was interrupted.";
            m_LastDocumentOperationFailed = true;
            return false;
        }
    }

    std::filesystem::rename(temporaryPath, path, error);
    if (error) {
        error.clear();
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporaryPath, path, error);
    }
    if (error) {
        std::filesystem::remove(temporaryPath, error);
        m_LastDocumentStatus = "Save failed: could not replace scene document.";
        m_LastDocumentOperationFailed = true;
        return false;
    }

    m_LastDocumentStatus = "Scene saved: " + path.generic_string();
    m_LastDocumentOperationFailed = false;
    return true;
}

bool SceneBuilder::LoadFromFile(
    const std::filesystem::path& path,
    std::optional<Camera3DState>& cameraState
) {
    cameraState.reset();
    SceneBuilderDocument document;
    std::string failure;
    if (!ReadSceneBuilderDocument(path, document, failure)) {
        m_LastDocumentStatus = "Load failed: " + failure;
        m_LastDocumentOperationFailed = true;
        return false;
    }
    if (!m_Objects.empty()) {
        m_LastDocumentStatus = "Load failed: the editor scene is not empty.";
        m_LastDocumentOperationFailed = true;
        return false;
    }
    for (const SceneBuilderDocumentObject& object : document.objects) {
        if (!PrimitiveAvailable(object.primitive) ||
            (IsImportedAsset(object.primitive) &&
                !AssetPathMatchesPrimitive(object.primitive, object.assetPath))) {
            m_LastDocumentStatus = "Load failed: a required primitive is unavailable.";
            m_LastDocumentOperationFailed = true;
            return false;
        }
    }

    // Version 3 owns the complete light inventory; version 4 additionally
    // persists the editor-only directional-light gizmo anchor. Older
    // documents predate light persistence and retain the real startup key
    // light they relied on.
    if (document.lightsAreExplicit) {
        while (!m_Lights.empty()) {
            if (!DestroyLight(m_Lights.back().lightIdentity)) {
                m_LastDocumentStatus = "Load failed: existing light could not be removed.";
                m_LastDocumentOperationFailed = true;
                return false;
            }
        }
    }

    for (const SceneBuilderDocumentObject& source : document.objects) {
        const u64 renderIdentity = CreatePrimitive(source.primitive);
        if (renderIdentity == 0u) {
            m_LastDocumentStatus = "Load failed: scene object creation was refused.";
            m_LastDocumentOperationFailed = true;
            return false;
        }
        if (source.name != FindObject(renderIdentity)->name &&
            !RenameObject(renderIdentity, source.name)) {
            m_LastDocumentStatus = "Load failed: scene object could not be named.";
            m_LastDocumentOperationFailed = true;
            return false;
        }

        ApplyObjectEdit(renderIdentity, source.edit);
        SceneBuilderObjectEdit applied{};
        if (!ReadObjectEdit(renderIdentity, applied) ||
            !SameDocumentEdit(applied, source.edit)) {
            m_LastDocumentStatus = "Load failed: scene object state is invalid.";
            m_LastDocumentOperationFailed = true;
            return false;
        }
    }

    for (const SceneBuilderDocumentLight& source : document.lights) {
        const u64 lightIdentity = CreateLight(source.edit.kind);
        if (lightIdentity == 0u ||
            !ApplyLightEdit(lightIdentity, source.edit)) {
            m_LastDocumentStatus = "Load failed: scene light creation was refused.";
            m_LastDocumentOperationFailed = true;
            return false;
        }

        SceneLightEdit applied{};
        if (!ReadLightEdit(lightIdentity, applied) ||
            !SameSceneLightEdit(applied, source.edit)) {
            m_LastDocumentStatus = "Load failed: scene light state is invalid.";
            m_LastDocumentOperationFailed = true;
            return false;
        }
    }

    std::vector<SceneBuilderReflectionProbeEdit> reflectionProbes =
        document.reflectionProbes;
    if (!document.reflectionProbesAreExplicit && reflectionProbes.empty()) {
        reflectionProbes.push_back(LegacyReflectionProbeEdit());
    }

    while (!m_Scene.ReflectionProbes().empty()) {
        const std::size_t lastIndex = m_Scene.ReflectionProbes().size() - 1u;
        if (!m_Scene.DestroyReflectionProbe(lastIndex)) {
            m_LastDocumentStatus = "Load failed: existing reflection probe could not be removed.";
            m_LastDocumentOperationFailed = true;
            return false;
        }
    }

    for (u32 index = 0u; index < reflectionProbes.size(); ++index) {
        const SceneBuilderReflectionProbeEdit& source = reflectionProbes[index];
        m_Scene.CreateReflectionProbe(
            source.name,
            source.center,
            source.radius,
            source.boxExtents,
            source.color,
            source.intensity,
            source.blendStrength,
            source.falloff,
            source.captureSource,
            source.captureAssetId,
            source.refreshPolicy
        );
        SceneBuilderReflectionProbeEdit applied{};
        if (!ApplyReflectionProbeEdit(index, source) ||
            !ReadReflectionProbeEdit(index, applied) ||
            !SameReflectionProbeEdit(applied, source)) {
            m_LastDocumentStatus = "Load failed: reflection probe state is invalid.";
            m_LastDocumentOperationFailed = true;
            return false;
        }
    }

    cameraState = document.cameraState;
    m_StartupLayoutId = document.startupLayoutId;
    SetEnvironment(document.environment);
    m_SelectedReflectionProbeIndex = ReflectionProbeCount() > 0u ? 0 : -1;
    m_SelectedIdentity = 0u;
    m_SelectedLightIdentity = 0u;
    m_LastDocumentStatus = "Scene loaded: " + path.generic_string();
    m_LastDocumentOperationFailed = false;
    return true;
}

void SceneBuilder::CreateFromSpecEntry(
    std::string_view entry,
    u32& createdCount
) {
    std::string_view remaining = TrimSpecToken(entry);
    const std::size_t firstModifier = remaining.find(':');
    const std::string_view nameToken = TrimSpecToken(
        remaining.substr(
            0,
            firstModifier == std::string_view::npos
                ? remaining.size()
                : firstModifier
        )
    );

    bool nameMatched = false;
    SceneBuilderPrimitive primitive = SceneBuilderPrimitive::Cube;
    const std::string loweredName = LowerSpecToken(nameToken);
    for (u32 index = 0; index < kSceneBuilderPrimitiveCount; ++index) {
        if (LowerSpecToken(kSceneBuilderPrimitives[index].name) == loweredName) {
            primitive = static_cast<SceneBuilderPrimitive>(index);
            nameMatched = true;
            break;
        }
    }
    if (!nameMatched) {
        m_LastCreateFailure = SceneBuilderCreateFailure::UnknownPrimitiveName;
        ++m_CreateFailureCount;
        return;
    }

    // Resolve every modifier before creating anything so an unknown modifier
    // cannot leave a half-configured object in the scene.
    bool castShadow = true;
    std::size_t modifierCursor = firstModifier;
    while (modifierCursor != std::string_view::npos) {
        const std::size_t modifierStart = modifierCursor + 1;
        const std::size_t nextModifier = remaining.find(':', modifierStart);
        const std::string_view modifierToken = TrimSpecToken(
            remaining.substr(
                modifierStart,
                nextModifier == std::string_view::npos
                    ? std::string_view::npos
                    : nextModifier - modifierStart
            )
        );

        if (!modifierToken.empty()) {
            const std::string loweredModifier = LowerSpecToken(modifierToken);
            if (loweredModifier == "noshadow") {
                castShadow = false;
            } else {
                m_LastCreateFailure = SceneBuilderCreateFailure::UnknownModifier;
                ++m_CreateFailureCount;
                return;
            }
        }

        modifierCursor = nextModifier;
    }

    const u64 identity = CreatePrimitive(primitive);
    if (identity == 0u) {
        return;
    }

    ++createdCount;
    if (!castShadow) {
        SceneBuilderObjectEdit edit{};
        if (ReadObjectEdit(identity, edit)) {
            edit.castShadow = false;
            ApplyObjectEdit(identity, edit);
        }
    }
}

bool SceneBuilder::DestroyObject(u64 renderIdentity) {
    const auto found = std::find_if(
        m_Objects.begin(),
        m_Objects.end(),
        [renderIdentity](const SceneBuilderObject& candidate) {
            return candidate.renderIdentity == renderIdentity ||
                std::find(
                    candidate.memberRenderIdentities.begin(),
                    candidate.memberRenderIdentities.end(),
                    renderIdentity
                ) != candidate.memberRenderIdentities.end();
        }
    );
    if (found == m_Objects.end()) {
        return false;
    }

    // Imported assets can expand to many renderables. Their GPU resources stay
    // owned by the importer/material library, while all scene instances are
    // removed as one editor entity.
    std::vector<u64> identities = found->memberRenderIdentities;
    if (identities.empty()) {
        identities.push_back(found->renderIdentity);
    }
    for (const u64 identity : identities) {
        if (!m_Scene.DestroyRenderableByIdentity(identity)) {
            return false;
        }
    }

    const bool destroyedSelection = m_SelectedIdentity == renderIdentity ||
        std::find(
            identities.begin(),
            identities.end(),
            m_SelectedIdentity
        ) != identities.end();
    m_Objects.erase(found);
    ++m_DestroyedObjectCount;
    ++m_EditRevision;
    if (destroyedSelection) {
        const u64 fallbackIdentity = m_Objects.empty()
            ? 0u
            : m_Objects.back().renderIdentity;
        if (fallbackIdentity != 0u) {
            m_Scene.SelectRenderableByIdentity(fallbackIdentity);
        }
        SetSelectedIdentity(fallbackIdentity);
    }

    return true;
}

bool SceneBuilder::DeleteSelectedObject() {
    if (m_SelectedIdentity == 0u || !DestroyObject(m_SelectedIdentity)) {
        return false;
    }

    ++m_SelectionShortcutDeleteCount;
    return true;
}

bool SceneBuilder::DeleteSelectedEntity() {
    if (m_SelectedReflectionProbeIndex >= 0) {
        if (!DestroyReflectionProbe(
                static_cast<u32>(m_SelectedReflectionProbeIndex)
            )) {
            return false;
        }
        ++m_SelectionShortcutDeleteCount;
        return true;
    }

    if (m_SelectedLightIdentity != 0u) {
        if (!DestroyLight(m_SelectedLightIdentity)) {
            return false;
        }
        ++m_SelectionShortcutDeleteCount;
        return true;
    }

    return DeleteSelectedObject();
}

bool SceneBuilder::RenameObject(u64 renderIdentity, std::string name) {
    SceneBuilderObject* object = FindMutableObject(renderIdentity);
    if (object == nullptr || object->renderable == nullptr) {
        return false;
    }
    if (name.empty() || object->name == name) {
        return false;
    }

    object->name = std::move(name);
    std::vector<u64> identities = object->memberRenderIdentities;
    if (identities.empty()) {
        identities.push_back(object->renderIdentity);
    }
    for (u32 partIndex = 0u; partIndex < static_cast<u32>(identities.size()); ++partIndex) {
        if (Renderable3D* part = m_Scene.FindRenderableByIdentity(identities[partIndex])) {
            part->SetName(partIndex == 0u
                ? object->name
                : object->name + " / Part " + std::to_string(partIndex + 1u));
        }
    }
    ++m_RenameCount;
    ++m_EditRevision;

    return true;
}

bool SceneBuilder::ReadObjectEdit(
    u64 renderIdentity,
    SceneBuilderObjectEdit& edit
) const {
    const SceneBuilderObject* object = FindObject(renderIdentity);
    if (object == nullptr ||
        object->renderable == nullptr ||
        object->material == nullptr) {
        return false;
    }

    const Transform3D& transform = object->renderable->Transform();
    const MaterialProperties& properties = object->material->Properties();

    edit.position = transform.Position();
    edit.rotationDegrees = transform.RotationDegrees();
    edit.scale = transform.Scale();
    edit.castShadow = object->renderable->CastShadow();
    edit.baseColor = glm::vec4(
        properties.baseColorFactor[0],
        properties.baseColorFactor[1],
        properties.baseColorFactor[2],
        properties.baseColorFactor[3]
    );
    edit.metallic = properties.cameraControls[0];
    edit.roughness = properties.cameraControls[1];
    edit.emissive = glm::vec3(
        properties.emissiveFactor[0],
        properties.emissiveFactor[1],
        properties.emissiveFactor[2]
    );
    edit.alphaMode = properties.alphaMode;
    edit.alphaCutoff = properties.alphaCutoff;
    edit.doubleSided = properties.doubleSided;

    return true;
}

bool SceneBuilder::ApplyObjectEdit(
    u64 renderIdentity,
    const SceneBuilderObjectEdit& edit
) {
    SceneBuilderObject* object = FindMutableObject(renderIdentity);
    if (object == nullptr ||
        object->renderable == nullptr ||
        object->material == nullptr) {
        return false;
    }

    Renderable3D& renderable = *object->renderable;
    Transform3D& transform = renderable.Transform();
    MaterialProperties& properties = object->material->Properties();

    const glm::vec3 clampedScale{
        std::max(edit.scale.x, 0.001f),
        std::max(edit.scale.y, 0.001f),
        std::max(edit.scale.z, 0.001f)
    };

    bool transformChanged = false;
    if (!NearlyEqual(transform.Position(), edit.position)) {
        transform.SetPosition(edit.position);
        transformChanged = true;
    }
    if (!NearlyEqual(transform.RotationDegrees(), edit.rotationDegrees)) {
        transform.SetRotationDegrees(edit.rotationDegrees);
        transformChanged = true;
    }
    if (!NearlyEqual(transform.Scale(), clampedScale)) {
        transform.SetScale(clampedScale);
        transformChanged = true;
    }
    if (renderable.CastShadow() != edit.castShadow) {
        renderable.SetCastShadow(edit.castShadow);
        transformChanged = true;
    }

    const glm::vec4 clampedBaseColor{
        std::clamp(edit.baseColor.r, 0.0f, 1.0f),
        std::clamp(edit.baseColor.g, 0.0f, 1.0f),
        std::clamp(edit.baseColor.b, 0.0f, 1.0f),
        std::clamp(edit.baseColor.a, 0.0f, 1.0f)
    };
    const f32 clampedMetallic = std::clamp(edit.metallic, 0.0f, 1.0f);
    const f32 clampedRoughness = std::clamp(edit.roughness, 0.04f, 1.0f);
    const glm::vec3 clampedEmissive{
        std::clamp(edit.emissive.r, 0.0f, 1.0f),
        std::clamp(edit.emissive.g, 0.0f, 1.0f),
        std::clamp(edit.emissive.b, 0.0f, 1.0f)
    };
    const f32 clampedAlphaCutoff = std::clamp(edit.alphaCutoff, 0.0f, 1.0f);

    bool materialChanged = false;
    if (!NearlyEqual(properties.baseColorFactor[0], clampedBaseColor.r) ||
        !NearlyEqual(properties.baseColorFactor[1], clampedBaseColor.g) ||
        !NearlyEqual(properties.baseColorFactor[2], clampedBaseColor.b) ||
        !NearlyEqual(properties.baseColorFactor[3], clampedBaseColor.a)) {
        properties.baseColorFactor = {
            clampedBaseColor.r,
            clampedBaseColor.g,
            clampedBaseColor.b,
            clampedBaseColor.a
        };
        materialChanged = true;
    }
    if (!NearlyEqual(properties.cameraControls[0], clampedMetallic) ||
        !NearlyEqual(properties.cameraControls[1], clampedRoughness)) {
        properties.cameraControls[0] = clampedMetallic;
        properties.cameraControls[1] = clampedRoughness;
        materialChanged = true;
    }
    if (!NearlyEqual(properties.emissiveFactor[0], clampedEmissive.r) ||
        !NearlyEqual(properties.emissiveFactor[1], clampedEmissive.g) ||
        !NearlyEqual(properties.emissiveFactor[2], clampedEmissive.b)) {
        properties.emissiveFactor[0] = clampedEmissive.r;
        properties.emissiveFactor[1] = clampedEmissive.g;
        properties.emissiveFactor[2] = clampedEmissive.b;
        materialChanged = true;
    }
    if (properties.alphaMode != edit.alphaMode ||
        !NearlyEqual(properties.alphaCutoff, clampedAlphaCutoff)) {
        properties.alphaMode = edit.alphaMode;
        properties.alphaCutoff = clampedAlphaCutoff;
        materialChanged = true;
    }
    if (properties.doubleSided != edit.doubleSided) {
        properties.doubleSided = edit.doubleSided;
        materialChanged = true;
    }

    // Render class follows the authored alpha mode. The renderer still decides
    // whether the transparent path is enabled; the builder never forces it on.
    const MaterialRenderClass desiredRenderClass =
        properties.alphaMode == MaterialAlphaMode::Blend
            ? MaterialRenderClass::Transparent
            : MaterialRenderClass::DeferredOpaque;
    if (properties.renderClass != desiredRenderClass) {
        properties.renderClass = desiredRenderClass;
        materialChanged = true;
    }

    // A runtime import may map one logical editor entity to several ordinary
    // scene renderables. Keep their transform/shadow and PBR overrides in
    // lockstep with the root while retaining the normal Scene3D render path.
    for (const u64 memberIdentity : object->memberRenderIdentities) {
        if (memberIdentity == object->renderIdentity) {
            continue;
        }
        Renderable3D* member = m_Scene.FindRenderableByIdentity(memberIdentity);
        if (member == nullptr) {
            continue;
        }
        Transform3D& memberTransform = member->Transform();
        if (!NearlyEqual(memberTransform.Position(), edit.position)) {
            memberTransform.SetPosition(edit.position);
            transformChanged = true;
        }
        if (!NearlyEqual(memberTransform.RotationDegrees(), edit.rotationDegrees)) {
            memberTransform.SetRotationDegrees(edit.rotationDegrees);
            transformChanged = true;
        }
        if (!NearlyEqual(memberTransform.Scale(), clampedScale)) {
            memberTransform.SetScale(clampedScale);
            transformChanged = true;
        }
        if (member->CastShadow() != edit.castShadow) {
            member->SetCastShadow(edit.castShadow);
            transformChanged = true;
        }
        if (!m_RenderResources.ContainsMaterial(member->MaterialId())) {
            continue;
        }
        MaterialProperties& memberProperties =
            m_RenderResources.Material(member->MaterialId()).Properties();
        if (memberProperties.baseColorFactor != properties.baseColorFactor ||
            memberProperties.cameraControls[0] != properties.cameraControls[0] ||
            memberProperties.cameraControls[1] != properties.cameraControls[1] ||
            memberProperties.emissiveFactor != properties.emissiveFactor ||
            memberProperties.alphaMode != properties.alphaMode ||
            memberProperties.alphaCutoff != properties.alphaCutoff ||
            memberProperties.doubleSided != properties.doubleSided ||
            memberProperties.renderClass != properties.renderClass) {
            memberProperties.baseColorFactor = properties.baseColorFactor;
            memberProperties.cameraControls[0] = properties.cameraControls[0];
            memberProperties.cameraControls[1] = properties.cameraControls[1];
            memberProperties.emissiveFactor = properties.emissiveFactor;
            memberProperties.alphaMode = properties.alphaMode;
            memberProperties.alphaCutoff = properties.alphaCutoff;
            memberProperties.doubleSided = properties.doubleSided;
            memberProperties.renderClass = properties.renderClass;
            materialChanged = true;
        }
    }

    if (transformChanged) {
        ++m_TransformEditCount;
    }
    if (materialChanged) {
        ++m_MaterialEditCount;
    }
    if (transformChanged || materialChanged) {
        ++m_EditRevision;
    }

    return transformChanged || materialChanged;
}

u64 SceneBuilder::CreateLight(SceneLightKind kind) {
    if (kind != SceneLightKind::Directional) {
        const u32 localLightCount = static_cast<u32>(std::count_if(
            m_Lights.begin(),
            m_Lights.end(),
            [](const SceneBuilderLight& light) {
                return light.kind != SceneLightKind::Directional;
            }
        ));
        if (localLightCount >= kMaxFrameLocalLights) {
            m_LastCreateFailure = SceneBuilderCreateFailure::LightLimitReached;
            ++m_CreateFailureCount;
            return 0u;
        }
    }

    if (kind == SceneLightKind::Directional) {
        const auto existing = std::find_if(
            m_Lights.begin(),
            m_Lights.end(),
            [](const SceneBuilderLight& light) {
                return light.kind == SceneLightKind::Directional;
            }
        );
        if (existing != m_Lights.end()) {
            SelectLight(existing->lightIdentity);
            return existing->lightIdentity;
        }
    }

    const u64 lightIdentity = m_Scene.CreateLight(kind);
    SceneLightEdit edit{};
    if (lightIdentity == 0u || !m_Scene.ReadLightEdit(lightIdentity, edit) ||
        edit.kind != kind) {
        return 0u;
    }

    m_Lights.push_back(SceneBuilderLight{ lightIdentity, kind });
    ++m_CreatedLightCount;
    m_LastCreateFailure = SceneBuilderCreateFailure::None;
    SelectLight(lightIdentity);
    ++m_EditRevision;
    return lightIdentity;
}

bool SceneBuilder::DestroyLight(u64 lightIdentity) {
    const auto found = std::find_if(
        m_Lights.begin(),
        m_Lights.end(),
        [lightIdentity](const SceneBuilderLight& light) {
            return light.lightIdentity == lightIdentity;
        }
    );
    if (found == m_Lights.end() || !m_Scene.DestroyLight(lightIdentity)) {
        return false;
    }

    const bool destroyedSelection = m_SelectedLightIdentity == lightIdentity;
    m_Lights.erase(found);
    ++m_DestroyedLightCount;
    ++m_EditRevision;
    if (destroyedSelection) {
        SetSelectedLightIdentity(0u);
    }
    return true;
}

bool SceneBuilder::ReadLightEdit(
    u64 lightIdentity,
    SceneLightEdit& edit
) const {
    const SceneBuilderLight* light = FindLight(lightIdentity);
    if (light == nullptr || !m_Scene.ReadLightEdit(lightIdentity, edit) ||
        edit.kind != light->kind) {
        return false;
    }

    if (light->kind == SceneLightKind::Directional) {
        edit.position = light->gizmoPosition;
    }
    return true;
}

bool SceneBuilder::ApplyLightEdit(
    u64 lightIdentity,
    const SceneLightEdit& edit
) {
    SceneBuilderLight* light = FindMutableLight(lightIdentity);
    if (light == nullptr || light->kind != edit.kind) {
        return false;
    }

    SceneLightEdit before{};
    if (!m_Scene.ReadLightEdit(lightIdentity, before)) {
        return false;
    }

    // Directional positions are editor-only gizmo anchors. Do not let them
    // become an accidental rendering input when Scene3D consumes this edit.
    SceneLightEdit sceneEdit = edit;
    if (light->kind == SceneLightKind::Directional) {
        sceneEdit.position = before.position;
    }
    if (!m_Scene.ApplyLightEdit(lightIdentity, sceneEdit)) {
        return false;
    }

    SceneLightEdit after{};
    if (!m_Scene.ReadLightEdit(lightIdentity, after)) {
        return false;
    }
    const bool gizmoPositionChanged =
        light->kind == SceneLightKind::Directional &&
        !NearlyEqual(light->gizmoPosition, edit.position);
    if (gizmoPositionChanged) {
        light->gizmoPosition = edit.position;
    }
    if (!SameSceneLightEdit(before, after) || gizmoPositionChanged) {
        ++m_LightEditCount;
        ++m_EditRevision;
    }
    return true;
}

SceneEnvironment3D SceneBuilder::Environment() const {
    return m_Scene.Environment();
}

void SceneBuilder::SetEnvironment(const SceneEnvironment3D& environment) {
    const u64 revisionBefore = m_Scene.RenderRevision();
    m_Scene.SetEnvironment(environment);
    if (m_Scene.RenderRevision() != revisionBefore) {
        ++m_EditRevision;
    }
}

bool SceneBuilder::EnvironmentIblEnabled() const {
    return Environment().iblEnabled;
}

void SceneBuilder::SetEnvironmentIblEnabled(bool enabled) {
    SceneEnvironment3D environment = Environment();
    environment.iblEnabled = enabled;
    SetEnvironment(environment);
}

u32 SceneBuilder::ReflectionProbeCount() const {
    return static_cast<u32>(std::min<std::size_t>(
        m_Scene.ReflectionProbes().size(),
        kSceneBuilderMaxReflectionProbes
    ));
}

i32 SceneBuilder::SelectedReflectionProbeIndex() const {
    return m_SelectedReflectionProbeIndex;
}

bool SceneBuilder::SelectReflectionProbe(u32 index) {
    if (index >= ReflectionProbeCount()) {
        return false;
    }

    m_SelectedReflectionProbeIndex = static_cast<i32>(index);
    m_SelectedIdentity = 0u;
    m_SelectedLightIdentity = 0u;
    return true;
}

bool SceneBuilder::CreateReflectionProbe() {
    if (ReflectionProbeCount() >= kSceneBuilderMaxReflectionProbes) {
        m_LastCreateFailure = SceneBuilderCreateFailure::ReflectionProbeLimitReached;
        ++m_CreateFailureCount;
        return false;
    }

    SceneBuilderReflectionProbeEdit edit = LegacyReflectionProbeEdit();
    edit.name = "Reflection Probe " +
        std::to_string(ReflectionProbeCount() + 1u);
    m_Scene.CreateReflectionProbe(
        edit.name,
        edit.center,
        edit.radius,
        edit.boxExtents,
        edit.color,
        edit.intensity,
        edit.blendStrength,
        edit.falloff,
        edit.captureSource,
        edit.captureAssetId,
        edit.refreshPolicy
    );
    const u32 index = ReflectionProbeCount() - 1u;
    if (!ApplyReflectionProbeEdit(index, edit)) {
        m_Scene.DestroyReflectionProbe(index);
        return false;
    }

    SelectReflectionProbe(index);
    m_LastCreateFailure = SceneBuilderCreateFailure::None;
    ++m_EditRevision;
    return true;
}

bool SceneBuilder::DuplicateReflectionProbe(u32 index) {
    SceneBuilderReflectionProbeEdit copy{};
    if (!ReadReflectionProbeEdit(index, copy) || !CreateReflectionProbe()) {
        return false;
    }

    const u32 duplicateIndex = ReflectionProbeCount() - 1u;
    copy.name += " Copy";
    if (ApplyReflectionProbeEdit(duplicateIndex, copy)) {
        return true;
    }

    DestroyReflectionProbe(duplicateIndex);
    return false;
}

bool SceneBuilder::DestroyReflectionProbe(u32 index) {
    if (index >= ReflectionProbeCount() || !m_Scene.DestroyReflectionProbe(index)) {
        return false;
    }

    const u32 count = ReflectionProbeCount();
    if (m_SelectedReflectionProbeIndex == static_cast<i32>(index)) {
        m_SelectedReflectionProbeIndex = count == 0u
            ? -1
            : static_cast<i32>(std::min(index, count - 1u));
    } else if (m_SelectedReflectionProbeIndex > static_cast<i32>(index)) {
        --m_SelectedReflectionProbeIndex;
    }
    ++m_ReflectionProbeEditCount;
    ++m_EditRevision;
    return true;
}

bool SceneBuilder::ReadReflectionProbeEdit(
    u32 index,
    SceneBuilderReflectionProbeEdit& edit
) const {
    const std::span<const ReflectionProbe3D> probes = m_Scene.ReflectionProbes();
    if (index >= probes.size()) {
        return false;
    }

    const ReflectionProbe3D& probe = probes[index];
    edit.name = probe.name;
    edit.center = probe.center;
    edit.radius = probe.radius;
    edit.boxCenter = probe.boxCenter;
    edit.boxExtents = probe.boxExtents;
    edit.color = probe.color;
    edit.intensity = probe.intensity;
    edit.blendStrength = probe.blendStrength;
    edit.falloff = probe.falloff;
    edit.enabled = probe.enabled;
    edit.captureSource = probe.captureSource;
    edit.captureAssetId = probe.captureAssetId;
    edit.refreshPolicy = probe.refreshPolicy;
    edit.captureExcludedRenderableIdentities =
        probe.captureExcludedRenderableIdentities;
    return true;
}

bool SceneBuilder::ApplyReflectionProbeEdit(
    u32 index,
    const SceneBuilderReflectionProbeEdit& edit
) {
    if (!DocumentReflectionProbeEditIsInRange(edit)) {
        return false;
    }

    SceneBuilderReflectionProbeEdit current{};
    if (!ReadReflectionProbeEdit(index, current)) {
        return false;
    }
    if (SameReflectionProbeEdit(current, edit)) {
        return true;
    }

    const ReflectionProbe3D updated{
        edit.name,
        edit.center,
        edit.radius,
        edit.boxCenter,
        edit.boxExtents,
        edit.color,
        edit.intensity,
        edit.blendStrength,
        edit.falloff,
        edit.enabled,
        edit.captureSource,
        edit.captureAssetId,
        edit.refreshPolicy,
        edit.captureExcludedRenderableIdentities
    };
    if (!m_Scene.UpdateReflectionProbe(index, updated)) {
        return false;
    }

    ++m_ReflectionProbeEditCount;
    ++m_EditRevision;
    return true;
}

bool SceneBuilder::ReadReflectionProbeEdit(
    SceneBuilderReflectionProbeEdit& edit
) const {
    return ReadReflectionProbeEdit(0u, edit);
}

bool SceneBuilder::ApplyReflectionProbeEdit(
    const SceneBuilderReflectionProbeEdit& edit
) {
    EnsureSceneBuilderReflectionCaptureProbe(m_Scene);
    return ApplyReflectionProbeEdit(0u, edit);
}

SceneBuilderReflectionProbeEdit SceneBuilder::LegacyReflectionProbeEdit() const {
    SceneBuilderReflectionProbeEdit edit{};
    SceneBuilderBounds sceneBounds{};
    for (const SceneBuilderObject& object : m_Objects) {
        const SceneBuilderBounds objectBounds = BoundsForBuilderObject(object);
        if (!objectBounds.valid) {
            continue;
        }
        ExpandSceneBuilderBounds(sceneBounds, objectBounds.min);
        ExpandSceneBuilderBounds(sceneBounds, objectBounds.max);
    }

    if (!sceneBounds.valid) {
        return edit;
    }

    // A local probe must capture from the local environment, rather than from
    // a point above it. Legacy documents have one position, so it becomes both
    // the capture origin and the proxy center during migration.
    edit.center = (sceneBounds.min + sceneBounds.max) * 0.5f;
    edit.boxCenter = edit.center;
    const glm::vec3 volumeMargin{ 1.25f };
    edit.boxExtents = glm::min(
        glm::max(
            glm::max(
                glm::abs(edit.center - sceneBounds.min),
                glm::abs(sceneBounds.max - edit.center)
            ) + volumeMargin,
            glm::vec3(0.01f)
        ),
        glm::vec3(256.0f)
    );
    edit.radius = std::clamp(
        glm::length(edit.boxExtents),
        0.01f,
        256.0f
    );

    return edit;
}

u64 SceneBuilder::SelectedIdentity() const {
    return m_SelectedIdentity;
}

bool SceneBuilder::SelectObject(u64 renderIdentity) {
    if (FindObject(renderIdentity) == nullptr) {
        return false;
    }

    if (!m_Scene.SelectRenderableByIdentity(renderIdentity)) {
        return false;
    }

    SetSelectedIdentity(renderIdentity);
    SetSelectedLightIdentity(0u);
    return true;
}

u64 SceneBuilder::SelectedLightIdentity() const {
    return m_SelectedLightIdentity;
}

bool SceneBuilder::SelectLight(u64 lightIdentity) {
    if (lightIdentity != 0u && FindLight(lightIdentity) == nullptr) {
        return false;
    }

    SetSelectedIdentity(0u);
    SetSelectedLightIdentity(lightIdentity);
    return true;
}

void SceneBuilder::SetLightIconOverlayCount(u32 count) {
    m_LightIconOverlayCount = count;
}

void SceneBuilder::RecordLightIconPick(bool selected) {
    ++m_LightIconHitTestCount;
    if (selected) {
        ++m_LightIconSelectionCount;
    }
}

bool SceneBuilder::SelectAlongRay(
    const glm::vec3& origin,
    const glm::vec3& direction
) {
#if !defined(NDEBUG)
    ++m_SelectionRayQueryCount;
#endif
    if (glm::dot(direction, direction) <= kPickEpsilon * kPickEpsilon) {
        return false;
    }

    const glm::vec3 normalizedDirection = glm::normalize(direction);
    const auto intersectMeshBounds = [this, &origin, &normalizedDirection](
        const Renderable3D& renderable,
        f32& hitDistance
    ) {
        if (!m_RenderResources.ContainsMesh(renderable.MeshId())) {
            return false;
        }

        const glm::mat4 inverseModel = glm::inverse(
            renderable.Transform().Matrix()
        );
        const glm::vec3 localOrigin = glm::vec3(
            inverseModel * glm::vec4(origin, 1.0f)
        );
        const glm::vec3 localDirection = glm::vec3(
            inverseModel * glm::vec4(origin + normalizedDirection, 1.0f)
        ) - localOrigin;
        const VulkanMesh& mesh = m_RenderResources.Mesh(renderable.MeshId());
        return IntersectLocalAabb(
            localOrigin,
            localDirection,
            mesh.BoundsMin(),
            mesh.BoundsMax(),
            hitDistance
        );
    };

    SceneBuilderObject* nearestObject = nullptr;
    f32 nearestDistance = std::numeric_limits<f32>::max();
    for (SceneBuilderObject& object : m_Objects) {
        if (object.renderable == nullptr || !object.renderable->Pickable()) {
            continue;
        }

        f32 hitDistance = std::numeric_limits<f32>::max();
        bool hit = false;
        if (IsImportedAsset(object.primitive)) {
            const auto considerMember = [&](u64 identity) {
                const Renderable3D* member = m_Scene.FindRenderableByIdentity(
                    identity
                );
                if (member == nullptr || !member->Pickable()) {
                    return;
                }

                f32 memberDistance = 0.0f;
                if (intersectMeshBounds(*member, memberDistance) &&
                    memberDistance < hitDistance) {
                    hitDistance = memberDistance;
                    hit = true;
                }
            };
            if (object.memberRenderIdentities.empty()) {
                considerMember(object.renderIdentity);
            } else {
                for (const u64 identity : object.memberRenderIdentities) {
                    considerMember(identity);
                }
            }
        } else {
            const glm::mat4 inverseModel = glm::inverse(
                object.renderable->Transform().Matrix()
            );
            const glm::vec3 localOrigin = glm::vec3(
                inverseModel * glm::vec4(origin, 1.0f)
            );
            const glm::vec3 localDirection = glm::vec3(
                inverseModel * glm::vec4(origin + normalizedDirection, 1.0f)
            ) - localOrigin;
            hit = IntersectPrimitiveSurface(
                object.primitive,
                localOrigin,
                localDirection,
                hitDistance
            );
        }

        if (!hit || hitDistance >= nearestDistance) {
            continue;
        }

        nearestDistance = hitDistance;
        nearestObject = &object;
    }

    if (nearestObject == nullptr || !SelectObject(nearestObject->renderIdentity)) {
        return false;
    }

#if !defined(NDEBUG)
    ++m_SelectionRayHitCount;
#endif
    return true;
}

void SceneBuilder::SyncSelectionFromScene() {
    if (m_SelectedReflectionProbeIndex >= 0) {
        if (static_cast<u32>(m_SelectedReflectionProbeIndex) <
            ReflectionProbeCount()) {
            return;
        }
        m_SelectedReflectionProbeIndex = -1;
    }

    if (m_SelectedLightIdentity != 0u) {
        SceneLightEdit light{};
        if (ReadLightEdit(m_SelectedLightIdentity, light)) {
            return;
        }
        SetSelectedLightIdentity(0u);
    }

    const Renderable3D* selected = m_Scene.SelectedRenderable();
    const u64 resolvedIdentity = selected != nullptr &&
        FindObject(selected->RenderIdentity()) != nullptr
            ? selected->RenderIdentity()
            : 0u;
    if (SetSelectedIdentity(resolvedIdentity)) {
        ++m_SelectionSyncCount;
    }
}

bool SceneBuilder::SetSelectedIdentity(u64 renderIdentity) {
    if (renderIdentity != 0u && FindObject(renderIdentity) == nullptr) {
        return false;
    }
    const bool changed = m_SelectedIdentity != renderIdentity ||
        (renderIdentity != 0u && m_SelectedReflectionProbeIndex >= 0);
    m_SelectedIdentity = renderIdentity;
    if (renderIdentity != 0u) {
        m_SelectedLightIdentity = 0u;
        m_SelectedReflectionProbeIndex = -1;
    }
    return changed;
}

bool SceneBuilder::SetSelectedLightIdentity(u64 lightIdentity) {
    if (lightIdentity != 0u && FindLight(lightIdentity) == nullptr) {
        return false;
    }
    const bool changed = m_SelectedLightIdentity != lightIdentity ||
        (lightIdentity != 0u && m_SelectedReflectionProbeIndex >= 0);
    m_SelectedLightIdentity = lightIdentity;
    if (lightIdentity != 0u) {
        m_SelectedIdentity = 0u;
        m_SelectedReflectionProbeIndex = -1;
    }
    return changed;
}

const std::vector<SceneBuilderObject>& SceneBuilder::Objects() const {
    return m_Objects;
}

const SceneBuilderObject* SceneBuilder::FindObject(u64 renderIdentity) const {
    if (renderIdentity == 0) {
        return nullptr;
    }

    for (const SceneBuilderObject& object : m_Objects) {
        if (object.renderIdentity == renderIdentity ||
            std::find(
                object.memberRenderIdentities.begin(),
                object.memberRenderIdentities.end(),
                renderIdentity
            ) != object.memberRenderIdentities.end()) {
            return &object;
        }
    }

    return nullptr;
}

SceneBuilderObject* SceneBuilder::FindMutableObject(u64 renderIdentity) {
    if (renderIdentity == 0) {
        return nullptr;
    }

    for (SceneBuilderObject& object : m_Objects) {
        if (object.renderIdentity == renderIdentity ||
            std::find(
                object.memberRenderIdentities.begin(),
                object.memberRenderIdentities.end(),
                renderIdentity
            ) != object.memberRenderIdentities.end()) {
            return &object;
        }
    }

    return nullptr;
}

const std::vector<SceneBuilderLight>& SceneBuilder::Lights() const {
    return m_Lights;
}

const SceneBuilderLight* SceneBuilder::FindLight(u64 lightIdentity) const {
    if (lightIdentity == 0u) {
        return nullptr;
    }

    const auto found = std::find_if(
        m_Lights.begin(),
        m_Lights.end(),
        [lightIdentity](const SceneBuilderLight& light) {
            return light.lightIdentity == lightIdentity;
        }
    );
    return found != m_Lights.end() ? &*found : nullptr;
}

SceneBuilderLight* SceneBuilder::FindMutableLight(u64 lightIdentity) {
    if (lightIdentity == 0u) {
        return nullptr;
    }

    const auto found = std::find_if(
        m_Lights.begin(),
        m_Lights.end(),
        [lightIdentity](const SceneBuilderLight& light) {
            return light.lightIdentity == lightIdentity;
        }
    );
    return found != m_Lights.end() ? &*found : nullptr;
}

void SceneBuilder::AdoptPrimaryDirectionalLight() {
    const DirectionalLight3D* primary = m_Scene.PrimaryDirectionalLight();
    if (primary == nullptr || primary->identity == 0u ||
        FindLight(primary->identity) != nullptr) {
        return;
    }

    m_Lights.push_back(SceneBuilderLight{
        primary->identity,
        SceneLightKind::Directional
    });
}

SceneBuilderStats SceneBuilder::Stats() const {
    SceneBuilderStats stats{};
    stats.available = Available() ? 1u : 0u;
    stats.primitiveAvailabilityMask = m_PrimitiveAvailabilityMask;
    stats.objectCount = static_cast<u32>(m_Objects.size());
    stats.createdObjectCount = m_CreatedObjectCount;
    stats.destroyedObjectCount = m_DestroyedObjectCount;
    stats.lightCount = static_cast<u32>(m_Lights.size());
    stats.createdLightCount = m_CreatedLightCount;
    stats.destroyedLightCount = m_DestroyedLightCount;
    stats.liveMaterialCount = static_cast<u32>(m_Objects.size());
    stats.materialLibraryCount = static_cast<u32>(m_MaterialLibrary.Count());
    stats.frameMaterialBudget = static_cast<u32>(kMaxFrameMaterials);
    stats.sceneRenderableCount = static_cast<u32>(m_Scene.Count());
    stats.selectedIdentity = m_SelectedIdentity;
    stats.selectedLightIdentity = m_SelectedLightIdentity;
    stats.selectedReflectionProbeIndex = m_SelectedReflectionProbeIndex;
    stats.editRevision = m_EditRevision;
    stats.transformEditCount = m_TransformEditCount;
    stats.materialEditCount = m_MaterialEditCount;
    stats.lightEditCount = m_LightEditCount;
    stats.lightIconOverlayCount = m_LightIconOverlayCount;
    stats.lightIconHitTestCount = m_LightIconHitTestCount;
    stats.lightIconSelectionCount = m_LightIconSelectionCount;
    stats.renameCount = m_RenameCount;
    stats.selectionSyncCount = m_SelectionSyncCount;
    stats.selectionShortcutDeleteCount = m_SelectionShortcutDeleteCount;
    stats.selectionRayQueryCount = m_SelectionRayQueryCount;
    stats.selectionRayHitCount = m_SelectionRayHitCount;
    stats.materialDescriptorRefreshCount = m_MaterialDescriptorRefreshCount;
    stats.lastCreateFailure = static_cast<u32>(m_LastCreateFailure);
    stats.createFailureCount = m_CreateFailureCount;
    const SceneEnvironment3D environment = Environment();
    stats.environmentIblEnabled = environment.iblEnabled ? 1u : 0u;
    stats.environmentSkyboxEnabled = environment.skyboxEnabled ? 1u : 0u;
    stats.environmentDiffuseIntensity = environment.diffuseIntensity;
    stats.environmentSpecularIntensity = environment.specularIntensity;
    stats.environmentHorizonBlend = environment.horizonBlend;
    stats.environmentSkyboxIntensity = environment.skyboxIntensity;
    stats.environmentSkyboxBlur = environment.skyboxBlur;
    stats.environmentLightingAsset = static_cast<u32>(environment.lightingAsset);
    const std::span<const ReflectionProbe3D> reflectionProbes =
        m_Scene.ReflectionProbes();
    stats.reflectionProbeCount = static_cast<u32>(reflectionProbes.size());
    stats.reflectionProbeEditCount = m_ReflectionProbeEditCount;
    for (const ReflectionProbe3D& probe : reflectionProbes) {
        if (probe.captureSource == ReflectionProbeCaptureSource::CapturedScene) {
            ++stats.reflectionProbeCapturedSceneCount;
        }
        if (probe.refreshPolicy == ReflectionProbeRefreshPolicy::Static) {
            ++stats.reflectionProbeStaticRefreshCount;
        }
        stats.reflectionProbeExcludedRenderableCount += static_cast<u32>(
            std::min<std::size_t>(
                probe.captureExcludedRenderableIdentities.size(),
                std::numeric_limits<u32>::max()
            )
        );
    }
    stats.selfTestRan = m_SelfTestRan;
    stats.selfTestPassed = m_SelfTestPassed;
    stats.selfTestFailedCheckMask = m_SelfTestFailedCheckMask;

    const SceneBuilderObject* selected = FindObject(m_SelectedIdentity);
    stats.selectedPrimitive = selected != nullptr
        ? static_cast<u32>(selected->primitive)
        : 0u;

    for (const SceneBuilderObject& object : m_Objects) {
        const u32 index = static_cast<u32>(object.primitive);
        if (index < kSceneBuilderPrimitiveCount) {
            ++stats.primitiveCounts[index];
        }
        if (object.material != nullptr &&
            object.material->Properties().alphaMode == MaterialAlphaMode::Blend) {
            ++stats.blendObjectCount;
        }
    }

    for (const SceneBuilderLight& light : m_Lights) {
        const u32 index = static_cast<u32>(light.kind);
        if (index < stats.lightCounts.size()) {
            ++stats.lightCounts[index];
        }
    }

    return stats;
}

std::string_view SceneBuilder::PrimitiveName(SceneBuilderPrimitive primitive) {
    return DescriptorFor(primitive).name;
}

std::string_view SceneBuilder::PrimitiveMeshId(SceneBuilderPrimitive primitive) {
    return DescriptorFor(primitive).meshId;
}

std::string_view SceneBuilder::PrimitiveAssetPath(
    SceneBuilderPrimitive primitive
) {
    switch (primitive) {
        case SceneBuilderPrimitive::Lvjuren:
            return kLvjurenAssetPath;
        case SceneBuilderPrimitive::Building1:
            return kBuilding1AssetPath;
        case SceneBuilderPrimitive::Building2:
            return kBuilding2AssetPath;
        case SceneBuilderPrimitive::Building3:
            return kBuilding3AssetPath;
        case SceneBuilderPrimitive::Building4:
            return kBuilding4AssetPath;
        case SceneBuilderPrimitive::Car1:
            return kCar1AssetPath;
        case SceneBuilderPrimitive::Car2:
            return kCar2AssetPath;
        default:
            return {};
    }
}

#if !defined(NDEBUG)
bool SceneBuilder::RunSelfTest() {
    m_SelfTestRan = 1u;
    m_SelfTestPassed = 0u;
    m_SelfTestFailedCheckMask = 0u;

    u32 checkBit = 0u;
    bool allChecksPassed = true;
    auto check = [this, &checkBit, &allChecksPassed](bool condition) {
        if (!condition) {
            allChecksPassed = false;
            const u32 recordedBit = std::min(checkBit, 31u);
            m_SelfTestFailedCheckMask |= (1u << recordedBit);
        }
        ++checkBit;
    };

#if defined(SE_ENABLE_SCENE_BUILDER_GIZMO)
    check(SceneBuilderGizmo::DebugValidateSceneBuilderTrsRoundTrip());
    check(SceneBuilderGizmo::DebugValidateCameraInputArbitration());
#endif

    // The G-buffer and CSM receiver bias both consume these normals. Check
    // the mesh producer against its actual side-triangle winding.
    const MeshData3D coneMesh = MeshFactory::CreateCone(16u);
    bool coneSideNormalsMatchGeometry =
        coneMesh.indices.size() == 16u * 6u &&
        coneMesh.vertices.size() >= 1u + 16u + 16u * 2u;
    for (u32 segment = 0u; coneSideNormalsMatchGeometry && segment < 16u; ++segment) {
        const std::size_t indexOffset = static_cast<std::size_t>(segment) * 6u + 3u;
        const u32 vertexIndices[] = {
            coneMesh.indices[indexOffset],
            coneMesh.indices[indexOffset + 1u],
            coneMesh.indices[indexOffset + 2u]
        };
        const auto position = [&coneMesh](u32 vertexIndex) {
            const auto& value = coneMesh.vertices[vertexIndex].position;
            return glm::vec3{ value[0], value[1], value[2] };
        };
        const auto normal = [&coneMesh](u32 vertexIndex) {
            const auto& value = coneMesh.vertices[vertexIndex].normal;
            return glm::vec3{ value[0], value[1], value[2] };
        };
        const glm::vec3 geometricNormal = glm::normalize(glm::cross(
            position(vertexIndices[1]) - position(vertexIndices[0]),
            position(vertexIndices[2]) - position(vertexIndices[0])
        ));
        for (u32 vertexIndex : vertexIndices) {
            const glm::vec3 vertexNormal = normal(vertexIndex);
            if (std::abs(glm::length(vertexNormal) - 1.0f) > 0.001f ||
                glm::dot(vertexNormal, geometricNormal) < 0.90f) {
                coneSideNormalsMatchGeometry = false;
                break;
            }
        }
    }
    check(coneSideNormalsMatchGeometry);

    // Lights use the normal Scene3D -> frame-light-buffer path. Exercise the
    // stable editor identity seam, including normalization/clamping performed
    // by Scene3D, and retain one local light of every supported type for the
    // renderer-consumer checks in the health lane.
    const u32 baselineLightCount = static_cast<u32>(m_Lights.size());
    const u64 lightRevisionBeforeEdit = m_Scene.LightRevision();
    const u64 directionalLight = CreateLight(SceneLightKind::Directional);
    const u64 pointLight = CreateLight(SceneLightKind::Point);
    const u64 spotLight = CreateLight(SceneLightKind::Spot);
    const u64 rectLight = CreateLight(SceneLightKind::Rect);
    const u64 destroyedLight = CreateLight(SceneLightKind::Point);
    check(directionalLight != 0u && pointLight != 0u && spotLight != 0u &&
        rectLight != 0u && destroyedLight != 0u);
    check(directionalLight != pointLight && pointLight != spotLight &&
        spotLight != rectLight && rectLight != destroyedLight);
    check(m_Lights.size() == baselineLightCount + 4u);
    check(FindLight(directionalLight) != nullptr &&
        FindLight(pointLight) != nullptr &&
        FindLight(spotLight) != nullptr &&
        FindLight(rectLight) != nullptr);

    SceneLightEdit directionalEdit{};
    check(ReadLightEdit(directionalLight, directionalEdit));
    directionalEdit.direction = { -4.0f, -8.0f, -3.0f };
    directionalEdit.position = { -2.5f, 3.5f, 1.25f };
    directionalEdit.intensity = 1.7f;
    directionalEdit.ambient = 0.11f;
    directionalEdit.specular = 0.48f;
    directionalEdit.angularRadiusRadians = 1.0f;
    check(ApplyLightEdit(directionalLight, directionalEdit));
    SceneLightEdit directionalReadBack{};
    check(ReadLightEdit(directionalLight, directionalReadBack));
    check(std::abs(glm::length(directionalReadBack.direction) - 1.0f) < 0.0001f &&
        NearlyEqual(directionalReadBack.angularRadiusRadians, 0.05f) &&
        NearlyEqual(directionalReadBack.position, directionalEdit.position));

    // A directional light's finite position is only its editor gizmo anchor.
    // Moving it must not mutate Scene3D lighting state or the rendered shadow.
    const u64 lightRevisionBeforeDirectionalGizmoMove = m_Scene.LightRevision();
    SceneLightEdit directionalGizmoMove = directionalReadBack;
    directionalGizmoMove.position += glm::vec3{ 1.25f, -0.5f, 0.75f };
    check(ApplyLightEdit(directionalLight, directionalGizmoMove));
    check(ReadLightEdit(directionalLight, directionalReadBack) &&
        NearlyEqual(directionalReadBack.position, directionalGizmoMove.position));
    check(m_Scene.LightRevision() == lightRevisionBeforeDirectionalGizmoMove);

    SceneLightEdit pointEdit{};
    check(ReadLightEdit(pointLight, pointEdit));
    pointEdit.name = "SelfTest Point Light";
    pointEdit.position = { -1.25f, 2.0f, 0.75f };
    pointEdit.color = { 0.25f, 0.8f, 1.2f };
    pointEdit.intensity = 7.5f;
    pointEdit.radius = -2.0f;
    pointEdit.sourceRadius = -1.0f;
    check(ApplyLightEdit(pointLight, pointEdit));
    SceneLightEdit pointReadBack{};
    check(ReadLightEdit(pointLight, pointReadBack));
    check(NearlyEqual(pointReadBack.position, pointEdit.position) &&
        NearlyEqual(pointReadBack.radius, 0.0f) &&
        NearlyEqual(pointReadBack.sourceRadius, 0.0f));
    // Keep the retained point light eligible for the renderer-consumer check
    // after proving Scene3D clamps the invalid radius/source-radius inputs.
    pointReadBack.radius = 6.0f;
    pointReadBack.sourceRadius = 0.05f;
    check(ApplyLightEdit(pointLight, pointReadBack));
    check(ReadLightEdit(pointLight, pointReadBack) &&
        NearlyEqual(pointReadBack.radius, 6.0f) &&
        NearlyEqual(pointReadBack.sourceRadius, 0.05f));

    SceneLightEdit spotEdit{};
    check(ReadLightEdit(spotLight, spotEdit));
    spotEdit.name = "SelfTest Spot Light";
    spotEdit.position = { 0.75f, 3.0f, 1.5f };
    spotEdit.direction = { 0.0f, -6.0f, 0.0f };
    spotEdit.color = { 1.0f, 0.45f, 0.2f };
    spotEdit.intensity = 6.0f;
    spotEdit.radius = 8.0f;
    spotEdit.sourceRadius = 0.08f;
    spotEdit.innerConeDegrees = 22.0f;
    spotEdit.outerConeDegrees = 34.0f;
    check(ApplyLightEdit(spotLight, spotEdit));
    SceneLightEdit spotReadBack{};
    check(ReadLightEdit(spotLight, spotReadBack));
    check(NearlyEqual(spotReadBack.direction, glm::vec3{ 0.0f, -1.0f, 0.0f }) &&
        NearlyEqual(spotReadBack.innerConeDegrees, spotEdit.innerConeDegrees) &&
        NearlyEqual(spotReadBack.outerConeDegrees, spotEdit.outerConeDegrees));

    SceneLightEdit rectEdit{};
    check(ReadLightEdit(rectLight, rectEdit));
    rectEdit.name = "SelfTest Rect Light";
    rectEdit.position = { 1.5f, 2.75f, -0.5f };
    rectEdit.direction = { 0.0f, -5.0f, 0.0f };
    rectEdit.color = { 0.7f, 0.5f, 1.0f };
    rectEdit.intensity = 5.0f;
    rectEdit.radius = 7.0f;
    rectEdit.width = 2.5f;
    rectEdit.height = 1.4f;
    rectEdit.specular = 0.65f;
    check(ApplyLightEdit(rectLight, rectEdit));
    SceneLightEdit rectReadBack{};
    check(ReadLightEdit(rectLight, rectReadBack));
    check(NearlyEqual(rectReadBack.width, rectEdit.width) &&
        NearlyEqual(rectReadBack.height, rectEdit.height) &&
        NearlyEqual(rectReadBack.specular, rectEdit.specular));
    check(m_Scene.LightRevision() > lightRevisionBeforeEdit);
    check(m_LightEditCount >= 4u);

    check(DestroyLight(destroyedLight));
    SceneLightEdit staleLight{};
    check(!ReadLightEdit(destroyedLight, staleLight));
    check(m_Lights.size() == baselineLightCount + 3u);

    const u32 baselineObjectCount = static_cast<u32>(m_Objects.size());
    const std::size_t baselineSceneCount = m_Scene.Count();
    const u64 baselineMembership = m_Scene.MembershipRevision();

    // 1. Create one of every primitive.
    std::array<u64, kSceneBuilderPrimitiveCount> identities{};
    for (u32 index = 0; index < kSceneBuilderPrimitiveCount; ++index) {
        identities[index] =
            CreatePrimitive(static_cast<SceneBuilderPrimitive>(index));
    }
    check(std::all_of(
        identities.begin(),
        identities.end(),
        [](u64 identity) { return identity != 0u; }
    ));
    check(m_Objects.size() == baselineObjectCount + kSceneBuilderPrimitiveCount);
    check(m_Scene.Count() == baselineSceneCount + kSceneBuilderPrimitiveCount);
    check(m_Scene.MembershipRevision() > baselineMembership);

    // 2. Every created object must resolve back through stable identity to a
    //    registered mesh and a registered material.
    bool resourcesResolve = true;
    bool identitiesUnique = true;
    for (u32 index = 0; index < kSceneBuilderPrimitiveCount; ++index) {
        const SceneBuilderObject* object = FindObject(identities[index]);
        if (object == nullptr ||
            object->renderable == nullptr ||
            object->material == nullptr ||
            m_Scene.FindRenderableByIdentity(identities[index]) != object->renderable ||
            !m_RenderResources.ContainsMesh(object->meshId) ||
            !m_RenderResources.ContainsMaterial(object->materialId) ||
            object->primitive != static_cast<SceneBuilderPrimitive>(index)) {
            resourcesResolve = false;
        }
        for (u32 other = index + 1; other < kSceneBuilderPrimitiveCount; ++other) {
            if (identities[index] == identities[other]) {
                identitiesUnique = false;
            }
        }
    }
    check(resourcesResolve);
    check(identitiesUnique);

    // 3. The generic viewport ray picker must import its stable result into
    // the builder without making the scene selection the deletion authority.
    const u32 selectionSyncCountBeforePick = m_SelectionSyncCount;
    check(m_Scene.SelectAlongRay({ 0.0f, 0.0f, 7.0f }, { 0.0f, 0.0f, -1.0f }));
    SyncSelectionFromScene();
    const Renderable3D* pickedRenderable = m_Scene.SelectedRenderable();
    check(pickedRenderable != nullptr &&
        pickedRenderable->RenderIdentity() == m_SelectedIdentity &&
        FindObject(m_SelectedIdentity) != nullptr);
    check(m_SelectionSyncCount > selectionSyncCountBeforePick);

    // 3a. Editor picking must test the actual primitive surface. A cone's
    // bounds overlap the ray here, but its apex-only y=0 section does not;
    // the visible cube behind it must win instead.
    const auto moveForPickTest = [&](u64 identity, const glm::vec3& position) {
        SceneBuilderObjectEdit edit{};
        if (!ReadObjectEdit(identity, edit)) {
            return false;
        }
        edit.position = position;
        return ApplyObjectEdit(identity, edit);
    };
    check(moveForPickTest(identities[0], { 0.4f, 0.0f, -1.0f }));
    check(moveForPickTest(identities[1], { 20.0f, 0.0f, 0.0f }));
    check(moveForPickTest(identities[2], { 20.0f, 0.0f, 0.0f }));
    check(moveForPickTest(identities[3], { 0.0f, 0.0f, 1.0f }));
    check(SelectAlongRay({ 0.4f, 0.0f, 7.0f }, { 0.0f, 0.0f, -1.0f }));
    check(m_SelectedIdentity == identities[0]);

    // 3b. When two real primitive surfaces overlap the cursor ray, select the
    // nearest visible surface instead of the first object in storage order.
    check(moveForPickTest(identities[0], { 0.0f, 0.0f, -1.0f }));
    check(moveForPickTest(identities[2], { 0.0f, 0.0f, 1.0f }));
    check(moveForPickTest(identities[3], { 20.0f, 0.0f, 0.0f }));
    check(SelectAlongRay({ 0.0f, 0.0f, 7.0f }, { 0.0f, 0.0f, -1.0f }));
    check(m_SelectedIdentity == identities[2]);
    check(m_SelectionRayQueryCount == 2u);
    check(m_SelectionRayHitCount == 2u);

    // 4. Mutating transform and PBR inputs must round-trip and bump the scene
    //    render revision so cached queues cannot be reused unchanged.
    const u64 mutatedIdentity = identities[0];
    const u64 renderRevisionBeforeEdit = m_Scene.RenderRevision();
    const SceneBuilderObject* mutatedObject = FindObject(mutatedIdentity);
    const u64 renderStateVersionBeforeEdit = mutatedObject != nullptr &&
        mutatedObject->renderable != nullptr
            ? mutatedObject->renderable->RenderStateVersion()
            : 0u;
    SceneBuilderObjectEdit edit{};
    check(ReadObjectEdit(mutatedIdentity, edit));
    edit.position = { 1.25f, 0.5f, -2.0f };
    edit.rotationDegrees = { 0.0f, 35.0f, 0.0f };
    edit.scale = { 1.5f, 0.75f, 1.5f };
    edit.baseColor = { 0.2f, 0.6f, 0.9f, 1.0f };
    edit.metallic = 0.85f;
    edit.roughness = 0.15f;
    edit.emissive = { 0.1f, 0.05f, 0.0f };
    edit.doubleSided = true;
    edit.castShadow = false;
    check(ApplyObjectEdit(mutatedIdentity, edit));

    SceneBuilderObjectEdit readBack{};
    check(ReadObjectEdit(mutatedIdentity, readBack));
    check(NearlyEqual(readBack.position, edit.position));
    check(NearlyEqual(readBack.scale, edit.scale));
    check(NearlyEqual(readBack.metallic, edit.metallic));
    check(NearlyEqual(readBack.roughness, edit.roughness));
    check(readBack.doubleSided);
    check(readBack.castShadow == false);
    check(m_Scene.RenderRevision() > renderRevisionBeforeEdit);
    mutatedObject = FindObject(mutatedIdentity);
    check(mutatedObject != nullptr &&
        mutatedObject->renderable != nullptr &&
        mutatedObject->renderable->RenderStateVersion() > renderStateVersionBeforeEdit);

    // The viewport move gizmo produces only a world-space position and must
    // preserve every other transform field when it uses this same edit path.
    const glm::vec3 gizmoRotationBeforeMove = readBack.rotationDegrees;
    const glm::vec3 gizmoScaleBeforeMove = readBack.scale;
    SceneBuilderObjectEdit gizmoMove = readBack;
    gizmoMove.position += glm::vec3{ 0.75f, -0.25f, 0.50f };
    check(ApplyObjectEdit(mutatedIdentity, gizmoMove));
    SceneBuilderObjectEdit gizmoMoveReadBack{};
    check(ReadObjectEdit(mutatedIdentity, gizmoMoveReadBack));
    check(NearlyEqual(gizmoMoveReadBack.position, gizmoMove.position));
    check(NearlyEqual(gizmoMoveReadBack.rotationDegrees, gizmoRotationBeforeMove));
    check(NearlyEqual(gizmoMoveReadBack.scale, gizmoScaleBeforeMove));

    // Rotation and scale gizmo results travel through the same edit contract.
    // Each mode must preserve the two transform fields it does not own.
    SceneBuilderObjectEdit gizmoRotate = gizmoMoveReadBack;
    gizmoRotate.rotationDegrees += glm::vec3{ 12.0f, -27.0f, 18.0f };
    check(ApplyObjectEdit(mutatedIdentity, gizmoRotate));
    SceneBuilderObjectEdit gizmoRotateReadBack{};
    check(ReadObjectEdit(mutatedIdentity, gizmoRotateReadBack));
    check(NearlyEqual(gizmoRotateReadBack.position, gizmoMove.position));
    check(NearlyEqual(gizmoRotateReadBack.rotationDegrees, gizmoRotate.rotationDegrees));
    check(NearlyEqual(gizmoRotateReadBack.scale, gizmoScaleBeforeMove));

    SceneBuilderObjectEdit gizmoScale = gizmoRotateReadBack;
    gizmoScale.scale *= glm::vec3{ 0.80f, 1.35f, 1.10f };
    check(ApplyObjectEdit(mutatedIdentity, gizmoScale));
    SceneBuilderObjectEdit gizmoScaleReadBack{};
    check(ReadObjectEdit(mutatedIdentity, gizmoScaleReadBack));
    check(NearlyEqual(gizmoScaleReadBack.position, gizmoMove.position));
    check(NearlyEqual(gizmoScaleReadBack.rotationDegrees, gizmoRotate.rotationDegrees));
    check(NearlyEqual(gizmoScaleReadBack.scale, gizmoScale.scale));

    // The editor's backface-culling control is the inverse of this material
    // flag. Restore culling before queue validation and prove both states work.
    edit.doubleSided = false;
    check(ApplyObjectEdit(mutatedIdentity, edit));
    check(ReadObjectEdit(mutatedIdentity, readBack) && !readBack.doubleSided);

    // 5. Rename must reach the scene renderable.
    check(RenameObject(mutatedIdentity, "SelfTest Renamed"));
    mutatedObject = FindObject(mutatedIdentity);
    check(mutatedObject != nullptr &&
        mutatedObject->renderable != nullptr &&
        mutatedObject->renderable->Name() == "SelfTest Renamed");

    // 6. The Delete shortcut path must delete only the selected builder object,
    //    invalidate membership, and retain a valid fallback selection.
    const u64 destroyedIdentity = identities[1];
    const u64 membershipBeforeDestroy = m_Scene.MembershipRevision();
    check(SelectObject(destroyedIdentity));
    check(DeleteSelectedObject());
    check(m_SelectionShortcutDeleteCount == 1u);
    check(FindObject(destroyedIdentity) == nullptr);
    check(m_Scene.FindRenderableByIdentity(destroyedIdentity) == nullptr);
    check(m_Scene.MembershipRevision() > membershipBeforeDestroy);
    check(m_Scene.Count() == baselineSceneCount + kSceneBuilderPrimitiveCount - 1u);
    check(!DestroyObject(destroyedIdentity));
    check(m_SelectedIdentity != destroyedIdentity);
    check(m_Scene.SelectedRenderable() == nullptr ||
        m_Scene.SelectedRenderable()->RenderIdentity() != destroyedIdentity);

    // The picking regressions intentionally move objects off camera. Restore
    // the surviving objects to a visible arrangement so the render-queue
    // assertions exercise all three live primitives after the test completes.
    check(moveForPickTest(identities[2], { -1.35f, 0.0f, 0.30f }));
    check(moveForPickTest(identities[3], { 1.20f, 0.0f, 0.60f }));

    // 7. The complete environment is scene state, not a renderer debug
    // override. It must invalidate render state and survive round-tripping.
    const u64 environmentRenderRevision = m_Scene.RenderRevision();
    SceneEnvironment3D expectedEnvironment = Environment();
    expectedEnvironment.iblEnabled = true;
    expectedEnvironment.diffuseIntensity = 0.35f;
    expectedEnvironment.specularIntensity = 0.70f;
    expectedEnvironment.horizonBlend = 0.18f;
    expectedEnvironment.skyboxEnabled = true;
    expectedEnvironment.skyboxIntensity = 0.80f;
    expectedEnvironment.skyboxBlur = 1.50f;
    expectedEnvironment.lightingAsset = SceneEnvironmentLightingAsset::StudioPanorama;
    SetEnvironment(expectedEnvironment);
    check(SameSceneEnvironment(Environment(), expectedEnvironment) &&
        m_Scene.RenderRevision() > environmentRenderRevision);

    // 8. A reflection probe is scene-owned state. Its edit path must reach
    // Scene3D and retain the exact producer, volume, and refresh contract.
    const u64 reflectionProbeLightRevision = m_Scene.LightRevision();
    SceneBuilderReflectionProbeEdit expectedReflectionProbe{};
    check(ReadReflectionProbeEdit(expectedReflectionProbe));
    expectedReflectionProbe.name = "Self Test Reflection Probe";
    expectedReflectionProbe.center = { 0.4f, 3.2f, -0.6f };
    expectedReflectionProbe.radius = 9.5f;
    expectedReflectionProbe.boxCenter = { -0.8f, 1.7f, 0.9f };
    expectedReflectionProbe.boxExtents = { 6.0f, 4.5f, 7.0f };
    expectedReflectionProbe.color = { 0.85f, 0.95f, 1.0f };
    expectedReflectionProbe.intensity = 0.90f;
    expectedReflectionProbe.blendStrength = 0.72f;
    expectedReflectionProbe.falloff = 1.85f;
    expectedReflectionProbe.enabled = true;
    expectedReflectionProbe.captureSource =
        ReflectionProbeCaptureSource::CapturedScene;
    expectedReflectionProbe.captureAssetId.clear();
    expectedReflectionProbe.refreshPolicy = ReflectionProbeRefreshPolicy::Static;
    check(ApplyReflectionProbeEdit(expectedReflectionProbe));
    SceneBuilderReflectionProbeEdit reflectionProbeReadBack{};
    check(ReadReflectionProbeEdit(reflectionProbeReadBack) &&
        SameReflectionProbeEdit(reflectionProbeReadBack, expectedReflectionProbe) &&
        m_Scene.LightRevision() > reflectionProbeLightRevision &&
        m_ReflectionProbeEditCount > 0u);

    // Legacy documents had no probe entity. Its migration must retain a single
    // coherent local-probe origin for both capture and proxy placement.
    const SceneBuilderReflectionProbeEdit legacyPlacement =
        LegacyReflectionProbeEdit();
    check(DocumentReflectionProbeEditIsInRange(legacyPlacement) &&
        NearlyEqual(legacyPlacement.center, legacyPlacement.boxCenter));

    // 9. Scene persistence must preserve every editable field through the
    // versioned JSON writer and the same parser used by startup auto-load.
    std::error_code persistenceError;
    const std::filesystem::path persistencePath =
        std::filesystem::temp_directory_path(persistenceError) /
        ("SelfEngineSceneBuilderSelfTest_" +
            std::to_string(m_NextObjectOrdinal) + ".json");
    check(!persistenceError);
    if (!persistenceError) {
        std::filesystem::remove(persistencePath, persistenceError);
        persistenceError.clear();
        const Camera3DState cameraState{
            { 2.25f, 1.40f, 6.75f },
            { -0.18f, -0.12f, -0.98f },
            7.25f,
            0.82f,
            true
        };
        check(SaveToFile(persistencePath, cameraState));

        SceneBuilderDocument persistedDocument;
        std::string persistenceFailure;
        check(ReadSceneBuilderDocument(
            persistencePath,
            persistedDocument,
            persistenceFailure
        ));
        bool persistenceRoundTrip =
            persistedDocument.objects.size() == m_Objects.size() &&
            persistedDocument.lights.size() == m_Lights.size() &&
            persistedDocument.lightsAreExplicit &&
            SameSceneEnvironment(persistedDocument.environment, expectedEnvironment) &&
            persistedDocument.reflectionProbesAreExplicit &&
            persistedDocument.reflectionProbes.size() == 1u &&
            SameReflectionProbeEdit(
                persistedDocument.reflectionProbes.front(),
                expectedReflectionProbe
            ) &&
            persistedDocument.cameraState.has_value() &&
            NearlyEqual(persistedDocument.cameraState->position, cameraState.position) &&
            NearlyEqual(persistedDocument.cameraState->forward, cameraState.forward) &&
            NearlyEqual(persistedDocument.cameraState->distance, cameraState.distance) &&
            NearlyEqual(persistedDocument.cameraState->fovScale, cameraState.fovScale) &&
            persistedDocument.cameraState->freeLookActive == cameraState.freeLookActive;
        for (const SceneBuilderObject& object : m_Objects) {
            SceneBuilderObjectEdit currentEdit{};
            if (!ReadObjectEdit(object.renderIdentity, currentEdit)) {
                persistenceRoundTrip = false;
                break;
            }
            const auto persisted = std::find_if(
                persistedDocument.objects.begin(),
                persistedDocument.objects.end(),
                [&object](const SceneBuilderDocumentObject& candidate) {
                    return candidate.primitive == object.primitive &&
                        candidate.name == object.name;
                }
            );
            if (persisted == persistedDocument.objects.end() ||
                !SameDocumentEdit(persisted->edit, currentEdit)) {
                persistenceRoundTrip = false;
                break;
            }
        }
        for (const SceneBuilderLight& light : m_Lights) {
            SceneLightEdit currentEdit{};
            if (!ReadLightEdit(light.lightIdentity, currentEdit)) {
                persistenceRoundTrip = false;
                break;
            }
            const auto persisted = std::find_if(
                persistedDocument.lights.begin(),
                persistedDocument.lights.end(),
                [&currentEdit](const SceneBuilderDocumentLight& candidate) {
                    return candidate.edit.kind == currentEdit.kind &&
                        candidate.edit.name == currentEdit.name;
                }
            );
            if (persisted == persistedDocument.lights.end() ||
                !SameSceneLightEdit(persisted->edit, currentEdit)) {
                persistenceRoundTrip = false;
                break;
            }
        }
        check(persistenceRoundTrip);

        const std::filesystem::path invalidEnvironmentPath =
            persistencePath.parent_path() /
            ("SelfEngineSceneBuilderInvalidEnvironment_" +
                std::to_string(m_NextObjectOrdinal) + ".json");
        std::filesystem::remove(invalidEnvironmentPath, persistenceError);
        persistenceError.clear();
        const nlohmann::json invalidEnvironmentDocument{
            { "format", kSceneBuilderDocumentFormat },
            { "version", kSceneBuilderDocumentVersion },
            { "objects", nlohmann::json::array() },
            { "environment", { { "iblEnabled", "invalid" } } }
        };
        {
            std::ofstream invalidEnvironmentOutput(
                invalidEnvironmentPath,
                std::ios::trunc
            );
            invalidEnvironmentOutput << invalidEnvironmentDocument.dump(2) << '\n';
            check(static_cast<bool>(invalidEnvironmentOutput));
        }
        SceneBuilderDocument invalidEnvironmentParsed;
        std::string invalidEnvironmentFailure;
        check(!ReadSceneBuilderDocument(
            invalidEnvironmentPath,
            invalidEnvironmentParsed,
            invalidEnvironmentFailure
        ));
        std::filesystem::remove(invalidEnvironmentPath, persistenceError);
        persistenceError.clear();
        std::filesystem::remove(persistencePath, persistenceError);
    }

    // Version 1/2 documents did not own a light array. Their startup key light
    // was implicit, so migration must keep that real Scene3D light rather than
    // converting it into an invisible material fallback.
    std::error_code legacyPersistenceError;
    const std::filesystem::path legacyPersistencePath =
        std::filesystem::temp_directory_path(legacyPersistenceError) /
        ("SelfEngineSceneBuilderLegacy_" +
            std::to_string(m_NextObjectOrdinal) + ".json");
    check(!legacyPersistenceError);
    if (!legacyPersistenceError) {
        std::filesystem::remove(legacyPersistencePath, legacyPersistenceError);
        legacyPersistenceError.clear();
        const nlohmann::json legacyDocument{
            { "format", kSceneBuilderDocumentFormat },
            { "version", kSceneBuilderCameraDocumentVersion },
            { "camera", {
                { "position", nlohmann::json::array({ 0.0f, 1.0f, 5.0f }) },
                { "forward", nlohmann::json::array({ 0.0f, 0.0f, -1.0f }) },
                { "distance", 5.0f },
                { "fovScale", 1.0f },
                { "freeLookActive", false }
            } },
            { "objects", nlohmann::json::array() }
        };
        {
            std::ofstream legacyOutput(legacyPersistencePath, std::ios::trunc);
            legacyOutput << legacyDocument.dump(2) << '\n';
            check(static_cast<bool>(legacyOutput));
        }
        Scene3D legacyScene;
        legacyScene.SetEnvironmentIblEnabled(false);
        legacyScene.SetPrimaryDirectionalLight(
            "Scene Builder Key Light",
            { -0.45f, -0.82f, -0.35f },
            2.35f,
            0.16f,
            0.32f
        );
        SceneBuilder legacyBuilder(
            m_MaterialLibrary,
            m_RenderResources,
            legacyScene
        );
        std::optional<Camera3DState> legacyCameraState;
        check(legacyBuilder.LoadFromFile(legacyPersistencePath, legacyCameraState));
        const SceneBuilderStats legacyStats = legacyBuilder.Stats();
        check(
            legacyStats.lightCount == 1u &&
            legacyStats.lightCounts[static_cast<u32>(SceneLightKind::Directional)] == 1u &&
            legacyScene.PrimaryDirectionalLight() != nullptr &&
            SameSceneEnvironment(
                legacyScene.Environment(),
                SceneEnvironment3D{}
            )
        );
        std::filesystem::remove(legacyPersistencePath, legacyPersistenceError);
    }

    // Version 5 persisted only the IBL gate. Its remaining v6 controls migrate
    // to stable defaults without making legacy documents ambiguous.
    std::error_code v5PersistenceError;
    const std::filesystem::path v5PersistencePath =
        std::filesystem::temp_directory_path(v5PersistenceError) /
        ("SelfEngineSceneBuilderV5_" + std::to_string(m_NextObjectOrdinal) + ".json");
    check(!v5PersistenceError);
    if (!v5PersistenceError) {
        std::filesystem::remove(v5PersistencePath, v5PersistenceError);
        v5PersistenceError.clear();
        const nlohmann::json v5Document{
            { "format", kSceneBuilderDocumentFormat },
            { "version", kSceneBuilderEnvironmentIblDocumentVersion },
            { "camera", {
                { "position", nlohmann::json::array({ 0.0f, 1.0f, 5.0f }) },
                { "forward", nlohmann::json::array({ 0.0f, 0.0f, -1.0f }) },
                { "distance", 5.0f },
                { "fovScale", 1.0f },
                { "freeLookActive", false }
            } },
            { "environment", { { "iblEnabled", false } } },
            { "objects", nlohmann::json::array() },
            { "lights", nlohmann::json::array() }
        };
        {
            std::ofstream v5Output(v5PersistencePath, std::ios::trunc);
            v5Output << v5Document.dump(2) << '\n';
            check(static_cast<bool>(v5Output));
        }
        Scene3D v5Scene;
        SceneBuilder v5Builder(m_MaterialLibrary, m_RenderResources, v5Scene);
        std::optional<Camera3DState> v5CameraState;
        check(v5Builder.LoadFromFile(v5PersistencePath, v5CameraState));
        SceneEnvironment3D expectedV5Environment{};
        expectedV5Environment.iblEnabled = false;
        check(SameSceneEnvironment(v5Scene.Environment(), expectedV5Environment));
        std::filesystem::remove(v5PersistencePath, v5PersistenceError);
    }

    // Version 7 is the user's current persisted format. It must migrate its
    // implicit bootstrap probe into the editable v8 entity when loaded, then
    // write that entity on the next save.
    std::error_code v7PersistenceError;
    const std::filesystem::path v7PersistencePath =
        std::filesystem::temp_directory_path(v7PersistenceError) /
        ("SelfEngineSceneBuilderV7_" + std::to_string(m_NextObjectOrdinal) +
            ".json");
    const std::filesystem::path v7UpgradePath =
        std::filesystem::temp_directory_path(v7PersistenceError) /
        ("SelfEngineSceneBuilderV8_" + std::to_string(m_NextObjectOrdinal) +
            ".json");
    check(!v7PersistenceError);
    if (!v7PersistenceError) {
        std::filesystem::remove(v7PersistencePath, v7PersistenceError);
        v7PersistenceError.clear();
        std::filesystem::remove(v7UpgradePath, v7PersistenceError);
        v7PersistenceError.clear();
        const nlohmann::json v7Document{
            { "format", kSceneBuilderDocumentFormat },
            { "version", kSceneBuilderEnvironmentLightingAssetDocumentVersion },
            { "camera", {
                { "position", nlohmann::json::array({ 0.0f, 1.0f, 5.0f }) },
                { "forward", nlohmann::json::array({ 0.0f, 0.0f, -1.0f }) },
                { "distance", 5.0f },
                { "fovScale", 1.0f },
                { "freeLookActive", false }
            } },
            { "environment", {
                { "iblEnabled", true },
                { "diffuseIntensity", 1.0f },
                { "specularIntensity", 1.0f },
                { "horizonBlend", 0.22f },
                { "skyboxEnabled", false },
                { "skyboxIntensity", 1.0f },
                { "skyboxBlur", 0.0f },
                { "lightingAsset", static_cast<u32>(
                    SceneEnvironmentLightingAsset::RendererDefault
                ) }
            } },
            { "objects", nlohmann::json::array() },
            { "lights", nlohmann::json::array() }
        };
        {
            std::ofstream v7Output(v7PersistencePath, std::ios::trunc);
            v7Output << v7Document.dump(2) << '\n';
            check(static_cast<bool>(v7Output));
        }
        Scene3D v7Scene;
        SceneBuilder v7Builder(m_MaterialLibrary, m_RenderResources, v7Scene);
        std::optional<Camera3DState> v7CameraState;
        check(v7Builder.LoadFromFile(v7PersistencePath, v7CameraState));
        SceneBuilderReflectionProbeEdit migratedV7Probe{};
        check(v7Builder.ReadReflectionProbeEdit(migratedV7Probe) &&
            DocumentReflectionProbeEditIsInRange(migratedV7Probe) &&
            migratedV7Probe.captureSource ==
                ReflectionProbeCaptureSource::CapturedScene &&
            migratedV7Probe.refreshPolicy == ReflectionProbeRefreshPolicy::Static);
        check(v7CameraState.has_value() &&
            v7Builder.SaveToFile(v7UpgradePath, *v7CameraState));
        nlohmann::json upgradedV7Document;
        {
            std::ifstream upgradedV7Input(v7UpgradePath);
            upgradedV7Input >> upgradedV7Document;
            check(static_cast<bool>(upgradedV7Input));
        }
        check(upgradedV7Document.value("version", 0) ==
                kSceneBuilderDocumentVersion &&
            upgradedV7Document.contains("reflectionProbes") &&
            upgradedV7Document["reflectionProbes"].is_array() &&
            upgradedV7Document["reflectionProbes"].size() == 1u);
        std::filesystem::remove(v7PersistencePath, v7PersistenceError);
        v7PersistenceError.clear();
        std::filesystem::remove(v7UpgradePath, v7PersistenceError);
    }

    // A material's legacy custom parameters must never manufacture a global
    // light. Once the authored directional is gone, only the retained local
    // lights may reach the frame-light buffer.
    check(DestroyLight(directionalLight));
    SceneLightEdit missingDirectional{};
    check(!ReadLightEdit(directionalLight, missingDirectional));
    check(m_Lights.size() == baselineLightCount + 2u);

    m_SelfTestPassed = allChecksPassed ? 1u : 0u;
    return m_SelfTestPassed != 0u;
}
#endif

void SceneBuilder::RefreshMaterials() {
    if (!m_MaterialsChanged) {
        return;
    }

    m_MaterialsChanged();
    ++m_MaterialDescriptorRefreshCount;
}

}
