#include "renderer/temporal_upscaler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>

#if defined(SE_ENABLE_NVIDIA_DLSS) && SE_ENABLE_NVIDIA_DLSS
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_vk.h>
#endif

namespace se {
namespace {

constexpr const char* kSelfEngineDlssProjectId =
    "b62bb5e0-7d74-4a20-9a7f-6a507a29c64c";
constexpr const char* kSelfEngineDlssEngineVersion = "SelfEngine";

std::string NormalizeProviderName(const std::string& name) {
    std::string normalized;
    normalized.reserve(name.size());
    for (char ch : name) {
        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))
        ));
    }
    return normalized;
}

bool FileExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

bool DirectoryExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

bool FileContains(const std::filesystem::path& path, std::string_view token) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }

    std::string contents(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>()
    );
    return contents.find(token) != std::string::npos;
}

bool AnyFileExists(
    const std::filesystem::path& root,
    std::initializer_list<std::filesystem::path> relativePaths
) {
    return std::any_of(
        relativePaths.begin(),
        relativePaths.end(),
        [&root](const std::filesystem::path& relativePath) {
            return FileExists(root / relativePath);
        }
    );
}

std::filesystem::path DefaultDlssSdkRoot() {
    return std::filesystem::current_path() / "thirdParty" / "nvidia_dlss";
}

std::filesystem::path DefaultNgxApplicationDataPath() {
    return std::filesystem::current_path() / "out" / "ngx";
}

bool ParseUnsigned(std::string_view text, u32& value) {
    u32 parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }

    value = parsed;
    return true;
}

void ParseVersionFromFilename(
    const std::string& filename,
    TemporalUpscalerPackageStatus& status
) {
    constexpr std::string_view prefix = "libnvidia-ngx-dlss.so.";
    const std::size_t offset = filename.find(prefix);
    if (offset == std::string::npos) {
        return;
    }

    std::array<u32, 3> parts{};
    std::size_t partIndex = 0;
    std::size_t partBegin = offset + prefix.size();
    while (partIndex < parts.size() && partBegin <= filename.size()) {
        const std::size_t partEnd = filename.find('.', partBegin);
        const std::size_t end =
            partEnd == std::string::npos ? filename.size() : partEnd;
        if (end <= partBegin ||
            !ParseUnsigned(
                std::string_view(filename.data() + partBegin, end - partBegin),
                parts[partIndex]
            )) {
            return;
        }

        ++partIndex;
        if (partEnd == std::string::npos) {
            break;
        }
        partBegin = partEnd + 1u;
    }

    if (partIndex == parts.size()) {
        status.sdkVersionMajor = parts[0];
        status.sdkVersionMinor = parts[1];
        status.sdkVersionPatch = parts[2];
    }
}

void ProbeDlssVersion(
    const std::filesystem::path& sdkRoot,
    TemporalUpscalerPackageStatus& status
) {
    const std::filesystem::path relRuntimeDir =
        sdkRoot / "lib" / "Linux_x86_64" / "rel";
    std::error_code error;
    if (!std::filesystem::is_directory(relRuntimeDir, error)) {
        return;
    }

    for (const std::filesystem::directory_entry& entry :
        std::filesystem::directory_iterator(relRuntimeDir, error)) {
        if (error) {
            return;
        }
        ParseVersionFromFilename(entry.path().filename().string(), status);
        if (status.sdkVersionMajor != 0u) {
            return;
        }
    }
}

TemporalUpscalerPackageFallbackReason DlssFallbackReason(
    const TemporalUpscalerPackageStatus& status
) {
    if (status.packageDirectoryFound == 0u) {
        return TemporalUpscalerPackageFallbackReason::PackageMissing;
    }
    if (status.headersFound == 0u) {
        return TemporalUpscalerPackageFallbackReason::HeadersMissing;
    }
    if (status.importLibraryFound == 0u) {
        return TemporalUpscalerPackageFallbackReason::ImportLibraryMissing;
    }
    if (status.runtimeFound == 0u) {
        return TemporalUpscalerPackageFallbackReason::RuntimeMissing;
    }
    if (status.superResolutionSymbolsFound == 0u) {
        return TemporalUpscalerPackageFallbackReason::SuperResolutionSymbolsMissing;
    }
    return TemporalUpscalerPackageFallbackReason::None;
}

