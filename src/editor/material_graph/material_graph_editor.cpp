#include "editor/material_graph/material_graph_editor.h"

#include <imgui.h>
#include <imgui_node_editor.h>
#include <nlohmann/json.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace se {
namespace {

namespace nodeEditor = ax::NodeEditor;
using json = nlohmann::json;

constexpr std::string_view kDocumentFormat = "SelfEngineMaterialGraph";
constexpr u32 kDocumentVersion = 2u;
constexpr u32 kLegacyPbrDocumentVersion = 1u;
constexpr std::string_view kBlackHoleDocumentFormat = "SelfEngineBlackHoleGraph";
constexpr u32 kBlackHoleDocumentVersion = 1u;
constexpr u64 kPinStride = 16u;

enum class NodeKind : u32 {
    Color = 0u,
    Float = 1u,
    PbrOutput = 2u,
    UnlitOutput = 3u,
    KerrSpacetime = 4u,
    NovikovThorneDisk = 5u,
    SceneEnvironment = 6u,
    KerrRadiativeTransfer = 7u,
    BlackHoleOutput = 8u
};

enum class GraphTarget : u32 {
    Surface = 0u,
    RelativisticBlackHole = 1u
};

enum class ShadingModel : u32 {
    LitPbr = 0u,
    Unlit = 1u
};

enum class ValueKind : u32 {
    Color = 0u,
    Float = 1u,
    Spacetime = 2u,
    AccretionMedium = 3u,
    EnvironmentRadiance = 4u,
    RelativisticRadiance = 5u
};

enum class PinDirection : u32 {
    Input = 0u,
    Output = 1u
};

struct GraphNode {
    u64 id = 0u;
    NodeKind kind = NodeKind::Float;
    glm::vec2 position{ 0.0f };
    glm::vec4 color{ 1.0f };
    f32 value = 0.0f;
    f32 massSolar = 10.0f;
    f32 dimensionlessSpin = 0.8f;
    f32 eddingtonRatio = 0.1f;
    f32 outerRadiusRg = 40.0f;
    f32 colorCorrection = 1.7f;
    f32 environmentIntensity = 1.0f;
    i32 maxSteps = 512;
    i32 maxImageOrder = 2;
    i32 spectralSamples = 16;
    f32 relativeTolerance = 0.00001f;
    bool restorePosition = true;
};

struct GraphLink {
    u64 id = 0u;
    u64 outputPin = 0u;
    u64 inputPin = 0u;
};

struct PinInfo {
    u64 nodeId = 0u;
    ValueKind valueKind = ValueKind::Float;
    PinDirection direction = PinDirection::Input;
};

struct CompiledMaterialGraph {
    bool valid = false;
    GraphTarget target = GraphTarget::Surface;
    ShadingModel shadingModel = ShadingModel::LitPbr;
    glm::vec4 baseColor{ 1.0f };
    f32 metallic = 0.0f;
    f32 roughness = 0.5f;
    std::string error;
};

const char* NodeKindName(NodeKind kind) {
    switch (kind) {
        case NodeKind::Color:
            return "Color";
        case NodeKind::Float:
            return "Float";
        case NodeKind::PbrOutput:
            return "PbrOutput";
        case NodeKind::UnlitOutput:
            return "UnlitOutput";
        case NodeKind::KerrSpacetime:
            return "KerrSpacetime";
        case NodeKind::NovikovThorneDisk:
            return "NovikovThorneDisk";
        case NodeKind::SceneEnvironment:
            return "SceneEnvironment";
        case NodeKind::KerrRadiativeTransfer:
            return "KerrRadiativeTransfer";
        case NodeKind::BlackHoleOutput:
            return "BlackHoleOutput";
    }
    return "Unknown";
}

std::optional<NodeKind> NodeKindFromName(std::string_view name) {
    if (name == "Color") {
        return NodeKind::Color;
    }
    if (name == "Float") {
        return NodeKind::Float;
    }
    if (name == "PbrOutput") {
        return NodeKind::PbrOutput;
    }
    if (name == "UnlitOutput") {
        return NodeKind::UnlitOutput;
    }
    if (name == "KerrSpacetime") {
        return NodeKind::KerrSpacetime;
    }
    if (name == "NovikovThorneDisk") {
        return NodeKind::NovikovThorneDisk;
    }
    if (name == "SceneEnvironment") {
        return NodeKind::SceneEnvironment;
    }
    if (name == "KerrRadiativeTransfer") {
        return NodeKind::KerrRadiativeTransfer;
    }
    if (name == "BlackHoleOutput") {
        return NodeKind::BlackHoleOutput;
    }
    return std::nullopt;
}

const char* ShadingModelName(ShadingModel model) {
    return model == ShadingModel::Unlit ? "Unlit" : "LitPBR";
}

bool IsOutputNode(NodeKind kind) {
    return kind == NodeKind::PbrOutput || kind == NodeKind::UnlitOutput ||
        kind == NodeKind::BlackHoleOutput;
}

bool IsBlackHoleNode(NodeKind kind) {
    return kind == NodeKind::KerrSpacetime ||
        kind == NodeKind::NovikovThorneDisk ||
        kind == NodeKind::SceneEnvironment ||
        kind == NodeKind::KerrRadiativeTransfer ||
        kind == NodeKind::BlackHoleOutput;
}

u64 PinId(u64 nodeId, u64 slot) {
    return nodeId * kPinStride + slot;
}

u64 ParameterOutputPin(const GraphNode& node) {
    return PinId(node.id, 1u);
}

u64 BaseColorInputPin(const GraphNode& node) {
    return PinId(node.id, 1u);
}

u64 MetallicInputPin(const GraphNode& node) {
    return PinId(node.id, 2u);
}

u64 RoughnessInputPin(const GraphNode& node) {
    return PinId(node.id, 3u);
}

u64 SpacetimeOutputPin(const GraphNode& node) {
    return PinId(node.id, 1u);
}

u64 DiskSpacetimeInputPin(const GraphNode& node) {
    return PinId(node.id, 1u);
}

u64 DiskMediumOutputPin(const GraphNode& node) {
    return PinId(node.id, 2u);
}

u64 EnvironmentOutputPin(const GraphNode& node) {
    return PinId(node.id, 1u);
}

u64 TransportSpacetimeInputPin(const GraphNode& node) {
    return PinId(node.id, 1u);
}

u64 TransportMediumInputPin(const GraphNode& node) {
    return PinId(node.id, 2u);
}

u64 TransportEnvironmentInputPin(const GraphNode& node) {
    return PinId(node.id, 3u);
}

u64 TransportRadianceOutputPin(const GraphNode& node) {
    return PinId(node.id, 4u);
}

u64 BlackHoleRadianceInputPin(const GraphNode& node) {
    return PinId(node.id, 1u);
}

f32 KerrIscoRadius(f32 spin) {
    const f32 clampedSpin = std::clamp(spin, -0.998f, 0.998f);
    const f32 z1 = 1.0f +
        std::cbrt(1.0f - clampedSpin * clampedSpin) *
        (std::cbrt(1.0f + clampedSpin) + std::cbrt(1.0f - clampedSpin));
    const f32 z2 = std::sqrt(3.0f * clampedSpin * clampedSpin + z1 * z1);
    const f32 direction = clampedSpin >= 0.0f ? 1.0f : -1.0f;
    return 3.0f + z2 - direction *
        std::sqrt((3.0f - z1) * (3.0f + z1 + 2.0f * z2));
}

bool NearlyEqual(f32 left, f32 right) {
    return std::abs(left - right) <= 0.0001f;
}

bool NearlyEqual(const glm::vec2& left, const glm::vec2& right) {
    return NearlyEqual(left.x, right.x) && NearlyEqual(left.y, right.y);
}

bool ReadVec2(const json& source, glm::vec2& value) {
    if (!source.is_array() || source.size() != 2u ||
        !source[0].is_number() || !source[1].is_number()) {
        return false;
    }
    value = { source[0].get<f32>(), source[1].get<f32>() };
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool ReadVec4(const json& source, glm::vec4& value) {
    if (!source.is_array() || source.size() != 4u) {
        return false;
    }
    for (u32 index = 0u; index < 4u; ++index) {
        if (!source[index].is_number()) {
            return false;
        }
        value[index] = source[index].get<f32>();
        if (!std::isfinite(value[index])) {
            return false;
        }
    }
    return true;
}

bool ReadFiniteFloat(const json& source, const char* name, f32& value) {
    if (!source.is_object() || !source.contains(name) ||
        !source[name].is_number()) {
        return false;
    }
    value = source[name].get<f32>();
    return std::isfinite(value);
}

bool ReadInteger(const json& source, const char* name, i32& value) {
    if (!source.is_object() || !source.contains(name) ||
        !source[name].is_number_integer()) {
        return false;
    }
    value = source[name].get<i32>();
    return true;
}

class MaterialGraphDocument {
public:
    void ResetPbr() {
        m_Nodes.clear();
        m_Links.clear();
        m_Target = GraphTarget::Surface;
        m_NextNodeId = 1u;
        m_NextLinkId = 1u;
        m_DefaultBaseColor = glm::vec4(0.82f, 0.82f, 0.84f, 1.0f);
        m_DefaultMetallic = 0.0f;
        m_DefaultRoughness = 0.5f;

        GraphNode output{};
        output.id = m_NextNodeId++;
        output.kind = NodeKind::PbrOutput;
        output.position = { 480.0f, 180.0f };
        const u64 outputId = output.id;
        m_Nodes.push_back(output);

        const u64 colorId = AddColor(m_DefaultBaseColor, { 80.0f, 80.0f });
        const u64 metallicId = AddFloat(m_DefaultMetallic, { 80.0f, 230.0f });
        const u64 roughnessId = AddFloat(m_DefaultRoughness, { 80.0f, 350.0f });
        Connect(PinId(colorId, 1u), PinId(outputId, 1u));
        Connect(PinId(metallicId, 1u), PinId(outputId, 2u));
        Connect(PinId(roughnessId, 1u), PinId(outputId, 3u));
        m_Revision = 1u;
    }

    void ResetBlackUnlit() {
        m_Nodes.clear();
        m_Links.clear();
        m_Target = GraphTarget::Surface;
        m_NextNodeId = 1u;
        m_NextLinkId = 1u;
        m_DefaultBaseColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        m_DefaultMetallic = 0.0f;
        m_DefaultRoughness = 1.0f;

        GraphNode output{};
        output.id = m_NextNodeId++;
        output.kind = NodeKind::UnlitOutput;
        output.position = { 480.0f, 180.0f };
        const u64 outputId = output.id;
        m_Nodes.push_back(output);

        const u64 colorId = AddColor(
            m_DefaultBaseColor,
            { 80.0f, 180.0f }
        );
        Connect(PinId(colorId, 1u), PinId(outputId, 1u));
        m_Revision = 1u;
    }

    void ResetPhysicalBlackHole() {
        m_Nodes.clear();
        m_Links.clear();
        m_Target = GraphTarget::RelativisticBlackHole;
        m_NextNodeId = 1u;
        m_NextLinkId = 1u;

        GraphNode spacetime{};
        spacetime.id = m_NextNodeId++;
        spacetime.kind = NodeKind::KerrSpacetime;
        spacetime.position = { 40.0f, 100.0f };
        const u64 spacetimeId = spacetime.id;
        m_Nodes.push_back(spacetime);

        GraphNode disk{};
        disk.id = m_NextNodeId++;
        disk.kind = NodeKind::NovikovThorneDisk;
        disk.position = { 310.0f, 40.0f };
        const u64 diskId = disk.id;
        m_Nodes.push_back(disk);

        GraphNode environment{};
        environment.id = m_NextNodeId++;
        environment.kind = NodeKind::SceneEnvironment;
        environment.position = { 310.0f, 390.0f };
        const u64 environmentId = environment.id;
        m_Nodes.push_back(environment);

        GraphNode transport{};
        transport.id = m_NextNodeId++;
        transport.kind = NodeKind::KerrRadiativeTransfer;
        transport.position = { 610.0f, 130.0f };
        const u64 transportId = transport.id;
        m_Nodes.push_back(transport);

        GraphNode output{};
        output.id = m_NextNodeId++;
        output.kind = NodeKind::BlackHoleOutput;
        output.position = { 930.0f, 220.0f };
        const u64 outputId = output.id;
        m_Nodes.push_back(output);

        Connect(
            SpacetimeOutputPin(*FindNode(spacetimeId)),
            DiskSpacetimeInputPin(*FindNode(diskId))
        );
        Connect(
            SpacetimeOutputPin(*FindNode(spacetimeId)),
            TransportSpacetimeInputPin(*FindNode(transportId))
        );
        Connect(
            DiskMediumOutputPin(*FindNode(diskId)),
            TransportMediumInputPin(*FindNode(transportId))
        );
        Connect(
            EnvironmentOutputPin(*FindNode(environmentId)),
            TransportEnvironmentInputPin(*FindNode(transportId))
        );
        Connect(
            TransportRadianceOutputPin(*FindNode(transportId)),
            BlackHoleRadianceInputPin(*FindNode(outputId))
        );
        m_Revision = 1u;
    }

    u64 AddColor(glm::vec4 value, glm::vec2 position) {
        GraphNode node{};
        node.id = m_NextNodeId++;
        node.kind = NodeKind::Color;
        node.position = position;
        node.color = glm::clamp(value, glm::vec4(0.0f), glm::vec4(1.0f));
        m_Nodes.push_back(node);
        TouchMaterial();
        return node.id;
    }

    u64 AddFloat(f32 value, glm::vec2 position) {
        GraphNode node{};
        node.id = m_NextNodeId++;
        node.kind = NodeKind::Float;
        node.position = position;
        node.value = std::clamp(value, 0.0f, 1.0f);
        m_Nodes.push_back(node);
        TouchMaterial();
        return node.id;
    }

    bool SetColor(u64 nodeId, const glm::vec4& value) {
        GraphNode* node = FindNode(nodeId);
        if (node == nullptr || node->kind != NodeKind::Color) {
            return false;
        }
        const glm::vec4 clamped = glm::clamp(
            value,
            glm::vec4(0.0f),
            glm::vec4(1.0f)
        );
        if (node->color == clamped) {
            return false;
        }
        node->color = clamped;
        TouchMaterial();
        return true;
    }

    bool SetFloat(u64 nodeId, f32 value) {
        GraphNode* node = FindNode(nodeId);
        if (node == nullptr || node->kind != NodeKind::Float) {
            return false;
        }
        const f32 clamped = std::clamp(value, 0.0f, 1.0f);
        if (NearlyEqual(node->value, clamped)) {
            return false;
        }
        node->value = clamped;
        TouchMaterial();
        return true;
    }

    bool CanConnect(u64 firstPin, u64 secondPin) const {
        const std::optional<PinInfo> first = DescribePin(firstPin);
        const std::optional<PinInfo> second = DescribePin(secondPin);
        if (!first.has_value() || !second.has_value() ||
            first->nodeId == second->nodeId ||
            first->direction == second->direction ||
            first->valueKind != second->valueKind) {
            return false;
        }
        return true;
    }

    bool Connect(u64 firstPin, u64 secondPin) {
        if (!CanConnect(firstPin, secondPin)) {
            return false;
        }
        const PinInfo first = *DescribePin(firstPin);
        const u64 outputPin = first.direction == PinDirection::Output
            ? firstPin
            : secondPin;
        const u64 inputPin = first.direction == PinDirection::Input
            ? firstPin
            : secondPin;

        m_Links.erase(
            std::remove_if(
                m_Links.begin(),
                m_Links.end(),
                [inputPin](const GraphLink& link) {
                    return link.inputPin == inputPin;
                }
            ),
            m_Links.end()
        );
        m_Links.push_back({ m_NextLinkId++, outputPin, inputPin });
        TouchMaterial();
        return true;
    }

    bool DeleteLink(u64 linkId) {
        const auto found = std::find_if(
            m_Links.begin(),
            m_Links.end(),
            [linkId](const GraphLink& link) { return link.id == linkId; }
        );
        if (found == m_Links.end()) {
            return false;
        }
        m_Links.erase(found);
        TouchMaterial();
        return true;
    }

    bool DeleteNode(u64 nodeId) {
        const GraphNode* node = FindNode(nodeId);
        if (node == nullptr || IsOutputNode(node->kind) ||
            (m_Target == GraphTarget::RelativisticBlackHole &&
                IsBlackHoleNode(node->kind))) {
            return false;
        }
        m_Nodes.erase(
            std::remove_if(
                m_Nodes.begin(),
                m_Nodes.end(),
                [nodeId](const GraphNode& candidate) {
                    return candidate.id == nodeId;
                }
            ),
            m_Nodes.end()
        );
        m_Links.erase(
            std::remove_if(
                m_Links.begin(),
                m_Links.end(),
                [this, nodeId](const GraphLink& link) {
                    const std::optional<PinInfo> output = DescribePin(link.outputPin);
                    const std::optional<PinInfo> input = DescribePin(link.inputPin);
                    return !output.has_value() || !input.has_value() ||
                        output->nodeId == nodeId || input->nodeId == nodeId;
                }
            ),
            m_Links.end()
        );
        TouchMaterial();
        return true;
    }

    void CapturePosition(u64 nodeId, glm::vec2 position) {
        GraphNode* node = FindNode(nodeId);
        if (node == nullptr || NearlyEqual(node->position, position)) {
            return;
        }
        node->position = position;
        ++m_Revision;
    }

    CompiledMaterialGraph Compile() const {
        CompiledMaterialGraph result{};
        result.target = m_Target;
        result.baseColor = m_DefaultBaseColor;
        result.metallic = m_DefaultMetallic;
        result.roughness = m_DefaultRoughness;
        if (!Validate(result.error)) {
            return result;
        }

        if (m_Target == GraphTarget::RelativisticBlackHole) {
            result.valid = true;
            return result;
        }

        const GraphNode* output = OutputNode();
        result.shadingModel = output->kind == NodeKind::UnlitOutput
            ? ShadingModel::Unlit
            : ShadingModel::LitPbr;
        for (const GraphLink& link : m_Links) {
            const PinInfo input = *DescribePin(link.inputPin);
            if (input.nodeId != output->id) {
                continue;
            }
            const PinInfo sourcePin = *DescribePin(link.outputPin);
            const GraphNode* source = FindNode(sourcePin.nodeId);
            if (link.inputPin == BaseColorInputPin(*output)) {
                result.baseColor = source->color;
            } else if (link.inputPin == MetallicInputPin(*output)) {
                result.metallic = source->value;
            } else if (link.inputPin == RoughnessInputPin(*output)) {
                result.roughness = std::clamp(source->value, 0.04f, 1.0f);
            }
        }
        result.valid = true;
        return result;
    }

    bool Save(const std::filesystem::path& path, std::string& status) const {
        std::string validationError;
        if (!Validate(validationError)) {
            status = "Save failed: " + validationError;
            return false;
        }

        json document;
        if (m_Target == GraphTarget::RelativisticBlackHole) {
            document = {
                { "format", kBlackHoleDocumentFormat },
                { "version", kBlackHoleDocumentVersion },
                { "target", "RelativisticBlackHole" },
                { "physicsContract", {
                    { "spacetime", "Kerr" },
                    { "lengthUnit", "GMOverC2" },
                    { "geodesics", "BackwardNull" },
                    { "diskModel", "NovikovThorneThinDisk" },
                    { "radiativeTransfer", "InvariantSpecificIntensity" }
                } },
                { "execution", {
                    { "requiredBackend", "KerrGeodesicV1" },
                    { "legacyFallbackAllowed", false },
                    { "unavailableBehavior", "Disabled" }
                } },
                { "nodes", json::array() },
                { "links", json::array() }
            };
        } else {
            const GraphNode* outputNode = OutputNode();
            const ShadingModel shadingModel =
                outputNode->kind == NodeKind::UnlitOutput
                    ? ShadingModel::Unlit
                    : ShadingModel::LitPbr;
            document = {
                { "format", kDocumentFormat },
                { "version", kDocumentVersion },
                { "target", "Surface" },
                { "shadingModel", ShadingModelName(shadingModel) },
                { "defaults", {
                    { "baseColor", {
                        m_DefaultBaseColor.r,
                        m_DefaultBaseColor.g,
                        m_DefaultBaseColor.b,
                        m_DefaultBaseColor.a
                    } },
                    { "metallic", m_DefaultMetallic },
                    { "roughness", m_DefaultRoughness }
                } },
                { "nodes", json::array() },
                { "links", json::array() }
            };
        }
        for (const GraphNode& node : m_Nodes) {
            json serialized = {
                { "id", node.id },
                { "type", NodeKindName(node.kind) },
                { "position", { node.position.x, node.position.y } }
            };
            if (node.kind == NodeKind::Color) {
                serialized["value"] = {
                    node.color.r, node.color.g, node.color.b, node.color.a
                };
            } else if (node.kind == NodeKind::Float) {
                serialized["value"] = node.value;
            } else if (node.kind == NodeKind::KerrSpacetime) {
                serialized["parameters"] = {
                    { "model", "Kerr" },
                    { "massSolar", node.massSolar },
                    { "dimensionlessSpin", node.dimensionlessSpin }
                };
            } else if (node.kind == NodeKind::NovikovThorneDisk) {
                serialized["parameters"] = {
                    { "model", "NovikovThorneThinDisk" },
                    { "innerEdge", "ISCO" },
                    { "outerRadiusRg", node.outerRadiusRg },
                    { "eddingtonRatio", node.eddingtonRatio },
                    { "colorCorrection", node.colorCorrection }
                };
            } else if (node.kind == NodeKind::SceneEnvironment) {
                serialized["parameters"] = {
                    { "source", "SceneEnvironment" },
                    { "intensity", node.environmentIntensity }
                };
            } else if (node.kind == NodeKind::KerrRadiativeTransfer) {
                serialized["parameters"] = {
                    { "integrator", "AdaptiveDormandPrince54" },
                    { "transfer", "InvariantSpecificIntensity" },
                    { "maxSteps", node.maxSteps },
                    { "relativeTolerance", node.relativeTolerance },
                    { "maxImageOrder", node.maxImageOrder },
                    { "spectralSamples", node.spectralSamples }
                };
            } else if (node.kind == NodeKind::BlackHoleOutput) {
                serialized["parameters"] = {
                    { "colorSpace", "LinearSceneRadiance" }
                };
            }
            document["nodes"].push_back(std::move(serialized));
        }
        for (const GraphLink& link : m_Links) {
            document["links"].push_back({
                { "id", link.id },
                { "fromPin", link.outputPin },
                { "toPin", link.inputPin }
            });
        }

        std::error_code error;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) {
                status = "Save failed: material directory could not be created.";
                return false;
            }
        }
        std::filesystem::path temporary = path;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::out | std::ios::trunc);
            if (!output) {
                status = "Save failed: material document could not be written.";
                return false;
            }
            output << document.dump(2) << '\n';
            if (!output) {
                std::filesystem::remove(temporary, error);
                status = "Save failed: material document write was interrupted.";
                return false;
            }
        }
        std::filesystem::rename(temporary, path, error);
        if (error) {
            error.clear();
            std::filesystem::remove(path, error);
            error.clear();
            std::filesystem::rename(temporary, path, error);
        }
        if (error) {
            std::filesystem::remove(temporary, error);
            status = "Save failed: material document could not be replaced.";
            return false;
        }
        status = "Saved " + path.generic_string();
        return true;
    }

    bool Load(const std::filesystem::path& path, std::string& status) {
        try {
            std::ifstream input(path);
            if (!input) {
                status = "Load failed: material document was not found.";
                return false;
            }
            const json source = json::parse(input);
            if (!source.is_object()) {
                status = "Load failed: unsupported graph document.";
                return false;
            }
            const u32 version = source.value("version", 0u);
            const std::string format = source.value("format", std::string{});
            const std::string target = source.value("target", std::string{});
            const bool legacyPbr =
                format == kDocumentFormat &&
                version == kLegacyPbrDocumentVersion &&
                target == "PbrSurface";
            const bool currentSurface =
                format == kDocumentFormat &&
                version == kDocumentVersion &&
                target == "Surface" &&
                source.contains("shadingModel") &&
                source["shadingModel"].is_string();
            const bool physicalBlackHole =
                format == kBlackHoleDocumentFormat &&
                version == kBlackHoleDocumentVersion &&
                target == "RelativisticBlackHole";
            if ((!legacyPbr && !currentSurface && !physicalBlackHole) ||
                !source.contains("nodes") || !source["nodes"].is_array() ||
                !source.contains("links") || !source["links"].is_array()) {
                status = "Load failed: unsupported graph document.";
                return false;
            }
            ShadingModel expectedShadingModel = ShadingModel::LitPbr;
            if (currentSurface) {
                const std::string shadingModel =
                    source["shadingModel"].get<std::string>();
                if (shadingModel == "Unlit") {
                    expectedShadingModel = ShadingModel::Unlit;
                } else if (shadingModel != "LitPBR") {
                    status = "Load failed: unsupported shading model.";
                    return false;
                }
            }

            MaterialGraphDocument parsed;
            parsed.m_Target = physicalBlackHole
                ? GraphTarget::RelativisticBlackHole
                : GraphTarget::Surface;
            if (physicalBlackHole) {
                const json physics = source.value("physicsContract", json{});
                const json execution = source.value("execution", json{});
                if (!physics.is_object() || !execution.is_object() ||
                    physics.value("spacetime", std::string{}) != "Kerr" ||
                    physics.value("lengthUnit", std::string{}) != "GMOverC2" ||
                    physics.value("geodesics", std::string{}) != "BackwardNull" ||
                    physics.value("diskModel", std::string{}) !=
                        "NovikovThorneThinDisk" ||
                    physics.value("radiativeTransfer", std::string{}) !=
                        "InvariantSpecificIntensity" ||
                    execution.value("requiredBackend", std::string{}) !=
                        "KerrGeodesicV1" ||
                    execution.value("legacyFallbackAllowed", true) ||
                    execution.value("unavailableBehavior", std::string{}) !=
                        "Disabled") {
                    status = "Load failed: unsupported physical contract.";
                    return false;
                }
            } else {
                if (!source.contains("defaults") ||
                    !source["defaults"].is_object()) {
                    status = "Load failed: material defaults are missing.";
                    return false;
                }
                const json& defaults = source["defaults"];
                if (!defaults.contains("baseColor") ||
                    !ReadVec4(defaults["baseColor"], parsed.m_DefaultBaseColor) ||
                    !defaults.contains("metallic") ||
                    !defaults["metallic"].is_number() ||
                    !defaults.contains("roughness") ||
                    !defaults["roughness"].is_number()) {
                    status = "Load failed: invalid material graph defaults.";
                    return false;
                }
                parsed.m_DefaultMetallic = defaults["metallic"].get<f32>();
                parsed.m_DefaultRoughness = defaults["roughness"].get<f32>();
                if (!std::isfinite(parsed.m_DefaultMetallic) ||
                    !std::isfinite(parsed.m_DefaultRoughness)) {
                    status = "Load failed: non-finite material graph defaults.";
                    return false;
                }
            }

            for (const json& serialized : source["nodes"]) {
                if (!serialized.is_object() ||
                    !serialized.contains("id") ||
                    !serialized["id"].is_number_unsigned() ||
                    !serialized.contains("type") ||
                    !serialized["type"].is_string() ||
                    !serialized.contains("position")) {
                    status = "Load failed: invalid material graph node.";
                    return false;
                }
                GraphNode node{};
                node.id = serialized["id"].get<u64>();
                const std::optional<NodeKind> kind = NodeKindFromName(
                    serialized["type"].get<std::string>()
                );
                if (node.id == 0u || !kind.has_value() ||
                    !ReadVec2(serialized["position"], node.position)) {
                    status = "Load failed: invalid material graph node state.";
                    return false;
                }
                node.kind = *kind;
                if (node.kind == NodeKind::Color) {
                    if (!serialized.contains("value") ||
                        !ReadVec4(serialized["value"], node.color)) {
                        status = "Load failed: invalid color node.";
                        return false;
                    }
                } else if (node.kind == NodeKind::Float) {
                    if (!serialized.contains("value") ||
                        !serialized["value"].is_number()) {
                        status = "Load failed: invalid float node.";
                        return false;
                    }
                    node.value = serialized["value"].get<f32>();
                    if (!std::isfinite(node.value)) {
                        status = "Load failed: non-finite float node.";
                        return false;
                    }
                } else if (IsBlackHoleNode(node.kind)) {
                    if (!serialized.contains("parameters") ||
                        !serialized["parameters"].is_object()) {
                        status = "Load failed: physical node parameters are missing.";
                        return false;
                    }
                    const json& parameters = serialized["parameters"];
                    if (node.kind == NodeKind::KerrSpacetime) {
                        if (parameters.value("model", std::string{}) != "Kerr" ||
                            !ReadFiniteFloat(
                                parameters,
                                "massSolar",
                                node.massSolar
                            ) ||
                            !ReadFiniteFloat(
                                parameters,
                                "dimensionlessSpin",
                                node.dimensionlessSpin
                            )) {
                            status = "Load failed: invalid Kerr spacetime node.";
                            return false;
                        }
                    } else if (node.kind == NodeKind::NovikovThorneDisk) {
                        if (parameters.value("model", std::string{}) !=
                                "NovikovThorneThinDisk" ||
                            parameters.value("innerEdge", std::string{}) != "ISCO" ||
                            !ReadFiniteFloat(
                                parameters,
                                "outerRadiusRg",
                                node.outerRadiusRg
                            ) ||
                            !ReadFiniteFloat(
                                parameters,
                                "eddingtonRatio",
                                node.eddingtonRatio
                            ) ||
                            !ReadFiniteFloat(
                                parameters,
                                "colorCorrection",
                                node.colorCorrection
                            )) {
                            status = "Load failed: invalid accretion disk node.";
                            return false;
                        }
                    } else if (node.kind == NodeKind::SceneEnvironment) {
                        if (parameters.value("source", std::string{}) !=
                                "SceneEnvironment" ||
                            !ReadFiniteFloat(
                                parameters,
                                "intensity",
                                node.environmentIntensity
                            )) {
                            status = "Load failed: invalid environment node.";
                            return false;
                        }
                    } else if (node.kind == NodeKind::KerrRadiativeTransfer) {
                        if (parameters.value("integrator", std::string{}) !=
                                "AdaptiveDormandPrince54" ||
                            parameters.value("transfer", std::string{}) !=
                                "InvariantSpecificIntensity" ||
                            !ReadInteger(parameters, "maxSteps", node.maxSteps) ||
                            !ReadFiniteFloat(
                                parameters,
                                "relativeTolerance",
                                node.relativeTolerance
                            ) ||
                            !ReadInteger(
                                parameters,
                                "maxImageOrder",
                                node.maxImageOrder
                            ) ||
                            !ReadInteger(
                                parameters,
                                "spectralSamples",
                                node.spectralSamples
                            )) {
                            status = "Load failed: invalid radiative transfer node.";
                            return false;
                        }
                    } else if (parameters.value("colorSpace", std::string{}) !=
                            "LinearSceneRadiance") {
                        status = "Load failed: invalid black-hole output node.";
                        return false;
                    }
                }
                parsed.m_NextNodeId = std::max(parsed.m_NextNodeId, node.id + 1u);
                parsed.m_Nodes.push_back(node);
            }
            for (const json& serialized : source["links"]) {
                if (!serialized.is_object() ||
                    !serialized.contains("id") ||
                    !serialized["id"].is_number_unsigned() ||
                    !serialized.contains("fromPin") ||
                    !serialized["fromPin"].is_number_unsigned() ||
                    !serialized.contains("toPin") ||
                    !serialized["toPin"].is_number_unsigned()) {
                    status = "Load failed: invalid material graph link.";
                    return false;
                }
                GraphLink link{
                    serialized["id"].get<u64>(),
                    serialized["fromPin"].get<u64>(),
                    serialized["toPin"].get<u64>()
                };
                parsed.m_NextLinkId = std::max(parsed.m_NextLinkId, link.id + 1u);
                parsed.m_Links.push_back(link);
            }
            std::string validationError;
            if (!parsed.Validate(validationError)) {
                status = "Load failed: " + validationError;
                return false;
            }
            const CompiledMaterialGraph compiled = parsed.Compile();
            if (parsed.m_Target == GraphTarget::Surface &&
                compiled.shadingModel != expectedShadingModel) {
                status = "Load failed: shading model and output node disagree.";
                return false;
            }
            parsed.m_Revision = m_Revision + 1u;
            *this = std::move(parsed);
            status = "Loaded " + path.generic_string();
            return true;
        } catch (const std::exception& error) {
            status = std::string("Load failed: ") + error.what();
            return false;
        }
    }

    std::vector<GraphNode>& Nodes() { return m_Nodes; }
    const std::vector<GraphNode>& Nodes() const { return m_Nodes; }
    const std::vector<GraphLink>& Links() const { return m_Links; }
    GraphTarget Target() const { return m_Target; }
    u64 Revision() const { return m_Revision; }

    f32 ResolvedIscoRadius() const {
        const auto found = std::find_if(
            m_Nodes.begin(),
            m_Nodes.end(),
            [](const GraphNode& node) {
                return node.kind == NodeKind::KerrSpacetime;
            }
        );
        return found != m_Nodes.end()
            ? KerrIscoRadius(found->dimensionlessSpin)
            : 6.0f;
    }

    void NotifyNodeEdited() { TouchMaterial(); }

