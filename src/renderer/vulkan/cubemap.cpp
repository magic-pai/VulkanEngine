#include "renderer/vulkan/cubemap.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace se {

namespace {

constexpr u32 kCubemapFaceCount = 6;

struct StbiImageDeleter {
    void operator()(stbi_uc* pixels) const {
        stbi_image_free(pixels);
    }
};

struct LoadedFacePixels {
    std::unique_ptr<stbi_uc, StbiImageDeleter> pixels;
    int width = 0;
    int height = 0;
    int channels = 0;
};

LoadedFacePixels LoadFace(const std::filesystem::path& path) {
    LoadedFacePixels face{};
    face.pixels.reset(stbi_load(
        path.string().c_str(),
        &face.width,
        &face.height,
        &face.channels,
        STBI_rgb_alpha
    ));

    if (face.pixels.get() == nullptr) {
        throw std::runtime_error("Failed to load cubemap face: " + path.string());
    }

    return face;
}

std::span<const std::byte> TextureBytes(const std::vector<u8>& pixels) {
    return std::as_bytes(std::span<const u8>(pixels.data(), pixels.size()));
}

u32 CalculateMipLevels(int width, int height) {
    const int largestDimension = std::max(width, height);
    return static_cast<u32>(std::floor(std::log2(largestDimension))) + 1;
}

}

VulkanCubemap::VulkanCubemap(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string directory
) {
    CreateCubemapImage(device, physicalDevice, commandPool, directory);
}

VulkanCubemap::~VulkanCubemap() = default;

VkImageView VulkanCubemap::View() const {
    return m_Image.View();
}

VkImageLayout VulkanCubemap::Layout() const {
    return m_Layout;
}

VkExtent2D VulkanCubemap::Extent() const {
    return m_Image.Extent();
}

u32 VulkanCubemap::MipLevels() const {
    return m_Image.MipLevels();
}

void VulkanCubemap::CreateCubemapImage(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const std::string& directory
) {
    const std::array<std::string, kCubemapFaceCount> faceNames = {
        "right.png",
        "left.png",
        "top.png",
        "bottom.png",
        "front.png",
        "back.png"
    };

    std::array<LoadedFacePixels, kCubemapFaceCount> faces;
    const std::filesystem::path cubemapDirectory(directory);

    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        faces[faceIndex] = LoadFace(cubemapDirectory / faceNames[faceIndex]);
    }

    const int width = faces[0].width;
    const int height = faces[0].height;
    SE_ASSERT(width > 0 && height > 0, "Cubemap face size must be valid");

    for (const LoadedFacePixels& face : faces) {
        SE_ASSERT(face.width == width && face.height == height, "Cubemap faces must share the same size");
    }

    const VkDeviceSize faceSize =
        static_cast<VkDeviceSize>(width) *
        static_cast<VkDeviceSize>(height) *
        4;
    const VkDeviceSize imageSize = faceSize * kCubemapFaceCount;

    std::vector<u8> pixels(static_cast<std::size_t>(imageSize));
    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        std::memcpy(
            pixels.data() + static_cast<std::size_t>(faceSize) * faceIndex,
            faces[faceIndex].pixels.get(),
            static_cast<std::size_t>(faceSize)
        );
    }

    VulkanBuffer stagingBuffer(
        device,
        physicalDevice,
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer.Upload(TextureBytes(pixels));

    const VkExtent2D extent = {
        static_cast<u32>(width),
        static_cast<u32>(height)
    };
    const u32 mipLevels = CalculateMipLevels(width, height);

    m_Image.Recreate(
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
        kCubemapFaceCount,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE
    );

    m_Image.TransitionLayout(
        device,
        commandPool,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        mipLevels
    );
    m_Image.CopyFromBuffer(device, commandPool, stagingBuffer.Handle(), kCubemapFaceCount);
    m_Image.GenerateMipmaps(physicalDevice, device, commandPool);

    m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

}
