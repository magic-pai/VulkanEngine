[[vk::binding(0, 0)]] ByteAddressBuffer g_source_vertices;
[[vk::binding(1, 0)]] RWByteAddressBuffer g_skinned_vertices;
[[vk::binding(0, 1)]] ByteAddressBuffer g_bone_palette;

#if defined(SE_HYBRID_REFLECTION_SKINNING_AUDIT)
[[vk::binding(2, 0)]] globallycoherent RWByteAddressBuffer g_skinning_audit;
#endif

struct SkinningPushConstants {
    uint vertexCount;
    uint vertexStride;
    uint currentPaletteOffset;
    uint currentPaletteCount;
};

[[vk::push_constant]] SkinningPushConstants g_skinning;

static const uint kPositionOffset = 0u;
static const uint kNormalOffset = 12u;
static const uint kTangentOffset = 44u;
static const uint kBoneIndicesOffset = 60u;
static const uint kBoneWeightsOffset = 76u;
static const uint kBoneMatrixStride = 64u;

#if defined(SE_HYBRID_REFLECTION_SKINNING_AUDIT)
void AuditAdd(uint byteOffset) {
    uint originalValue = 0u;
    g_skinning_audit.InterlockedAdd(byteOffset, 1u, originalValue);
}
#else
void AuditAdd(uint byteOffset) {
}
#endif

float3 LoadFloat3(uint byteOffset) {
    return asfloat(g_source_vertices.Load3(byteOffset));
}

float4 LoadFloat4(uint byteOffset) {
    return asfloat(g_source_vertices.Load4(byteOffset));
}

void CopyVertex(uint sourceOffset, uint destinationOffset) {
    for (uint byteOffset = 0u; byteOffset < g_skinning.vertexStride; byteOffset += 4u) {
        g_skinned_vertices.Store(
            destinationOffset + byteOffset,
            g_source_vertices.Load(sourceOffset + byteOffset)
        );
    }
}

void AccumulateBoneMatrix(
    uint boneIndex,
    float weight,
    inout float4 column0,
    inout float4 column1,
    inout float4 column2,
    inout float4 column3
) {
    const uint matrixOffset =
        (g_skinning.currentPaletteOffset + boneIndex) * kBoneMatrixStride;
    column0 += asfloat(g_bone_palette.Load4(matrixOffset + 0u)) * weight;
    column1 += asfloat(g_bone_palette.Load4(matrixOffset + 16u)) * weight;
    column2 += asfloat(g_bone_palette.Load4(matrixOffset + 32u)) * weight;
    column3 += asfloat(g_bone_palette.Load4(matrixOffset + 48u)) * weight;
}

float4 TransformByColumns(
    float4 value,
    float4 column0,
    float4 column1,
    float4 column2,
    float4 column3
) {
    return column0 * value.x +
        column1 * value.y +
        column2 * value.z +
        column3 * value.w;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    const uint vertexIndex = dispatchThreadId.x;
    if (vertexIndex >= g_skinning.vertexCount) {
        return;
    }

    AuditAdd(0u);
    const uint vertexOffset = vertexIndex * g_skinning.vertexStride;
    CopyVertex(vertexOffset, vertexOffset);

    const uint4 boneIndices = g_source_vertices.Load4(
        vertexOffset + kBoneIndicesOffset
    );
    const float4 sourceWeights = asfloat(g_source_vertices.Load4(
        vertexOffset + kBoneWeightsOffset
    ));
    const float weightSum = dot(sourceWeights, 1.0f.xxxx);
    if (!isfinite(weightSum) || weightSum <= 0.00001f) {
        AuditAdd(8u);
        return;
    }

    const float4 weights = sourceWeights / weightSum;
    bool validIndices = true;
    validIndices = validIndices &&
        (weights.x <= 0.00001f || boneIndices.x < g_skinning.currentPaletteCount);
    validIndices = validIndices &&
        (weights.y <= 0.00001f || boneIndices.y < g_skinning.currentPaletteCount);
    validIndices = validIndices &&
        (weights.z <= 0.00001f || boneIndices.z < g_skinning.currentPaletteCount);
    validIndices = validIndices &&
        (weights.w <= 0.00001f || boneIndices.w < g_skinning.currentPaletteCount);
    if (!validIndices || g_skinning.currentPaletteOffset == 0u) {
        AuditAdd(12u);
        return;
    }

    float4 column0 = 0.0f.xxxx;
    float4 column1 = 0.0f.xxxx;
    float4 column2 = 0.0f.xxxx;
    float4 column3 = 0.0f.xxxx;
    if (weights.x > 0.00001f) {
        AccumulateBoneMatrix(
            boneIndices.x, weights.x, column0, column1, column2, column3
        );
    }
    if (weights.y > 0.00001f) {
        AccumulateBoneMatrix(
            boneIndices.y, weights.y, column0, column1, column2, column3
        );
    }
    if (weights.z > 0.00001f) {
        AccumulateBoneMatrix(
            boneIndices.z, weights.z, column0, column1, column2, column3
        );
    }
    if (weights.w > 0.00001f) {
        AccumulateBoneMatrix(
            boneIndices.w, weights.w, column0, column1, column2, column3
        );
    }

    const float3 sourcePosition = LoadFloat3(vertexOffset + kPositionOffset);
    const float3 sourceNormal = LoadFloat3(vertexOffset + kNormalOffset);
    const float4 sourceTangent = LoadFloat4(vertexOffset + kTangentOffset);
    const float3 skinnedPosition = TransformByColumns(
        float4(sourcePosition, 1.0f), column0, column1, column2, column3
    ).xyz;
    const float3 skinnedNormal = TransformByColumns(
        float4(sourceNormal, 0.0f), column0, column1, column2, column3
    ).xyz;
    const float3 skinnedTangent = TransformByColumns(
        float4(sourceTangent.xyz, 0.0f), column0, column1, column2, column3
    ).xyz;
    if (!all(isfinite(skinnedPosition)) ||
        !all(isfinite(skinnedNormal)) ||
        !all(isfinite(skinnedTangent))) {
        AuditAdd(16u);
        return;
    }

    g_skinned_vertices.Store3(
        vertexOffset + kPositionOffset,
        asuint(skinnedPosition)
    );
    g_skinned_vertices.Store3(
        vertexOffset + kNormalOffset,
        asuint(skinnedNormal)
    );
    g_skinned_vertices.Store3(
        vertexOffset + kTangentOffset,
        asuint(skinnedTangent)
    );
    AuditAdd(4u);
}
