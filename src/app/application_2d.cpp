#include "app/application_2d.h"

#include <utility>

namespace se {

Application2D::Application2D(
    int width,
    int height,
    std::string title,
    int monitorIndex,
    PipelineSpec pipelineSpec
) : Application(
        width,
        height,
        std::move(title),
        monitorIndex,
        std::move(pipelineSpec)
    ) {
}

Application2D::~Application2D() {
    DestroyRenderer();
}

Scene2D& Application2D::Scene() {
    return m_Scene;
}

Camera2D& Application2D::Camera() {
    return m_Camera;
}

Scene2D* Application2D::Scene2DForRenderer() {
    return &m_Scene;
}

Camera2D* Application2D::Camera2DForRenderer() {
    return &m_Camera;
}

}
