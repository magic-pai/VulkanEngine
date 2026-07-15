#include "renderer/temporal_upscaler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <type_traits>

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
    std::vector<std::filesystem::path> candidates;
#ifdef SE_NVIDIA_DLSS_SDK_DIR
    candidates.emplace_back(SE_NVIDIA_DLSS_SDK_DIR);
#endif
    std::error_code error;
    std::filesystem::path base = std::filesystem::current_path(error);
    if (!error) {
        for (u32 depth = 0; depth < 4u && !base.empty(); ++depth) {
            candidates.emplace_back(base / "thirdParty" / "nvidia_dlss");
            const std::filesystem::path parent = base.parent_path();
            if (parent == base) {
                break;
            }
            base = parent;
        }
    }

    for (const std::filesystem::path& candidate : candidates) {
        if (DirectoryExists(candidate)) {
            return candidate;
        }
    }

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

struct TemporalUpscalerPackageProbeCache {
    bool valid = false;
    TemporalUpscalerProviderKind providerKind =
        TemporalUpscalerProviderKind::None;
    std::filesystem::path sdkRoot;
    TemporalUpscalerPackageStatus status{};
};

TemporalUpscalerPackageProbeCache g_PackageProbeCache{};

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

TemporalUpscalerPackageStatus ProbeDlssPackageCached(
    TemporalUpscalerPackageStatus status
) {
    if (g_PackageProbeCache.valid &&
        g_PackageProbeCache.providerKind == status.providerKind &&
        g_PackageProbeCache.sdkRoot == status.sdkRoot) {
        return g_PackageProbeCache.status;
    }

    ProbeDlssPackage(status);
    g_PackageProbeCache.valid = true;
    g_PackageProbeCache.providerKind = status.providerKind;
    g_PackageProbeCache.sdkRoot = status.sdkRoot;
    g_PackageProbeCache.status = status;
    return status;
}

TemporalUpscalerDlssPreset RecommendedDlssPresetForQuality(
    TemporalUpscalerDlssQualityMode qualityMode
) {
    switch (qualityMode) {
    case TemporalUpscalerDlssQualityMode::UltraPerformance:
    case TemporalUpscalerDlssQualityMode::Dlaa:
        return TemporalUpscalerDlssPreset::L;
    case TemporalUpscalerDlssQualityMode::Performance:
        return TemporalUpscalerDlssPreset::M;
    case TemporalUpscalerDlssQualityMode::Balanced:
    case TemporalUpscalerDlssQualityMode::UltraQuality:
    case TemporalUpscalerDlssQualityMode::Quality:
    case TemporalUpscalerDlssQualityMode::Default:
    default:
        return TemporalUpscalerDlssPreset::K;
    }
}

TemporalUpscalerDlssPreset EffectiveDlssPreset(
    TemporalUpscalerDlssQualityMode qualityMode,
    TemporalUpscalerDlssPreset presetOverride
) {
    return presetOverride == TemporalUpscalerDlssPreset::Default
        ? RecommendedDlssPresetForQuality(qualityMode)
        : presetOverride;
}

