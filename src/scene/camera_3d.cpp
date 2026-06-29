#include "scene/camera_3d.h"

#include "platform/window.h"

#include <algorithm>
#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace se {

Camera3D::Camera3D() {
    UpdateOrbitPosition();
}

Camera3D::Camera3D(Controls controls)
    : m_Controls(controls) {
    UpdateOrbitPosition();
}

void Camera3D::Update(Window& window, f32 deltaSeconds, bool inputBlocked) {
    UpdateOrbit(window, inputBlocked);
    UpdateFreeLook(window, inputBlocked, deltaSeconds);
}

void Camera3D::SetOrbit(f32 yawRadians, f32 pitchRadians, f32 distance) {
    m_Yaw = yawRadians;
    m_Pitch = std::clamp(
        pitchRadians,
        m_Controls.minOrbitPitch,
        m_Controls.maxOrbitPitch
    );
    m_Distance = ClampDistance(distance);
    UpdateOrbitPosition();
}

void Camera3D::SetPose(glm::vec3 position, glm::vec3 forward) {
    if (glm::length(forward) <= 0.0001f) {
        return;
    }

    m_Position = position;
    m_Forward = glm::normalize(forward);
    m_Distance = glm::length(position);
    m_Yaw = std::atan2(-m_Forward.z, -m_Forward.x);
    m_Pitch = std::asin(std::clamp(-m_Forward.y, -1.0f, 1.0f));
    m_FreeLookActive = true;
}

void Camera3D::SetDistance(f32 distance) {
    m_Distance = ClampDistance(distance);
    if (!m_FreeLookActive) {
        UpdateOrbitPosition();
    }
}

void Camera3D::SetFovScale(f32 fovScale) {
    m_FovScale = ClampFovScale(fovScale);
}

void Camera3D::SetMoveSpeed(f32 moveSpeed) {
    m_Controls.moveSpeed = std::max(moveSpeed, 0.05f);
}

void Camera3D::SetClipPlanes(f32 nearClip, f32 farClip) {
    m_Controls.nearClip = std::max(nearClip, 0.001f);
    m_Controls.farClip = std::max(farClip, m_Controls.nearClip + 1.0f);
}

void Camera3D::ResetOrbit() {
    m_Yaw = 3.14159265f;
    m_Pitch = 0.16f;
    m_Distance = 15.0f;
    m_FovScale = 1.0f;
    UpdateOrbitPosition();
}

const glm::vec3& Camera3D::Position() const {
    return m_Position;
}

const glm::vec3& Camera3D::Forward() const {
    return m_Forward;
}

f32 Camera3D::Distance() const {
    return m_Distance;
}

f32 Camera3D::FovScale() const {
    return m_FovScale;
}

f32 Camera3D::MoveSpeed() const {
    return m_Controls.moveSpeed;
}

f32 Camera3D::NearClip() const {
    return m_Controls.nearClip;
}

f32 Camera3D::FarClip() const {
    return m_Controls.farClip;
}

bool Camera3D::FreeLookActive() const {
    return m_FreeLookActive;
}

Camera3DState Camera3D::State() const {
    return Camera3DState{
        m_Position,
        m_Forward,
        m_Distance,
        m_FovScale,
        m_FreeLookActive
    };
}

