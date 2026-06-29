#pragma once

#include "app/application.h"
#include "scene/camera_2d.h"
#include "scene/scene_2d.h"

namespace se {

class Application2D : public Application {
public:
    Application2D(
        int width,
        int height,
        std::string title,
        int monitorIndex,
        PipelineSpec pipelineSpec
    );
    ~Application2D();

    SE_DISABLE_COPY(Application2D);
    SE_DISABLE_MOVE(Application2D);

    Scene2D& Scene();
    Camera2D& Camera();

protected:
    Scene2D* Scene2DForRenderer() override;
    Camera2D* Camera2DForRenderer() override;

private:
    Scene2D m_Scene;
    Camera2D m_Camera;
};

}
