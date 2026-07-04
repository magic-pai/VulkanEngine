#include "renderer/temporal_upscaler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <string_view>

namespace se {
namespace {

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
    return TemporalUpscalerPackageFallbackReason::EvaluateAdapterMissing;
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

}
