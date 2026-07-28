#include "renderer/vulkan/scene_builder_gizmo.h"

#include "scene/camera_3d.h"
#include "scene/renderable_3d.h"
#include "scene/scene_builder.h"
#include "scene/transform.h"
#include "platform/window.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace se {

namespace {

constexpr f32 kLightIconFontPixels = 26.0f;
constexpr f32 kLightIconHitRadiusPixels = 18.0f;
constexpr ImWchar kDirectionalLightIcon = 0xe430;
constexpr ImWchar kPointLightIcon = 0xe0f0;
constexpr ImWchar kSpotLightIcon = 0xe25f;
constexpr ImWchar kRectLightIcon = 0xe3c3;

ImWchar LightIconGlyph(SceneLightKind kind) {
    switch (kind) {
        case SceneLightKind::Directional:
            return kDirectionalLightIcon;
        case SceneLightKind::Point:
            return kPointLightIcon;
        case SceneLightKind::Spot:
            return kSpotLightIcon;
        case SceneLightKind::Rect:
            return kRectLightIcon;
    }

    return kPointLightIcon;
}

std::array<char, 4> Utf8Glyph(ImWchar glyph) {
    const u32 codePoint = static_cast<u32>(glyph);
    return {
        static_cast<char>(0xe0u | (codePoint >> 12u)),
        static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu)),
        static_cast<char>(0x80u | (codePoint & 0x3fu)),
        '\0'
    };
}

bool ProjectLightIcon(
    const SceneLightEdit& light,
    const Camera3D& camera,
    const glm::vec2& viewportExtent,
    ImVec2& screenPosition,
    f32& depth
) {
    if (viewportExtent.x <= 0.0f || viewportExtent.y <= 0.0f) {
        return false;
    }

    const f32 aspectRatio = viewportExtent.x / viewportExtent.y;
    const glm::vec4 clip =
        camera.ProjectionMatrix(aspectRatio) * camera.ViewMatrix() *
        glm::vec4(light.position, 1.0f);
    if (clip.w <= 0.00001f) {
        return false;
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.x < -1.05f || ndc.x > 1.05f ||
        ndc.y < -1.05f || ndc.y > 1.05f ||
        ndc.z < 0.0f || ndc.z > 1.0f) {
        return false;
    }

    screenPosition = {
        (ndc.x * 0.5f + 0.5f) * viewportExtent.x,
        (ndc.y * 0.5f + 0.5f) * viewportExtent.y
    };
    depth = ndc.z;
    return true;
}

