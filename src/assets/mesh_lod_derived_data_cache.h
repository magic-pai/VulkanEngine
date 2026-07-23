#pragma once

#include "core.h"
#include "renderer/vulkan/mesh_lod.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace se {

enum class MeshLodCacheFallbackReason : u32 {
    None = 0,
    Disabled = 1,
    ForcedRebuild = 2,
    NoCacheableMeshes = 3,
    SourceUnreadable = 4,
    Missing = 5,
    IoError = 6,
    InvalidHeader = 7,
    KeyMismatch = 8,
    IntegrityFailure = 9,
    TopologyMismatch = 10,
    DecodeFailure = 11,
    WriteFailure = 12,
    InMemoryReuse = 13
};

struct MeshLodCacheSourceMesh {
    u32 vertexCount = 0;
    u32 indexCount = 0;
    bool skinned = false;
};

struct MeshLodDerivedDataCacheOptions {
    bool enabled = true;
    bool forceRebuild = false;
    bool generateTangents = true;
    f32 targetMaxExtent = 2.1f;
    std::filesystem::path directory;
    std::string keySalt;
};

struct MeshLodDerivedDataCacheStats {
    u32 requested = 0;
    u32 hit = 0;
    u32 miss = 0;
    u32 rejected = 0;
    u32 written = 0;
    u32 cacheableMeshCount = 0;
    u32 decodedChainCount = 0;
    u32 decodedLevelCount = 0;
    MeshLodCacheFallbackReason fallbackReason = MeshLodCacheFallbackReason::None;
    u64 sourceFileBytes = 0;
    u64 sourceHash = 0;
    u64 settingsHash = 0;
    u64 cacheKeyHash = 0;
    u64 rawBytes = 0;
    u64 encodedBytes = 0;
    u64 fileBytes = 0;
    u64 sourceHashMicroseconds = 0;
    u64 readMicroseconds = 0;
    u64 decodeMicroseconds = 0;
    u64 buildMicroseconds = 0;
    u64 writeMicroseconds = 0;
    u64 importMicroseconds = 0;
    u64 totalLoadMicroseconds = 0;
};

class MeshLodDerivedDataCache {
public:
    MeshLodDerivedDataCache(
        std::filesystem::path sourcePath,
        std::vector<MeshLodCacheSourceMesh> sourceMeshes,
        MeshLodDerivedDataCacheOptions options
    );

    bool TryLoad(std::vector<GeneratedMeshLodChain>& chains);
    bool Store(std::span<const GeneratedMeshLodChain> chains);

    void SetBuildMicroseconds(u64 microseconds);
    void SetImportMicroseconds(u64 microseconds);
    void SetTotalLoadMicroseconds(u64 microseconds);
    const MeshLodDerivedDataCacheStats& Stats() const;
    const std::filesystem::path& CachePath() const;

private:
    bool PrepareKey();

    std::filesystem::path m_SourcePath;
    std::vector<MeshLodCacheSourceMesh> m_SourceMeshes;
    MeshLodDerivedDataCacheOptions m_Options;
    MeshLodDerivedDataCacheStats m_Stats{};
    std::filesystem::path m_CachePath;
    bool m_KeyReady = false;
};

}
