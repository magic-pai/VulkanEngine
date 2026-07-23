#pragma once

#include "renderer/vulkan/vertex.h"
#include "renderer/vulkan/vulkan_common.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace se {

class VulkanMesh;

struct GeneratedMeshLodLevel {
    std::vector<Vertex3D> vertices;
    std::vector<u32> indices;
    f32 targetRatio = 1.0f;
    f32 simplificationError = 0.0f;
};

struct GeneratedMeshLodChain {
    std::vector<GeneratedMeshLodLevel> levels;

    bool Empty() const { return levels.empty(); }
    std::size_t Count() const { return levels.size(); }
};

struct MeshLodLevel {
    const VulkanMesh* mesh = nullptr;
    u32 vertexCount = 0;
    u32 indexCount = 0;
    f32 targetRatio = 1.0f;
    f32 simplificationError = 0.0f;
};

struct MeshLodChain {
    std::vector<MeshLodLevel> levels;
    u64 residentVertexBytes = 0;
    u64 residentIndexBytes = 0;

    bool Empty() const { return levels.empty(); }
    std::size_t Count() const { return levels.size(); }
};

struct MeshLodSelection {
    const VulkanMesh* mesh = nullptr;
    u32 level = 0;
    f32 projectedErrorPixels = 0.0f;
};

class MeshLodGenerator {
public:
    static GeneratedMeshLodChain Generate(
        std::vector<Vertex3D> vertices,
        std::vector<u32> indices,
        const std::vector<f32>& targetRatios = { 0.5f, 0.25f, 0.1f },
        const std::vector<f32>& targetErrors = { 0.0025f, 0.006f, 0.015f }
    );
    static MeshLodSelection SelectLod(
        f32 projectedDiameterPixels,
        const MeshLodChain& chain,
        u32 previousLod = 0,
        bool previousLodValid = false,
        f32 targetPixelError = 1.0f,
        f32 hysteresisRatio = 0.15f
    );
    static f32 ComputeProjectedDiameterPixels(
        f32 objectRadius,
        f32 distanceToCamera,
        f32 screenHeight = 1080.0f,
        f32 fovYRadians = 1.0472f
    );
};

}
