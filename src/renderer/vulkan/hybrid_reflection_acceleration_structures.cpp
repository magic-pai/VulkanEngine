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
constexpr u32 kInvalidFrameIndex = std::numeric_limits<u32>::max();

VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1u) {
        return value;
    }
    return (value + alignment - 1u) & ~(alignment - 1u);
}

u32 NextPowerOfTwo(u32 value) {
    u32 result = 1u;
    while (result < value && result < kMaxHybridReflectionInstances) {
        result <<= 1u;
    }
    return std::min(result, kMaxHybridReflectionInstances);
}

std::array<u32, 2> SplitDeviceAddress(VkDeviceAddress address) {
    return {
        static_cast<u32>(address & 0xffffffffull),
        static_cast<u32>(address >> 32u)
    };
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
        u32 materialCount = 0;
        std::vector<HybridReflectionInstanceMetadata> instanceMetadata;
        std::vector<const VulkanMaterial*> instanceMaterials;
        std::vector<u32> instanceSubmissionIndices;
        std::vector<HybridReflectionInstanceAuditRecord> instanceAuditRecords;
        bool built = false;
        bool prepared = false;
    };

    Impl(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        u32 frameCount
    ) : device(device), physicalDevice(physicalDevice), frames(frameCount) {
        fullAuditRequested = VulkanEnvironmentFlagEnabled(
            "SE_HYBRID_REFLECTIONS_FULL_AUDIT"
        );
#if !defined(NDEBUG)
        forceFrontCounterClockwise = VulkanEnvironmentFlagEnabled(
            "SE_HYBRID_REFLECTIONS_TLAS_FRONT_COUNTERCLOCKWISE"
        );
#endif
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

        stats.accelerationStructureContractVersion = 2u;
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
            kMaxHybridReflectionInstances
        ));
        std::vector<HybridReflectionInstanceMetadata> instanceMetadata;
        instanceMetadata.reserve(instances.capacity());
        std::vector<u32> instanceSubmissionIndices;
        instanceSubmissionIndices.reserve(instances.capacity());
        std::vector<HybridReflectionInstanceAuditRecord> instanceAuditRecords;
        if (fullAuditRequested) {
            instanceAuditRecords.reserve(renderCommands.size());
        }
        std::unordered_map<const VulkanMaterial*, u32> materialIndices;
        std::vector<const VulkanMaterial*> instanceMaterials;
        std::unordered_set<const VulkanMesh*> meshesUsedThisFrame;
        for (const RenderCommand& command : renderCommands) {
            HybridReflectionInstanceAuditRecord* audit = nullptr;
            if (fullAuditRequested) {
                instanceAuditRecords.emplace_back();
                audit = &instanceAuditRecords.back();
                audit->submissionIndex = static_cast<u32>(
                    std::min<std::size_t>(
                        command.submissionIndex,
                        std::numeric_limits<u32>::max()
                    )
                );
                audit->reflectionAuditObjectId =
                    command.reflectionAuditObjectId;
#if !defined(NDEBUG)
                audit->renderIdentity = command.renderableIdentity;
                audit->renderableName = std::string(command.debugRenderableName);
#else
                audit->renderIdentity = command.renderableIdentity;
                audit->renderableName =
                    "submission#" + std::to_string(audit->submissionIndex);
#endif
                audit->meshIdentity = static_cast<u64>(command.meshSortKey);
                audit->materialIdentity =
                    static_cast<u64>(command.materialSortKey);
                for (u32 column = 0u; column < 4u; ++column) {
                    for (u32 row = 0u; row < 4u; ++row) {
                        audit->model[column * 4u + row] =
                            command.model[column][row];
                    }
                }
                if (command.worldBounds.valid) {
                    for (u32 component = 0u; component < 3u; ++component) {
                        audit->boundsMin[component] =
                            command.worldBounds.min[component];
                        audit->boundsMax[component] =
                            command.worldBounds.max[component];
                    }
                }
                if (command.mesh != nullptr) {
                    audit->vertexCount = command.mesh->VertexCount();
                    audit->indexCount = command.mesh->IndexCount();
                    audit->vertexStride = command.mesh->VertexStride();
                }
                audit->castShadow = command.castShadow ? 1u : 0u;
                audit->reflectionCaptureVisible =
                    command.reflectionCaptureVisible ? 1u : 0u;
                const f32 m00 = command.model[0][0];
                const f32 m01 = command.model[1][0];
                const f32 m02 = command.model[2][0];
                const f32 m10 = command.model[0][1];
                const f32 m11 = command.model[1][1];
                const f32 m12 = command.model[2][1];
                const f32 m20 = command.model[0][2];
                const f32 m21 = command.model[1][2];
                const f32 m22 = command.model[2][2];
                audit->modelDeterminant =
                    m00 * (m11 * m22 - m12 * m21) -
                    m01 * (m10 * m22 - m12 * m20) +
                    m02 * (m10 * m21 - m11 * m20);
                if (command.material != nullptr) {
                    const MaterialProperties& properties =
                        command.material->Properties();
                    audit->baseColor = properties.baseColorFactor;
                    audit->emissive = properties.emissiveFactor;
                    audit->alphaMode = static_cast<u32>(properties.alphaMode);
                    audit->renderClass =
                        static_cast<u32>(properties.renderClass);
                    audit->doubleSided = properties.doubleSided ? 1u : 0u;
                    audit->metallic = properties.cameraControls[0];
                    audit->roughness = properties.cameraControls[1];
                    audit->textureMix = properties.textureMix;
                }
            }
            if (command.mesh == nullptr || !command.mesh->Is3D()) {
                if (audit != nullptr) {
                    audit->exclusionReason = 1u;
                }
                ++stats.invalidGeometryCount;
                continue;
            }
            if (IsSkinnedCommand(command)) {
                if (audit != nullptr) {
                    audit->exclusionReason = 2u;
                }
                ++stats.skinnedFallbackCount;
                continue;
            }
            if (!IsOpaqueCommand(command)) {
                if (audit != nullptr) {
                    audit->exclusionReason = 3u;
                }
                ++stats.alphaFallbackCount;
                continue;
            }
            if (!command.mesh->AccelerationStructureInputReady()) {
                if (audit != nullptr) {
                    audit->exclusionReason = 4u;
                }
                ++stats.invalidGeometryCount;
                continue;
            }
            if (instances.size() >= kMaxHybridReflectionInstances) {
                if (audit != nullptr) {
                    audit->exclusionReason = 5u;
                }
                ++stats.instanceOverflowCount;
                continue;
            }

            auto found = blasCache.find(command.mesh);
            if (found == blasCache.end()) {
                if (blasCache.size() >= kMaxBlasCacheEntries) {
                    if (audit != nullptr) {
                        audit->exclusionReason = 6u;
                    }
                    ++stats.instanceOverflowCount;
                    continue;
                }
                std::unique_ptr<BlasEntry> created = CreateBlasEntry(*command.mesh);
                if (created == nullptr) {
                    if (audit != nullptr) {
                        audit->exclusionReason = 7u;
                    }
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
            // SelfEngine's Vulkan projection flips clip-space Y. The raster
            // front-face contract is therefore counter-clockwise after the
            // projection, while the object-space mesh winding presented to
            // ray tracing uses Vulkan's default clockwise front convention.
            // Keep the two contracts aligned so culling selects the nearest
            // exterior surface instead of the far side of closed meshes.
            instance.flags = forceFrontCounterClockwise
                ? VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR
                : 0u;
            if (command.material != nullptr &&
                command.material->Properties().doubleSided) {
                instance.flags |=
                    VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            }
            instance.accelerationStructureReference = blas.address;
            u32 materialIndex = 0u;
            if (command.material != nullptr) {
                const auto [material, inserted] = materialIndices.emplace(
                    command.material,
                    static_cast<u32>(materialIndices.size()) + 1u
                );
                if (inserted) {
                    instanceMaterials.push_back(command.material);
                }
                materialIndex = material->second;
            }
            HybridReflectionInstanceMetadata metadata{};
            metadata.vertexAddress = SplitDeviceAddress(
                command.mesh->VertexDeviceAddress()
            );
            metadata.indexAddress = SplitDeviceAddress(
                command.mesh->IndexDeviceAddress()
            );
            metadata.vertexCount = command.mesh->VertexCount();
            metadata.indexCount = command.mesh->IndexCount();
            metadata.vertexStride = command.mesh->VertexStride();
            metadata.materialIndex = materialIndex;
            const u32 submissionIndex = static_cast<u32>(std::min<std::size_t>(
                command.submissionIndex,
                std::numeric_limits<u32>::max()
            ));
            metadata.submissionIndex = submissionIndex;
            metadata.reflectionAuditObjectId =
                command.reflectionAuditObjectId;
            if (audit != nullptr) {
                audit->tlasIndex = static_cast<u32>(instances.size());
                audit->materialIndex = materialIndex;
                audit->instanceFlags = instance.flags;
                audit->inTlas = 1u;
                audit->exclusionReason = 0u;
            }
            instances.push_back(instance);
            instanceMetadata.push_back(metadata);
            instanceSubmissionIndices.push_back(submissionIndex);
            meshesUsedThisFrame.insert(command.mesh);
            ++stats.opaqueRigidCommandCount;
        }

        FrameTlas& frame = frames[frameIndex];
        frame.prepared = false;
        frame.preparedInstanceCount = static_cast<u32>(instances.size());
        frame.materialCount = static_cast<u32>(materialIndices.size());
        frame.instanceMetadata = std::move(instanceMetadata);
        frame.instanceMaterials = std::move(instanceMaterials);
        frame.instanceSubmissionIndices = std::move(instanceSubmissionIndices);
        frame.instanceAuditRecords = std::move(instanceAuditRecords);
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
        stats.rayQueryInstanceMetadataCount =
            static_cast<u32>(frame.instanceMetadata.size());
        stats.rayQueryInstanceMaterialCount = frame.materialCount;
        stats.rayQueryInstanceAddressReadyCount = static_cast<u32>(
            std::count_if(
                frame.instanceMetadata.begin(),
                frame.instanceMetadata.end(),
                [](const HybridReflectionInstanceMetadata& metadata) {
                    return (metadata.vertexAddress[0] != 0u ||
                            metadata.vertexAddress[1] != 0u) &&
                        (metadata.indexAddress[0] != 0u ||
                            metadata.indexAddress[1] != 0u);
                }
            )
        );
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
    bool fullAuditRequested = false;
    bool forceFrontCounterClockwise = false;
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

std::span<const HybridReflectionInstanceMetadata>
VulkanHybridReflectionAccelerationStructures::InstanceMetadata(
    u32 frameIndex
) const {
    if (frameIndex >= m_Impl->frames.size()) {
        return {};
    }
    return m_Impl->frames[frameIndex].instanceMetadata;
}

std::span<const VulkanMaterial* const>
VulkanHybridReflectionAccelerationStructures::InstanceMaterials(
    u32 frameIndex
) const {
    if (frameIndex >= m_Impl->frames.size()) {
        return {};
    }
    return m_Impl->frames[frameIndex].instanceMaterials;
}

u32 VulkanHybridReflectionAccelerationStructures::
FindInstanceIndexBySubmissionIndex(
    u32 frameIndex,
    u32 submissionIndex
) const {
    if (frameIndex >= m_Impl->frames.size()) {
        return std::numeric_limits<u32>::max();
    }
    const std::vector<u32>& submissionIndices =
        m_Impl->frames[frameIndex].instanceSubmissionIndices;
    const auto found = std::find(
        submissionIndices.begin(),
        submissionIndices.end(),
        submissionIndex
    );
    return found != submissionIndices.end()
        ? static_cast<u32>(std::distance(submissionIndices.begin(), found))
        : std::numeric_limits<u32>::max();
}

std::span<const HybridReflectionInstanceAuditRecord>
VulkanHybridReflectionAccelerationStructures::InstanceAuditRecords(
    u32 frameIndex
) const {
    if (frameIndex >= m_Impl->frames.size()) {
        return {};
    }
    return m_Impl->frames[frameIndex].instanceAuditRecords;
}

}
