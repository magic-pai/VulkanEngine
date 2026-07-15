#include "renderer/vulkan/ibl_generator.h"
#include "renderer/vulkan/image.h"
#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace se {

namespace {

static u16 f2h(float v) {
    uint32_t x; memcpy(&x, &v, 4);
    u32 sign = (x >> 16) & 0x8000;
    int exp = int((x >> 23) & 0xFF) - 127;
    u32 mant = (x >> 12) & 0x7FF;
    if (exp > 15) return u16(sign | 0x7C00);
    if (exp >= -14) return u16(sign | (u32(exp + 15) << 10) | mant);
    if (exp >= -24) { mant |= 0x800; mant >>= (-14 - exp); return u16(sign | mant); }
    return u16(sign);
}

float RadicalInverseVdc(u32 bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

glm::vec2 Hammersley(u32 index, u32 count) {
    return {
        static_cast<float>(index) / static_cast<float>(count),
        RadicalInverseVdc(index)
    };
}

glm::vec3 ImportanceSampleGgx(glm::vec2 xi, float roughness) {
    const float a = roughness * roughness;
    const float phi = 2.0f * glm::pi<float>() * xi.x;
    const float cosTheta = std::sqrt(
        (1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y)
    );
    const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
    return glm::vec3(
        std::cos(phi) * sinTheta,
        std::sin(phi) * sinTheta,
        cosTheta
    );
}

glm::vec3 CubemapDirection(u32 face, float u, float v) {
    switch (face) {
    case 0:
        return glm::normalize(glm::vec3(1.0f, -v, -u));
    case 1:
        return glm::normalize(glm::vec3(-1.0f, -v, u));
    case 2:
        return glm::normalize(glm::vec3(u, 1.0f, v));
    case 3:
        return glm::normalize(glm::vec3(u, -1.0f, -v));
    case 4:
        return glm::normalize(glm::vec3(u, -v, 1.0f));
    default:
        return glm::normalize(glm::vec3(-u, -v, -1.0f));
    }
}

glm::vec3 ReflectionProbeTexel(u32 face, u32 x, u32 y, u32 faceSize) {
    const float u = (float(x) + 0.5f) / float(faceSize) * 2.0f - 1.0f;
    const float v = (float(y) + 0.5f) / float(faceSize) * 2.0f - 1.0f;
    const glm::vec3 direction = CubemapDirection(face, u, v);
    const std::array<glm::vec3, 6> faceTints{
        glm::vec3(1.00f, 0.32f, 0.18f),
        glm::vec3(0.18f, 0.56f, 1.00f),
        glm::vec3(0.72f, 1.00f, 0.28f),
        glm::vec3(0.22f, 0.18f, 0.14f),
        glm::vec3(1.00f, 0.76f, 0.22f),
        glm::vec3(0.64f, 0.38f, 1.00f)
    };
    const float horizon = std::clamp(direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
    const float center =
        1.0f - std::clamp(std::sqrt(u * u + v * v) * 0.72f, 0.0f, 1.0f);
    const float band =
        std::sin((direction.x * 5.0f + direction.z * 3.0f + float(face)) *
            glm::pi<float>()) * 0.5f + 0.5f;
    glm::vec3 color =
        faceTints[face] * (0.28f + 0.42f * center) +
        glm::vec3(0.06f, 0.09f, 0.14f) * (1.0f - horizon) +
        glm::vec3(0.18f, 0.24f, 0.34f) * horizon +
        glm::vec3(0.16f, 0.10f, 0.04f) * band;
    return glm::clamp(color, glm::vec3(0.0f), glm::vec3(4.0f));
}

struct IblTexturePayload {
    std::vector<u8> prefilteredBasePixels;
    std::vector<u8> irradiancePixels;
    std::vector<u8> brdfPixels;
};

struct IblCacheHeader {
    char magic[8]{ 'S', 'E', 'I', 'B', 'L', 'C', '0', '2' };
    u32 version = 2;
    u32 quality = 0;
    u32 actualSource = 0;
    u32 brdfLutSize = 0;
    u32 irradianceFaceSize = 0;
    u32 prefilteredFaceSize = 0;
    u32 prefilteredMipCount = 0;
    u32 brdfFormat = 0;
    u32 environmentFormat = 0;
    u64 prefilteredBaseBytes = 0;
    u64 irradianceBytes = 0;
    u64 brdfBytes = 0;
    u64 sourceSignature = 0;
};

struct LoadedIblEquirectangularImage {
    std::vector<float> pixels;
    int width = 0;
    int height = 0;
    bool hdr = false;
};

struct IblSample {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

std::string ReadEnvironmentVariable(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
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

std::filesystem::path IblCacheDirectory() {
    std::string path = ReadEnvironmentVariable("SE_GLOBAL_IBL_CACHE_DIR");
    if (path.empty()) {
        path = ReadEnvironmentVariable("SE_IBL_CACHE_DIR");
    }
    if (!path.empty()) {
        return std::filesystem::path(path);
    }

    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    if (!error) {
        return cwd / ".selfengine" / "ibl_cache";
    }
    return std::filesystem::path(".selfengine") / "ibl_cache";
}

std::filesystem::path ResolveSourcePath(std::string_view sourcePath) {
    std::filesystem::path path{ std::string(sourcePath) };
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

u64 HashCombine64(u64 seed, u64 value) {
    return seed ^
        (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
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

std::string IblCacheFileName(const VulkanIblGenerationInfo& info) {
    return
        "global_ibl_v2_q" + std::to_string(static_cast<u32>(info.quality)) +
        "_src" + std::to_string(static_cast<u32>(info.actualSource)) +
        "_sig" + std::to_string(info.sourceSignature) +
        "_b" + std::to_string(info.brdfLutSize) +
        "_i" + std::to_string(info.irradianceFaceSize) +
        "_p" + std::to_string(info.prefilteredFaceSize) +
        "_m" + std::to_string(info.prefilteredMipCount) +
        ".seibl";
}

std::filesystem::path IblCachePath(const VulkanIblGenerationInfo& info) {
    return IblCacheDirectory() / IblCacheFileName(info);
}

u64 ExpectedPrefilteredBaseBytes(const VulkanIblGenerationInfo& info) {
    return static_cast<u64>(info.prefilteredFaceSize) *
        static_cast<u64>(info.prefilteredFaceSize) *
        6ull *
        8ull;
}

u64 ExpectedIrradianceBytes(const VulkanIblGenerationInfo& info) {
    return static_cast<u64>(info.irradianceFaceSize) *
        static_cast<u64>(info.irradianceFaceSize) *
        6ull *
        8ull;
}

u64 ExpectedBrdfBytes(const VulkanIblGenerationInfo& info) {
    return static_cast<u64>(info.brdfLutSize) *
        static_cast<u64>(info.brdfLutSize) *
        4ull;
}

IblCacheHeader MakeIblCacheHeader(
    const VulkanIblGenerationInfo& info,
    const IblTexturePayload& payload
) {
    IblCacheHeader header{};
    header.quality = static_cast<u32>(info.quality);
    header.actualSource = static_cast<u32>(info.actualSource);
    header.brdfLutSize = info.brdfLutSize;
    header.irradianceFaceSize = info.irradianceFaceSize;
    header.prefilteredFaceSize = info.prefilteredFaceSize;
    header.prefilteredMipCount = info.prefilteredMipCount;
    header.brdfFormat = static_cast<u32>(kIblBrdfLutFormat);
    header.environmentFormat = static_cast<u32>(kIblEnvironmentFormat);
    header.prefilteredBaseBytes =
        static_cast<u64>(payload.prefilteredBasePixels.size());
    header.irradianceBytes = static_cast<u64>(payload.irradiancePixels.size());
    header.brdfBytes = static_cast<u64>(payload.brdfPixels.size());
    header.sourceSignature = info.sourceSignature;
    return header;
}

bool IblCacheHeaderMatches(
    const IblCacheHeader& header,
    const VulkanIblGenerationInfo& info
) {
    IblCacheHeader expected{};
    expected.quality = static_cast<u32>(info.quality);
    expected.actualSource = static_cast<u32>(info.actualSource);
    expected.brdfLutSize = info.brdfLutSize;
    expected.irradianceFaceSize = info.irradianceFaceSize;
    expected.prefilteredFaceSize = info.prefilteredFaceSize;
    expected.prefilteredMipCount = info.prefilteredMipCount;
    expected.brdfFormat = static_cast<u32>(kIblBrdfLutFormat);
    expected.environmentFormat = static_cast<u32>(kIblEnvironmentFormat);
    expected.prefilteredBaseBytes = ExpectedPrefilteredBaseBytes(info);
    expected.irradianceBytes = ExpectedIrradianceBytes(info);
    expected.brdfBytes = ExpectedBrdfBytes(info);
    expected.sourceSignature = info.sourceSignature;
    return std::memcmp(header.magic, expected.magic, sizeof(header.magic)) == 0 &&
        header.version == expected.version &&
        header.quality == expected.quality &&
        header.actualSource == expected.actualSource &&
        header.brdfLutSize == expected.brdfLutSize &&
        header.irradianceFaceSize == expected.irradianceFaceSize &&
        header.prefilteredFaceSize == expected.prefilteredFaceSize &&
        header.prefilteredMipCount == expected.prefilteredMipCount &&
        header.brdfFormat == expected.brdfFormat &&
        header.environmentFormat == expected.environmentFormat &&
        header.prefilteredBaseBytes == expected.prefilteredBaseBytes &&
        header.irradianceBytes == expected.irradianceBytes &&
        header.brdfBytes == expected.brdfBytes &&
        header.sourceSignature == expected.sourceSignature;
}

bool ReadExact(std::ifstream& file, std::vector<u8>& bytes) {
    if (bytes.empty()) {
        return true;
    }
    file.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    return file.good();
}

std::optional<IblTexturePayload> TryReadIblCache(
    const VulkanIblGenerationInfo& info
) {
    const std::filesystem::path path = IblCachePath(info);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    IblCacheHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good() || !IblCacheHeaderMatches(header, info)) {
        return std::nullopt;
    }

    IblTexturePayload payload{};
    payload.prefilteredBasePixels.resize(
        static_cast<std::size_t>(header.prefilteredBaseBytes)
    );
    payload.irradiancePixels.resize(
        static_cast<std::size_t>(header.irradianceBytes)
    );
    payload.brdfPixels.resize(static_cast<std::size_t>(header.brdfBytes));
    if (!ReadExact(file, payload.prefilteredBasePixels) ||
        !ReadExact(file, payload.irradiancePixels) ||
        !ReadExact(file, payload.brdfPixels)) {
        return std::nullopt;
    }

    return payload;
}

void TryWriteIblCache(
    const VulkanIblGenerationInfo& info,
    const IblTexturePayload& payload
) {
    std::error_code error;
    const std::filesystem::path path = IblCachePath(info);
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return;
    }

    const std::filesystem::path tempPath = path.string() + ".tmp";
    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        return;
    }

    const IblCacheHeader header = MakeIblCacheHeader(info, payload);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(
        reinterpret_cast<const char*>(payload.prefilteredBasePixels.data()),
        static_cast<std::streamsize>(payload.prefilteredBasePixels.size())
    );
    file.write(
        reinterpret_cast<const char*>(payload.irradiancePixels.data()),
        static_cast<std::streamsize>(payload.irradiancePixels.size())
    );
    file.write(
        reinterpret_cast<const char*>(payload.brdfPixels.data()),
        static_cast<std::streamsize>(payload.brdfPixels.size())
    );
    file.close();
    if (!file.good()) {
        std::filesystem::remove(tempPath, error);
        return;
    }

    std::filesystem::rename(tempPath, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(tempPath, path, error);
        if (error) {
            std::filesystem::remove(tempPath, error);
        }
    }
}

VulkanIblGenerationInfo ResolveIblGenerationInfo(
    const VulkanIblGenerationSettings& settings
) {
    VulkanIblGenerationInfo info{};
    info.quality = settings.quality;
    info.requestedSource = settings.source;
    info.actualSource = VulkanIblSource::Procedural;
    info.cachePolicy = settings.cachePolicy;
    info.runtimeGenerated = 1u;
    info.cacheHit = 0u;
    info.sourceFallbackReason = VulkanIblSourceFallbackReason::None;
    info.cacheFallbackReason = VulkanIblCacheFallbackReason::None;
    info.sourceAssetSpecified = settings.sourceAssetPath.empty() ? 0u : 1u;

    switch (settings.quality) {
    case VulkanIblQuality::Low:
        info.brdfLutSize = 128u;
        info.irradianceFaceSize = 16u;
        info.prefilteredFaceSize = 128u;
        info.prefilteredMipCount = 5u;
        break;
    case VulkanIblQuality::Ultra:
        info.brdfLutSize = 128u;
        info.irradianceFaceSize = 64u;
        info.prefilteredFaceSize = 512u;
        info.prefilteredMipCount = 5u;
        break;
    case VulkanIblQuality::Medium:
    case VulkanIblQuality::High:
    default:
        info.brdfLutSize = kIblBrdfLutSize;
        info.irradianceFaceSize = kIblIrradianceFaceSize;
        info.prefilteredFaceSize = kIblPrefilteredFaceSize;
        info.prefilteredMipCount = kIblPrefilteredMipCount;
        break;
    }

    if (settings.source == VulkanIblSource::AuthoredEquirectangular ||
        settings.source == VulkanIblSource::VisibleSkybox) {
        if (settings.sourceAssetPath.empty()) {
            info.sourceFallbackReason =
                VulkanIblSourceFallbackReason::SourceAssetMissing;
            return info;
        }

        const std::filesystem::path sourcePath =
            ResolveSourcePath(settings.sourceAssetPath);
        if (!RegularFileExists(sourcePath)) {
            info.sourceFallbackReason =
                VulkanIblSourceFallbackReason::SourceAssetMissing;
            return info;
        }

        info.actualSource = settings.source;
        info.sourceAssetFound = 1u;
        info.sourceSignature = FileSignature(sourcePath);
        return info;
    }

    if (settings.source != VulkanIblSource::Procedural) {
        info.sourceFallbackReason =
            VulkanIblSourceFallbackReason::RuntimeSourceUnsupported;
    }

    return info;
}

glm::vec2 IntegrateBrdf(float nDotV, float roughness);

glm::vec2 EquirectUv(glm::vec3 direction) {
    if (glm::dot(direction, direction) <= 0.000001f) {
        direction = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        direction = glm::normalize(direction);
    }
    const float u =
        std::atan2(direction.z, direction.x) /
        (2.0f * glm::pi<float>()) +
        0.5f;
    const float v =
        std::acos(std::clamp(direction.y, -1.0f, 1.0f)) /
        glm::pi<float>();
    return glm::vec2(u, v);
}

LoadedIblEquirectangularImage LoadIblEquirectangularImage(
    const std::filesystem::path& path
) {
    LoadedIblEquirectangularImage image{};
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
            throw std::runtime_error("Failed to load authored global IBL HDR image");
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
            throw std::runtime_error("Failed to load authored global IBL image");
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
        throw std::runtime_error("Authored global IBL image must use 2:1 equirectangular aspect");
    }
    return image;
}

IblSample FetchIblEquirectangularPixel(
    const LoadedIblEquirectangularImage& image,
    int x,
    int y
) {
    x %= image.width;
    if (x < 0) {
        x += image.width;
    }
    y = std::clamp(y, 0, image.height - 1);
    const std::size_t offset =
        (static_cast<std::size_t>(y) *
            static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(x)) *
        4u;
    return IblSample{
        image.pixels[offset + 0u],
        image.pixels[offset + 1u],
        image.pixels[offset + 2u],
        image.pixels[offset + 3u]
    };
}

IblSample SampleIblEquirectangularImage(
    const LoadedIblEquirectangularImage& image,
    glm::vec2 uv
) {
    uv.x = uv.x - std::floor(uv.x);
    uv.y = std::clamp(uv.y, 0.0f, 1.0f);

    const float x = uv.x * static_cast<float>(image.width) - 0.5f;
    const float y = uv.y * static_cast<float>(image.height - 1);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const IblSample c00 = FetchIblEquirectangularPixel(image, x0, y0);
    const IblSample c10 = FetchIblEquirectangularPixel(image, x0 + 1, y0);
    const IblSample c01 = FetchIblEquirectangularPixel(image, x0, y0 + 1);
    const IblSample c11 = FetchIblEquirectangularPixel(image, x0 + 1, y0 + 1);
    const auto lerp = [](float a, float b, float factor) {
        return a + (b - a) * factor;
    };
    const auto bilinear = [&](float IblSample::*channel) {
        return lerp(
            lerp(c00.*channel, c10.*channel, tx),
            lerp(c01.*channel, c11.*channel, tx),
            ty
        );
    };
    return IblSample{
        bilinear(&IblSample::r),
        bilinear(&IblSample::g),
        bilinear(&IblSample::b),
        bilinear(&IblSample::a)
    };
}

IblSample SampleIblEquirectangularDirection(
    const LoadedIblEquirectangularImage& image,
    glm::vec3 direction
) {
    return SampleIblEquirectangularImage(image, EquirectUv(direction));
}

void StoreIblHalfTexel(
    std::vector<u8>& pixels,
    std::size_t offset,
    glm::vec3 color,
    float alpha = 1.0f
) {
    const u16 r = f2h(glm::clamp(color.r, 0.0f, 65504.0f));
    const u16 g = f2h(glm::clamp(color.g, 0.0f, 65504.0f));
    const u16 b = f2h(glm::clamp(color.b, 0.0f, 65504.0f));
    const u16 a = f2h(glm::clamp(alpha, 0.0f, 1.0f));
    memcpy(&pixels[offset + 0u], &r, 2);
    memcpy(&pixels[offset + 2u], &g, 2);
    memcpy(&pixels[offset + 4u], &b, 2);
    memcpy(&pixels[offset + 6u], &a, 2);
}

std::vector<u8> BuildBrdfLutPixels(u32 lutSize) {
    std::vector<u8> brdfPixels(lutSize * lutSize * 4);
    for (u32 y = 0; y < lutSize; ++y) {
        for (u32 x = 0; x < lutSize; ++x) {
            float roughness = (float(y) + 0.5f) / float(lutSize);
            float nDotV = (float(x) + 0.5f) / float(lutSize);
            glm::vec2 integrated = IntegrateBrdf(nDotV, roughness);
            u32 off = (y * lutSize + x) * 4;
            u16 sh=f2h(glm::clamp(integrated.x, 0.0f, 4.0f));
            u16 bh=f2h(glm::clamp(integrated.y, 0.0f, 4.0f));
            memcpy(&brdfPixels[off], &sh, 2);
            memcpy(&brdfPixels[off+2], &bh, 2);
        }
    }
    return brdfPixels;
}

IblTexturePayload BuildProceduralIblPayload(
    const VulkanIblGenerationInfo& info
) {
    IblTexturePayload payload{};
    const u32 srcFace = info.prefilteredFaceSize;
    const u32 irrFace = info.irradianceFaceSize;
    const u32 faceCount = 6;
    const u32 pxBytes = 8;

    payload.prefilteredBasePixels.resize(srcFace * srcFace * faceCount * pxBytes);
    for (u32 f = 0; f < faceCount; ++f) {
        for (u32 y = 0; y < srcFace; ++y) {
            for (u32 x = 0; x < srcFace; ++x) {
                float ny = (float(y) + 0.5f) / float(srcFace) * 2.0f - 1.0f;
                float skyLerp = (ny + 0.15f) / 1.15f;
                float r = std::lerp(0.05f, 0.15f, skyLerp);
                float g = std::lerp(0.06f, 0.20f, skyLerp);
                float b = std::lerp(0.10f, 0.40f, skyLerp);
                u32 off = ((f * srcFace + y) * srcFace + x) * pxBytes;
                u16 rh=f2h(r), gh=f2h(g), bh=f2h(b), ah=u16(0x3C00);
                memcpy(&payload.prefilteredBasePixels[off], &rh, 2);
                memcpy(&payload.prefilteredBasePixels[off+2], &gh, 2);
                memcpy(&payload.prefilteredBasePixels[off+4], &bh, 2);
                memcpy(&payload.prefilteredBasePixels[off+6], &ah, 2);
            }
        }
    }

    const u32 irrBytes = irrFace * irrFace * faceCount * pxBytes;
    payload.irradiancePixels.resize(irrBytes);
    for (u32 f = 0; f < faceCount; ++f) {
        for (u32 y = 0; y < irrFace; ++y) {
            for (u32 x = 0; x < irrFace; ++x) {
                float u = (float(x) + 0.5f) / float(irrFace);
                float v = (float(y) + 0.5f) / float(irrFace);
                glm::vec3 N;
                switch(f) {
                case 0: N=glm::normalize(glm::vec3(1,-v*2+1,-u*2+1)); break;
                case 1: N=glm::normalize(glm::vec3(-1,-v*2+1,u*2-1)); break;
                case 2: N=glm::normalize(glm::vec3(u*2-1,1,v*2-1)); break;
                case 3: N=glm::normalize(glm::vec3(u*2-1,-1,-(v*2-1))); break;
                case 4: N=glm::normalize(glm::vec3(u*2-1,-(v*2-1),1)); break;
                default:N=glm::normalize(glm::vec3(-(u*2-1),-(v*2-1),-1)); break;
                }
                glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0,0,1) : glm::vec3(1,0,0);
                glm::vec3 T = glm::normalize(glm::cross(up, N));
                glm::vec3 B = glm::cross(N, T);
                glm::vec3 irr(0); float cnt = 0;
                for (float phi = 0; phi < 6.2832f; phi += 0.14f) {
                    for (float theta = 0; theta < 1.5708f; theta += 0.14f) {
                        glm::vec3 ts(std::sin(theta)*std::cos(phi), std::sin(theta)*std::sin(phi), std::cos(theta));
                        glm::vec3 sd = T*ts.x + B*ts.y + N*ts.z;
                        float sky = 0.04f + 0.10f * std::max(sd.y, 0.0f);
                        irr += glm::vec3(sky) * std::cos(theta) * std::sin(theta);
                        cnt += 1;
                    }
                }
                irr = 3.141593f * irr / std::max(cnt, 1.0f);
                u32 off = ((f * irrFace + y) * irrFace + x) * pxBytes;
                u16 rh=f2h(glm::clamp(irr.r,0.0f,65504.0f));
                u16 gh=f2h(glm::clamp(irr.g,0.0f,65504.0f));
                u16 bh=f2h(glm::clamp(irr.b,0.0f,65504.0f));
                u16 ah=u16(0x3C00);
                memcpy(&payload.irradiancePixels[off],&rh,2);
                memcpy(&payload.irradiancePixels[off+2],&gh,2);
                memcpy(&payload.irradiancePixels[off+4],&bh,2);
                memcpy(&payload.irradiancePixels[off+6],&ah,2);
            }
        }
    }

    const u32 lutSize = info.brdfLutSize;
    payload.brdfPixels = BuildBrdfLutPixels(lutSize);

    return payload;
}

IblTexturePayload BuildEquirectangularIblPayload(
    const VulkanIblGenerationInfo& info,
    const std::filesystem::path& sourcePath
) {
    const LoadedIblEquirectangularImage image =
        LoadIblEquirectangularImage(sourcePath);
    IblTexturePayload payload{};
    const u32 srcFace = info.prefilteredFaceSize;
    const u32 irrFace = info.irradianceFaceSize;
    const u32 faceCount = 6;
    const u32 pxBytes = 8;

    payload.prefilteredBasePixels.resize(srcFace * srcFace * faceCount * pxBytes);
    for (u32 f = 0; f < faceCount; ++f) {
        for (u32 y = 0; y < srcFace; ++y) {
            for (u32 x = 0; x < srcFace; ++x) {
                const float u =
                    (float(x) + 0.5f) / float(srcFace) * 2.0f - 1.0f;
                const float v =
                    (float(y) + 0.5f) / float(srcFace) * 2.0f - 1.0f;
                const glm::vec3 direction = CubemapDirection(f, u, v);
                const IblSample sample =
                    SampleIblEquirectangularDirection(image, direction);
                const std::size_t offset =
                    ((static_cast<std::size_t>(f) *
                        static_cast<std::size_t>(srcFace) +
                      static_cast<std::size_t>(y)) *
                        static_cast<std::size_t>(srcFace) +
                     static_cast<std::size_t>(x)) *
                    pxBytes;
                StoreIblHalfTexel(
                    payload.prefilteredBasePixels,
                    offset,
                    glm::vec3(sample.r, sample.g, sample.b),
                    sample.a
                );
            }
        }
    }

    payload.irradiancePixels.resize(irrFace * irrFace * faceCount * pxBytes);
    for (u32 f = 0; f < faceCount; ++f) {
        for (u32 y = 0; y < irrFace; ++y) {
            for (u32 x = 0; x < irrFace; ++x) {
                const float u =
                    (float(x) + 0.5f) / float(irrFace) * 2.0f - 1.0f;
                const float v =
                    (float(y) + 0.5f) / float(irrFace) * 2.0f - 1.0f;
                const glm::vec3 normal = CubemapDirection(f, u, v);
                const glm::vec3 up =
                    std::abs(normal.z) < 0.999f
                        ? glm::vec3(0.0f, 0.0f, 1.0f)
                        : glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
                const glm::vec3 bitangent = glm::cross(normal, tangent);
                glm::vec3 irradiance(0.0f);
                float totalWeight = 0.0f;
                for (float phi = 0.0f; phi < 6.2832f; phi += 0.18f) {
                    for (float theta = 0.0f; theta < 1.5708f; theta += 0.18f) {
                        const glm::vec3 tangentSample(
                            std::sin(theta) * std::cos(phi),
                            std::sin(theta) * std::sin(phi),
                            std::cos(theta)
                        );
                        const glm::vec3 sampleDirection =
                            tangent * tangentSample.x +
                            bitangent * tangentSample.y +
                            normal * tangentSample.z;
                        const IblSample sample =
                            SampleIblEquirectangularDirection(
                                image,
                                sampleDirection
                            );
                        const float weight = std::cos(theta) * std::sin(theta);
                        irradiance += glm::vec3(sample.r, sample.g, sample.b) * weight;
                        totalWeight += weight;
                    }
                }
                irradiance =
                    glm::pi<float>() *
                    irradiance /
                    std::max(totalWeight, 0.000001f);
                const std::size_t offset =
                    ((static_cast<std::size_t>(f) *
                        static_cast<std::size_t>(irrFace) +
                      static_cast<std::size_t>(y)) *
                        static_cast<std::size_t>(irrFace) +
                     static_cast<std::size_t>(x)) *
                    pxBytes;
                StoreIblHalfTexel(payload.irradiancePixels, offset, irradiance);
            }
        }
    }

    payload.brdfPixels = BuildBrdfLutPixels(info.brdfLutSize);
    return payload;
}

float GeometrySchlickGgx(float nDotV, float roughness) {
    const float a = roughness;
    const float k = (a * a) / 2.0f;
    return nDotV / (nDotV * (1.0f - k) + k);
}

float GeometrySmith(float nDotV, float nDotL, float roughness) {
    return GeometrySchlickGgx(nDotV, roughness) *
        GeometrySchlickGgx(nDotL, roughness);
}

glm::vec2 IntegrateBrdf(float nDotV, float roughness) {
    const glm::vec3 view(
        std::sqrt(std::max(1.0f - nDotV * nDotV, 0.0f)),
        0.0f,
        nDotV
    );
    constexpr u32 kSampleCount = 1024;
    float scale = 0.0f;
    float bias = 0.0f;

    for (u32 sampleIndex = 0; sampleIndex < kSampleCount; ++sampleIndex) {
        const glm::vec2 xi = Hammersley(sampleIndex, kSampleCount);
        const glm::vec3 halfVector = ImportanceSampleGgx(xi, roughness);
        const glm::vec3 light =
            glm::normalize(2.0f * glm::dot(view, halfVector) * halfVector - view);

        const float nDotL = std::max(light.z, 0.0f);
        const float nDotH = std::max(halfVector.z, 0.0f);
        const float vDotH = std::max(glm::dot(view, halfVector), 0.0f);
        if (nDotL <= 0.0f) {
            continue;
        }

        const float geometry = GeometrySmith(nDotV, nDotL, roughness);
        const float geometryVisible =
            (geometry * vDotH) / std::max(nDotH * nDotV, 0.0001f);
        const float fresnel = std::pow(1.0f - vDotH, 5.0f);
        scale += (1.0f - fresnel) * geometryVisible;
        bias += fresnel * geometryVisible;
    }

    return glm::vec2(scale, bias) / static_cast<float>(kSampleCount);
}

} // namespace

void GenerateReflectionProbeCubemap(const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice, const VulkanCommandPool& commandPool,
    std::unique_ptr<VulkanImage>& cubemapImage, VkImageView& cubemapView)
{
    constexpr u32 faceSize = kReflectionProbeCubemapFaceSize;
    constexpr u32 mipCount = kReflectionProbeCubemapMipCount;
    constexpr u32 faceCount = 6;
    constexpr u32 pxBytes = 8;
    const VkFormat fmt = kReflectionProbeCubemapFormat;

    std::vector<u8> pixels(faceSize * faceSize * faceCount * pxBytes);
    for (u32 face = 0; face < faceCount; ++face) {
        for (u32 y = 0; y < faceSize; ++y) {
            for (u32 x = 0; x < faceSize; ++x) {
                const glm::vec3 color = ReflectionProbeTexel(face, x, y, faceSize);
                const u32 offset = ((face * faceSize + y) * faceSize + x) * pxBytes;
                const u16 r = f2h(color.r);
                const u16 g = f2h(color.g);
                const u16 b = f2h(color.b);
                const u16 a = u16(0x3C00);
                memcpy(&pixels[offset], &r, 2);
                memcpy(&pixels[offset + 2], &g, 2);
                memcpy(&pixels[offset + 4], &b, 2);
                memcpy(&pixels[offset + 6], &a, 2);
            }
        }
    }

    VulkanBuffer staging(device, physicalDevice, pixels.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.Upload(std::as_bytes(std::span<const u8>(pixels)));

    cubemapImage = std::make_unique<VulkanImage>(device, physicalDevice,
        VkExtent2D{faceSize, faceSize}, fmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
        mipCount, faceCount, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE);
    cubemapImage->TransitionLayout(device, commandPool, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipCount);
    cubemapImage->CopyFromBuffer(device, commandPool, staging.Handle(), faceCount);
    cubemapImage->GenerateMipmaps(physicalDevice, device, commandPool, faceCount);
    cubemapView = cubemapImage->View();
}

void GenerateIblTextures(const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice, const VulkanCommandPool& commandPool,
    std::unique_ptr<VulkanImage>& brdfImage, std::unique_ptr<VulkanImage>& irradianceImage,
    std::unique_ptr<VulkanImage>& prefilteredImage,
    VkImageView& irradianceView, VkImageView& prefilteredView, VkSampler& sampler,
    const VulkanIblGenerationSettings& settings,
    VulkanIblGenerationInfo* generationInfo)
{
    VulkanIblGenerationInfo info = ResolveIblGenerationInfo(settings);

    const u32 srcFace = info.prefilteredFaceSize;
    const u32 irrFace = info.irradianceFaceSize;
    const u32 mipCount = info.prefilteredMipCount;
    const u32 faceCount = 6;
    const VkFormat fmt = kIblEnvironmentFormat;

    IblTexturePayload payload{};
    if (info.cachePolicy == VulkanIblCachePolicy::PreferOffline) {
        if (std::optional<IblTexturePayload> cachedPayload =
                TryReadIblCache(info)) {
            payload = std::move(*cachedPayload);
            info.cacheHit = 1u;
            info.runtimeGenerated = 0u;
            info.cacheFallbackReason = VulkanIblCacheFallbackReason::None;
        } else {
            info.cacheFallbackReason =
                VulkanIblCacheFallbackReason::OfflineCacheUnavailable;
        }
    }

    if (payload.prefilteredBasePixels.empty() ||
        payload.irradiancePixels.empty() ||
        payload.brdfPixels.empty()) {
        try {
            if (info.actualSource == VulkanIblSource::AuthoredEquirectangular ||
                info.actualSource == VulkanIblSource::VisibleSkybox) {
                payload = BuildEquirectangularIblPayload(
                    info,
                    ResolveSourcePath(settings.sourceAssetPath)
                );
            } else {
                payload = BuildProceduralIblPayload(info);
            }
        } catch (...) {
            info.actualSource = VulkanIblSource::Procedural;
            info.sourceFallbackReason =
                VulkanIblSourceFallbackReason::SourceLoadFailed;
            info.sourceAssetFound = 0u;
            info.sourceSignature = 0u;
            payload = BuildProceduralIblPayload(info);
        }
        info.runtimeGenerated = 1u;
        info.cacheHit = 0u;
        if (info.cachePolicy == VulkanIblCachePolicy::PreferOffline) {
            TryWriteIblCache(info, payload);
        }
    }

    if (generationInfo != nullptr) {
        *generationInfo = info;
    }

    VulkanBuffer staging(device, physicalDevice, payload.prefilteredBasePixels.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.Upload(std::as_bytes(std::span<const u8>(
        payload.prefilteredBasePixels
    )));

    // Prefiltered cubemap. Generate mips from the cached/generated base map so
    // startup does not depend on optional compute IBL shaders being present.
    prefilteredImage = std::make_unique<VulkanImage>(device, physicalDevice,
        VkExtent2D{srcFace, srcFace}, fmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
        mipCount, faceCount, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
    prefilteredImage->TransitionLayout(device, commandPool, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipCount);
    prefilteredImage->CopyFromBuffer(device, commandPool, staging.Handle(), faceCount);
    prefilteredImage->GenerateMipmaps(physicalDevice, device, commandPool, faceCount);
    prefilteredView = prefilteredImage->View();

    // Irradiance cubemap
    VulkanBuffer irrStaging(device, physicalDevice, payload.irradiancePixels.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    irrStaging.Upload(std::as_bytes(std::span<const u8>(
        payload.irradiancePixels
    )));
    irradianceImage = std::make_unique<VulkanImage>(device, physicalDevice,
        VkExtent2D{irrFace, irrFace}, fmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
        1, faceCount, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
    irradianceImage->TransitionLayout(device, commandPool, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
    irradianceImage->CopyFromBuffer(device, commandPool, irrStaging.Handle(), faceCount);
    irradianceImage->TransitionLayout(device, commandPool, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    irradianceView = irradianceImage->View();

    // BRDF LUT
    const u32 lutSize = info.brdfLutSize;
    VulkanBuffer brdfStaging(device, physicalDevice, payload.brdfPixels.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    brdfStaging.Upload(std::as_bytes(std::span<const u8>(
        payload.brdfPixels
    )));
    brdfImage = std::make_unique<VulkanImage>(device, physicalDevice,
        VkExtent2D{lutSize, lutSize}, kIblBrdfLutFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    brdfImage->TransitionLayout(device, commandPool, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
    brdfImage->CopyFromBuffer(device, commandPool, brdfStaging.Handle());
    brdfImage->TransitionLayout(device, commandPool, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);

    VkSamplerCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = float(mipCount);
    vkCreateSampler(device.Handle(), &si, nullptr, &sampler);
}

}
