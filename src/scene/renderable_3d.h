#pragma once

#include "core.h"
#include "scene/renderable_core.h"
#include "scene/transform.h"

#include <functional>

namespace se {

class Renderable3D {
public:
    using ChangeCallback = std::function<void()>;

    Renderable3D(
        std::string name,
        std::string meshId,
        std::string materialId
    );

    SE_DISABLE_COPY(Renderable3D);
    SE_DISABLE_MOVE(Renderable3D);

    const std::string& Name() const;
    void SetName(std::string name);

    std::string_view MeshId() const;
    void SetMeshId(std::string meshId);
    std::string_view MaterialId() const;
    void SetMaterialId(std::string materialId);
    i32 DrawOrder() const;
    void SetDrawOrder(i32 drawOrder);
    u64 RenderIdentity() const;
    u64 RenderStateVersion() const;
    Transform3D& Transform();
    const Transform3D& Transform() const;
    bool Pickable() const;
    void SetPickable(bool pickable);
    bool CastShadow() const;
    void SetCastShadow(bool castShadow);
    void SetRenderChangeCallback(ChangeCallback callback);

private:
    void MarkRenderStateChanged();
    void MarkTransformChanged();

    RenderableCore m_Core;
    Transform3D m_Transform;
    u64 m_RenderIdentity = 0;
    u64 m_RenderStateVersion = 1;
    bool m_Pickable = true;
    bool m_CastShadow = true;
    ChangeCallback m_OnRenderChange;
};

}