#if defined(SE_ENABLE_NVIDIA_DLSS) && SE_ENABLE_NVIDIA_DLSS
std::optional<bool> ProcessEnvironmentFlagOverride(const char* name);
bool ProcessEnvironmentFlagEnabled(const char* name);

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
    TemporalUpscalerDlssQualityMode qualityMode,
    TemporalUpscalerDlssPreset presetOverride
) {
    if (parameters == nullptr) {
        return;
    }

    const int preset =
        NgxPresetHint(EffectiveDlssPreset(qualityMode, presetOverride));
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

bool MvJitteredDefaultForPreset(
    TemporalUpscalerDlssQualityMode qualityMode,
    TemporalUpscalerDlssPreset presetOverride
) {
    (void)qualityMode;
    (void)presetOverride;
    return !ProcessEnvironmentFlagEnabled(
            "SE_DLSS_DISABLE_MV_JITTERED_DEFAULT"
        ) &&
        !ProcessEnvironmentFlagEnabled(
            "SE_DLSS_DISABLE_M_PRESET_MV_JITTERED_DEFAULT"
        ) &&
        !ProcessEnvironmentFlagEnabled(
            "SE_DLSS_DISABLE_M_PRESET_INPUT_DEFAULTS"
        );
}

u32 NgxDlssCreateFlags(
    TemporalUpscalerDlssQualityMode qualityMode,
    TemporalUpscalerDlssPreset presetOverride
) {
    u32 flags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    std::optional<bool> mvJitteredOverride =
        ProcessEnvironmentFlagOverride("SE_DLSS_CREATE_FLAG_MV_JITTERED");
    if (!mvJitteredOverride.has_value()) {
        mvJitteredOverride = ProcessEnvironmentFlagOverride("SE_DLSS_MV_JITTERED");
    }
    const bool mvJittered =
        mvJitteredOverride.value_or(
            MvJitteredDefaultForPreset(qualityMode, presetOverride)
        );
    if (mvJittered) {
        flags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
    }
    if (ProcessEnvironmentFlagEnabled("SE_DLSS_CREATE_FLAG_DEPTH_INVERTED") ||
        ProcessEnvironmentFlagEnabled("SE_DLSS_DEPTH_INVERTED")) {
        flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    }
    return flags;
}

bool DlssOutputSubrectsEnabled(
    const TemporalUpscalerEvaluateRequest& request
) {
    if (const std::optional<bool> overrideValue =
            ProcessEnvironmentFlagOverride("SE_DLSS_ENABLE_OUTPUT_SUBRECTS")) {
        return *overrideValue;
    }
    if (const std::optional<bool> overrideValue =
            ProcessEnvironmentFlagOverride("SE_DLSS_OUTPUT_SUBRECTS")) {
        return *overrideValue;
    }

    return request.renderExtent.width != request.outputExtent.width ||
        request.renderExtent.height != request.outputExtent.height;
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

std::string ReadProcessEnvironmentString(const char* name);

enum class DlssRuntimeFlavor : u32 {
    Default = 0,
    Release = 1,
    Dev = 2
};

struct DlssRuntimeSelection {
    DlssRuntimeFlavor flavor = DlssRuntimeFlavor::Release;
    std::filesystem::path directory;
    std::filesystem::path dllPath;
    u32 pathOverridden = 0;
    u32 pathFound = 0;
    u32 dllFound = 0;
    u32 dllSizeBytes = 0;
    u32 dllHash = 0;
};

struct DlssRuntimeSelectionCache {
    bool valid = false;
    DlssRuntimeFlavor flavor = DlssRuntimeFlavor::Release;
    std::filesystem::path sdkRoot;
    std::filesystem::path runtimePathOverride;
    DlssRuntimeSelection selection{};
};

DlssRuntimeSelectionCache g_DlssRuntimeSelectionCache{};

DlssRuntimeFlavor DlssRuntimeFlavorFromEnvironment() {
    std::string value = ReadProcessEnvironmentString("SE_DLSS_RUNTIME_FLAVOR");
    if (value.empty()) {
        value = ReadProcessEnvironmentString("SE_NGX_RUNTIME_FLAVOR");
    }

    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))
        ));
    }

    if (normalized == "dev" ||
        normalized == "debug" ||
        normalized == "development") {
        return DlssRuntimeFlavor::Dev;
    }
    return DlssRuntimeFlavor::Release;
}

const char* DlssRuntimeFlavorDirectoryName(DlssRuntimeFlavor flavor) {
    return flavor == DlssRuntimeFlavor::Dev ? "dev" : "rel";
}

std::filesystem::path DlssRuntimePathOverrideFromEnvironment() {
    std::string value = ReadProcessEnvironmentString("SE_DLSS_RUNTIME_PATH");
    if (value.empty()) {
        value = ReadProcessEnvironmentString("SE_NGX_RUNTIME_PATH");
    }
    if (value.empty()) {
        return {};
    }

    return std::filesystem::absolute(std::filesystem::path(value));
}

u32 HashFileFnv1a32(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return 0u;
    }

    u32 hash = 2166136261u;
    char buffer[4096];
    while (stream) {
        stream.read(buffer, sizeof(buffer));
        const std::streamsize count = stream.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 16777619u;
        }
    }
    return hash;
}

u32 FileSizeU32(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return 0u;
    }
    return static_cast<u32>(
        std::min<std::uintmax_t>(
            size,
            static_cast<std::uintmax_t>(std::numeric_limits<u32>::max())
        )
    );
}

DlssRuntimeSelection SelectDlssRuntime(
    const std::filesystem::path& sdkRoot
) {
    const DlssRuntimeFlavor flavor = DlssRuntimeFlavorFromEnvironment();
    const std::filesystem::path runtimePathOverride =
        DlssRuntimePathOverrideFromEnvironment();
    if (g_DlssRuntimeSelectionCache.valid &&
        g_DlssRuntimeSelectionCache.flavor == flavor &&
        g_DlssRuntimeSelectionCache.sdkRoot == sdkRoot &&
        g_DlssRuntimeSelectionCache.runtimePathOverride ==
            runtimePathOverride) {
        return g_DlssRuntimeSelectionCache.selection;
    }

    DlssRuntimeSelection selection{};
    selection.flavor = flavor;
    if (!runtimePathOverride.empty()) {
        selection.directory = runtimePathOverride;
        selection.pathOverridden = 1u;
    } else {
        selection.directory =
            sdkRoot /
            "lib" /
            "Windows_x86_64" /
            DlssRuntimeFlavorDirectoryName(selection.flavor);
    }
    selection.dllPath = selection.directory / "nvngx_dlss.dll";
    selection.dllFound = FileExists(selection.dllPath) ? 1u : 0u;
    selection.pathFound = selection.dllFound;
    if (selection.dllFound != 0u) {
        selection.dllSizeBytes = FileSizeU32(selection.dllPath);
        selection.dllHash = HashFileFnv1a32(selection.dllPath);
    }
    g_DlssRuntimeSelectionCache.valid = true;
    g_DlssRuntimeSelectionCache.flavor = flavor;
    g_DlssRuntimeSelectionCache.sdkRoot = sdkRoot;
    g_DlssRuntimeSelectionCache.runtimePathOverride = runtimePathOverride;
    g_DlssRuntimeSelectionCache.selection = selection;
    return selection;
}

