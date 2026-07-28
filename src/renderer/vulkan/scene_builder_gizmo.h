#pragma once

#include "core.h"

#include <glm/glm.hpp>

struct ImFont;

namespace se {

class Camera3D;
class SceneBuilder;
class Window;

enum class SceneBuilderGizmoMode : u32 {
    Translate = 0u,
    Rotate = 1u,
    Scale = 2u
};

// UI-only transform control for the selected Scene Builder object. It owns no
// scene data; all mutations return through SceneBuilder.
class SceneBuilderGizmo {
public:
    void Draw(SceneBuilder& builder, const Camera3D& camera);
    void SetLightIconFont(ImFont* font);
    static bool SelectLightIconAtCursor(
        SceneBuilder& builder,
        const Camera3D& camera,
        const Window& window
    );
    void Reset();
    bool CapturesMouse() const;
    static bool BlocksCameraInput(
        bool imguiWantsMouse,
        bool rightMouseLookRequested,
        bool gizmoCapturesMouse
    );
    SceneBuilderGizmoMode Mode() const;
    void SetMode(SceneBuilderGizmoMode mode);
    void SetModeFromShortcut(SceneBuilderGizmoMode mode);
    u32 ShortcutModeSwitchCount() const;
    u32 ShortcutModeMask() const;
    static glm::vec2 DirectionalLightRotationDegrees(glm::vec3 direction);
    static glm::vec3 DirectionalLightDirectionFromRotationDegrees(
        glm::vec2 rotationDegrees
    );
#if !defined(NDEBUG)
    static bool DebugValidateSceneBuilderTrsRoundTrip();
    static bool DebugValidateCameraInputArbitration();
#endif

private:
    SceneBuilderGizmoMode m_Mode = SceneBuilderGizmoMode::Translate;
    ImFont* m_LightIconFont = nullptr;
    u32 m_ShortcutModeSwitchCount = 0;
    u32 m_ShortcutModeMask = 0;
    bool m_CapturesMouse = false;
    bool m_CapturedLeftDrag = false;
};

}
