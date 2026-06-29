#pragma once

#include "core.h"
#include "scene/renderable_core.h"
#include "scene/transform.h"

namespace se {

class Renderable2D {
public:
    Renderable2D(
        std::string name,
        std::string meshId,
        std::string materialId
    );

    SE_DISABLE_COPY(Renderable2D);
    SE_DISABLE_MOVE(Renderable2D);

    const std::string& Name() const;
    void SetName(std::string name);

    std::string_view MeshId() const;
    void SetMeshId(std::string meshId);
    std::string_view MaterialId() const;
    void SetMaterialId(std::string materialId);
    i32 DrawOrder() const;
    void SetDrawOrder(i32 drawOrder);
    Transform2D& Transform();
    const Transform2D& Transform() const;
    bool HighlightEnabled() const;
    void SetHighlightEnabled(bool enabled);

private:
    RenderableCore m_Core;
    Transform2D m_Transform;
    bool m_HighlightEnabled = true;
};

}
