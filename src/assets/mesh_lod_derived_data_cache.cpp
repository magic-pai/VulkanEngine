#include "assets/mesh_lod_derived_data_cache.h"

#include "renderer/vulkan/vertex.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <system_error>
#include <type_traits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace se {

namespace {

constexpr std::array<u8, 8> kMagic{ 'S', 'E', 'L', 'O', 'D', 'D', 'C', '2' };
constexpr u32 kSchemaVersion = 2u;
constexpr u32 kHeaderBytes = 80u;
constexpr u32 kVertexCodecVersion = 1u;
constexpr u32 kIndexCodecVersion = 1u;
constexpr u32 kMaxLodLevels = 8u;
constexpr u64 kMaxCacheFileBytes = 1024ull * 1024ull * 1024ull;
constexpr u64 kFnvOffset = 14695981039346656037ull;
constexpr u64 kFnvPrime = 1099511628211ull;

using Clock = std::chrono::steady_clock;

u64 Microseconds(Clock::time_point begin, Clock::time_point end) {
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()
    );
}

u64 HashBytes(std::span<const u8> bytes, u64 seed = kFnvOffset) {
    u64 hash = seed;
    for (u8 value : bytes) {
        hash ^= value;
        hash *= kFnvPrime;
    }
    return hash;
}

template <typename T>
u64 HashValue(u64 seed, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    return HashBytes(
        std::span<const u8>(
            reinterpret_cast<const u8*>(&value),
            sizeof(value)
        ),
        seed
    );
}

u64 HashString(u64 seed, std::string_view value) {
    return HashBytes(
        std::span<const u8>(
            reinterpret_cast<const u8*>(value.data()),
            value.size()
        ),
        seed
    );
}

bool HashFile(
    const std::filesystem::path& path,
    u64& hash,
    u64& byteCount
) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }

    std::vector<u8> buffer(1024u * 1024u);
    hash = kFnvOffset;
    byteCount = 0;
    while (stream) {
        stream.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size())
        );
        const std::streamsize read = stream.gcount();
        if (read <= 0) {
            break;
        }
        hash = HashBytes(
            std::span<const u8>(buffer.data(), static_cast<std::size_t>(read)),
            hash
        );
        byteCount += static_cast<u64>(read);
    }
    return stream.eof() && byteCount > 0u;
}

void AppendU32(std::vector<u8>& bytes, u32 value) {
    for (u32 shift = 0; shift < 32u; shift += 8u) {
        bytes.push_back(static_cast<u8>((value >> shift) & 0xffu));
    }
}

void AppendU64(std::vector<u8>& bytes, u64 value) {
    for (u32 shift = 0; shift < 64u; shift += 8u) {
        bytes.push_back(static_cast<u8>((value >> shift) & 0xffu));
    }
}

void AppendF32(std::vector<u8>& bytes, f32 value) {
    AppendU32(bytes, std::bit_cast<u32>(value));
}

class ByteReader {
public:
    explicit ByteReader(std::span<const u8> bytes)
        : m_Bytes(bytes) {
    }

    bool ReadU32(u32& value) {
        if (Remaining() < sizeof(u32)) {
            return false;
        }
        value = 0;
        for (u32 shift = 0; shift < 32u; shift += 8u) {
            value |= static_cast<u32>(m_Bytes[m_Offset++]) << shift;
        }
        return true;
    }

    bool ReadU64(u64& value) {
        if (Remaining() < sizeof(u64)) {
            return false;
        }
        value = 0;
        for (u32 shift = 0; shift < 64u; shift += 8u) {
            value |= static_cast<u64>(m_Bytes[m_Offset++]) << shift;
        }
        return true;
    }

    bool ReadF32(f32& value) {
        u32 bits = 0;
        if (!ReadU32(bits)) {
            return false;
        }
        value = std::bit_cast<f32>(bits);
        return true;
    }

    bool ReadBytes(u64 count, std::span<const u8>& bytes) {
        if (count > Remaining()) {
            return false;
        }
        bytes = m_Bytes.subspan(m_Offset, static_cast<std::size_t>(count));
        m_Offset += static_cast<std::size_t>(count);
        return true;
    }

