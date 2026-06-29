#include "scene/renderable_2d.h"

#include <utility>

namespace se {

Renderable2D::Renderable2D(
    std::string name,
    std::string meshId,
    std::string materialId
) : m_Core(
        std::move(name),
        std::move(meshId),
        std::move(materialId)
    ) {
}

const std::string& Renderable2D::Name() const {
    return m_Core.Name();
}

void Renderable2D::SetName(std::string name) {
    m_Core.SetName(std::move(name));
}

std::string_view Renderable2D::MeshId() const {
    return m_Core.MeshId();
}

void Renderable2D::SetMeshId(std::string meshId) {
    m_Core.SetMeshId(std::move(meshId));
}

std::string_view Renderable2D::MaterialId() const {
    return m_Core.MaterialId();
}

void Renderable2D::SetMaterialId(std::string materialId) {
    m_Core.SetMaterialId(std::move(materialId));
}

i32 Renderable2D::DrawOrder() const {
    return m_Core.DrawOrder();
}

void Renderable2D::SetDrawOrder(i32 drawOrder) {
    m_Core.SetDrawOrder(drawOrder);
}

Transform2D& Renderable2D::Transform() {
    return m_Transform;
}

const Transform2D& Renderable2D::Transform() const {
    return m_Transform;
}

bool Renderable2D::HighlightEnabled() const {
    return m_HighlightEnabled;
}

void Renderable2D::SetHighlightEnabled(bool enabled) {
    m_HighlightEnabled = enabled;
}

}
