#include "app/application_2d.h"
#include "renderer/vulkan/pipeline_spec.h"
#include "scene/sample_scene_2d.h"

#ifndef SE_SHADER_DIR
#define SE_SHADER_DIR "shaders"
#endif

#ifndef SE_ASSET_DIR
#define SE_ASSET_DIR "assets"
#endif

int main() {
    constexpr int kDisplay1 = 1;
    const std::string vertexShaderPath = std::string(SE_SHADER_DIR) + "/triangle.vert.spv";
    const std::string fragmentShaderPath = std::string(SE_SHADER_DIR) + "/triangle.frag.spv";

    se::Application2D app(
        1280,
        720,
        "MagicPai Engine",
        kDisplay1,
        se::PipelineSpec::Default2D(vertexShaderPath, fragmentShaderPath)
    );

    se::SampleScene2D sampleScene(
        app.Device(),
        
        app.PhysicalDevice(),
        app.CommandPool(),
        app.MaterialLibrary(),
        app.RenderResources(),
        app.Scene(),
        SE_ASSET_DIR
    );

    app.CreateRenderer();
    app.Run([&](float deltaSeconds, float) {
        app.Scene().Update(deltaSeconds);
    });

    return 0;
}
