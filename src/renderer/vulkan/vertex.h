#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <algorithm>
#include <cstddef>
#include <type_traits>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace se {

struct Vertex {
    std::array<f32, 2> position;
    std::array<f32, 3> color;
    std::array<f32, 2> texCoord;

    static VkVertexInputBindingDescription BindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 3> AttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, position);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
};

struct Vertex3D {
    std::array<f32, 3> position;
    std::array<f32, 3> normal;
    std::array<f32, 3> color;
    std::array<f32, 2> texCoord;
    std::array<f32, 4> tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
    std::array<u32, 4> boneIndices{ 0u, 0u, 0u, 0u };
    std::array<f32, 4> boneWeights{ 0.0f, 0.0f, 0.0f, 0.0f };

    static constexpr u32 BoneIndicesLocation = 5;
    static constexpr u32 BoneWeightsLocation = 6;
    static constexpr u32 InstanceModelLocationBase = 5;

    static VkVertexInputBindingDescription BindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex3D);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 5> AttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex3D, position);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex3D, normal);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex3D, color);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex3D, texCoord);

        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Vertex3D, tangent);

        return attributeDescriptions;
    }

    static std::array<VkVertexInputAttributeDescription, 7> SkinnedAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 7> attributeDescriptions{};
        const auto baseAttributes = AttributeDescriptions();
        std::copy(
            baseAttributes.begin(),
            baseAttributes.end(),
            attributeDescriptions.begin()
        );

        attributeDescriptions[5].binding = 0;
        attributeDescriptions[5].location = BoneIndicesLocation;
        attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_UINT;
        attributeDescriptions[5].offset = offsetof(Vertex3D, boneIndices);

        attributeDescriptions[6].binding = 0;
        attributeDescriptions[6].location = BoneWeightsLocation;
        attributeDescriptions[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[6].offset = offsetof(Vertex3D, boneWeights);

        return attributeDescriptions;
    }
};

static_assert(std::is_standard_layout_v<Vertex3D>);
static_assert(sizeof(Vertex3D) == 92u);
static_assert(offsetof(Vertex3D, position) == 0u);
static_assert(offsetof(Vertex3D, normal) == 12u);
static_assert(offsetof(Vertex3D, color) == 24u);
static_assert(offsetof(Vertex3D, texCoord) == 36u);
static_assert(offsetof(Vertex3D, tangent) == 44u);
static_assert(offsetof(Vertex3D, boneIndices) == 60u);
static_assert(offsetof(Vertex3D, boneWeights) == 76u);

struct Instance3D {
    glm::mat4 model{ 1.0f };

    static VkVertexInputBindingDescription BindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 1;
        bindingDescription.stride = sizeof(Instance3D);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 4> AttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};

        for (u32 index = 0; index < static_cast<u32>(attributeDescriptions.size()); ++index) {
            attributeDescriptions[index].binding = 1;
            attributeDescriptions[index].location =
                Vertex3D::InstanceModelLocationBase + index;
            attributeDescriptions[index].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[index].offset =
                offsetof(Instance3D, model) + sizeof(glm::vec4) * index;
        }

        return attributeDescriptions;
    }
};

}