private:
    GraphNode* FindNode(u64 nodeId) {
        const auto found = std::find_if(
            m_Nodes.begin(),
            m_Nodes.end(),
            [nodeId](const GraphNode& node) { return node.id == nodeId; }
        );
        return found != m_Nodes.end() ? &*found : nullptr;
    }

    const GraphNode* FindNode(u64 nodeId) const {
        const auto found = std::find_if(
            m_Nodes.begin(),
            m_Nodes.end(),
            [nodeId](const GraphNode& node) { return node.id == nodeId; }
        );
        return found != m_Nodes.end() ? &*found : nullptr;
    }

    const GraphNode* OutputNode() const {
        const auto found = std::find_if(
            m_Nodes.begin(),
            m_Nodes.end(),
            [](const GraphNode& node) {
                return IsOutputNode(node.kind);
            }
        );
        return found != m_Nodes.end() ? &*found : nullptr;
    }

    std::optional<PinInfo> DescribePin(u64 pinId) const {
        for (const GraphNode& node : m_Nodes) {
            if (node.kind == NodeKind::Color &&
                pinId == ParameterOutputPin(node)) {
                return PinInfo{ node.id, ValueKind::Color, PinDirection::Output };
            }
            if (node.kind == NodeKind::Float &&
                pinId == ParameterOutputPin(node)) {
                return PinInfo{ node.id, ValueKind::Float, PinDirection::Output };
            }
            if (node.kind == NodeKind::PbrOutput) {
                if (pinId == BaseColorInputPin(node)) {
                    return PinInfo{ node.id, ValueKind::Color, PinDirection::Input };
                }
                if (pinId == MetallicInputPin(node) ||
                    pinId == RoughnessInputPin(node)) {
                    return PinInfo{ node.id, ValueKind::Float, PinDirection::Input };
                }
            }
            if (node.kind == NodeKind::UnlitOutput &&
                pinId == BaseColorInputPin(node)) {
                return PinInfo{ node.id, ValueKind::Color, PinDirection::Input };
            }
            if (node.kind == NodeKind::KerrSpacetime &&
                pinId == SpacetimeOutputPin(node)) {
                return PinInfo{
                    node.id,
                    ValueKind::Spacetime,
                    PinDirection::Output
                };
            }
            if (node.kind == NodeKind::NovikovThorneDisk) {
                if (pinId == DiskSpacetimeInputPin(node)) {
                    return PinInfo{
                        node.id,
                        ValueKind::Spacetime,
                        PinDirection::Input
                    };
                }
                if (pinId == DiskMediumOutputPin(node)) {
                    return PinInfo{
                        node.id,
                        ValueKind::AccretionMedium,
                        PinDirection::Output
                    };
                }
            }
            if (node.kind == NodeKind::SceneEnvironment &&
                pinId == EnvironmentOutputPin(node)) {
                return PinInfo{
                    node.id,
                    ValueKind::EnvironmentRadiance,
                    PinDirection::Output
                };
            }
            if (node.kind == NodeKind::KerrRadiativeTransfer) {
                if (pinId == TransportSpacetimeInputPin(node)) {
                    return PinInfo{
                        node.id,
                        ValueKind::Spacetime,
                        PinDirection::Input
                    };
                }
                if (pinId == TransportMediumInputPin(node)) {
                    return PinInfo{
                        node.id,
                        ValueKind::AccretionMedium,
                        PinDirection::Input
                    };
                }
                if (pinId == TransportEnvironmentInputPin(node)) {
                    return PinInfo{
                        node.id,
                        ValueKind::EnvironmentRadiance,
                        PinDirection::Input
                    };
                }
                if (pinId == TransportRadianceOutputPin(node)) {
                    return PinInfo{
                        node.id,
                        ValueKind::RelativisticRadiance,
                        PinDirection::Output
                    };
                }
            }
            if (node.kind == NodeKind::BlackHoleOutput &&
                pinId == BlackHoleRadianceInputPin(node)) {
                return PinInfo{
                    node.id,
                    ValueKind::RelativisticRadiance,
                    PinDirection::Input
                };
            }
        }
        return std::nullopt;
    }

    bool Validate(std::string& error) const {
        std::unordered_set<u64> nodeIds;
        u32 outputCount = 0u;
        u32 kerrCount = 0u;
        u32 diskCount = 0u;
        u32 environmentCount = 0u;
        u32 transportCount = 0u;
        u32 blackHoleOutputCount = 0u;
        for (const GraphNode& node : m_Nodes) {
            if (node.id == 0u || !nodeIds.insert(node.id).second ||
                !std::isfinite(node.position.x) ||
                !std::isfinite(node.position.y)) {
                error = "node identities are invalid.";
                return false;
            }
            outputCount += IsOutputNode(node.kind) ? 1u : 0u;
            kerrCount += node.kind == NodeKind::KerrSpacetime ? 1u : 0u;
            diskCount += node.kind == NodeKind::NovikovThorneDisk ? 1u : 0u;
            environmentCount +=
                node.kind == NodeKind::SceneEnvironment ? 1u : 0u;
            transportCount +=
                node.kind == NodeKind::KerrRadiativeTransfer ? 1u : 0u;
            blackHoleOutputCount +=
                node.kind == NodeKind::BlackHoleOutput ? 1u : 0u;
        }
        if (outputCount != 1u) {
            error = "exactly one graph output node is required.";
            return false;
        }

        if (m_Target == GraphTarget::Surface) {
            if (kerrCount != 0u || diskCount != 0u ||
                environmentCount != 0u || transportCount != 0u ||
                blackHoleOutputCount != 0u) {
                error = "surface graphs cannot contain black-hole nodes.";
                return false;
            }
        } else {
            if (m_Nodes.size() != 5u || kerrCount != 1u || diskCount != 1u ||
                environmentCount != 1u || transportCount != 1u ||
                blackHoleOutputCount != 1u) {
                error = "the physical black-hole graph requires one node of each stage.";
                return false;
            }
        }

        std::unordered_set<u64> linkIds;
        std::unordered_set<u64> connectedInputs;
        for (const GraphLink& link : m_Links) {
            const std::optional<PinInfo> output = DescribePin(link.outputPin);
            const std::optional<PinInfo> input = DescribePin(link.inputPin);
            if (link.id == 0u || !linkIds.insert(link.id).second ||
                !output.has_value() || !input.has_value() ||
                output->direction != PinDirection::Output ||
                input->direction != PinDirection::Input ||
                output->valueKind != input->valueKind ||
                !connectedInputs.insert(link.inputPin).second) {
                error = "links are invalid or type-incompatible.";
                return false;
            }
        }

        if (m_Target == GraphTarget::RelativisticBlackHole) {
            const GraphNode* spacetime = nullptr;
            const GraphNode* disk = nullptr;
            const GraphNode* environment = nullptr;
            const GraphNode* transport = nullptr;
            const GraphNode* output = nullptr;
            for (const GraphNode& node : m_Nodes) {
                switch (node.kind) {
                    case NodeKind::KerrSpacetime:
                        spacetime = &node;
                        break;
                    case NodeKind::NovikovThorneDisk:
                        disk = &node;
                        break;
                    case NodeKind::SceneEnvironment:
                        environment = &node;
                        break;
                    case NodeKind::KerrRadiativeTransfer:
                        transport = &node;
                        break;
                    case NodeKind::BlackHoleOutput:
                        output = &node;
                        break;
                    default:
                        error = "the black-hole graph contains a surface node.";
                        return false;
                }
            }
            if (!std::isfinite(spacetime->massSolar) ||
                spacetime->massSolar < 0.001f ||
                spacetime->massSolar > 1.0e11f ||
                !std::isfinite(spacetime->dimensionlessSpin) ||
                spacetime->dimensionlessSpin < -0.998f ||
                spacetime->dimensionlessSpin > 0.998f) {
                error = "Kerr mass or spin is outside the supported physical range.";
                return false;
            }
            const f32 iscoRadius = KerrIscoRadius(
                spacetime->dimensionlessSpin
            );
            if (!std::isfinite(disk->outerRadiusRg) ||
                disk->outerRadiusRg <= iscoRadius + 1.0f ||
                disk->outerRadiusRg > 1000.0f ||
                !std::isfinite(disk->eddingtonRatio) ||
                disk->eddingtonRatio < 0.001f ||
                disk->eddingtonRatio > 0.3f ||
                !std::isfinite(disk->colorCorrection) ||
                disk->colorCorrection < 1.0f ||
                disk->colorCorrection > 3.0f) {
                error = "the thin-disk parameters are outside their model domain.";
                return false;
            }
            if (!std::isfinite(environment->environmentIntensity) ||
                environment->environmentIntensity < 0.0f ||
                environment->environmentIntensity > 16.0f) {
                error = "environment radiance intensity is invalid.";
                return false;
            }
            if (transport->maxSteps < 64 || transport->maxSteps > 4096 ||
                !std::isfinite(transport->relativeTolerance) ||
                transport->relativeTolerance < 1.0e-7f ||
                transport->relativeTolerance > 1.0e-3f ||
                transport->maxImageOrder < 1 ||
                transport->maxImageOrder > 4 ||
                transport->spectralSamples < 3 ||
                transport->spectralSamples > 64) {
                error = "radiative-transfer quality is outside the bounded range.";
                return false;
            }

            const auto hasLink = [this](u64 from, u64 to) {
                return std::any_of(
                    m_Links.begin(),
                    m_Links.end(),
                    [from, to](const GraphLink& link) {
                        return link.outputPin == from && link.inputPin == to;
                    }
                );
            };
            const bool completeTopology = m_Links.size() == 5u &&
                hasLink(
                    SpacetimeOutputPin(*spacetime),
                    DiskSpacetimeInputPin(*disk)
                ) &&
                hasLink(
                    SpacetimeOutputPin(*spacetime),
                    TransportSpacetimeInputPin(*transport)
                ) &&
                hasLink(
                    DiskMediumOutputPin(*disk),
                    TransportMediumInputPin(*transport)
                ) &&
                hasLink(
                    EnvironmentOutputPin(*environment),
                    TransportEnvironmentInputPin(*transport)
                ) &&
                hasLink(
                    TransportRadianceOutputPin(*transport),
                    BlackHoleRadianceInputPin(*output)
                );
            if (!completeTopology) {
                error = "the physical stages are not connected in the required order.";
                return false;
            }
        }
        error.clear();
        return true;
    }

    void TouchMaterial() {
        ++m_Revision;
    }

    std::vector<GraphNode> m_Nodes;
    std::vector<GraphLink> m_Links;
    glm::vec4 m_DefaultBaseColor{ 1.0f };
    f32 m_DefaultMetallic = 0.0f;
    f32 m_DefaultRoughness = 0.5f;
    GraphTarget m_Target = GraphTarget::Surface;
    u64 m_NextNodeId = 1u;
    u64 m_NextLinkId = 1u;
    u64 m_Revision = 0u;
};

}

