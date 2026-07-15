#include "scene/mesh_factory.h"

#include <algorithm>
#include <cmath>

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

MeshData3D MeshFactory::CreateUvSphere(u32 slices, u32 stacks) {
    slices = std::max<u32>(slices, 8u);
    stacks = std::max<u32>(stacks, 4u);

    constexpr f32 kPi = 3.14159265359f;
    MeshData3D meshData;
    meshData.vertices.reserve(
        static_cast<std::size_t>((slices + 1u) * (stacks + 1u))
    );
    meshData.indices.reserve(static_cast<std::size_t>(slices * stacks * 6u));

    for (u32 stack = 0; stack <= stacks; ++stack) {
        const f32 v = static_cast<f32>(stack) / static_cast<f32>(stacks);
        const f32 phi = v * kPi;
        const f32 y = std::cos(phi);
        const f32 ringRadius = std::sin(phi);

        for (u32 slice = 0; slice <= slices; ++slice) {
            const f32 u = static_cast<f32>(slice) / static_cast<f32>(slices);
            const f32 theta = u * kPi * 2.0f;
            const f32 x = ringRadius * std::cos(theta);
            const f32 z = ringRadius * std::sin(theta);
            const std::array<f32, 3> normal{ x, y, z };
            const std::array<f32, 4> tangent{
                -std::sin(theta),
                0.0f,
                std::cos(theta),
                1.0f
            };
            const std::array<f32, 3> color{
                1.0f,
                1.0f,
                1.0f
            };
            meshData.vertices.push_back({
                { x * 0.5f, y * 0.5f, z * 0.5f },
                normal,
                color,
                { u, v },
                tangent
            });
        }
    }

    const u32 rowStride = slices + 1u;
    for (u32 stack = 0; stack < stacks; ++stack) {
        for (u32 slice = 0; slice < slices; ++slice) {
            const u32 i0 = stack * rowStride + slice;
            const u32 i1 = i0 + 1u;
            const u32 i2 = i0 + rowStride;
            const u32 i3 = i2 + 1u;

            meshData.indices.push_back(i0);
            meshData.indices.push_back(i1);
            meshData.indices.push_back(i2);
            meshData.indices.push_back(i1);
            meshData.indices.push_back(i3);
            meshData.indices.push_back(i2);
        }
    }

    return meshData;
}

MeshData3D MeshFactory::CreateCone(u32 segments) {
    segments = std::max<u32>(segments, 8u);

    constexpr f32 kPi = 3.14159265359f;
    MeshData3D meshData;
    meshData.vertices.reserve(static_cast<std::size_t>(segments * 4u + 2u));
    meshData.indices.reserve(static_cast<std::size_t>(segments * 6u));

    const std::array<f32, 3> sideColor{ 1.0f, 0.86f, 0.34f };
    const std::array<f32, 3> capColor{ 1.0f, 0.70f, 0.26f };
    const u32 capCenterIndex = 0u;
    meshData.vertices.push_back({
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        capColor,
        { 0.5f, 0.5f }
    });

    for (u32 segment = 0; segment < segments; ++segment) {
        const f32 u = static_cast<f32>(segment) / static_cast<f32>(segments);
        const f32 theta = u * kPi * 2.0f;
        const f32 x = std::cos(theta) * 0.5f;
        const f32 z = std::sin(theta) * 0.5f;
        meshData.vertices.push_back({
            { x, 1.0f, z },
            { 0.0f, 1.0f, 0.0f },
            capColor,
            { x + 0.5f, z + 0.5f }
        });
    }

    const u32 apexStart = static_cast<u32>(meshData.vertices.size());
    for (u32 segment = 0; segment < segments; ++segment) {
        const f32 u = static_cast<f32>(segment) / static_cast<f32>(segments);
        const f32 theta = u * kPi * 2.0f;
        const f32 x = std::cos(theta) * 0.5f;
        const f32 z = std::sin(theta) * 0.5f;
        const glm::vec3 normal = glm::normalize(glm::vec3{ x, 0.25f, z });
        meshData.vertices.push_back({
            { 0.0f, 0.0f, 0.0f },
            { normal.x, normal.y, normal.z },
            sideColor,
            { u, 0.0f }
        });
        meshData.vertices.push_back({
            { x, 1.0f, z },
            { normal.x, normal.y, normal.z },
            sideColor,
            { u, 1.0f }
        });
    }

    for (u32 segment = 0; segment < segments; ++segment) {
        const u32 next = (segment + 1u) % segments;
        meshData.indices.push_back(capCenterIndex);
        meshData.indices.push_back(1u + next);
        meshData.indices.push_back(1u + segment);

        const u32 apex0 = apexStart + segment * 2u;
        const u32 base0 = apex0 + 1u;
        const u32 apex1 = apexStart + next * 2u;
        const u32 base1 = apex1 + 1u;
        meshData.indices.push_back(apex0);
        meshData.indices.push_back(base0);
        meshData.indices.push_back(base1);
    }

    return meshData;
}

}
