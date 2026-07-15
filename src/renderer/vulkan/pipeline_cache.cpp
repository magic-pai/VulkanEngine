#include "renderer/vulkan/pipeline_cache.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace se {

namespace {

bool PipelineCacheFlagEnabled(const char* name) {
    const std::string value = LowerAscii(ReadVulkanEnvironmentString(name));
    return value == "1" ||
        value == "true" ||
        value == "yes" ||
        value == "on";
}

bool PipelineCacheFlagDisabled(const char* name) {
    const std::string value = LowerAscii(ReadVulkanEnvironmentString(name));
    return value == "0" ||
        value == "false" ||
        value == "no" ||
        value == "off";
}

bool PipelineCacheEnabledFromEnvironment() {
    return !PipelineCacheFlagDisabled("SE_VK_PIPELINE_CACHE") &&
        !PipelineCacheFlagDisabled("SE_PIPELINE_CACHE");
}

bool PipelineCacheResetRequested() {
    return PipelineCacheFlagEnabled("SE_VK_PIPELINE_CACHE_RESET") ||
        PipelineCacheFlagEnabled("SE_PIPELINE_CACHE_RESET");
}

bool PipelineCacheTraceEnabled() {
    return PipelineCacheFlagEnabled("SE_VK_PIPELINE_CACHE_TRACE") ||
        PipelineCacheFlagEnabled("SE_PIPELINE_CACHE_TRACE");
}

std::filesystem::path PipelineCacheDirectoryFromEnvironment() {
    std::string path = ReadVulkanEnvironmentString("SE_VK_PIPELINE_CACHE_DIR");
    if (path.empty()) {
        path = ReadVulkanEnvironmentString("SE_PIPELINE_CACHE_DIR");
    }
    if (!path.empty()) {
        return std::filesystem::path(path);
    }
    return std::filesystem::current_path() / ".selfengine" / "pipeline_cache";
}

std::string HexU32(u32 value) {
    std::ostringstream stream;
    stream << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

std::string PipelineCacheUuidHex(const u8* uuid) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < VK_UUID_SIZE; ++i) {
        stream << std::setw(2) << static_cast<u32>(uuid[i]);
    }
    return stream.str();
}

std::string SanitizeDeviceName(std::string value) {
    for (char& ch : value) {
        const bool allowed =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9');
        if (!allowed) {
            ch = '_';
        }
    }
    while (value.find("__") != std::string::npos) {
        value.replace(value.find("__"), 2, "_");
    }
    if (!value.empty() && value.back() == '_') {
        value.pop_back();
    }
    return value.empty() ? std::string("unknown_gpu") : value;
}

std::filesystem::path PipelineCachePathFor(
    const VkPhysicalDeviceProperties& properties
) {
    const std::string deviceName = SanitizeDeviceName(properties.deviceName);
    const std::string fileName =
        "pipeline_cache_v1_" +
        deviceName +
        "_vendor" + HexU32(properties.vendorID) +
        "_device" + HexU32(properties.deviceID) +
        "_driver" + HexU32(properties.driverVersion) +
        "_api" + HexU32(properties.apiVersion) +
        "_uuid" + PipelineCacheUuidHex(properties.pipelineCacheUUID) +
        ".bin";
    return PipelineCacheDirectoryFromEnvironment() / fileName;
}

std::vector<char> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }

    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) {
        return {};
    }

    std::vector<char> data(static_cast<std::size_t>(fileSize));
    file.seekg(0);
    file.read(data.data(), fileSize);
    if (!file) {
        return {};
    }
    return data;
}

float ElapsedMilliseconds(std::chrono::steady_clock::time_point startTime) {
    using Milliseconds = std::chrono::duration<float, std::milli>;
    return std::chrono::duration_cast<Milliseconds>(
        std::chrono::steady_clock::now() - startTime
    ).count();
}

}

