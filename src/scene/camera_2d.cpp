#include "scene/camera_2d.h"

#include <algorithm>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace se {

namespace {

constexpr f32 kMinZoom = 0.05f;
constexpr f32 kViewDistance = 10.0f;
constexpr f32 kNearPlane = 0.01f;
constexpr f32 kFarPlane = 100.0f;

}

glm::vec2& Camera2D::Position() {
    return m_Position;
}

const glm::vec2& Camera2D::Position() const {
    return m_Position;
}

f32 Camera2D::Zoom() const {
    return m_Zoom;
}

void Camera2D::SetZoom(f32 zoom) {
    m_Zoom = std::max(zoom, kMinZoom);
}

glm::mat4 Camera2D::ViewMatrix() const {
    const glm::vec3 eye{ m_Position.x, m_Position.y, kViewDistance };
    const glm::vec3 target{ m_Position.x, m_Position.y, 0.0f };

    return glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera2D::ProjectionMatrix(f32 aspectRatio) const {
    const f32 halfHeight = 1.0f / m_Zoom;
    const f32 halfWidth = aspectRatio * halfHeight;

    glm::mat4 projection = glm::ortho(
        -halfWidth,
        halfWidth,
        -halfHeight,
        halfHeight,
        kNearPlane,
        kFarPlane
    );
    projection[1][1] *= -1.0f;

    return projection;
}

void Camera2D::Reset() {
    m_Position = glm::vec2(0.0f, 0.0f);
    m_Zoom = 1.0f;
}

}