struct MaterialGraphEditor::Impl {
    nodeEditor::EditorContext* context = nullptr;
    MaterialGraphDocument document;
    MaterialGraphRuntimeStats stats{};
    std::filesystem::path path = MaterialGraphEditor::DefaultDocumentPath();
    std::string status = "New PBR material graph.";

    void EnsureContext() {
        if (context != nullptr) {
            return;
        }
        nodeEditor::Config config{};
        config.SettingsFile = nullptr;
        context = nodeEditor::CreateEditor(&config);
    }

    void ResetContext() {
        if (context != nullptr) {
            nodeEditor::SetCurrentEditor(nullptr);
            nodeEditor::DestroyEditor(context);
            context = nullptr;
        }
        EnsureContext();
        for (GraphNode& node : document.Nodes()) {
            node.restorePosition = true;
        }
    }

    void NewPbrDocument() {
        document.ResetPbr();
        path = MaterialGraphEditor::DefaultDocumentPath();
        ResetContext();
        status = "New PBR material graph.";
    }

    void NewBlackUnlitDocument() {
        document.ResetBlackUnlit();
        path = MaterialGraphEditor::DefaultDocumentPath();
        ResetContext();
        status = "New black unlit material graph.";
    }

    void NewPhysicalBlackHoleDocument() {
        document.ResetPhysicalBlackHole();
        path = MaterialGraphEditor::PhysicalBlackHoleDocumentPath();
        ResetContext();
        status = "New constrained Kerr black-hole graph.";
    }

