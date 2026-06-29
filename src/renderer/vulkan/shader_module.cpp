#include "renderer/vulkan/shader_module.h"

#include "renderer/vulkan/device.h"

#include <fstream>

namespace se {

VulkanShaderModule::VulkanShaderModule(const VulkanDevice& device, const std::string& filePath)
    : m_Device(device.Handle()) {
    const std::vector<char> code = ReadFile(filePath);
    CreateShaderModule(device, code);
}

VulkanShaderModule::~VulkanShaderModule() {
    Release();
}

VkShaderModule VulkanShaderModule::Handle() const {
    return m_ShaderModule;
}

std::vector<char> VulkanShaderModule::ReadFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filePath);
    }

    const std::streamsize fileSize = file.tellg();
    std::vector<char> buffer(static_cast<std::size_t>(fileSize));

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

void VulkanShaderModule::CreateShaderModule(const VulkanDevice& device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const u32*>(code.data());

    if (vkCreateShaderModule(device.Handle(), &createInfo, nullptr, &m_ShaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shader module");
    }
}

void VulkanShaderModule::Release() {
    if (m_ShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_Device, m_ShaderModule, nullptr);
        m_ShaderModule = VK_NULL_HANDLE;
    }
}

}