void ProbeDlssPackage(TemporalUpscalerPackageStatus& status) {
    const std::filesystem::path& root = status.sdkRoot;
    status.packageDirectoryFound = DirectoryExists(root) ? 1u : 0u;
    status.headersFound =
        FileExists(root / "include" / "nvsdk_ngx.h") &&
        FileExists(root / "include" / "nvsdk_ngx_vk.h") &&
        FileExists(root / "include" / "nvsdk_ngx_defs.h") &&
        FileExists(root / "include" / "nvsdk_ngx_helpers_vk.h")
            ? 1u
            : 0u;
    status.importLibraryFound = AnyFileExists(
        root,
        {
            "lib/Windows_x86_64/x64/nvsdk_ngx_s.lib",
            "lib/Windows_x86_64/x64/nvsdk_ngx_d.lib",
            "lib/Windows_x86_64/khr/x64/nvsdk_ngx_khr_s.lib"
        }
    ) ? 1u : 0u;
    status.runtimeFound = AnyFileExists(
        root,
        {
            "lib/Windows_x86_64/rel/nvngx_dlss.dll",
            "lib/Windows_x86_64/dev/nvngx_dlss.dll"
        }
    ) ? 1u : 0u;

    const std::filesystem::path defsHeader =
        root / "include" / "nvsdk_ngx_defs.h";
    const std::filesystem::path dlssVkHeader =
        root / "include" / "nvsdk_ngx_helpers_vk.h";
    const std::filesystem::path dlssgVkHeader =
        root / "include" / "nvsdk_ngx_helpers_dlssg_vk.h";
    const std::filesystem::path dlssdVkHeader =
        root / "include" / "nvsdk_ngx_helpers_dlssd_vk.h";
    status.superResolutionSymbolsFound =
        FileContains(defsHeader, "NVSDK_NGX_Feature_SuperSampling") &&
        FileContains(dlssVkHeader, "NGX_VULKAN_CREATE_DLSS_EXT") &&
        FileContains(dlssVkHeader, "NGX_VULKAN_EVALUATE_DLSS_EXT")
            ? 1u
            : 0u;
    status.frameGenerationSymbolsFound =
        FileContains(defsHeader, "NVSDK_NGX_Feature_FrameGeneration") &&
        FileContains(dlssgVkHeader, "NGX_VK_CREATE_DLSSG") &&
        FileContains(dlssgVkHeader, "NGX_VK_EVALUATE_DLSSG")
            ? 1u
            : 0u;
    status.rayReconstructionSymbolsFound =
        FileContains(defsHeader, "NVSDK_NGX_Feature_RayReconstruction") &&
        FileContains(dlssdVkHeader, "DLSSD")
            ? 1u
            : 0u;
    status.transformerPresetSymbolsFound =
        FileContains(defsHeader, "NVSDK_NGX_DLSS_Hint_Render_Preset_K") &&
        FileContains(defsHeader, "NVSDK_NGX_DLSS_Hint_Render_Preset_L") &&
        FileContains(defsHeader, "NVSDK_NGX_DLSS_Hint_Render_Preset_M")
            ? 1u
            : 0u;

    ProbeDlssVersion(root, status);
    status.packageReady =
        status.packageDirectoryFound > 0u &&
        status.headersFound > 0u &&
        status.importLibraryFound > 0u &&
        status.runtimeFound > 0u &&
        status.superResolutionSymbolsFound > 0u
            ? 1u
            : 0u;
    status.evaluateAdapterAvailable = 0u;
    status.fallbackReason = DlssFallbackReason(status);
}

TemporalUpscalerDlssPreset RecommendedDlssPresetForQuality(
    TemporalUpscalerDlssQualityMode qualityMode
) {
    switch (qualityMode) {
    case TemporalUpscalerDlssQualityMode::UltraPerformance:
        return TemporalUpscalerDlssPreset::L;
    case TemporalUpscalerDlssQualityMode::Performance:
        return TemporalUpscalerDlssPreset::M;
    case TemporalUpscalerDlssQualityMode::Dlaa:
    case TemporalUpscalerDlssQualityMode::Balanced:
    case TemporalUpscalerDlssQualityMode::UltraQuality:
    case TemporalUpscalerDlssQualityMode::Quality:
    case TemporalUpscalerDlssQualityMode::Default:
    default:
        return TemporalUpscalerDlssPreset::K;
    }
}

#if defined(SE_ENABLE_NVIDIA_DLSS) && SE_ENABLE_NVIDIA_DLSS
NVSDK_NGX_PerfQuality_Value NgxQualityMode(
    TemporalUpscalerDlssQualityMode qualityMode
) {
    switch (qualityMode) {
    case TemporalUpscalerDlssQualityMode::Performance:
        return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case TemporalUpscalerDlssQualityMode::Balanced:
        return NVSDK_NGX_PerfQuality_Value_Balanced;
    case TemporalUpscalerDlssQualityMode::UltraPerformance:
        return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    case TemporalUpscalerDlssQualityMode::UltraQuality:
        return NVSDK_NGX_PerfQuality_Value_UltraQuality;
    case TemporalUpscalerDlssQualityMode::Dlaa:
        return NVSDK_NGX_PerfQuality_Value_DLAA;
    case TemporalUpscalerDlssQualityMode::Quality:
    case TemporalUpscalerDlssQualityMode::Default:
    default:
        return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    }
}

int NgxPresetHint(TemporalUpscalerDlssPreset preset) {
    switch (preset) {
    case TemporalUpscalerDlssPreset::K:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_K;
    case TemporalUpscalerDlssPreset::L:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_L;
    case TemporalUpscalerDlssPreset::M:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_M;
    case TemporalUpscalerDlssPreset::Default:
    default:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
    }
}

void SetNgxPresetHint(
    NVSDK_NGX_Parameter* parameters,
    TemporalUpscalerDlssQualityMode qualityMode
) {
    if (parameters == nullptr) {
        return;
    }

    const int preset =
        NgxPresetHint(RecommendedDlssPresetForQuality(qualityMode));
    switch (qualityMode) {
    case TemporalUpscalerDlssQualityMode::Dlaa:
        NVSDK_NGX_Parameter_SetI(
            parameters,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
            preset
        );
        break;
    case TemporalUpscalerDlssQualityMode::Balanced:
        NVSDK_NGX_Parameter_SetI(
            parameters,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
            preset
        );
        break;
    case TemporalUpscalerDlssQualityMode::Performance:
        NVSDK_NGX_Parameter_SetI(
            parameters,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
            preset
        );
        break;
    case TemporalUpscalerDlssQualityMode::UltraPerformance:
        NVSDK_NGX_Parameter_SetI(
            parameters,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
            preset
        );
        break;
    case TemporalUpscalerDlssQualityMode::UltraQuality:
        NVSDK_NGX_Parameter_SetI(
            parameters,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality,
            preset
        );
        break;
    case TemporalUpscalerDlssQualityMode::Quality:
    case TemporalUpscalerDlssQualityMode::Default:
    default:
        NVSDK_NGX_Parameter_SetI(
            parameters,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
            preset
        );
        break;
    }
}

u32 NgxDlssCreateFlags() {
    return NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
}

bool NgxGetU32(NVSDK_NGX_Parameter* parameters, const char* name, u32& value) {
    unsigned int uiValue = 0;
    NVSDK_NGX_Result result =
        NVSDK_NGX_Parameter_GetUI(parameters, name, &uiValue);
    if (NVSDK_NGX_SUCCEED(result)) {
        value = static_cast<u32>(uiValue);
        return true;
    }

    int intValue = 0;
    result = NVSDK_NGX_Parameter_GetI(parameters, name, &intValue);
    if (NVSDK_NGX_SUCCEED(result)) {
        value = intValue < 0 ? 0u : static_cast<u32>(intValue);
        return true;
    }

    return false;
}