    void DrawNode(GraphNode& node) {
        nodeEditor::BeginNode(nodeEditor::NodeId(node.id));
        ImGui::PushID(static_cast<int>(node.id));
        if (node.kind == NodeKind::Color) {
            ImGui::TextUnformatted("Color");
            glm::vec4 edited = node.color;
            if (ImGui::ColorEdit4(
                    "##Color",
                    &edited.x,
                    ImGuiColorEditFlags_NoInputs
                )) {
                document.SetColor(node.id, edited);
            }
            ImGui::SameLine();
            nodeEditor::BeginPin(
                nodeEditor::PinId(ParameterOutputPin(node)),
                nodeEditor::PinKind::Output
            );
            ImGui::TextUnformatted("Color >");
            nodeEditor::EndPin();
        } else if (node.kind == NodeKind::Float) {
            ImGui::TextUnformatted("Float");
            f32 edited = node.value;
            if (ImGui::DragFloat("##Float", &edited, 0.01f, 0.0f, 1.0f)) {
                document.SetFloat(node.id, edited);
            }
            ImGui::SameLine();
            nodeEditor::BeginPin(
                nodeEditor::PinId(ParameterOutputPin(node)),
                nodeEditor::PinKind::Output
            );
            ImGui::TextUnformatted("Value >");
            nodeEditor::EndPin();
        } else if (node.kind == NodeKind::PbrOutput) {
            ImGui::TextUnformatted("PBR Material Output");
            nodeEditor::BeginPin(
                nodeEditor::PinId(BaseColorInputPin(node)),
                nodeEditor::PinKind::Input
            );
            ImGui::TextUnformatted("> Base Color");
            nodeEditor::EndPin();
            nodeEditor::BeginPin(
                nodeEditor::PinId(MetallicInputPin(node)),
                nodeEditor::PinKind::Input
            );
            ImGui::TextUnformatted("> Metallic");
            nodeEditor::EndPin();
            nodeEditor::BeginPin(
                nodeEditor::PinId(RoughnessInputPin(node)),
                nodeEditor::PinKind::Input
            );
            ImGui::TextUnformatted("> Roughness");
            nodeEditor::EndPin();
        } else if (node.kind == NodeKind::UnlitOutput) {
            ImGui::TextUnformatted("Unlit Material Output");
            nodeEditor::BeginPin(
                nodeEditor::PinId(BaseColorInputPin(node)),
                nodeEditor::PinKind::Input
            );
            ImGui::TextUnformatted("> Color");
            nodeEditor::EndPin();
        } else if (node.kind == NodeKind::KerrSpacetime) {
            ImGui::TextUnformatted("Kerr Spacetime");
            bool edited = false;
            ImGui::PushItemWidth(170.0f);
            edited |= ImGui::DragFloat(
                "Mass (solar)",
                &node.massSolar,
                0.1f,
                0.001f,
                1.0e11f,
                "%.6g",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic
            );
            edited |= ImGui::DragFloat(
                "Spin a*",
                &node.dimensionlessSpin,
                0.001f,
                -0.998f,
                0.998f,
                "%.3f",
                ImGuiSliderFlags_AlwaysClamp
            );
            ImGui::PopItemWidth();
            if (edited) {
                document.NotifyNodeEdited();
            }
            nodeEditor::BeginPin(
                nodeEditor::PinId(SpacetimeOutputPin(node)),
                nodeEditor::PinKind::Output
            );
            ImGui::TextUnformatted("Spacetime >");
            nodeEditor::EndPin();
        } else if (node.kind == NodeKind::NovikovThorneDisk) {
            ImGui::TextUnformatted("Novikov-Thorne Thin Disk");
            nodeEditor::BeginPin(
                nodeEditor::PinId(DiskSpacetimeInputPin(node)),
                nodeEditor::PinKind::Input
            );
            ImGui::TextUnformatted("> Spacetime");
            nodeEditor::EndPin();
            ImGui::Text("ISCO %.3f rg", document.ResolvedIscoRadius());
            bool edited = false;
            const f32 minimumOuterRadius =
                document.ResolvedIscoRadius() + 1.01f;
            ImGui::PushItemWidth(170.0f);
            edited |= ImGui::DragFloat(
                "Outer radius (rg)",
                &node.outerRadiusRg,
                0.25f,
                minimumOuterRadius,
                1000.0f,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic
            );
            edited |= ImGui::DragFloat(
                "Eddington ratio",
                &node.eddingtonRatio,
                0.001f,
                0.001f,
                0.3f,
                "%.3f",
                ImGuiSliderFlags_AlwaysClamp
            );
            edited |= ImGui::DragFloat(
                "Color correction",
                &node.colorCorrection,
                0.01f,
                1.0f,
                3.0f,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp
            );
            ImGui::PopItemWidth();
            if (edited) {
                document.NotifyNodeEdited();
            }
            nodeEditor::BeginPin(
                nodeEditor::PinId(DiskMediumOutputPin(node)),
                nodeEditor::PinKind::Output
            );
            ImGui::TextUnformatted("Disk medium >");
            nodeEditor::EndPin();
        } else if (node.kind == NodeKind::SceneEnvironment) {
            ImGui::TextUnformatted("Scene Environment");
            ImGui::PushItemWidth(150.0f);
            const bool edited = ImGui::DragFloat(
                "Intensity",
                &node.environmentIntensity,
                0.01f,
                0.0f,
                16.0f,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp
            );
            ImGui::PopItemWidth();
            if (edited) {
                document.NotifyNodeEdited();
            }
            nodeEditor::BeginPin(
                nodeEditor::PinId(EnvironmentOutputPin(node)),
                nodeEditor::PinKind::Output
            );
            ImGui::TextUnformatted("Background radiance >");
            nodeEditor::EndPin();
        } else if (node.kind == NodeKind::KerrRadiativeTransfer) {
            ImGui::TextUnformatted("Kerr Radiative Transfer");
            nodeEditor::BeginPin(
                nodeEditor::PinId(TransportSpacetimeInputPin(node)),
                nodeEditor::PinKind::Input
            );
            ImGui::TextUnformatted("> Spacetime");
            nodeEditor::EndPin();
            nodeEditor::BeginPin(
                nodeEditor::PinId(TransportMediumInputPin(node)),
                nodeEditor::PinKind::Input
            );
            ImGui::TextUnformatted("> Disk medium");
            nodeEditor::EndPin();
            nodeEditor::BeginPin(
                nodeEditor::PinId(TransportEnvironmentInputPin(node)),
                nodeEditor::PinKind::Input
            );
            ImGui::TextUnformatted("> Background radiance");
            nodeEditor::EndPin();
            bool edited = false;
            ImGui::PushItemWidth(150.0f);
            edited |= ImGui::DragInt(
                "Max steps",
                &node.maxSteps,
                4.0f,
                64,
                4096,
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            );
            edited |= ImGui::DragFloat(
                "Relative tolerance",
                &node.relativeTolerance,
                0.000001f,
                0.0000001f,
                0.001f,
                "%.1e",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic
            );
            edited |= ImGui::DragInt(
                "Image order",
                &node.maxImageOrder,
                0.05f,
                1,
                4,
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            );
            edited |= ImGui::DragInt(
                "Spectral samples",
                &node.spectralSamples,
                1.0f,
                3,
                64,
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            );
            ImGui::PopItemWidth();
            if (edited) {
                document.NotifyNodeEdited();
            }
            nodeEditor::BeginPin(
                nodeEditor::PinId(TransportRadianceOutputPin(node)),
                nodeEditor::PinKind::Output
            );
            ImGui::TextUnformatted("Relativistic radiance >");
            nodeEditor::EndPin();
        } else {
            ImGui::TextUnformatted("Black Hole Output");
            nodeEditor::BeginPin(
                nodeEditor::PinId(BlackHoleRadianceInputPin(node)),
                nodeEditor::PinKind::Input
            );
            ImGui::TextUnformatted("> Linear scene radiance");
            nodeEditor::EndPin();
        }
        ImGui::PopID();
        nodeEditor::EndNode();
        if (node.restorePosition) {
            nodeEditor::SetNodePosition(
                nodeEditor::NodeId(node.id),
                ImVec2(node.position.x, node.position.y)
            );
            node.restorePosition = false;
        }
    }

