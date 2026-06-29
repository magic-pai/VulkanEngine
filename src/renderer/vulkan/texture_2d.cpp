#include "renderer/vulkan/texture_2d.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/upload_batch.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <filesystem>
#include <cmath>

namespace se {

namespace {

struct StbiImageDeleter {
    void operator()(stbi_uc* pixels) const {
        stbi_image_free(pixels);
    }
};

struct LoadedTexturePixels {
    std::unique_ptr<stbi_uc, StbiImageDeleter> pixels;
    int width = 0;
    int height = 0;
    int channels = 0;
};

LoadedTexturePixels LoadTextureFromFile(
    const std::string& path,
    bool flipVertically
) {
    LoadedTexturePixels texture{};
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    texture.pixels.reset(stbi_load(
        path.c_str(),
        &texture.width,
        &texture.height,
        &texture.channels,
        STBI_rgb_alpha
    ));
    stbi_set_flip_vertically_on_load(0);

    if (texture.pixels.get() == nullptr) {
        throw std::runtime_error("Failed to load texture image: " + path);
    }

    return texture;
}

LoadedTexturePixels LoadFallbackTexture(bool flipVertically) {
    constexpr std::array<u8, 23> kFallbackPpm = {
        'P', '6', '\n',
        '2', ' ', '2', '\n',
        '2', '5', '5', '\n',
        255, 255, 255,
        40, 40, 40,
        40, 40, 40,
        255, 255, 255
    };

    LoadedTexturePixels texture{};
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    texture.pixels.reset(stbi_load_from_memory(
        kFallbackPpm.data(),
        static_cast<int>(kFallbackPpm.size()),
        &texture.width,
        &texture.height,
        &texture.channels,
        STBI_rgb_alpha
    ));
    stbi_set_flip_vertically_on_load(0);

    if (texture.pixels.get() == nullptr) {
        throw std::runtime_error("Failed to load fallback texture image");
    }

    return texture;
}

LoadedTexturePixels LoadTexturePixels(
    const std::string& path,
    bool flipVertically
) {
    if (std::filesystem::exists(path)) {
        return LoadTextureFromFile(path, flipVertically);
    }

    return LoadFallbackTexture(flipVertically);
}

std::vector<u8> FlippedRgba(
    std::span<const u8> rgba,
    u32 width,
    u32 height
) {
    std::vector<u8> flipped(rgba.size());
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    for (u32 y = 0; y < height; ++y) {
        const std::size_t sourceOffset =
            static_cast<std::size_t>(height - 1 - y) * rowBytes;
        const std::size_t destinationOffset = static_cast<std::size_t>(y) * rowBytes;
        std::copy_n(
            rgba.data() + sourceOffset,
            rowBytes,
            flipped.data() + destinationOffset
        );
    }

    return flipped;
}

LoadedTexturePixels DecodeTextureBytes(
    std::span<const u8> bytes,
    bool flipVertically
) {
    LoadedTexturePixels texture{};
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    texture.pixels.reset(stbi_load_from_memory(
        bytes.data(),
        static_cast<int>(bytes.size()),
        &texture.width,
        &texture.height,
        &texture.channels,
        STBI_rgb_alpha
    ));
    stbi_set_flip_vertically_on_load(0);

    if (texture.pixels.get() == nullptr) {
        throw std::runtime_error("Failed to decode embedded texture image");
    }

    return texture;
}

u32 CalculateMipLevels(int width, int height) {
    const int largestDimension = std::max(width, height);
    return static_cast<u32>(std::floor(std::log2(largestDimension))) + 1;
}

}

VulkanTexture2D::VulkanTexture2D(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    std::string path,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    CreateTextureImage(
        device,
        physicalDevice,
        commandPool,
        path,
        srgb,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

VulkanTexture2D::VulkanTexture2D(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels pixels,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    CreateTextureImage(
        device,
        physicalDevice,
        commandPool,
        pixels,
        srgb,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

VulkanTexture2D::VulkanTexture2D(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes encoded,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    CreateTextureImage(
        device,
        physicalDevice,
        commandPool,
        encoded,
        srgb,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
}

VulkanTexture2D::~VulkanTexture2D() = default;

VkImageView VulkanTexture2D::View() const {
    return m_Image.View();
}

VkImageLayout VulkanTexture2D::Layout() const {
    return m_Layout;
}

VkExtent2D VulkanTexture2D::Extent() const {
    return m_Image.Extent();
}

u32 VulkanTexture2D::MipLevels() const {
    return m_Image.MipLevels();
}

void VulkanTexture2D::CreateTextureImage(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const std::string& path,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    const LoadedTexturePixels texture = LoadTexturePixels(path, flipVertically);
    SE_ASSERT(texture.width > 0 && texture.height > 0, "Texture size must be valid");

    CreateTextureImage(
        device,
        physicalDevice,
        commandPool,
        VulkanTexturePixels{
            std::span<const u8>(
                texture.pixels.get(),
                static_cast<std::size_t>(texture.width) *
                    static_cast<std::size_t>(texture.height) *
                    4
            ),
            static_cast<u32>(texture.width),
            static_cast<u32>(texture.height)
        },
        srgb,
        generateMipmaps,
        false,
        uploadBatch
    );
}

void VulkanTexture2D::CreateTextureImage(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanEncodedTextureBytes encoded,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    SE_ASSERT(!encoded.bytes.empty(), "Encoded texture byte span must not be empty");
    const LoadedTexturePixels texture = DecodeTextureBytes(encoded.bytes, flipVertically);

    CreateTextureImage(
        device,
        physicalDevice,
        commandPool,
        VulkanTexturePixels{
            std::span<const u8>(
                texture.pixels.get(),
                static_cast<std::size_t>(texture.width) *
                    static_cast<std::size_t>(texture.height) *
                    4
            ),
            static_cast<u32>(texture.width),
            static_cast<u32>(texture.height)
        },
        srgb,
        generateMipmaps,
        false,
        uploadBatch
    );
}

void VulkanTexture2D::CreateTextureImage(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanTexturePixels pixels,
    bool srgb,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    SE_ASSERT(pixels.width > 0 && pixels.height > 0, "Texture size must be valid");
    const VkDeviceSize imageSize =
        static_cast<VkDeviceSize>(pixels.width) *
        static_cast<VkDeviceSize>(pixels.height) *
        4;
    SE_ASSERT(
        pixels.rgba.size_bytes() >= imageSize,
        "Texture pixel span is smaller than the requested image size"
    );

    std::vector<u8> flippedPixels;
    if (flipVertically) {
        flippedPixels = FlippedRgba(pixels.rgba, pixels.width, pixels.height);
        pixels.rgba = std::span<const u8>(flippedPixels.data(), flippedPixels.size());
    }

    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer->Upload(std::as_bytes(std::span<const u8>(
        pixels.rgba.data(),
        static_cast<std::size_t>(imageSize)
    )));

    const VkExtent2D extent = {
        pixels.width,
        pixels.height
    };
    const u32 mipLevels = generateMipmaps
        ? CalculateMipLevels(static_cast<int>(pixels.width), static_cast<int>(pixels.height))
        : 1;

    m_Image.Recreate(
        device,
        physicalDevice,
        extent,
        srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        mipLevels,
        1,
        0,
        VK_IMAGE_VIEW_TYPE_2D
    );

    m_Image.TransitionLayout(
        device,
        commandPool,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        mipLevels,
        uploadBatch
    );
    m_Image.CopyFromBuffer(device, commandPool, stagingBuffer->Handle(), uploadBatch);
    if (generateMipmaps) {
        m_Image.GenerateMipmaps(physicalDevice, device, commandPool, uploadBatch);
    } else {
        m_Image.TransitionLayout(
            device,
            commandPool,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            mipLevels,
            uploadBatch
        );
    }
    if (uploadBatch != nullptr) {
        uploadBatch->KeepAlive(std::move(stagingBuffer));
    }

    m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

}
