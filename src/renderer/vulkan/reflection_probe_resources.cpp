#include "renderer/vulkan/reflection_probe_resources.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/depth_buffer.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/ibl_generator.h"
#include "renderer/vulkan/image.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/shader_module.h"

#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#ifndef SE_SHADER_DIR
#define SE_SHADER_DIR "assets/shaders"
#endif

namespace se {

namespace {

constexpr u32 kDefaultEquirectangularCubemapFaceSizeLimit = 128u;
constexpr u32 kMinEquirectangularCubemapFaceSizeLimit = 64u;
constexpr u32 kMaxEquirectangularCubemapFaceSizeLimit = 1024u;
constexpr const char* kEquirectangularCubemapFaceSizeEnv =
    "SE_REFLECTION_PROBE_EQUIRECT_FACE_SIZE";
constexpr u32 kCapturedSceneMaxLightSamples = 16u;
constexpr std::size_t kMaxCapturedSceneProbeResourceCount = 4u;
constexpr VkFormat kGpuCapturedSceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr u32 kGpuCapturedSceneDiffuseIrradianceFaceSize = 32u;
constexpr u32 kGpuCapturedSceneDiffuseIrradianceSampleCount = 64u;

u64 CapturedSceneCubemapMipMemoryBytes(u32 faceSize, u32 mipCount) {
    u64 texelCount = 0u;
    u32 extent = faceSize;
    for (u32 mip = 0u; mip < mipCount; ++mip) {
        texelCount += static_cast<u64>(extent) * static_cast<u64>(extent) * 6u;
        extent = std::max(1u, extent >> 1u);
    }
    return texelCount * sizeof(u16) * 4u;
}

u32 MipCountForExtent(u32 extent) {
    u32 mipCount = 1u;
    while (extent > 1u) {
        extent >>= 1u;
        ++mipCount;
    }
    return mipCount;
}

void RecordImageBarrier(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags sourceAccess,
    VkAccessFlags destinationAccess,
    VkPipelineStageFlags sourceStage,
    VkPipelineStageFlags destinationStage,
    VkImageAspectFlags aspectMask,
    u32 baseMipLevel,
    u32 levelCount,
    u32 baseArrayLayer,
    u32 layerCount
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount = layerCount;
    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage,
        destinationStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

VkRenderPass CreateGpuCapturedSceneRenderPass(
    const VulkanDevice& device,
    VkFormat depthFormat
) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = kGpuCapturedSceneFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const std::array<VkAttachmentDescription, 2> attachments = {
        colorAttachment,
        depthAttachment
    };
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<u32>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device.Handle(), &createInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU reflection capture render pass");
    }
    return renderPass;
}

VkImageView CreateGpuCapturedSceneFaceView(
    const VulkanDevice& device,
    VkImage image,
    u32 face
) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = kGpuCapturedSceneFormat;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = face;
    createInfo.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device.Handle(), &createInfo, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU reflection capture face view");
    }
    return view;
}

VkImageView CreateGpuCapturedSceneCubeView(
    const VulkanDevice& device,
    VkImage image,
    u32 mipCount
) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    createInfo.format = kGpuCapturedSceneFormat;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0u;
    createInfo.subresourceRange.levelCount = mipCount;
    createInfo.subresourceRange.baseArrayLayer = 0u;
    createInfo.subresourceRange.layerCount = 6u;
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device.Handle(), &createInfo, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU reflection capture source cube view");
    }
    return view;
}

VkImageView CreateGpuCapturedSceneMipArrayView(
    const VulkanDevice& device,
    VkImage image,
    u32 mipLevel
) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    createInfo.format = kGpuCapturedSceneFormat;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = mipLevel;
    createInfo.subresourceRange.levelCount = 1u;
    createInfo.subresourceRange.baseArrayLayer = 0u;
    createInfo.subresourceRange.layerCount = 6u;
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device.Handle(), &createInfo, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU reflection capture prefilter mip view");
    }
    return view;
}

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
    AuthoredReflectionProbeFilterQuality filterQuality =
        AuthoredReflectionProbeFilterQuality::Medium;
    bool seamAwareFiltering = true;
    bool irradianceReady = false;
    std::array<f32, 3> irradianceColor{ 1.0f, 1.0f, 1.0f };
    bool diffuseLobesReady = false;
    AuthoredReflectionProbeDiffuseLobes diffuseLobes{};
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
    AuthoredReflectionProbeFilterQuality filterQuality =
        AuthoredReflectionProbeFilterQuality::Medium;
    bool seamAwareFiltering = true;
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

u32 ParseEquirectangularCubemapFaceSizeLimit(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return kDefaultEquirectangularCubemapFaceSizeLimit;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value) {
        return kDefaultEquirectangularCubemapFaceSizeLimit;
    }

    return std::clamp(
        static_cast<u32>(parsed),
        kMinEquirectangularCubemapFaceSizeLimit,
        kMaxEquirectangularCubemapFaceSizeLimit
    );
}

u32 EquirectangularCubemapFaceSizeLimit() {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, kEquirectangularCubemapFaceSizeEnv) != 0) {
        return kDefaultEquirectangularCubemapFaceSizeLimit;
    }
    const u32 limit = ParseEquirectangularCubemapFaceSizeLimit(value);
    std::free(value);
    return limit;
#else
    return ParseEquirectangularCubemapFaceSizeLimit(
        std::getenv(kEquirectangularCubemapFaceSizeEnv)
    );
#endif
}