    void DrawGraph() {
        EnsureContext();
        nodeEditor::SetCurrentEditor(context);
        nodeEditor::Begin("MaterialGraphCanvas");

        for (GraphNode& node : document.Nodes()) {
            DrawNode(node);
        }
        for (const GraphLink& link : document.Links()) {
            nodeEditor::Link(
                nodeEditor::LinkId(link.id),
                nodeEditor::PinId(link.outputPin),
                nodeEditor::PinId(link.inputPin)
            );
        }

        if (nodeEditor::BeginCreate()) {
            nodeEditor::PinId first;
            nodeEditor::PinId second;
            if (nodeEditor::QueryNewLink(&first, &second) && first && second) {
                if (document.CanConnect(first.Get(), second.Get())) {
                    if (nodeEditor::AcceptNewItem()) {
                        document.Connect(first.Get(), second.Get());
                    }
                } else {
                    nodeEditor::RejectNewItem(ImVec4(0.9f, 0.25f, 0.2f, 1.0f));
                }
            }
        }
        nodeEditor::EndCreate();

        if (nodeEditor::BeginDelete()) {
            nodeEditor::LinkId deletedLink;
            while (nodeEditor::QueryDeletedLink(&deletedLink)) {
                if (nodeEditor::AcceptDeletedItem()) {
                    document.DeleteLink(deletedLink.Get());
                }
            }
            nodeEditor::NodeId deletedNode;
            while (nodeEditor::QueryDeletedNode(&deletedNode)) {
                const auto found = std::find_if(
                    document.Nodes().begin(),
                    document.Nodes().end(),
                    [id = deletedNode.Get()](const GraphNode& node) {
                        return node.id == id;
                    }
                );
                const bool canDelete = found != document.Nodes().end() &&
                    !IsOutputNode(found->kind) &&
                    !(document.Target() == GraphTarget::RelativisticBlackHole &&
                        IsBlackHoleNode(found->kind));
                if (canDelete && nodeEditor::AcceptDeletedItem()) {
                    document.DeleteNode(deletedNode.Get());
                } else {
                    nodeEditor::RejectDeletedItem();
                }
            }
            nodeEditor::EndDelete();
        }

        nodeEditor::End();
        for (const GraphNode& node : document.Nodes()) {
            const ImVec2 position = nodeEditor::GetNodePosition(
                nodeEditor::NodeId(node.id)
            );
            document.CapturePosition(node.id, { position.x, position.y });
        }
        nodeEditor::SetCurrentEditor(nullptr);
    }