void DrawLightIcons(
    SceneBuilder& builder,
    const Camera3D& camera,
    ImFont* iconFont
) {
    if (iconFont == nullptr) {
        builder.SetLightIconOverlayCount(0u);
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 viewportExtent{ io.DisplaySize.x, io.DisplaySize.y };
    const ImVec2 mousePosition = ImGui::GetMousePos();
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const u64 selectedLightIdentity = builder.SelectedLightIdentity();
    u32 drawnCount = 0u;

    for (const SceneBuilderLight& light : builder.Lights()) {
        SceneLightEdit edit{};
        if (!builder.ReadLightEdit(light.lightIdentity, edit)) {
            continue;
        }

        ImVec2 screenPosition{};
        f32 depth = 0.0f;
        if (!ProjectLightIcon(edit, camera, viewportExtent, screenPosition, depth)) {
            continue;
        }

        const bool selected = light.lightIdentity == selectedLightIdentity;
        const f32 distanceToMouse = std::sqrt(
            (mousePosition.x - screenPosition.x) * (mousePosition.x - screenPosition.x) +
            (mousePosition.y - screenPosition.y) * (mousePosition.y - screenPosition.y)
        );
        const bool hovered = distanceToMouse <= kLightIconHitRadiusPixels;
        const f32 alpha = edit.enabled ? 1.0f : 0.34f;
        const ImU32 backgroundColor = IM_COL32(10, 13, 20, selected ? 232 : 188);
        const ImU32 outlineColor = selected
            ? IM_COL32(255, 255, 255, 255)
            : hovered
                ? IM_COL32(255, 224, 112, 255)
                : IM_COL32(120, 156, 204, 215);
        const ImU32 iconColor = ImGui::GetColorU32(ImVec4{
            glm::clamp(edit.color.r, 0.16f, 1.0f),
            glm::clamp(edit.color.g, 0.16f, 1.0f),
            glm::clamp(edit.color.b, 0.16f, 1.0f),
            alpha
        });
        const std::array<char, 4> glyph = Utf8Glyph(LightIconGlyph(light.kind));
        const ImVec2 glyphSize = iconFont->CalcTextSizeA(
            kLightIconFontPixels,
            1000.0f,
            0.0f,
            glyph.data()
        );

        drawList->AddCircleFilled(screenPosition, 15.0f, backgroundColor, 20);
        drawList->AddCircle(
            screenPosition,
            selected ? 17.0f : 15.0f,
            outlineColor,
            20,
            selected ? 2.0f : 1.25f
        );
        drawList->AddText(
            iconFont,
            kLightIconFontPixels,
            {
                screenPosition.x - glyphSize.x * 0.5f,
                screenPosition.y - glyphSize.y * 0.5f
            },
            iconColor,
            glyph.data()
        );
        ++drawnCount;
    }

    builder.SetLightIconOverlayCount(drawnCount);
}

ImGuizmo::OPERATION OperationFor(SceneBuilderGizmoMode mode) {
    switch (mode) {
        case SceneBuilderGizmoMode::Translate:
            return ImGuizmo::TRANSLATE;
        case SceneBuilderGizmoMode::Rotate:
            return ImGuizmo::ROTATE;
        case SceneBuilderGizmoMode::Scale:
            return ImGuizmo::SCALE;
    }

    return ImGuizmo::TRANSLATE;
}

ImGuizmo::MODE SpaceFor(
    SceneBuilderGizmoMode mode,
    bool directionalLight
) {
    if (directionalLight && mode == SceneBuilderGizmoMode::Rotate) {
        return ImGuizmo::WORLD;
    }
    return mode == SceneBuilderGizmoMode::Translate
        ? ImGuizmo::WORLD
        : ImGuizmo::LOCAL;
}

glm::vec3 ExtractSceneBuilderRotationDegrees(const glm::mat4& source) {
    glm::mat4 rotation = source;
    for (u32 axis = 0u; axis < 3u; ++axis) {
        const f32 axisLength = glm::length(glm::vec3(rotation[axis]));
        if (axisLength <= 0.000001f) {
            return {};
        }
        rotation[axis] /= axisLength;
    }

    f32 xRadians = 0.0f;
    f32 yRadians = 0.0f;
    f32 zRadians = 0.0f;
    glm::extractEulerAngleXYZ(rotation, xRadians, yRadians, zRadians);
    return glm::degrees(glm::vec3{ xRadians, yRadians, zRadians });
}

glm::vec3 NormalizedLightDirection(glm::vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        return { 0.0f, -1.0f, 0.0f };
    }

    return glm::normalize(direction);
}

glm::mat4 LightTransformMatrix(const SceneLightEdit& light) {
    glm::mat4 model{ 1.0f };
    const glm::vec3 forward = NormalizedLightDirection(light.direction);
    const glm::vec3 referenceUp = std::abs(glm::dot(forward, glm::vec3{ 0.0f, 1.0f, 0.0f }))
        > 0.99f
            ? glm::vec3{ 0.0f, 0.0f, 1.0f }
            : glm::vec3{ 0.0f, 1.0f, 0.0f };
    const glm::vec3 right = glm::normalize(glm::cross(forward, referenceUp));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    model[0] = glm::vec4(right, 0.0f);
    model[1] = glm::vec4(up, 0.0f);
    // The gizmo's local -Z axis is the light-facing direction.
    model[2] = glm::vec4(-forward, 0.0f);
    model[3] = glm::vec4(light.position, 1.0f);
    return model;
}

glm::vec3 LightDirectionFromTransform(const glm::mat4& model) {
    return NormalizedLightDirection(-glm::vec3(model[2]));
}

glm::mat4 ReflectionProbeTransformMatrix(
    const SceneBuilderReflectionProbeEdit& probe
) {
    glm::mat4 model{ 1.0f };
    const glm::vec3 dimensions = glm::max(
        probe.boxExtents * 2.0f,
        glm::vec3(0.02f)
    );
    model[0][0] = dimensions.x;
    model[1][1] = dimensions.y;
    model[2][2] = dimensions.z;
    model[3] = glm::vec4(probe.boxCenter, 1.0f);
    return model;
}

bool LightSupportsOperation(
    SceneLightKind kind,
    SceneBuilderGizmoMode mode
) {
    switch (kind) {
        case SceneLightKind::Directional:
            return mode != SceneBuilderGizmoMode::Scale;
        case SceneLightKind::Point:
            return mode == SceneBuilderGizmoMode::Translate;
        case SceneLightKind::Spot:
        case SceneLightKind::Rect:
            return mode != SceneBuilderGizmoMode::Scale;
    }

    return false;
}

