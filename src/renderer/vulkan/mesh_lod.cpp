#include "renderer/vulkan/mesh_lod.h"
#include <meshoptimizer.h>
#include <algorithm>
#include <glm/glm.hpp>

namespace se {

MeshLodChain MeshLodGenerator::Generate(
    const std::vector<Vertex3D>& vertices, const std::vector<u32>& indices,
    const std::vector<f32>& targetRatios, f32 baseScreenSize)
{
    MeshLodChain chain;
    if (vertices.empty() || indices.empty()) return chain;
    MeshLodLevel base; base.vertices=vertices; base.indices=indices;
    base.screenSizeThreshold=1.0f; chain.levels.push_back(std::move(base));
    if (targetRatios.empty()) return chain;
    std::vector<f32> positions; positions.reserve(vertices.size()*3);
    for (auto& v : vertices) { positions.push_back(v.position[0]); positions.push_back(v.position[1]); positions.push_back(v.position[2]); }
    std::size_t bc = indices.size()/3, bv = vertices.size();
    for (std::size_t li=0; li<targetRatios.size(); ++li) {
        f32 ratio = targetRatios[li];
        std::size_t tt = std::size_t(f32(bc)*ratio);
        if (tt<3) break;
        auto& prev = chain.levels.back();
        std::size_t pic = prev.indices.size();
        if (pic/3 <= tt) break;
        std::vector<u32> lodIndices(pic);
        std::size_t nic = meshopt_simplify(lodIndices.data(), prev.indices.data(), pic,
            positions.data(), bv, sizeof(f32)*3, tt*3, 0.01f, 0, nullptr);
        if (nic==0 || nic>=pic) break;
        lodIndices.resize(nic);
        meshopt_optimizeVertexCache(lodIndices.data(), lodIndices.data(), nic, bv);
        MeshLodLevel lvl; lvl.vertices=vertices; lvl.indices=std::move(lodIndices);
        lvl.screenSizeThreshold=baseScreenSize*ratio;
        chain.levels.push_back(std::move(lvl));
    }
    return chain;
}

u32 MeshLodGenerator::SelectLod(f32 screenFraction, const MeshLodChain& chain,
    u32 previousLod, f32 hysteresis)
{
    if (chain.Empty()) return 0;
    u32 sel=0, max=u32(chain.levels.size())-1;
    for (u32 l=max; l>0; --l) {
        f32 th=chain.levels[l].screenSizeThreshold;
        if (l>previousLod) th -= hysteresis;
        if (screenFraction < th) { sel=l; break; }
    }
    return sel;
}

f32 MeshLodGenerator::ComputeScreenFraction(f32 radius, f32 dist, f32 sh, f32 fov)
{
    if (dist<=0||radius<=0) return 1.0f;
    f32 vh = 2.0f*dist*std::tan(fov*0.5f);
    return std::clamp(radius*2.0f/vh, 0.0f, 1.0f);
}

}
