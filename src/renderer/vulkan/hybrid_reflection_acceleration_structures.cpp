#include "renderer/vulkan/hybrid_reflection_acceleration_structures.h"

#include "renderer/render_queue.h"
#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/material.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/renderer_stats.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace se {

namespace {

constexpr u32 kMaxBlasCacheEntries = 1024u;
constexpr u32 kMaxTlasInstances = 4096u;
constexpr u32 kInvalidFrameIndex = std::numeric_limits<u32>::max();

VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1u) {
        return value;
    }
    return (value + alignment - 1u) & ~(alignment - 1u);
}

u32 NextPowerOfTwo(u32 value) {
    u32 result = 1u;
    while (result < value && result < kMaxTlasInstances) {
        result <<= 1u;
    }
    return std::min(result, kMaxTlasInstances);
}

VkTransformMatrixKHR ToAccelerationStructureTransform(const glm::mat4& model) {
    VkTransformMatrixKHR transform{};
    for (u32 row = 0u; row < 3u; ++row) {
        for (u32 column = 0u; column < 4u; ++column) {
            transform.matrix[row][column] = model[column][row];
        }
    }
    return transform;
}

bool IsSkinnedCommand(const RenderCommand& command) {
    return !command.bonePaletteResourceId.empty();
}

bool IsOpaqueCommand(const RenderCommand& command) {
    return command.material == nullptr ||
        command.material->Properties().alphaMode == MaterialAlphaMode::Opaque;
}

}

struct VulkanHybridReflectionAccelerationStructures::Impl {
    enum class BlasState : u32 {
        Allocated = 0,
        Scheduled = 1,
        Ready = 2
    };

    struct BlasEntry {
        const VulkanMesh* mesh = nullptr;
        std::unique_ptr<VulkanBuffer> storage;
        std::unique_ptr<VulkanBuffer> scratch;
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        VkDeviceAddress address = 0;
        VkDeviceAddress scratchAddress = 0;
        VkDeviceSize storageBytes = 0;
        VkDeviceSize scratchBytes = 0;
        u32 primitiveCount = 0;
        u32 buildFrameIndex = kInvalidFrameIndex;
        BlasState state = BlasState::Allocated;
    };

    struct FrameTlas {
        std::unique_ptr<VulkanBuffer> instances;
        std::unique_ptr<VulkanBuffer> storage;
        std::unique_ptr<VulkanBuffer> scratch;
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        VkDeviceAddress address = 0;
        VkDeviceAddress scratchAddress = 0;
        VkDeviceSize storageBytes = 0;
        VkDeviceSize scratchBytes = 0;
        VkDeviceSize instanceBytes = 0;
        u32 capacity = 0;
        u32 preparedInstanceCount = 0;
        bool built = false;
        bool prepared = false;
    };