    std::size_t Remaining() const { return m_Bytes.size() - m_Offset; }

private:
    std::span<const u8> m_Bytes;
    std::size_t m_Offset = 0;
};

bool ReadFile(
    const std::filesystem::path& path,
    std::vector<u8>& bytes
) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size == 0u || size > kMaxCacheFileBytes) {
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    return stream.good() ||
        (stream.eof() && static_cast<std::size_t>(stream.gcount()) == bytes.size());
}

bool PublishAtomically(
    const std::filesystem::path& temporaryPath,
    const std::filesystem::path& destinationPath
) {
#if defined(_WIN32)
    return MoveFileExW(
        temporaryPath.c_str(),
        destinationPath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    ) != FALSE;
#else
    return std::rename(
        temporaryPath.string().c_str(),
        destinationPath.string().c_str()
    ) == 0;
#endif
}

std::filesystem::path TemporaryPathFor(const std::filesystem::path& path) {
    std::ostringstream suffix;
    suffix << ".tmp.";
#if defined(_WIN32)
    suffix << GetCurrentProcessId() << '.';
#endif
    suffix << Clock::now().time_since_epoch().count();
    return std::filesystem::path(path.string() + suffix.str());
}

std::string Hex64(u64 value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

bool IndicesValid(const GeneratedMeshLodLevel& level) {
    if (level.vertices.empty() || level.indices.empty() ||
        level.indices.size() % 3u != 0u) {
        return false;
    }
    return std::all_of(
        level.indices.begin(),
        level.indices.end(),
        [&](u32 index) { return index < level.vertices.size(); }
    );
}

void PinIndexCodecVersion() {
    static std::once_flag once;
    std::call_once(once, []() {
        meshopt_encodeIndexVersion(static_cast<int>(kIndexCodecVersion));
    });
}

}

MeshLodDerivedDataCache::MeshLodDerivedDataCache(
    std::filesystem::path sourcePath,
    std::vector<MeshLodCacheSourceMesh> sourceMeshes,
    MeshLodDerivedDataCacheOptions options
) : m_SourcePath(std::move(sourcePath)),
    m_SourceMeshes(std::move(sourceMeshes)),
    m_Options(std::move(options)) {
    m_Stats.cacheableMeshCount = static_cast<u32>(std::count_if(
        m_SourceMeshes.begin(),
        m_SourceMeshes.end(),
        [](const MeshLodCacheSourceMesh& mesh) { return !mesh.skinned; }
    ));
    if (!m_Options.enabled) {
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::Disabled;
        return;
    }
    if (m_Stats.cacheableMeshCount == 0u) {
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::NoCacheableMeshes;
        return;
    }
    m_Stats.requested = 1u;
    PrepareKey();
}

bool MeshLodDerivedDataCache::PrepareKey() {
    const Clock::time_point begin = Clock::now();
    m_KeyReady = HashFile(
        m_SourcePath,
        m_Stats.sourceHash,
        m_Stats.sourceFileBytes
    );
    m_Stats.sourceHashMicroseconds = Microseconds(begin, Clock::now());
    if (!m_KeyReady) {
        m_Stats.miss = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::SourceUnreadable;
        return false;
    }

    u64 settingsHash = HashString(kFnvOffset, "SelfEngineMeshLodDDC-v2");
    settingsHash = HashValue(settingsHash, kSchemaVersion);
    settingsHash = HashValue(settingsHash, kVertexCodecVersion);
    settingsHash = HashValue(settingsHash, kIndexCodecVersion);
    settingsHash = HashValue(settingsHash, static_cast<u32>(sizeof(Vertex3D)));
    settingsHash = HashValue(settingsHash, static_cast<u32>(offsetof(Vertex3D, normal)));
    settingsHash = HashValue(settingsHash, static_cast<u32>(offsetof(Vertex3D, texCoord)));
    settingsHash = HashValue(settingsHash, m_Options.targetMaxExtent);
    settingsHash = HashValue(settingsHash, m_Options.generateTangents);
    settingsHash = HashString(settingsHash, m_Options.keySalt);
    for (const MeshLodCacheSourceMesh& mesh : m_SourceMeshes) {
        settingsHash = HashValue(settingsHash, mesh.vertexCount);
        settingsHash = HashValue(settingsHash, mesh.indexCount);
        settingsHash = HashValue(settingsHash, mesh.skinned);
    }
    m_Stats.settingsHash = settingsHash;
    m_Stats.cacheKeyHash = HashValue(m_Stats.sourceHash, settingsHash);
    m_CachePath = m_Options.directory /
        (Hex64(m_Stats.sourceHash) + "-" + Hex64(settingsHash) + ".selod");
    return true;
}

