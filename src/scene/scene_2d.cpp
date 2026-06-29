#include "scene/scene_2d.h"

#include "scene/renderable_2d.h"
#include "scene/renderable_2d_order.h"

#include <algorithm>

#include <glm/gtc/matrix_inverse.hpp>

namespace se {

namespace {

bool UnitQuadContainsWorldPoint(const Renderable2D& renderable, const glm::vec2& worldPosition) {
    const glm::mat4 inverseTransform = glm::inverse(renderable.Transform().Matrix());
    const glm::vec4 localPosition = inverseTransform * glm::vec4(worldPosition, 0.0f, 1.0f);

    return localPosition.x >= -0.5f &&
        localPosition.x <= 0.5f &&
        localPosition.y >= -0.5f &&
        localPosition.y <= 0.5f;
}

}

Scene2D::Scene2D() = default;

Scene2D::~Scene2D() = default;

Renderable2D& Scene2D::CreateRenderable(
    std::string name,
    std::string meshId,
    std::string materialId
) {
    return m_Storage.CreateRenderable(
        std::move(name),
        std::move(meshId),
        std::move(materialId)
    );
}

void Scene2D::Clear() {
    m_Storage.Clear();
}

void Scene2D::Update(f32 deltaSeconds) {
    const f32 clampedDeltaSeconds = std::max(deltaSeconds, 0.0f);
    for (Renderable2D* renderable : m_Storage.Renderables()) {
        if (renderable == nullptr) {
            continue;
        }

        Transform2D& transform = renderable->Transform();
        if (!transform.animateRotation) {
            continue;
        }

        transform.rotationDegrees +=
            transform.rotationSpeedDegreesPerSecond * clampedDeltaSeconds;
        while (transform.rotationDegrees > 180.0f) {
            transform.rotationDegrees -= 360.0f;
        }
        while (transform.rotationDegrees < -180.0f) {
            transform.rotationDegrees += 360.0f;
        }
    }
}

std::span<Renderable2D* const> Scene2D::Renderables() const {
    return m_Storage.Renderables();
}

bool Scene2D::Empty() const {
    return m_Storage.Empty();
}

std::size_t Scene2D::Count() const {
    return m_Storage.Count();
}

Renderable2D* Scene2D::SelectedRenderable() {
    return m_Storage.SelectedRenderable();
}

const Renderable2D* Scene2D::SelectedRenderable() const {
    return m_Storage.SelectedRenderable();
}

std::size_t Scene2D::SelectedIndex() const {
    return m_Storage.SelectedIndex();
}

void Scene2D::SetSelectedIndex(std::size_t index) {
    m_Storage.SetSelectedIndex(index);
}

bool Scene2D::SelectAtWorldPosition(const glm::vec2& worldPosition) {
    const std::vector<Renderable2D*> orderedRenderables =
        RenderablesInDrawOrder(Renderables());

    for (std::size_t reverseIndex = orderedRenderables.size(); reverseIndex > 0; --reverseIndex) {
        Renderable2D* renderable = orderedRenderables[reverseIndex - 1];

        if (renderable != nullptr && UnitQuadContainsWorldPoint(*renderable, worldPosition)) {
            const bool selected = m_Storage.SelectRenderable(*renderable);
            SE_ASSERT(selected, "Selected renderable is not part of the scene");
            return true;
        }
    }

    return false;
}

}
