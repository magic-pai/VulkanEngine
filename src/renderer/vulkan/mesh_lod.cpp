#include "renderer/vulkan/mesh_lod.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace se {

namespace {

struct SimplificationAttributes {
    std::array<f32, 3> normal{};
    std::array<f32, 2> texCoord{};
};

static_assert(sizeof(SimplificationAttributes) == sizeof(f32) * 5u);

void OptimizeAndCompact(GeneratedMeshLodLevel& level) {
    if (level.vertices.empty() || level.indices.empty()) {
        return;
    }

    meshopt_optimizeVertexCache(
        level.indices.data(),
        level.indices.data(),
        level.indices.size(),
        level.vertices.size()
    );

    std::vector<Vertex3D> compactVertices(level.vertices.size());
    const std::size_t compactVertexCount = meshopt_optimizeVertexFetch(
        compactVertices.data(),
        level.indices.data(),
        level.indices.size(),
        level.vertices.data(),
        level.vertices.size(),
        sizeof(Vertex3D)
    );
    compactVertices.resize(compactVertexCount);
    level.vertices = std::move(compactVertices);
}

std::vector<SimplificationAttributes> AttributesFor(
    const std::vector<Vertex3D>& vertices
) {
    std::vector<SimplificationAttributes> attributes(vertices.size());
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        attributes[index].normal = vertices[index].normal;
        attributes[index].texCoord = vertices[index].texCoord;
    }
    return attributes;
}

}

GeneratedMeshLodChain MeshLodGenerator::Generate(
    std::vector<Vertex3D> vertices,
    std::vector<u32> indices,
    const std::vector<f32>& targetRatios,
    const std::vector<f32>& targetErrors
) {
    GeneratedMeshLodChain chain;
    if (vertices.empty() || indices.empty()) {
        return chain;
    }

    GeneratedMeshLodLevel base{};
    base.vertices = std::move(vertices);
    base.indices = std::move(indices);
    if (base.indices.size() < 3u || base.indices.size() % 3u != 0u) {
        chain.levels.push_back(std::move(base));
        return chain;
    }
    OptimizeAndCompact(base);
    chain.levels.push_back(std::move(base));

    const std::size_t sourceIndexCount = chain.levels.front().indices.size();
    const std::size_t levelCount = std::min(targetRatios.size(), targetErrors.size());
    f32 accumulatedError = 0.0f;
    constexpr std::array<f32, 5> kAttributeWeights{
        0.5f, 0.5f, 0.5f,
        10.0f, 10.0f
    };

    for (std::size_t levelIndex = 0; levelIndex < levelCount; ++levelIndex) {
        const f32 targetRatio = std::clamp(targetRatios[levelIndex], 0.01f, 0.99f);
        const std::size_t targetIndexCount = std::max<std::size_t>(
            3u,
            (static_cast<std::size_t>(
                static_cast<f64>(sourceIndexCount) * targetRatio
            ) / 3u) * 3u
        );
        const GeneratedMeshLodLevel& previous = chain.levels.back();
        if (targetIndexCount >= previous.indices.size()) {
            continue;
        }

        const std::vector<SimplificationAttributes> attributes =
            AttributesFor(previous.vertices);
        std::vector<u32> simplifiedIndices(previous.indices.size());
        f32 stepError = 0.0f;
        const std::size_t simplifiedIndexCount = meshopt_simplifyWithAttributes(
            simplifiedIndices.data(),
            previous.indices.data(),
            previous.indices.size(),
            previous.vertices.front().position.data(),
            previous.vertices.size(),
            sizeof(Vertex3D),
            attributes.front().normal.data(),
            sizeof(SimplificationAttributes),
            kAttributeWeights.data(),
            kAttributeWeights.size(),
            nullptr,
            targetIndexCount,
            std::max(targetErrors[levelIndex], 0.00001f),
            meshopt_SimplifyLockBorder,
            &stepError
        );
        if (simplifiedIndexCount < 3u ||
            simplifiedIndexCount >= previous.indices.size() * 95u / 100u) {
            continue;
        }

        simplifiedIndices.resize(simplifiedIndexCount);
        GeneratedMeshLodLevel level{};
        level.vertices = previous.vertices;
        level.indices = std::move(simplifiedIndices);
        level.targetRatio = static_cast<f32>(simplifiedIndexCount) /
            static_cast<f32>(sourceIndexCount);
        accumulatedError += std::max(stepError, 0.0f);
        level.simplificationError = accumulatedError;
        OptimizeAndCompact(level);
        chain.levels.push_back(std::move(level));
    }

    return chain;
}

MeshLodSelection MeshLodGenerator::SelectLod(
    f32 projectedDiameterPixels,
    const MeshLodChain& chain,
    u32 previousLod,
    bool previousLodValid,
    f32 targetPixelError,
    f32 hysteresisRatio
) {
    MeshLodSelection selection{};
    if (chain.Empty() || chain.levels.front().mesh == nullptr) {
        return selection;
    }

    const u32 maxLevel = static_cast<u32>(chain.levels.size() - 1u);
    const f32 diameterPixels = std::max(projectedDiameterPixels, 0.0f);
    const f32 targetError = std::max(targetPixelError, 0.1f);
    const f32 hysteresis = std::clamp(hysteresisRatio, 0.0f, 0.45f);
    auto errorPixels = [&](u32 level) {
        return std::max(chain.levels[level].simplificationError, 0.0f) *
            diameterPixels;
    };

    u32 selectedLevel = 0u;
    if (!previousLodValid) {
        for (u32 level = 1u; level <= maxLevel; ++level) {
            if (errorPixels(level) <= targetError) {
                selectedLevel = level;
            }
        }
    } else {
        selectedLevel = std::min(previousLod, maxLevel);
        const f32 downgradeLimit = targetError * (1.0f - hysteresis);
        const f32 upgradeLimit = targetError * (1.0f + hysteresis);
        while (selectedLevel < maxLevel &&
            errorPixels(selectedLevel + 1u) <= downgradeLimit) {
            ++selectedLevel;
        }
        while (selectedLevel > 0u && errorPixels(selectedLevel) > upgradeLimit) {
            --selectedLevel;
        }
    }

    const MeshLodLevel& level = chain.levels[selectedLevel];
    selection.mesh = level.mesh;
    selection.level = selectedLevel;
    selection.projectedErrorPixels = errorPixels(selectedLevel);
    return selection;
}

f32 MeshLodGenerator::ComputeProjectedDiameterPixels(
    f32 objectRadius,
    f32 distanceToCamera,
    f32 screenHeight,
    f32 fovYRadians
) {
    if (objectRadius <= 0.0f || screenHeight <= 0.0f) {
        return 0.0f;
    }

    const f32 distanceToSurface = std::max(
        distanceToCamera - objectRadius,
        0.0001f
    );
    const f32 halfFov = std::clamp(
        fovYRadians * 0.5f,
        0.01f,
        1.55f
    );
    const f32 viewHeight = 2.0f * distanceToSurface * std::tan(halfFov);
    if (viewHeight <= std::numeric_limits<f32>::epsilon()) {
        return screenHeight;
    }

    return std::min((objectRadius * 2.0f / viewHeight) * screenHeight, screenHeight);
}

}