u32 EquirectangularCubemapFaceSizeForSource(
    const LoadedEquirectangularImage& image
) {
    const u32 sourceFaceSize =
        std::max(1u, static_cast<u32>(image.height / 2));
    return std::min(sourceFaceSize, EquirectangularCubemapFaceSizeLimit());
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

std::array<float, 3> Sub3(
    const std::array<float, 3>& a,
    const std::array<float, 3>& b
) {
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

float SmoothStep01(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0f : 1.0f;
    }
    const float t =
        std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
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

float CubemapTexelSolidAngle(u32 faceSize, u32 x, u32 y) {
    const float size = static_cast<float>(faceSize);
    const float u = (2.0f * (static_cast<float>(x) + 0.5f) / size) - 1.0f;
    const float v = (2.0f * (static_cast<float>(y) + 0.5f) / size) - 1.0f;
    const float denominator = 1.0f + u * u + v * v;
    return 4.0f / (size * size * std::sqrt(denominator * denominator * denominator));
}

std::array<f32, 3> ComputeCubemapDiffuseIrradianceColor(
    const AuthoredCubemapPixelData& pixelData,
    u32 faceSize
) {
    std::array<double, 3> weightedColor{ 0.0, 0.0, 0.0 };
    double totalWeight = 0.0;
    for (std::size_t face = 0; face < 6u; ++face) {
        for (u32 y = 0; y < faceSize; ++y) {
            for (u32 x = 0; x < faceSize; ++x) {
                const float solidAngle = CubemapTexelSolidAngle(faceSize, x, y);
                const std::array<float, 4> texel =
                    LoadCubemapBaseTexel(pixelData, faceSize, face, x, y);
                weightedColor[0] += static_cast<double>(texel[0]) * solidAngle;
                weightedColor[1] += static_cast<double>(texel[1]) * solidAngle;
                weightedColor[2] += static_cast<double>(texel[2]) * solidAngle;
                totalWeight += solidAngle;
            }
        }
    }

    if (totalWeight <= 0.000001) {
        return { 1.0f, 1.0f, 1.0f };
    }
    const double invWeight = 1.0 / totalWeight;
    return {
        static_cast<f32>(std::max(0.0, weightedColor[0] * invWeight)),
        static_cast<f32>(std::max(0.0, weightedColor[1] * invWeight)),
        static_cast<f32>(std::max(0.0, weightedColor[2] * invWeight))
    };
}

AuthoredReflectionProbeDiffuseLobes ComputeCubemapDiffuseIrradianceLobes(
    const AuthoredCubemapPixelData& pixelData,
    u32 faceSize,
    const std::array<f32, 3>& baseColor
) {
    constexpr std::array<std::array<float, 3>, kAuthoredReflectionProbeDiffuseLobeCount>
        kLobeAxes{ {
            { 1.0f, 0.0f, 0.0f },
            { -1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            { 0.0f, -1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f },
            { 0.0f, 0.0f, -1.0f }
        } };

    std::array<std::array<double, 3>, kAuthoredReflectionProbeDiffuseLobeCount>
        weightedColor{};
    std::array<double, kAuthoredReflectionProbeDiffuseLobeCount> totalWeight{};
    const float size = static_cast<float>(faceSize);
    for (std::size_t face = 0; face < 6u; ++face) {
        for (u32 y = 0; y < faceSize; ++y) {
            for (u32 x = 0; x < faceSize; ++x) {
                const float u =
                    (2.0f * (static_cast<float>(x) + 0.5f) / size) - 1.0f;
                const float v =
                    (2.0f * (static_cast<float>(y) + 0.5f) / size) - 1.0f;
                const std::array<float, 3> direction =
                    CubemapDirection(face, u, v);
                const float solidAngle = CubemapTexelSolidAngle(faceSize, x, y);
                const std::array<float, 4> texel =
                    LoadCubemapBaseTexel(pixelData, faceSize, face, x, y);

                for (std::size_t lobe = 0; lobe < kLobeAxes.size(); ++lobe) {
                    const float cosine =
                        std::max(Dot3(direction, kLobeAxes[lobe]), 0.0f);
                    if (cosine <= 0.000001f) {
                        continue;
                    }
                    const double weight =
                        static_cast<double>(solidAngle) *
                        static_cast<double>(cosine);
                    weightedColor[lobe][0] += static_cast<double>(texel[0]) * weight;
                    weightedColor[lobe][1] += static_cast<double>(texel[1]) * weight;
                    weightedColor[lobe][2] += static_cast<double>(texel[2]) * weight;
                    totalWeight[lobe] += weight;
                }
            }
        }
    }

    AuthoredReflectionProbeDiffuseLobes lobes{};
    for (std::size_t lobe = 0; lobe < lobes.size(); ++lobe) {
        if (totalWeight[lobe] <= 0.000001) {
            continue;
        }
        const double invWeight = 1.0 / totalWeight[lobe];
        for (std::size_t channel = 0; channel < 3u; ++channel) {
            lobes[lobe][channel] =
                static_cast<f32>(weightedColor[lobe][channel] * invWeight) -
                baseColor[channel];
        }
    }
    return lobes;
}

std::array<float, 3> Multiply3(
    const std::array<float, 3>& a,
    const std::array<float, 3>& b
) {
    return { a[0] * b[0], a[1] * b[1], a[2] * b[2] };
}

std::array<float, 3> Clamp3(
    const std::array<float, 3>& value,
    float minValue,
    float maxValue
) {
    return {
        std::clamp(value[0], minValue, maxValue),
        std::clamp(value[1], minValue, maxValue),
        std::clamp(value[2], minValue, maxValue)
    };
}

std::array<float, 3> BoxFaceTintForDirection(
    const std::array<float, 3>& direction
) {
    const float up = std::max(direction[1], 0.0f);
    const float down = std::max(-direction[1], 0.0f);
    const float side = std::max(
        std::fabs(direction[0]),
        std::fabs(direction[2])
    );
    std::array<float, 3> tint{
        0.30f + side * 0.16f + up * 0.16f,
        0.32f + side * 0.15f + up * 0.17f,
        0.35f + side * 0.16f + up * 0.20f
    };
    tint[0] += down * 0.08f;
    tint[1] += down * 0.075f;
    tint[2] += down * 0.07f;
    return tint;
}

float CapturedPointLightLobe(
    const CapturedReflectionProbeSceneSample& sceneSample,
    const CapturedReflectionProbeLightSample& light,
    const std::array<float, 3>& sampleDirection
) {
    std::array<float, 3> lightVector{
        light.position[0] - sceneSample.center[0],
        light.position[1] - sceneSample.center[1],
        light.position[2] - sceneSample.center[2]
    };
    const float distanceSquared = std::max(Dot3(lightVector, lightVector), 0.0001f);
    const float distance = std::sqrt(distanceSquared);
    const std::array<float, 3> lightDirection =
        Scale3(lightVector, 1.0f / distance);
    const float angular = std::pow(
        std::max(Dot3(sampleDirection, lightDirection), 0.0f),
        42.0f
    );
    const float radius = std::max(light.radius, 0.25f);
    const float range = std::max(0.0f, 1.0f - distance / radius);
    return angular * range * range;
}

float CapturedDirectionalLightLobe(
    const CapturedReflectionProbeLightSample& light,
    const std::array<float, 3>& sampleDirection
) {
    const std::array<float, 3> lightForward =
        Normalize3(light.direction);
    const float facing = std::max(Dot3(sampleDirection, Scale3(lightForward, -1.0f)), 0.0f);
    return std::pow(facing, light.kind == 2u ? 26.0f : 36.0f);
}

float CapturedRectLightLobe(
    const CapturedReflectionProbeSceneSample& sceneSample,
    const CapturedReflectionProbeLightSample& light,
    const std::array<float, 3>& sampleDirection
) {
    const std::array<float, 3> center{
        sceneSample.center[0],
        sceneSample.center[1],
        sceneSample.center[2]
    };
    const std::array<float, 3> lightPosition{
        light.position[0],
        light.position[1],
        light.position[2]
    };
    const std::array<float, 3> lightVector = Sub3(lightPosition, center);
    const float distanceSquared =
        std::max(Dot3(lightVector, lightVector), 0.0001f);
    const float distance = std::sqrt(distanceSquared);
    const std::array<float, 3> lightDirection =
        Scale3(lightVector, 1.0f / distance);
    const std::array<float, 3> normal = Normalize3(light.direction);
    const float denom = Dot3(sampleDirection, normal);
    if (std::fabs(denom) <= 0.0001f) {
        return 0.0f;
    }

    const float t = Dot3(lightVector, normal) / denom;
    if (t <= 0.0f) {
        return 0.0f;
    }

    const std::array<float, 3> hitOffset =
        Sub3(Scale3(sampleDirection, t), lightVector);
    const std::array<float, 3> reference =
        std::fabs(normal[1]) > 0.94f
            ? std::array<float, 3>{ 1.0f, 0.0f, 0.0f }
            : std::array<float, 3>{ 0.0f, 1.0f, 0.0f };
    const std::array<float, 3> right =
        Normalize3(Cross3(reference, normal));
    const std::array<float, 3> up =
        Normalize3(Cross3(normal, right));
    const float halfWidth = std::max(light.width * 0.5f, 0.04f);
    const float halfHeight = std::max(light.height * 0.5f, 0.04f);
    const float u = Dot3(hitOffset, right) / halfWidth;
    const float v = Dot3(hitOffset, up) / halfHeight;
    const float edgeDistance = std::max(std::fabs(u), std::fabs(v));
    const float rectMask =
        1.0f - SmoothStep01(0.92f, 1.18f, edgeDistance);
    if (rectMask <= 0.0001f) {
        return 0.0f;
    }

    const float radius = std::max(light.radius, 0.25f);
    const float range = std::max(0.0f, 1.0f - distance / radius);
    const float facing =
        std::max(Dot3(normal, Scale3(lightDirection, -1.0f)), 0.0f);
    const float facingWeight = 0.22f + 0.78f * facing;
    const float angleWeight =
        std::pow(std::clamp(std::fabs(denom), 0.02f, 1.0f), 0.35f);
    return rectMask * range * range * facingWeight * angleWeight;
}

std::array<float, 3> CapturedSceneRadiance(
    const CapturedReflectionProbeSceneSample& sceneSample,
    std::span<const CapturedReflectionProbeLightSample> lights,
    const std::array<float, 3>& direction
) {
    std::array<float, 3> radiance = Scale3(
        Multiply3(BoxFaceTintForDirection(direction), sceneSample.tint),
        std::clamp(sceneSample.ambientStrength, 0.0f, 2.0f)
    );
    radiance = Add3(
        radiance,
        Scale3(
            sceneSample.ambientColor,
            std::clamp(sceneSample.ambientStrength, 0.0f, 2.0f)
        )
    );

    const std::array<float, 3> sunDirection =
        Normalize3(sceneSample.directionalDirection);
    const float sunFacing =
        std::pow(std::max(Dot3(direction, Scale3(sunDirection, -1.0f)), 0.0f), 16.0f);
    radiance = Add3(
        radiance,
        Scale3(
            sceneSample.tint,
            sunFacing * std::max(sceneSample.directionalIntensity, 0.0f) * 0.16f
        )
    );

    const std::size_t lightCount = std::min<std::size_t>(
        lights.size(),
        kCapturedSceneMaxLightSamples
    );
    for (std::size_t index = 0; index < lightCount; ++index) {
        const CapturedReflectionProbeLightSample& light = lights[index];
        float lobe = 0.0f;
        float energyScale = 0.12f;
        if (light.kind == 3u) {
            lobe = CapturedRectLightLobe(sceneSample, light, direction);
            energyScale = 0.15f;
        } else {
            const float pointLobe = CapturedPointLightLobe(
                sceneSample,
                light,
                direction
            );
            const float directionalLobe = light.kind == 2u
                ? CapturedDirectionalLightLobe(light, direction)
                : 1.0f;
            lobe = pointLobe * directionalLobe;
            energyScale = light.kind == 2u ? 0.16f : 0.12f;
        }
        const float energy =
            lobe *
            std::max(light.intensity, 0.0f) *
            energyScale;
        radiance = Add3(
            radiance,
            Scale3(
                Multiply3(light.color, sceneSample.tint),
                energy
            )
        );
    }

    return Clamp3(
        Scale3(radiance, std::clamp(sceneSample.intensity, 0.0f, 4.0f)),
        0.0f,
        1.0f
    );
}

AuthoredCubemapPixelData BuildCapturedSceneCubemapPixels(
    const CapturedReflectionProbeSceneSample& sceneSample,
    std::span<const CapturedReflectionProbeLightSample> lights,
    u32 faceSize
) {
    AuthoredCubemapPixelData pixelData{};
    pixelData.format = VK_FORMAT_R8G8B8A8_SRGB;
    pixelData.hdr = false;
    pixelData.bytesPerTexel = 4u;
    pixelData.pixels.resize(
        static_cast<std::size_t>(faceSize) *
        static_cast<std::size_t>(faceSize) *
        6u *
        pixelData.bytesPerTexel
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
                const std::array<float, 3> radiance =
                    CapturedSceneRadiance(sceneSample, lights, direction);
                const std::size_t offset =
                    (((face * static_cast<std::size_t>(faceSize) +
                       static_cast<std::size_t>(y)) *
                      static_cast<std::size_t>(faceSize)) +
                     static_cast<std::size_t>(x)) *
                    pixelData.bytesPerTexel;
                pixelData.pixels[offset + 0u] =
                    EncodeReflectionChannel(radiance[0], false);
                pixelData.pixels[offset + 1u] =
                    EncodeReflectionChannel(radiance[1], false);
                pixelData.pixels[offset + 2u] =
                    EncodeReflectionChannel(radiance[2], false);
                pixelData.pixels[offset + 3u] = 255u;
            }
        }
    }

    return pixelData;
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
    const std::array<float, 3>& direction,
    bool seamAwareFiltering
) {
    const CubemapSampleLocation location =
        CubemapLocationForDirection(direction, faceSize);
    const int x0 = static_cast<int>(std::floor(location.x));
    const int y0 = static_cast<int>(std::floor(location.y));
    const float tx = location.x - static_cast<float>(x0);
    const float ty = location.y - static_cast<float>(y0);

    const auto load = [&](int x, int y) {
        std::size_t sampleFace = location.face;
        int sampleX = x;
        int sampleY = y;
        const int maxTexel = static_cast<int>(faceSize) - 1;
        if (seamAwareFiltering &&
            (x < 0 || y < 0 || x > maxTexel || y > maxTexel)) {
            const float size = static_cast<float>(faceSize);
            const float u =
                (2.0f * (static_cast<float>(x) + 0.5f) / size) - 1.0f;
            const float v =
                (2.0f * (static_cast<float>(y) + 0.5f) / size) - 1.0f;
            const CubemapSampleLocation remapped =
                CubemapLocationForDirection(
                    CubemapDirection(location.face, u, v),
                    faceSize
                );
            sampleFace = remapped.face;
            sampleX = static_cast<int>(std::floor(remapped.x + 0.5f));
            sampleY = static_cast<int>(std::floor(remapped.y + 0.5f));
        }
        const u32 clampedX = static_cast<u32>(std::clamp(sampleX, 0, maxTexel));
        const u32 clampedY = static_cast<u32>(std::clamp(sampleY, 0, maxTexel));
        return LoadCubemapBaseTexel(
            pixelData,
            faceSize,
            sampleFace,
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

u32 PrefilterSampleCountForMip(
    u32 mipLevel,
    u32 mipLevels,
    AuthoredReflectionProbeFilterQuality quality
) {
    if (mipLevel == 0u || mipLevels <= 1u) {
        return 1u;
    }

    switch (quality) {
    case AuthoredReflectionProbeFilterQuality::Low:
        return std::min(64u, 8u + mipLevel * 8u);
    case AuthoredReflectionProbeFilterQuality::High:
        return std::min(256u, 64u + mipLevel * 64u);
    case AuthoredReflectionProbeFilterQuality::Ultra:
        return std::min(512u, 128u + mipLevel * 128u);
    case AuthoredReflectionProbeFilterQuality::Medium:
        return std::min(128u, 32u + mipLevel * 32u);
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
    u32 sampleCount,
    bool seamAwareFiltering
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
        return SampleCubemapBaseBilinear(
            pixelData,
            faceSize,
            normal,
            seamAwareFiltering
        );
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
            SampleCubemapBaseBilinear(
                pixelData,
                faceSize,
                light,
                seamAwareFiltering
            );
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
    u32 faceSize,
    AuthoredReflectionProbeFilteringSettings filteringSettings
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
    mipChain.filterQuality = filteringSettings.quality;
    mipChain.seamAwareFiltering = filteringSettings.seamAwareFiltering;
    mipChain.prefilterSampleCount =
        PrefilterSampleCountForMip(
            mipChain.mipLevels - 1u,
            mipChain.mipLevels,
            filteringSettings.quality
        );
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
                    mipChain.mipLevels,
                    filteringSettings.quality
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
                                sampleCount,
                                filteringSettings.seamAwareFiltering
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
    std::string_view assetId,
    AuthoredReflectionProbeFilteringSettings filteringSettings
) {
    const ResolvedAuthoredCubemapSource source =
        ResolveAuthoredCubemapSource(assetId);

    if (source.type == AuthoredReflectionCubemapSourceType::Equirectangular) {
        const LoadedEquirectangularImage equirectangular =
            LoadEquirectangularImage(source.equirectangularPath);
        const u32 faceSize =
            EquirectangularCubemapFaceSizeForSource(equirectangular);
        AuthoredCubemapPixelData pixelData =
            ConvertEquirectangularToCubemapPixels(equirectangular, faceSize);
        const std::array<f32, 3> irradianceColor =
            ComputeCubemapDiffuseIrradianceColor(pixelData, faceSize);
        const AuthoredReflectionProbeDiffuseLobes diffuseLobes =
            ComputeCubemapDiffuseIrradianceLobes(
                pixelData,
                faceSize,
                irradianceColor
            );
        AuthoredCubemapMipChain mipChain =
            BuildPrefilteredCubemapMipChain(
                pixelData,
                faceSize,
                filteringSettings
            );
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
            mipChain.prefilterSampleCount,
            mipChain.filterQuality,
            mipChain.seamAwareFiltering,
            true,
            irradianceColor,
            true,
            diffuseLobes
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
    const std::array<f32, 3> irradianceColor =
        ComputeCubemapDiffuseIrradianceColor(
            pixelData,
            static_cast<u32>(faces[0].width)
        );
    const AuthoredReflectionProbeDiffuseLobes diffuseLobes =
        ComputeCubemapDiffuseIrradianceLobes(
            pixelData,
            static_cast<u32>(faces[0].width),
            irradianceColor
        );
    AuthoredCubemapMipChain mipChain =
        BuildPrefilteredCubemapMipChain(
            pixelData,
            static_cast<u32>(faces[0].width),
            filteringSettings
        );

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
        mipChain.prefilterSampleCount,
        mipChain.filterQuality,
        mipChain.seamAwareFiltering,
        true,
        irradianceColor,
        true,
        diffuseLobes
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
    std::string_view assetId,
    AuthoredReflectionProbeFilteringSettings filteringSettings
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
        resource.filterQuality = filteringSettings.quality;
        resource.seamAwareFiltering = filteringSettings.seamAwareFiltering;
        resource.irradianceReady = false;
        resource.irradianceColor = { 1.0f, 1.0f, 1.0f };
        resource.diffuseLobesReady = false;
        resource.diffuseLobes = {};
        return;
    }

    u64 assetSignature = 0;
    try {
        const ResolvedAuthoredCubemapSource source =
            ResolveAuthoredCubemapSource(assetId);
        assetSignature = AuthoredCubemapSourceSignature(source, assetPath);
    } catch (...) {
        assetSignature = FileSystemEntrySignature(assetPath);
        assetSignature = HashCombine64(
            assetSignature,
            static_cast<u64>(filteringSettings.quality)
        );
        assetSignature = HashCombine64(
            assetSignature,
            filteringSettings.seamAwareFiltering ? 1u : 0u
        );
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
        resource.filterQuality = filteringSettings.quality;
        resource.seamAwareFiltering = filteringSettings.seamAwareFiltering;
        resource.irradianceReady = false;
        resource.irradianceColor = { 1.0f, 1.0f, 1.0f };
        resource.diffuseLobesReady = false;
        resource.diffuseLobes = {};
        return;
    }

    assetSignature = HashCombine64(
        assetSignature,
        static_cast<u64>(filteringSettings.quality)
    );
    assetSignature = HashCombine64(
        assetSignature,
        filteringSettings.seamAwareFiltering ? 1u : 0u
    );

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
    resource.filterQuality = filteringSettings.quality;
    resource.seamAwareFiltering = filteringSettings.seamAwareFiltering;
    resource.irradianceReady = false;
    resource.irradianceColor = { 1.0f, 1.0f, 1.0f };
    resource.diffuseLobesReady = false;
    resource.diffuseLobes = {};

    try {
        AuthoredCubemapLoadResult loadResult = CreateAuthoredCubemapImage(
            device,
            physicalDevice,
            commandPool,
            assetId,
            filteringSettings
        );
        resource.image = std::move(loadResult.image);
        resource.sourceType = loadResult.sourceType;
        resource.assetSignature = assetSignature;
        resource.hdr = loadResult.hdr;
        resource.prefiltered = loadResult.prefilteredMipChain;
        resource.generatedMipCount = loadResult.generatedMipCount;
        resource.prefilterSampleCount = loadResult.prefilterSampleCount;
        resource.filterQuality = loadResult.filterQuality;
        resource.seamAwareFiltering = loadResult.seamAwareFiltering;
        resource.irradianceReady = loadResult.irradianceReady;
        resource.irradianceColor = loadResult.irradianceColor;
        resource.diffuseLobesReady = loadResult.diffuseLobesReady;
        resource.diffuseLobes = loadResult.diffuseLobes;
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
        resource.filterQuality = filteringSettings.quality;
        resource.seamAwareFiltering = filteringSettings.seamAwareFiltering;
        resource.irradianceReady = false;
        resource.irradianceColor = { 1.0f, 1.0f, 1.0f };
        resource.diffuseLobesReady = false;
        resource.diffuseLobes = {};
    }
}

VulkanReflectionProbeResources::CapturedSceneProbeResource*
VulkanReflectionProbeResources::FindCapturedSceneProbeResource(
    i32 probeSceneIndex
) {
    const auto found = m_CapturedSceneProbeResources.find(probeSceneIndex);
    return found != m_CapturedSceneProbeResources.end() ? &found->second : nullptr;
}

const VulkanReflectionProbeResources::CapturedSceneProbeResource*
VulkanReflectionProbeResources::FindCapturedSceneProbeResource(
    i32 probeSceneIndex
) const {
    const auto found = m_CapturedSceneProbeResources.find(probeSceneIndex);
    return found != m_CapturedSceneProbeResources.end() ? &found->second : nullptr;
}

VulkanReflectionProbeResources::CapturedSceneProbeResource*
VulkanReflectionProbeResources::FindOrCreateCapturedSceneProbeResource(
    i32 probeSceneIndex
) {
    if (probeSceneIndex < 0) {
        return nullptr;
    }
    if (const auto found = FindCapturedSceneProbeResource(probeSceneIndex);
        found != nullptr) {
        return found;
    }
    if (m_CapturedSceneProbeResources.size() >=
        kMaxCapturedSceneProbeResourceCount) {
        return nullptr;
    }
    auto [inserted, created] = m_CapturedSceneProbeResources.try_emplace(
        probeSceneIndex
    );
    inserted->second.audit.probeSceneIndex = probeSceneIndex;
    return &inserted->second;
}

void VulkanReflectionProbeResources::EnsureCapturedSceneCubemap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    i32 probeSceneIndex,
    const CapturedReflectionProbeSceneSample& sceneSample,
    std::span<const CapturedReflectionProbeLightSample> lights,
    const CapturedSceneRefreshRequest& refreshRequest,
    AuthoredReflectionProbeFilteringSettings filteringSettings
) {
    CapturedSceneProbeResource* resource =
        FindOrCreateCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr) {
        return;
    }
    m_CapturedSceneCubemapFaceSize = std::clamp(
        filteringSettings.faceSize,
        128u,
        1024u
    );
    m_CapturedSceneCubemapFaceSizeInitialized = true;
    m_LastCapturedSceneProbeSceneIndex = probeSceneIndex;
    ++resource->refreshCheckCount;
    const bool resourceReady =
        resource->activeImage != nullptr &&
        resource->activeImage->View() != VK_NULL_HANDLE;
    CapturedSceneCaptureAudit audit{};
    audit.backend = CapturedSceneCaptureBackend::AnalyticCpu;
    audit.lastRefreshReason = resource->lastRefreshReason;
    audit.probeSceneIndex = probeSceneIndex;
    audit.faceCount = 6u;
    audit.captureSignature = refreshRequest.captureSignature;
    audit.radianceSignature = sceneSample.signature;
    audit.membershipRevision = refreshRequest.membershipRevision;
    audit.lightRevision = refreshRequest.lightRevision;
    audit.renderRevision = refreshRequest.renderRevision;
    audit.schedulerFrame = refreshRequest.schedulerFrame;
    audit.lastRefreshCompletedFrame = resource->lastRefreshCompletedFrame;
    audit.localLightSignature = refreshRequest.localLightSignature;
    audit.geometrySignature = refreshRequest.geometrySignature;
    audit.affectedLocalLightCount = refreshRequest.affectedLocalLightCount;
    audit.affectedRenderableCount = refreshRequest.affectedRenderableCount;
    audit.localLightIdentityMask = refreshRequest.localLightIdentityMask;
    audit.geometryIdentityMask = refreshRequest.geometryIdentityMask;
    audit.localLightRegionMask = refreshRequest.localLightRegionMask;
    audit.geometryRegionMask = refreshRequest.geometryRegionMask;
    audit.refreshPriority = refreshRequest.refreshPriority;
    audit.minimumRefreshIntervalFrames =
        refreshRequest.minimumRefreshIntervalFrames;
    audit.refreshDeferredCount = resource->refreshDeferredCount;
    audit.selectiveInvalidationEnabled =
        refreshRequest.selectiveInvalidationEnabled;
    audit.resourceReady = resourceReady;
    audit.rasterizedGeometry = false;

    if (!resourceReady) {
        audit.refreshReason = CapturedSceneRefreshReason::Initial;
        audit.refreshRequested = true;
    } else {
        const bool geometryChanged =
            refreshRequest.geometrySignature != resource->geometrySignature;
        const bool localLightsChanged =
            refreshRequest.localLightSignature != resource->localLightSignature;
        const bool membershipChanged =
            refreshRequest.membershipRevision != resource->membershipRevision;
        const bool renderChanged =
            refreshRequest.renderRevision != resource->renderRevision;
        const bool lightChanged =
            refreshRequest.lightRevision != resource->lightRevision;
        const bool globalGeometryChanged = membershipChanged || renderChanged;
        audit.localityIgnoredLightRevision =
            lightChanged &&
            refreshRequest.selectiveInvalidationEnabled &&
            !localLightsChanged;
        audit.localityIgnoredGeometryRevision =
            globalGeometryChanged &&
            refreshRequest.selectiveInvalidationEnabled &&
            !geometryChanged;
        audit.localLightDirty = lightChanged &&
            (!refreshRequest.selectiveInvalidationEnabled || localLightsChanged);
        audit.geometryDirty = globalGeometryChanged &&
            (!refreshRequest.selectiveInvalidationEnabled || geometryChanged);
        audit.dirtyLocalLightCount =
            audit.localLightDirty ? refreshRequest.affectedLocalLightCount : 0u;
        audit.dirtyRenderableCount =
            audit.geometryDirty ? refreshRequest.affectedRenderableCount : 0u;
        if (membershipChanged &&
            (!refreshRequest.selectiveInvalidationEnabled || geometryChanged)) {
            audit.dirtyMask |= CapturedSceneDirtyMembership;
        }
        if (lightChanged &&
            (!refreshRequest.selectiveInvalidationEnabled || localLightsChanged)) {
            audit.dirtyMask |= CapturedSceneDirtyLight;
        }
        if (renderChanged &&
            (!refreshRequest.selectiveInvalidationEnabled || geometryChanged)) {
            audit.dirtyMask |= CapturedSceneDirtyRender;
        }
        if (refreshRequest.captureSignature != resource->signature &&
            audit.dirtyMask == CapturedSceneDirtyNone) {
            audit.dirtyMask |= CapturedSceneDirtyContent;
        }
        if (refreshRequest.sceneDirtyOverride) {
            audit.dirtyMask |= CapturedSceneDirtyExternal;
        }

        if (refreshRequest.forceRefresh) {
            audit.refreshReason = CapturedSceneRefreshReason::Forced;
            audit.refreshRequested = true;
        } else if (refreshRequest.refreshPolicy ==
                   RendererReflectionProbeRefreshPolicy::Forced) {
            audit.refreshReason = CapturedSceneRefreshReason::ForcedPolicy;
            audit.refreshRequested = true;
        } else if (refreshRequest.refreshPolicy ==
                   RendererReflectionProbeRefreshPolicy::SceneDirty) {
            if ((audit.dirtyMask & CapturedSceneDirtyExternal) != 0u) {
                audit.refreshReason = CapturedSceneRefreshReason::SceneDirtyOverride;
            } else if ((audit.dirtyMask & CapturedSceneDirtyMembership) != 0u) {
                audit.refreshReason = CapturedSceneRefreshReason::MembershipChanged;
            } else if ((audit.dirtyMask & CapturedSceneDirtyLight) != 0u) {
                audit.refreshReason = CapturedSceneRefreshReason::LightChanged;
            } else if ((audit.dirtyMask & CapturedSceneDirtyRender) != 0u) {
                audit.refreshReason = CapturedSceneRefreshReason::RenderChanged;
            } else if ((audit.dirtyMask & CapturedSceneDirtyContent) != 0u) {
                audit.refreshReason = CapturedSceneRefreshReason::ContentChanged;
            }
            audit.refreshRequested = audit.dirtyMask != CapturedSceneDirtyNone;
        } else if (refreshRequest.refreshPolicy ==
                   RendererReflectionProbeRefreshPolicy::FileSignature &&
                   (audit.dirtyMask & CapturedSceneDirtyContent) != 0u) {
            audit.refreshReason = CapturedSceneRefreshReason::ContentChanged;
            audit.refreshRequested = true;
        }
    }

    const bool budgetDeferrable = audit.refreshRequested &&
        !refreshRequest.forceRefresh &&
        refreshRequest.refreshPolicy !=
            RendererReflectionProbeRefreshPolicy::Forced &&
        !refreshRequest.sceneDirtyOverride &&
        resource->lastRefreshCompletedFrame > 0u &&
        refreshRequest.minimumRefreshIntervalFrames > 0u &&
        refreshRequest.schedulerFrame >= resource->lastRefreshCompletedFrame &&
        refreshRequest.schedulerFrame - resource->lastRefreshCompletedFrame <
            refreshRequest.minimumRefreshIntervalFrames;
    if (budgetDeferrable) {
        audit.refreshRequested = false;
        audit.refreshDeferredByBudget = true;
        ++resource->refreshDeferredCount;
        audit.refreshDeferredCount = resource->refreshDeferredCount;
    }

    if (!audit.refreshRequested) {
        resource->audit = audit;
        return;
    }

    const AuthoredCubemapPixelData pixelData =
        BuildCapturedSceneCubemapPixels(
            sceneSample,
            lights,
            m_CapturedSceneCubemapFaceSize
        );
    AuthoredCubemapMipChain mipChain =
        BuildPrefilteredCubemapMipChain(
            pixelData,
            m_CapturedSceneCubemapFaceSize,
            filteringSettings
        );
    resource->activeImage = UploadAuthoredCubemapMipChain(
        device,
        physicalDevice,
        commandPool,
        mipChain
    );
    resource->signature = refreshRequest.captureSignature;
    resource->radianceSignature = sceneSample.signature;
    resource->membershipRevision = refreshRequest.membershipRevision;
    resource->lightRevision = refreshRequest.lightRevision;
    resource->renderRevision = refreshRequest.renderRevision;
    resource->localLightSignature = refreshRequest.localLightSignature;
    resource->geometrySignature = refreshRequest.geometrySignature;
    resource->localLightIdentityMask = refreshRequest.localLightIdentityMask;
    resource->geometryIdentityMask = refreshRequest.geometryIdentityMask;
    resource->localLightRegionMask = refreshRequest.localLightRegionMask;
    resource->geometryRegionMask = refreshRequest.geometryRegionMask;
    resource->lastRefreshCompletedFrame = refreshRequest.schedulerFrame;
    resource->activeBackend = CapturedSceneCaptureBackend::AnalyticCpu;
    ++resource->uploadCount;
    audit.lastRefreshCompletedFrame = resource->lastRefreshCompletedFrame;
    audit.resourceReady = true;
    audit.refreshPerformed = true;
    resource->lastRefreshReason = audit.refreshReason;
    audit.lastRefreshReason = resource->lastRefreshReason;
    resource->audit = audit;
}

bool VulkanReflectionProbeResources::EnsureGpuCapturedScenePrefilterResources(
    const VulkanDevice& device
) {
    if (m_GpuCapturedScenePrefilterPipeline != VK_NULL_HANDLE &&
        m_GpuCapturedScenePrefilterPipelineLayout != VK_NULL_HANDLE &&
        m_GpuCapturedScenePrefilterDescriptorSetLayout != VK_NULL_HANDLE &&
        m_GpuCapturedScenePrefilterDescriptorPool != VK_NULL_HANDLE &&
        m_GpuCapturedScenePrefilterSampler != VK_NULL_HANDLE) {
        return true;
    }

    try {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<f32>(
            MipCountForExtent(m_CapturedSceneCubemapFaceSize) - 1u
        );
        if (vkCreateSampler(
                device.Handle(),
                &samplerInfo,
                nullptr,
                &m_GpuCapturedScenePrefilterSampler
            ) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create reflection capture prefilter sampler");
        }

        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0u;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1u;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding = 1u;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1u;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<u32>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(
                device.Handle(),
                &layoutInfo,
                nullptr,
                &m_GpuCapturedScenePrefilterDescriptorSetLayout
            ) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create reflection capture prefilter descriptor layout");
        }

        const u32 descriptorSetCapacity =
            static_cast<u32>(kMaxCapturedSceneProbeResourceCount) *
            (MipCountForExtent(m_CapturedSceneCubemapFaceSize) - 1u);
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            descriptorSetCapacity
        };
        poolSizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSetCapacity };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = descriptorSetCapacity;
        if (vkCreateDescriptorPool(
                device.Handle(),
                &poolInfo,
                nullptr,
                &m_GpuCapturedScenePrefilterDescriptorPool
            ) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create reflection capture prefilter descriptor pool");
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0u;
        pushRange.size = sizeof(f32) + sizeof(u32) * 7u;
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1u;
        pipelineLayoutInfo.pSetLayouts = &m_GpuCapturedScenePrefilterDescriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1u;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(
                device.Handle(),
                &pipelineLayoutInfo,
                nullptr,
                &m_GpuCapturedScenePrefilterPipelineLayout
            ) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create reflection capture prefilter pipeline layout");
        }

        const VulkanShaderModule shader(
            device,
            std::string(SE_SHADER_DIR) + "/reflection_probe_prefilter.comp.spv"
        );
        VkPipelineShaderStageCreateInfo shaderStage{};
        shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStage.module = shader.Handle();
        shaderStage.pName = "main";
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStage;
        pipelineInfo.layout = m_GpuCapturedScenePrefilterPipelineLayout;
        if (vkCreateComputePipelines(
                device.Handle(),
                device.PipelineCacheHandle(),
                1u,
                &pipelineInfo,
                nullptr,
                &m_GpuCapturedScenePrefilterPipeline
            ) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create reflection capture prefilter pipeline");
        }
    } catch (...) {
        ReleaseGpuCapturedScenePrefilterResources();
        return false;
    }
    return true;
}