VulkanPipelineCache::VulkanPipelineCache(
    VkDevice device,
    const VkPhysicalDeviceProperties& physicalDeviceProperties
) : m_Device(device),
    m_CachePath(PipelineCachePathFor(physicalDeviceProperties)),
    m_Enabled(PipelineCacheEnabledFromEnvironment()) {
    if (!m_Enabled || m_Device == VK_NULL_HANDLE) {
        return;
    }

    const bool trace = PipelineCacheTraceEnabled();
    const auto startTime = std::chrono::steady_clock::now();
    std::vector<char> initialData;
    if (!PipelineCacheResetRequested()) {
        initialData = ReadBinaryFile(m_CachePath);
    }

    VkPipelineCacheCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    createInfo.initialDataSize = initialData.size();
    createInfo.pInitialData = initialData.empty() ? nullptr : initialData.data();

    VkResult result = vkCreatePipelineCache(
        m_Device,
        &createInfo,
        nullptr,
        &m_PipelineCache
    );
    if (result != VK_SUCCESS && !initialData.empty()) {
        createInfo.initialDataSize = 0;
        createInfo.pInitialData = nullptr;
        result = vkCreatePipelineCache(m_Device, &createInfo, nullptr, &m_PipelineCache);
    }

    if (result != VK_SUCCESS) {
        m_PipelineCache = VK_NULL_HANDLE;
        m_Enabled = false;
        if (trace) {
            std::cout << "[pipeline-cache] disabled create_result="
                << static_cast<int>(result) << " path=" << m_CachePath.string()
                << std::endl;
        }
        return;
    }

    m_LoadedFromDisk = !initialData.empty();
    if (trace) {
        std::cout << "[pipeline-cache] create loaded=" << (m_LoadedFromDisk ? 1 : 0)
            << " bytes=" << initialData.size()
            << " elapsed_ms=" << ElapsedMilliseconds(startTime)
            << " path=" << m_CachePath.string() << std::endl;
    }
}

VulkanPipelineCache::~VulkanPipelineCache() {
    Release();
}

VkPipelineCache VulkanPipelineCache::Handle() const {
    return m_Enabled ? m_PipelineCache : VK_NULL_HANDLE;
}

bool VulkanPipelineCache::Enabled() const {
    return m_Enabled;
}

bool VulkanPipelineCache::LoadedFromDisk() const {
    return m_LoadedFromDisk;
}

bool VulkanPipelineCache::SavedToDisk() const {
    return m_SavedToDisk;
}

const std::filesystem::path& VulkanPipelineCache::CachePath() const {
    return m_CachePath;
}

void VulkanPipelineCache::Save() {
    if (!m_Enabled ||
        m_Device == VK_NULL_HANDLE ||
        m_PipelineCache == VK_NULL_HANDLE ||
        m_SavedToDisk) {
        return;
    }

    const bool trace = PipelineCacheTraceEnabled();
    const auto startTime = std::chrono::steady_clock::now();
    std::size_t dataSize = 0;
    VkResult result = vkGetPipelineCacheData(
        m_Device,
        m_PipelineCache,
        &dataSize,
        nullptr
    );
    if (result != VK_SUCCESS || dataSize == 0) {
        if (trace) {
            std::cout << "[pipeline-cache] save_skipped query_result="
                << static_cast<int>(result) << " bytes=" << dataSize
                << " path=" << m_CachePath.string() << std::endl;
        }
        return;
    }

    std::vector<char> data(dataSize);
    result = vkGetPipelineCacheData(
        m_Device,
        m_PipelineCache,
        &dataSize,
        data.data()
    );
    if (result != VK_SUCCESS) {
        if (trace) {
            std::cout << "[pipeline-cache] save_skipped read_result="
                << static_cast<int>(result) << " path=" << m_CachePath.string()
                << std::endl;
        }
        return;
    }
    data.resize(dataSize);

    std::error_code ec;
    std::filesystem::create_directories(m_CachePath.parent_path(), ec);
    if (ec) {
        if (trace) {
            std::cout << "[pipeline-cache] save_skipped mkdir_error="
                << ec.message() << " path=" << m_CachePath.string() << std::endl;
        }
        return;
    }

    const std::filesystem::path temporaryPath = m_CachePath.string() + ".tmp";
    {
        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            if (trace) {
                std::cout << "[pipeline-cache] save_skipped open_failed path="
                    << temporaryPath.string() << std::endl;
            }
            return;
        }
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file) {
            return;
        }
    }

    std::filesystem::remove(m_CachePath, ec);
    ec.clear();
    std::filesystem::rename(temporaryPath, m_CachePath, ec);
    if (ec) {
        if (trace) {
            std::cout << "[pipeline-cache] save_skipped rename_error="
                << ec.message() << " path=" << m_CachePath.string() << std::endl;
        }
        return;
    }

    m_SavedToDisk = true;
    if (trace) {
        std::cout << "[pipeline-cache] saved bytes=" << data.size()
            << " elapsed_ms=" << ElapsedMilliseconds(startTime)
            << " path=" << m_CachePath.string() << std::endl;
    }
}

void VulkanPipelineCache::Release() {
    Save();
    if (m_PipelineCache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(m_Device, m_PipelineCache, nullptr);
        m_PipelineCache = VK_NULL_HANDLE;
    }
}

}
