#pragma once

#include "core.h"

namespace se {

class Renderable2D;

std::vector<Renderable2D*> RenderablesInDrawOrder(
    std::span<Renderable2D* const> renderables
);

}