void VulkanReflectionProbeResources::ReleaseGpuCapturedScenePrefilterResources() {
    if (m_GpuCapturedScenePrefilterPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(
            m_GpuCapturedSceneDevice,
            m_GpuCapturedScenePrefilterPipeline,
            nullptr
        );
        m_GpuCapturedScenePrefilterPipeline = VK_NULL_HANDLE;
    }
    if (m_GpuCapturedScenePrefilterPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(
            m_GpuCapturedSceneDevice,
            m_GpuCapturedScenePrefilterPipelineLayout,
            nullptr
        );
        m_GpuCapturedScenePrefilterPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_GpuCapturedScenePrefilterDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(
            m_GpuCapturedSceneDevice,
            m_GpuCapturedScenePrefilterDescriptorPool,
            nullptr
        );
        m_GpuCapturedScenePrefilterDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_GpuCapturedScenePrefilterDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(
            m_GpuCapturedSceneDevice,
            m_GpuCapturedScenePrefilterDescriptorSetLayout,
            nullptr
        );
        m_GpuCapturedScenePrefilterDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_GpuCapturedScenePrefilterSampler != VK_NULL_HANDLE) {
        vkDestroySampler(
            m_GpuCapturedSceneDevice,
            m_GpuCapturedScenePrefilterSampler,
            nullptr
        );
        m_GpuCapturedScenePrefilterSampler = VK_NULL_HANDLE;
    }
}

