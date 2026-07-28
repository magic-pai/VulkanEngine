#pragma once

#include "core.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace se {

// A renderer-owned, metadata-only snapshot of the completed frame. It has no
// GPU readback or Vulkan object addresses; it is the stable seam consumed by
// the Scene Builder runtime monitor.
struct RendererMonitorSessionSnapshot {
    std::string deviceName;
    std::string buildConfiguration;
    u32 vendorId = 0;
    u32 deviceId = 0;
    u32 driverVersion = 0;
    u32 apiVersion = 0;
    u32 graphicsQueueFamily = 0;
    u32 presentQueueFamily = 0;
    u32 rayQueryHardwareReady = 0;
    u32 accelerationStructureSupported = 0;
    u32 rayTracingPipelineSupported = 0;
};

struct RendererMonitorFrameContext {
    u32 swapchainImageIndex = 0;
    u32 displayWidth = 0;
    u32 displayHeight = 0;
    u32 renderWidth = 0;
    u32 renderHeight = 0;
    u32 swapchainFormat = 0;
    u32 antialiasingModeId = 0;
    std::string antialiasingMode;
    f32 instantaneousFps = 0.0f;
    glm::mat4 view{ 1.0f };
    glm::mat4 projection{ 1.0f };
    bool matricesAvailable = false;
};

struct RendererMonitorCommandSnapshot {
    u64 renderIdentity = 0;
    std::string debugName;
    u32 submissionIndex = 0;
    u32 vertexCount = 0;
    u32 indexCount = 0;
    u32 triangleCount = 0;
    u32 vertexStride = 0;
    i32 drawOrder = 0;
    u32 lodLevel = 0;
    f32 lodScreenFraction = 1.0f;
    bool castShadow = false;
    bool reflectionCaptureVisible = true;
    bool boundsValid = false;
    glm::vec3 boundsMin{ 0.0f };
    glm::vec3 boundsMax{ 0.0f };
    u32 gpuOcclusionCandidateIndex = 0;
    u32 reflectionProbeAssignmentCode = 0;
    i32 reflectionProbeSceneIndex = -1;
    std::string bonePaletteResourceId;
    u32 bonePaletteReady = 0;
    u32 bonePaletteDescriptorReady = 0;
    u64 bonePaletteRevision = 0;
};

struct RendererMonitorQueueSnapshot {
    std::string name;
    std::vector<RendererMonitorCommandSnapshot> commands;
    u64 submittedTriangles = 0;
};

struct RendererMonitorResourceSnapshot {
    u32 id = 0;
    std::string name;
    std::string status;
    std::string lifetime;
    std::string format;
    std::string usage;
    std::string scale;
    u32 firstUsePassId = 0;
    u32 lastUsePassId = 0;
    u32 readCount = 0;
    u32 writeCount = 0;
};

struct RendererMonitorPassExecutionSnapshot {
    u32 id = 0;
    std::string name;
    std::string queue;
    std::string status;
    u32 kind = 0;
    bool executionKnown = false;
    u32 drawCount = 0;
    u64 triangleCount = 0;
    u32 dispatchCount = 0;
    std::string executionSource;
};

struct RendererFrameMonitorSnapshot {
    RendererMonitorSessionSnapshot session;
    RendererMonitorFrameContext frame;
    std::vector<RendererMonitorQueueSnapshot> queues;
    std::vector<RendererMonitorResourceSnapshot> resources;
    std::vector<RendererMonitorPassExecutionSnapshot> passes;
};

}
