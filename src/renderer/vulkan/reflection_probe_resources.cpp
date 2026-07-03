#include "renderer/vulkan/reflection_probe_resources.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/ibl_generator.h"
#include "renderer/vulkan/image.h"
#include "renderer/vulkan/physical_device.h"

#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace se {

namespace {

struct StbiImageDeleter {
    void operator()(stbi_uc* pixels) const {
        stbi_image_free(pixels);
    }
};

struct LoadedCubemapFace {
    std::unique_ptr<stbi_uc, StbiImageDeleter> pixels;
    int width = 0;
    int height = 0;
};

std::string Lowercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

std::string Trim(std::string value) {
    const auto first = std::find_if_not(
        value.begin(),
        value.end(),
        [](unsigned char character) {
            return std::isspace(character) != 0;
        }
    );
    const auto last = std::find_if_not(
        value.rbegin(),
        value.rend(),
        [](unsigned char character) {
            return std::isspace(character) != 0;
        }
    ).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

std::filesystem::path ResolveAssetPath(std::string_view assetId) {
    std::filesystem::path path{ std::string(assetId) };
    if (path.is_relative()) {
        std::error_code error;
        const std::filesystem::path cwd = std::filesystem::current_path(error);
        if (!error) {
            path = cwd / path;
        }
    }
    return path.lexically_normal();
}

bool RegularFileExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

bool DirectoryExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

std::optional<std::filesystem::path> FindFacePathInDirectory(
    const std::filesystem::path& directory,
    std::span<const std::string_view> names
) {
    static constexpr std::array<std::string_view, 8> kExtensions{
        ".png",
        ".jpg",
        ".jpeg",
        ".tga",
        ".bmp",
        ".ppm",
        ".hdr",
        ".exr"
    };
    for (std::string_view name : names) {
        for (std::string_view extension : kExtensions) {
            std::filesystem::path candidate =
                directory / (std::string(name) + std::string(extension));
            if (RegularFileExists(candidate)) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> ManifestFacePath(
    const std::filesystem::path& manifestPath,
    std::string_view key
) {
    std::ifstream manifest(manifestPath);
    if (!manifest) {
        return std::nullopt;
    }

    std::string line;
    while (std::getline(manifest, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const std::size_t separator = line.find_first_of("=:");
        if (separator == std::string::npos) {
            continue;
        }

        const std::string lineKey = Lowercase(Trim(line.substr(0, separator)));
        if (lineKey != key) {
            continue;
        }

        std::filesystem::path facePath(Trim(line.substr(separator + 1)));
        if (facePath.is_relative()) {
            facePath = manifestPath.parent_path() / facePath;
        }
        if (RegularFileExists(facePath)) {
            return facePath.lexically_normal();
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::array<std::filesystem::path, 6> ResolveCubemapFacePaths(
    std::string_view assetId
) {
    const std::filesystem::path assetPath = ResolveAssetPath(assetId);
    static const std::array<std::array<std::string_view, 5>, 6> kFaceNames{ {
        { "px", "+x", "posx", "right", "positive_x" },
        { "nx", "-x", "negx", "left", "negative_x" },
        { "py", "+y", "posy", "up", "positive_y" },
        { "ny", "-y", "negy", "down", "negative_y" },
        { "pz", "+z", "posz", "front", "positive_z" },
        { "nz", "-z", "negz", "back", "negative_z" }
    } };

    std::array<std::filesystem::path, 6> paths{};
    if (DirectoryExists(assetPath)) {
        for (std::size_t face = 0; face < paths.size(); ++face) {
            const std::optional<std::filesystem::path> facePath =
                FindFacePathInDirectory(assetPath, kFaceNames[face]);
            if (!facePath.has_value()) {
                throw std::runtime_error(
                    "Authored cubemap directory is missing a required face"
                );
            }
            paths[face] = *facePath;
        }
        return paths;
    }

    if (RegularFileExists(assetPath)) {
        static constexpr std::array<std::string_view, 6> kManifestKeys{
            "px",
            "nx",
            "py",
            "ny",
            "pz",
            "nz"
        };
        for (std::size_t face = 0; face < paths.size(); ++face) {
            const std::optional<std::filesystem::path> facePath =
                ManifestFacePath(assetPath, kManifestKeys[face]);
            if (!facePath.has_value()) {
                throw std::runtime_error(
                    "Authored cubemap manifest is missing a required face"
                );
            }
            paths[face] = *facePath;
        }
        return paths;
    }

    throw std::runtime_error("Authored cubemap asset path does not exist");
}

LoadedCubemapFace LoadCubemapFace(const std::filesystem::path& path) {
    LoadedCubemapFace face{};
    int channels = 0;
    face.pixels.reset(stbi_load(
        path.string().c_str(),
        &face.width,
        &face.height,
        &channels,
        STBI_rgb_alpha
    ));
    if (face.pixels == nullptr || face.width <= 0 || face.height <= 0) {
        throw std::runtime_error("Failed to load authored cubemap face");
    }
    return face;
}

u32 CalculateMipLevels(int width, int height) {
    const int largestDimension = std::max(width, height);
    return static_cast<u32>(std::floor(std::log2(largestDimension))) + 1u;
}

std::unique_ptr<VulkanImage> CreateAuthoredCubemapImage(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string_view assetId
) {
    const std::array<std::filesystem::path, 6> facePaths =
        ResolveCubemapFacePaths(assetId);

    std::array<LoadedCubemapFace, 6> faces{};
    for (std::size_t face = 0; face < faces.size(); ++face) {
        faces[face] = LoadCubemapFace(facePaths[face]);
        if (face > 0 &&
            (faces[face].width != faces[0].width ||
             faces[face].height != faces[0].height)) {
            throw std::runtime_error("Authored cubemap faces must have matching dimensions");
        }
    }
    if (faces[0].width != faces[0].height) {
        throw std::runtime_error("Authored cubemap faces must be square");
    }

    const VkDeviceSize faceSize =
        static_cast<VkDeviceSize>(faces[0].width) *
        static_cast<VkDeviceSize>(faces[0].height) *
        4u;
    std::vector<u8> pixels(static_cast<std::size_t>(faceSize) * faces.size());
    for (std::size_t face = 0; face < faces.size(); ++face) {
        std::copy_n(
            faces[face].pixels.get(),
            static_cast<std::size_t>(faceSize),
            pixels.data() + static_cast<std::size_t>(faceSize) * face
        );
    }

    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        static_cast<VkDeviceSize>(pixels.size()),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer->Upload(std::as_bytes(std::span<const u8>(
        pixels.data(),
        pixels.size()
    )));

    const VkExtent2D extent{
        static_cast<u32>(faces[0].width),
        static_cast<u32>(faces[0].height)
    };
    const u32 mipLevels = CalculateMipLevels(faces[0].width, faces[0].height);
    auto image = std::make_unique<VulkanImage>(
        device,
        physicalDevice,
        extent,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        mipLevels,
        6u,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE
    );
    image->TransitionLayout(
        device,
        commandPool,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        mipLevels
    );
    image->CopyFromBuffer(device, commandPool, stagingBuffer->Handle(), 6u);
    image->GenerateMipmaps(physicalDevice, device, commandPool, 6u);
    return image;
}

} // namespace

void VulkanReflectionProbeResources::CreateBuiltInProcedural(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool
) {
    GenerateReflectionProbeCubemap(
        device,
        physicalDevice,
        commandPool,
        m_BuiltInCubemapImage,
        m_BuiltInCubemapView
    );
}

void VulkanReflectionProbeResources::EnsureAuthoredCubemap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string_view assetId
) {
    if (assetId.empty()) {
        return;
    }

    const std::string key(assetId);
    AuthoredCubemapResource& resource = m_AuthoredCubemaps[key];
    if (resource.image != nullptr || resource.loadFailed) {
        return;
    }

    const std::filesystem::path assetPath = ResolveAssetPath(assetId);
    resource.assetFound = RegularFileExists(assetPath) || DirectoryExists(assetPath);
    if (!resource.assetFound) {
        return;
    }

    try {
        resource.image = CreateAuthoredCubemapImage(
            device,
            physicalDevice,
            commandPool,
            assetId
        );
        ++m_AuthoredCubemapUploadCount;
    } catch (...) {
        resource.image.reset();
        resource.loadFailed = true;
    }
}

void VulkanReflectionProbeResources::Release() {
    m_BuiltInCubemapImage.reset();
    m_BuiltInCubemapView = VK_NULL_HANDLE;
    m_AuthoredCubemaps.clear();
    m_AuthoredCubemapUploadCount = 0;
    m_DescriptorSetsBound = 0;
}

bool VulkanReflectionProbeResources::BuiltInProceduralReady(
    VkSampler sampler
) const {
    return m_BuiltInCubemapImage != nullptr &&
        m_BuiltInCubemapView != VK_NULL_HANDLE &&
        sampler != VK_NULL_HANDLE;
}

bool VulkanReflectionProbeResources::AuthoredCubemapReady(
    std::string_view assetId,
    VkSampler sampler
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() &&
        found->second.image != nullptr &&
        found->second.image->View() != VK_NULL_HANDLE &&
        sampler != VK_NULL_HANDLE;
}

bool VulkanReflectionProbeResources::AuthoredCubemapAssetFound(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() && found->second.assetFound;
}

bool VulkanReflectionProbeResources::AuthoredCubemapLoadFailed(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() && found->second.loadFailed;
}

VkImageView VulkanReflectionProbeResources::DescriptorViewFor(
    VkImageView fallbackView,
    VkSampler sampler
) const {
    return BuiltInProceduralReady(sampler) ? m_BuiltInCubemapView : fallbackView;
}

VkImageView VulkanReflectionProbeResources::AuthoredDescriptorViewFor(
    std::string_view assetId,
    VkImageView fallbackView,
    VkSampler sampler
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() &&
            found->second.image != nullptr &&
            found->second.image->View() != VK_NULL_HANDLE &&
            sampler != VK_NULL_HANDLE
        ? found->second.image->View()
        : fallbackView;
}

VkImageView VulkanReflectionProbeResources::BuiltInView() const {
    return m_BuiltInCubemapView;
}

u32 VulkanReflectionProbeResources::FaceSize() const {
    return m_BuiltInCubemapImage != nullptr
        ? m_BuiltInCubemapImage->Extent().width
        : 0u;
}

u32 VulkanReflectionProbeResources::MipCount() const {
    return m_BuiltInCubemapImage != nullptr
        ? m_BuiltInCubemapImage->MipLevels()
        : 0u;
}

VkFormat VulkanReflectionProbeResources::Format() const {
    return m_BuiltInCubemapImage != nullptr
        ? m_BuiltInCubemapImage->Format()
        : VK_FORMAT_UNDEFINED;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapLoadedCount() const {
    u32 count = 0;
    for (const auto& [assetId, resource] : m_AuthoredCubemaps) {
        (void)assetId;
        if (resource.image != nullptr) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapMissingCount() const {
    u32 count = 0;
    for (const auto& [assetId, resource] : m_AuthoredCubemaps) {
        (void)assetId;
        if (!resource.assetFound) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapLoadFailedCount() const {
    u32 count = 0;
    for (const auto& [assetId, resource] : m_AuthoredCubemaps) {
        (void)assetId;
        if (resource.loadFailed) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapUploadCount() const {
    return m_AuthoredCubemapUploadCount;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapFaceSize(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() && found->second.image != nullptr
        ? found->second.image->Extent().width
        : 0u;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapMipCount(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() && found->second.image != nullptr
        ? found->second.image->MipLevels()
        : 0u;
}

VkFormat VulkanReflectionProbeResources::AuthoredCubemapFormat(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() && found->second.image != nullptr
        ? found->second.image->Format()
        : VK_FORMAT_UNDEFINED;
}

void VulkanReflectionProbeResources::SetDescriptorSetsBound(u32 count) {
    m_DescriptorSetsBound = count;
}

u32 VulkanReflectionProbeResources::DescriptorSetsBound() const {
    return m_DescriptorSetsBound;
}

}
