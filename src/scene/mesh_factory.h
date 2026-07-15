#pragma once

#include "core.h"
#include "renderer/vulkan/vertex.h"

namespace se {

struct MeshData2D {
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
};

struct MeshData3D {
    std::vector<Vertex3D> vertices;
    std::vector<u32> indices;
};

class MeshFactory {
public:
    static MeshData2D CreateQuad();
    static MeshData3D CreateCube();
    static MeshData3D CreatePlane();
    static MeshData3D CreateGrid(u32 halfLineCount = 10, f32 spacing = 0.5f, f32 lineWidth = 0.012f);
    static MeshData3D CreateUvSphere(u32 slices = 32, u32 stacks = 16);
    static MeshData3D CreateCone(u32 segments = 48);
};

}