#if !defined(NDEBUG)
f32 MaxMatrixDifference(const glm::mat4& left, const glm::mat4& right) {
    f32 maximum = 0.0f;
    for (u32 column = 0u; column < 4u; ++column) {
        for (u32 row = 0u; row < 4u; ++row) {
            maximum = std::max(
                maximum,
                std::abs(left[column][row] - right[column][row])
            );
        }
    }
    return maximum;
}

bool SceneBuilderTrsRoundTripIsStable(
    const glm::vec3& rotationDegrees,
    const glm::vec3& scale
) {
    Transform3D source{};
    source.SetPosition({ 0.0f, -0.94f, 0.0f });
    source.SetRotationDegrees(rotationDegrees);
    source.SetScale(scale);
    const glm::mat4 matrix = source.Matrix();

    std::array<f32, 3> position{};
    std::array<f32, 3> imGuizmoRotation{};
    std::array<f32, 3> extractedScale{};
    ImGuizmo::DecomposeMatrixToComponents(
        glm::value_ptr(matrix),
        position.data(),
        imGuizmoRotation.data(),
        extractedScale.data()
    );

    Transform3D rebuilt{};
    rebuilt.SetPosition({ position[0], position[1], position[2] });
    rebuilt.SetRotationDegrees(ExtractSceneBuilderRotationDegrees(matrix));
    rebuilt.SetScale({ extractedScale[0], extractedScale[1], extractedScale[2] });
    return MaxMatrixDifference(matrix, rebuilt.Matrix()) <= 0.0001f;
}

bool LightDirectionRoundTripIsStable(glm::vec3 direction) {
    SceneLightEdit light{};
    light.kind = SceneLightKind::Spot;
    light.direction = direction;
    return glm::length(
        LightDirectionFromTransform(LightTransformMatrix(light)) -
        NormalizedLightDirection(direction)
    ) <= 0.0001f;
}

bool LightRotationRoundTripIsStable(glm::vec3 direction) {
    return glm::length(
        SceneBuilderGizmo::DirectionalLightDirectionFromRotationDegrees(
            SceneBuilderGizmo::DirectionalLightRotationDegrees(direction)
        ) -
        NormalizedLightDirection(direction)
    ) <= 0.0001f;
}

bool DirectionalLightRotationUsesWorldSpace() {
    return SpaceFor(SceneBuilderGizmoMode::Rotate, true) == ImGuizmo::WORLD;
}
#endif

}

