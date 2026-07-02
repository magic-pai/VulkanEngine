#include "renderer/vulkan/ibl_generator.h"
#include "renderer/vulkan/image.h"
#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

void GenerateIblTextures(const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice, const VulkanCommandPool& commandPool,
    std::unique_ptr<VulkanImage>& brdfImage, std::unique_ptr<VulkanImage>& irradianceImage,
    std::unique_ptr<VulkanImage>& prefilteredImage,
    VkImageView& irradianceView, VkImageView& prefilteredView, VkSampler& sampler)
{
    const u32 srcFace = kIblPrefilteredFaceSize;
    const u32 irrFace = kIblIrradianceFaceSize;
    const u32 mipCount = kIblPrefilteredMipCount;
    const u32 faceCount = 6;
    const VkFormat fmt = kIblEnvironmentFormat;
    const u32 pxBytes = 8;

    // Procedural sky
    std::vector<u8> srcPx(srcFace * srcFace * faceCount * pxBytes);
    for (u32 f = 0; f < faceCount; ++f)
        for (u32 y = 0; y < srcFace; ++y)
            for (u32 x = 0; x < srcFace; ++x) {
                float ny = (float(y) + 0.5f) / float(srcFace) * 2.0f - 1.0f;
                float skyLerp = (ny + 0.15f) / 1.15f;
                float r = std::lerp(0.05f, 0.15f, skyLerp);
                float g = std::lerp(0.06f, 0.20f, skyLerp);
                float b = std::lerp(0.10f, 0.40f, skyLerp);
                u32 off = ((f * srcFace + y) * srcFace + x) * pxBytes;
                u16 rh=f2h(r), gh=f2h(g), bh=f2h(b), ah=u16(0x3C00);
                memcpy(&srcPx[off], &rh, 2); memcpy(&srcPx[off+2], &gh, 2);
                memcpy(&srcPx[off+4], &bh, 2); memcpy(&srcPx[off+6], &ah, 2);
            }

    VulkanBuffer staging(device, physicalDevice, srcPx.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.Upload(std::as_bytes(std::span<const u8>(srcPx)));

    // Prefiltered cubemap. Generate mips from the procedural sky so startup does
    // not depend on optional compute IBL shaders being present.
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
    const u32 irrBytes = irrFace * irrFace * faceCount * pxBytes;
    std::vector<u8> irrPx(irrBytes);
    for (u32 f = 0; f < faceCount; ++f)
        for (u32 y = 0; y < irrFace; ++y)
            for (u32 x = 0; x < irrFace; ++x) {
                float u = (float(x) + 0.5f) / float(irrFace), v = (float(y) + 0.5f) / float(irrFace);
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
                for (float phi = 0; phi < 6.2832f; phi += 0.14f)
                    for (float theta = 0; theta < 1.5708f; theta += 0.14f) {
                        glm::vec3 ts(std::sin(theta)*std::cos(phi), std::sin(theta)*std::sin(phi), std::cos(theta));
                        glm::vec3 sd = T*ts.x + B*ts.y + N*ts.z;
                        float sky = 0.04f + 0.10f * std::max(sd.y, 0.0f);
                        irr += glm::vec3(sky) * std::cos(theta) * std::sin(theta);
                        cnt += 1;
                    }
                irr = 3.141593f * irr / std::max(cnt, 1.0f);
                u32 off = ((f * irrFace + y) * irrFace + x) * pxBytes;
                u16 rh=f2h(glm::clamp(irr.r,0.0f,65504.0f)), gh=f2h(glm::clamp(irr.g,0.0f,65504.0f));
                u16 bh=f2h(glm::clamp(irr.b,0.0f,65504.0f)), ah=u16(0x3C00);
                memcpy(&irrPx[off],&rh,2); memcpy(&irrPx[off+2],&gh,2);
                memcpy(&irrPx[off+4],&bh,2); memcpy(&irrPx[off+6],&ah,2);
            }

    VulkanBuffer irrStaging(device, physicalDevice, irrBytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    irrStaging.Upload(std::as_bytes(std::span<const u8>(irrPx)));
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
    const u32 lutSize = kIblBrdfLutSize;
    std::vector<u8> brdfPx(lutSize * lutSize * 4);
    for (u32 y = 0; y < lutSize; ++y)
        for (u32 x = 0; x < lutSize; ++x) {
            float roughness = (float(y) + 0.5f) / float(lutSize);
            float nDotV = (float(x) + 0.5f) / float(lutSize);
            glm::vec2 integrated = IntegrateBrdf(nDotV, roughness);
            u32 off = (y * lutSize + x) * 4;
            u16 sh=f2h(glm::clamp(integrated.x, 0.0f, 4.0f));
            u16 bh=f2h(glm::clamp(integrated.y, 0.0f, 4.0f));
            memcpy(&brdfPx[off], &sh, 2); memcpy(&brdfPx[off+2], &bh, 2);
        }
    VulkanBuffer brdfStaging(device, physicalDevice, brdfPx.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    brdfStaging.Upload(std::as_bytes(std::span<const u8>(brdfPx)));
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
