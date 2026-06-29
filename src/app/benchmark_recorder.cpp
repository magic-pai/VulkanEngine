#include "app/benchmark_recorder.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace se {

namespace {

std::string ReadEnvironmentString(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
        return {};
    }

    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

u32 ReadEnvironmentU32(const char* name, u32 fallback, bool allowZero = false) {
    const std::string value = ReadEnvironmentString(name);
    if (value.empty()) {
        return fallback;
    }

    const int parsed = std::atoi(value.c_str());
    if (parsed < 0 || (parsed == 0 && !allowZero)) {
        return fallback;
    }

    return static_cast<u32>(parsed);
}

void WriteGpuValue(std::ofstream& csv, bool available, f32 value) {
    if (available) {
        csv << value;
    }
}

}

BenchmarkRecorderConfig BenchmarkRecorder::ConfigFromEnvironment() {
    BenchmarkRecorderConfig config{};
    config.captureFrames = ReadEnvironmentU32("SE_BENCHMARK_FRAMES", 0);
    config.enabled = config.captureFrames > 0;
    if (!config.enabled) {
        return config;
    }

    config.warmupFrames = ReadEnvironmentU32(
        "SE_BENCHMARK_WARMUP_FRAMES",
        config.warmupFrames,
        true
    );
    const std::string csvPath = ReadEnvironmentString("SE_BENCHMARK_CSV");
    if (!csvPath.empty()) {
        config.csvPath = csvPath;
    }

    return config;
}

BenchmarkRecorder::BenchmarkRecorder(BenchmarkRecorderConfig config)
    : m_Config(std::move(config)) {
    if (m_Config.enabled) {
        OpenCsv();
        WriteHeader();
        std::cout << "Benchmark enabled: warmup=" << m_Config.warmupFrames
            << " capture=" << m_Config.captureFrames
            << " csv=" << m_Config.csvPath.string() << std::endl;
    }
}

BenchmarkRecorder::~BenchmarkRecorder() {
    if (m_Config.enabled) {
        std::cout << "Benchmark captured " << m_CapturedFrames
            << " frames to " << m_Config.csvPath.string() << std::endl;
    }
}

bool BenchmarkRecorder::Enabled() const {
    return m_Config.enabled;
}

bool BenchmarkRecorder::ShouldStop() const {
    return m_StopRequested;
}