bool MeshLodDerivedDataCache::TryLoad(
    std::vector<GeneratedMeshLodChain>& chains
) {
    chains.clear();
    chains.resize(m_SourceMeshes.size());
    if (m_Stats.requested == 0u || !m_KeyReady) {
        return false;
    }
    if (m_Options.forceRebuild) {
        m_Stats.miss = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::ForcedRebuild;
        return false;
    }

    std::error_code existsError;
    if (!std::filesystem::exists(m_CachePath, existsError) || existsError) {
        m_Stats.miss = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::Missing;
        return false;
    }

    const Clock::time_point readBegin = Clock::now();
    std::vector<u8> fileBytes;
    if (!ReadFile(m_CachePath, fileBytes)) {
        m_Stats.readMicroseconds = Microseconds(readBegin, Clock::now());
        m_Stats.miss = 1u;
        m_Stats.rejected = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::IoError;
        return false;
    }
    m_Stats.readMicroseconds = Microseconds(readBegin, Clock::now());
    m_Stats.fileBytes = fileBytes.size();

    if (fileBytes.size() < kHeaderBytes ||
        !std::equal(kMagic.begin(), kMagic.end(), fileBytes.begin())) {
        m_Stats.miss = 1u;
        m_Stats.rejected = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::InvalidHeader;
        return false;
    }

    ByteReader header(std::span<const u8>(fileBytes).subspan(kMagic.size()));
    u32 schema = 0;
    u32 headerBytes = 0;
    u32 vertexStride = 0;
    u32 meshCount = 0;
    u32 cacheableMeshCount = 0;
    u32 vertexCodecVersion = 0;
    u32 indexCodecVersion = 0;
    u32 reserved = 0;
    u64 sourceFileBytes = 0;
    u64 sourceHash = 0;
    u64 settingsHash = 0;
    u64 payloadBytes = 0;
    u64 payloadHash = 0;
    if (!header.ReadU32(schema) || !header.ReadU32(headerBytes) ||
        !header.ReadU32(vertexStride) || !header.ReadU32(meshCount) ||
        !header.ReadU32(cacheableMeshCount) ||
        !header.ReadU32(vertexCodecVersion) ||
        !header.ReadU32(indexCodecVersion) || !header.ReadU32(reserved) ||
        !header.ReadU64(sourceFileBytes) || !header.ReadU64(sourceHash) ||
        !header.ReadU64(settingsHash) || !header.ReadU64(payloadBytes) ||
        !header.ReadU64(payloadHash) ||
        schema != kSchemaVersion || headerBytes != kHeaderBytes ||
        vertexStride != sizeof(Vertex3D) ||
        vertexCodecVersion != kVertexCodecVersion ||
        indexCodecVersion != kIndexCodecVersion) {
        m_Stats.miss = 1u;
        m_Stats.rejected = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::InvalidHeader;
        return false;
    }
    if (meshCount != m_SourceMeshes.size() ||
        cacheableMeshCount != m_Stats.cacheableMeshCount ||
        sourceFileBytes != m_Stats.sourceFileBytes ||
        sourceHash != m_Stats.sourceHash || settingsHash != m_Stats.settingsHash) {
        m_Stats.miss = 1u;
        m_Stats.rejected = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::KeyMismatch;
        return false;
    }
    if (payloadBytes != fileBytes.size() - kHeaderBytes) {
        m_Stats.miss = 1u;
        m_Stats.rejected = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::IntegrityFailure;
        return false;
    }

    const std::span<const u8> payload =
        std::span<const u8>(fileBytes).subspan(kHeaderBytes);
    if (HashBytes(payload) != payloadHash) {
        m_Stats.miss = 1u;
        m_Stats.rejected = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::IntegrityFailure;
        return false;
    }

    const Clock::time_point decodeBegin = Clock::now();
    ByteReader reader(payload);
    u32 recordCount = 0;
    if (!reader.ReadU32(recordCount) || recordCount != cacheableMeshCount) {
        m_Stats.miss = 1u;
        m_Stats.rejected = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::TopologyMismatch;
        return false;
    }
    std::vector<bool> decoded(m_SourceMeshes.size(), false);
    u64 rawBytes = 0;
    u64 encodedBytes = 0;
    u32 decodedLevels = 0;
    for (u32 record = 0; record < recordCount; ++record) {
        u32 meshIndex = 0;
        u32 sourceVertexCount = 0;
        u32 sourceIndexCount = 0;
        u32 levelCount = 0;
        if (!reader.ReadU32(meshIndex) || !reader.ReadU32(sourceVertexCount) ||
            !reader.ReadU32(sourceIndexCount) || !reader.ReadU32(levelCount) ||
            meshIndex >= m_SourceMeshes.size() || decoded[meshIndex] ||
            m_SourceMeshes[meshIndex].skinned ||
            sourceVertexCount != m_SourceMeshes[meshIndex].vertexCount ||
            sourceIndexCount != m_SourceMeshes[meshIndex].indexCount ||
            levelCount == 0u || levelCount > kMaxLodLevels) {
            m_Stats.miss = 1u;
            m_Stats.rejected = 1u;
            m_Stats.fallbackReason = MeshLodCacheFallbackReason::TopologyMismatch;
            return false;
        }

        GeneratedMeshLodChain& chain = chains[meshIndex];
        chain.levels.reserve(levelCount);
        for (u32 levelIndex = 0; levelIndex < levelCount; ++levelIndex) {
            u32 vertexCount = 0;
            u32 indexCount = 0;
            f32 targetRatio = 0.0f;
            f32 simplificationError = 0.0f;
            u64 encodedVertexBytes = 0;
            u64 encodedIndexBytes = 0;
            if (!reader.ReadU32(vertexCount) || !reader.ReadU32(indexCount) ||
                !reader.ReadF32(targetRatio) ||
                !reader.ReadF32(simplificationError) ||
                !reader.ReadU64(encodedVertexBytes) ||
                !reader.ReadU64(encodedIndexBytes) ||
                vertexCount == 0u || vertexCount > sourceVertexCount ||
                indexCount == 0u || indexCount > sourceIndexCount ||
                indexCount % 3u != 0u ||
                !std::isfinite(targetRatio) || targetRatio <= 0.0f ||
                targetRatio > 1.0f ||
                !std::isfinite(simplificationError) || simplificationError < 0.0f) {
                m_Stats.miss = 1u;
                m_Stats.rejected = 1u;
                m_Stats.fallbackReason = MeshLodCacheFallbackReason::TopologyMismatch;
                return false;
            }
            std::span<const u8> encodedVertices;
            std::span<const u8> encodedIndices;
            if (!reader.ReadBytes(encodedVertexBytes, encodedVertices) ||
                !reader.ReadBytes(encodedIndexBytes, encodedIndices)) {
                m_Stats.miss = 1u;
                m_Stats.rejected = 1u;
                m_Stats.fallbackReason = MeshLodCacheFallbackReason::IntegrityFailure;
                return false;
            }

            GeneratedMeshLodLevel level{};
            level.vertices.resize(vertexCount);
            level.indices.resize(indexCount);
            level.targetRatio = targetRatio;
            level.simplificationError = simplificationError;
            if (meshopt_decodeVertexBuffer(
                    level.vertices.data(),
                    vertexCount,
                    sizeof(Vertex3D),
                    encodedVertices.data(),
                    encodedVertices.size()
                ) != 0 ||
                meshopt_decodeIndexBuffer(
                    level.indices.data(),
                    indexCount,
                    encodedIndices.data(),
                    encodedIndices.size()
                ) != 0 ||
                !IndicesValid(level)) {
                m_Stats.miss = 1u;
                m_Stats.rejected = 1u;
                m_Stats.fallbackReason = MeshLodCacheFallbackReason::DecodeFailure;
                return false;
            }
            if (levelIndex == 0u && indexCount != sourceIndexCount) {
                m_Stats.miss = 1u;
                m_Stats.rejected = 1u;
                m_Stats.fallbackReason = MeshLodCacheFallbackReason::TopologyMismatch;
                return false;
            }
            rawBytes += static_cast<u64>(vertexCount) * sizeof(Vertex3D) +
                static_cast<u64>(indexCount) * sizeof(u32);
            encodedBytes += encodedVertexBytes + encodedIndexBytes;
            chain.levels.push_back(std::move(level));
            ++decodedLevels;
        }
        decoded[meshIndex] = true;
    }
    if (reader.Remaining() != 0u) {
        m_Stats.miss = 1u;
        m_Stats.rejected = 1u;
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::IntegrityFailure;
        return false;
    }
    for (std::size_t index = 0; index < m_SourceMeshes.size(); ++index) {
        if (!m_SourceMeshes[index].skinned && !decoded[index]) {
            m_Stats.miss = 1u;
            m_Stats.rejected = 1u;
            m_Stats.fallbackReason = MeshLodCacheFallbackReason::TopologyMismatch;
            return false;
        }
    }

    m_Stats.decodeMicroseconds = Microseconds(decodeBegin, Clock::now());
    m_Stats.hit = 1u;
    m_Stats.decodedChainCount = recordCount;
    m_Stats.decodedLevelCount = decodedLevels;
    m_Stats.rawBytes = rawBytes;
    m_Stats.encodedBytes = encodedBytes;
    m_Stats.fallbackReason = MeshLodCacheFallbackReason::None;
    return true;
}

