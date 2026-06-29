#pragma once

#include "core.h"

#include <utility>

namespace se {

class RenderableCore {
public:
    RenderableCore(
        std::string name,
        std::string meshId,
        std::string materialId
    ) : m_Name(std::move(name)),
        m_MeshId(std::move(meshId)),
        m_MaterialId(std::move(materialId)) {
    }

    const std::string& Name() const {
        return m_Name;
    }

    void SetName(std::string name) {
        m_Name = std::move(name);
    }

    std::string_view MeshId() const {
        return m_MeshId;
    }

    void SetMeshId(std::string meshId) {
        m_MeshId = std::move(meshId);
    }

    std::string_view MaterialId() const {
        return m_MaterialId;
    }

    void SetMaterialId(std::string materialId) {
        m_MaterialId = std::move(materialId);
    }

    i32 DrawOrder() const {
        return m_DrawOrder;
    }

    void SetDrawOrder(i32 drawOrder) {
        m_DrawOrder = drawOrder;
    }

private:
    std::string m_Name;
    std::string m_MeshId;
    std::string m_MaterialId;
    i32 m_DrawOrder = 0;
};

}
