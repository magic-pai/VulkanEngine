#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;

class VulkanShaderModule {
public:
    VulkanShaderModule(const VulkanDevice& device, const std::string& filePath);
    ~VulkanShaderModule();

    SE_DISABLE_COPY(VulkanShaderModule);
    SE_DISABLE_MOVE(VulkanShaderModule);

    VkShaderModule Handle() const;

private:
    static std::vector<char> ReadFile(const std::string& filePath);
    void CreateShaderModule(const VulkanDevice& device, const std::vector<char>& code);
    void Release();

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkShaderModule m_ShaderModule = VK_NULL_HANDLE;
};

}