void PopulateDlssRuntimeSelectionStatus(
    const TemporalUpscalerRuntimeRequest& request,
    TemporalUpscalerRuntimeStatus& status
) {
    const DlssRuntimeSelection selection =
        SelectDlssRuntime(request.packageStatus.sdkRoot);
    status.runtimeFlavor = static_cast<u32>(selection.flavor);
    status.runtimePathOverridden = selection.pathOverridden;
    status.runtimePathFound = selection.pathFound;
    status.runtimePath = selection.directory.string();
    status.runtimeDllFound = selection.dllFound;
    status.runtimeDllSizeBytes = selection.dllSizeBytes;
    status.runtimeDllHash = selection.dllHash;
}

void PopulateNgxFeatureRequirementStatus(
    const TemporalUpscalerRuntimeRequest& request,
    TemporalUpscalerRuntimeStatus& status
) {
    if (status.runtimePath.empty()) {
        PopulateDlssRuntimeSelectionStatus(request, status);
    }
    const std::filesystem::path appDataPath =
        request.applicationDataPath.empty()
            ? DefaultNgxApplicationDataPath()
            : request.applicationDataPath;
    std::error_code error;
    std::filesystem::create_directories(appDataPath, error);

    const std::filesystem::path runtimePath = status.runtimePath;
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
    std::filesystem::path cachedSdkRoot;
    VkDevice cachedStatusDevice = VK_NULL_HANDLE;
    VkExtent2D cachedDisplayExtent{};
    TemporalUpscalerDlssQualityMode cachedQualityMode =
        TemporalUpscalerDlssQualityMode::Quality;
    TemporalUpscalerDlssPreset cachedPreset =
        TemporalUpscalerDlssPreset::Default;
    TemporalUpscalerRuntimeStatus cachedStatus{};
    NVSDK_NGX_Handle* dlssFeatureHandle = nullptr;
    NVSDK_NGX_Parameter* dlssParameters = nullptr;
    VkExtent2D dlssFeatureRenderExtent{};
    VkExtent2D dlssFeatureOutputExtent{};
    TemporalUpscalerDlssQualityMode dlssFeatureQualityMode =
        TemporalUpscalerDlssQualityMode::Quality;
    TemporalUpscalerDlssPreset dlssFeaturePreset =
        TemporalUpscalerDlssPreset::Default;
    u32 dlssFeatureCreateFlags = 0;
    bool dlssFeatureOutputSubrectsEnabled = false;
    bool dlssFeatureResetPending = false;
};

DlssRuntimeCache g_DlssRuntimeCache{};

bool DlssShutdownTraceEnabled() {
    return ProcessEnvironmentFlagEnabled("SE_SHUTDOWN_TRACE") ||
        ProcessEnvironmentFlagEnabled("SE_DLSS_SHUTDOWN_TRACE");
}

double DlssShutdownElapsedMilliseconds(
    std::chrono::steady_clock::time_point startTime
) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime
    ).count();
}

void ReleaseDlssFeatureCache() {
    const bool traceShutdown = DlssShutdownTraceEnabled();
    const auto shutdownStartTime = std::chrono::steady_clock::now();
    auto traceStep = [&](const char* label) {
        if (!traceShutdown) {
            return;
        }
        std::cout << "[shutdown] dlss_feature_cache " << label << " +"
            << DlssShutdownElapsedMilliseconds(shutdownStartTime) << "ms"
            << std::endl;
    };

    traceStep("begin");
    if (g_DlssRuntimeCache.dlssFeatureHandle != nullptr) {
        NVSDK_NGX_VULKAN_ReleaseFeature(g_DlssRuntimeCache.dlssFeatureHandle);
        g_DlssRuntimeCache.dlssFeatureHandle = nullptr;
        traceStep("release_feature");
    }
    if (g_DlssRuntimeCache.dlssParameters != nullptr) {
        NVSDK_NGX_VULKAN_DestroyParameters(g_DlssRuntimeCache.dlssParameters);
        g_DlssRuntimeCache.dlssParameters = nullptr;
        traceStep("destroy_parameters");
    }
    g_DlssRuntimeCache.dlssFeatureRenderExtent = {};
    g_DlssRuntimeCache.dlssFeatureOutputExtent = {};
    g_DlssRuntimeCache.dlssFeatureQualityMode =
        TemporalUpscalerDlssQualityMode::Quality;
    g_DlssRuntimeCache.dlssFeaturePreset =
        TemporalUpscalerDlssPreset::Default;
    g_DlssRuntimeCache.dlssFeatureCreateFlags = 0u;
    g_DlssRuntimeCache.dlssFeatureOutputSubrectsEnabled = false;
    g_DlssRuntimeCache.dlssFeatureResetPending = false;
    traceStep("end");
}

