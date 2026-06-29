#pragma once

#include "core.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <functional>
#include <limits>

namespace se {

void ResetTransformMatrixRecalculationCount();
u32 TransformMatrixRecalculationCount();

struct Transform3D {
    using ChangeCallback = std::function<void()>;

    const glm::vec3& Position() const;
    void SetPosition(glm::vec3 position);
    const glm::vec3& RotationDegrees() const;
    void SetRotationDegrees(glm::vec3 rotationDegrees);
    const glm::vec3& RotationSpeedDegreesPerSecond() const;
    void SetRotationSpeedDegreesPerSecond(glm::vec3 rotationSpeedDegreesPerSecond);
    const glm::vec3& Scale() const;
    void SetScale(glm::vec3 scale);
    bool AnimateRotation() const;
    void SetAnimateRotation(bool animateRotation);

    glm::mat4 Matrix() const;
    u64 MatrixVersion() const;
    void Reset();
    void SetChangeCallback(ChangeCallback callback);

private:
    void MarkMatrixDirty();

    glm::vec3 m_Position{ 0.0f, 0.0f, 0.0f };
    glm::vec3 m_RotationDegrees{ 0.0f, 0.0f, 0.0f };
    glm::vec3 m_RotationSpeedDegreesPerSecond{ 0.0f, 24.0f, 0.0f };
    glm::vec3 m_Scale{ 1.0f, 1.0f, 1.0f };
    bool m_AnimateRotation = true;
    mutable bool m_MatrixDirty = true;
    mutable glm::mat4 m_CachedMatrix{ 1.0f };
    u64 m_MatrixVersion = 1;
    ChangeCallback m_OnChange;
};

struct Transform2D {
    glm::vec2 position{ 0.0f, 0.0f };
    f32 rotationDegrees = 0.0f;
    f32 rotationSpeedDegreesPerSecond = 60.0f;
    glm::vec2 scale{ 1.0f, 1.0f };
    bool animateRotation = true;

    Transform3D AsTransform3D(f32 z = 0.0f) const;
    glm::mat4 Matrix() const;
    void Reset();

private:
    mutable glm::vec2 m_CachedPosition{ std::numeric_limits<f32>::quiet_NaN() };
    mutable f32 m_CachedRotationDegrees = std::numeric_limits<f32>::quiet_NaN();
    mutable glm::vec2 m_CachedScale{ std::numeric_limits<f32>::quiet_NaN() };
    mutable glm::mat4 m_CachedMatrix{ 1.0f };
};

}
