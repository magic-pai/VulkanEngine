#pragma once

#include "core.h"

#include <array>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace se {

class Window;

struct Camera3DState {
    glm::vec3 position{ 0.0f, 0.0f, 15.0f };
    glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
    f32 distance = 15.0f;
    f32 fovScale = 1.0f;
    bool freeLookActive = false;
};

class Camera3D {
public:
    struct Controls {
        f32 orbitSensitivity = 0.006f;
        f32 lookSensitivity = 0.004f;
        f32 moveSpeed = 9.5f;
        f32 minOrbitPitch = -1.35f;
        f32 maxOrbitPitch = 1.35f;
        f32 minLookPitch = -1.45f;
        f32 maxLookPitch = 1.45f;
        f32 minDistance = 3.0f;
        f32 maxDistance = 35.0f;
        f32 minFovScale = 0.35f;
        f32 maxFovScale = 2.3f;
        f32 nearClip = 0.01f;
        f32 farClip = 1000.0f;
    };

    Camera3D();
    explicit Camera3D(Controls controls);

    void Update(Window& window, f32 deltaSeconds, bool inputBlocked = false);
    void SetOrbit(f32 yawRadians, f32 pitchRadians, f32 distance);
    void SetPose(glm::vec3 position, glm::vec3 forward);
    void SetDistance(f32 distance);
    void SetFovScale(f32 fovScale);
    void SetMoveSpeed(f32 moveSpeed);
    void SetClipPlanes(f32 nearClip, f32 farClip);
    void ResetOrbit();

    const glm::vec3& Position() const;
    const glm::vec3& Forward() const;
    f32 Distance() const;
    f32 FovScale() const;
    f32 MoveSpeed() const;
    f32 NearClip() const;
    f32 FarClip() const;
    bool FreeLookActive() const;
    Camera3DState State() const;
    glm::mat4 ViewMatrix() const;
    glm::mat4 ProjectionMatrix(f32 aspectRatio) const;

private:
    f32 VerticalFovRadians() const;
    glm::vec3 DirectionFromAngles() const;
    void UpdateOrbitPosition();
    void UpdateOrbit(Window& window, bool inputBlocked);
    void UpdateFreeLook(Window& window, bool inputBlocked, f32 deltaSeconds);
    f32 ClampDistance(f32 distance) const;
    f32 ClampFovScale(f32 fovScale) const;

private:
    Controls m_Controls{};
    f32 m_Yaw = 3.14159265f;
    f32 m_Pitch = 0.16f;
    f32 m_Distance = 15.0f;
    f32 m_FovScale = 1.0f;
    glm::vec3 m_Position{ 0.0f };
    glm::vec3 m_Forward{ 0.0f, 0.0f, -1.0f };
    bool m_OrbitDragging = false;
    bool m_LookDragging = false;
    bool m_FreeLookActive = false;
    bool m_CursorCaptured = false;
    std::array<f64, 2> m_PreviousOrbitCursor{};
    std::array<f64, 2> m_PreviousLookCursor{};
};

}
