#include "renderer/vulkan/ibl_generator.h"
#include "renderer/vulkan/image.h"
#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/compute_pipeline.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/shader_module.h"
#include "renderer/vulkan/physical_device.h"
#include <glm/glm.hpp>

namespace se {

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

void GenerateIblTextures(const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice, const VulkanCommandPool& commandPool,
    std::unique_ptr<VulkanImage>& brdfImage, std::unique_ptr<VulkanImage>& irradianceImage,
    std::unique_ptr<VulkanImage>& prefilteredImage,
    VkImageView& irradianceView, VkImageView& prefilteredView, VkSampler& sampler)
{
    const u32 srcFace = 256, irrFace = 32, mipCount = 5, faceCount = 6;
    const VkFormat fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
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
                u32 off = (f * srcFace + y) * srcFace + x * pxBytes;
                u16 rh=f2h(r), gh=f2h(g), bh=f2h(b), ah=u16(0x3C00);
                memcpy(&srcPx[off], &rh, 2); memcpy(&srcPx[off+2], &gh, 2);
                memcpy(&srcPx[off+4], &bh, 2); memcpy(&srcPx[off+6], &ah, 2);
            }

    VulkanBuffer staging(device, physicalDevice, srcPx.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.Upload(std::as_bytes(std::span<const u8>(srcPx)));

    // Source cubemap (for GPU prefilter sampling)
    auto srcCubemap = std::make_unique<VulkanImage>(device, physicalDevice,
        VkExtent2D{srcFace, srcFace}, fmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
        1, faceCount, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
    srcCubemap->TransitionLayout(device, commandPool, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
    srcCubemap->CopyFromBuffer(device, commandPool, staging.Handle(), faceCount);
    srcCubemap->TransitionLayout(device, commandPool, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    VkImageView srcView = VK_NULL_HANDLE;
    { VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      vi.image = srcCubemap->Handle(); vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE; vi.format = fmt;
      vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = faceCount;
      vkCreateImageView(device.Handle(), &vi, nullptr, &srcView); }

    // Prefiltered cubemap (filled by GPU GGX prefilter instead of box-filter mips)
    prefilteredImage = std::make_unique<VulkanImage>(device, physicalDevice,
        VkExtent2D{srcFace, srcFace}, fmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
        mipCount, faceCount, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
    prefilteredImage->TransitionLayout(device, commandPool, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipCount);
    { VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      vi.image = prefilteredImage->Handle(); vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE; vi.format = fmt;
      vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      vi.subresourceRange.levelCount = mipCount; vi.subresourceRange.layerCount = faceCount;
      vkCreateImageView(device.Handle(), &vi, nullptr, &prefilteredView); }

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
                u32 off = (f * irrFace + y) * irrFace + x * pxBytes;
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
    { VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      vi.image = irradianceImage->Handle(); vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE; vi.format = fmt;
      vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = faceCount;
      vkCreateImageView(device.Handle(), &vi, nullptr, &irradianceView); }

    // ---- GPU prefilter (GGX importance sampling) ----
    RunGpuIblPrefilter(device, commandPool, srcView,
        irradianceImage->Handle(), irradianceView,
        prefilteredImage->Handle(), prefilteredView,
        srcFace, irrFace, mipCount);

    // Cleanup source cubemap (not needed after prefilter)
    vkDestroyImageView(device.Handle(), srcView, nullptr);
    srcCubemap.reset();

    // BRDF LUT
    const u32 lutSize = 128;
    std::vector<u8> brdfPx(lutSize * lutSize * 4);
    for (u32 y = 0; y < lutSize; ++y)
        for (u32 x = 0; x < lutSize; ++x) {
            float roughness = (float(y) + 0.5f) / float(lutSize);
            float r = 1.0f - roughness;
            float scale = r * r * 0.5f + 0.25f, bias = r * 0.15f + 0.02f;
            u32 off = (y * lutSize + x) * 4;
            u16 sh=f2h(scale), bh=f2h(bias);
            memcpy(&brdfPx[off], &sh, 2); memcpy(&brdfPx[off+2], &bh, 2);
        }
    VulkanBuffer brdfStaging(device, physicalDevice, brdfPx.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    brdfStaging.Upload(std::as_bytes(std::span<const u8>(brdfPx)));
    brdfImage = std::make_unique<VulkanImage>(device, physicalDevice,
        VkExtent2D{lutSize, lutSize}, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
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

void RunGpuIblPrefilter(const VulkanDevice& device, const VulkanCommandPool& commandPool,
    VkImageView sourceCubemapView,
    VkImage irradianceImage, VkImageView irradianceView,
    VkImage prefilteredImage, VkImageView prefilteredView,
    u32 faceSize, u32 irrFaceSize, u32 mipCount)
{
    // Descriptor set layout: samplerCube + storage imageCube
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    { VkDescriptorSetLayoutBinding bs[2]{};
      bs[0].binding=0; bs[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      bs[0].descriptorCount=1; bs[0].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
      bs[1].binding=1; bs[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      bs[1].descriptorCount=1; bs[1].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
      VkDescriptorSetLayoutCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      ci.bindingCount=2; ci.pBindings=bs;
      vkCreateDescriptorSetLayout(device.Handle(), &ci, nullptr, &dsl); }

    // Sampler for source cubemap
    VkSampler samp = VK_NULL_HANDLE;
    { VkSamplerCreateInfo si{}; si.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      si.magFilter=VK_FILTER_LINEAR; si.minFilter=VK_FILTER_LINEAR;
      si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
      si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      si.maxLod=1.0f; vkCreateSampler(device.Handle(),&si,nullptr,&samp); }

    // Create compute pipelines manually
    auto buildPipe = [&device,&dsl](const std::string& path, VkPipeline& pipe, VkPipelineLayout& layout) {
        VulkanShaderModule sm(device, path);
        VkPipelineShaderStageCreateInfo ssi{};
        ssi.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ssi.stage=VK_SHADER_STAGE_COMPUTE_BIT; ssi.module=sm.Handle(); ssi.pName="main";
        VkPushConstantRange pcr{}; pcr.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset=0; pcr.size=16;
        VkPipelineLayoutCreateInfo pli{}; pli.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount=1; pli.pSetLayouts=&dsl; pli.pushConstantRangeCount=1; pli.pPushConstantRanges=&pcr;
        vkCreatePipelineLayout(device.Handle(),&pli,nullptr,&layout);
        VkComputePipelineCreateInfo cpi{}; cpi.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.stage=ssi; cpi.layout=layout;
        vkCreateComputePipelines(device.Handle(),VK_NULL_HANDLE,1,&cpi,nullptr,&pipe);
    };
    VkPipeline irrPipe=VK_NULL_HANDLE, prePipe=VK_NULL_HANDLE;
    VkPipelineLayout irrLayout=VK_NULL_HANDLE, preLayout=VK_NULL_HANDLE;
    buildPipe(std::string(SE_SHADER_DIR)+"/irradiance_map.comp.spv", irrPipe, irrLayout);
    buildPipe(std::string(SE_SHADER_DIR)+"/prefilter_env.comp.spv", prePipe, preLayout);

    // Pool + descriptor sets (1 for irradiance, mipCount for prefilter)
    const u32 setCount = 1 + mipCount;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    { VkDescriptorPoolSize ps[2]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,setCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,setCount}};
      VkDescriptorPoolCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      pi.poolSizeCount=2; pi.pPoolSizes=ps; pi.maxSets=setCount;
      vkCreateDescriptorPool(device.Handle(),&pi,nullptr,&pool); }
    std::vector<VkDescriptorSetLayout> layouts(setCount, dsl);
    std::vector<VkDescriptorSet> dSets(setCount);
    { VkDescriptorSetAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      ai.descriptorPool=pool; ai.descriptorSetCount=setCount; ai.pSetLayouts=layouts.data();
      vkAllocateDescriptorSets(device.Handle(),&ai,dSets.data()); }

    // Write irradiance descriptor
    { VkDescriptorImageInfo si{}; si.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      si.imageView=sourceCubemapView; si.sampler=samp;
      VkDescriptorImageInfo di{}; di.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
      di.imageView=irradianceView; di.sampler=VK_NULL_HANDLE;
      VkWriteDescriptorSet ws[2]{{},{}};
      ws[0].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet=dSets[0]; ws[0].dstBinding=0;
      ws[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[0].descriptorCount=1; ws[0].pImageInfo=&si;
      ws[1].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[1].dstSet=dSets[0]; ws[1].dstBinding=1;
      ws[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ws[1].descriptorCount=1; ws[1].pImageInfo=&di;
      vkUpdateDescriptorSets(device.Handle(),2,ws,0,nullptr); }

    // Per-mip views for prefiltered output
    std::vector<VkImageView> mipViews(mipCount);
    for (u32 m = 0; m < mipCount; ++m) {
        VkImageViewCreateInfo mvi{}; mvi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        mvi.image=prefilteredImage; mvi.viewType=VK_IMAGE_VIEW_TYPE_CUBE;
        mvi.format=VK_FORMAT_R16G16B16A16_SFLOAT;
        mvi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        mvi.subresourceRange.baseMipLevel=m; mvi.subresourceRange.levelCount=1;
        mvi.subresourceRange.layerCount=6;
        vkCreateImageView(device.Handle(),&mvi,nullptr,&mipViews[m]);
    }

    // Write prefilter descriptors (one per mip with per-mip view)
    for (u32 m = 0; m < mipCount; ++m) {
        VkDescriptorImageInfo si{}; si.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        si.imageView=sourceCubemapView; si.sampler=samp;
        VkDescriptorImageInfo di{}; di.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
        di.imageView=mipViews[m]; di.sampler=VK_NULL_HANDLE;
        VkWriteDescriptorSet ws[2]{{},{}};
        ws[0].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet=dSets[1+m]; ws[0].dstBinding=0;
        ws[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[0].descriptorCount=1; ws[0].pImageInfo=&si;
        ws[1].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[1].dstSet=dSets[1+m]; ws[1].dstBinding=1;
        ws[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ws[1].descriptorCount=1; ws[1].pImageInfo=&di;
        vkUpdateDescriptorSets(device.Handle(),2,ws,0,nullptr); }

    // One-shot command buffer
    VkCommandBuffer cb = VK_NULL_HANDLE;
    { VkCommandBufferAllocateInfo cai{}; cai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      cai.commandPool=commandPool.Handle(); cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cai.commandBufferCount=1; vkAllocateCommandBuffers(device.Handle(),&cai,&cb); }
    VkCommandBufferBeginInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb,&bi);

    // Transition irradiance to GENERAL
    { VkImageMemoryBarrier bar{}; bar.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      bar.image=irradianceImage; bar.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      bar.newLayout=VK_IMAGE_LAYOUT_GENERAL; bar.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;
      bar.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
      bar.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,6};
      vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&bar); }

    // Irradiance convolution
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,irrPipe);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,irrLayout,0,1,&dSets[0],0,nullptr);
    for (u32 face=0; face<6; ++face) {
        struct { u32 f; u32 _p[3]; } pc{face,{0,0,0}};
        vkCmdPushConstants(cb,irrLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),&pc);
        vkCmdDispatch(cb,(irrFaceSize+7)/8,(irrFaceSize+7)/8,1);
        VkMemoryBarrier fb{}; fb.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        fb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; fb.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&fb,0,nullptr,0,nullptr); }

    // Transition irradiance back
    { VkImageMemoryBarrier bar{}; bar.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      bar.image=irradianceImage; bar.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
      bar.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      bar.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; bar.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
      bar.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,6};
      vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&bar); }

    // GGX prefilter per mip
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,prePipe);
    for (u32 m=0; m<mipCount; ++m) {
        { VkImageMemoryBarrier bar{}; bar.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          bar.image=prefilteredImage; bar.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          bar.newLayout=VK_IMAGE_LAYOUT_GENERAL;
          bar.srcAccessMask=VK_ACCESS_SHADER_READ_BIT; bar.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
          bar.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,m,1,0,6};
          vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&bar); }

        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,preLayout,
            0,1,&dSets[1+m],0,nullptr);
        const float roughness = float(m)/float(std::max(mipCount-1u,1u));
        for (u32 face=0; face<6; ++face) {
            struct { float r; u32 f; u32 _p[2]; } pc{roughness,face,{0,0}};
            vkCmdPushConstants(cb,preLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),&pc);
            u32 ms=std::max(faceSize>>m,1u);
            vkCmdDispatch(cb,(ms+7)/8,(ms+7)/8,1);
            VkMemoryBarrier fb{}; fb.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            fb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; fb.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&fb,0,nullptr,0,nullptr); }

        { VkImageMemoryBarrier bar{}; bar.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          bar.image=prefilteredImage; bar.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
          bar.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          bar.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; bar.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
          bar.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,m,1,0,6};
          vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&bar); }
    }

    vkEndCommandBuffer(cb);
    { VkSubmitInfo si{}; si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
      si.commandBufferCount=1; si.pCommandBuffers=&cb;
      vkQueueSubmit(device.GraphicsQueue(),1,&si,VK_NULL_HANDLE);
      vkQueueWaitIdle(device.GraphicsQueue()); }

    // Cleanup
    vkFreeCommandBuffers(device.Handle(),commandPool.Handle(),1,&cb);
    for (auto& v : mipViews) if(v) vkDestroyImageView(device.Handle(),v,nullptr);
    vkDestroyDescriptorPool(device.Handle(),pool,nullptr);
    vkDestroyDescriptorSetLayout(device.Handle(),dsl,nullptr);
    vkDestroySampler(device.Handle(),samp,nullptr);
    vkDestroyPipeline(device.Handle(), irrPipe, nullptr);
    vkDestroyPipeline(device.Handle(), prePipe, nullptr);
    vkDestroyPipelineLayout(device.Handle(), irrLayout, nullptr);
    vkDestroyPipelineLayout(device.Handle(), preLayout, nullptr);
}

}