void SceneBuilderGizmo::Draw(SceneBuilder& builder, const Camera3D& camera) {
    ImGuizmo::BeginFrame();
    builder.SyncSelectionFromScene();
    DrawLightIcons(builder, camera, m_LightIconFont);

    const SceneBuilderObject* selected = builder.FindObject(builder.SelectedIdentity());
    const i32 selectedReflectionProbeIndex =
        builder.SelectedReflectionProbeIndex();
    SceneBuilderReflectionProbeEdit selectedReflectionProbe{};
    const bool editingReflectionProbe = selectedReflectionProbeIndex >= 0 &&
        builder.ReadReflectionProbeEdit(
            static_cast<u32>(selectedReflectionProbeIndex),
            selectedReflectionProbe
        );
    const u64 selectedLightIdentity = builder.SelectedLightIdentity();
    SceneLightEdit selectedLight{};
    const bool editingLight = !editingReflectionProbe && selected == nullptr &&
        selectedLightIdentity != 0u &&
        builder.ReadLightEdit(selectedLightIdentity, selectedLight);
    const ImGuiIO& io = ImGui::GetIO();
    if ((!editingReflectionProbe && !editingLight &&
            (selected == nullptr || selected->renderable == nullptr)) ||
        io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
        Reset();
        return;
    }
    if (editingReflectionProbe && m_Mode == SceneBuilderGizmoMode::Rotate) {
        m_Mode = SceneBuilderGizmoMode::Translate;
    }
    if (editingLight && !LightSupportsOperation(selectedLight.kind, m_Mode)) {
        m_Mode = SceneBuilderGizmoMode::Translate;
    }

    const bool wasDragging = m_CapturedLeftDrag;
    const f32 aspectRatio = io.DisplaySize.x / io.DisplaySize.y;
    const glm::mat4 view = camera.ViewMatrix();
    glm::mat4 projection = camera.ProjectionMatrix(aspectRatio);
    // ImGuizmo maps clip-space Y into ImGui's top-left coordinate system. The
    // render camera is Vulkan-flipped, so undo that presentation-only flip.
    projection[1][1] *= -1.0f;
    glm::mat4 model = editingReflectionProbe
        ? ReflectionProbeTransformMatrix(selectedReflectionProbe)
        : editingLight
            ? LightTransformMatrix(selectedLight)
            : selected->renderable->Transform().Matrix();

    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
    ImGuizmo::SetOrthographic(false);
    const ImGuizmo::OPERATION operation =
        editingLight && selectedLight.kind == SceneLightKind::Directional &&
                m_Mode == SceneBuilderGizmoMode::Rotate
            ? ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y
            : OperationFor(m_Mode);
    const bool transformed = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(projection),
        operation,
        editingReflectionProbe
            ? (m_Mode == SceneBuilderGizmoMode::Translate
                ? ImGuizmo::WORLD
                : ImGuizmo::LOCAL)
            : SpaceFor(
                m_Mode,
                editingLight && selectedLight.kind == SceneLightKind::Directional
            ),
        glm::value_ptr(model)
    );

    const bool usingGizmo = ImGuizmo::IsUsing();
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && usingGizmo) {
        m_CapturedLeftDrag = true;
    }
    m_CapturesMouse = wasDragging || ImGuizmo::IsOver() || usingGizmo;

    if (transformed) {
        if (editingReflectionProbe) {
            if (m_Mode == SceneBuilderGizmoMode::Translate) {
                const glm::vec3 proxyCenter = glm::vec3(model[3]);
                const glm::vec3 delta = proxyCenter -
                    selectedReflectionProbe.boxCenter;
                selectedReflectionProbe.boxCenter = proxyCenter;
                selectedReflectionProbe.center += delta;
            } else if (m_Mode == SceneBuilderGizmoMode::Scale) {
                selectedReflectionProbe.boxExtents = glm::max(
                    glm::vec3{
                        glm::length(glm::vec3(model[0])),
                        glm::length(glm::vec3(model[1])),
                        glm::length(glm::vec3(model[2]))
                    } * 0.5f,
                    glm::vec3(0.01f)
                );
                selectedReflectionProbe.radius = glm::length(
                    selectedReflectionProbe.boxExtents
                );
            }
            builder.ApplyReflectionProbeEdit(
                static_cast<u32>(selectedReflectionProbeIndex),
                selectedReflectionProbe
            );
        } else if (editingLight) {
            if (m_Mode == SceneBuilderGizmoMode::Translate) {
                selectedLight.position = glm::vec3(model[3]);
            } else if (m_Mode == SceneBuilderGizmoMode::Rotate) {
                selectedLight.direction = LightDirectionFromTransform(model);
            }
            builder.ApplyLightEdit(selectedLightIdentity, selectedLight);
        } else {
            SceneBuilderObjectEdit edit{};
            if (builder.ReadObjectEdit(selected->renderIdentity, edit)) {
            std::array<f32, 3> position{};
            std::array<f32, 3> rotationDegrees{};
            std::array<f32, 3> scale{};
            ImGuizmo::DecomposeMatrixToComponents(
                glm::value_ptr(model),
                position.data(),
                rotationDegrees.data(),
                scale.data()
            );

            switch (m_Mode) {
                case SceneBuilderGizmoMode::Translate:
                    edit.position = glm::vec3{
                        position[0], position[1], position[2]
                    };
                    break;
                case SceneBuilderGizmoMode::Rotate:
                    edit.rotationDegrees = ExtractSceneBuilderRotationDegrees(model);
                    break;
                case SceneBuilderGizmoMode::Scale:
                    edit.scale = glm::vec3{ scale[0], scale[1], scale[2] };
                    break;
            }
            builder.ApplyObjectEdit(selected->renderIdentity, edit);
            }
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_CapturedLeftDrag = false;
    }
}

void SceneBuilderGizmo::SetLightIconFont(ImFont* font) {
    m_LightIconFont = font;
}

bool SceneBuilderGizmo::SelectLightIconAtCursor(
    SceneBuilder& builder,
    const Camera3D& camera,
    const Window& window
) {
    const std::array<int, 2> windowSize = window.WindowSize();
    const glm::vec2 viewportExtent{
        static_cast<f32>(std::max(windowSize[0], 1)),
        static_cast<f32>(std::max(windowSize[1], 1))
    };
    const std::array<f64, 2> cursorPosition = window.CursorPosition();
    const ImVec2 cursor{
        static_cast<f32>(cursorPosition[0]),
        static_cast<f32>(cursorPosition[1])
    };
    u64 nearestLightIdentity = 0u;
    f32 nearestDepth = std::numeric_limits<f32>::max();

    for (const SceneBuilderLight& light : builder.Lights()) {
        SceneLightEdit edit{};
        if (!builder.ReadLightEdit(light.lightIdentity, edit)) {
            continue;
        }

        ImVec2 screenPosition{};
        f32 depth = 0.0f;
        if (!ProjectLightIcon(edit, camera, viewportExtent, screenPosition, depth)) {
            continue;
        }

        const f32 offsetX = cursor.x - screenPosition.x;
        const f32 offsetY = cursor.y - screenPosition.y;
        if (offsetX * offsetX + offsetY * offsetY >
            kLightIconHitRadiusPixels * kLightIconHitRadiusPixels) {
            continue;
        }
        if (depth < nearestDepth) {
            nearestDepth = depth;
            nearestLightIdentity = light.lightIdentity;
        }
    }

    const bool selected = nearestLightIdentity != 0u &&
        builder.SelectLight(nearestLightIdentity);
    builder.RecordLightIconPick(selected);
    return selected;
}