bool VulkanReflectionProbeResources::EnsureGpuCapturedSceneDiffuseIrradianceResources(
    const VulkanDevice& device
) {
    if (m_GpuCapturedSceneDiffuseIrradiancePipeline != VK_NULL_HANDLE &&
        m_GpuCapturedSceneDiffuseIrradiancePipelineLayout != VK_NULL_HANDLE &&
        m_GpuCapturedSceneDiffuseIrradianceDescriptorSetLayout != VK_NULL_HANDLE &&
        m_GpuCapturedSceneDiffuseIrradianceDescriptorPool != VK_NULL_HANDLE &&
        m_GpuCapturedScenePrefilterSampler != VK_NULL_HANDLE) {
        return true;
    }

    try {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0u;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1u;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding = 1u;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1u;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<u32>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(
                device.Handle(),
                &layoutInfo,
                nullptr,
                &m_GpuCapturedSceneDiffuseIrradianceDescriptorSetLayout
            ) != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to create reflection capture diffuse irradiance descriptor layout"
            );
        }

        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            static_cast<u32>(kMaxCapturedSceneProbeResourceCount)
        };
        poolSizes[1] = {
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            static_cast<u32>(kMaxCapturedSceneProbeResourceCount)
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<u32>(kMaxCapturedSceneProbeResourceCount);
        if (vkCreateDescriptorPool(
                device.Handle(),
                &poolInfo,
                nullptr,
                &m_GpuCapturedSceneDiffuseIrradianceDescriptorPool
            ) != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to create reflection capture diffuse irradiance descriptor pool"
            );
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0u;
        pushRange.size = sizeof(u32) * 8u;
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1u;
        pipelineLayoutInfo.pSetLayouts =
            &m_GpuCapturedSceneDiffuseIrradianceDescriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1u;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(
                device.Handle(),
                &pipelineLayoutInfo,
                nullptr,
                &m_GpuCapturedSceneDiffuseIrradiancePipelineLayout
            ) != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to create reflection capture diffuse irradiance pipeline layout"
            );
        }

        const VulkanShaderModule shader(
            device,
            std::string(SE_SHADER_DIR) +
                "/reflection_probe_diffuse_irradiance.comp.spv"
        );
        VkPipelineShaderStageCreateInfo shaderStage{};
        shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStage.module = shader.Handle();
        shaderStage.pName = "main";
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStage;
        pipelineInfo.layout = m_GpuCapturedSceneDiffuseIrradiancePipelineLayout;
        if (vkCreateComputePipelines(
                device.Handle(),
                device.PipelineCacheHandle(),
                1u,
                &pipelineInfo,
                nullptr,
                &m_GpuCapturedSceneDiffuseIrradiancePipeline
            ) != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to create reflection capture diffuse irradiance pipeline"
            );
        }
    } catch (...) {
        ReleaseGpuCapturedSceneDiffuseIrradianceResources();
        return false;
    }
    return true;
}

void VulkanReflectionProbeResources::ReleaseGpuCapturedSceneDiffuseIrradianceResources() {
    if (m_GpuCapturedSceneDiffuseIrradiancePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(
            m_GpuCapturedSceneDevice,
            m_GpuCapturedSceneDiffuseIrradiancePipeline,
            nullptr
        );
        m_GpuCapturedSceneDiffuseIrradiancePipeline = VK_NULL_HANDLE;
    }
    if (m_GpuCapturedSceneDiffuseIrradiancePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(
            m_GpuCapturedSceneDevice,
            m_GpuCapturedSceneDiffuseIrradiancePipelineLayout,
            nullptr
        );
        m_GpuCapturedSceneDiffuseIrradiancePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_GpuCapturedSceneDiffuseIrradianceDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(
            m_GpuCapturedSceneDevice,
            m_GpuCapturedSceneDiffuseIrradianceDescriptorPool,
            nullptr
        );
        m_GpuCapturedSceneDiffuseIrradianceDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_GpuCapturedSceneDiffuseIrradianceDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(
            m_GpuCapturedSceneDevice,
            m_GpuCapturedSceneDiffuseIrradianceDescriptorSetLayout,
            nullptr
        );
        m_GpuCapturedSceneDiffuseIrradianceDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

bool VulkanReflectionProbeResources::EnsureGpuCapturedSceneResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    i32 probeSceneIndex,
    CapturedReflectionProbeFilteringSettings filteringSettings
) {
    CapturedSceneProbeResource* resource =
        FindOrCreateCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr) {
        return false;
    }
    if (m_GpuCapturedSceneDevice != VK_NULL_HANDLE &&
        m_GpuCapturedSceneDevice != device.Handle()) {
        return false;
    }
    const u32 requestedFaceSize = std::clamp(
        filteringSettings.faceSize,
        128u,
        1024u
    );
    if (!m_CapturedSceneCubemapFaceSizeInitialized) {
        m_CapturedSceneCubemapFaceSize = requestedFaceSize;
        m_CapturedSceneCubemapFaceSizeInitialized = true;
    } else if (m_CapturedSceneCubemapFaceSize != requestedFaceSize) {
        // Capture resolution is process-scoped because the render pass and
        // descriptor pools are shared by every captured probe.
        return false;
    }
    if (resource->targetImage != nullptr &&
        resource->sourceRadianceImage != nullptr &&
        resource->depthImage != nullptr &&
        m_GpuCapturedSceneRenderPass != VK_NULL_HANDLE &&
        resource->framebuffers.size() == 6u) {
        return true;
    }

    ReleaseGpuCapturedSceneResources(*resource);

    try {
        const VkExtent2D extent{
            m_CapturedSceneCubemapFaceSize,
            m_CapturedSceneCubemapFaceSize
        };
        const u32 mipCount = MipCountForExtent(extent.width);
        const VkFormat depthFormat = VulkanDepthBuffer::FindDepthFormat(physicalDevice);
        m_GpuCapturedSceneDevice = device.Handle();
        if (!EnsureGpuCapturedScenePrefilterResources(device) ||
            !EnsureGpuCapturedSceneDiffuseIrradianceResources(device)) {
            throw std::runtime_error(
                "Failed to prepare GPU reflection capture filtering resources"
            );
        }
        if (m_GpuCapturedSceneRenderPass == VK_NULL_HANDLE) {
            m_GpuCapturedSceneRenderPass = CreateGpuCapturedSceneRenderPass(
                device,
                depthFormat
            );
        }
        resource->targetImage = std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            extent,
            kGpuCapturedSceneFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_STORAGE_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            mipCount,
            6u,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            VK_IMAGE_VIEW_TYPE_CUBE
        );
        resource->sourceRadianceImage = std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            extent,
            kGpuCapturedSceneFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            mipCount,
            6u,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            VK_IMAGE_VIEW_TYPE_CUBE
        );
        resource->targetDiffuseIrradianceImage = std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            VkExtent2D{
                kGpuCapturedSceneDiffuseIrradianceFaceSize,
                kGpuCapturedSceneDiffuseIrradianceFaceSize
            },
            kGpuCapturedSceneFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1u,
            6u,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            VK_IMAGE_VIEW_TYPE_CUBE
        );
        resource->depthImage = std::make_unique<VulkanImage>(
            device,
            physicalDevice,
            extent,
            depthFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );
        resource->prefilterSourceView = CreateGpuCapturedSceneCubeView(
            device,
            resource->sourceRadianceImage->Handle(),
            resource->sourceRadianceImage->MipLevels()
        );
        resource->diffuseIrradianceArrayView = CreateGpuCapturedSceneMipArrayView(
            device,
            resource->targetDiffuseIrradianceImage->Handle(),
            0u
        );
        resource->prefilterMipViews.reserve(mipCount - 1u);
        for (u32 mip = 1u; mip < mipCount; ++mip) {
            resource->prefilterMipViews.push_back(
                CreateGpuCapturedSceneMipArrayView(
                    device,
                    resource->targetImage->Handle(),
                    mip
                )
            );
        }
        if (resource->prefilterDescriptorSets.empty()) {
            std::vector<VkDescriptorSetLayout> layouts(
                mipCount - 1u,
                m_GpuCapturedScenePrefilterDescriptorSetLayout
            );
            resource->prefilterDescriptorSets.resize(mipCount - 1u);
            VkDescriptorSetAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocateInfo.descriptorPool = m_GpuCapturedScenePrefilterDescriptorPool;
            allocateInfo.descriptorSetCount = static_cast<u32>(layouts.size());
            allocateInfo.pSetLayouts = layouts.data();
            if (vkAllocateDescriptorSets(
                    device.Handle(),
                    &allocateInfo,
                    resource->prefilterDescriptorSets.data()
                ) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate reflection capture prefilter descriptors");
            }
        }
        if (resource->prefilterDescriptorSets.size() != mipCount - 1u) {
            throw std::runtime_error("Reflection capture prefilter descriptor count mismatch");
        }
        for (u32 mip = 1u; mip < mipCount; ++mip) {
            VkDescriptorImageInfo sourceInfo{};
            sourceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sourceInfo.imageView = resource->prefilterSourceView;
            sourceInfo.sampler = m_GpuCapturedScenePrefilterSampler;
            VkDescriptorImageInfo destinationInfo{};
            destinationInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            destinationInfo.imageView = resource->prefilterMipViews[mip - 1u];
            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = resource->prefilterDescriptorSets[mip - 1u];
            writes[0].dstBinding = 0u;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].descriptorCount = 1u;
            writes[0].pImageInfo = &sourceInfo;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = resource->prefilterDescriptorSets[mip - 1u];
            writes[1].dstBinding = 1u;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].descriptorCount = 1u;
            writes[1].pImageInfo = &destinationInfo;
            vkUpdateDescriptorSets(
                device.Handle(),
                static_cast<u32>(writes.size()),
                writes.data(),
                0u,
                nullptr
            );
        }
        if (resource->diffuseIrradianceDescriptorSet == VK_NULL_HANDLE) {
            VkDescriptorSetAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocateInfo.descriptorPool =
                m_GpuCapturedSceneDiffuseIrradianceDescriptorPool;
            allocateInfo.descriptorSetCount = 1u;
            allocateInfo.pSetLayouts =
                &m_GpuCapturedSceneDiffuseIrradianceDescriptorSetLayout;
            if (vkAllocateDescriptorSets(
                    device.Handle(),
                    &allocateInfo,
                    &resource->diffuseIrradianceDescriptorSet
                ) != VK_SUCCESS) {
                throw std::runtime_error(
                    "Failed to allocate reflection capture diffuse irradiance descriptor"
                );
            }
        }
        VkDescriptorImageInfo sourceInfo{};
        sourceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sourceInfo.imageView = resource->prefilterSourceView;
        sourceInfo.sampler = m_GpuCapturedScenePrefilterSampler;
        VkDescriptorImageInfo destinationInfo{};
        destinationInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        destinationInfo.imageView = resource->diffuseIrradianceArrayView;
        std::array<VkWriteDescriptorSet, 2> diffuseWrites{};
        diffuseWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        diffuseWrites[0].dstSet = resource->diffuseIrradianceDescriptorSet;
        diffuseWrites[0].dstBinding = 0u;
        diffuseWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        diffuseWrites[0].descriptorCount = 1u;
        diffuseWrites[0].pImageInfo = &sourceInfo;
        diffuseWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        diffuseWrites[1].dstSet = resource->diffuseIrradianceDescriptorSet;
        diffuseWrites[1].dstBinding = 1u;
        diffuseWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        diffuseWrites[1].descriptorCount = 1u;
        diffuseWrites[1].pImageInfo = &destinationInfo;
        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(diffuseWrites.size()),
            diffuseWrites.data(),
            0u,
            nullptr
        );
        resource->faceViews.reserve(6u);
        resource->framebuffers.reserve(6u);
        for (u32 face = 0; face < 6u; ++face) {
            const VkImageView faceView = CreateGpuCapturedSceneFaceView(
                device,
                resource->sourceRadianceImage->Handle(),
                face
            );
            resource->faceViews.push_back(faceView);

            const VkImageView attachments[] = {
                faceView,
                resource->depthImage->View()
            };
            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = m_GpuCapturedSceneRenderPass;
            createInfo.attachmentCount = static_cast<u32>(std::size(attachments));
            createInfo.pAttachments = attachments;
            createInfo.width = extent.width;
            createInfo.height = extent.height;
            createInfo.layers = 1;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            if (vkCreateFramebuffer(
                    device.Handle(),
                    &createInfo,
                    nullptr,
                    &framebuffer
                ) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create GPU reflection capture framebuffer");
            }
            resource->framebuffers.push_back(framebuffer);
        }
    } catch (...) {
        ReleaseGpuCapturedSceneResources(*resource);
        return false;
    }
    return true;
}

