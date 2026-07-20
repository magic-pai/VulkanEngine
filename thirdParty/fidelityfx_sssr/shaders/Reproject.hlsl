/**********************************************************************
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/
#include "Common.hlsl"

// Inputs
[[vk::binding( 0, 1)]] Texture2D<float> g_depth_buffer							: register(t0);
[[vk::binding( 1, 1)]] Texture2D<float> g_roughness						        : register(t1);
[[vk::binding( 2, 1)]] Texture2D<float3> g_normal							    : register(t2);
// SelfEngine packs previous receiver view-depth, octahedral world normal, and
// roughness into one RGBA history image. FidelityFX DNSR still consumes the
// same semantic inputs through its wrapper callbacks below.
[[vk::binding( 3, 1)]] Texture2D<float4> g_receiver_history_metadata		        : register(t3);
[[vk::binding( 4, 1)]] Texture2D<float> g_roughness_history				        : register(t4);
[[vk::binding( 5, 1)]] Texture2D<float3> g_normal_history					    : register(t5);

[[vk::binding( 6, 1)]] Texture2D<float4> g_in_radiance					        : register(t6);
[[vk::binding( 7, 1)]] Texture2D<float4> g_radiance_history				        : register(t7);
[[vk::binding( 8, 1)]] Texture2D<float2> g_motion_vector				        : register(t8);

[[vk::binding( 9, 1)]] Texture2D<float3> g_average_radiance_history		        : register(t9);
[[vk::binding(10, 1)]] Texture2D<float> g_variance_history		                : register(t10);
[[vk::binding(11, 1)]] Texture2D<float> g_sample_count_history			        : register(t11);
[[vk::binding(12, 1)]] Texture2D<float2> g_blue_noise_texture                   : register(t12);

// Samplers
[[vk::binding(13, 1)]] SamplerState g_linear_sampler                            : register(s0);

// Outputs
[[vk::binding(14, 1)]] RWTexture2D<float4> g_out_reprojected_radiance		    : register(u0);
[[vk::binding(15, 1)]] RWTexture2D<float4> g_out_average_radiance	            : register(u1);
[[vk::binding(16, 1)]] RWTexture2D<float> g_out_variance		                : register(u2);
[[vk::binding(17, 1)]] RWTexture2D<float> g_out_sample_count		            : register(u3);

[[vk::binding(18, 1)]] Buffer<uint> g_denoiser_tile_list                        : register(t13);

float FFX_DNSR_Reflections_GetRandom(int2 pixel_coordinate) { return g_blue_noise_texture.Load(int3(pixel_coordinate.xy % 128, 0)).x; }
float FFX_DNSR_Reflections_LoadDepth(int2 pixel_coordinate) { return g_depth_buffer.Load(int3(pixel_coordinate, 0)); }
min16float3 SelfEngine_DecodeHistoryNormal(min16float2 encoded) {
    min16float3 value = min16float3(encoded, 1.0 - abs(encoded.x) - abs(encoded.y));
    if (value.z < 0.0) {
        min16float2 sign_value = min16float2(
            value.x >= 0.0 ? 1.0 : -1.0,
            value.y >= 0.0 ? 1.0 : -1.0
        );
        value.xy = (1.0 - abs(value.yx)) * sign_value;
    }
    return normalize(value);
}

float FFX_DNSR_Reflections_LoadDepthHistory(int2 pixel_coordinate) { return g_receiver_history_metadata.Load(int3(pixel_coordinate, 0)).x; }
float FFX_DNSR_Reflections_SampleDepthHistory(float2 uv) { return g_receiver_history_metadata.SampleLevel(g_linear_sampler, uv, 0.0f).x; }
min16float3 FFX_DNSR_Reflections_LoadRadiance(int2 pixel_coordinate) { return (min16float3)g_in_radiance.Load(int3(pixel_coordinate, 0)).xyz; }
min16float3 FFX_DNSR_Reflections_LoadRadianceHistory(int2 pixel_coordinate) { return (min16float3)g_radiance_history.Load(int3(pixel_coordinate, 0)).xyz; }
min16float3 FFX_DNSR_Reflections_SampleRadianceHistory(float2 uv) { return (min16float3)g_radiance_history.SampleLevel(g_linear_sampler, uv, 0.0f).xyz; }
min16float FFX_DNSR_Reflections_SampleNumSamplesHistory(float2 uv) { return (min16float)g_sample_count_history.SampleLevel(g_linear_sampler, uv, 0.0f).x; }
min16float3 FFX_DNSR_Reflections_LoadWorldSpaceNormal(int2 pixel_coordinate) { return normalize(2.0 * (min16float3)g_normal.Load(int3(pixel_coordinate, 0)) - 1.0); }
min16float3 FFX_DNSR_Reflections_LoadWorldSpaceNormalHistory(int2 pixel_coordinate) { return SelfEngine_DecodeHistoryNormal((min16float2)g_receiver_history_metadata.Load(int3(pixel_coordinate, 0)).yz); }
min16float3 FFX_DNSR_Reflections_SampleWorldSpaceNormalHistory(float2 uv) { return SelfEngine_DecodeHistoryNormal((min16float2)g_receiver_history_metadata.SampleLevel(g_linear_sampler, uv, 0.0f).yz); }
min16float FFX_DNSR_Reflections_LoadRoughness(int2 pixel_coordinate) { return (min16float)g_roughness.Load(int3(pixel_coordinate, 0)); }
min16float FFX_DNSR_Reflections_SampleRoughnessHistory(float2 uv) { return (min16float)g_receiver_history_metadata.SampleLevel(g_linear_sampler, uv, 0.0f).w; }
min16float FFX_DNSR_Reflections_LoadRoughnessHistory(int2 pixel_coordinate) { return (min16float)g_receiver_history_metadata.Load(int3(pixel_coordinate, 0)).w; }
float2 SelfEngine_FfxSssrMotionVectorScale() {
    if (g_motion_vector_contract_ready != 0u && g_motion_vector_mode == 2u) {
        return float2(0.5, -0.5);
    }
    return float2(1.0, 1.0);
}
bool FFX_DNSR_Reflections_HitPositionReprojectionEnabled() {
    return g_reprojection_contract_ready != 0u && g_hit_reprojection_enabled != 0u;
}
float2 FFX_DNSR_Reflections_LoadMotionVector(int2 pixel_coordinate) { return g_motion_vector.Load(int3(pixel_coordinate, 0)) * SelfEngine_FfxSssrMotionVectorScale(); }
min16float3 FFX_DNSR_Reflections_SamplePreviousAverageRadiance(float2 uv) { return (min16float3)g_average_radiance_history.SampleLevel(g_linear_sampler, uv, 0.0f).xyz; }
min16float FFX_DNSR_Reflections_SampleVarianceHistory(float2 uv) { return (min16float)g_variance_history.SampleLevel(g_linear_sampler, uv, 0.0f).x; }
min16float FFX_DNSR_Reflections_LoadRayLength(int2 pixel_coordinate) { return (min16float)g_in_radiance.Load(int3(pixel_coordinate, 0)).w; }
void FFX_DNSR_Reflections_StoreRadianceReprojected(int2 pixel_coordinate, min16float3 value) { g_out_reprojected_radiance[pixel_coordinate] = value.xyzz; }
void FFX_DNSR_Reflections_StoreAverageRadiance(int2 pixel_coordinate, min16float3 value) { g_out_average_radiance[pixel_coordinate] = value.xyzz; }
void FFX_DNSR_Reflections_StoreVariance(int2 pixel_coordinate, min16float value) { g_out_variance[pixel_coordinate] = value; }
void FFX_DNSR_Reflections_StoreNumSamples(int2 pixel_coordinate, min16float value) { g_out_sample_count[pixel_coordinate] = value; }
#include "ffx_denoiser_reflections_reproject.h"

[numthreads(8, 8, 1)]
void main(int2 group_thread_id      : SV_GroupThreadID,
                uint group_index    : SV_GroupIndex,
                uint    group_id    : SV_GroupID) {
    uint  packed_coords               = g_denoiser_tile_list[group_id];
    int2  dispatch_thread_id          = int2(packed_coords & 0xffffu, (packed_coords >> 16) & 0xffffu) + group_thread_id;
    int2  dispatch_group_id           = dispatch_thread_id / 8;
    uint2 remapped_group_thread_id    = FFX_DNSR_Reflections_RemapLane8x8(group_index);
    uint2 remapped_dispatch_thread_id = dispatch_group_id * 8 + remapped_group_thread_id;

    FFX_DNSR_Reflections_Reproject(remapped_dispatch_thread_id, remapped_group_thread_id, g_buffer_dimensions, g_temporal_stability_factor, 32);
}
