#pragma once

#include "core.h"

#include <algorithm>
#include <utility>

namespace se {

template <typename Renderable>
class SceneStorage {
public:
    Renderable& CreateRenderable(
        std::string name,
        std::string meshId,
        std::string materialId
    ) {
        m_RenderableStorage.push_back(std::make_unique<Renderable>(
            std::move(name),
            std::move(meshId),
            std::move(materialId)
        ));
        RebuildRenderableView();

        return *m_RenderableStorage.back();
    }

    void Clear() {
        m_RenderableStorage.clear();
        m_Renderables.clear();
        m_SelectedIndex = 0;
    }

    std::span<Renderable* const> Renderables() const {
        return std::span<Renderable* const>(m_Renderables.data(), m_Renderables.size());
    }

    bool Empty() const {
        return m_Renderables.empty();
    }

    std::size_t Count() const {
        return m_Renderables.size();
    }

    Renderable* SelectedRenderable() {
        if (m_Renderables.empty()) {
            return nullptr;
        }

        return m_Renderables[m_SelectedIndex];
    }

    const Renderable* SelectedRenderable() const {
        if (m_Renderables.empty()) {
            return nullptr;
        }

        return m_Renderables[m_SelectedIndex];
    }

    std::size_t SelectedIndex() const {
        return m_SelectedIndex;
    }

    void SetSelectedIndex(std::size_t index) {
        if (m_Renderables.empty()) {
            m_SelectedIndex = 0;
            return;
        }

        m_SelectedIndex = std::min(index, m_Renderables.size() - 1);
    }

    bool SelectRenderable(Renderable& renderable) {
        const auto found = std::find(m_Renderables.begin(), m_Renderables.end(), &renderable);
        if (found == m_Renderables.end()) {
            return false;
        }

        m_SelectedIndex = static_cast<std::size_t>(std::distance(m_Renderables.begin(), found));
        return true;
    }

private:
    void RebuildRenderableView() {
        m_Renderables.clear();
        m_Renderables.reserve(m_RenderableStorage.size());

        for (const std::unique_ptr<Renderable>& renderable : m_RenderableStorage) {
            SE_ASSERT(renderable != nullptr, "Scene storage contains a null renderable");
            m_Renderables.push_back(renderable.get());
        }

        SetSelectedIndex(m_SelectedIndex);
    }

private:
    std::vector<std::unique_ptr<Renderable>> m_RenderableStorage;
    std::vector<Renderable*> m_Renderables;
    std::size_t m_SelectedIndex = 0;
};

}
