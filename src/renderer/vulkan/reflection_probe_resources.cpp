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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
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
    std::vector<float> hdrPixels;
    int width = 0;
    int height = 0;
    bool hdr = false;
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
    bool hdr = false;
    bool prefilteredMipChain = false;
    u32 generatedMipCount = 0;
    u32 prefilterSampleCount = 0;
};

struct AuthoredCubemapPixelData {
    std::vector<u8> pixels;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    bool hdr = false;
    u32 bytesPerTexel = 4;
};

struct AuthoredCubemapMipChain {
    std::vector<u8> pixels;
    std::vector<VkBufferImageCopy> copyRegions;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    bool hdr = false;
    bool prefiltered = false;
    u32 faceSize = 0;
    u32 mipLevels = 1;
    u32 bytesPerTexel = 4;
    u32 prefilterSampleCount = 0;
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

u64 HashCombine64(u64 seed, u64 value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
}

u64 PathHash(const std::filesystem::path& path) {
    return static_cast<u64>(std::hash<std::string>{}(
        path.lexically_normal().string()
    ));
}

u64 FileSignature(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return 0;
    }

    const u64 size = static_cast<u64>(std::filesystem::file_size(path, error));
    if (error) {
        return PathHash(path);
    }
    const auto lastWriteTime = std::filesystem::last_write_time(path, error);
    const u64 timestamp = error
        ? 0u
        : static_cast<u64>(lastWriteTime.time_since_epoch().count());

    u64 signature = 0xcbf29ce484222325ull;
    signature = HashCombine64(signature, PathHash(path));
    signature = HashCombine64(signature, size);
    signature = HashCombine64(signature, timestamp);
    return signature;
}

u64 FileSystemEntrySignature(const std::filesystem::path& path) {
    if (RegularFileExists(path)) {
        return FileSignature(path);
    }

    std::error_code error;
    if (!std::filesystem::is_directory(path, error) || error) {
        return PathHash(path);
    }

    const auto lastWriteTime = std::filesystem::last_write_time(path, error);
    const u64 timestamp = error
        ? 0u
        : static_cast<u64>(lastWriteTime.time_since_epoch().count());
    return HashCombine64(PathHash(path), timestamp);
}

u64 AuthoredCubemapSourceSignature(
    const ResolvedAuthoredCubemapSource& source,
    const std::filesystem::path& rootPath
) {
    u64 signature = HashCombine64(
        PathHash(rootPath),
        static_cast<u64>(source.type)
    );
    if (RegularFileExists(rootPath)) {
        signature = HashCombine64(signature, FileSignature(rootPath));
    }
    if (source.type == AuthoredReflectionCubemapSourceType::Equirectangular) {
        return HashCombine64(signature, FileSignature(source.equirectangularPath));
    }

    for (const std::filesystem::path& facePath : source.facePaths) {
        signature = HashCombine64(signature, FileSignature(facePath));
    }
    return signature;
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
    const std::string pathString = path.string();
    int channels = 0;
    face.hdr = stbi_is_hdr(pathString.c_str()) != 0;
    if (face.hdr) {
        float* pixels = stbi_loadf(
            pathString.c_str(),
            &face.width,
            &face.height,
            &channels,
            STBI_rgb_alpha
        );
        if (pixels == nullptr || face.width <= 0 || face.height <= 0) {
            stbi_image_free(pixels);
            throw std::runtime_error("Failed to load authored HDR cubemap face");
        }
        const std::size_t valueCount =
            static_cast<std::size_t>(face.width) *
            static_cast<std::size_t>(face.height) *
            4u;
        face.hdrPixels.assign(pixels, pixels + valueCount);
        stbi_image_free(pixels);
    } else {
        face.pixels.reset(stbi_load(
            pathString.c_str(),
            &face.width,
            &face.height,
            &channels,
            STBI_rgb_alpha
        ));
        if (face.pixels == nullptr || face.width <= 0 || face.height <= 0) {
            throw std::runtime_error("Failed to load authored cubemap face");
        }
    }
    return face;
}

u32 CalculateMipLevels(int width, int height) {
    const int largestDimension = std::max(width, height);
    return static_cast<u32>(std::floor(std::log2(largestDimension))) + 1u;
}

u32 MipExtent(u32 faceSize, u32 mipLevel) {
    return std::max(1u, faceSize >> mipLevel);
}