void BenchmarkRecorder::RecordFrame(
    u32 renderedFrameIndex,
    f32 elapsedSeconds,
    const RendererStats& stats
) {
    if (!m_Config.enabled || m_StopRequested) {
        return;
    }

    if (renderedFrameIndex <= m_Config.warmupFrames) {
        return;
    }

    const RendererCpuStats& cpu = stats.cpu;
    const RendererDrawStats& draw = stats.draw;
    const RendererShadowCascadeStats& shadowCascades = stats.shadowCascades;
    const RendererLocalShadowAtlasStats& localShadowAtlas = stats.localShadowAtlas;
    const RendererBindStats& binds = stats.binds;
    const RendererGpuStats& gpu = stats.gpu;

    m_Csv << m_CapturedFrames << ','
        << renderedFrameIndex << ','
        << elapsedSeconds << ','
        << stats.frameGraph.activePassCount << ','
        << stats.frameGraph.roadmapPassCount << ','
        << stats.frameGraph.physicalResourceCount << ','
        << stats.frameGraph.plannedResourceCount << ','
        << cpu.totalFrameMs << ','
        << cpu.waitAcquireMs << ','
        << cpu.imguiMs << ','
        << cpu.pickingMs << ','
        << cpu.queueBuildMs << ','
        << cpu.uniformUpdateMs << ','
        << cpu.commandRecordMs << ','
        << cpu.submitPresentMs << ','
        << (gpu.available ? 1 : 0) << ',';
    WriteGpuValue(m_Csv, gpu.available, gpu.totalRecordedMs);
    m_Csv << ',';
    WriteGpuValue(m_Csv, gpu.available, gpu.shadowMs);
    m_Csv << ',';
    WriteGpuValue(m_Csv, gpu.available, gpu.mainMs);
    m_Csv << ',';
    WriteGpuValue(m_Csv, gpu.available, gpu.overlayMs);
    m_Csv << ',';
    WriteGpuValue(m_Csv, gpu.available, gpu.imguiMs);
    m_Csv << ','
        << draw.mainDraws << ','
        << draw.gBufferDraws << ','
        << draw.overlayDraws << ','
        << draw.shadowDraws << ','
        << draw.hybridDeferredOpaqueDraws << ','
        << draw.hybridForwardTransparentDraws << ','
        << draw.hybridForwardSpecialDraws << ','
        << draw.hybridForwardResidualDraws << ','
        << draw.hybridForwardResidualSortOps << ','
        << draw.hybridForwardResidualSortedTransparentDraws << ','
        << draw.hybridForwardResidualStableSpecialDraws << ','
        << draw.mainTriangles << ','
        << draw.gBufferTriangles << ','
        << draw.overlayTriangles << ','
        << draw.shadowTriangles << ','
        << draw.hybridDeferredOpaqueTriangles << ','
        << draw.hybridForwardResidualTriangles << ','
        << draw.matrixRecalculations << ','
        << draw.mainBoundsCacheHits << ','
        << draw.mainBoundsCacheMisses << ','
        << draw.mainCommandCacheHits << ','
        << draw.mainCommandCacheMisses << ','
        << draw.mainVisibilityCacheHits << ','
        << draw.mainVisibilityCacheMisses << ','
        << draw.mainQueueCacheHits << ','
        << draw.mainQueueCacheMisses << ','
        << draw.overlayBoundsCacheHits << ','
        << draw.overlayBoundsCacheMisses << ','
        << draw.overlayCommandCacheHits << ','
        << draw.overlayCommandCacheMisses << ','
        << draw.overlayVisibilityCacheHits << ','
        << draw.overlayVisibilityCacheMisses << ','
        << draw.overlayQueueCacheHits << ','
        << draw.overlayQueueCacheMisses << ','
        << draw.mainInstancedDraws << ','
        << draw.mainInstancedInstances << ','
        << draw.mainInstanceBatchCacheHits << ','
        << draw.mainInstanceBatchCacheMisses << ','
        << draw.mainVisible << ','
        << draw.mainCulled << ','
        << draw.overlayVisible << ','
        << draw.overlayCulled << ','
        << draw.shadowVisible << ','
        << draw.shadowCulled << ','
        << shadowCascades.configuredCount << ','
        << shadowCascades.activeCount << ','
        << shadowCascades.stableSnappingEnabled << ','
        << shadowCascades.pcfKernelRadius << ','
        << shadowCascades.pcssStrength << ','
        << shadowCascades.splitLambda << ','
        << shadowCascades.blendRatio << ','
        << shadowCascades.fadeRatio << ','
        << shadowCascades.maxDistance << ','
        << shadowCascades.nearDepth << ','
        << shadowCascades.farDepth << ','
        << shadowCascades.splitDepths[0] << ','
        << shadowCascades.splitDepths[1] << ','
        << shadowCascades.splitDepths[2] << ','
        << shadowCascades.splitDepths[3] << ','
        << shadowCascades.texelWorldSizes[0] << ','
        << shadowCascades.texelWorldSizes[1] << ','
        << shadowCascades.texelWorldSizes[2] << ','
        << shadowCascades.texelWorldSizes[3] << ','
        << shadowCascades.atlasAllocated << ','
        << shadowCascades.atlasTileSize << ','
        << shadowCascades.atlasWidth << ','
        << shadowCascades.atlasHeight << ','
        << shadowCascades.atlasTileColumns << ','
        << shadowCascades.atlasTileRows << ','
        << shadowCascades.atlasCascadeCapacity << ','
        << localShadowAtlas.allocated << ','
        << localShadowAtlas.tileSize << ','
        << localShadowAtlas.atlasWidth << ','
        << localShadowAtlas.atlasHeight << ','
        << localShadowAtlas.tileColumns << ','
        << localShadowAtlas.tileRows << ','
        << localShadowAtlas.tileCapacity << ','
        << localShadowAtlas.shadowableLocalLights << ','
        << localShadowAtlas.pointLightCount << ','
        << localShadowAtlas.spotLightCount << ','
        << localShadowAtlas.pointFaceTiles << ','
        << localShadowAtlas.spotTiles << ','
        << localShadowAtlas.requestedTiles << ','
        << localShadowAtlas.assignedTiles << ','
        << localShadowAtlas.droppedTiles << ','
        << localShadowAtlas.recordedTilePasses << ','
        << localShadowAtlas.recordedDraws << ','
        << localShadowAtlas.recordedMeshBinds << ','
        << localShadowAtlas.biasMin << ','
        << localShadowAtlas.biasSlope << ','
        << localShadowAtlas.pcfRadius << ','
        << localShadowAtlas.pcfKernelRadius << ','
        << localShadowAtlas.pcssStrength << ','
        << binds.mainMaterialBinds << ','
        << binds.mainMeshBinds << ','
        << binds.gBufferMaterialBinds << ','
        << binds.gBufferMeshBinds << ','
        << binds.deferredLightingDraws << ','
        << binds.deferredLightingFrameBinds << ','
        << binds.deferredLightingGBufferBinds << ','
        << binds.deferredPbrDebugDraws << ','
        << binds.deferredPbrDebugFrameBinds << ','
        << binds.deferredPbrDebugGBufferBinds << ','
        << binds.hdrCompositeDraws << ','
        << binds.hdrCompositeFrameBinds << ','
        << binds.hdrCompositeTextureBinds << ','
        << binds.gBufferDebugDraws << ','
        << binds.gBufferDebugFrameBinds << ','
        << binds.gBufferDebugTextureBinds << ','
        << binds.deferredShadowDebugDraws << ','
        << binds.deferredShadowDebugFrameBinds << ','
        << binds.deferredShadowDebugTextureBinds << ','
        << binds.shadowCascadeDebugDraws << ','
        << binds.shadowCascadeDebugFrameBinds << ','
        << binds.shadowCascadeDebugTextureBinds << ','
        << binds.localShadowAtlasDebugDraws << ','
        << binds.localShadowAtlasDebugFrameBinds << ','
        << binds.localShadowAtlasDebugTextureBinds << ','
        << binds.localShadowVisibilityDebugDraws << ','
        << binds.localShadowVisibilityDebugFrameBinds << ','
        << binds.localShadowVisibilityDebugTextureBinds << ','
        << binds.lightTileCullComputeDispatches << ','
        << binds.lightTileCullComputeFrameBinds << ','
        << binds.lightTileCullComputeGroupsX << ','
        << binds.lightTileCullComputeGroupsY << ','
        << binds.depthCopyOps << ','
        << binds.depthPrefillDraws << ','
        << binds.depthPrefillMeshBinds << ','
        << binds.forwardResidualDraws << ','
        << binds.forwardResidualFrameBinds << ','
        << binds.forwardResidualSharedLightListDraws << ','
        << binds.forwardResidualMaterialBinds << ','
        << binds.forwardResidualMeshBinds << ','
        << binds.overlayMaterialBinds << ','
        << binds.overlayMeshBinds << ','
        << binds.shadowMeshBinds << ','
        << binds.shadowCascadeAtlasPasses << ','
        << binds.shadowCascadeAtlasDraws << ','
        << binds.shadowCascadeAtlasMeshBinds << ','
        << binds.localShadowAtlasPasses << ','
        << binds.localShadowAtlasDraws << ','
        << binds.localShadowAtlasMeshBinds << ','
        << binds.localShadowResolveEnabled << ','
        << binds.shadowCascadeBufferUpdates << ','
        << binds.localShadowBufferUpdates << ','
        << binds.frameLightConstantUpdates << ','
        << binds.frameLightBufferUpdates << ','
        << binds.frameLightTotalCount << ','
        << binds.frameDirectionalLightCount << ','
        << binds.frameLocalLightCount << ','
        << binds.frameRectLightCount << ','
        << binds.frameLightTileSize << ','
        << binds.frameLightTileCountX << ','
        << binds.frameLightTileCountY << ','
        << binds.frameLightTileCount << ','
        << binds.frameLightTileAssignments << ','
        << binds.frameLightTileAssignmentCapacity << ','
        << binds.frameLightTileOverflowAssignments << ','
        << binds.frameLightTileOverflowCapacity << ','
        << binds.frameLightTileOverflowTiles << ','
        << binds.frameLightTileOverflowDropped << ','
        << binds.frameLightTileAssignmentFallbacks << ','
        << binds.frameLightTileGpuReadbackValid << ','
        << binds.frameLightTileGpuSaturatedTiles << ','
        << binds.frameLightTileGpuMaxCandidates << ','
        << binds.frameLightTileGpuRawCandidates << ','
        << binds.frameLightTileGpuOverflowTiles << ','
        << binds.frameLightTileGpuOverflowDroppedTiles << ','
        << binds.frameLightTileGpuOverflowStored << ','
        << binds.frameLightTileGpuOverflowDropped << ','
        << binds.frameMaterialBufferUpdates << ','
        << binds.frameMaterialCount << ','
        << binds.frameMaterialCapacity << ','
        << binds.frameMaterialOverflowCount << ','
        << binds.frameMaterialOpaqueCount << ','
        << binds.frameMaterialTransparentCount << ','
        << binds.frameMaterialForwardSpecialCount << ','
        << binds.frameMaterialEmissiveHintCount << ','
        << binds.frameMaterialSpecularHintCount << ','
        << binds.frameMaterialSpecularTextureCount << ','
        << binds.frameMaterialAlphaMaskCount << ','
        << binds.frameMaterialAlphaBlendCount << ','
        << binds.frameMaterialUvTransformCount << ','
        << binds.frameMaterialDoubleSidedCount << ','
        << binds.frameMaterialClearcoatCount << ','
        << binds.frameMaterialClearcoatTextureCount << ','
        << binds.frameMaterialClearcoatRoughnessTextureCount << ','
        << binds.frameMaterialTransmissionCount << ','
        << binds.frameMaterialTransmissionTextureCount << ','
        << binds.frameMaterialVolumeCount << ','
        << binds.frameMaterialOpacityTextureCount << ','
        << binds.frameMaterialTexturedCount << ','
        << binds.mainInstanceBufferUploads << ','
        << binds.mainInstanceBufferUploadSkips << ','
        << binds.pushConstantUpdates << ','
        << binds.pushConstantBytes << '\n';

    ++m_CapturedFrames;
    if (m_CapturedFrames >= m_Config.captureFrames) {
        m_StopRequested = true;
    }
}

