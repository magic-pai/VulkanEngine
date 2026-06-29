#include "scene/renderable_2d_order.h"

#include "scene/renderable_2d.h"

#include <algorithm>

namespace se {

std::vector<Renderable2D*> RenderablesInDrawOrder(
    std::span<Renderable2D* const> renderables
) {
    std::vector<Renderable2D*> orderedRenderables;
    orderedRenderables.reserve(renderables.size());

    for (Renderable2D* renderable : renderables) {
        SE_ASSERT(renderable != nullptr, "Scene contains a null renderable");
        orderedRenderables.push_back(renderable);
    }

    std::stable_sort(
        orderedRenderables.begin(),
        orderedRenderables.end(),
        [](const Renderable2D* lhs, const Renderable2D* rhs) {
            return lhs->DrawOrder() < rhs->DrawOrder();
        }
    );

    return orderedRenderables;
}

}
