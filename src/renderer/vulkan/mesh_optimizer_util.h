#pragma once
#include "renderer/vulkan/vertex.h"
#include <meshoptimizer.h>
#include <vector>

namespace se {

struct OptimizedMeshData3D { std::vector<Vertex3D> vertices; std::vector<u32> indices; };

inline OptimizedMeshData3D OptimizeMesh3D(std::vector<Vertex3D> vertices, std::vector<u32> indices)
{
    if (indices.empty() || vertices.empty()) return {std::move(vertices), std::move(indices)};
    std::size_t vc = vertices.size(), ic = indices.size();
    meshopt_optimizeVertexCache(indices.data(), indices.data(), ic, vc);
    std::vector<u32> remap(vc);
    std::size_t nvc = meshopt_optimizeVertexFetchRemap(remap.data(), indices.data(), ic, vc);
    meshopt_remapIndexBuffer(indices.data(), indices.data(), ic, remap.data());
    std::vector<Vertex3D> nv(nvc);
    meshopt_remapVertexBuffer(nv.data(), vertices.data(), vc, sizeof(Vertex3D), remap.data());
    vertices = std::move(nv);
    return {std::move(vertices), std::move(indices)};
}

}