bool VulkanReflectionProbeResources::CapturedSceneRefreshRequested(
    const CapturedSceneProbeResource& resource,
    const CapturedSceneRefreshRequest& refreshRequest,
    CapturedSceneCaptureAudit& audit
) const {
    const bool resourceReady =
        resource.activeImage != nullptr &&
        resource.activeImage->View() != VK_NULL_HANDLE;
    audit.resourceReady = resourceReady;
    audit.lastRefreshReason = resource.lastRefreshReason;
    audit.faceCount = 6u;
    audit.captureSignature = refreshRequest.captureSignature;
    audit.radianceSignature = refreshRequest.captureSignature;
    audit.membershipRevision = refreshRequest.membershipRevision;
    audit.lightRevision = refreshRequest.lightRevision;
    audit.renderRevision = refreshRequest.renderRevision;
    audit.schedulerFrame = refreshRequest.schedulerFrame;
    audit.lastRefreshCompletedFrame = resource.lastRefreshCompletedFrame;
    audit.localLightSignature = refreshRequest.localLightSignature;
    audit.geometrySignature = refreshRequest.geometrySignature;
    audit.affectedLocalLightCount = refreshRequest.affectedLocalLightCount;
    audit.affectedRenderableCount = refreshRequest.affectedRenderableCount;
    audit.localLightIdentityMask = refreshRequest.localLightIdentityMask;
    audit.geometryIdentityMask = refreshRequest.geometryIdentityMask;
    audit.localLightRegionMask = refreshRequest.localLightRegionMask;
    audit.geometryRegionMask = refreshRequest.geometryRegionMask;
    audit.refreshPriority = refreshRequest.refreshPriority;
    audit.minimumRefreshIntervalFrames = refreshRequest.minimumRefreshIntervalFrames;
    audit.refreshDeferredCount = resource.refreshDeferredCount;
    audit.selectiveInvalidationEnabled = refreshRequest.selectiveInvalidationEnabled;
    audit.gpuResourcesAllocated =
        resource.targetImage != nullptr &&
        resource.sourceRadianceImage != nullptr &&
        m_GpuCapturedSceneRenderPass != VK_NULL_HANDLE;
    audit.mipChainReady = resourceReady &&
        resource.activeBackend == CapturedSceneCaptureBackend::RasterizedGpu;
    audit.ggxPrefilterQuality = static_cast<u32>(
        resource.filteringSettings.quality
    );
    audit.ggxPrefilterSampleCount = CapturedReflectionProbeGgxSampleCount(
        resource.filteringSettings.quality
    );
    audit.ggxPrefilterFallbackActive =
        !CapturedReflectionProbeGgxPrefilterEnabled(
            resource.filteringSettings.quality
        );
    audit.ggxPrefilterReady = audit.mipChainReady &&
        !audit.ggxPrefilterFallbackActive;
    audit.diffuseIrradianceReady =
        resource.activeDiffuseIrradianceImage != nullptr &&
        resource.activeDiffuseIrradianceImage->View() != VK_NULL_HANDLE &&
        audit.mipChainReady;

    if (!resourceReady ||
        resource.activeBackend != CapturedSceneCaptureBackend::RasterizedGpu) {
        audit.refreshReason = CapturedSceneRefreshReason::Initial;
        audit.refreshRequested = true;
        return true;
    }

    const bool geometryChanged =
        refreshRequest.geometrySignature != resource.geometrySignature;
    const bool localLightsChanged =
        refreshRequest.localLightSignature != resource.localLightSignature;
    const bool membershipChanged =
        refreshRequest.membershipRevision != resource.membershipRevision;
    const bool renderChanged =
        refreshRequest.renderRevision != resource.renderRevision;
    const bool lightChanged =
        refreshRequest.lightRevision != resource.lightRevision;
    const bool globalGeometryChanged = membershipChanged || renderChanged;
    audit.localityIgnoredLightRevision =
        lightChanged &&
        refreshRequest.selectiveInvalidationEnabled &&
        !localLightsChanged;
    audit.localityIgnoredGeometryRevision =
        globalGeometryChanged &&
        refreshRequest.selectiveInvalidationEnabled &&
        !geometryChanged;
    audit.localLightDirty = lightChanged &&
        (!refreshRequest.selectiveInvalidationEnabled || localLightsChanged);
    audit.geometryDirty = globalGeometryChanged &&
        (!refreshRequest.selectiveInvalidationEnabled || geometryChanged);
    audit.dirtyLocalLightCount =
        audit.localLightDirty ? refreshRequest.affectedLocalLightCount : 0u;
    audit.dirtyRenderableCount =
        audit.geometryDirty ? refreshRequest.affectedRenderableCount : 0u;
    if (membershipChanged &&
        (!refreshRequest.selectiveInvalidationEnabled || geometryChanged)) {
        audit.dirtyMask |= CapturedSceneDirtyMembership;
    }
    if (lightChanged &&
        (!refreshRequest.selectiveInvalidationEnabled || localLightsChanged)) {
        audit.dirtyMask |= CapturedSceneDirtyLight;
    }
    if (renderChanged &&
        (!refreshRequest.selectiveInvalidationEnabled || geometryChanged)) {
        audit.dirtyMask |= CapturedSceneDirtyRender;
    }
    if (refreshRequest.captureSignature != resource.signature &&
        audit.dirtyMask == CapturedSceneDirtyNone) {
        audit.dirtyMask |= CapturedSceneDirtyContent;
    }
    if (refreshRequest.sceneDirtyOverride) {
        audit.dirtyMask |= CapturedSceneDirtyExternal;
    }

    if (refreshRequest.forceRefresh) {
        audit.refreshReason = CapturedSceneRefreshReason::Forced;
        audit.refreshRequested = true;
    } else if (refreshRequest.refreshPolicy ==
               RendererReflectionProbeRefreshPolicy::Forced) {
        audit.refreshReason = CapturedSceneRefreshReason::ForcedPolicy;
        audit.refreshRequested = true;
    } else if (refreshRequest.refreshPolicy ==
               RendererReflectionProbeRefreshPolicy::SceneDirty) {
        if ((audit.dirtyMask & CapturedSceneDirtyExternal) != 0u) {
            audit.refreshReason = CapturedSceneRefreshReason::SceneDirtyOverride;
        } else if ((audit.dirtyMask & CapturedSceneDirtyMembership) != 0u) {
            audit.refreshReason = CapturedSceneRefreshReason::MembershipChanged;
        } else if ((audit.dirtyMask & CapturedSceneDirtyLight) != 0u) {
            audit.refreshReason = CapturedSceneRefreshReason::LightChanged;
        } else if ((audit.dirtyMask & CapturedSceneDirtyRender) != 0u) {
            audit.refreshReason = CapturedSceneRefreshReason::RenderChanged;
        } else if ((audit.dirtyMask & CapturedSceneDirtyContent) != 0u) {
            audit.refreshReason = CapturedSceneRefreshReason::ContentChanged;
        }
        audit.refreshRequested = audit.dirtyMask != CapturedSceneDirtyNone;
    } else if (refreshRequest.refreshPolicy ==
               RendererReflectionProbeRefreshPolicy::FileSignature &&
               (audit.dirtyMask & CapturedSceneDirtyContent) != 0u) {
        audit.refreshReason = CapturedSceneRefreshReason::ContentChanged;
        audit.refreshRequested = true;
    }
    return audit.refreshRequested;
}

void VulkanReflectionProbeResources::BeginGpuCapturedSceneRefresh(
    CapturedSceneProbeResource& resource,
    const CapturedSceneRefreshRequest& refreshRequest
) {
    resource.captureInProgress = true;
    resource.nextFace = 0u;
    resource.facesRendered = 0u;
    resource.refreshRequest = refreshRequest;
    resource.audit.gpuCaptureInProgress = true;
    resource.audit.facesRendered = 0u;
    resource.audit.facesPending = 6u;
    resource.audit.capturePassCount = 0u;
    resource.audit.captureDrawCount = 0u;
    resource.audit.captureVisibleCount = 0u;
    resource.audit.captureCulledCount = 0u;
    resource.audit.captureFaceOrientationMask = 0u;
    resource.audit.captureFaceOrientationValid = false;
    resource.audit.mipGenerationCount = 0u;
    resource.audit.sourceMipGenerationCount = 0u;
    resource.audit.sourceMipCount = 0u;
    resource.audit.sourceMipMemoryBytes = 0u;
    resource.audit.sourceMipChainReady = false;
    resource.audit.ggxPrefilterSourceImageSeparated = false;
    resource.audit.ggxPrefilterPdfLodEnabled = false;
    resource.audit.ggxPrefilterDispatchCount = 0u;
    resource.audit.ggxPrefilterSampleCount = 0u;
    resource.audit.ggxPrefilterQuality = 0u;
    resource.audit.ggxPrefilterReady = false;
    resource.audit.ggxPrefilterFallbackActive = false;
    resource.audit.diffuseIrradianceDispatchCount = 0u;
    resource.audit.diffuseIrradianceSampleCount = 0u;
    resource.audit.diffuseIrradianceFaceSize = 0u;
    resource.audit.diffuseIrradianceReady = false;
    resource.audit.directionalShadowRequested = false;
    resource.audit.directionalShadowReady = false;
    resource.audit.directionalShadowPassCount = 0u;
    resource.audit.directionalShadowDrawCount = 0u;
    resource.audit.directionalShadowCasterCount = 0u;
    resource.audit.directionalShadowMapSize = 0u;
    resource.audit.directionalShadowFaceMask = 0u;
    resource.audit.directionalShadowProbeSceneIndex = -1;
    resource.audit.directionalShadowCameraIndependent = false;
    resource.audit.directionalShadowLocalTilesSuppressed = false;
    resource.audit.localShadowRequested = false;
    resource.audit.localShadowReady = false;
    resource.audit.localShadowPassCount = 0u;
    resource.audit.localShadowDrawCount = 0u;
    resource.audit.localShadowCasterCount = 0u;
    resource.audit.localShadowTileCount = 0u;
    resource.audit.localShadowPointFaceTileCount = 0u;
    resource.audit.localShadowSpotTileCount = 0u;
    resource.audit.localShadowRectTileCount = 0u;
    resource.audit.localShadowRequestedTileCount = 0u;
    resource.audit.localShadowDroppedTileCount = 0u;
    resource.audit.localShadowRectRequestedTileCount = 0u;
    resource.audit.localShadowRectMaximumTileCount = 0u;
    resource.audit.localShadowRectExtraSampleTileCount = 0u;
    resource.audit.localShadowRectBudgetLimitedSampleTileCount = 0u;
    resource.audit.localShadowRectDroppedTileCount = 0u;
    resource.audit.localShadowMapTileSize = 0u;
    resource.audit.localShadowFaceMask = 0u;
    resource.audit.localShadowSupportedKindMask = 0u;
    resource.audit.localShadowSuppressedKindMask = 0u;
    resource.audit.localShadowProbeSceneIndex = -1;
    resource.audit.localShadowCameraIndependent = false;
    resource.audit.shadowSnapshotBuildCount = 0u;
    resource.audit.shadowSnapshotReuseFaceCount = 0u;
    resource.audit.shadowSnapshotSavedDirectionalPassCount = 0u;
    resource.audit.shadowSnapshotSavedLocalTilePassCount = 0u;
    resource.audit.shadowSnapshotSavedLocalDrawCount = 0u;
    resource.audit.shadowSnapshotBuildFaceMask = 0u;
    resource.audit.shadowSnapshotReuseFaceMask = 0u;
    resource.audit.shadowSnapshotProbeSceneIndex = -1;
    resource.audit.shadowSnapshotPersistentCacheSlot = -1;
    resource.audit.shadowSnapshotPersistentHitCount = 0u;
    resource.audit.shadowSnapshotPersistentCacheResourceCount = 0u;
    resource.audit.shadowSnapshotPersistentCacheEvictionCount = 0u;
    resource.audit.shadowSnapshotInputSignature = 0u;
    resource.audit.shadowSnapshotReady = false;
    resource.audit.shadowSnapshotCameraIndependent = false;
    resource.audit.shadowSnapshotEnabled = false;
    resource.audit.shadowSnapshotFallbackActive = false;
    resource.audit.shadowSnapshotPersistentEnabled = false;
    resource.audit.shadowSnapshotPersistentHit = false;
    resource.audit.refreshDeferredByBudget = false;
    resource.audit.rasterizedGeometry = true;
    resource.audit.backend = CapturedSceneCaptureBackend::RasterizedGpu;
}

