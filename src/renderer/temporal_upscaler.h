#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <filesystem>
#include <string>

namespace se {

enum class TemporalUpscalerProviderKind : u32 {
    None = 0,
    Dlss = 1,
    Unsupported = 2
};

enum class TemporalUpscalerPackageFallbackReason : u32 {
    None = 0,
    NotRequested = 1,
    UnsupportedProvider = 2,
    PackageMissing = 3,
    HeadersMissing = 4,
    ImportLibraryMissing = 5,
    RuntimeMissing = 6,
    SuperResolutionSymbolsMissing = 7,
    EvaluateAdapterMissing = 8
};

enum class TemporalUpscalerDlssQualityMode : u32 {
    Default = 0,
    Performance = 1,
    Balanced = 2,
    Quality = 3,
    UltraPerformance = 4,
    UltraQuality = 5,
    Dlaa = 6
};

enum class TemporalUpscalerDlssPreset : u32 {
    Default = 0,
    K = 11,
    L = 12,
    M = 13
};

enum class TemporalUpscalerRuntimeFallbackReason : u32 {
    None = 0,
    NotRequested = 1,
    UnsupportedProvider = 2,
    PackageNotReady = 3,
    AdapterNotCompiled = 4,
    VulkanHandlesUnavailable = 5,
    InitializationFailed = 6,
    CapabilityParametersFailed = 7,
    SuperResolutionUnavailable = 8,
    DriverUpdateRequired = 9,
    OptimalSettingsFailed = 10,
    FeatureRequirementsFailed = 11,
    RequiredVulkanExtensionMissing = 12
};

enum class TemporalUpscalerEvaluateFallbackReason : u32 {
    None = 0,
    NotRequested = 1,
    RuntimeUnavailable = 2,
    VulkanResourcesUnavailable = 3,
    ParametersUnavailable = 4,
    FeatureCreateFailed = 5,
    EvaluateFailed = 6,
    FeatureCreateWarmup = 7
};

enum class TemporalUpscalerFeatureRecreationReason : u32 {
    None = 0,
    FirstCreate = 1,
    InputExtentChanged = 2,
    OutputExtentChanged = 3,
    QualityModeChanged = 4,
    CreateFlagsChanged = 5,
    PresetChanged = 6
};

struct TemporalUpscalerProbeRequest {
    std::string providerName;
    std::filesystem::path sdkRootOverride;
};

struct TemporalUpscalerPackageStatus {
    TemporalUpscalerProviderKind providerKind =
        TemporalUpscalerProviderKind::None;
    TemporalUpscalerPackageFallbackReason fallbackReason =
        TemporalUpscalerPackageFallbackReason::NotRequested;
    std::filesystem::path sdkRoot;
    u32 requested = 0;
    u32 packageDirectoryFound = 0;
    u32 headersFound = 0;
    u32 importLibraryFound = 0;
    u32 runtimeFound = 0;
    u32 superResolutionSymbolsFound = 0;
    u32 frameGenerationSymbolsFound = 0;
    u32 rayReconstructionSymbolsFound = 0;
    u32 transformerPresetSymbolsFound = 0;
    u32 sdkVersionMajor = 0;
    u32 sdkVersionMinor = 0;
    u32 sdkVersionPatch = 0;
    u32 packageReady = 0;
    u32 evaluateAdapterAvailable = 0;
};

struct TemporalUpscalerRuntimeRequest {
    TemporalUpscalerPackageStatus packageStatus{};
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    VkExtent2D displayExtent{};
    TemporalUpscalerDlssQualityMode dlssQualityMode =
        TemporalUpscalerDlssQualityMode::Quality;
    TemporalUpscalerDlssPreset dlssPreset =
        TemporalUpscalerDlssPreset::Default;
    std::filesystem::path applicationDataPath;
};

struct TemporalUpscalerRuntimeStatus {
    TemporalUpscalerRuntimeFallbackReason fallbackReason =
        TemporalUpscalerRuntimeFallbackReason::NotRequested;
    u32 requested = 0;
    u32 adapterCompiled = 0;
    u32 initializationAttempted = 0;
    u32 initialized = 0;
    u32 initializationResult = 0;
    u32 capabilityParametersReady = 0;
    u32 capabilityQueryResult = 0;
    u32 featureRequirementsQueried = 0;
    u32 featureRequirementsResult = 0;
    u32 featureSupportedMask = 0;
    u32 featureRequirementsSupported = 0;
    u32 minHardwareArchitecture = 0;
    std::string minOsVersion;
    u32 instanceExtensionRequirementsQueried = 0;
    u32 instanceExtensionRequirementsResult = 0;
    u32 instanceExtensionRequirementCount = 0;
    u32 instanceExtensionAvailableCount = 0;
    u32 instanceExtensionMissingAvailableCount = 0;
    u32 instanceExtensionEnabledCount = 0;
    u32 instanceExtensionMissingEnabledCount = 0;
    std::string instanceExtensionRequirements;
    std::string instanceExtensionMissingAvailable;
    std::string instanceExtensionMissingEnabled;
    u32 deviceExtensionRequirementsQueried = 0;
    u32 deviceExtensionRequirementsResult = 0;
    u32 deviceExtensionRequirementCount = 0;
    u32 deviceExtensionAvailableCount = 0;
    u32 deviceExtensionMissingAvailableCount = 0;
    u32 deviceExtensionEnabledCount = 0;
    u32 deviceExtensionMissingEnabledCount = 0;
    std::string deviceExtensionRequirements;
    std::string deviceExtensionMissingAvailable;
    std::string deviceExtensionMissingEnabled;
    u32 runtimeFlavor = 0;
    u32 runtimePathOverridden = 0;
    u32 runtimePathFound = 0;
    std::string runtimePath;
    u32 runtimeDllFound = 0;
    u32 runtimeDllSizeBytes = 0;
    u32 runtimeDllHash = 0;
    u32 superResolutionSupported = 0;
    u32 needsUpdatedDriver = 0;
    u32 minDriverVersionMajor = 0;
    u32 minDriverVersionMinor = 0;
    u32 featureInitResult = 0;
    u32 dlssQualityMode = 0;
    u32 recommendedPreset = 0;
    u32 optimalSettingsQueried = 0;
    u32 optimalSettingsResult = 0;
    u32 optimalRenderWidth = 0;
    u32 optimalRenderHeight = 0;
    u32 minRenderWidth = 0;
    u32 minRenderHeight = 0;
    u32 maxRenderWidth = 0;
    u32 maxRenderHeight = 0;
    f32 sharpness = 0.0f;
    u32 evaluateAdapterAvailable = 0;
};

struct TemporalUpscalerVulkanImageResource {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bool readWrite = false;
};

struct TemporalUpscalerEvaluateRequest {
    TemporalUpscalerRuntimeStatus runtimeStatus{};
    VkDevice device = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    TemporalUpscalerVulkanImageResource inputColor{};
    TemporalUpscalerVulkanImageResource inputDepth{};
    TemporalUpscalerVulkanImageResource inputMotionVectors{};
    TemporalUpscalerVulkanImageResource inputBiasCurrentColorMask{};
    TemporalUpscalerVulkanImageResource inputTransparencyMask{};
    TemporalUpscalerVulkanImageResource outputColor{};
    VkExtent2D renderExtent{};
    VkExtent2D outputExtent{};
    TemporalUpscalerDlssQualityMode dlssQualityMode =
        TemporalUpscalerDlssQualityMode::Quality;
    TemporalUpscalerDlssPreset dlssPreset =
        TemporalUpscalerDlssPreset::Default;
    u32 reset = 0;
    f32 jitterOffsetX = 0.0f;
    f32 jitterOffsetY = 0.0f;
    f32 motionVectorScaleX = 1.0f;
    f32 motionVectorScaleY = 1.0f;
    f32 sharpness = 0.0f;
};

struct TemporalUpscalerEvaluateStatus {
    TemporalUpscalerEvaluateFallbackReason fallbackReason =
        TemporalUpscalerEvaluateFallbackReason::NotRequested;
    TemporalUpscalerFeatureRecreationReason featureRecreationReason =
        TemporalUpscalerFeatureRecreationReason::None;
    u32 requested = 0;
    u32 attempted = 0;
    u32 parametersAllocated = 0;
    u32 parameterAllocationResult = 0;
    u32 featureCreateAttempted = 0;
    u32 featureCreated = 0;
    u32 featureCreateResult = 0;
    u32 featureRecreated = 0;
    u32 evaluateAttempted = 0;
    u32 evaluateResult = 0;
    u32 outputReady = 0;
    u32 biasCurrentColorMaskReady = 0;
    u32 transparencyMaskReady = 0;
    u32 renderWidth = 0;
    u32 renderHeight = 0;
    u32 outputWidth = 0;
    u32 outputHeight = 0;
    u32 createFlags = 0;
    u32 createFlagIsHdr = 0;
    u32 createFlagMvLowRes = 0;
    u32 createFlagMvJittered = 0;
    u32 createFlagDepthInverted = 0;
    u32 createFlagAutoExposure = 0;
    u32 inputColorFormat = 0;
    u32 inputDepthFormat = 0;
    u32 inputMotionVectorFormat = 0;
    u32 inputColorWidth = 0;
    u32 inputColorHeight = 0;
    u32 inputDepthWidth = 0;
    u32 inputDepthHeight = 0;
    u32 inputMotionVectorWidth = 0;
    u32 inputMotionVectorHeight = 0;
    u32 inputDepthAspectMask = 0;
    u32 inputMotionVectorAspectMask = 0;
    u32 inputDepthMatchesRenderExtent = 0;
    u32 inputMotionVectorMatchesRenderExtent = 0;
    u32 motionVectorScalePixelSpace = 0;
    u32 motionVectorScaleUnitSpace = 0;
    u32 motionVectorScaleMatchesRenderExtent = 0;
    u32 reset = 0;
    f32 jitterOffsetX = 0.0f;
    f32 jitterOffsetY = 0.0f;
    f32 motionVectorScaleX = 1.0f;
    f32 motionVectorScaleY = 1.0f;
    f32 sharpness = 0.0f;
};

TemporalUpscalerProviderKind TemporalUpscalerProviderKindFromName(
    const std::string& name
);

TemporalUpscalerDlssQualityMode TemporalUpscalerDlssQualityModeFromName(
    const std::string& name
);

TemporalUpscalerDlssPreset TemporalUpscalerDlssPresetFromName(
    const std::string& name
);

TemporalUpscalerPackageStatus ProbeTemporalUpscalerPackage(
    const TemporalUpscalerProbeRequest& request
);

TemporalUpscalerRuntimeStatus QueryTemporalUpscalerRuntime(
    const TemporalUpscalerRuntimeRequest& request
);

TemporalUpscalerEvaluateStatus EvaluateTemporalUpscaler(
    const TemporalUpscalerEvaluateRequest& request
);

void ResetTemporalUpscalerFeatureCache();
void ShutdownTemporalUpscalerRuntime(VkDevice device);

}