glm::mat4 Camera3D::ViewMatrix() const {
    return glm::lookAt(
        m_Position,
        m_Position + m_Forward,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
}

glm::mat4 Camera3D::ProjectionMatrix(f32 aspectRatio) const {
    glm::mat4 projection = glm::perspective(
        VerticalFovRadians(),
        aspectRatio,
        std::max(m_Controls.nearClip, 0.001f),
        std::max(m_Controls.farClip, m_Controls.nearClip + 1.0f)
    );
    projection[1][1] *= -1.0f;

    return projection;
}

f32 Camera3D::VerticalFovRadians() const {
    return 2.0f * std::atan(ClampFovScale(m_FovScale) * 0.5f);
}

glm::vec3 Camera3D::DirectionFromAngles() const {
    return glm::normalize(glm::vec3(
        -std::cos(m_Pitch) * std::cos(m_Yaw),
        -std::sin(m_Pitch),
        -std::cos(m_Pitch) * std::sin(m_Yaw)
    ));
}

void Camera3D::UpdateOrbitPosition() {
    m_Position = glm::vec3(
        std::cos(m_Pitch) * std::cos(m_Yaw),
        std::sin(m_Pitch),
        std::cos(m_Pitch) * std::sin(m_Yaw)
    ) * m_Distance;
    m_Forward = glm::normalize(-m_Position);
    m_FreeLookActive = false;
}

void Camera3D::UpdateOrbit(Window& window, bool inputBlocked) {
    const bool leftMouseDown =
        window.IsLeftMouseDown() &&
        !window.IsRightMouseDown() &&
        !inputBlocked;
    const std::array<f64, 2> cursor = window.CursorPosition();

    if (!leftMouseDown) {
        m_OrbitDragging = false;
        m_PreviousOrbitCursor = cursor;
        return;
    }

    if (!m_OrbitDragging) {
        m_OrbitDragging = true;
        m_PreviousOrbitCursor = cursor;
        return;
    }

    const f32 deltaX = static_cast<f32>(cursor[0] - m_PreviousOrbitCursor[0]);
    const f32 deltaY = static_cast<f32>(cursor[1] - m_PreviousOrbitCursor[1]);
    m_PreviousOrbitCursor = cursor;

    m_Yaw += deltaX * m_Controls.orbitSensitivity;
    m_Pitch = std::clamp(
        m_Pitch + deltaY * m_Controls.orbitSensitivity,
        m_Controls.minOrbitPitch,
        m_Controls.maxOrbitPitch
    );

    UpdateOrbitPosition();
}

void Camera3D::UpdateFreeLook(Window& window, bool inputBlocked, f32 deltaSeconds) {
    const bool rightMouseDown = window.IsRightMouseDown() && !inputBlocked;
    const std::array<f64, 2> cursor = window.CursorPosition();

    if (!rightMouseDown) {
        if (m_CursorCaptured) {
            window.SetCursorCaptured(false);
            m_CursorCaptured = false;
        }
        m_LookDragging = false;
        m_PreviousLookCursor = cursor;
        return;
    }

    if (!m_CursorCaptured) {
        window.SetCursorCaptured(true);
        m_CursorCaptured = true;
    }

    if (!m_LookDragging) {
        m_LookDragging = true;
        m_FreeLookActive = true;
        m_Yaw = std::atan2(-m_Forward.z, -m_Forward.x);
        m_Pitch = std::asin(std::clamp(-m_Forward.y, -1.0f, 1.0f));
        m_PreviousLookCursor = cursor;
        return;
    }

    const f32 deltaX = static_cast<f32>(cursor[0] - m_PreviousLookCursor[0]);
    const f32 deltaY = static_cast<f32>(cursor[1] - m_PreviousLookCursor[1]);
    m_PreviousLookCursor = cursor;

    m_Yaw += deltaX * m_Controls.lookSensitivity;
    m_Pitch = std::clamp(
        m_Pitch + deltaY * m_Controls.lookSensitivity,
        m_Controls.minLookPitch,
        m_Controls.maxLookPitch
    );

    m_Forward = DirectionFromAngles();
    const glm::vec3 right = glm::normalize(
        glm::cross(m_Forward, glm::vec3(0.0f, 1.0f, 0.0f))
    );
    glm::vec3 movement{ 0.0f };

    if (window.IsKeyDown(GLFW_KEY_W)) {
        movement += m_Forward;
    }
    if (window.IsKeyDown(GLFW_KEY_S)) {
        movement -= m_Forward;
    }
    if (window.IsKeyDown(GLFW_KEY_A)) {
        movement -= right;
    }
    if (window.IsKeyDown(GLFW_KEY_D)) {
        movement += right;
    }

    if (glm::length(movement) > 0.001f) {
        m_Position += glm::normalize(movement) *
            m_Controls.moveSpeed *
            std::max(deltaSeconds, 0.0f);
        m_Distance = glm::length(m_Position);
    }
}

f32 Camera3D::ClampDistance(f32 distance) const {
    return std::clamp(
        distance,
        m_Controls.minDistance,
        m_Controls.maxDistance
    );
}

f32 Camera3D::ClampFovScale(f32 fovScale) const {
    return std::clamp(
        fovScale,
        m_Controls.minFovScale,
        m_Controls.maxFovScale
    );
}

}