bool ExtentsEqual(VkExtent2D lhs, VkExtent2D rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

bool DlssRuntimeStatusCacheMatches(
    const TemporalUpscalerRuntimeRequest& request
) {
    return g_DlssRuntimeCache.statusCached &&
        g_DlssRuntimeCache.cachedStatusDevice == request.device &&
        g_DlssRuntimeCache.cachedSdkRoot == request.packageStatus.sdkRoot &&
        g_DlssRuntimeCache.cachedDisplayExtent.width ==
            request.displayExtent.width &&
        g_DlssRuntimeCache.cachedDisplayExtent.height ==
            request.displayExtent.height &&
        g_DlssRuntimeCache.cachedQualityMode == request.dlssQualityMode &&
        g_DlssRuntimeCache.cachedPreset == request.dlssPreset;
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
    if (g_DlssRuntimeCache.dlssFeaturePreset != request.dlssPreset) {
        return TemporalUpscalerFeatureRecreationReason::PresetChanged;
    }
    if (g_DlssRuntimeCache.dlssFeatureCreateFlags != createFlags) {
        return TemporalUpscalerFeatureRecreationReason::CreateFlagsChanged;
    }
    if (g_DlssRuntimeCache.dlssFeatureOutputSubrectsEnabled !=
        DlssOutputSubrectsEnabled(request)) {
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
        static_cast<u32>(
            EffectiveDlssPreset(request.dlssQualityMode, request.dlssPreset)
        );

    const bool vulkanHandlesAvailable =
        request.instance != VK_NULL_HANDLE &&
        request.physicalDevice != VK_NULL_HANDLE &&
        request.device != VK_NULL_HANDLE &&
        request.getInstanceProcAddr != nullptr &&
        request.getDeviceProcAddr != nullptr &&
        request.displayExtent.width > 0u &&
        request.displayExtent.height > 0u;
    if (vulkanHandlesAvailable &&
        g_DlssRuntimeCache.initialized &&
        DlssRuntimeStatusCacheMatches(request)) {
        return g_DlssRuntimeCache.cachedStatus;
    }

    PopulateDlssRuntimeSelectionStatus(request, status);

    if (!vulkanHandlesAvailable) {
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

        const std::filesystem::path runtimePath = status.runtimePath;
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

    if (DlssRuntimeStatusCacheMatches(request)) {
        return g_DlssRuntimeCache.cachedStatus;
    }

    PopulateNgxCapabilityStatus(request, status);
    g_DlssRuntimeCache.statusCached = true;
    g_DlssRuntimeCache.cachedSdkRoot = request.packageStatus.sdkRoot;
    g_DlssRuntimeCache.cachedStatusDevice = request.device;
    g_DlssRuntimeCache.cachedDisplayExtent = request.displayExtent;
    g_DlssRuntimeCache.cachedQualityMode = request.dlssQualityMode;
    g_DlssRuntimeCache.cachedPreset = request.dlssPreset;
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

u32 DlssCreateFlagBit(u32 flags, u32 bit) {
    return (flags & bit) != 0u ? 1u : 0u;
}

u32 ExtentMatches(
    const TemporalUpscalerVulkanImageResource& image,
    const VkExtent2D& extent
) {
    return image.extent.width == extent.width &&
        image.extent.height == extent.height
        ? 1u
        : 0u;
}

bool NearlyEqual(f32 a, f32 b) {
    return std::fabs(a - b) <= 0.0001f;
}

void PopulateDlssEvaluateInputDiagnostics(
    const TemporalUpscalerEvaluateRequest& request,
    TemporalUpscalerEvaluateStatus& status
) {
    status.createFlagIsHdr = DlssCreateFlagBit(
        status.createFlags,
        NVSDK_NGX_DLSS_Feature_Flags_IsHDR
    );
    status.createFlagMvLowRes = DlssCreateFlagBit(
        status.createFlags,
        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes
    );
    status.createFlagMvJittered = DlssCreateFlagBit(
        status.createFlags,
        NVSDK_NGX_DLSS_Feature_Flags_MVJittered
    );
    status.createFlagDepthInverted = DlssCreateFlagBit(
        status.createFlags,
        NVSDK_NGX_DLSS_Feature_Flags_DepthInverted
    );
    status.createFlagAutoExposure = DlssCreateFlagBit(
        status.createFlags,
        NVSDK_NGX_DLSS_Feature_Flags_AutoExposure
    );

    status.inputColorFormat = static_cast<u32>(request.inputColor.format);
    status.inputDepthFormat = static_cast<u32>(request.inputDepth.format);
    status.inputMotionVectorFormat =
        static_cast<u32>(request.inputMotionVectors.format);
    status.inputColorWidth = request.inputColor.extent.width;
    status.inputColorHeight = request.inputColor.extent.height;
    status.inputDepthWidth = request.inputDepth.extent.width;
    status.inputDepthHeight = request.inputDepth.extent.height;
    status.inputMotionVectorWidth = request.inputMotionVectors.extent.width;
    status.inputMotionVectorHeight = request.inputMotionVectors.extent.height;
    status.inputDepthAspectMask = request.inputDepth.aspectMask;
    status.inputMotionVectorAspectMask = request.inputMotionVectors.aspectMask;
    status.inputDepthMatchesRenderExtent =
        ExtentMatches(request.inputDepth, request.renderExtent);
    status.inputMotionVectorMatchesRenderExtent =
        ExtentMatches(request.inputMotionVectors, request.renderExtent);
    status.motionVectorScaleUnitSpace =
        NearlyEqual(std::fabs(request.motionVectorScaleX), 1.0f) &&
            NearlyEqual(std::fabs(request.motionVectorScaleY), 1.0f)
        ? 1u
        : 0u;
    status.motionVectorScaleMatchesRenderExtent =
        NearlyEqual(
            std::fabs(request.motionVectorScaleX),
            static_cast<f32>(request.renderExtent.width)
        ) &&
            NearlyEqual(
                std::fabs(request.motionVectorScaleY),
                static_cast<f32>(request.renderExtent.height)
            )
        ? 1u
        : 0u;
    status.motionVectorScalePixelSpace =
        status.motionVectorScaleMatchesRenderExtent;
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

std::string ReadProcessEnvironmentString(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t size = 0u;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

bool EvaluateResourceDiagnosticsEnabled() {
    const std::string value =
        ReadProcessEnvironmentString("SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS");
    if (value.empty()) {
        return false;
    }

    std::string normalized;
    for (char ch : value) {
        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))
        ));
    }
    return normalized != "0" &&
        normalized != "false" &&
        normalized != "off" &&
        normalized != "no";
}

std::optional<bool> ProcessEnvironmentFlagOverride(const char* name) {
    const std::string value = ReadProcessEnvironmentString(name);
    if (value.empty()) {
        return std::nullopt;
    }

    std::string normalized;
    for (char ch : value) {
        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))
        ));
    }
    if (normalized == "1" ||
        normalized == "true" ||
        normalized == "on" ||
        normalized == "yes") {
        return true;
    }
    if (normalized == "0" ||
        normalized == "false" ||
        normalized == "off" ||
        normalized == "no") {
        return false;
    }
    return true;
}

