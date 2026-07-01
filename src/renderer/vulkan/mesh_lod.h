#pragma once
#include "renderer/vulkan/vertex.h"
#include "renderer/vulkan/vulkan_common.h"
#include <glm/glm.hpp>
#include <vector>

namespace se {

struct MeshLodLevel {
    std::vector<Vertex3D> vertices; std::vector<u32> indices; f32 screenSizeThreshold=0.0f;
};
struct MeshLodChain {
    std::vector<MeshLodLevel> levels;
    bool Empty() const { return levels.empty(); }
    std::size_t Count() const { return levels.size(); }
};
class MeshLodGenerator {
public:
    static MeshLodChain Generate(const std::vector<Vertex3D>& vertices,
        const std::vector<u32>& indices,
        const std::vector<f32>& targetRatios={0.5f,0.25f,0.1f}, f32 baseScreenSize=0.25f);
    static u32 SelectLod(f32 screenFraction, const MeshLodChain& chain,
        u32 previousLod=0, f32 hysteresis=0.05f);
    static f32 ComputeScreenFraction(f32 objectRadius, f32 distanceToCamera,
        f32 screenHeight=1080.0f, f32 fovYRadians=1.0472f);
};

}