bool VulkanReflectionProbeResources::RequestGpuCapturedSceneRefresh(
    const CapturedSceneRefreshRequest& refreshRequest,
    i32 probeSceneIndex
) {
    CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr) {
        return false;
    }
    m_LastCapturedSceneProbeSceneIndex = probeSceneIndex;
    ++resource->refreshCheckCount;
    CapturedSceneCaptureAudit audit{};
    audit.backend = CapturedSceneCaptureBackend::RasterizedGpu;
    audit.rasterizedGeometry = true;
    audit.probeSceneIndex = probeSceneIndex;

    if (resource->captureInProgress) {
        // A six-face capture is a coherent snapshot. Changes arriving while it is
        // in flight schedule the next snapshot instead of repeatedly restarting it.
        audit = resource->audit;
        audit.backend = CapturedSceneCaptureBackend::RasterizedGpu;
        audit.rasterizedGeometry = true;
        audit.captureSignature = resource->refreshRequest.captureSignature;
        audit.radianceSignature = resource->refreshRequest.captureSignature;
        audit.membershipRevision = resource->refreshRequest.membershipRevision;
        audit.lightRevision = resource->refreshRequest.lightRevision;
        audit.renderRevision = resource->refreshRequest.renderRevision;
        audit.schedulerFrame = refreshRequest.schedulerFrame;
        audit.lastRefreshCompletedFrame = resource->lastRefreshCompletedFrame;
        audit.localLightSignature = resource->refreshRequest.localLightSignature;
        audit.geometrySignature = resource->refreshRequest.geometrySignature;
        audit.affectedLocalLightCount =
            resource->refreshRequest.affectedLocalLightCount;
        audit.affectedRenderableCount =
            resource->refreshRequest.affectedRenderableCount;
        audit.localLightIdentityMask =
            resource->refreshRequest.localLightIdentityMask;
        audit.geometryIdentityMask = resource->refreshRequest.geometryIdentityMask;
        audit.localLightRegionMask = resource->refreshRequest.localLightRegionMask;
        audit.geometryRegionMask = resource->refreshRequest.geometryRegionMask;
        audit.refreshPriority = resource->refreshRequest.refreshPriority;
        audit.minimumRefreshIntervalFrames =
            resource->refreshRequest.minimumRefreshIntervalFrames;
        audit.refreshDeferredCount = resource->refreshDeferredCount;
        audit.selectiveInvalidationEnabled =
            resource->refreshRequest.selectiveInvalidationEnabled;
        audit.probeSceneIndex = probeSceneIndex;
        audit.resourceReady = resource->activeImage != nullptr;
        audit.gpuResourcesAllocated = true;
        audit.gpuCaptureInProgress = true;
        audit.facesRendered = resource->facesRendered;
        audit.facesPending = 6u - resource->facesRendered;
        audit.refreshRequested = true;
        resource->audit = audit;
        return true;
    }

    const bool refreshRequested = CapturedSceneRefreshRequested(
        *resource,
        refreshRequest,
        audit
    );
    const bool budgetDeferrable = refreshRequested &&
        !refreshRequest.forceRefresh &&
        refreshRequest.refreshPolicy !=
            RendererReflectionProbeRefreshPolicy::Forced &&
        !refreshRequest.sceneDirtyOverride &&
        resource->lastRefreshCompletedFrame > 0u &&
        refreshRequest.minimumRefreshIntervalFrames > 0u &&
        refreshRequest.schedulerFrame >= resource->lastRefreshCompletedFrame &&
        refreshRequest.schedulerFrame - resource->lastRefreshCompletedFrame <
            refreshRequest.minimumRefreshIntervalFrames;
    if (budgetDeferrable) {
        audit.refreshRequested = false;
        audit.refreshDeferredByBudget = true;
        ++resource->refreshDeferredCount;
        audit.refreshDeferredCount = resource->refreshDeferredCount;
    }

    const CapturedSceneCaptureAudit previousAudit = resource->audit;
    if (!audit.refreshRequested) {
        audit.backend = CapturedSceneCaptureBackend::RasterizedGpu;
        audit.rasterizedGeometry = true;
        audit.facesRendered = previousAudit.facesRendered;
        audit.facesPending = previousAudit.facesPending;
        audit.capturePassCount = previousAudit.capturePassCount;
        audit.captureDrawCount = previousAudit.captureDrawCount;
        audit.captureVisibleCount = previousAudit.captureVisibleCount;
        audit.captureCulledCount = previousAudit.captureCulledCount;
        audit.captureFaceOrientationMask =
            previousAudit.captureFaceOrientationMask;
        audit.captureFaceOrientationValid =
            previousAudit.captureFaceOrientationValid;
        audit.mipGenerationCount = previousAudit.mipGenerationCount;
        audit.sourceMipGenerationCount =
            previousAudit.sourceMipGenerationCount;
        audit.sourceMipCount = previousAudit.sourceMipCount;
        audit.sourceMipMemoryBytes = previousAudit.sourceMipMemoryBytes;
        audit.sourceMipChainReady = previousAudit.sourceMipChainReady;
        audit.ggxPrefilterSourceImageSeparated =
            previousAudit.ggxPrefilterSourceImageSeparated;
        audit.ggxPrefilterPdfLodEnabled =
            previousAudit.ggxPrefilterPdfLodEnabled;
        audit.ggxPrefilterDispatchCount =
            previousAudit.ggxPrefilterDispatchCount;
        audit.ggxPrefilterSampleCount = previousAudit.ggxPrefilterSampleCount;
        audit.ggxPrefilterQuality = previousAudit.ggxPrefilterQuality;
        audit.ggxPrefilterReady = previousAudit.ggxPrefilterReady;
        audit.ggxPrefilterFallbackActive =
            previousAudit.ggxPrefilterFallbackActive;
        audit.diffuseIrradianceDispatchCount =
            previousAudit.diffuseIrradianceDispatchCount;
        audit.diffuseIrradianceSampleCount =
            previousAudit.diffuseIrradianceSampleCount;
        audit.diffuseIrradianceFaceSize = previousAudit.diffuseIrradianceFaceSize;
        audit.diffuseIrradianceReady = previousAudit.diffuseIrradianceReady;
        audit.directionalShadowRequested = previousAudit.directionalShadowRequested;
        audit.directionalShadowReady = previousAudit.directionalShadowReady;
        audit.directionalShadowPassCount = previousAudit.directionalShadowPassCount;
        audit.directionalShadowDrawCount = previousAudit.directionalShadowDrawCount;
        audit.directionalShadowCasterCount = previousAudit.directionalShadowCasterCount;
        audit.directionalShadowMapSize = previousAudit.directionalShadowMapSize;
        audit.directionalShadowFaceMask = previousAudit.directionalShadowFaceMask;
        audit.directionalShadowProbeSceneIndex =
            previousAudit.directionalShadowProbeSceneIndex;
        audit.directionalShadowCameraIndependent =
            previousAudit.directionalShadowCameraIndependent;
        audit.directionalShadowLocalTilesSuppressed =
            previousAudit.directionalShadowLocalTilesSuppressed;
        audit.localShadowRequested = previousAudit.localShadowRequested;
        audit.localShadowReady = previousAudit.localShadowReady;
        audit.localShadowPassCount = previousAudit.localShadowPassCount;
        audit.localShadowDrawCount = previousAudit.localShadowDrawCount;
        audit.localShadowCasterCount = previousAudit.localShadowCasterCount;
        audit.localShadowTileCount = previousAudit.localShadowTileCount;
        audit.localShadowPointFaceTileCount =
            previousAudit.localShadowPointFaceTileCount;
        audit.localShadowSpotTileCount = previousAudit.localShadowSpotTileCount;
        audit.localShadowRectTileCount = previousAudit.localShadowRectTileCount;
        audit.localShadowRequestedTileCount =
            previousAudit.localShadowRequestedTileCount;
        audit.localShadowDroppedTileCount =
            previousAudit.localShadowDroppedTileCount;
        audit.localShadowRectRequestedTileCount =
            previousAudit.localShadowRectRequestedTileCount;
        audit.localShadowRectMaximumTileCount =
            previousAudit.localShadowRectMaximumTileCount;
        audit.localShadowRectExtraSampleTileCount =
            previousAudit.localShadowRectExtraSampleTileCount;
        audit.localShadowRectBudgetLimitedSampleTileCount =
            previousAudit.localShadowRectBudgetLimitedSampleTileCount;
        audit.localShadowRectDroppedTileCount =
            previousAudit.localShadowRectDroppedTileCount;
        audit.localShadowMapTileSize = previousAudit.localShadowMapTileSize;
        audit.localShadowFaceMask = previousAudit.localShadowFaceMask;
        audit.localShadowSupportedKindMask =
            previousAudit.localShadowSupportedKindMask;
        audit.localShadowSuppressedKindMask =
            previousAudit.localShadowSuppressedKindMask;
        audit.localShadowProbeSceneIndex = previousAudit.localShadowProbeSceneIndex;
        audit.localShadowCameraIndependent =
            previousAudit.localShadowCameraIndependent;
        audit.shadowSnapshotBuildCount = previousAudit.shadowSnapshotBuildCount;
        audit.shadowSnapshotReuseFaceCount =
            previousAudit.shadowSnapshotReuseFaceCount;
        audit.shadowSnapshotSavedDirectionalPassCount =
            previousAudit.shadowSnapshotSavedDirectionalPassCount;
        audit.shadowSnapshotSavedLocalTilePassCount =
            previousAudit.shadowSnapshotSavedLocalTilePassCount;
        audit.shadowSnapshotSavedLocalDrawCount =
            previousAudit.shadowSnapshotSavedLocalDrawCount;
        audit.shadowSnapshotBuildFaceMask =
            previousAudit.shadowSnapshotBuildFaceMask;
        audit.shadowSnapshotReuseFaceMask =
            previousAudit.shadowSnapshotReuseFaceMask;
        audit.shadowSnapshotProbeSceneIndex =
            previousAudit.shadowSnapshotProbeSceneIndex;
        audit.shadowSnapshotPersistentCacheSlot =
            previousAudit.shadowSnapshotPersistentCacheSlot;
        audit.shadowSnapshotPersistentHitCount =
            previousAudit.shadowSnapshotPersistentHitCount;
        audit.shadowSnapshotPersistentCacheResourceCount =
            previousAudit.shadowSnapshotPersistentCacheResourceCount;
        audit.shadowSnapshotPersistentCacheEvictionCount =
            previousAudit.shadowSnapshotPersistentCacheEvictionCount;
        audit.shadowSnapshotInputSignature =
            previousAudit.shadowSnapshotInputSignature;
        audit.shadowSnapshotReady = previousAudit.shadowSnapshotReady;
        audit.shadowSnapshotCameraIndependent =
            previousAudit.shadowSnapshotCameraIndependent;
        audit.shadowSnapshotEnabled = previousAudit.shadowSnapshotEnabled;
        audit.shadowSnapshotFallbackActive =
            previousAudit.shadowSnapshotFallbackActive;
        audit.shadowSnapshotPersistentEnabled =
            previousAudit.shadowSnapshotPersistentEnabled;
        audit.shadowSnapshotPersistentHit =
            previousAudit.shadowSnapshotPersistentHit;
        audit.lastCapturedFace = previousAudit.lastCapturedFace;
        audit.probeSceneIndex = probeSceneIndex;
        resource->audit = audit;
        return false;
    }

    resource->audit = audit;
    BeginGpuCapturedSceneRefresh(*resource, refreshRequest);
    return true;
}

bool VulkanReflectionProbeResources::GpuCapturedSceneRefreshPending(
    i32 probeSceneIndex
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr && resource->captureInProgress &&
        resource->targetImage != nullptr && resource->nextFace < 6u;
}

u32 VulkanReflectionProbeResources::GpuCapturedSceneNextFace(
    i32 probeSceneIndex
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr ? resource->nextFace : 0u;
}

VkRenderPass VulkanReflectionProbeResources::GpuCapturedSceneRenderPass() const {
    return m_GpuCapturedSceneRenderPass;
}

VkExtent2D VulkanReflectionProbeResources::GpuCapturedSceneExtent() const {
    return m_GpuCapturedSceneRenderPass != VK_NULL_HANDLE
        ? VkExtent2D{
            m_CapturedSceneCubemapFaceSize,
            m_CapturedSceneCubemapFaceSize
        }
        : VkExtent2D{};
}

VkFramebuffer VulkanReflectionProbeResources::GpuCapturedSceneFramebuffer(
    i32 probeSceneIndex,
    u32 face
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr && face < resource->framebuffers.size()
        ? resource->framebuffers[face]
        : VK_NULL_HANDLE;
}

VkExtent2D VulkanReflectionProbeResources::GpuCapturedSceneExtent(
    i32 probeSceneIndex
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr && resource->targetImage != nullptr
        ? resource->targetImage->Extent()
        : VkExtent2D{};
}

void VulkanReflectionProbeResources::RecordGpuCapturedSceneMipGeneration(
    i32 probeSceneIndex,
    VkCommandBuffer commandBuffer,
    CapturedReflectionProbeFilteringSettings filteringSettings
) {
    CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr || resource->targetImage == nullptr ||
        resource->sourceRadianceImage == nullptr ||
        commandBuffer == VK_NULL_HANDLE) {
        return;
    }

    const VkImage sourceImage = resource->sourceRadianceImage->Handle();
    const VkImage destinationImage = resource->targetImage->Handle();
    const u32 mipCount = resource->targetImage->MipLevels();
    if (m_GpuCapturedScenePrefilterPipeline == VK_NULL_HANDLE ||
        resource->sourceRadianceImage->MipLevels() != mipCount ||
        resource->prefilterDescriptorSets.size() != mipCount - 1u) {
        return;
    }

    resource->filteringSettings = filteringSettings;
    const bool ggxPrefilterEnabled = CapturedReflectionProbeGgxPrefilterEnabled(
        filteringSettings.quality
    );
    const u32 sampleCount = CapturedReflectionProbeGgxSampleCount(
        filteringSettings.quality
    );

    RecordImageBarrier(
        commandBuffer,
        destinationImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0u,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u,
        1u,
        0u,
        6u
    );
    VkImageCopy baseCopy{};
    baseCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    baseCopy.srcSubresource.mipLevel = 0u;
    baseCopy.srcSubresource.baseArrayLayer = 0u;
    baseCopy.srcSubresource.layerCount = 6u;
    baseCopy.dstSubresource = baseCopy.srcSubresource;
    baseCopy.extent = {
        m_CapturedSceneCubemapFaceSize,
        m_CapturedSceneCubemapFaceSize,
        1u
    };
    vkCmdCopyImage(
        commandBuffer,
        sourceImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destinationImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1u,
        &baseCopy
    );
    RecordImageBarrier(
        commandBuffer,
        destinationImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u,
        1u,
        0u,
        6u
    );

    RecordImageBarrier(
        commandBuffer,
        sourceImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0u,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1u,
        mipCount - 1u,
        0u,
        6u
    );
    for (u32 mip = 1u; mip < mipCount; ++mip) {
        const i32 sourceExtent = static_cast<i32>(
            std::max(1u, m_CapturedSceneCubemapFaceSize >> (mip - 1u))
        );
        const i32 destinationExtent = static_cast<i32>(
            std::max(1u, m_CapturedSceneCubemapFaceSize >> mip)
        );
        VkImageBlit blit{};
        blit.srcOffsets[1] = { sourceExtent, sourceExtent, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = mip - 1u;
        blit.srcSubresource.baseArrayLayer = 0u;
        blit.srcSubresource.layerCount = 6u;
        blit.dstOffsets[1] = { destinationExtent, destinationExtent, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = mip;
        blit.dstSubresource.baseArrayLayer = 0u;
        blit.dstSubresource.layerCount = 6u;
        vkCmdBlitImage(
            commandBuffer,
            sourceImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            sourceImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1u,
            &blit,
            VK_FILTER_LINEAR
        );
        RecordImageBarrier(
            commandBuffer,
            sourceImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            mip,
            1u,
            0u,
            6u
        );
    }

    if (!ggxPrefilterEnabled) {
        RecordImageBarrier(
            commandBuffer,
            destinationImage,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0u,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1u,
            mipCount - 1u,
            0u,
            6u
        );
        for (u32 mip = 1u; mip < mipCount; ++mip) {
            VkImageCopy mipCopy{};
            mipCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mipCopy.srcSubresource.mipLevel = mip;
            mipCopy.srcSubresource.baseArrayLayer = 0u;
            mipCopy.srcSubresource.layerCount = 6u;
            mipCopy.dstSubresource = mipCopy.srcSubresource;
            mipCopy.extent = {
                std::max(1u, m_CapturedSceneCubemapFaceSize >> mip),
                std::max(1u, m_CapturedSceneCubemapFaceSize >> mip),
                1u
            };
            vkCmdCopyImage(
                commandBuffer,
                sourceImage,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                destinationImage,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1u,
                &mipCopy
            );
        }
        RecordImageBarrier(
            commandBuffer,
            destinationImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1u,
            mipCount - 1u,
            0u,
            6u
        );
    } else {
        RecordImageBarrier(
            commandBuffer,
            destinationImage,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            0u,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1u,
            mipCount - 1u,
            0u,
            6u
        );
    }

    RecordImageBarrier(
        commandBuffer,
        sourceImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u,
        mipCount,
        0u,
        6u
    );

    if (!ggxPrefilterEnabled) {
        return;
    }

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_GpuCapturedScenePrefilterPipeline
    );
    for (u32 mip = 1u; mip < mipCount; ++mip) {
        struct PrefilterPushConstants {
            f32 roughness = 0.0f;
            u32 mipExtent = 1u;
            u32 sampleCount = 1u;
            u32 sourceFaceSize = 1u;
            u32 sourceMipCount = 1u;
            u32 pdfLodEnabled = 1u;
            u32 reserved0 = 0u;
            u32 reserved1 = 0u;
        } constants;
        constants.roughness = static_cast<f32>(mip) /
            static_cast<f32>(std::max(1u, mipCount - 1u));
        constants.mipExtent = std::max(1u, m_CapturedSceneCubemapFaceSize >> mip);
        constants.sampleCount = sampleCount;
        constants.sourceFaceSize = m_CapturedSceneCubemapFaceSize;
        constants.sourceMipCount = mipCount;
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            m_GpuCapturedScenePrefilterPipelineLayout,
            0u,
            1u,
            &resource->prefilterDescriptorSets[mip - 1u],
            0u,
            nullptr
        );
        vkCmdPushConstants(
            commandBuffer,
            m_GpuCapturedScenePrefilterPipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0u,
            sizeof(PrefilterPushConstants),
            &constants
        );
        vkCmdDispatch(
            commandBuffer,
            (constants.mipExtent + 7u) / 8u,
            (constants.mipExtent + 7u) / 8u,
            6u
        );
    }
    RecordImageBarrier(
        commandBuffer,
        destinationImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1u,
        mipCount - 1u,
        0u,
        6u
    );
}

