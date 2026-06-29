#pragma once

#include "core.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace se {

class Camera2D {
public:
    glm::vec2& Position();
    const glm::vec2& Position() const;

    f32 Zoom() const;
    void SetZoom(f32 zoom);

    glm::mat4 ViewMatrix() const;
    glm::mat4 ProjectionMatrix(f32 aspectRatio) const;
    void Reset();

private:
    glm::vec2 m_Position{ 0.0f, 0.0f };
    f32 m_Zoom = 1.0f;
};

}
