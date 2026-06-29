#include "scene/renderable_3d.h"

#include <atomic>
#include <utility>

namespace se {

namespace {

std::atomic<u64> g_NextRenderable3DIdentity{ 1 };

}

Renderable3D::Renderable3D(
    std::string name,
    std::string meshId,
    std::string materialId
) : m_Core(
        std::move(name),
        std::move(meshId),
        std::move(materialId)
    ),
    m_RenderIdentity(g_NextRenderable3DIdentity.fetch_add(1, std::memory_order_relaxed)) {
    m_Transform.SetChangeCallback([this]() {
        MarkTransformChanged();
    });
}

const std::string& Renderable3D::Name() const {
    return m_Core.Name();
}

void Renderable3D::SetName(std::string name) {
    m_Core.SetName(std::move(name));
}

std::string_view Renderable3D::MeshId() const {
    return m_Core.MeshId();
}

void Renderable3D::SetMeshId(std::string meshId) {
    if (m_Core.MeshId() != meshId) {
        MarkRenderStateChanged();
    }
    m_Core.SetMeshId(std::move(meshId));
}

std::string_view Renderable3D::MaterialId() const {
    return m_Core.MaterialId();
}

void Renderable3D::SetMaterialId(std::string materialId) {
    if (m_Core.MaterialId() != materialId) {
        MarkRenderStateChanged();
    }
    m_Core.SetMaterialId(std::move(materialId));
}

i32 Renderable3D::DrawOrder() const {
    return m_Core.DrawOrder();
}

void Renderable3D::SetDrawOrder(i32 drawOrder) {
    if (m_Core.DrawOrder() != drawOrder) {
        MarkRenderStateChanged();
    }
    m_Core.SetDrawOrder(drawOrder);
}

u64 Renderable3D::RenderIdentity() const {
    return m_RenderIdentity;
}

u64 Renderable3D::RenderStateVersion() const {
    return m_RenderStateVersion;
}

Transform3D& Renderable3D::Transform() {
    return m_Transform;
}

const Transform3D& Renderable3D::Transform() const {
    return m_Transform;
}

bool Renderable3D::Pickable() const {
    return m_Pickable;
}

void Renderable3D::SetPickable(bool pickable) {
    m_Pickable = pickable;
}

bool Renderable3D::CastShadow() const {
    return m_CastShadow;
}

void Renderable3D::SetCastShadow(bool castShadow) {
    if (m_CastShadow != castShadow) {
        MarkRenderStateChanged();
    }
    m_CastShadow = castShadow;
}

void Renderable3D::SetRenderChangeCallback(ChangeCallback callback) {
    m_OnRenderChange = std::move(callback);
}

void Renderable3D::MarkRenderStateChanged() {
    ++m_RenderStateVersion;
    if (m_OnRenderChange) {
        m_OnRenderChange();
    }
}

void Renderable3D::MarkTransformChanged() {
    if (m_OnRenderChange) {
        m_OnRenderChange();
    }
}

}
