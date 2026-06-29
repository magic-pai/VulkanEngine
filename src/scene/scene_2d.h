#pragma once

#include "core.h"
#include "scene/renderable_2d.h"
#include "scene/scene_storage.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace se {

class Scene2D {
public:
    Scene2D();
    ~Scene2D();

    Renderable2D& CreateRenderable(
        std::string name,
        std::string meshId,
        std::string materialId
    );
    void Clear();
    void Update(f32 deltaSeconds);

    std::span<Renderable2D* const> Renderables() const;
    bool Empty() const;
    std::size_t Count() const;

    Renderable2D* SelectedRenderable();
    const Renderable2D* SelectedRenderable() const;
    std::size_t SelectedIndex() const;
    void SetSelectedIndex(std::size_t index);
    bool SelectAtWorldPosition(const glm::vec2& worldPosition);

private:
    SceneStorage<Renderable2D> m_Storage;
};

}