VkDeviceSize CubemapMipFaceByteSize(
    u32 faceSize,
    u32 mipLevel,
    u32 bytesPerTexel
) {
    const u32 extent = MipExtent(faceSize, mipLevel);
    return static_cast<VkDeviceSize>(extent) *
        static_cast<VkDeviceSize>(extent) *
        static_cast<VkDeviceSize>(bytesPerTexel);
}

VkCommandBuffer BeginReflectionProbeUploadCommands(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool
) {
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool = commandPool.Handle();
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(
            device.Handle(),
            &allocateInfo,
            &commandBuffer
        ) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate reflection probe upload command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
        throw std::runtime_error("Failed to begin reflection probe upload command buffer");
    }
    return commandBuffer;
}

void EndReflectionProbeUploadCommands(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    VkCommandBuffer commandBuffer
) {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
        throw std::runtime_error("Failed to end reflection probe upload command buffer");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(device.GraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) !=
        VK_SUCCESS) {
        vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
        throw std::runtime_error("Failed to submit reflection probe upload command buffer");
    }

    vkQueueWaitIdle(device.GraphicsQueue());
    vkFreeCommandBuffers(device.Handle(), commandPool.Handle(), 1, &commandBuffer);
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

float Dot3(
    const std::array<float, 3>& a,
    const std::array<float, 3>& b
) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<float, 3> Cross3(
    const std::array<float, 3>& a,
    const std::array<float, 3>& b
) {
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

std::array<float, 3> Normalize3(std::array<float, 3> value) {
    const float length = std::sqrt(std::max(0.0f, Dot3(value, value)));
    if (length <= 0.000001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    const float invLength = 1.0f / length;
    value[0] *= invLength;
    value[1] *= invLength;
    value[2] *= invLength;
    return value;
}

std::array<float, 3> Scale3(const std::array<float, 3>& value, float scale) {
    return { value[0] * scale, value[1] * scale, value[2] * scale };
}

std::array<float, 3> Add3(
    const std::array<float, 3>& a,
    const std::array<float, 3>& b
) {
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}

float RadicalInverseVdc(u32 bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

std::array<float, 2> Hammersley(u32 index, u32 sampleCount) {
    return {
        static_cast<float>(index) / static_cast<float>(sampleCount),
        RadicalInverseVdc(index)
    };
}

std::array<float, 3> ImportanceSampleGgx(
    const std::array<float, 2>& xi,
    float roughness,
    const std::array<float, 3>& normal
) {
    static constexpr float kPi = 3.14159265358979323846f;
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float phi = 2.0f * kPi * xi[0];
    const float cosTheta = std::sqrt(std::clamp(
        (1.0f - xi[1]) / (1.0f + (alphaSquared - 1.0f) * xi[1]),
        0.0f,
        1.0f
    ));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const std::array<float, 3> halfVectorLocal{
        std::cos(phi) * sinTheta,
        std::sin(phi) * sinTheta,
        cosTheta
    };

    const std::array<float, 3> up =
        std::fabs(normal[2]) < 0.999f
            ? std::array<float, 3>{ 0.0f, 0.0f, 1.0f }
            : std::array<float, 3>{ 1.0f, 0.0f, 0.0f };
    const std::array<float, 3> tangent = Normalize3(Cross3(up, normal));
    const std::array<float, 3> bitangent = Cross3(normal, tangent);
    return Normalize3(Add3(
        Add3(
            Scale3(tangent, halfVectorLocal[0]),
            Scale3(bitangent, halfVectorLocal[1])
        ),
        Scale3(normal, halfVectorLocal[2])
    ));
}

std::array<float, 3> Reflect3(
    const std::array<float, 3>& incident,
    const std::array<float, 3>& normal
) {
    return Add3(incident, Scale3(normal, -2.0f * Dot3(incident, normal)));
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

u16 FloatToHalfBits(float value) {
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    const u32 sign = (bits >> 16u) & 0x8000u;
    u32 exponent = (bits >> 23u) & 0xffu;
    u32 mantissa = bits & 0x7fffffu;

    if (exponent == 0xffu) {
        if (mantissa == 0u) {
            return static_cast<u16>(sign | 0x7c00u);
        }
        return static_cast<u16>(sign | 0x7c00u | (mantissa >> 13u));
    }

    const int halfExponent = static_cast<int>(exponent) - 127 + 15;
    if (halfExponent >= 31) {
        return static_cast<u16>(sign | 0x7c00u);
    }
    if (halfExponent <= 0) {
        if (halfExponent < -10) {
            return static_cast<u16>(sign);
        }
        mantissa |= 0x800000u;
        const u32 shift = static_cast<u32>(14 - halfExponent);
        const u32 rounded = (mantissa + (1u << (shift - 1u))) >> shift;
        return static_cast<u16>(sign | rounded);
    }

    u32 roundedMantissa = mantissa + 0x1000u;
    u32 roundedExponent = static_cast<u32>(halfExponent);
    if ((roundedMantissa & 0x800000u) != 0u) {
        roundedMantissa = 0u;
        ++roundedExponent;
        if (roundedExponent >= 31u) {
            return static_cast<u16>(sign | 0x7c00u);
        }
    }

    return static_cast<u16>(
        sign |
        (roundedExponent << 10u) |
        ((roundedMantissa >> 13u) & 0x3ffu)
    );
}

void StoreHalfFloat(std::vector<u8>& pixels, std::size_t offset, float value) {
    const u16 half = FloatToHalfBits(value);
    pixels[offset + 0u] = static_cast<u8>(half & 0xffu);
    pixels[offset + 1u] = static_cast<u8>((half >> 8u) & 0xffu);
}

float HalfBitsToFloat(u16 half) {
    const u32 sign = (static_cast<u32>(half & 0x8000u)) << 16u;
    const u32 exponentBits = (half >> 10u) & 0x1fu;
    u32 mantissa = half & 0x03ffu;
    u32 bits = 0;

    if (exponentBits == 0u) {
        if (mantissa == 0u) {
            bits = sign;
        } else {
            int exponent = 1;
            while ((mantissa & 0x0400u) == 0u) {
                mantissa <<= 1u;
                --exponent;
            }
            mantissa &= 0x03ffu;
            bits = sign |
                (static_cast<u32>(exponent + (127 - 15)) << 23u) |
                (mantissa << 13u);
        }
    } else if (exponentBits == 31u) {
        bits = sign | 0x7f800000u | (mantissa << 13u);
    } else {
        bits = sign |
            ((exponentBits + (127u - 15u)) << 23u) |
            (mantissa << 13u);
    }

    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

u16 LoadHalfFloat(std::span<const u8> pixels, std::size_t offset) {
    return static_cast<u16>(
        static_cast<u16>(pixels[offset + 0u]) |
        (static_cast<u16>(pixels[offset + 1u]) << 8u)
    );
}

AuthoredCubemapPixelData ConvertEquirectangularToCubemapPixels(
    const LoadedEquirectangularImage& image,
    u32 faceSize
) {
    static constexpr float kPi = 3.14159265358979323846f;
    AuthoredCubemapPixelData result{};
    result.hdr = image.hdr;
    result.format = image.hdr
        ? VK_FORMAT_R16G16B16A16_SFLOAT
        : VK_FORMAT_R8G8B8A8_SRGB;
    result.bytesPerTexel = image.hdr ? 8u : 4u;
    result.pixels.resize(
        static_cast<std::size_t>(faceSize) *
        static_cast<std::size_t>(faceSize) *
        6u *
        result.bytesPerTexel
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
                    result.bytesPerTexel;
                if (image.hdr) {
                    StoreHalfFloat(result.pixels, offset + 0u, sample.r);
                    StoreHalfFloat(result.pixels, offset + 2u, sample.g);
                    StoreHalfFloat(result.pixels, offset + 4u, sample.b);
                    StoreHalfFloat(result.pixels, offset + 6u, sample.a);
                } else {
                    result.pixels[offset + 0u] =
                        EncodeReflectionChannel(sample.r, false);
                    result.pixels[offset + 1u] =
                        EncodeReflectionChannel(sample.g, false);
                    result.pixels[offset + 2u] =
                        EncodeReflectionChannel(sample.b, false);
                    result.pixels[offset + 3u] =
                        EncodeReflectionChannel(sample.a, false);
                }
            }
        }
    }

    return result;
}

AuthoredCubemapPixelData PackSixFaceCubemapPixels(
    const std::array<LoadedCubemapFace, 6>& faces
) {
    const bool hdr = std::any_of(
        faces.begin(),
        faces.end(),
        [](const LoadedCubemapFace& face) {
            return face.hdr;
        }
    );
    const u32 bytesPerTexel = hdr ? 8u : 4u;
    const std::size_t faceTexelCount =
        static_cast<std::size_t>(faces[0].width) *
        static_cast<std::size_t>(faces[0].height);
    const std::size_t faceSizeBytes = faceTexelCount * bytesPerTexel;

    AuthoredCubemapPixelData result{};
    result.hdr = hdr;
    result.format = hdr
        ? VK_FORMAT_R16G16B16A16_SFLOAT
        : VK_FORMAT_R8G8B8A8_SRGB;
    result.bytesPerTexel = bytesPerTexel;
    result.pixels.resize(faceSizeBytes * faces.size());

    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        const LoadedCubemapFace& face = faces[faceIndex];
        const std::size_t faceOffset = faceSizeBytes * faceIndex;
        if (!hdr) {
            std::copy_n(
                face.pixels.get(),
                faceSizeBytes,
                result.pixels.data() + faceOffset
            );
            continue;
        }

        for (std::size_t texel = 0; texel < faceTexelCount; ++texel) {
            const std::size_t outputOffset =
                faceOffset + texel * bytesPerTexel;
            if (face.hdr) {
                const std::size_t inputOffset = texel * 4u;
                StoreHalfFloat(result.pixels, outputOffset + 0u, face.hdrPixels[inputOffset + 0u]);
                StoreHalfFloat(result.pixels, outputOffset + 2u, face.hdrPixels[inputOffset + 1u]);
                StoreHalfFloat(result.pixels, outputOffset + 4u, face.hdrPixels[inputOffset + 2u]);
                StoreHalfFloat(result.pixels, outputOffset + 6u, face.hdrPixels[inputOffset + 3u]);
            } else {
                const stbi_uc* input = face.pixels.get() + texel * 4u;
                StoreHalfFloat(
                    result.pixels,
                    outputOffset + 0u,
                    static_cast<float>(input[0]) / 255.0f
                );
                StoreHalfFloat(
                    result.pixels,
                    outputOffset + 2u,
                    static_cast<float>(input[1]) / 255.0f
                );
                StoreHalfFloat(
                    result.pixels,
                    outputOffset + 4u,
                    static_cast<float>(input[2]) / 255.0f
                );
                StoreHalfFloat(
                    result.pixels,
                    outputOffset + 6u,
                    static_cast<float>(input[3]) / 255.0f
                );
            }
        }
    }

    return result;
}

std::array<float, 4> LoadCubemapBaseTexel(
    const AuthoredCubemapPixelData& pixelData,
    u32 faceSize,
    std::size_t face,
    u32 x,
    u32 y
) {
    const std::size_t offset =
        ((face * static_cast<std::size_t>(faceSize) *
          static_cast<std::size_t>(faceSize)) +
         (static_cast<std::size_t>(y) * static_cast<std::size_t>(faceSize)) +
         static_cast<std::size_t>(x)) *
        pixelData.bytesPerTexel;
    if (pixelData.hdr) {
        return {
            HalfBitsToFloat(LoadHalfFloat(pixelData.pixels, offset + 0u)),
            HalfBitsToFloat(LoadHalfFloat(pixelData.pixels, offset + 2u)),
            HalfBitsToFloat(LoadHalfFloat(pixelData.pixels, offset + 4u)),
            HalfBitsToFloat(LoadHalfFloat(pixelData.pixels, offset + 6u))
        };
    }

    return {
        static_cast<float>(pixelData.pixels[offset + 0u]) / 255.0f,
        static_cast<float>(pixelData.pixels[offset + 1u]) / 255.0f,
        static_cast<float>(pixelData.pixels[offset + 2u]) / 255.0f,
        static_cast<float>(pixelData.pixels[offset + 3u]) / 255.0f
    };
}

struct CubemapSampleLocation {
    std::size_t face = 0;
    float x = 0.0f;
    float y = 0.0f;
};

CubemapSampleLocation CubemapLocationForDirection(
    const std::array<float, 3>& direction,
    u32 faceSize
) {
    const float absX = std::fabs(direction[0]);
    const float absY = std::fabs(direction[1]);
    const float absZ = std::fabs(direction[2]);
    std::size_t face = 0;
    float u = 0.0f;
    float v = 0.0f;

    if (absX >= absY && absX >= absZ) {
        if (direction[0] >= 0.0f) {
            face = 0;
            u = -direction[2] / absX;
            v = -direction[1] / absX;
        } else {
            face = 1;
            u = direction[2] / absX;
            v = -direction[1] / absX;
        }
    } else if (absY >= absX && absY >= absZ) {
        if (direction[1] >= 0.0f) {
            face = 2;
            u = direction[0] / absY;
            v = direction[2] / absY;
        } else {
            face = 3;
            u = direction[0] / absY;
            v = -direction[2] / absY;
        }
    } else {
        if (direction[2] >= 0.0f) {
            face = 4;
            u = direction[0] / absZ;
            v = -direction[1] / absZ;
        } else {
            face = 5;
            u = -direction[0] / absZ;
            v = -direction[1] / absZ;
        }
    }

    const float size = static_cast<float>(faceSize);
    return CubemapSampleLocation{
        face,
        (u + 1.0f) * 0.5f * size - 0.5f,
        (v + 1.0f) * 0.5f * size - 0.5f
    };
}

std::array<float, 4> SampleCubemapBaseBilinear(
    const AuthoredCubemapPixelData& pixelData,
    u32 faceSize,
    const std::array<float, 3>& direction
) {
    const CubemapSampleLocation location =
        CubemapLocationForDirection(direction, faceSize);
    const int x0 = static_cast<int>(std::floor(location.x));
    const int y0 = static_cast<int>(std::floor(location.y));
    const float tx = location.x - static_cast<float>(x0);
    const float ty = location.y - static_cast<float>(y0);

    const auto load = [&](int x, int y) {
        const u32 clampedX = static_cast<u32>(std::clamp(
            x,
            0,
            static_cast<int>(faceSize) - 1
        ));
        const u32 clampedY = static_cast<u32>(std::clamp(
            y,
            0,
            static_cast<int>(faceSize) - 1
        ));
        return LoadCubemapBaseTexel(
            pixelData,
            faceSize,
            location.face,
            clampedX,
            clampedY
        );
    };

    const std::array<float, 4> c00 = load(x0, y0);
    const std::array<float, 4> c10 = load(x0 + 1, y0);
    const std::array<float, 4> c01 = load(x0, y0 + 1);
    const std::array<float, 4> c11 = load(x0 + 1, y0 + 1);
    const auto lerp = [](float a, float b, float factor) {
        return a + (b - a) * factor;
    };

    std::array<float, 4> result{};
    for (std::size_t channel = 0; channel < result.size(); ++channel) {
        result[channel] = lerp(
            lerp(c00[channel], c10[channel], tx),
            lerp(c01[channel], c11[channel], tx),
            ty
        );
    }
    return result;
}

void StoreCubemapMipTexel(
    AuthoredCubemapMipChain& mipChain,
    std::size_t offset,
    const std::array<float, 4>& color
) {
    if (mipChain.hdr) {
        StoreHalfFloat(mipChain.pixels, offset + 0u, color[0]);
        StoreHalfFloat(mipChain.pixels, offset + 2u, color[1]);
        StoreHalfFloat(mipChain.pixels, offset + 4u, color[2]);
        StoreHalfFloat(mipChain.pixels, offset + 6u, color[3]);
        return;
    }

    mipChain.pixels[offset + 0u] = EncodeReflectionChannel(color[0], false);
    mipChain.pixels[offset + 1u] = EncodeReflectionChannel(color[1], false);
    mipChain.pixels[offset + 2u] = EncodeReflectionChannel(color[2], false);
    mipChain.pixels[offset + 3u] = EncodeReflectionChannel(color[3], false);
}

u32 PrefilterSampleCountForMip(u32 mipLevel, u32 mipLevels) {
    if (mipLevel == 0u || mipLevels <= 1u) {
        return 1u;
    }
    return std::min(128u, 32u + mipLevel * 32u);
}

std::array<float, 4> PrefilterCubemapBaseTexelsForMip(
    const AuthoredCubemapPixelData& pixelData,
    u32 faceSize,
    std::size_t face,
    u32 mipLevel,
    u32 mipLevels,
    u32 mipX,
    u32 mipY,
    u32 sampleCount
) {
    const u32 mipExtent = MipExtent(faceSize, mipLevel);
    const float faceU =
        (2.0f * (static_cast<float>(mipX) + 0.5f) /
         static_cast<float>(mipExtent)) -
        1.0f;
    const float faceV =
        (2.0f * (static_cast<float>(mipY) + 0.5f) /
         static_cast<float>(mipExtent)) -
        1.0f;
    const std::array<float, 3> normal =
        CubemapDirection(face, faceU, faceV);
    const float roughness = mipLevels > 1u
        ? std::clamp(
            static_cast<float>(mipLevel) / static_cast<float>(mipLevels - 1u),
            0.0f,
            1.0f
        )
        : 0.0f;

    if (roughness <= 0.0001f || sampleCount <= 1u) {
        return SampleCubemapBaseBilinear(pixelData, faceSize, normal);
    }

    const std::array<float, 3> view = normal;
    std::array<float, 4> sum{ 0.0f, 0.0f, 0.0f, 0.0f };
    float totalWeight = 0.0f;
    for (u32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const std::array<float, 2> xi = Hammersley(sampleIndex, sampleCount);
        const std::array<float, 3> halfVector =
            ImportanceSampleGgx(xi, roughness, normal);
        const std::array<float, 3> light =
            Normalize3(Reflect3(Scale3(view, -1.0f), halfVector));
        const float nDotL = std::max(0.0f, Dot3(normal, light));
        if (nDotL <= 0.0f) {
            continue;
        }

        const std::array<float, 4> sample =
            SampleCubemapBaseBilinear(pixelData, faceSize, light);
        for (std::size_t channel = 0; channel < sum.size(); ++channel) {
            sum[channel] += sample[channel] * nDotL;
        }
        totalWeight += nDotL;
    }

    const float invSampleCount = totalWeight > 0.000001f
        ? 1.0f / totalWeight
        : 0.0f;
    for (float& channel : sum) {
        channel *= invSampleCount;
    }
    return sum;
}

AuthoredCubemapMipChain BuildPrefilteredCubemapMipChain(
    const AuthoredCubemapPixelData& pixelData,
    u32 faceSize
) {
    AuthoredCubemapMipChain mipChain{};
    mipChain.format = pixelData.format;
    mipChain.hdr = pixelData.hdr;
    mipChain.faceSize = faceSize;
    mipChain.mipLevels = CalculateMipLevels(
        static_cast<int>(faceSize),
        static_cast<int>(faceSize)
    );
    mipChain.bytesPerTexel = pixelData.bytesPerTexel;
    mipChain.prefiltered = mipChain.mipLevels > 1u;
    mipChain.prefilterSampleCount =
        PrefilterSampleCountForMip(mipChain.mipLevels - 1u, mipChain.mipLevels);
    mipChain.copyRegions.reserve(6u * mipChain.mipLevels);

    VkDeviceSize bufferOffset = 0;
    for (std::size_t face = 0; face < 6u; ++face) {
        for (u32 mipLevel = 0; mipLevel < mipChain.mipLevels; ++mipLevel) {
            const u32 extent = MipExtent(faceSize, mipLevel);
            const VkDeviceSize mipByteSize =
                CubemapMipFaceByteSize(faceSize, mipLevel, pixelData.bytesPerTexel);

            VkBufferImageCopy region{};
            region.bufferOffset = bufferOffset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mipLevel;
            region.imageSubresource.baseArrayLayer = static_cast<u32>(face);
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { extent, extent, 1 };
            mipChain.copyRegions.push_back(region);

            const std::size_t outputOffset =
                static_cast<std::size_t>(bufferOffset);
            mipChain.pixels.resize(outputOffset + static_cast<std::size_t>(mipByteSize));
            if (mipLevel == 0u) {
                const std::size_t sourceFaceOffset =
                    face *
                    static_cast<std::size_t>(faceSize) *
                    static_cast<std::size_t>(faceSize) *
                    pixelData.bytesPerTexel;
                std::copy_n(
                    pixelData.pixels.data() + sourceFaceOffset,
                    static_cast<std::size_t>(mipByteSize),
                    mipChain.pixels.data() + outputOffset
                );
            } else {
                const u32 sampleCount = PrefilterSampleCountForMip(
                    mipLevel,
                    mipChain.mipLevels
                );
                for (u32 y = 0; y < extent; ++y) {
                    for (u32 x = 0; x < extent; ++x) {
                        const std::array<float, 4> color =
                            PrefilterCubemapBaseTexelsForMip(
                                pixelData,
                                faceSize,
                                face,
                                mipLevel,
                                mipChain.mipLevels,
                                x,
                                y,
                                sampleCount
                            );
                        const std::size_t texelOffset =
                            outputOffset +
                            ((static_cast<std::size_t>(y) *
                              static_cast<std::size_t>(extent)) +
                             static_cast<std::size_t>(x)) *
                                pixelData.bytesPerTexel;
                        StoreCubemapMipTexel(mipChain, texelOffset, color);
                    }
                }
            }

            bufferOffset += mipByteSize;
        }
    }

    return mipChain;
}

std::unique_ptr<VulkanImage> UploadAuthoredCubemapMipChain(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const AuthoredCubemapMipChain& mipChain
) {
    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        static_cast<VkDeviceSize>(mipChain.pixels.size()),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer->Upload(std::as_bytes(std::span<const u8>(
        mipChain.pixels.data(),
        mipChain.pixels.size()
    )));

    const VkExtent2D extent{ mipChain.faceSize, mipChain.faceSize };
    auto image = std::make_unique<VulkanImage>(
        device,
        physicalDevice,
        extent,
        mipChain.format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        mipChain.mipLevels,
        6u,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE
    );
    image->TransitionLayout(
        device,
        commandPool,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        mipChain.mipLevels
    );

    VkCommandBuffer commandBuffer =
        BeginReflectionProbeUploadCommands(device, commandPool);
    vkCmdCopyBufferToImage(
        commandBuffer,
        stagingBuffer->Handle(),
        image->Handle(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<u32>(mipChain.copyRegions.size()),
        mipChain.copyRegions.data()
    );

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image->Handle();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipChain.mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6u;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
    EndReflectionProbeUploadCommands(device, commandPool, commandBuffer);
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
        AuthoredCubemapPixelData pixelData =
            ConvertEquirectangularToCubemapPixels(equirectangular, faceSize);
        AuthoredCubemapMipChain mipChain =
            BuildPrefilteredCubemapMipChain(pixelData, faceSize);
        return AuthoredCubemapLoadResult{
            UploadAuthoredCubemapMipChain(
                device,
                physicalDevice,
                commandPool,
                mipChain
            ),
            source.type,
            pixelData.hdr,
            mipChain.prefiltered,
            mipChain.mipLevels,
            mipChain.prefilterSampleCount
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

    AuthoredCubemapPixelData pixelData = PackSixFaceCubemapPixels(faces);
    AuthoredCubemapMipChain mipChain =
        BuildPrefilteredCubemapMipChain(pixelData, static_cast<u32>(faces[0].width));

    return AuthoredCubemapLoadResult{
            UploadAuthoredCubemapMipChain(
                device,
                physicalDevice,
                commandPool,
                mipChain
            ),
        source.type,
        pixelData.hdr,
        mipChain.prefiltered,
        mipChain.mipLevels,
        mipChain.prefilterSampleCount
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
    ++m_AuthoredCubemapRefreshCheckCount;
    const auto releaseImageAfterIdle = [&]() {
        if (resource.image == nullptr) {
            return;
        }
        vkDeviceWaitIdle(device.Handle());
        resource.image.reset();
    };

    const std::filesystem::path assetPath = ResolveAssetPath(assetId);
    resource.assetFound = RegularFileExists(assetPath) || DirectoryExists(assetPath);
    if (!resource.assetFound) {
        if (resource.image != nullptr || resource.loadFailed) {
            ++m_AuthoredCubemapReloadCount;
        }
        releaseImageAfterIdle();
        resource.sourceType = AuthoredReflectionCubemapSourceType::Unknown;
        resource.assetSignature = 0;
        resource.loadFailed = false;
        resource.hdr = false;
        resource.prefiltered = false;
        resource.generatedMipCount = 0;
        resource.prefilterSampleCount = 0;
        return;
    }

    u64 assetSignature = 0;
    try {
        const ResolvedAuthoredCubemapSource source =
            ResolveAuthoredCubemapSource(assetId);
        assetSignature = AuthoredCubemapSourceSignature(source, assetPath);
    } catch (...) {
        assetSignature = FileSystemEntrySignature(assetPath);
        if (resource.loadFailed && resource.assetSignature == assetSignature) {
            ++m_AuthoredCubemapCacheHitCount;
            return;
        }
        if (resource.image != nullptr || resource.loadFailed) {
            ++m_AuthoredCubemapReloadCount;
        }
        releaseImageAfterIdle();
        resource.sourceType = AuthoredReflectionCubemapSourceType::Unknown;
        resource.assetSignature = assetSignature;
        resource.loadFailed = true;
        resource.hdr = false;
        resource.prefiltered = false;
        resource.generatedMipCount = 0;
        resource.prefilterSampleCount = 0;
        return;
    }

    if ((resource.image != nullptr || resource.loadFailed) &&
        resource.assetSignature == assetSignature) {
        ++m_AuthoredCubemapCacheHitCount;
        return;
    }

    if (resource.image != nullptr || resource.loadFailed) {
        ++m_AuthoredCubemapReloadCount;
    }
    releaseImageAfterIdle();
    resource.sourceType = AuthoredReflectionCubemapSourceType::Unknown;
    resource.loadFailed = false;
    resource.hdr = false;
    resource.prefiltered = false;
    resource.generatedMipCount = 0;
    resource.prefilterSampleCount = 0;

    try {
        AuthoredCubemapLoadResult loadResult = CreateAuthoredCubemapImage(
            device,
            physicalDevice,
            commandPool,
            assetId
        );
        resource.image = std::move(loadResult.image);
        resource.sourceType = loadResult.sourceType;
        resource.assetSignature = assetSignature;
        resource.hdr = loadResult.hdr;
        resource.prefiltered = loadResult.prefilteredMipChain;
        resource.generatedMipCount = loadResult.generatedMipCount;
        resource.prefilterSampleCount = loadResult.prefilterSampleCount;
        ++m_AuthoredCubemapUploadCount;
        if (resource.prefiltered) {
            ++m_AuthoredCubemapPrefilteredUploadCount;
        }
        if (resource.sourceType ==
            AuthoredReflectionCubemapSourceType::Equirectangular) {
            ++m_AuthoredCubemapEquirectangularConversionCount;
        }
    } catch (...) {
        releaseImageAfterIdle();
        resource.sourceType = AuthoredReflectionCubemapSourceType::Unknown;
        resource.assetSignature = assetSignature;
        resource.loadFailed = true;
        resource.hdr = false;
        resource.prefiltered = false;
        resource.generatedMipCount = 0;
        resource.prefilterSampleCount = 0;
    }
}

void VulkanReflectionProbeResources::Release() {
    m_BuiltInCubemapImage.reset();
    m_BuiltInCubemapView = VK_NULL_HANDLE;
    m_AuthoredCubemaps.clear();
    m_AuthoredCubemapUploadCount = 0;
    m_AuthoredCubemapEquirectangularConversionCount = 0;
    m_AuthoredCubemapPrefilteredUploadCount = 0;
    m_AuthoredCubemapCacheHitCount = 0;
    m_AuthoredCubemapReloadCount = 0;
    m_AuthoredCubemapRefreshCheckCount = 0;
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

u32 VulkanReflectionProbeResources::AuthoredCubemapHdrLoadedCount() const {
    u32 count = 0;
    for (const auto& [assetId, resource] : m_AuthoredCubemaps) {
        (void)assetId;
        if (resource.image != nullptr && resource.hdr) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapPrefilteredLoadedCount() const {
    u32 count = 0;
    for (const auto& [assetId, resource] : m_AuthoredCubemaps) {
        (void)assetId;
        if (resource.image != nullptr && resource.prefiltered) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapPrefilteredUploadCount() const {
    return m_AuthoredCubemapPrefilteredUploadCount;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapCacheHitCount() const {
    return m_AuthoredCubemapCacheHitCount;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapReloadCount() const {
    return m_AuthoredCubemapReloadCount;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapRefreshCheckCount() const {
    return m_AuthoredCubemapRefreshCheckCount;
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

bool VulkanReflectionProbeResources::AuthoredCubemapHdr(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() &&
        found->second.image != nullptr &&
        found->second.hdr;
}

bool VulkanReflectionProbeResources::AuthoredCubemapPrefiltered(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() &&
        found->second.image != nullptr &&
        found->second.prefiltered;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapGeneratedMipCount(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() && found->second.image != nullptr
        ? found->second.generatedMipCount
        : 0u;
}

u32 VulkanReflectionProbeResources::AuthoredCubemapPrefilterSampleCount(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() && found->second.image != nullptr
        ? found->second.prefilterSampleCount
        : 0u;
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