bool ProcessEnvironmentFlagEnabled(const char* name) {
    return ProcessEnvironmentFlagOverride(name).value_or(false);
}

bool DlssOptionalMaskBindingDisabled() {
    return ProcessEnvironmentFlagEnabled("SE_DLSS_DISABLE_OPTIONAL_MASK_BINDINGS") ||
        ProcessEnvironmentFlagEnabled("SE_DLSS_DISABLE_OPTIONAL_MASK_BINDING");
}

bool DlssBiasCurrentColorMaskBindingDisabled() {
    return DlssOptionalMaskBindingDisabled() ||
        ProcessEnvironmentFlagEnabled("SE_DLSS_DISABLE_BIAS_CURRENT_COLOR_MASK_BINDING") ||
        ProcessEnvironmentFlagEnabled("SE_DLSS_DISABLE_BIAS_CURRENT_COLOR_MASK_BIND");
}

bool DlssTransparencyMaskBindingDisabled() {
    return DlssOptionalMaskBindingDisabled() ||
        ProcessEnvironmentFlagEnabled("SE_DLSS_DISABLE_TRANSPARENCY_MASK_BINDING") ||
        ProcessEnvironmentFlagEnabled("SE_DLSS_DISABLE_TRANSPARENCY_MASK_BIND");
}

template <typename Handle>
std::uint64_t VulkanHandleValue(Handle handle) {
    if constexpr (std::is_pointer_v<Handle>) {
        return static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(handle)
        );
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}

template <typename Handle>
std::string VulkanHandleHex(Handle handle) {
    std::ostringstream stream;
    stream << "0x" << std::hex << VulkanHandleValue(handle);
    return stream.str();
}

void TraceEvaluateResource(
    std::uint32_t sample,
    const char* name,
    const TemporalUpscalerVulkanImageResource& image
) {
    std::cout
        << "SelfEngineDLSSResourceTrace"
        << " sample=" << sample
        << " name=" << name
        << " image=" << VulkanHandleHex(image.image)
        << " view=" << VulkanHandleHex(image.imageView)
        << " format=" << static_cast<int>(image.format)
        << " extent=" << image.extent.width << "x" << image.extent.height
        << " aspect=0x" << std::hex << image.aspectMask << std::dec
        << " readWrite=" << (image.readWrite ? 1 : 0)
        << " intendedLayout=VK_IMAGE_LAYOUT_GENERAL"
        << '\n';
}