void VulkanReflectionProbeResources::RecordGpuCapturedSceneFaceOrientation(
    i32 probeSceneIndex,
    u32 face,
    bool valid
) {
    CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr || !resource->captureInProgress ||
        face != resource->nextFace || face >= 6u) {
        return;
    }

    if (valid) {
        resource->audit.captureFaceOrientationMask |= 1u << face;
    }
}

void VulkanReflectionProbeResources::RecordGpuCapturedSceneDiffuseIrradiance(
    i32 probeSceneIndex,
    VkCommandBuffer commandBuffer
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr || resource->targetImage == nullptr ||
        resource->sourceRadianceImage == nullptr ||
        resource->targetDiffuseIrradianceImage == nullptr ||
        resource->diffuseIrradianceDescriptorSet == VK_NULL_HANDLE ||
        commandBuffer == VK_NULL_HANDLE ||
        m_GpuCapturedSceneDiffuseIrradiancePipeline == VK_NULL_HANDLE) {
        return;
    }

    RecordImageBarrier(
        commandBuffer,
        resource->targetDiffuseIrradianceImage->Handle(),
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        0u,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u,
        1u,
        0u,
        6u
    );
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_GpuCapturedSceneDiffuseIrradiancePipeline
    );
    struct DiffuseIrradiancePushConstants {
        u32 faceSize = kGpuCapturedSceneDiffuseIrradianceFaceSize;
        u32 sampleCount = kGpuCapturedSceneDiffuseIrradianceSampleCount;
        u32 sourceFaceSize = 1u;
        u32 sourceMipCount = 1u;
        u32 pdfLodEnabled = 1u;
        u32 reserved0 = 0u;
        u32 reserved1 = 0u;
        u32 reserved2 = 0u;
    } constants;
    constants.sourceFaceSize = m_CapturedSceneCubemapFaceSize;
    constants.sourceMipCount = resource->sourceRadianceImage->MipLevels();
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_GpuCapturedSceneDiffuseIrradiancePipelineLayout,
        0u,
        1u,
        &resource->diffuseIrradianceDescriptorSet,
        0u,
        nullptr
    );
    vkCmdPushConstants(
        commandBuffer,
        m_GpuCapturedSceneDiffuseIrradiancePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0u,
        sizeof(DiffuseIrradiancePushConstants),
        &constants
    );
    vkCmdDispatch(
        commandBuffer,
        (constants.faceSize + 7u) / 8u,
        (constants.faceSize + 7u) / 8u,
        6u
    );
    RecordImageBarrier(
        commandBuffer,
        resource->targetDiffuseIrradianceImage->Handle(),
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u,
        1u,
        0u,
        6u
    );
}

void VulkanReflectionProbeResources::RecordGpuCapturedSceneDirectionalShadow(
    i32 probeSceneIndex,
    u32 face,
    u32 mapSize,
    u32 passCount,
    u32 drawCount,
    u32 casterCount,
    bool requested,
    bool ready,
    bool cameraIndependent,
    bool localTilesSuppressed
) {
    CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr || !resource->captureInProgress ||
        face != resource->nextFace || face >= 6u) {
        return;
    }

    CapturedSceneCaptureAudit& audit = resource->audit;
    audit.directionalShadowRequested =
        audit.directionalShadowRequested || requested;
    audit.directionalShadowPassCount += passCount;
    audit.directionalShadowDrawCount += drawCount;
    audit.directionalShadowCasterCount += casterCount;
    audit.directionalShadowMapSize = std::max(
        audit.directionalShadowMapSize,
        mapSize
    );
    audit.directionalShadowFaceMask |= ready ? (1u << face) : 0u;
    audit.directionalShadowProbeSceneIndex = probeSceneIndex;
    audit.directionalShadowCameraIndependent =
        audit.directionalShadowCameraIndependent || cameraIndependent;
    audit.directionalShadowLocalTilesSuppressed =
        audit.directionalShadowLocalTilesSuppressed || localTilesSuppressed;
    audit.directionalShadowReady = audit.directionalShadowRequested &&
        audit.directionalShadowFaceMask == 0x3fu &&
        audit.directionalShadowPassCount >= 1u &&
        audit.directionalShadowDrawCount > 0u &&
        audit.directionalShadowCameraIndependent &&
        audit.directionalShadowLocalTilesSuppressed;
}

void VulkanReflectionProbeResources::RecordGpuCapturedSceneLocalShadow(
    i32 probeSceneIndex,
    u32 face,
    u32 mapTileSize,
    u32 passCount,
    u32 drawCount,
    u32 casterCount,
    u32 tileCount,
    u32 pointFaceTileCount,
    u32 spotTileCount,
    u32 rectTileCount,
    u32 requestedTileCount,
    u32 droppedTileCount,
    u32 rectRequestedTileCount,
    u32 rectMaximumTileCount,
    u32 rectExtraSampleTileCount,
    u32 rectBudgetLimitedSampleTileCount,
    u32 rectDroppedTileCount,
    bool requested,
    bool ready,
    bool cameraIndependent,
    u32 supportedKindMask,
    u32 suppressedKindMask
) {
    CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr || !resource->captureInProgress ||
        face != resource->nextFace || face >= 6u) {
        return;
    }

    CapturedSceneCaptureAudit& audit = resource->audit;
    audit.localShadowRequested = audit.localShadowRequested || requested;
    audit.localShadowPassCount += passCount;
    audit.localShadowDrawCount += drawCount;
    audit.localShadowCasterCount += casterCount;
    audit.localShadowTileCount += tileCount;
    audit.localShadowPointFaceTileCount += pointFaceTileCount;
    audit.localShadowSpotTileCount += spotTileCount;
    audit.localShadowRectTileCount += rectTileCount;
    audit.localShadowRequestedTileCount += requestedTileCount;
    audit.localShadowDroppedTileCount += droppedTileCount;
    audit.localShadowRectRequestedTileCount += rectRequestedTileCount;
    audit.localShadowRectMaximumTileCount += rectMaximumTileCount;
    audit.localShadowRectExtraSampleTileCount += rectExtraSampleTileCount;
    audit.localShadowRectBudgetLimitedSampleTileCount +=
        rectBudgetLimitedSampleTileCount;
    audit.localShadowRectDroppedTileCount += rectDroppedTileCount;
    audit.localShadowMapTileSize = std::max(audit.localShadowMapTileSize, mapTileSize);
    audit.localShadowFaceMask |= ready ? (1u << face) : 0u;
    audit.localShadowSupportedKindMask |= supportedKindMask;
    audit.localShadowSuppressedKindMask |= suppressedKindMask;
    audit.localShadowProbeSceneIndex = probeSceneIndex;
    audit.localShadowCameraIndependent =
        audit.localShadowCameraIndependent || cameraIndependent;
    audit.localShadowReady = audit.localShadowRequested &&
        audit.localShadowFaceMask == 0x3fu &&
        audit.localShadowPassCount >= 1u &&
        audit.localShadowDrawCount > 0u &&
        audit.localShadowTileCount > 0u &&
        audit.localShadowCameraIndependent &&
        (audit.localShadowSupportedKindMask & 0x3u) == 0x3u &&
        (audit.localShadowSupportedKindMask |
         audit.localShadowSuppressedKindMask) == 0x7u &&
        (audit.localShadowSupportedKindMask &
         audit.localShadowSuppressedKindMask) == 0u;
}

void VulkanReflectionProbeResources::RecordGpuCapturedSceneShadowSnapshot(
    i32 probeSceneIndex,
    u32 face,
    bool built,
    u32 savedDirectionalPassCount,
    u32 savedLocalTilePassCount,
    u32 savedLocalDrawCount,
    bool ready,
    bool cameraIndependent,
    bool enabled,
    bool persistentEnabled,
    bool persistentHit,
    i32 persistentCacheSlot,
    u32 persistentCacheResourceCount,
    u32 persistentCacheEvictionCount,
    u32 inputSignature
) {
    CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr || !resource->captureInProgress ||
        face != resource->nextFace || face >= 6u) {
        return;
    }

    CapturedSceneCaptureAudit& audit = resource->audit;
    audit.shadowSnapshotEnabled = enabled;
    audit.shadowSnapshotFallbackActive = !enabled;
    audit.shadowSnapshotPersistentEnabled = persistentEnabled;
    audit.shadowSnapshotPersistentHit =
        audit.shadowSnapshotPersistentHit || persistentHit;
    audit.shadowSnapshotPersistentHitCount += persistentHit ? 1u : 0u;
    audit.shadowSnapshotPersistentCacheSlot = persistentCacheSlot;
    audit.shadowSnapshotPersistentCacheResourceCount =
        persistentCacheResourceCount;
    audit.shadowSnapshotPersistentCacheEvictionCount =
        persistentCacheEvictionCount;
    audit.shadowSnapshotInputSignature = inputSignature;
    if (!enabled) {
        audit.shadowSnapshotReady = false;
        return;
    }
    audit.shadowSnapshotBuildCount += built ? 1u : 0u;
    audit.shadowSnapshotReuseFaceCount += built ? 0u : 1u;
    audit.shadowSnapshotSavedDirectionalPassCount += savedDirectionalPassCount;
    audit.shadowSnapshotSavedLocalTilePassCount += savedLocalTilePassCount;
    audit.shadowSnapshotSavedLocalDrawCount += savedLocalDrawCount;
    audit.shadowSnapshotBuildFaceMask |= built ? (1u << face) : 0u;
    audit.shadowSnapshotReuseFaceMask |= built ? 0u : (1u << face);
    audit.shadowSnapshotProbeSceneIndex = probeSceneIndex;
    audit.shadowSnapshotCameraIndependent =
        audit.shadowSnapshotCameraIndependent || cameraIndependent;
    audit.shadowSnapshotReady = ready &&
        (audit.shadowSnapshotBuildCount == 1u ||
            audit.shadowSnapshotPersistentHit) &&
        audit.shadowSnapshotReuseFaceCount + audit.shadowSnapshotBuildCount >= 6u &&
        audit.shadowSnapshotCameraIndependent;
}

void VulkanReflectionProbeResources::CompleteGpuCapturedSceneFace(
    i32 probeSceneIndex,
    u32 face,
    u32 drawCount,
    u32 visibleCount,
    u32 culledCount,
    u32 selfCaptureExcludedCount,
    bool captureComplete,
    u64 schedulerFrame
) {
    CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr || !resource->captureInProgress ||
        face != resource->nextFace) {
        return;
    }

    ++resource->facesRendered;
    ++resource->nextFace;
    resource->audit.lastCapturedFace = face;
    ++resource->audit.capturePassCount;
    resource->audit.captureDrawCount += drawCount;
    resource->audit.captureVisibleCount += visibleCount;
    resource->audit.captureCulledCount += culledCount;
    resource->audit.selfCaptureExcludedCount = selfCaptureExcludedCount;
    resource->audit.facesRendered = resource->facesRendered;
    resource->audit.facesPending = 6u - resource->facesRendered;
    resource->audit.captureFaceOrientationValid =
        resource->audit.captureFaceOrientationMask == 0x3fu;

    if (!captureComplete || resource->facesRendered != 6u) {
        return;
    }

    const u32 completedSourceMipCount = resource->sourceRadianceImage != nullptr
        ? resource->sourceRadianceImage->MipLevels()
        : 0u;
    const bool sourceImageSeparated = resource->sourceRadianceImage != nullptr &&
        resource->targetImage != nullptr &&
        resource->sourceRadianceImage->Handle() != resource->targetImage->Handle();
    ReleaseGpuCapturedSceneAttachments(*resource);
    resource->sourceRadianceImage.reset();
    resource->activeImage = std::move(resource->targetImage);
    resource->activeDiffuseIrradianceImage =
        std::move(resource->targetDiffuseIrradianceImage);
    resource->depthImage.reset();
    resource->captureInProgress = false;
    resource->nextFace = 0u;
    resource->facesRendered = 0u;
    resource->signature = resource->refreshRequest.captureSignature;
    resource->radianceSignature = resource->refreshRequest.captureSignature;
    resource->membershipRevision = resource->refreshRequest.membershipRevision;
    resource->lightRevision = resource->refreshRequest.lightRevision;
    resource->renderRevision = resource->refreshRequest.renderRevision;
    resource->localLightSignature = resource->refreshRequest.localLightSignature;
    resource->geometrySignature = resource->refreshRequest.geometrySignature;
    resource->localLightIdentityMask =
        resource->refreshRequest.localLightIdentityMask;
    resource->geometryIdentityMask = resource->refreshRequest.geometryIdentityMask;
    resource->localLightRegionMask = resource->refreshRequest.localLightRegionMask;
    resource->geometryRegionMask = resource->refreshRequest.geometryRegionMask;
    resource->lastRefreshCompletedFrame = schedulerFrame;
    resource->activeBackend = CapturedSceneCaptureBackend::RasterizedGpu;
    ++resource->uploadCount;
    resource->lastRefreshReason = resource->audit.refreshReason;
    resource->audit.lastRefreshReason = resource->lastRefreshReason;
    resource->audit.resourceReady = true;
    resource->audit.refreshPerformed = true;
    resource->audit.refreshRequested = false;
    resource->audit.refreshDeferredByBudget = false;
    resource->audit.lastRefreshCompletedFrame = schedulerFrame;
    resource->audit.gpuCaptureInProgress = false;
    resource->audit.mipChainReady = resource->activeImage != nullptr;
    resource->audit.mipGenerationCount = 1u;
    resource->audit.sourceMipGenerationCount = 1u;
    resource->audit.sourceMipCount = completedSourceMipCount;
    resource->audit.sourceMipMemoryBytes = CapturedSceneCubemapMipMemoryBytes(
        m_CapturedSceneCubemapFaceSize,
        completedSourceMipCount
    );
    resource->audit.sourceMipChainReady = completedSourceMipCount ==
        MipCountForExtent(m_CapturedSceneCubemapFaceSize);
    resource->audit.ggxPrefilterSourceImageSeparated = sourceImageSeparated;
    resource->audit.ggxPrefilterPdfLodEnabled =
        CapturedReflectionProbeGgxPrefilterEnabled(
            resource->filteringSettings.quality
        );
    resource->audit.ggxPrefilterDispatchCount =
        resource->audit.ggxPrefilterPdfLodEnabled &&
            resource->activeImage != nullptr
            ? resource->activeImage->MipLevels() - 1u
            : 0u;
    resource->audit.ggxPrefilterSampleCount = CapturedReflectionProbeGgxSampleCount(
        resource->filteringSettings.quality
    );
    resource->audit.ggxPrefilterQuality = static_cast<u32>(
        resource->filteringSettings.quality
    );
    resource->audit.ggxPrefilterFallbackActive =
        !CapturedReflectionProbeGgxPrefilterEnabled(
            resource->filteringSettings.quality
        );
    resource->audit.ggxPrefilterReady =
        !resource->audit.ggxPrefilterFallbackActive &&
        resource->audit.sourceMipChainReady &&
        resource->audit.ggxPrefilterSourceImageSeparated &&
        resource->audit.ggxPrefilterPdfLodEnabled;
    resource->audit.diffuseIrradianceDispatchCount = 1u;
    resource->audit.diffuseIrradianceSampleCount =
        kGpuCapturedSceneDiffuseIrradianceSampleCount;
    resource->audit.diffuseIrradianceFaceSize =
        kGpuCapturedSceneDiffuseIrradianceFaceSize;
    resource->audit.diffuseIrradianceReady =
        resource->activeDiffuseIrradianceImage != nullptr &&
        resource->activeDiffuseIrradianceImage->View() != VK_NULL_HANDLE;
    resource->audit.probeSceneIndex = probeSceneIndex;
    resource->audit.facesRendered = 6u;
    resource->audit.facesPending = 0u;
    m_LastCapturedSceneProbeSceneIndex = probeSceneIndex;
}

