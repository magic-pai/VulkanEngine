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

TemporalUpscalerProviderKind TemporalUpscalerProviderKindFromName(
    const std::string& name
);

TemporalUpscalerPackageStatus ProbeTemporalUpscalerPackage(
    const TemporalUpscalerProbeRequest& request
);

}
