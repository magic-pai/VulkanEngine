#include "scene/mesh_factory.h"

namespace se {

MeshData2D MeshFactory::CreateQuad() {
    MeshData2D meshData;
    meshData.vertices = {
        { { -0.5f, -0.5f }, { 1.0f, 0.2f, 0.2f }, { 0.0f, 1.0f } },
        { { 0.5f, -0.5f }, { 0.2f, 1.0f, 0.2f }, { 1.0f, 1.0f } },
        { { 0.5f, 0.5f }, { 0.2f, 0.4f, 1.0f }, { 1.0f, 0.0f } },
        { { -0.5f, 0.5f }, { 1.0f, 0.9f, 0.2f }, { 0.0f, 0.0f } }
    };
    meshData.indices = {
        0, 1, 2,
        2, 3, 0
    };

    return meshData;
}

MeshData3D MeshFactory::CreateCube() {
    MeshData3D meshData;
    meshData.vertices = {
        { { -0.5f, -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.32f, 0.26f }, { 0.0f, 1.0f } },
        { { 0.5f, -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.58f, 0.22f }, { 1.0f, 1.0f } },
        { { 0.5f, 0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.86f, 0.35f }, { 1.0f, 0.0f } },
        { { -0.5f, 0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.95f, 0.42f, 0.9f }, { 0.0f, 0.0f } },

        { { 0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f }, { 0.2f, 0.65f, 1.0f }, { 0.0f, 1.0f } },
        { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f }, { 0.14f, 0.95f, 0.76f }, { 1.0f, 1.0f } },
        { { -0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f }, { 0.34f, 0.78f, 1.0f }, { 1.0f, 0.0f } },
        { { 0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f }, { 0.5f, 0.48f, 1.0f }, { 0.0f, 0.0f } },

        { { -0.5f, -0.5f, -0.5f }, { -1.0f, 0.0f, 0.0f }, { 0.1f, 0.86f, 0.62f }, { 0.0f, 1.0f } },
        { { -0.5f, -0.5f, 0.5f }, { -1.0f, 0.0f, 0.0f }, { 0.35f, 1.0f, 0.62f }, { 1.0f, 1.0f } },
        { { -0.5f, 0.5f, 0.5f }, { -1.0f, 0.0f, 0.0f }, { 0.68f, 1.0f, 0.42f }, { 1.0f, 0.0f } },
        { { -0.5f, 0.5f, -0.5f }, { -1.0f, 0.0f, 0.0f }, { 0.17f, 0.58f, 0.92f }, { 0.0f, 0.0f } },

        { { 0.5f, -0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.55f, 0.34f }, { 0.0f, 1.0f } },
        { { 0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.38f, 0.52f }, { 1.0f, 1.0f } },
        { { 0.5f, 0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.94f, 0.74f, 0.2f }, { 1.0f, 0.0f } },
        { { 0.5f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.9f, 0.42f }, { 0.0f, 0.0f } },

        { { -0.5f, 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.8f, 0.95f, 1.0f }, { 0.0f, 1.0f } },
        { { 0.5f, 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.6f, 0.82f, 1.0f }, { 1.0f, 1.0f } },
        { { 0.5f, 0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.52f, 1.0f, 0.68f }, { 1.0f, 0.0f } },
        { { -0.5f, 0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.42f, 0.72f, 1.0f }, { 0.0f, 0.0f } },

        { { -0.5f, -0.5f, -0.5f }, { 0.0f, -1.0f, 0.0f }, { 0.32f, 0.3f, 0.48f }, { 0.0f, 1.0f } },
        { { 0.5f, -0.5f, -0.5f }, { 0.0f, -1.0f, 0.0f }, { 0.44f, 0.34f, 0.56f }, { 1.0f, 1.0f } },
        { { 0.5f, -0.5f, 0.5f }, { 0.0f, -1.0f, 0.0f }, { 0.56f, 0.4f, 0.62f }, { 1.0f, 0.0f } },
        { { -0.5f, -0.5f, 0.5f }, { 0.0f, -1.0f, 0.0f }, { 0.36f, 0.32f, 0.5f }, { 0.0f, 0.0f } }
    };

    meshData.indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        8, 9, 10, 10, 11, 8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20
    };

    return meshData;
}

MeshData3D MeshFactory::CreatePlane() {
    MeshData3D meshData;
    meshData.vertices = {
        { { -0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.42f, 0.46f, 0.50f }, { 0.0f, 1.0f } },
        { { -0.5f, 0.0f, 0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.46f, 0.50f, 0.54f }, { 0.0f, 0.0f } },
        { { 0.5f, 0.0f, 0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.52f, 0.56f, 0.60f }, { 1.0f, 0.0f } },
        { { 0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.38f, 0.42f, 0.46f }, { 1.0f, 1.0f } }
    };
    meshData.indices = {
        0, 1, 2,
        2, 3, 0
    };

    return meshData;
}

MeshData3D MeshFactory::CreateGrid(u32 halfLineCount, f32 spacing, f32 lineWidth) {
    MeshData3D meshData;
    const f32 halfExtent = static_cast<f32>(halfLineCount) * spacing;

    auto addQuad = [&](
        f32 minX,
        f32 maxX,
        f32 minZ,
        f32 maxZ,
        std::array<f32, 3> color
    ) {
        const u32 firstIndex = static_cast<u32>(meshData.vertices.size());
        meshData.vertices.push_back({ { minX, 0.0f, minZ }, { 0.0f, 1.0f, 0.0f }, color, { 0.0f, 1.0f } });
        meshData.vertices.push_back({ { minX, 0.0f, maxZ }, { 0.0f, 1.0f, 0.0f }, color, { 0.0f, 0.0f } });
        meshData.vertices.push_back({ { maxX, 0.0f, maxZ }, { 0.0f, 1.0f, 0.0f }, color, { 1.0f, 0.0f } });
        meshData.vertices.push_back({ { maxX, 0.0f, minZ }, { 0.0f, 1.0f, 0.0f }, color, { 1.0f, 1.0f } });

        meshData.indices.push_back(firstIndex);
        meshData.indices.push_back(firstIndex + 1);
        meshData.indices.push_back(firstIndex + 2);
        meshData.indices.push_back(firstIndex + 2);
        meshData.indices.push_back(firstIndex + 3);
        meshData.indices.push_back(firstIndex);
    };

    const std::array<f32, 3> minorColor{ 0.42f, 0.47f, 0.52f };
    const std::array<f32, 3> majorColor{ 0.62f, 0.67f, 0.72f };

    for (i32 i = -static_cast<i32>(halfLineCount);
         i <= static_cast<i32>(halfLineCount);
         ++i) {
        const f32 coordinate = static_cast<f32>(i) * spacing;
        const bool majorLine = i == 0 || i % 5 == 0;
        const std::array<f32, 3> color = majorLine ? majorColor : minorColor;
        const f32 width = majorLine ? lineWidth * 1.7f : lineWidth;
        const f32 currentHalfWidth = width * 0.5f;

        addQuad(
            -halfExtent,
            halfExtent,
            coordinate - currentHalfWidth,
            coordinate + currentHalfWidth,
            color
        );
        addQuad(
            coordinate - currentHalfWidth,
            coordinate + currentHalfWidth,
            -halfExtent,
            halfExtent,
            color
        );
    }

    return meshData;
}

}