void TraceEvaluateResources(const TemporalUpscalerEvaluateRequest& request) {
    if (!EvaluateResourceDiagnosticsEnabled()) {
        return;
    }

    static std::uint32_t s_traceCount = 0u;
    constexpr std::uint32_t kMaxTraceSamples = 16u;
    if (s_traceCount >= kMaxTraceSamples) {
        return;
    }

    const std::uint32_t sample = ++s_traceCount;
    std::cout
        << "SelfEngineDLSSResourceTrace"
        << " sample=" << sample
        << " phase=evaluate"
        << " quality=" << static_cast<int>(request.dlssQualityMode)
        << " preset=" << static_cast<int>(request.dlssPreset)
        << " runtimeFlavor=" << request.runtimeStatus.runtimeFlavor
        << " runtimePathOverridden="
        << request.runtimeStatus.runtimePathOverridden
        << " runtimePathFound=" << request.runtimeStatus.runtimePathFound
        << " runtimePath=" << request.runtimeStatus.runtimePath
        << " runtimeDllFound=" << request.runtimeStatus.runtimeDllFound
        << " runtimeDllSizeBytes="
        << request.runtimeStatus.runtimeDllSizeBytes
        << " runtimeDllHash=" << request.runtimeStatus.runtimeDllHash
        << " render=" << request.renderExtent.width << "x"
        << request.renderExtent.height
        << " output=" << request.outputExtent.width << "x"
        << request.outputExtent.height
        << " reset=" << request.reset
        << " jitter=" << request.jitterOffsetX << ","
        << request.jitterOffsetY
        << " mvScale=" << request.motionVectorScaleX << ","
        << request.motionVectorScaleY
        << '\n';
    TraceEvaluateResource(sample, "inputColor", request.inputColor);
    TraceEvaluateResource(sample, "inputDepth", request.inputDepth);
    TraceEvaluateResource(
        sample,
        "inputMotionVectors",
        request.inputMotionVectors
    );
    TraceEvaluateResource(
        sample,
        "inputBiasCurrentColorMask",
        request.inputBiasCurrentColorMask
    );
    TraceEvaluateResource(
        sample,
        "inputTransparencyMask",
        request.inputTransparencyMask
    );
    TraceEvaluateResource(sample, "outputColor", request.outputColor);
}

void TraceOptionalMaskBindings(
    bool biasCurrentColorMaskReady,
    bool transparencyMaskReady,
    bool biasCurrentColorMaskBound,
    bool transparencyMaskBound
) {
    if (!EvaluateResourceDiagnosticsEnabled()) {
        return;
    }

    std::cout
        << "SelfEngineDLSSResourceTrace"
        << " phase=optionalMaskBindings"
        << " biasReady=" << (biasCurrentColorMaskReady ? 1 : 0)
        << " transparencyReady=" << (transparencyMaskReady ? 1 : 0)
        << " biasBound=" << (biasCurrentColorMaskBound ? 1 : 0)
        << " transparencyBound=" << (transparencyMaskBound ? 1 : 0)
        << " disableOptionalMasks="
        << (DlssOptionalMaskBindingDisabled() ? 1 : 0)
        << " disableBias="
        << (DlssBiasCurrentColorMaskBindingDisabled() ? 1 : 0)
        << " disableTransparency="
        << (DlssTransparencyMaskBindingDisabled() ? 1 : 0)
        << '\n';
}