bool MeshLodDerivedDataCache::Store(
    std::span<const GeneratedMeshLodChain> chains
) {
    if (m_Stats.requested == 0u || !m_KeyReady || m_Stats.hit != 0u ||
        chains.size() != m_SourceMeshes.size()) {
        return false;
    }

    const Clock::time_point begin = Clock::now();
    PinIndexCodecVersion();
    std::vector<u8> payload;
    AppendU32(payload, m_Stats.cacheableMeshCount);
    u64 rawBytes = 0;
    u64 encodedBytes = 0;
    u32 levelCount = 0;
    for (std::size_t meshIndex = 0; meshIndex < m_SourceMeshes.size(); ++meshIndex) {
        const MeshLodCacheSourceMesh& source = m_SourceMeshes[meshIndex];
        if (source.skinned) {
            continue;
        }
        const GeneratedMeshLodChain& chain = chains[meshIndex];
        if (chain.Empty() || chain.Count() > kMaxLodLevels) {
            m_Stats.fallbackReason = MeshLodCacheFallbackReason::WriteFailure;
            return false;
        }
        AppendU32(payload, static_cast<u32>(meshIndex));
        AppendU32(payload, source.vertexCount);
        AppendU32(payload, source.indexCount);
        AppendU32(payload, static_cast<u32>(chain.Count()));
        for (const GeneratedMeshLodLevel& level : chain.levels) {
            if (!IndicesValid(level) || level.vertices.size() > source.vertexCount ||
                level.indices.size() > source.indexCount ||
                level.vertices.size() > std::numeric_limits<u32>::max() ||
                level.indices.size() > std::numeric_limits<u32>::max()) {
                m_Stats.fallbackReason = MeshLodCacheFallbackReason::WriteFailure;
                return false;
            }
            std::vector<u8> encodedVertices(
                meshopt_encodeVertexBufferBound(level.vertices.size(), sizeof(Vertex3D))
            );
            const std::size_t encodedVertexSize = meshopt_encodeVertexBufferLevel(
                encodedVertices.data(),
                encodedVertices.size(),
                level.vertices.data(),
                level.vertices.size(),
                sizeof(Vertex3D),
                2,
                static_cast<int>(kVertexCodecVersion)
            );
            std::vector<u8> encodedIndices(
                meshopt_encodeIndexBufferBound(
                    level.indices.size(),
                    level.vertices.size()
                )
            );
            const std::size_t encodedIndexSize = meshopt_encodeIndexBuffer(
                encodedIndices.data(),
                encodedIndices.size(),
                level.indices.data(),
                level.indices.size()
            );
            if (encodedVertexSize == 0u || encodedIndexSize == 0u) {
                m_Stats.fallbackReason = MeshLodCacheFallbackReason::WriteFailure;
                return false;
            }
            encodedVertices.resize(encodedVertexSize);
            encodedIndices.resize(encodedIndexSize);

            AppendU32(payload, static_cast<u32>(level.vertices.size()));
            AppendU32(payload, static_cast<u32>(level.indices.size()));
            AppendF32(payload, level.targetRatio);
            AppendF32(payload, level.simplificationError);
            AppendU64(payload, encodedVertices.size());
            AppendU64(payload, encodedIndices.size());
            payload.insert(payload.end(), encodedVertices.begin(), encodedVertices.end());
            payload.insert(payload.end(), encodedIndices.begin(), encodedIndices.end());
            rawBytes += static_cast<u64>(level.vertices.size()) * sizeof(Vertex3D) +
                static_cast<u64>(level.indices.size()) * sizeof(u32);
            encodedBytes += encodedVertices.size() + encodedIndices.size();
            ++levelCount;
        }
    }

    std::vector<u8> file;
    file.reserve(kHeaderBytes + payload.size());
    file.insert(file.end(), kMagic.begin(), kMagic.end());
    AppendU32(file, kSchemaVersion);
    AppendU32(file, kHeaderBytes);
    AppendU32(file, static_cast<u32>(sizeof(Vertex3D)));
    AppendU32(file, static_cast<u32>(m_SourceMeshes.size()));
    AppendU32(file, m_Stats.cacheableMeshCount);
    AppendU32(file, kVertexCodecVersion);
    AppendU32(file, kIndexCodecVersion);
    AppendU32(file, 0u);
    AppendU64(file, m_Stats.sourceFileBytes);
    AppendU64(file, m_Stats.sourceHash);
    AppendU64(file, m_Stats.settingsHash);
    AppendU64(file, payload.size());
    AppendU64(file, HashBytes(payload));
    if (file.size() != kHeaderBytes) {
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::WriteFailure;
        return false;
    }
    file.insert(file.end(), payload.begin(), payload.end());

    std::error_code directoryError;
    std::filesystem::create_directories(m_Options.directory, directoryError);
    if (directoryError) {
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::WriteFailure;
        return false;
    }
    const std::filesystem::path temporaryPath = TemporaryPathFor(m_CachePath);
    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream) {
            m_Stats.fallbackReason = MeshLodCacheFallbackReason::WriteFailure;
            return false;
        }
        stream.write(
            reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size())
        );
        stream.flush();
        if (!stream) {
            stream.close();
            std::filesystem::remove(temporaryPath, directoryError);
            m_Stats.fallbackReason = MeshLodCacheFallbackReason::WriteFailure;
            return false;
        }
    }
    if (!PublishAtomically(temporaryPath, m_CachePath)) {
        std::filesystem::remove(temporaryPath, directoryError);
        m_Stats.fallbackReason = MeshLodCacheFallbackReason::WriteFailure;
        return false;
    }

    m_Stats.written = 1u;
    m_Stats.decodedChainCount = m_Stats.cacheableMeshCount;
    m_Stats.decodedLevelCount = levelCount;
    m_Stats.rawBytes = rawBytes;
    m_Stats.encodedBytes = encodedBytes;
    m_Stats.fileBytes = file.size();
    m_Stats.writeMicroseconds = Microseconds(begin, Clock::now());
    return true;
}

void MeshLodDerivedDataCache::SetBuildMicroseconds(u64 microseconds) {
    m_Stats.buildMicroseconds = microseconds;
}

void MeshLodDerivedDataCache::SetImportMicroseconds(u64 microseconds) {
    m_Stats.importMicroseconds = microseconds;
}

void MeshLodDerivedDataCache::SetTotalLoadMicroseconds(u64 microseconds) {
    m_Stats.totalLoadMicroseconds = microseconds;
}

const MeshLodDerivedDataCacheStats& MeshLodDerivedDataCache::Stats() const {
    return m_Stats;
}

const std::filesystem::path& MeshLodDerivedDataCache::CachePath() const {
    return m_CachePath;
}

}