void BenchmarkRecorder::OpenCsv() {
    const std::filesystem::path parentPath = m_Config.csvPath.parent_path();
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath);
    }

    m_Csv.open(m_Config.csvPath, std::ios::out | std::ios::trunc);
    if (!m_Csv) {
        throw std::runtime_error("Failed to open benchmark CSV: " + m_Config.csvPath.string());
    }
}

void BenchmarkRecorder::WriteHeader() {
    m_Csv
        << "sample_frame,rendered_frame,elapsed_seconds,"
        << "framegraph_active_passes,framegraph_roadmap_passes,"
        << "framegraph_physical_resources,framegraph_planned_resources,"
        << "cpu_total_ms,cpu_wait_acquire_ms,cpu_imgui_ms,cpu_picking_ms,"
        << "cpu_queue_build_ms,cpu_uniform_update_ms,cpu_command_record_ms,cpu_submit_present_ms,"
        << "gpu_available,gpu_total_recorded_ms,gpu_shadow_ms,gpu_main_ms,gpu_overlay_ms,gpu_imgui_ms,"
        << "main_draws,gbuffer_draws,overlay_draws,shadow_draws,"
        << "hybrid_deferred_opaque_draws,hybrid_forward_transparent_draws,"
        << "hybrid_forward_special_draws,hybrid_forward_residual_draws,"
        << "hybrid_forward_residual_sort_ops,"
        << "hybrid_forward_residual_sorted_transparent_draws,"
        << "hybrid_forward_residual_stable_special_draws,"
        << "main_triangles,gbuffer_triangles,overlay_triangles,shadow_triangles,"
        << "hybrid_deferred_opaque_triangles,hybrid_forward_residual_triangles,"
        << "matrix_recalculations,"
        << "main_bounds_cache_hits,main_bounds_cache_misses,"
        << "main_command_cache_hits,main_command_cache_misses,"
        << "main_visibility_cache_hits,main_visibility_cache_misses,"
        << "main_queue_cache_hits,main_queue_cache_misses,"
        << "overlay_bounds_cache_hits,overlay_bounds_cache_misses,"
        << "overlay_command_cache_hits,overlay_command_cache_misses,"
        << "overlay_visibility_cache_hits,overlay_visibility_cache_misses,"
        << "overlay_queue_cache_hits,overlay_queue_cache_misses,"
        << "main_instanced_draws,main_instanced_instances,"
        << "main_instance_batch_cache_hits,main_instance_batch_cache_misses,"
        << "main_visible,main_culled,overlay_visible,overlay_culled,shadow_visible,shadow_culled,"
        << "shadow_cascade_configured_count,shadow_cascade_active_count,"
        << "shadow_cascade_stable_snapping,shadow_pcf_kernel_radius,shadow_pcss_strength,"
        << "shadow_cascade_split_lambda,"
        << "shadow_cascade_blend_ratio,shadow_cascade_fade_ratio,"
        << "shadow_cascade_max_distance,shadow_cascade_near_depth,shadow_cascade_far_depth,"
        << "shadow_cascade_split0,shadow_cascade_split1,"
        << "shadow_cascade_split2,shadow_cascade_split3,"
        << "shadow_cascade_texel0,shadow_cascade_texel1,"
        << "shadow_cascade_texel2,shadow_cascade_texel3,"
        << "shadow_cascade_atlas_allocated,shadow_cascade_atlas_tile_size,"
        << "shadow_cascade_atlas_width,shadow_cascade_atlas_height,"
        << "shadow_cascade_atlas_tile_columns,shadow_cascade_atlas_tile_rows,"
        << "shadow_cascade_atlas_capacity,"
        << "local_shadow_atlas_allocated,local_shadow_atlas_tile_size,"
        << "local_shadow_atlas_width,local_shadow_atlas_height,"
        << "local_shadow_atlas_tile_columns,local_shadow_atlas_tile_rows,"
        << "local_shadow_atlas_capacity,local_shadow_shadowable_light_count,"
        << "local_shadow_point_light_count,local_shadow_spot_light_count,"
        << "local_shadow_point_face_tiles,local_shadow_spot_tiles,"
        << "local_shadow_requested_tiles,local_shadow_assigned_tiles,"
        << "local_shadow_dropped_tiles,local_shadow_recorded_tile_passes,"
        << "local_shadow_recorded_draws,local_shadow_recorded_mesh_binds,"
        << "local_shadow_bias_min,local_shadow_bias_slope,"
        << "local_shadow_pcf_radius,local_shadow_pcf_kernel_radius,"
        << "local_shadow_pcss_strength,"
        << "main_material_binds,main_mesh_binds,gbuffer_material_binds,gbuffer_mesh_binds,"
        << "deferred_lighting_draws,deferred_lighting_frame_binds,deferred_lighting_gbuffer_binds,"
        << "deferred_pbr_debug_draws,deferred_pbr_debug_frame_binds,deferred_pbr_debug_gbuffer_binds,"
        << "hdr_composite_draws,hdr_composite_frame_binds,hdr_composite_texture_binds,"
        << "gbuffer_debug_draws,gbuffer_debug_frame_binds,gbuffer_debug_texture_binds,"
        << "deferred_shadow_debug_draws,deferred_shadow_debug_frame_binds,deferred_shadow_debug_texture_binds,"
        << "shadow_cascade_debug_draws,shadow_cascade_debug_frame_binds,shadow_cascade_debug_texture_binds,"
        << "local_shadow_atlas_debug_draws,local_shadow_atlas_debug_frame_binds,"
        << "local_shadow_atlas_debug_texture_binds,"
        << "local_shadow_visibility_debug_draws,local_shadow_visibility_debug_frame_binds,"
        << "local_shadow_visibility_debug_texture_binds,"
        << "light_tile_cull_compute_dispatches,light_tile_cull_compute_frame_binds,"
        << "light_tile_cull_compute_groups_x,light_tile_cull_compute_groups_y,"
        << "depth_copy_ops,depth_prefill_draws,depth_prefill_mesh_binds,"
        << "forward_residual_draws,forward_residual_frame_binds,"
        << "forward_residual_shared_light_list_draws,"
        << "forward_residual_material_binds,forward_residual_mesh_binds,"
        << "overlay_material_binds,overlay_mesh_binds,"
        << "shadow_mesh_binds,shadow_cascade_atlas_passes,"
        << "shadow_cascade_atlas_draws,shadow_cascade_atlas_mesh_binds,"
        << "local_shadow_atlas_passes,local_shadow_atlas_draws,"
        << "local_shadow_atlas_mesh_binds,"
        << "local_shadow_resolve_enabled,"
        << "shadow_cascade_buffer_updates,local_shadow_buffer_updates,"
        << "frame_light_constant_updates,frame_light_buffer_updates,"
        << "frame_light_total_count,frame_directional_light_count,frame_local_light_count,"
        << "frame_rect_light_count,"
        << "frame_light_tile_size,frame_light_tile_count_x,frame_light_tile_count_y,"
        << "frame_light_tile_count,frame_light_tile_assignments,"
        << "frame_light_tile_assignment_capacity,"
        << "frame_light_tile_overflow_assignments,frame_light_tile_overflow_capacity,"
        << "frame_light_tile_overflow_tiles,frame_light_tile_overflow_dropped,"
        << "frame_light_tile_assignment_fallbacks,"
        << "frame_light_tile_gpu_readback_valid,frame_light_tile_gpu_saturated_tiles,"
        << "frame_light_tile_gpu_max_candidates,frame_light_tile_gpu_raw_candidates,"
        << "frame_light_tile_gpu_overflow_tiles,"
        << "frame_light_tile_gpu_overflow_dropped_tiles,"
        << "frame_light_tile_gpu_overflow_stored,frame_light_tile_gpu_overflow_dropped,"
        << "frame_material_buffer_updates,frame_material_count,"
        << "frame_material_capacity,frame_material_overflow_count,"
        << "frame_material_opaque_count,frame_material_transparent_count,"
        << "frame_material_forward_special_count,frame_material_emissive_hint_count,"
        << "frame_material_specular_hint_count,"
        << "frame_material_specular_texture_count,"
        << "frame_material_alpha_mask_count,frame_material_alpha_blend_count,"
        << "frame_material_uv_transform_count,"
        << "frame_material_double_sided_count,"
        << "frame_material_clearcoat_count,"
        << "frame_material_clearcoat_texture_count,"
        << "frame_material_clearcoat_roughness_texture_count,"
        << "frame_material_transmission_count,"
        << "frame_material_transmission_texture_count,"
        << "frame_material_volume_count,"
        << "frame_material_opacity_texture_count,"
        << "frame_material_textured_count,"
        << "main_instance_buffer_uploads,main_instance_buffer_upload_skips,"
        << "push_constant_updates,push_constant_bytes\n";
}

}