void TraceDlssLifecycle(
    const char* phase,
    const TemporalUpscalerEvaluateRequest& request,
    const TemporalUpscalerEvaluateStatus& status,
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Success
) {
    if (!EvaluateResourceDiagnosticsEnabled()) {
        return;
    }

    static std::uint32_t s_lifecycleEventCount = 0u;
    constexpr std::uint32_t kMaxLifecycleEvents = 64u;
    if (s_lifecycleEventCount >= kMaxLifecycleEvents) {
        return;
    }

    const std::uint32_t event = ++s_lifecycleEventCount;
    std::cout
        << "SelfEngineDLSSLifecycleTrace"
        << " event=" << event
        << " phase=" << phase
        << " quality=" << static_cast<int>(request.dlssQualityMode)
        << " preset=" << static_cast<int>(request.dlssPreset)
        << " runtimeFlavor=" << request.runtimeStatus.runtimeFlavor
        << " runtimePathOverridden="
        << request.runtimeStatus.runtimePathOverridden
        << " runtimePathFound=" << request.runtimeStatus.runtimePathFound
        << " runtimePath=" << request.runtimeStatus.runtimePath
        << " runtimeDllFound=" << request.runtimeStatus.runtimeDllFound
        << " runtimeDllSizeBytes="
        << request.runtimeStatus.runtimeDllSizeBytes
        << " runtimeDllHash=" << request.runtimeStatus.runtimeDllHash
        << " render=" << request.renderExtent.width << "x"
        << request.renderExtent.height
        << " output=" << request.outputExtent.width << "x"
        << request.outputExtent.height
        << " createFlags=" << status.createFlags
        << " recreationReason="
        << static_cast<int>(status.featureRecreationReason)
        << " featureCreateAttempted=" << status.featureCreateAttempted
        << " featureCreated="
        << (g_DlssRuntimeCache.dlssFeatureHandle != nullptr ? 1 : 0)
        << " featureRecreated=" << status.featureRecreated
        << " evaluateAttempted=" << status.evaluateAttempted
        << " outputReady=" << status.outputReady
        << " result=" << static_cast<std::uint32_t>(result)
        << " featureHandle="
        << VulkanHandleHex(g_DlssRuntimeCache.dlssFeatureHandle)
        << " parameters="
        << VulkanHandleHex(g_DlssRuntimeCache.dlssParameters)
        << '\n';
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
    status.createFlags = NgxDlssCreateFlags(
        request.dlssQualityMode,
        request.dlssPreset
    );
    const bool effectiveReset =
        request.reset != 0u ||
        g_DlssRuntimeCache.dlssFeatureResetPending;
    status.reset = effectiveReset ? 1u : 0u;
    status.jitterOffsetX = request.jitterOffsetX;
    status.jitterOffsetY = request.jitterOffsetY;
    status.motionVectorScaleX = request.motionVectorScaleX;
    status.motionVectorScaleY = request.motionVectorScaleY;
    status.sharpness = request.sharpness;
    PopulateDlssEvaluateInputDiagnostics(request, status);

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
    TraceDlssLifecycle("featureRecreationCheck", request, status);
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
            request.dlssQualityMode,
            request.dlssPreset
        );

        NVSDK_NGX_DLSS_Create_Params createParams{};
        const bool outputSubrectsEnabled =
            DlssOutputSubrectsEnabled(request);
        createParams.Feature.InWidth = request.renderExtent.width;
        createParams.Feature.InHeight = request.renderExtent.height;
        createParams.Feature.InTargetWidth = request.outputExtent.width;
        createParams.Feature.InTargetHeight = request.outputExtent.height;
        createParams.Feature.InPerfQualityValue =
            NgxQualityMode(request.dlssQualityMode);
        createParams.InFeatureCreateFlags =
            static_cast<int>(status.createFlags);
        createParams.InEnableOutputSubrects = outputSubrectsEnabled;

        status.featureCreateAttempted = 1u;
        TraceDlssLifecycle("featureCreateRequest", request, status);
        BeginVulkanDebugLabel(
            request.device,
            request.commandBuffer,
            "SelfEngine.DLSS.FeatureCreate",
            0.25f,
            0.55f,
            1.0f,
            1.0f
        );
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
        EndVulkanDebugLabel(request.device, request.commandBuffer);
        status.featureCreateResult = static_cast<u32>(createResult);
        TraceDlssLifecycle(
            "featureCreateResult",
            request,
            status,
            createResult
        );
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
        g_DlssRuntimeCache.dlssFeaturePreset = request.dlssPreset;
        g_DlssRuntimeCache.dlssFeatureCreateFlags = status.createFlags;
        g_DlssRuntimeCache.dlssFeatureOutputSubrectsEnabled =
            outputSubrectsEnabled;
        g_DlssRuntimeCache.dlssFeatureResetPending = true;
        status.featureRecreated = 1u;
    } else {
        status.featureCreateResult =
            static_cast<u32>(NVSDK_NGX_Result_Success);
        TraceDlssLifecycle("featureReuse", request, status);
    }
    status.featureCreated =
        g_DlssRuntimeCache.dlssFeatureHandle != nullptr ? 1u : 0u;
    if (status.featureRecreated > 0u) {
        status.fallbackReason =
            TemporalUpscalerEvaluateFallbackReason::FeatureCreateWarmup;
        TraceDlssLifecycle("featureCreateWarmup", request, status);
        return status;
    }

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
    const bool biasCurrentColorMaskBound =
        biasCurrentColorMaskReady &&
        !DlssBiasCurrentColorMaskBindingDisabled();
    const bool transparencyMaskBound =
        transparencyMaskReady && !DlssTransparencyMaskBindingDisabled();
    if (biasCurrentColorMaskBound) {
        biasCurrentColorMaskResource =
            NgxImageResource(request.inputBiasCurrentColorMask);
        status.biasCurrentColorMaskReady = 1u;
    }
    if (transparencyMaskBound) {
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
        biasCurrentColorMaskBound ? &biasCurrentColorMaskResource : nullptr;
    evalParams.pInTransparencyMask =
        transparencyMaskBound ? &transparencyMaskResource : nullptr;
    evalParams.InJitterOffsetX = request.jitterOffsetX;
    evalParams.InJitterOffsetY = request.jitterOffsetY;
    evalParams.InRenderSubrectDimensions.Width = request.renderExtent.width;
    evalParams.InRenderSubrectDimensions.Height = request.renderExtent.height;
    evalParams.InColorSubrectBase = { 0u, 0u };
    evalParams.InDepthSubrectBase = { 0u, 0u };
    evalParams.InMVSubrectBase = { 0u, 0u };
    evalParams.InTranslucencySubrectBase = { 0u, 0u };
    evalParams.InBiasCurrentColorSubrectBase = { 0u, 0u };
    evalParams.InOutputSubrectBase = { 0u, 0u };
    evalParams.InReset = effectiveReset ? 1 : 0;
    evalParams.InMVScaleX = request.motionVectorScaleX;
    evalParams.InMVScaleY = request.motionVectorScaleY;
    evalParams.InPreExposure = 1.0f;
    evalParams.InExposureScale = 1.0f;

    TraceEvaluateResources(request);
    TraceOptionalMaskBindings(
        biasCurrentColorMaskReady,
        transparencyMaskReady,
        biasCurrentColorMaskBound,
        transparencyMaskBound
    );

    status.evaluateAttempted = 1u;
    TraceDlssLifecycle("evaluateRequest", request, status);
    BeginVulkanDebugLabel(
        request.device,
        request.commandBuffer,
        "SelfEngine.DLSS.Evaluate",
        0.1f,
        0.75f,
        0.35f,
        1.0f
    );
    const NVSDK_NGX_Result evaluateResult =
        NGX_VULKAN_EVALUATE_DLSS_EXT(
            request.commandBuffer,
            g_DlssRuntimeCache.dlssFeatureHandle,
            g_DlssRuntimeCache.dlssParameters,
            &evalParams
        );
    EndVulkanDebugLabel(request.device, request.commandBuffer);
    status.evaluateResult = static_cast<u32>(evaluateResult);
    if (NVSDK_NGX_FAILED(evaluateResult)) {
        TraceDlssLifecycle(
            "evaluateResult",
            request,
            status,
            evaluateResult
        );
        status.fallbackReason =
            TemporalUpscalerEvaluateFallbackReason::EvaluateFailed;
        return status;
    }

    status.outputReady = 1u;
    if (effectiveReset) {
        g_DlssRuntimeCache.dlssFeatureResetPending = false;
    }
    TraceDlssLifecycle(
        "evaluateResult",
        request,
        status,
        evaluateResult
    );
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

TemporalUpscalerDlssPreset TemporalUpscalerDlssPresetFromName(
    const std::string& name
) {
    const std::string normalized = NormalizeProviderName(name);
    if (normalized.empty() ||
        normalized == "0" ||
        normalized == "default" ||
        normalized == "auto") {
        return TemporalUpscalerDlssPreset::Default;
    }
    if (normalized == "k" ||
        normalized == "presetk" ||
        normalized == "preset_k" ||
        normalized == "renderpresetk" ||
        normalized == "11") {
        return TemporalUpscalerDlssPreset::K;
    }
    if (normalized == "l" ||
        normalized == "presetl" ||
        normalized == "preset_l" ||
        normalized == "renderpresetl" ||
        normalized == "12") {
        return TemporalUpscalerDlssPreset::L;
    }
    if (normalized == "m" ||
        normalized == "presetm" ||
        normalized == "preset_m" ||
        normalized == "renderpresetm" ||
        normalized == "13") {
        return TemporalUpscalerDlssPreset::M;
    }
    return TemporalUpscalerDlssPreset::Default;
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

    return ProbeDlssPackageCached(status);
}

TemporalUpscalerRuntimeStatus QueryTemporalUpscalerRuntime(
    const TemporalUpscalerRuntimeRequest& request
) {
    TemporalUpscalerRuntimeStatus status{};
    status.requested = request.packageStatus.requested;
    status.dlssQualityMode = static_cast<u32>(request.dlssQualityMode);
    status.recommendedPreset =
        static_cast<u32>(
            EffectiveDlssPreset(request.dlssQualityMode, request.dlssPreset)
        );

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

void ResetTemporalUpscalerFeatureCache() {
#if defined(SE_ENABLE_NVIDIA_DLSS) && SE_ENABLE_NVIDIA_DLSS
    ReleaseDlssFeatureCache();
    g_DlssRuntimeCache.statusCached = false;
    g_DlssRuntimeCache.cachedSdkRoot = std::filesystem::path{};
    g_DlssRuntimeCache.cachedStatusDevice = VK_NULL_HANDLE;
    g_DlssRuntimeCache.cachedDisplayExtent = {};
    g_DlssRuntimeCache.cachedQualityMode =
        TemporalUpscalerDlssQualityMode::Quality;
    g_DlssRuntimeCache.cachedPreset = TemporalUpscalerDlssPreset::Default;
    g_DlssRuntimeCache.cachedStatus = {};
#endif
}

void ShutdownTemporalUpscalerRuntime(VkDevice device) {
#if defined(SE_ENABLE_NVIDIA_DLSS) && SE_ENABLE_NVIDIA_DLSS
    const bool traceShutdown = DlssShutdownTraceEnabled();
    const auto shutdownStartTime = std::chrono::steady_clock::now();
    auto traceStep = [&](const char* label) {
        if (!traceShutdown) {
            return;
        }
        std::cout << "[shutdown] dlss_runtime " << label << " +"
            << DlssShutdownElapsedMilliseconds(shutdownStartTime) << "ms"
            << std::endl;
    };

    traceStep("begin");
    ResetTemporalUpscalerFeatureCache();
    traceStep("reset_feature_cache");
    if (g_DlssRuntimeCache.initialized) {
        const auto shutdownResult = NVSDK_NGX_VULKAN_Shutdown1(
            device != VK_NULL_HANDLE ? device : g_DlssRuntimeCache.device
        );
        if (traceShutdown) {
            std::cout << "[shutdown] dlss_runtime ngx_shutdown_result="
                << static_cast<int>(shutdownResult) << std::endl;
        }
        traceStep("ngx_shutdown1");
    }
    g_DlssRuntimeCache = DlssRuntimeCache{};
    traceStep("end");
#else
    (void)device;
#endif
}

}