std::string JoinNames(const std::vector<std::string>& names) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0u) {
            stream << ';';
        }
        stream << names[index];
    }
    return stream.str();
}

std::vector<std::string> ExtensionPropertyNames(
    const VkExtensionProperties* extensionProperties,
    u32 extensionCount
) {
    std::vector<std::string> names;
    names.reserve(extensionCount);
    for (u32 index = 0; index < extensionCount; ++index) {
        names.emplace_back(extensionProperties[index].extensionName);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::set<std::string> ExtensionNameSet(
    const std::vector<const char*>& extensionNames
) {
    std::set<std::string> names;
    for (const char* extensionName : extensionNames) {
        names.insert(extensionName);
    }
    return names;
}

u32 CountContained(
    const std::vector<std::string>& requiredNames,
    const std::set<std::string>& availableNames,
    std::vector<std::string>& missingNames
) {
    u32 contained = 0;
    for (const std::string& requiredName : requiredNames) {
        if (availableNames.find(requiredName) != availableNames.end()) {
            ++contained;
        } else {
            missingNames.push_back(requiredName);
        }
    }
    return contained;
}

void PopulateNgxFeatureRequirementStatus(
    const TemporalUpscalerRuntimeRequest& request,
    TemporalUpscalerRuntimeStatus& status
) {
    const std::filesystem::path appDataPath =
        request.applicationDataPath.empty()
            ? DefaultNgxApplicationDataPath()
            : request.applicationDataPath;
    std::error_code error;
    std::filesystem::create_directories(appDataPath, error);

    const std::filesystem::path runtimePath =
        request.packageStatus.sdkRoot /
        "lib" / "Windows_x86_64" / "rel";
    const std::wstring runtimePathWide = runtimePath.wstring();
    const wchar_t* featurePaths[] = { runtimePathWide.c_str() };
    NVSDK_NGX_FeatureCommonInfo featureInfo{};
    featureInfo.PathListInfo.Path = featurePaths;
    featureInfo.PathListInfo.Length = 1u;

    NVSDK_NGX_Application_Identifier identifier{};
    identifier.IdentifierType =
        NVSDK_NGX_Application_Identifier_Type_Project_Id;
    identifier.v.ProjectDesc.ProjectId = kSelfEngineDlssProjectId;
    identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    identifier.v.ProjectDesc.EngineVersion = kSelfEngineDlssEngineVersion;

    const std::wstring appDataPathWide = appDataPath.wstring();
    NVSDK_NGX_FeatureDiscoveryInfo discoveryInfo{};
    discoveryInfo.SDKVersion = NVSDK_NGX_Version_API;
    discoveryInfo.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    discoveryInfo.Identifier = identifier;
    discoveryInfo.ApplicationDataPath = appDataPathWide.c_str();
    discoveryInfo.FeatureInfo = &featureInfo;

    VkExtensionProperties* instanceExtensionProperties = nullptr;
    uint32_t instanceExtensionCount = 0;
    status.instanceExtensionRequirementsQueried = 1u;
    const NVSDK_NGX_Result instanceExtensionResult =
        NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(
            &discoveryInfo,
            &instanceExtensionCount,
            &instanceExtensionProperties
        );
    status.instanceExtensionRequirementsResult =
        static_cast<u32>(instanceExtensionResult);
    if (NVSDK_NGX_SUCCEED(instanceExtensionResult)) {
        status.instanceExtensionRequirementCount = instanceExtensionCount;
        const std::vector<std::string> requiredNames =
            ExtensionPropertyNames(
                instanceExtensionProperties,
                instanceExtensionCount
            );
        std::vector<std::string> missingNames;
        status.instanceExtensionAvailableCount = CountContained(
            requiredNames,
            AvailableVulkanInstanceExtensionNames(),
            missingNames
        );
        std::vector<std::string> missingEnabledNames;
        status.instanceExtensionEnabledCount = CountContained(
            requiredNames,
            ExtensionNameSet(EnabledOptionalDlssVulkanInstanceExtensions()),
            missingEnabledNames
        );
        status.instanceExtensionMissingAvailableCount =
            static_cast<u32>(missingNames.size());
        status.instanceExtensionMissingEnabledCount =
            static_cast<u32>(missingEnabledNames.size());
        status.instanceExtensionRequirements = JoinNames(requiredNames);
        status.instanceExtensionMissingAvailable = JoinNames(missingNames);
        status.instanceExtensionMissingEnabled =
            JoinNames(missingEnabledNames);
    }

    VkExtensionProperties* deviceExtensionProperties = nullptr;
    uint32_t deviceExtensionCount = 0;
    status.deviceExtensionRequirementsQueried = 1u;
    const NVSDK_NGX_Result deviceExtensionResult =
        NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
            request.instance,
            request.physicalDevice,
            &discoveryInfo,
            &deviceExtensionCount,
            &deviceExtensionProperties
        );
    status.deviceExtensionRequirementsResult =
        static_cast<u32>(deviceExtensionResult);
    if (NVSDK_NGX_SUCCEED(deviceExtensionResult)) {
        status.deviceExtensionRequirementCount = deviceExtensionCount;
        const std::vector<std::string> requiredNames =
            ExtensionPropertyNames(
                deviceExtensionProperties,
                deviceExtensionCount
            );
        std::vector<std::string> missingAvailableNames;
        status.deviceExtensionAvailableCount = CountContained(
            requiredNames,
            AvailableVulkanDeviceExtensionNames(request.physicalDevice),
            missingAvailableNames
        );
        std::vector<std::string> missingEnabledNames;
        status.deviceExtensionEnabledCount = CountContained(
            requiredNames,
            ExtensionNameSet(EnabledVulkanDeviceExtensionsForPhysicalDevice(
                request.physicalDevice
            )),
            missingEnabledNames
        );
        status.deviceExtensionMissingAvailableCount =
            static_cast<u32>(missingAvailableNames.size());
        status.deviceExtensionMissingEnabledCount =
            static_cast<u32>(missingEnabledNames.size());
        status.deviceExtensionRequirements = JoinNames(requiredNames);
        status.deviceExtensionMissingAvailable =
            JoinNames(missingAvailableNames);
        status.deviceExtensionMissingEnabled =
            JoinNames(missingEnabledNames);
    }

    NVSDK_NGX_FeatureRequirement featureRequirement{};
    status.featureRequirementsQueried = 1u;
    const NVSDK_NGX_Result featureRequirementResult =
        NVSDK_NGX_VULKAN_GetFeatureRequirements(
            request.instance,
            request.physicalDevice,
            &discoveryInfo,
            &featureRequirement
        );
    status.featureRequirementsResult =
        static_cast<u32>(featureRequirementResult);
    if (NVSDK_NGX_SUCCEED(featureRequirementResult)) {
        status.featureSupportedMask =
            static_cast<u32>(featureRequirement.FeatureSupported);
        status.featureRequirementsSupported =
            featureRequirement.FeatureSupported == 0 ? 1u : 0u;
        status.minHardwareArchitecture =
            featureRequirement.MinHWArchitecture;
        status.minOsVersion = featureRequirement.MinOSVersion;
    }

    if (NVSDK_NGX_FAILED(instanceExtensionResult) ||
        NVSDK_NGX_FAILED(deviceExtensionResult) ||
        NVSDK_NGX_FAILED(featureRequirementResult)) {
        status.fallbackReason =
            TemporalUpscalerRuntimeFallbackReason::FeatureRequirementsFailed;
    } else if (status.instanceExtensionMissingAvailableCount != 0u ||
        status.instanceExtensionMissingEnabledCount != 0u ||
        status.deviceExtensionMissingAvailableCount != 0u ||
        status.deviceExtensionMissingEnabledCount != 0u) {
        status.fallbackReason =
            TemporalUpscalerRuntimeFallbackReason::RequiredVulkanExtensionMissing;
    }
}

bool NgxRequirementsPermitEvaluation(
    const TemporalUpscalerRuntimeStatus& status
) {
    return status.featureRequirementsQueried != 0u &&
        status.featureRequirementsResult ==
            static_cast<u32>(NVSDK_NGX_Result_Success) &&
        status.featureRequirementsSupported != 0u &&
        status.instanceExtensionRequirementsQueried != 0u &&
        status.instanceExtensionRequirementsResult ==
            static_cast<u32>(NVSDK_NGX_Result_Success) &&
        status.instanceExtensionMissingAvailableCount == 0u &&
        status.instanceExtensionMissingEnabledCount == 0u &&
        status.deviceExtensionRequirementsQueried != 0u &&
        status.deviceExtensionRequirementsResult ==
            static_cast<u32>(NVSDK_NGX_Result_Success) &&
        status.deviceExtensionMissingAvailableCount == 0u &&
        status.deviceExtensionMissingEnabledCount == 0u;
}

struct DlssRuntimeCache {
    bool initializationAttempted = false;
    bool initialized = false;
    VkDevice device = VK_NULL_HANDLE;
    u32 initializationResult = 0;
    bool statusCached = false;
    VkExtent2D cachedDisplayExtent{};
    TemporalUpscalerDlssQualityMode cachedQualityMode =
        TemporalUpscalerDlssQualityMode::Quality;
    TemporalUpscalerRuntimeStatus cachedStatus{};
    NVSDK_NGX_Handle* dlssFeatureHandle = nullptr;
    NVSDK_NGX_Parameter* dlssParameters = nullptr;
    VkExtent2D dlssFeatureRenderExtent{};
    VkExtent2D dlssFeatureOutputExtent{};
    TemporalUpscalerDlssQualityMode dlssFeatureQualityMode =
        TemporalUpscalerDlssQualityMode::Quality;
    u32 dlssFeatureCreateFlags = 0;
};

DlssRuntimeCache g_DlssRuntimeCache{};

void ReleaseDlssFeatureCache() {
    if (g_DlssRuntimeCache.dlssFeatureHandle != nullptr) {
        NVSDK_NGX_VULKAN_ReleaseFeature(g_DlssRuntimeCache.dlssFeatureHandle);
        g_DlssRuntimeCache.dlssFeatureHandle = nullptr;
    }
    if (g_DlssRuntimeCache.dlssParameters != nullptr) {
        NVSDK_NGX_VULKAN_DestroyParameters(g_DlssRuntimeCache.dlssParameters);
        g_DlssRuntimeCache.dlssParameters = nullptr;
    }
    g_DlssRuntimeCache.dlssFeatureRenderExtent = {};
    g_DlssRuntimeCache.dlssFeatureOutputExtent = {};
    g_DlssRuntimeCache.dlssFeatureQualityMode =
        TemporalUpscalerDlssQualityMode::Quality;
    g_DlssRuntimeCache.dlssFeatureCreateFlags = 0u;
}

bool ExtentsEqual(VkExtent2D lhs, VkExtent2D rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

TemporalUpscalerFeatureRecreationReason DlssFeatureRecreationReason(
    const TemporalUpscalerEvaluateRequest& request,
    u32 createFlags
) {
    if (g_DlssRuntimeCache.dlssFeatureHandle == nullptr) {
        return TemporalUpscalerFeatureRecreationReason::FirstCreate;
    }
    if (!ExtentsEqual(
        g_DlssRuntimeCache.dlssFeatureRenderExtent,
        request.renderExtent
    )) {
        return TemporalUpscalerFeatureRecreationReason::InputExtentChanged;
    }
    if (!ExtentsEqual(
        g_DlssRuntimeCache.dlssFeatureOutputExtent,
        request.outputExtent
    )) {
        return TemporalUpscalerFeatureRecreationReason::OutputExtentChanged;
    }
    if (g_DlssRuntimeCache.dlssFeatureQualityMode !=
        request.dlssQualityMode) {
        return TemporalUpscalerFeatureRecreationReason::QualityModeChanged;
    }
    if (g_DlssRuntimeCache.dlssFeatureCreateFlags != createFlags) {
        return TemporalUpscalerFeatureRecreationReason::CreateFlagsChanged;
    }
    return TemporalUpscalerFeatureRecreationReason::None;
}

void PopulateNgxCapabilityStatus(
    const TemporalUpscalerRuntimeRequest& request,
    TemporalUpscalerRuntimeStatus& status
) {
    NVSDK_NGX_Parameter* capabilityParameters = nullptr;
    const NVSDK_NGX_Result capabilityResult =
        NVSDK_NGX_VULKAN_GetCapabilityParameters(&capabilityParameters);
    status.capabilityQueryResult = static_cast<u32>(capabilityResult);
    if (NVSDK_NGX_FAILED(capabilityResult) ||
        capabilityParameters == nullptr) {
        status.fallbackReason =
            TemporalUpscalerRuntimeFallbackReason::CapabilityParametersFailed;
        return;
    }

    status.capabilityParametersReady = 1u;
    NgxGetU32(
        capabilityParameters,
        NVSDK_NGX_Parameter_SuperSampling_Available,
        status.superResolutionSupported
    );
    NgxGetU32(
        capabilityParameters,
        NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
        status.needsUpdatedDriver
    );
    NgxGetU32(
        capabilityParameters,
        NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,
        status.minDriverVersionMajor
    );
    NgxGetU32(
        capabilityParameters,
        NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,
        status.minDriverVersionMinor
    );
    NgxGetU32(
        capabilityParameters,
        NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,
        status.featureInitResult
    );

    if (status.superResolutionSupported == 0u) {
        status.fallbackReason =
            TemporalUpscalerRuntimeFallbackReason::SuperResolutionUnavailable;
    } else if (status.needsUpdatedDriver != 0u) {
        status.fallbackReason =
            TemporalUpscalerRuntimeFallbackReason::DriverUpdateRequired;
    } else {
        unsigned int optimalWidth = 0;
        unsigned int optimalHeight = 0;
        unsigned int maxWidth = 0;
        unsigned int maxHeight = 0;
        unsigned int minWidth = 0;
        unsigned int minHeight = 0;
        float sharpness = 0.0f;
        status.optimalSettingsQueried = 1u;
        const NVSDK_NGX_Result optimalResult = NGX_DLSS_GET_OPTIMAL_SETTINGS(
            capabilityParameters,
            request.displayExtent.width,
            request.displayExtent.height,
            NgxQualityMode(request.dlssQualityMode),
            &optimalWidth,
            &optimalHeight,
            &maxWidth,
            &maxHeight,
            &minWidth,
            &minHeight,
            &sharpness
        );
        status.optimalSettingsResult = static_cast<u32>(optimalResult);
        if (NVSDK_NGX_SUCCEED(optimalResult)) {
            status.optimalRenderWidth = optimalWidth;
            status.optimalRenderHeight = optimalHeight;
            status.maxRenderWidth = maxWidth;
            status.maxRenderHeight = maxHeight;
            status.minRenderWidth = minWidth;
            status.minRenderHeight = minHeight;
            status.sharpness = sharpness;
            if (NgxRequirementsPermitEvaluation(status)) {
                status.evaluateAdapterAvailable = 1u;
                status.fallbackReason =
                    TemporalUpscalerRuntimeFallbackReason::None;
            } else if (status.deviceExtensionMissingAvailableCount != 0u ||
                status.deviceExtensionMissingEnabledCount != 0u ||
                status.instanceExtensionMissingAvailableCount != 0u ||
                status.instanceExtensionMissingEnabledCount != 0u) {
                status.fallbackReason =
                    TemporalUpscalerRuntimeFallbackReason::RequiredVulkanExtensionMissing;
            } else {
                status.fallbackReason =
                    TemporalUpscalerRuntimeFallbackReason::FeatureRequirementsFailed;
            }
        } else {
            status.fallbackReason =
                TemporalUpscalerRuntimeFallbackReason::OptimalSettingsFailed;
        }
    }

    NVSDK_NGX_VULKAN_DestroyParameters(capabilityParameters);
}

TemporalUpscalerRuntimeStatus QueryCompiledDlssRuntime(
    const TemporalUpscalerRuntimeRequest& request
) {
    TemporalUpscalerRuntimeStatus status{};
    status.requested = request.packageStatus.requested;
    status.adapterCompiled = 1u;
    status.dlssQualityMode = static_cast<u32>(request.dlssQualityMode);
    status.recommendedPreset =
        static_cast<u32>(RecommendedDlssPresetForQuality(request.dlssQualityMode));

    if (request.instance == VK_NULL_HANDLE ||
        request.physicalDevice == VK_NULL_HANDLE ||
        request.device == VK_NULL_HANDLE ||
        request.getInstanceProcAddr == nullptr ||
        request.getDeviceProcAddr == nullptr ||
        request.displayExtent.width == 0u ||
        request.displayExtent.height == 0u) {
        status.fallbackReason =
            TemporalUpscalerRuntimeFallbackReason::VulkanHandlesUnavailable;
        return status;
    }

    PopulateNgxFeatureRequirementStatus(request, status);

    if (!g_DlssRuntimeCache.initializationAttempted) {
        g_DlssRuntimeCache.initializationAttempted = true;
        g_DlssRuntimeCache.device = request.device;

        const std::filesystem::path appDataPath =
            request.applicationDataPath.empty()
                ? DefaultNgxApplicationDataPath()
                : request.applicationDataPath;
        std::error_code error;
        std::filesystem::create_directories(appDataPath, error);

        const std::filesystem::path runtimePath =
            request.packageStatus.sdkRoot /
            "lib" / "Windows_x86_64" / "rel";
        const std::wstring runtimePathWide = runtimePath.wstring();
        const wchar_t* featurePaths[] = { runtimePathWide.c_str() };
        NVSDK_NGX_FeatureCommonInfo featureInfo{};
        featureInfo.PathListInfo.Path = featurePaths;
        featureInfo.PathListInfo.Length = 1u;

        const std::wstring appDataPathWide = appDataPath.wstring();
        const NVSDK_NGX_Result initResult =
            NVSDK_NGX_VULKAN_Init_with_ProjectID(
                kSelfEngineDlssProjectId,
                NVSDK_NGX_ENGINE_TYPE_CUSTOM,
                kSelfEngineDlssEngineVersion,
                appDataPathWide.c_str(),
                request.instance,
                request.physicalDevice,
                request.device,
                request.getInstanceProcAddr,
                request.getDeviceProcAddr,
                &featureInfo,
                NVSDK_NGX_Version_API
            );
        g_DlssRuntimeCache.initializationResult =
            static_cast<u32>(initResult);
        g_DlssRuntimeCache.initialized = NVSDK_NGX_SUCCEED(initResult);
    }

    status.initializationAttempted =
        g_DlssRuntimeCache.initializationAttempted ? 1u : 0u;
    status.initialized = g_DlssRuntimeCache.initialized ? 1u : 0u;
    status.initializationResult = g_DlssRuntimeCache.initializationResult;

    if (!g_DlssRuntimeCache.initialized) {
        status.fallbackReason =
            TemporalUpscalerRuntimeFallbackReason::InitializationFailed;
        return status;
    }

    if (g_DlssRuntimeCache.statusCached &&
        g_DlssRuntimeCache.cachedDisplayExtent.width ==
            request.displayExtent.width &&
        g_DlssRuntimeCache.cachedDisplayExtent.height ==
            request.displayExtent.height &&
        g_DlssRuntimeCache.cachedQualityMode == request.dlssQualityMode) {
        return g_DlssRuntimeCache.cachedStatus;
    }

    PopulateNgxCapabilityStatus(request, status);
    g_DlssRuntimeCache.statusCached = true;
    g_DlssRuntimeCache.cachedDisplayExtent = request.displayExtent;
    g_DlssRuntimeCache.cachedQualityMode = request.dlssQualityMode;
    g_DlssRuntimeCache.cachedStatus = status;
    return status;
}

bool IsValidEvaluateImage(const TemporalUpscalerVulkanImageResource& image) {
    return image.image != VK_NULL_HANDLE &&
        image.imageView != VK_NULL_HANDLE &&
        image.format != VK_FORMAT_UNDEFINED &&
        image.extent.width > 0u &&
        image.extent.height > 0u;
}

NVSDK_NGX_Resource_VK NgxImageResource(
    const TemporalUpscalerVulkanImageResource& image
) {
    VkImageSubresourceRange range{};
    range.aspectMask = image.aspectMask;
    range.baseMipLevel = 0u;
    range.levelCount = 1u;
    range.baseArrayLayer = 0u;
    range.layerCount = 1u;
    return NVSDK_NGX_Create_ImageView_Resource_VK(
        image.imageView,
        image.image,
        range,
        image.format,
        image.extent.width,
        image.extent.height,
        image.readWrite
    );
}

TemporalUpscalerEvaluateStatus EvaluateCompiledDlssRuntime(
    const TemporalUpscalerEvaluateRequest& request
) {
    TemporalUpscalerEvaluateStatus status{};
    status.requested = request.runtimeStatus.requested;
    status.renderWidth = request.renderExtent.width;
    status.renderHeight = request.renderExtent.height;
    status.outputWidth = request.outputExtent.width;
    status.outputHeight = request.outputExtent.height;
    status.createFlags = NgxDlssCreateFlags();
    status.reset = request.reset;
    status.jitterOffsetX = request.jitterOffsetX;
    status.jitterOffsetY = request.jitterOffsetY;
    status.motionVectorScaleX = request.motionVectorScaleX;
    status.motionVectorScaleY = request.motionVectorScaleY;
    status.sharpness = request.sharpness;

    if (request.runtimeStatus.evaluateAdapterAvailable == 0u ||
        !g_DlssRuntimeCache.initialized) {
        status.fallbackReason =
            TemporalUpscalerEvaluateFallbackReason::RuntimeUnavailable;
        return status;
    }
    if (request.device == VK_NULL_HANDLE ||
        request.commandBuffer == VK_NULL_HANDLE ||
        request.renderExtent.width == 0u ||
        request.renderExtent.height == 0u ||
        request.outputExtent.width == 0u ||
        request.outputExtent.height == 0u ||
        !IsValidEvaluateImage(request.inputColor) ||
        !IsValidEvaluateImage(request.inputDepth) ||
        !IsValidEvaluateImage(request.inputMotionVectors) ||
        !IsValidEvaluateImage(request.outputColor)) {
        status.fallbackReason =
            TemporalUpscalerEvaluateFallbackReason::VulkanResourcesUnavailable;
        return status;
    }

    status.attempted = 1u;
    if (g_DlssRuntimeCache.dlssParameters == nullptr) {
        NVSDK_NGX_Parameter* parameters = nullptr;
        const NVSDK_NGX_Result allocateResult =
            NVSDK_NGX_VULKAN_AllocateParameters(&parameters);
        status.parameterAllocationResult = static_cast<u32>(allocateResult);
        if (NVSDK_NGX_FAILED(allocateResult) || parameters == nullptr) {
            status.fallbackReason =
                TemporalUpscalerEvaluateFallbackReason::ParametersUnavailable;
            return status;
        }
        g_DlssRuntimeCache.dlssParameters = parameters;
    } else {
        status.parameterAllocationResult =
            static_cast<u32>(NVSDK_NGX_Result_Success);
    }
    status.parametersAllocated = 1u;

    const TemporalUpscalerFeatureRecreationReason recreationReason =
        DlssFeatureRecreationReason(request, status.createFlags);
    status.featureRecreationReason = recreationReason;
    if (recreationReason != TemporalUpscalerFeatureRecreationReason::None) {
        if (g_DlssRuntimeCache.dlssFeatureHandle != nullptr) {
            NVSDK_NGX_VULKAN_ReleaseFeature(
                g_DlssRuntimeCache.dlssFeatureHandle
            );
            g_DlssRuntimeCache.dlssFeatureHandle = nullptr;
        }

        g_DlssRuntimeCache.dlssParameters->Reset();
        SetNgxPresetHint(
            g_DlssRuntimeCache.dlssParameters,
            request.dlssQualityMode
        );

        NVSDK_NGX_DLSS_Create_Params createParams{};
        createParams.Feature.InWidth = request.renderExtent.width;
        createParams.Feature.InHeight = request.renderExtent.height;
        createParams.Feature.InTargetWidth = request.outputExtent.width;
        createParams.Feature.InTargetHeight = request.outputExtent.height;
        createParams.Feature.InPerfQualityValue =
            NgxQualityMode(request.dlssQualityMode);
        createParams.InFeatureCreateFlags =
            static_cast<int>(status.createFlags);
        createParams.InEnableOutputSubrects = false;

        status.featureCreateAttempted = 1u;
        const NVSDK_NGX_Result createResult =
            NGX_VULKAN_CREATE_DLSS_EXT1(
                request.device,
                request.commandBuffer,
                1u,
                1u,
                &g_DlssRuntimeCache.dlssFeatureHandle,
                g_DlssRuntimeCache.dlssParameters,
                &createParams
            );
        status.featureCreateResult = static_cast<u32>(createResult);
        if (NVSDK_NGX_FAILED(createResult) ||
            g_DlssRuntimeCache.dlssFeatureHandle == nullptr) {
            g_DlssRuntimeCache.dlssFeatureHandle = nullptr;
            status.fallbackReason =
                TemporalUpscalerEvaluateFallbackReason::FeatureCreateFailed;
            return status;
        }

        g_DlssRuntimeCache.dlssFeatureRenderExtent = request.renderExtent;
        g_DlssRuntimeCache.dlssFeatureOutputExtent = request.outputExtent;
        g_DlssRuntimeCache.dlssFeatureQualityMode = request.dlssQualityMode;
        g_DlssRuntimeCache.dlssFeatureCreateFlags = status.createFlags;
        status.featureRecreated = 1u;
    } else {
        status.featureCreateResult =
            static_cast<u32>(NVSDK_NGX_Result_Success);
    }
    status.featureCreated =
        g_DlssRuntimeCache.dlssFeatureHandle != nullptr ? 1u : 0u;

    g_DlssRuntimeCache.dlssParameters->Reset();
    NVSDK_NGX_Resource_VK colorResource = NgxImageResource(request.inputColor);
    NVSDK_NGX_Resource_VK depthResource = NgxImageResource(request.inputDepth);
    NVSDK_NGX_Resource_VK motionResource =
        NgxImageResource(request.inputMotionVectors);
    NVSDK_NGX_Resource_VK biasCurrentColorMaskResource{};
    NVSDK_NGX_Resource_VK transparencyMaskResource{};
    const bool biasCurrentColorMaskReady =
        IsValidEvaluateImage(request.inputBiasCurrentColorMask);
    const bool transparencyMaskReady =
        IsValidEvaluateImage(request.inputTransparencyMask);
    if (biasCurrentColorMaskReady) {
        biasCurrentColorMaskResource =
            NgxImageResource(request.inputBiasCurrentColorMask);
        status.biasCurrentColorMaskReady = 1u;
    }
    if (transparencyMaskReady) {
        transparencyMaskResource =
            NgxImageResource(request.inputTransparencyMask);
        status.transparencyMaskReady = 1u;
    }
    NVSDK_NGX_Resource_VK outputResource = NgxImageResource(request.outputColor);

    NVSDK_NGX_VK_DLSS_Eval_Params evalParams{};
    evalParams.Feature.pInColor = &colorResource;
    evalParams.Feature.pInOutput = &outputResource;
    evalParams.Feature.InSharpness = request.sharpness;
    evalParams.pInDepth = &depthResource;
    evalParams.pInMotionVectors = &motionResource;
    evalParams.pInBiasCurrentColorMask =
        biasCurrentColorMaskReady ? &biasCurrentColorMaskResource : nullptr;
    evalParams.pInTransparencyMask =
        transparencyMaskReady ? &transparencyMaskResource : nullptr;
    evalParams.InJitterOffsetX = request.jitterOffsetX;
    evalParams.InJitterOffsetY = request.jitterOffsetY;
    evalParams.InRenderSubrectDimensions.Width = request.renderExtent.width;
    evalParams.InRenderSubrectDimensions.Height = request.renderExtent.height;
    evalParams.InReset = request.reset != 0u ? 1 : 0;
    evalParams.InMVScaleX = request.motionVectorScaleX;
    evalParams.InMVScaleY = request.motionVectorScaleY;
    evalParams.InPreExposure = 1.0f;
    evalParams.InExposureScale = 1.0f;

    status.evaluateAttempted = 1u;
    const NVSDK_NGX_Result evaluateResult =
        NGX_VULKAN_EVALUATE_DLSS_EXT(
            request.commandBuffer,
            g_DlssRuntimeCache.dlssFeatureHandle,
            g_DlssRuntimeCache.dlssParameters,
            &evalParams
        );
    status.evaluateResult = static_cast<u32>(evaluateResult);
    if (NVSDK_NGX_FAILED(evaluateResult)) {
        status.fallbackReason =
            TemporalUpscalerEvaluateFallbackReason::EvaluateFailed;
        return status;
    }

    status.outputReady = 1u;
    status.fallbackReason = TemporalUpscalerEvaluateFallbackReason::None;
    return status;
}
#endif

}

TemporalUpscalerProviderKind TemporalUpscalerProviderKindFromName(
    const std::string& name
) {
    const std::string normalized = NormalizeProviderName(name);
    if (normalized.empty() ||
        normalized == "0" ||
        normalized == "off" ||
        normalized == "none") {
        return TemporalUpscalerProviderKind::None;
    }
    if (normalized == "dlss" ||
        normalized == "nvidia-dlss" ||
        normalized == "nvidia_dlss" ||
        normalized == "ngx") {
        return TemporalUpscalerProviderKind::Dlss;
    }
    return TemporalUpscalerProviderKind::Unsupported;
}

TemporalUpscalerDlssQualityMode TemporalUpscalerDlssQualityModeFromName(
    const std::string& name
) {
    const std::string normalized = NormalizeProviderName(name);
    if (normalized == "performance" ||
        normalized == "perf" ||
        normalized == "maxperf" ||
        normalized == "max_performance") {
        return TemporalUpscalerDlssQualityMode::Performance;
    }
    if (normalized == "balanced" || normalized == "balance") {
        return TemporalUpscalerDlssQualityMode::Balanced;
    }
    if (normalized == "ultraperformance" ||
        normalized == "ultra_performance" ||
        normalized == "ultra-performance" ||
        normalized == "ultraperf") {
        return TemporalUpscalerDlssQualityMode::UltraPerformance;
    }
    if (normalized == "ultraquality" ||
        normalized == "ultra_quality" ||
        normalized == "ultra-quality") {
        return TemporalUpscalerDlssQualityMode::UltraQuality;
    }
    if (normalized == "dlaa" || normalized == "aa") {
        return TemporalUpscalerDlssQualityMode::Dlaa;
    }
    return TemporalUpscalerDlssQualityMode::Quality;
}

TemporalUpscalerPackageStatus ProbeTemporalUpscalerPackage(
    const TemporalUpscalerProbeRequest& request
) {
    TemporalUpscalerPackageStatus status{};
    status.providerKind =
        TemporalUpscalerProviderKindFromName(request.providerName);
    status.requested =
        status.providerKind != TemporalUpscalerProviderKind::None ? 1u : 0u;
    status.sdkRoot =
        request.sdkRootOverride.empty()
            ? DefaultDlssSdkRoot()
            : request.sdkRootOverride;

    if (status.providerKind == TemporalUpscalerProviderKind::None) {
        status.fallbackReason =
            TemporalUpscalerPackageFallbackReason::NotRequested;
        return status;
    }
    if (status.providerKind == TemporalUpscalerProviderKind::Unsupported) {
        status.fallbackReason =
            TemporalUpscalerPackageFallbackReason::UnsupportedProvider;
        return status;
    }

    ProbeDlssPackage(status);
    return status;
}

TemporalUpscalerRuntimeStatus QueryTemporalUpscalerRuntime(
    const TemporalUpscalerRuntimeRequest& request
) {
    TemporalUpscalerRuntimeStatus status{};
    status.requested = request.packageStatus.requested;
    status.dlssQualityMode = static_cast<u32>(request.dlssQualityMode);
    status.recommendedPreset =
        static_cast<u32>(RecommendedDlssPresetForQuality(request.dlssQualityMode));

    if (request.packageStatus.providerKind == TemporalUpscalerProviderKind::None) {
        status.fallbackReason =
            TemporalUpscalerRuntimeFallbackReason::NotRequested;
        return status;
    }
    if (request.packageStatus.providerKind ==
        TemporalUpscalerProviderKind::Unsupported) {
        status.fallbackReason =
            TemporalUpscalerRuntimeFallbackReason::UnsupportedProvider;
        return status;
    }
#if defined(SE_ENABLE_NVIDIA_DLSS) && SE_ENABLE_NVIDIA_DLSS
    status.adapterCompiled = 1u;
#endif
    if (request.packageStatus.packageReady == 0u) {
        status.fallbackReason =
            TemporalUpscalerRuntimeFallbackReason::PackageNotReady;
        return status;
    }

#if defined(SE_ENABLE_NVIDIA_DLSS) && SE_ENABLE_NVIDIA_DLSS
    return QueryCompiledDlssRuntime(request);
#else
    status.fallbackReason =
        TemporalUpscalerRuntimeFallbackReason::AdapterNotCompiled;
    return status;
#endif
}

TemporalUpscalerEvaluateStatus EvaluateTemporalUpscaler(
    const TemporalUpscalerEvaluateRequest& request
) {
    TemporalUpscalerEvaluateStatus status{};
    status.requested = request.runtimeStatus.requested;
    status.renderWidth = request.renderExtent.width;
    status.renderHeight = request.renderExtent.height;
    status.outputWidth = request.outputExtent.width;
    status.outputHeight = request.outputExtent.height;
    status.reset = request.reset;
    status.jitterOffsetX = request.jitterOffsetX;
    status.jitterOffsetY = request.jitterOffsetY;
    status.motionVectorScaleX = request.motionVectorScaleX;
    status.motionVectorScaleY = request.motionVectorScaleY;
    status.sharpness = request.sharpness;

    if (request.runtimeStatus.requested == 0u) {
        status.fallbackReason =
            TemporalUpscalerEvaluateFallbackReason::NotRequested;
        return status;
    }
#if defined(SE_ENABLE_NVIDIA_DLSS) && SE_ENABLE_NVIDIA_DLSS
    return EvaluateCompiledDlssRuntime(request);
#else
    status.fallbackReason =
        TemporalUpscalerEvaluateFallbackReason::RuntimeUnavailable;
    return status;
#endif
}

void ShutdownTemporalUpscalerRuntime(VkDevice device) {
#if defined(SE_ENABLE_NVIDIA_DLSS) && SE_ENABLE_NVIDIA_DLSS
    ReleaseDlssFeatureCache();
    if (g_DlssRuntimeCache.initialized) {
        NVSDK_NGX_VULKAN_Shutdown1(
            device != VK_NULL_HANDLE ? device : g_DlssRuntimeCache.device
        );
    }
    g_DlssRuntimeCache = DlssRuntimeCache{};
#else
    (void)device;
#endif
}

}
