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
#include <optional>
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

struct LoadedEquirectangularImage {
    std::vector<float> pixels;
    int width = 0;
    int height = 0;
    bool hdr = false;
};

struct EquirectangularSample {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct ResolvedAuthoredCubemapSource {
    AuthoredReflectionCubemapSourceType type =
        AuthoredReflectionCubemapSourceType::Unknown;
    std::array<std::filesystem::path, 6> facePaths{};
    std::filesystem::path equirectangularPath{};
};

struct AuthoredCubemapLoadResult {
    std::unique_ptr<VulkanImage> image;
    AuthoredReflectionCubemapSourceType sourceType =
        AuthoredReflectionCubemapSourceType::Unknown;
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

ResolvedAuthoredCubemapSource ResolveAuthoredCubemapSource(
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

    ResolvedAuthoredCubemapSource source{};
    if (DirectoryExists(assetPath)) {
        source.type = AuthoredReflectionCubemapSourceType::SixFace;
        for (std::size_t face = 0; face < source.facePaths.size(); ++face) {
            const std::optional<std::filesystem::path> facePath =
                FindFacePathInDirectory(assetPath, kFaceNames[face]);
            if (!facePath.has_value()) {
                throw std::runtime_error(
                    "Authored cubemap directory is missing a required face"
                );
            }
            source.facePaths[face] = *facePath;
        }
        return source;
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
        bool manifestComplete = true;
        std::array<std::filesystem::path, 6> manifestFacePaths{};
        for (std::size_t face = 0; face < manifestFacePaths.size(); ++face) {
            const std::optional<std::filesystem::path> facePath =
                ManifestFacePath(assetPath, kManifestKeys[face]);
            if (!facePath.has_value()) {
                manifestComplete = false;
                break;
            }
            manifestFacePaths[face] = *facePath;
        }
        if (manifestComplete) {
            source.type = AuthoredReflectionCubemapSourceType::SixFace;
            source.facePaths = manifestFacePaths;
            return source;
        }

        source.type = AuthoredReflectionCubemapSourceType::Equirectangular;
        source.equirectangularPath = assetPath;
        return source;
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

LoadedEquirectangularImage LoadEquirectangularImage(
    const std::filesystem::path& path
) {
    LoadedEquirectangularImage image{};
    const std::string pathString = path.string();
    int channels = 0;
    image.hdr = stbi_is_hdr(pathString.c_str()) != 0;
    if (image.hdr) {
        float* pixels = stbi_loadf(
            pathString.c_str(),
            &image.width,
            &image.height,
            &channels,
            STBI_rgb_alpha
        );
        if (pixels == nullptr || image.width <= 0 || image.height <= 0) {
            stbi_image_free(pixels);
            throw std::runtime_error("Failed to load authored equirectangular HDR image");
        }
        const std::size_t valueCount =
            static_cast<std::size_t>(image.width) *
            static_cast<std::size_t>(image.height) *
            4u;
        image.pixels.assign(pixels, pixels + valueCount);
        stbi_image_free(pixels);
    } else {
        stbi_uc* pixels = stbi_load(
            pathString.c_str(),
            &image.width,
            &image.height,
            &channels,
            STBI_rgb_alpha
        );
        if (pixels == nullptr || image.width <= 0 || image.height <= 0) {
            stbi_image_free(pixels);
            throw std::runtime_error("Failed to load authored equirectangular image");
        }
        const std::size_t valueCount =
            static_cast<std::size_t>(image.width) *
            static_cast<std::size_t>(image.height) *
            4u;
        image.pixels.resize(valueCount);
        for (std::size_t index = 0; index < valueCount; ++index) {
            image.pixels[index] = static_cast<float>(pixels[index]) / 255.0f;
        }
        stbi_image_free(pixels);
    }

    if (image.width != image.height * 2) {
        throw std::runtime_error(
            "Authored equirectangular reflection image must use a 2:1 aspect ratio"
        );
    }
    return image;
}

EquirectangularSample FetchEquirectangularPixel(
    const LoadedEquirectangularImage& image,
    int x,
    int y
) {
    x %= image.width;
    if (x < 0) {
        x += image.width;
    }
    y = std::clamp(y, 0, image.height - 1);
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(x)) *
        4u;
    return EquirectangularSample{
        image.pixels[offset + 0u],
        image.pixels[offset + 1u],
        image.pixels[offset + 2u],
        image.pixels[offset + 3u]
    };
}

EquirectangularSample SampleEquirectangularImage(
    const LoadedEquirectangularImage& image,
    float u,
    float v
) {
    u = u - std::floor(u);
    v = std::clamp(v, 0.0f, 1.0f);

    const float x = u * static_cast<float>(image.width) - 0.5f;
    const float y = v * static_cast<float>(image.height - 1);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const EquirectangularSample c00 =
        FetchEquirectangularPixel(image, x0, y0);
    const EquirectangularSample c10 =
        FetchEquirectangularPixel(image, x0 + 1, y0);
    const EquirectangularSample c01 =
        FetchEquirectangularPixel(image, x0, y0 + 1);
    const EquirectangularSample c11 =
        FetchEquirectangularPixel(image, x0 + 1, y0 + 1);

    const auto lerp = [](float a, float b, float factor) {
        return a + (b - a) * factor;
    };
    const auto bilinear = [&](float EquirectangularSample::*channel) {
        return lerp(
            lerp(c00.*channel, c10.*channel, tx),
            lerp(c01.*channel, c11.*channel, tx),
            ty
        );
    };

    return EquirectangularSample{
        bilinear(&EquirectangularSample::r),
        bilinear(&EquirectangularSample::g),
        bilinear(&EquirectangularSample::b),
        bilinear(&EquirectangularSample::a)
    };
}

std::array<float, 3> CubemapDirection(
    std::size_t face,
    float u,
    float v
) {
    std::array<float, 3> direction{};
    switch (face) {
    case 0:
        direction = { 1.0f, -v, -u };
        break;
    case 1:
        direction = { -1.0f, -v, u };
        break;
    case 2:
        direction = { u, 1.0f, v };
        break;
    case 3:
        direction = { u, -1.0f, -v };
        break;
    case 4:
        direction = { u, -v, 1.0f };
        break;
    default:
        direction = { -u, -v, -1.0f };
        break;
    }

    const float length = std::sqrt(
        direction[0] * direction[0] +
        direction[1] * direction[1] +
        direction[2] * direction[2]
    );
    direction[0] /= length;
    direction[1] /= length;
    direction[2] /= length;
    return direction;
}

u8 EncodeReflectionChannel(float value, bool hdr) {
    value = std::clamp(value, 0.0f, 1.0f);
    if (hdr) {
        value = value <= 0.0031308f
            ? value * 12.92f
            : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    }
    return static_cast<u8>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

std::vector<u8> ConvertEquirectangularToCubemapPixels(
    const LoadedEquirectangularImage& image,
    u32 faceSize
) {
    static constexpr float kPi = 3.14159265358979323846f;
    std::vector<u8> pixels(
        static_cast<std::size_t>(faceSize) *
        static_cast<std::size_t>(faceSize) *
        6u *
        4u
    );

    for (std::size_t face = 0; face < 6u; ++face) {
        for (u32 y = 0; y < faceSize; ++y) {
            for (u32 x = 0; x < faceSize; ++x) {
                const float faceU =
                    (2.0f * (static_cast<float>(x) + 0.5f) /
                     static_cast<float>(faceSize)) -
                    1.0f;
                const float faceV =
                    (2.0f * (static_cast<float>(y) + 0.5f) /
                     static_cast<float>(faceSize)) -
                    1.0f;
                const std::array<float, 3> direction =
                    CubemapDirection(face, faceU, faceV);
                const float longitude =
                    std::atan2(direction[2], direction[0]);
                const float latitude = std::acos(std::clamp(
                    direction[1],
                    -1.0f,
                    1.0f
                ));
                const float sampleU = longitude / (2.0f * kPi) + 0.5f;
                const float sampleV = latitude / kPi;
                const EquirectangularSample sample =
                    SampleEquirectangularImage(image, sampleU, sampleV);

                const std::size_t offset =
                    (((face * static_cast<std::size_t>(faceSize) +
                       static_cast<std::size_t>(y)) *
                      static_cast<std::size_t>(faceSize)) +
                     static_cast<std::size_t>(x)) *
                    4u;
                pixels[offset + 0u] = EncodeReflectionChannel(sample.r, image.hdr);
                pixels[offset + 1u] = EncodeReflectionChannel(sample.g, image.hdr);
                pixels[offset + 2u] = EncodeReflectionChannel(sample.b, image.hdr);
                pixels[offset + 3u] = EncodeReflectionChannel(sample.a, false);
            }
        }
    }

    return pixels;
}

std::unique_ptr<VulkanImage> UploadAuthoredCubemapPixels(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::span<const u8> pixels,
    u32 faceSize
) {
    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        static_cast<VkDeviceSize>(pixels.size()),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer->Upload(std::as_bytes(pixels));

    const VkExtent2D extent{ faceSize, faceSize };
    const u32 mipLevels = CalculateMipLevels(
        static_cast<int>(faceSize),
        static_cast<int>(faceSize)
    );
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

AuthoredCubemapLoadResult CreateAuthoredCubemapImage(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string_view assetId
) {
    const ResolvedAuthoredCubemapSource source =
        ResolveAuthoredCubemapSource(assetId);

    if (source.type == AuthoredReflectionCubemapSourceType::Equirectangular) {
        const LoadedEquirectangularImage equirectangular =
            LoadEquirectangularImage(source.equirectangularPath);
        const u32 faceSize =
            std::max(1u, static_cast<u32>(equirectangular.height / 2));
        std::vector<u8> pixels =
            ConvertEquirectangularToCubemapPixels(equirectangular, faceSize);
        return AuthoredCubemapLoadResult{
            UploadAuthoredCubemapPixels(
                device,
                physicalDevice,
                commandPool,
                std::span<const u8>(pixels.data(), pixels.size()),
                faceSize
            ),
            source.type
        };
    }

    std::array<LoadedCubemapFace, 6> faces{};
    for (std::size_t face = 0; face < faces.size(); ++face) {
        faces[face] = LoadCubemapFace(source.facePaths[face]);
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

    return AuthoredCubemapLoadResult{
        UploadAuthoredCubemapPixels(
            device,
            physicalDevice,
            commandPool,
            std::span<const u8>(pixels.data(), pixels.size()),
            static_cast<u32>(faces[0].width)
        ),
        source.type
    };
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
        AuthoredCubemapLoadResult loadResult = CreateAuthoredCubemapImage(
            device,
            physicalDevice,
            commandPool,
            assetId
        );
        resource.image = std::move(loadResult.image);
        resource.sourceType = loadResult.sourceType;
        ++m_AuthoredCubemapUploadCount;
        if (resource.sourceType ==
            AuthoredReflectionCubemapSourceType::Equirectangular) {
            ++m_AuthoredCubemapEquirectangularConversionCount;
        }
    } catch (...) {
        resource.image.reset();
        resource.sourceType = AuthoredReflectionCubemapSourceType::Unknown;
        resource.loadFailed = true;
    }
}

void VulkanReflectionProbeResources::Release() {
    m_BuiltInCubemapImage.reset();
    m_BuiltInCubemapView = VK_NULL_HANDLE;
    m_AuthoredCubemaps.clear();
    m_AuthoredCubemapUploadCount = 0;
    m_AuthoredCubemapEquirectangularConversionCount = 0;
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

u32 VulkanReflectionProbeResources::AuthoredCubemapSixFaceLoadedCount() const {
    u32 count = 0;
    for (const auto& [assetId, resource] : m_AuthoredCubemaps) {
        (void)assetId;
        if (resource.image != nullptr &&
            resource.sourceType == AuthoredReflectionCubemapSourceType::SixFace) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapEquirectangularLoadedCount() const {
    u32 count = 0;
    for (const auto& [assetId, resource] : m_AuthoredCubemaps) {
        (void)assetId;
        if (resource.image != nullptr &&
            resource.sourceType ==
                AuthoredReflectionCubemapSourceType::Equirectangular) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapEquirectangularConversionCount() const {
    return m_AuthoredCubemapEquirectangularConversionCount;
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

AuthoredReflectionCubemapSourceType VulkanReflectionProbeResources::AuthoredCubemapSourceType(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end()
        ? found->second.sourceType
        : AuthoredReflectionCubemapSourceType::Unknown;
}

void VulkanReflectionProbeResources::SetDescriptorSetsBound(u32 count) {
    m_DescriptorSetsBound = count;
}

u32 VulkanReflectionProbeResources::DescriptorSetsBound() const {
    return m_DescriptorSetsBound;
}

}