    Impl(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        u32 frameCount
    ) : device(device), physicalDevice(physicalDevice), frames(frameCount) {
        createAccelerationStructure = reinterpret_cast<
            PFN_vkCreateAccelerationStructureKHR
        >(vkGetDeviceProcAddr(device.Handle(), "vkCreateAccelerationStructureKHR"));
        destroyAccelerationStructure = reinterpret_cast<
            PFN_vkDestroyAccelerationStructureKHR
        >(vkGetDeviceProcAddr(device.Handle(), "vkDestroyAccelerationStructureKHR"));
        getBuildSizes = reinterpret_cast<
            PFN_vkGetAccelerationStructureBuildSizesKHR
        >(vkGetDeviceProcAddr(device.Handle(), "vkGetAccelerationStructureBuildSizesKHR"));
        getAccelerationStructureAddress = reinterpret_cast<
            PFN_vkGetAccelerationStructureDeviceAddressKHR
        >(vkGetDeviceProcAddr(device.Handle(), "vkGetAccelerationStructureDeviceAddressKHR"));
        cmdBuildAccelerationStructures = reinterpret_cast<
            PFN_vkCmdBuildAccelerationStructuresKHR
        >(vkGetDeviceProcAddr(device.Handle(), "vkCmdBuildAccelerationStructuresKHR"));

        if (createAccelerationStructure == nullptr ||
            destroyAccelerationStructure == nullptr ||
            getBuildSizes == nullptr ||
            getAccelerationStructureAddress == nullptr ||
            cmdBuildAccelerationStructures == nullptr) {
            throw std::runtime_error(
                "Vulkan Ray Query acceleration-structure entry points are unavailable"
            );
        }

        VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationProperties{};
        accelerationProperties.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &accelerationProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice.Handle(), &properties);
        scratchAlignment = std::max<VkDeviceSize>(
            accelerationProperties.minAccelerationStructureScratchOffsetAlignment,
            1u
        );
    }

    ~Impl() {
        for (FrameTlas& frame : frames) {
            DestroyAccelerationStructure(frame.handle);
        }
        for (auto& entry : blasCache) {
            DestroyAccelerationStructure(entry.second->handle);
        }
    }

    void DestroyAccelerationStructure(VkAccelerationStructureKHR& handle) {
        if (handle != VK_NULL_HANDLE) {
            destroyAccelerationStructure(device.Handle(), handle, nullptr);
            handle = VK_NULL_HANDLE;
        }
    }

    VkDeviceAddress AccelerationStructureAddress(
        VkAccelerationStructureKHR handle
    ) const {
        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.accelerationStructure = handle;
        return getAccelerationStructureAddress(device.Handle(), &addressInfo);
    }

    void CreateAccelerationStructureResource(
        VkAccelerationStructureTypeKHR type,
        VkDeviceSize size,
        std::unique_ptr<VulkanBuffer>& storage,
        VkAccelerationStructureKHR& handle
    ) {
        storage = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            size,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
        );
        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = storage->Handle();
        createInfo.size = size;
        createInfo.type = type;
        if (createAccelerationStructure(
                device.Handle(),
                &createInfo,
                nullptr,
                &handle
            ) != VK_SUCCESS) {
            storage.reset();
            throw std::runtime_error("Failed to create Vulkan acceleration structure");
        }
    }

    std::unique_ptr<VulkanBuffer> CreateScratchBuffer(
        VkDeviceSize requiredBytes,
        VkDeviceAddress& alignedAddress
    ) {
        const VkDeviceSize allocatedBytes = requiredBytes + scratchAlignment;
        auto buffer = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            allocatedBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
        );
        alignedAddress = AlignUp(buffer->DeviceAddress(), scratchAlignment);
        return buffer;
    }

    std::unique_ptr<BlasEntry> CreateBlasEntry(const VulkanMesh& mesh) {
        const u32 primitiveCount = mesh.IndexCount() / 3u;
        if (primitiveCount == 0u) {
            return nullptr;
        }

        VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
        triangles.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = mesh.VertexDeviceAddress();
        triangles.vertexStride = mesh.VertexStride();
        triangles.maxVertex = mesh.VertexCount() - 1u;
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = mesh.IndexDeviceAddress();

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles = triangles;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags =
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1u;
        buildInfo.pGeometries = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizes{};
        sizes.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        getBuildSizes(
            device.Handle(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo,
            &primitiveCount,
            &sizes
        );
        if (sizes.accelerationStructureSize == 0u || sizes.buildScratchSize == 0u) {
            return nullptr;
        }

        auto entry = std::make_unique<BlasEntry>();
        entry->mesh = &mesh;
        entry->primitiveCount = primitiveCount;
        entry->storageBytes = sizes.accelerationStructureSize;
        entry->scratchBytes = sizes.buildScratchSize;
        CreateAccelerationStructureResource(
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            sizes.accelerationStructureSize,
            entry->storage,
            entry->handle
        );
        entry->scratch = CreateScratchBuffer(
            sizes.buildScratchSize,
            entry->scratchAddress
        );
        entry->address = AccelerationStructureAddress(entry->handle);
        if (entry->address == 0u) {
            DestroyAccelerationStructure(entry->handle);
            return nullptr;
        }
        return entry;
    }

    void EnsureTlasCapacity(FrameTlas& frame, u32 requiredCapacity) {
        if (frame.handle != VK_NULL_HANDLE && frame.capacity >= requiredCapacity) {
            return;
        }

        DestroyAccelerationStructure(frame.handle);
        frame.instances.reset();
        frame.storage.reset();
        frame.scratch.reset();
        frame = FrameTlas{};
        frame.capacity = NextPowerOfTwo(requiredCapacity);

        frame.instances = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            sizeof(VkAccelerationStructureInstanceKHR) * frame.capacity,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
        );

        VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
        instancesData.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = frame.instances->DeviceAddress();
        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances = instancesData;
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags =
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1u;
        buildInfo.pGeometries = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizes{};
        sizes.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        getBuildSizes(
            device.Handle(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo,
            &frame.capacity,
            &sizes
        );
        CreateAccelerationStructureResource(
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            sizes.accelerationStructureSize,
            frame.storage,
            frame.handle
        );
        frame.storageBytes = sizes.accelerationStructureSize;
        frame.scratchBytes = std::max(
            sizes.buildScratchSize,
            sizes.updateScratchSize
        );
        frame.scratch = CreateScratchBuffer(
            frame.scratchBytes,
            frame.scratchAddress
        );
        frame.instanceBytes = frame.instances->Size();
        frame.address = AccelerationStructureAddress(frame.handle);
    }

    void PrepareFrame(
        u32 frameIndex,
        std::span<const RenderCommand> renderCommands,
        RendererHybridReflectionStats& stats
    ) {
        if (frameIndex >= frames.size()) {
            throw std::runtime_error("Hybrid reflection TLAS frame index is out of range");
        }

        for (auto& cached : blasCache) {
            BlasEntry& entry = *cached.second;
            if (entry.state == BlasState::Scheduled &&
                entry.buildFrameIndex == frameIndex) {
                entry.scratch.reset();
                entry.scratchAddress = 0u;
                entry.state = BlasState::Ready;
            }
        }

        stats.accelerationStructureContractVersion = 1u;
        stats.fullSceneCommandCount = static_cast<u32>(renderCommands.size());
        stats.opaqueRigidCommandCount = 0u;
        stats.skinnedFallbackCount = 0u;
        stats.alphaFallbackCount = 0u;
        stats.invalidGeometryCount = 0u;
        stats.instanceOverflowCount = 0u;
        stats.blasBuildCount = 0u;
        stats.blasReuseCount = 0u;
        stats.tlasBuildCount = 0u;
        stats.tlasUpdateCount = 0u;

        std::vector<VkAccelerationStructureInstanceKHR> instances;
        instances.reserve(std::min<std::size_t>(
            renderCommands.size(),
            kMaxTlasInstances
        ));
        std::unordered_set<const VulkanMesh*> meshesUsedThisFrame;
        for (const RenderCommand& command : renderCommands) {
            if (command.mesh == nullptr || !command.mesh->Is3D()) {
                ++stats.invalidGeometryCount;
                continue;
            }
            if (IsSkinnedCommand(command)) {
                ++stats.skinnedFallbackCount;
                continue;
            }
            if (!IsOpaqueCommand(command)) {
                ++stats.alphaFallbackCount;
                continue;
            }
            if (!command.mesh->AccelerationStructureInputReady()) {
                ++stats.invalidGeometryCount;
                continue;
            }
            if (instances.size() >= kMaxTlasInstances) {
                ++stats.instanceOverflowCount;
                continue;
            }

            auto found = blasCache.find(command.mesh);
            if (found == blasCache.end()) {
                if (blasCache.size() >= kMaxBlasCacheEntries) {
                    ++stats.instanceOverflowCount;
                    continue;
                }
                std::unique_ptr<BlasEntry> created = CreateBlasEntry(*command.mesh);
                if (created == nullptr) {
                    ++stats.invalidGeometryCount;
                    continue;
                }
                found = blasCache.emplace(command.mesh, std::move(created)).first;
            } else {
                ++stats.blasReuseCount;
            }

            BlasEntry& blas = *found->second;
            VkAccelerationStructureInstanceKHR instance{};
            instance.transform = ToAccelerationStructureTransform(command.model);
            instance.instanceCustomIndex = static_cast<u32>(instances.size()) & 0x00ffffffu;
            instance.mask = 0xffu;
            instance.instanceShaderBindingTableRecordOffset = 0u;
            instance.flags = command.material != nullptr &&
                    command.material->Properties().doubleSided
                ? VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR
                : 0u;
            instance.accelerationStructureReference = blas.address;
            instances.push_back(instance);
            meshesUsedThisFrame.insert(command.mesh);
            ++stats.opaqueRigidCommandCount;
        }

        FrameTlas& frame = frames[frameIndex];
        frame.prepared = false;
        frame.preparedInstanceCount = static_cast<u32>(instances.size());
        if (!instances.empty()) {
            EnsureTlasCapacity(frame, static_cast<u32>(instances.size()));
            frame.instances->Upload(std::as_bytes(std::span(instances)));
            frame.prepared = true;
        }

        stats.blasCacheCount = static_cast<u32>(blasCache.size());
        stats.blasReadyCount = 0u;
        stats.blasPrimitiveCount = 0u;
        stats.blasStorageBytes = 0u;
        stats.blasScratchBytes = 0u;
        for (const auto& cached : blasCache) {
            const BlasEntry& entry = *cached.second;
            if (entry.state != BlasState::Allocated) {
                ++stats.blasReadyCount;
            }
            stats.blasPrimitiveCount += entry.primitiveCount;
            stats.blasStorageBytes += entry.storageBytes;
            stats.blasScratchBytes +=
                entry.scratch != nullptr ? entry.scratch->Size() : 0u;
        }
        stats.frameUniqueBlasCount = static_cast<u32>(meshesUsedThisFrame.size());
        stats.tlasInstanceCount = frame.preparedInstanceCount;
        stats.tlasInstanceCapacity = frame.capacity;
        stats.tlasStorageBytes = frame.storageBytes;
        stats.tlasScratchBytes =
            frame.scratch != nullptr ? frame.scratch->Size() : 0u;
        stats.tlasInstanceBufferBytes = frame.instanceBytes;
        stats.tlasAddressReady = frame.address != 0u ? 1u : 0u;
        stats.runtimeResourcesReady = 0u;
        stats.active = 0u;
        stats.fallbackReason = 6u;
    }

    void RecordBuilds(
        VkCommandBuffer commandBuffer,
        u32 frameIndex,
        RendererHybridReflectionStats& stats
    ) {
        if (frameIndex >= frames.size()) {
            return;
        }
        FrameTlas& frame = frames[frameIndex];
        if (!frame.prepared || frame.preparedInstanceCount == 0u) {
            return;
        }

        VkBufferMemoryBarrier hostInstanceBarrier{};
        hostInstanceBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hostInstanceBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        hostInstanceBarrier.dstAccessMask =
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        hostInstanceBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostInstanceBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostInstanceBarrier.buffer = frame.instances->Handle();
        hostInstanceBarrier.offset = 0u;
        hostInstanceBarrier.size = frame.instanceBytes;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0u,
            0u,
            nullptr,
            1u,
            &hostInstanceBarrier,
            0u,
            nullptr
        );

        for (auto& cached : blasCache) {
            BlasEntry& entry = *cached.second;
            if (entry.state != BlasState::Allocated || entry.scratch == nullptr) {
                continue;
            }

            VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
            triangles.sType =
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            triangles.vertexData.deviceAddress = entry.mesh->VertexDeviceAddress();
            triangles.vertexStride = entry.mesh->VertexStride();
            triangles.maxVertex = entry.mesh->VertexCount() - 1u;
            triangles.indexType = VK_INDEX_TYPE_UINT32;
            triangles.indexData.deviceAddress = entry.mesh->IndexDeviceAddress();
            VkAccelerationStructureGeometryKHR geometry{};
            geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            geometry.geometry.triangles = triangles;
            VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
            buildInfo.sType =
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildInfo.flags =
                VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildInfo.dstAccelerationStructure = entry.handle;
            buildInfo.geometryCount = 1u;
            buildInfo.pGeometries = &geometry;
            buildInfo.scratchData.deviceAddress = entry.scratchAddress;
            VkAccelerationStructureBuildRangeInfoKHR range{};
            range.primitiveCount = entry.primitiveCount;
            const VkAccelerationStructureBuildRangeInfoKHR* rangePointer = &range;
            cmdBuildAccelerationStructures(
                commandBuffer,
                1u,
                &buildInfo,
                &rangePointer
            );
            entry.state = BlasState::Scheduled;
            entry.buildFrameIndex = frameIndex;
            ++stats.blasBuildCount;
        }

        VkMemoryBarrier blasToTlasBarrier{};
        blasToTlasBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        blasToTlasBarrier.srcAccessMask =
            VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        blasToTlasBarrier.dstAccessMask =
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0u,
            1u,
            &blasToTlasBarrier,
            0u,
            nullptr,
            0u,
            nullptr
        );

        VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
        instancesData.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = frame.instances->DeviceAddress();
        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances = instancesData;
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags =
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        buildInfo.mode = frame.built
            ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
            : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.srcAccelerationStructure = frame.built
            ? frame.handle
            : VK_NULL_HANDLE;
        buildInfo.dstAccelerationStructure = frame.handle;
        buildInfo.geometryCount = 1u;
        buildInfo.pGeometries = &geometry;
        buildInfo.scratchData.deviceAddress = frame.scratchAddress;
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = frame.preparedInstanceCount;
        const VkAccelerationStructureBuildRangeInfoKHR* rangePointer = &range;
        cmdBuildAccelerationStructures(
            commandBuffer,
            1u,
            &buildInfo,
            &rangePointer
        );
        if (frame.built) {
            stats.tlasUpdateCount = 1u;
        } else {
            stats.tlasBuildCount = 1u;
        }
        frame.built = true;

        VkMemoryBarrier tlasConsumerBarrier{};
        tlasConsumerBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        tlasConsumerBarrier.srcAccessMask =
            VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        tlasConsumerBarrier.dstAccessMask =
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
            VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0u,
            1u,
            &tlasConsumerBarrier,
            0u,
            nullptr,
            0u,
            nullptr
        );

        stats.blasReadyCount = 0u;
        for (const auto& cached : blasCache) {
            if (cached.second->state != BlasState::Allocated) {
                ++stats.blasReadyCount;
            }
        }
        stats.accelerationStructureResourcesReady = 1u;
        stats.runtimeResourcesReady = 1u;
        stats.active = 0u;
        stats.fallbackReason = 7u;
    }

    const VulkanDevice& device;
    const VulkanPhysicalDevice& physicalDevice;
    PFN_vkCreateAccelerationStructureKHR createAccelerationStructure = nullptr;
    PFN_vkDestroyAccelerationStructureKHR destroyAccelerationStructure = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR getBuildSizes = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR
        getAccelerationStructureAddress = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAccelerationStructures = nullptr;
    VkDeviceSize scratchAlignment = 1u;
    std::unordered_map<const VulkanMesh*, std::unique_ptr<BlasEntry>> blasCache;
    std::vector<FrameTlas> frames;
};