    void UpdateStats() {
        stats.nodeCount = static_cast<u32>(document.Nodes().size());
        stats.linkCount = static_cast<u32>(document.Links().size());
        stats.revision = document.Revision();
        const CompiledMaterialGraph compiled = document.Compile();
        stats.valid = compiled.valid ? 1u : 0u;
    }
};

MaterialGraphEditor::MaterialGraphEditor()
    : m_Impl(std::make_unique<Impl>()) {
    if (m_Impl->document.Load(m_Impl->path, m_Impl->status)) {
        ++m_Impl->stats.loadCount;
        m_Impl->ResetContext();
    } else {
        m_Impl->NewPbrDocument();
    }
    m_Impl->UpdateStats();
}

MaterialGraphEditor::~MaterialGraphEditor() {
    Shutdown();
}

void MaterialGraphEditor::Draw() {
    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("Material Graph Workspace", nullptr, windowFlags)) {
        ImGui::End();
        m_Impl->UpdateStats();
        return;
    }

    const bool newRequested = ImGui::Button("New PBR##MaterialGraph") ||
        (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false));
    if (newRequested) {
        m_Impl->NewPbrDocument();
    }
    ImGui::SameLine();
    const bool blackUnlitRequested =
        ImGui::Button("Black Unlit##MaterialGraph") ||
        (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B, false));
    if (blackUnlitRequested) {
        m_Impl->NewBlackUnlitDocument();
    }
    ImGui::SameLine();
    const bool blackHoleRequested =
        ImGui::Button("Kerr Black Hole##MaterialGraph") ||
        (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K, false));
    if (blackHoleRequested) {
        m_Impl->NewPhysicalBlackHoleDocument();
    }
    ImGui::SameLine();
    const bool openRequested = ImGui::Button("Open##MaterialGraph") ||
        (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false));
    if (openRequested) {
        if (m_Impl->document.Load(m_Impl->path, m_Impl->status)) {
            ++m_Impl->stats.loadCount;
            m_Impl->ResetContext();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Black Hole##MaterialGraph")) {
        const std::filesystem::path blackHolePath =
            MaterialGraphEditor::PhysicalBlackHoleDocumentPath();
        if (m_Impl->document.Load(blackHolePath, m_Impl->status)) {
            m_Impl->path = blackHolePath;
            ++m_Impl->stats.loadCount;
            m_Impl->ResetContext();
        }
    }
    ImGui::SameLine();
    const bool saveRequested = ImGui::Button("Save##MaterialGraph") ||
        (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false));
    if (saveRequested && m_Impl->document.Save(m_Impl->path, m_Impl->status)) {
        ++m_Impl->stats.saveCount;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(
        m_Impl->document.Target() == GraphTarget::RelativisticBlackHole
    );
    if (ImGui::Button("+ Color##MaterialGraph")) {
        const f32 offset = static_cast<f32>(m_Impl->document.Nodes().size()) * 24.0f;
        m_Impl->document.AddColor(
            glm::vec4(1.0f),
            { 120.0f + offset, 120.0f + offset }
        );
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Float##MaterialGraph")) {
        const f32 offset = static_cast<f32>(m_Impl->document.Nodes().size()) * 24.0f;
        m_Impl->document.AddFloat(
            0.5f,
            { 150.0f + offset, 150.0f + offset }
        );
    }
    ImGui::EndDisabled();

    const CompiledMaterialGraph compiled = m_Impl->document.Compile();
    if (compiled.target == GraphTarget::RelativisticBlackHole) {
        ImGui::Text(
            "Nodes %u  Links %u  Relativistic Kerr graph %s",
            static_cast<u32>(m_Impl->document.Nodes().size()),
            static_cast<u32>(m_Impl->document.Links().size()),
            compiled.valid ? "valid" : "invalid"
        );
        ImGui::SameLine();
        ImGui::TextDisabled("Authoring contract only; runtime backend pending");
    } else {
        ImGui::Text(
            "Nodes %u  Links %u  %s output %s",
            static_cast<u32>(m_Impl->document.Nodes().size()),
            static_cast<u32>(m_Impl->document.Links().size()),
            ShadingModelName(compiled.shadingModel),
            compiled.valid ? "valid" : "invalid"
        );
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_Impl->path.generic_string().c_str());
    ImGui::TextWrapped("%s", m_Impl->status.c_str());
    ImGui::Separator();
    m_Impl->DrawGraph();
    ImGui::End();
    m_Impl->UpdateStats();
}

void MaterialGraphEditor::Shutdown() {
    if (!m_Impl || m_Impl->context == nullptr) {
        return;
    }
    nodeEditor::SetCurrentEditor(nullptr);
    nodeEditor::DestroyEditor(m_Impl->context);
    m_Impl->context = nullptr;
}

const MaterialGraphRuntimeStats& MaterialGraphEditor::Stats() const {
    return m_Impl->stats;
}

std::filesystem::path MaterialGraphEditor::DefaultDocumentPath() {
#if defined(SE_ASSET_DIR)
    return std::filesystem::path(SE_ASSET_DIR) /
        "materials" / "scene_material.material.json";
#else
    return std::filesystem::path("assets") /
        "materials" / "scene_material.material.json";
#endif
}

std::filesystem::path MaterialGraphEditor::PhysicalBlackHoleDocumentPath() {
#if defined(SE_ASSET_DIR)
    return std::filesystem::path(SE_ASSET_DIR) /
        "materials" / "physical_black_hole.material.json";
#else
    return std::filesystem::path("assets") /
        "materials" / "physical_black_hole.material.json";
#endif
}

}