void VulkanReflectionProbeResources::FailGpuCapturedSceneRefresh(i32 probeSceneIndex) {
    CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    if (resource == nullptr) {
        return;
    }
    ReleaseGpuCapturedSceneResources(*resource);
    resource->captureInProgress = false;
    resource->nextFace = 0u;
    resource->facesRendered = 0u;
    resource->audit.gpuCaptureInProgress = false;
    resource->audit.gpuResourcesAllocated = false;
    m_LastCapturedSceneProbeSceneIndex = probeSceneIndex;
}

void VulkanReflectionProbeResources::ReleaseGpuCapturedSceneAttachments(
    CapturedSceneProbeResource& resource
) {
    for (VkFramebuffer framebuffer : resource.framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_GpuCapturedSceneDevice, framebuffer, nullptr);
        }
    }
    resource.framebuffers.clear();
    for (VkImageView view : resource.faceViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_GpuCapturedSceneDevice, view, nullptr);
        }
    }
    resource.faceViews.clear();
    if (resource.prefilterSourceView != VK_NULL_HANDLE) {
        vkDestroyImageView(
            m_GpuCapturedSceneDevice,
            resource.prefilterSourceView,
            nullptr
        );
        resource.prefilterSourceView = VK_NULL_HANDLE;
    }
    for (VkImageView view : resource.prefilterMipViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_GpuCapturedSceneDevice, view, nullptr);
        }
    }
    resource.prefilterMipViews.clear();
    if (resource.diffuseIrradianceArrayView != VK_NULL_HANDLE) {
        vkDestroyImageView(
            m_GpuCapturedSceneDevice,
            resource.diffuseIrradianceArrayView,
            nullptr
        );
        resource.diffuseIrradianceArrayView = VK_NULL_HANDLE;
    }
}

void VulkanReflectionProbeResources::ReleaseGpuCapturedSceneResources(
    CapturedSceneProbeResource& resource
) {
    ReleaseGpuCapturedSceneAttachments(resource);
    resource.depthImage.reset();
    resource.sourceRadianceImage.reset();
    resource.targetImage.reset();
    resource.targetDiffuseIrradianceImage.reset();
}

void VulkanReflectionProbeResources::Release() {
    for (auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        (void)sceneIndex;
        ReleaseGpuCapturedSceneResources(resource);
        resource.activeImage.reset();
        resource.activeDiffuseIrradianceImage.reset();
    }
    m_CapturedSceneProbeResources.clear();
    ReleaseGpuCapturedSceneDiffuseIrradianceResources();
    ReleaseGpuCapturedScenePrefilterResources();
    if (m_GpuCapturedSceneRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_GpuCapturedSceneDevice, m_GpuCapturedSceneRenderPass, nullptr);
        m_GpuCapturedSceneRenderPass = VK_NULL_HANDLE;
    }
    m_GpuCapturedSceneDevice = VK_NULL_HANDLE;
    m_CapturedSceneCubemapFaceSize = 512u;
    m_CapturedSceneCubemapFaceSizeInitialized = false;
    m_BuiltInCubemapImage.reset();
    m_BuiltInCubemapView = VK_NULL_HANDLE;
    m_LastCapturedSceneProbeSceneIndex = -1;
    m_EmptyCapturedSceneAudit = {};
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

bool VulkanReflectionProbeResources::CapturedSceneReady(
    i32 probeSceneIndex,
    VkSampler sampler
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr && resource->activeImage != nullptr &&
        resource->activeImage->View() != VK_NULL_HANDLE &&
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

VkImageView VulkanReflectionProbeResources::CapturedSceneDescriptorViewFor(
    i32 probeSceneIndex,
    VkImageView fallbackView,
    VkSampler sampler
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return CapturedSceneReady(probeSceneIndex, sampler)
        ? resource->activeImage->View()
        : fallbackView;
}

bool VulkanReflectionProbeResources::CapturedSceneDescriptorMatchesProbe(
    i32 probeSceneIndex,
    VkImageView descriptorView,
    VkSampler sampler
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr && CapturedSceneReady(probeSceneIndex, sampler) &&
        descriptorView == resource->activeImage->View();
}

bool VulkanReflectionProbeResources::CapturedSceneDiffuseIrradianceReady(
    i32 probeSceneIndex,
    VkSampler sampler
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr &&
        resource->activeDiffuseIrradianceImage != nullptr &&
        resource->activeDiffuseIrradianceImage->View() != VK_NULL_HANDLE &&
        sampler != VK_NULL_HANDLE;
}

VkImageView
VulkanReflectionProbeResources::CapturedSceneDiffuseIrradianceDescriptorViewFor(
    i32 probeSceneIndex,
    VkImageView fallbackView,
    VkSampler sampler
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return CapturedSceneDiffuseIrradianceReady(probeSceneIndex, sampler)
        ? resource->activeDiffuseIrradianceImage->View()
        : fallbackView;
}

bool VulkanReflectionProbeResources::CapturedSceneDiffuseIrradianceDescriptorMatchesProbe(
    i32 probeSceneIndex,
    VkImageView descriptorView,
    VkSampler sampler
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr &&
        CapturedSceneDiffuseIrradianceReady(probeSceneIndex, sampler) &&
        descriptorView == resource->activeDiffuseIrradianceImage->View();
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

AuthoredReflectionProbeFilterQuality
VulkanReflectionProbeResources::AuthoredCubemapFilterQuality(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end()
        ? found->second.filterQuality
        : AuthoredReflectionProbeFilterQuality::Medium;
}

bool VulkanReflectionProbeResources::AuthoredCubemapSeamAwareFiltering(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() &&
        found->second.image != nullptr &&
        found->second.seamAwareFiltering;
}

bool VulkanReflectionProbeResources::AuthoredCubemapIrradianceReady(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() &&
        found->second.image != nullptr &&
        found->second.irradianceReady;
}

std::array<f32, 3> VulkanReflectionProbeResources::AuthoredCubemapIrradianceColor(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() &&
            found->second.image != nullptr &&
            found->second.irradianceReady
        ? found->second.irradianceColor
        : std::array<f32, 3>{ 1.0f, 1.0f, 1.0f };
}

u32 VulkanReflectionProbeResources::AuthoredCubemapIrradianceReadyCount() const {
    u32 count = 0;
    for (const auto& [assetId, resource] : m_AuthoredCubemaps) {
        (void)assetId;
        if (resource.image != nullptr && resource.irradianceReady) {
            ++count;
        }
    }
    return count;
}

bool VulkanReflectionProbeResources::AuthoredCubemapDiffuseLobesReady(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() &&
        found->second.image != nullptr &&
        found->second.diffuseLobesReady;
}

AuthoredReflectionProbeDiffuseLobes
VulkanReflectionProbeResources::AuthoredCubemapDiffuseLobes(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end() &&
            found->second.image != nullptr &&
            found->second.diffuseLobesReady
        ? found->second.diffuseLobes
        : AuthoredReflectionProbeDiffuseLobes{};
}

u32 VulkanReflectionProbeResources::AuthoredCubemapDiffuseLobesReadyCount() const {
    u32 count = 0;
    for (const auto& [assetId, resource] : m_AuthoredCubemaps) {
        (void)assetId;
        if (resource.image != nullptr && resource.diffuseLobesReady) {
            ++count;
        }
    }
    return count;
}

AuthoredReflectionCubemapSourceType VulkanReflectionProbeResources::AuthoredCubemapSourceType(
    std::string_view assetId
) const {
    const auto found = m_AuthoredCubemaps.find(std::string(assetId));
    return found != m_AuthoredCubemaps.end()
        ? found->second.sourceType
        : AuthoredReflectionCubemapSourceType::Unknown;
}

u32 VulkanReflectionProbeResources::CapturedSceneFaceSize(
    i32 probeSceneIndex
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr && resource->activeImage != nullptr
        ? resource->activeImage->Extent().width
        : 0u;
}

u32 VulkanReflectionProbeResources::CapturedSceneMipCount(
    i32 probeSceneIndex
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr && resource->activeImage != nullptr
        ? resource->activeImage->MipLevels()
        : 0u;
}

VkFormat VulkanReflectionProbeResources::CapturedSceneFormat(
    i32 probeSceneIndex
) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr && resource->activeImage != nullptr
        ? resource->activeImage->Format()
        : VK_FORMAT_UNDEFINED;
}

u32 VulkanReflectionProbeResources::CapturedSceneUploadCount() const {
    u32 count = 0u;
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        (void)sceneIndex;
        count += resource.uploadCount;
    }
    return count;
}

u32 VulkanReflectionProbeResources::CapturedSceneRefreshCheckCount() const {
    u32 count = 0u;
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        (void)sceneIndex;
        count += resource.refreshCheckCount;
    }
    return count;
}

u32 VulkanReflectionProbeResources::CapturedSceneLocalityIgnoredLightRevisionCount() const {
    u32 count = 0u;
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        (void)sceneIndex;
        if (resource.audit.localityIgnoredLightRevision) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::CapturedSceneLocalityIgnoredGeometryRevisionCount() const {
    u32 count = 0u;
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        (void)sceneIndex;
        if (resource.audit.localityIgnoredGeometryRevision) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::CapturedSceneDirtyLocalLightProbeCount() const {
    u32 count = 0u;
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        (void)sceneIndex;
        if (resource.audit.localLightDirty) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::CapturedSceneDirtyGeometryProbeCount() const {
    u32 count = 0u;
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        (void)sceneIndex;
        if (resource.audit.geometryDirty) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::CapturedSceneSignature() const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(m_LastCapturedSceneProbeSceneIndex);
    return resource != nullptr ? resource->signature : 0u;
}

const CapturedSceneCaptureAudit&
VulkanReflectionProbeResources::CapturedSceneAudit() const {
    return CapturedSceneAudit(m_LastCapturedSceneProbeSceneIndex);
}

const CapturedSceneCaptureAudit&
VulkanReflectionProbeResources::CapturedSceneAudit(i32 probeSceneIndex) const {
    const CapturedSceneProbeResource* resource =
        FindCapturedSceneProbeResource(probeSceneIndex);
    return resource != nullptr ? resource->audit : m_EmptyCapturedSceneAudit;
}

u32 VulkanReflectionProbeResources::CapturedSceneProbeResourceCount() const {
    return static_cast<u32>(std::min<std::size_t>(
        m_CapturedSceneProbeResources.size(),
        std::numeric_limits<u32>::max()
    ));
}

u32 VulkanReflectionProbeResources::CapturedSceneReadyProbeCount(
    VkSampler sampler
) const {
    u32 count = 0u;
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        (void)resource;
        if (CapturedSceneReady(sceneIndex, sampler)) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::CapturedSceneInFlightProbeCount() const {
    u32 count = 0u;
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        (void)sceneIndex;
        if (resource.captureInProgress) {
            ++count;
        }
    }
    return count;
}

u32 VulkanReflectionProbeResources::CapturedSceneDistinctActiveViewCount(
    VkSampler sampler
) const {
    std::vector<VkImageView> views;
    views.reserve(m_CapturedSceneProbeResources.size());
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        if (CapturedSceneReady(sceneIndex, sampler)) {
            views.push_back(resource.activeImage->View());
        }
    }
    std::sort(views.begin(), views.end());
    return static_cast<u32>(std::unique(views.begin(), views.end()) - views.begin());
}

u32 VulkanReflectionProbeResources::CapturedSceneDiffuseIrradianceReadyProbeCount(
    VkSampler sampler
) const {
    u32 count = 0u;
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        (void)resource;
        if (CapturedSceneDiffuseIrradianceReady(sceneIndex, sampler)) {
            ++count;
        }
    }
    return count;
}

u32
VulkanReflectionProbeResources::CapturedSceneDistinctActiveDiffuseIrradianceViewCount(
    VkSampler sampler
) const {
    std::vector<VkImageView> views;
    views.reserve(m_CapturedSceneProbeResources.size());
    for (const auto& [sceneIndex, resource] : m_CapturedSceneProbeResources) {
        if (CapturedSceneDiffuseIrradianceReady(sceneIndex, sampler)) {
            views.push_back(resource.activeDiffuseIrradianceImage->View());
        }
    }
    std::sort(views.begin(), views.end());
    return static_cast<u32>(std::unique(views.begin(), views.end()) - views.begin());
}

void VulkanReflectionProbeResources::SetDescriptorSetsBound(u32 count) {
    m_DescriptorSetsBound = count;
}

u32 VulkanReflectionProbeResources::DescriptorSetsBound() const {
    return m_DescriptorSetsBound;
}

}
