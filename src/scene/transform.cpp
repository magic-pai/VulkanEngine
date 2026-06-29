#include "scene/transform.h"

#include <atomic>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

namespace se {

namespace {

std::atomic<u32> g_TransformMatrixRecalculationCount{ 0 };

void CountMatrixRecalculation() {
    g_TransformMatrixRecalculationCount.fetch_add(1, std::memory_order_relaxed);
}

}

void ResetTransformMatrixRecalculationCount() {
    g_TransformMatrixRecalculationCount.store(0, std::memory_order_relaxed);
}

u32 TransformMatrixRecalculationCount() {
    return g_TransformMatrixRecalculationCount.load(std::memory_order_relaxed);
}

Transform3D Transform2D::AsTransform3D(f32 z) const {
    Transform3D transform{};
    transform.SetPosition(glm::vec3(position, z));
    transform.SetRotationDegrees(glm::vec3(0.0f, 0.0f, rotationDegrees));
    transform.SetRotationSpeedDegreesPerSecond(glm::vec3(0.0f, 0.0f, rotationSpeedDegreesPerSecond));
    transform.SetScale(glm::vec3(scale, 1.0f));
    transform.SetAnimateRotation(animateRotation);

    return transform;
}

glm::mat4 Transform2D::Matrix() const {
    if (position == m_CachedPosition &&
        rotationDegrees == m_CachedRotationDegrees &&
        scale == m_CachedScale) {
        return m_CachedMatrix;
    }

    glm::mat4 transform{ 1.0f };
    transform = glm::translate(transform, glm::vec3(position, 0.0f));
    transform = glm::rotate(
        transform,
        glm::radians(rotationDegrees),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );
    transform = glm::scale(transform, glm::vec3(scale, 1.0f));

    m_CachedPosition = position;
    m_CachedRotationDegrees = rotationDegrees;
    m_CachedScale = scale;
    m_CachedMatrix = transform;
    CountMatrixRecalculation();

    return m_CachedMatrix;
}

void Transform2D::Reset() {
    *this = Transform2D{};
}

const glm::vec3& Transform3D::Position() const {
    return m_Position;
}

void Transform3D::SetPosition(glm::vec3 position) {
    if (m_Position == position) {
        return;
    }

    m_Position = position;
    MarkMatrixDirty();
}

const glm::vec3& Transform3D::RotationDegrees() const {
    return m_RotationDegrees;
}

void Transform3D::SetRotationDegrees(glm::vec3 rotationDegrees) {
    if (m_RotationDegrees == rotationDegrees) {
        return;
    }

    m_RotationDegrees = rotationDegrees;
    MarkMatrixDirty();
}

const glm::vec3& Transform3D::RotationSpeedDegreesPerSecond() const {
    return m_RotationSpeedDegreesPerSecond;
}

void Transform3D::SetRotationSpeedDegreesPerSecond(glm::vec3 rotationSpeedDegreesPerSecond) {
    m_RotationSpeedDegreesPerSecond = rotationSpeedDegreesPerSecond;
}

const glm::vec3& Transform3D::Scale() const {
    return m_Scale;
}

void Transform3D::SetScale(glm::vec3 scale) {
    if (m_Scale == scale) {
        return;
    }

    m_Scale = scale;
    MarkMatrixDirty();
}

bool Transform3D::AnimateRotation() const {
    return m_AnimateRotation;
}

void Transform3D::SetAnimateRotation(bool animateRotation) {
    m_AnimateRotation = animateRotation;
}

void Transform3D::MarkMatrixDirty() {
    m_MatrixDirty = true;
    ++m_MatrixVersion;
    if (m_OnChange) {
        m_OnChange();
    }
}

glm::mat4 Transform3D::Matrix() const {
    if (!m_MatrixDirty) {
        return m_CachedMatrix;
    }

    glm::mat4 transform{ 1.0f };
    transform = glm::translate(transform, m_Position);
    transform = glm::rotate(transform, glm::radians(m_RotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(m_RotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(m_RotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::scale(transform, m_Scale);

    m_CachedMatrix = transform;
    m_MatrixDirty = false;
    CountMatrixRecalculation();

    return m_CachedMatrix;
}

u64 Transform3D::MatrixVersion() const {
    return m_MatrixVersion;
}

void Transform3D::Reset() {
    const bool matrixChanged =
        m_Position != glm::vec3(0.0f, 0.0f, 0.0f) ||
        m_RotationDegrees != glm::vec3(0.0f, 0.0f, 0.0f) ||
        m_Scale != glm::vec3(1.0f, 1.0f, 1.0f);

    m_Position = { 0.0f, 0.0f, 0.0f };
    m_RotationDegrees = { 0.0f, 0.0f, 0.0f };
    m_RotationSpeedDegreesPerSecond = { 0.0f, 24.0f, 0.0f };
    m_Scale = { 1.0f, 1.0f, 1.0f };
    m_AnimateRotation = true;
    if (matrixChanged) {
        MarkMatrixDirty();
    }
}

void Transform3D::SetChangeCallback(ChangeCallback callback) {
    m_OnChange = std::move(callback);
}

}