VulkanHybridReflectionAccelerationStructures::
VulkanHybridReflectionAccelerationStructures(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    u32 frameCount
) : m_Impl(std::make_unique<Impl>(device, physicalDevice, frameCount)) {
    if (frameCount == 0u) {
        throw std::runtime_error(
            "Hybrid reflection acceleration structures need at least one frame"
        );
    }
}

VulkanHybridReflectionAccelerationStructures::
~VulkanHybridReflectionAccelerationStructures() = default;

void VulkanHybridReflectionAccelerationStructures::PrepareFrame(
    u32 frameIndex,
    std::span<const RenderCommand> renderCommands,
    RendererHybridReflectionStats& stats
) {
    m_Impl->PrepareFrame(frameIndex, renderCommands, stats);
}

void VulkanHybridReflectionAccelerationStructures::RecordBuilds(
    VkCommandBuffer commandBuffer,
    u32 frameIndex,
    RendererHybridReflectionStats& stats
) {
    m_Impl->RecordBuilds(commandBuffer, frameIndex, stats);
}

VkAccelerationStructureKHR
VulkanHybridReflectionAccelerationStructures::TopLevelHandle(u32 frameIndex) const {
    if (frameIndex >= m_Impl->frames.size()) {
        return VK_NULL_HANDLE;
    }
    return m_Impl->frames[frameIndex].handle;
}

}