void SceneBuilderGizmo::Reset() {
    m_CapturesMouse = false;
    m_CapturedLeftDrag = false;
}

bool SceneBuilderGizmo::CapturesMouse() const {
    return m_CapturesMouse;
}

bool SceneBuilderGizmo::BlocksCameraInput(
    bool imguiWantsMouse,
    bool rightMouseLookRequested,
    bool gizmoCapturesMouse
) {
    // ImGuizmo requests ImGui capture while merely hovered. Its handles only
    // mutate through left-drag, so right-mouse free look can safely win here.
    return imguiWantsMouse &&
        !(rightMouseLookRequested && gizmoCapturesMouse);
}

SceneBuilderGizmoMode SceneBuilderGizmo::Mode() const {
    return m_Mode;
}

void SceneBuilderGizmo::SetMode(SceneBuilderGizmoMode mode) {
    m_Mode = mode;
}

void SceneBuilderGizmo::SetModeFromShortcut(SceneBuilderGizmoMode mode) {
    m_Mode = mode;
    ++m_ShortcutModeSwitchCount;
    const u32 modeIndex = static_cast<u32>(mode);
    if (modeIndex <= static_cast<u32>(SceneBuilderGizmoMode::Scale)) {
        m_ShortcutModeMask |= 1u << modeIndex;
    }
}

u32 SceneBuilderGizmo::ShortcutModeSwitchCount() const {
    return m_ShortcutModeSwitchCount;
}

u32 SceneBuilderGizmo::ShortcutModeMask() const {
    return m_ShortcutModeMask;
}

glm::vec2 SceneBuilderGizmo::DirectionalLightRotationDegrees(glm::vec3 direction) {
    const glm::vec3 normalizedDirection = NormalizedLightDirection(direction);
    return glm::degrees(glm::vec2{
        std::atan2(normalizedDirection.x, -normalizedDirection.z),
        std::asin(glm::clamp(normalizedDirection.y, -1.0f, 1.0f))
    });
}

glm::vec3 SceneBuilderGizmo::DirectionalLightDirectionFromRotationDegrees(
    glm::vec2 rotationDegrees
) {
    const glm::vec2 rotationRadians = glm::radians(rotationDegrees);
    const f32 cosinePitch = std::cos(rotationRadians.y);
    return NormalizedLightDirection({
        std::sin(rotationRadians.x) * cosinePitch,
        std::sin(rotationRadians.y),
        -std::cos(rotationRadians.x) * cosinePitch
    });
}

#if !defined(NDEBUG)
bool SceneBuilderGizmo::DebugValidateSceneBuilderTrsRoundTrip() {
    return SceneBuilderTrsRoundTripIsStable(
               { 0.0f, 0.0f, 0.0f },
               { 1.0f, 1.0f, 1.0f }
           ) &&
        SceneBuilderTrsRoundTripIsStable(
            { 23.0f, -41.0f, 17.0f },
            { 1.0f, 1.0f, 1.0f }
        ) &&
        SceneBuilderTrsRoundTripIsStable(
            { 23.0f, -41.0f, 17.0f },
            { 4.0f, 1.0f, 4.0f }
        ) &&
        LightDirectionRoundTripIsStable({ 0.0f, -1.0f, 0.0f }) &&
        LightDirectionRoundTripIsStable({ -0.36f, -0.82f, 0.44f }) &&
        LightRotationRoundTripIsStable({ 0.0f, -1.0f, 0.0f }) &&
        LightRotationRoundTripIsStable({ -0.36f, -0.82f, 0.44f }) &&
        DirectionalLightRotationUsesWorldSpace();
}

bool SceneBuilderGizmo::DebugValidateCameraInputArbitration() {
    return BlocksCameraInput(true, false, true) &&
        !BlocksCameraInput(true, true, true) &&
        BlocksCameraInput(true, true, false) &&
        !BlocksCameraInput(false, true, true);
}
#endif

}
