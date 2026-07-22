cmake_minimum_required(VERSION 3.20)

project(SelfEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v6.0.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(assimp)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG v1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(meshoptimizer)

find_program(GLSLC_EXECUTABLE glslc
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)
find_program(DXC_EXECUTABLE dxc
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

if(WIN32)
    find_program(POWERSHELL_EXECUTABLE powershell REQUIRED)
    find_program(SPIRV_DIS_EXECUTABLE spirv-dis
        HINTS "$ENV{VULKAN_SDK}/Bin"
        REQUIRED
    )
    find_program(SPIRV_AS_EXECUTABLE spirv-as
        HINTS "$ENV{VULKAN_SDK}/Bin"
        REQUIRED
    )
    find_program(SPIRV_VAL_EXECUTABLE spirv-val
        HINTS "$ENV{VULKAN_SDK}/Bin"
        REQUIRED
    )
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/triangle.vert
    ${SHADER_SOURCE_DIR}/triangle.frag
    ${SHADER_SOURCE_DIR}/forward_3d.vert
    ${SHADER_SOURCE_DIR}/forward_3d_instanced.vert
    ${SHADER_SOURCE_DIR}/forward_3d.frag
    ${SHADER_SOURCE_DIR}/weighted_translucency_3d.frag
    ${SHADER_SOURCE_DIR}/weighted_translucency_resolve.frag
    ${SHADER_SOURCE_DIR}/gbuffer_3d.vert
    ${SHADER_SOURCE_DIR}/gbuffer_3d.frag
    ${SHADER_SOURCE_DIR}/forward_velocity_3d.frag
    ${SHADER_SOURCE_DIR}/dlss_mask_3d.frag
    ${SHADER_SOURCE_DIR}/deferred_lighting.vert
    ${SHADER_SOURCE_DIR}/deferred_lighting.frag
    ${SHADER_SOURCE_DIR}/ffx_sssr_apply.frag
    ${SHADER_SOURCE_DIR}/hdr_composite.vert
    ${SHADER_SOURCE_DIR}/hdr_composite.frag
    ${SHADER_SOURCE_DIR}/taa_resolve.frag
    ${SHADER_SOURCE_DIR}/bloom_downsample.frag
    ${SHADER_SOURCE_DIR}/bloom_upsample.frag
    ${SHADER_SOURCE_DIR}/gbuffer_debug.vert
    ${SHADER_SOURCE_DIR}/gbuffer_debug.frag
    ${SHADER_SOURCE_DIR}/auto_exposure_histogram.comp
    ${SHADER_SOURCE_DIR}/light_tile_cull.comp
    ${SHADER_SOURCE_DIR}/light_cluster_cull.comp
      ${SHADER_SOURCE_DIR}/ssr_depth_pyramid.comp
      ${SHADER_SOURCE_DIR}/ssr_trace.comp
      ${SHADER_SOURCE_DIR}/ssr_temporal.comp
      ${SHADER_SOURCE_DIR}/ssr_spatial.comp
      ${SHADER_SOURCE_DIR}/ssr_diagnostics.comp
    ${SHADER_SOURCE_DIR}/reflection_probe_diffuse_irradiance.comp
    ${SHADER_SOURCE_DIR}/reflection_probe_prefilter.comp
    ${SHADER_SOURCE_DIR}/depth_prefill_3d.vert
    ${SHADER_SOURCE_DIR}/shadow_depth.vert
    ${SHADER_SOURCE_DIR}/black_hole.frag
)

set(SHADER_OUTPUTS)
set(FFX_SSSR_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/fidelityfx_sssr)
set(FFX_SSSR_SHADER_SOURCE_DIR ${FFX_SSSR_SOURCE_DIR}/shaders)
set(FFX_SSSR_INCLUDE_DIRS
    ${FFX_SSSR_SHADER_SOURCE_DIR}
    ${FFX_SSSR_SOURCE_DIR}/include/ffx-sssr
    ${FFX_SSSR_SOURCE_DIR}/include/ffx-dnsr
    ${FFX_SSSR_SOURCE_DIR}/include/ffx-spd
)
set(FFX_SSSR_HLSL_SHADERS
    ${FFX_SSSR_SHADER_SOURCE_DIR}/ClassifyTiles.hlsl
    ${FFX_SSSR_SHADER_SOURCE_DIR}/PrepareBlueNoiseTexture.hlsl
    ${FFX_SSSR_SHADER_SOURCE_DIR}/PrepareIndirectArgs.hlsl
    ${FFX_SSSR_SHADER_SOURCE_DIR}/Intersect.hlsl
    ${FFX_SSSR_SHADER_SOURCE_DIR}/Reproject.hlsl
    ${FFX_SSSR_SHADER_SOURCE_DIR}/Prefilter.hlsl
    ${FFX_SSSR_SHADER_SOURCE_DIR}/ResolveTemporal.hlsl
    ${FFX_SSSR_SHADER_SOURCE_DIR}/DepthDownsample.hlsl
)
set(LOCAL_SHADOW_CONSUMER_SHADERS
    forward_3d.frag
    deferred_lighting.frag
    weighted_translucency_3d.frag
)
foreach(SHADER_SOURCE ${SHADER_SOURCES})
    get_filename_component(SHADER_FILE_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_FILE_NAME}.spv)

    if(WIN32 AND SHADER_FILE_NAME IN_LIST LOCAL_SHADOW_CONSUMER_SHADERS)
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
            COMMAND ${POWERSHELL_EXECUTABLE}
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/Set-SpirvShadowFunctionControl.ps1
                -InputPath ${SHADER_OUTPUT}
                -SpirvDisPath ${SPIRV_DIS_EXECUTABLE}
                -SpirvAsPath ${SPIRV_AS_EXECUTABLE}
                -SpirvValPath ${SPIRV_VAL_EXECUTABLE}
            DEPENDS
                ${SHADER_SOURCE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/Set-SpirvShadowFunctionControl.ps1
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE}
            VERBATIM
        )
    endif()

    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

set(FFX_SSSR_APPLY_AUDIT_OUTPUT
    ${SHADER_OUTPUT_DIR}/ffx_sssr_apply_audit.frag.spv
)
add_custom_command(
    OUTPUT ${FFX_SSSR_APPLY_AUDIT_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE}
        -DSE_REFLECTION_FULL_AUDIT=1
        ${SHADER_SOURCE_DIR}/ffx_sssr_apply.frag
        -o ${FFX_SSSR_APPLY_AUDIT_OUTPUT}
    DEPENDS ${SHADER_SOURCE_DIR}/ffx_sssr_apply.frag
    VERBATIM
)
list(APPEND SHADER_OUTPUTS ${FFX_SSSR_APPLY_AUDIT_OUTPUT})

set(GBUFFER_3D_AUDIT_OUTPUT
    ${SHADER_OUTPUT_DIR}/gbuffer_3d_audit.frag.spv
)
add_custom_command(
    OUTPUT ${GBUFFER_3D_AUDIT_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE}
        -DSE_REFLECTION_FULL_AUDIT=1
        ${SHADER_SOURCE_DIR}/gbuffer_3d.frag
        -o ${GBUFFER_3D_AUDIT_OUTPUT}
    DEPENDS ${SHADER_SOURCE_DIR}/gbuffer_3d.frag
    VERBATIM
)
list(APPEND SHADER_OUTPUTS ${GBUFFER_3D_AUDIT_OUTPUT})

foreach(FFX_SSSR_SHADER_SOURCE ${FFX_SSSR_HLSL_SHADERS})
    get_filename_component(FFX_SSSR_SHADER_FILE_NAME ${FFX_SSSR_SHADER_SOURCE} NAME)
    set(FFX_SSSR_SHADER_OUTPUT
        ${SHADER_OUTPUT_DIR}/ffx_sssr_${FFX_SSSR_SHADER_FILE_NAME}.spv)

    set(FFX_SSSR_INCLUDE_ARGS)
    foreach(FFX_SSSR_INCLUDE_DIR ${FFX_SSSR_INCLUDE_DIRS})
        list(APPEND FFX_SSSR_INCLUDE_ARGS -I${FFX_SSSR_INCLUDE_DIR})
    endforeach()

    add_custom_command(
        OUTPUT ${FFX_SSSR_SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
        COMMAND ${DXC_EXECUTABLE}
            -spirv
            "-fspv-target-env=vulkan1.2"
            -T cs_6_6
            -E main
            -DSELFENGINE_FFX_POSITIVE_VIEWPORT=1
            ${FFX_SSSR_INCLUDE_ARGS}
            ${FFX_SSSR_SHADER_SOURCE}
            -Fo ${FFX_SSSR_SHADER_OUTPUT}
        DEPENDS
            ${FFX_SSSR_SHADER_SOURCE}
            ${FFX_SSSR_SHADER_SOURCE_DIR}/Common.hlsl
            ${FFX_SSSR_SOURCE_DIR}/include/ffx-sssr/ffx_sssr.h
            ${FFX_SSSR_SOURCE_DIR}/include/ffx-dnsr/ffx_denoiser_reflections_common.h
            ${FFX_SSSR_SOURCE_DIR}/include/ffx-dnsr/ffx_denoiser_reflections_config.h
            ${FFX_SSSR_SOURCE_DIR}/include/ffx-dnsr/ffx_denoiser_reflections_prefilter.h
            ${FFX_SSSR_SOURCE_DIR}/include/ffx-dnsr/ffx_denoiser_reflections_reproject.h
            ${FFX_SSSR_SOURCE_DIR}/include/ffx-dnsr/ffx_denoiser_reflections_resolve_temporal.h
            ${FFX_SSSR_SOURCE_DIR}/include/ffx-spd/ffx_a.h
            ${FFX_SSSR_SOURCE_DIR}/include/ffx-spd/ffx_spd.h
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS ${FFX_SSSR_SHADER_OUTPUT})
endforeach()

set(HYBRID_REFLECTION_RAY_QUERY_SHADER
    ${SHADER_SOURCE_DIR}/hybrid_reflection_ray_query.hlsl
)
set(HYBRID_REFLECTION_RAY_QUERY_OUTPUT
    ${SHADER_OUTPUT_DIR}/hybrid_reflection_ray_query.hlsl.spv
)
set(HYBRID_REFLECTION_RAY_QUERY_AUDIT_OUTPUT
    ${SHADER_OUTPUT_DIR}/hybrid_reflection_ray_query_audit.hlsl.spv
)
set(HYBRID_REFLECTION_RAY_QUERY_INCLUDE_ARGS)
foreach(FFX_SSSR_INCLUDE_DIR ${FFX_SSSR_INCLUDE_DIRS})
    list(APPEND HYBRID_REFLECTION_RAY_QUERY_INCLUDE_ARGS
        -I${FFX_SSSR_INCLUDE_DIR}
    )
endforeach()
add_custom_command(
    OUTPUT ${HYBRID_REFLECTION_RAY_QUERY_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${DXC_EXECUTABLE}
        -spirv
        "-fspv-target-env=vulkan1.2"
        -T cs_6_6
        -E main
        -DSELFENGINE_FFX_POSITIVE_VIEWPORT=1
        ${HYBRID_REFLECTION_RAY_QUERY_INCLUDE_ARGS}
        ${HYBRID_REFLECTION_RAY_QUERY_SHADER}
        -Fo ${HYBRID_REFLECTION_RAY_QUERY_OUTPUT}
    DEPENDS
        ${HYBRID_REFLECTION_RAY_QUERY_SHADER}
        ${FFX_SSSR_SHADER_SOURCE_DIR}/Common.hlsl
        ${FFX_SSSR_SOURCE_DIR}/include/ffx-sssr/ffx_sssr.h
    VERBATIM
)
list(APPEND SHADER_OUTPUTS ${HYBRID_REFLECTION_RAY_QUERY_OUTPUT})
add_custom_command(
    OUTPUT ${HYBRID_REFLECTION_RAY_QUERY_AUDIT_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${DXC_EXECUTABLE}
        -spirv
        "-fspv-target-env=vulkan1.2"
        -T cs_6_6
        -E main
        -DSELFENGINE_FFX_POSITIVE_VIEWPORT=1
        -DSE_HYBRID_REFLECTION_FULL_AUDIT=1
        ${HYBRID_REFLECTION_RAY_QUERY_INCLUDE_ARGS}
        ${HYBRID_REFLECTION_RAY_QUERY_SHADER}
        -Fo ${HYBRID_REFLECTION_RAY_QUERY_AUDIT_OUTPUT}
    DEPENDS
        ${HYBRID_REFLECTION_RAY_QUERY_SHADER}
        ${FFX_SSSR_SHADER_SOURCE_DIR}/Common.hlsl
        ${FFX_SSSR_SOURCE_DIR}/include/ffx-sssr/ffx_sssr.h
    VERBATIM
)
list(APPEND SHADER_OUTPUTS ${HYBRID_REFLECTION_RAY_QUERY_AUDIT_OUTPUT})

add_custom_target(SelfEngineShaders DEPENDS ${SHADER_OUTPUTS})

set(SELFENGINE_COMMON_SOURCES
    src/app/application.cpp
    src/app/application_2d.cpp
    src/app/benchmark_recorder.cpp
    src/app/renderdoc_capture.cpp
    src/assets/model_importer.cpp
    src/assets/unreal_project_bridge.cpp
    src/renderer/render_queue.cpp
    src/renderer/temporal_upscaler.cpp
    src/scene/transform.cpp
    src/scene/renderable_2d.cpp
    src/scene/renderable_3d.cpp
    src/scene/renderable_2d_order.cpp
    src/scene/scene_2d.cpp
    src/scene/scene_3d.cpp
    src/scene/sample_scene_2d.cpp
    src/scene/runtime_model_loader.cpp
    src/scene/camera_2d.cpp
    src/scene/camera_3d.cpp
    src/scene/mesh_factory.cpp
    src/platform/window.cpp
    thirdParty/imgui/imgui.cpp
    thirdParty/imgui/imgui_demo.cpp
    thirdParty/imgui/imgui_draw.cpp
    thirdParty/imgui/imgui_tables.cpp
    thirdParty/imgui/imgui_widgets.cpp
    thirdParty/imgui/backends/imgui_impl_glfw.cpp
    thirdParty/imgui/backends/imgui_impl_vulkan.cpp
    src/renderer/vulkan/Instance.cpp
    src/renderer/vulkan/buffer.cpp
    src/renderer/vulkan/image.cpp
    src/renderer/vulkan/instance_buffer.cpp
    src/renderer/vulkan/texture_2d.cpp
    src/renderer/vulkan/cubemap.cpp
    src/renderer/vulkan/shadow_map.cpp
    src/renderer/vulkan/shadow_cascade_atlas.cpp
    src/renderer/vulkan/local_shadow_atlas.cpp
    src/renderer/vulkan/shadow_render_pass.cpp
    src/renderer/vulkan/shadow_framebuffer.cpp
    src/renderer/vulkan/sampler.cpp
    src/renderer/vulkan/material.cpp
    src/renderer/vulkan/material_library.cpp
    src/renderer/vulkan/render_resources_2d.cpp
    src/renderer/vulkan/render_targets.cpp
    src/renderer/vulkan/reflection_probe_resources.cpp
    src/renderer/vulkan/fidelityfx_sssr_adapter.cpp
    src/renderer/vulkan/hybrid_reflection_acceleration_structures.cpp
    src/renderer/vulkan/hybrid_reflection_ray_query.cpp
    src/renderer/vulkan/depth_buffer.cpp
    src/renderer/vulkan/surface.cpp
    src/renderer/vulkan/pipeline_spec.cpp
    src/renderer/vulkan/physical_device.cpp
    src/renderer/vulkan/device.cpp
    src/renderer/vulkan/swapchain.cpp
    src/renderer/vulkan/render_pass.cpp
    src/renderer/vulkan/framebuffer.cpp
    src/renderer/vulkan/frame_graph.cpp
    src/renderer/vulkan/render_feature_registry.cpp
    src/renderer/vulkan/features/height_fog_feature.cpp
    src/renderer/vulkan/features/post_process_feature.cpp
    src/renderer/vulkan/features/reflection_probe_fallback_feature.cpp
    src/renderer/vulkan/features/ssao_feature.cpp
    src/renderer/vulkan/features/ssr_feature.cpp
    src/renderer/vulkan/gpu_timer.cpp
    src/renderer/vulkan/command_pool.cpp
    src/renderer/vulkan/upload_batch.cpp
    src/renderer/vulkan/command_buffer.cpp
    src/renderer/vulkan/descriptor_set_layout.cpp
    src/renderer/vulkan/descriptor_sets.cpp
    src/renderer/vulkan/sync_objects.cpp
    src/renderer/vulkan/renderer.cpp
    src/renderer/vulkan/imgui_layer.cpp
    src/renderer/vulkan/shader_module.cpp
    src/renderer/vulkan/pipeline_cache.cpp
    src/renderer/vulkan/graphics_pipeline.cpp
    src/renderer/vulkan/compute_pipeline.cpp
    src/renderer/vulkan/uniform_buffer.cpp
    src/renderer/vulkan/vertex_buffer.cpp
    src/renderer/vulkan/index_buffer.cpp
    src/renderer/vulkan/mesh.cpp
    src/renderer/vulkan/texture_cache.cpp
    src/renderer/vulkan/ibl_generator.cpp
    src/renderer/vulkan/mesh_lod.cpp
)

set(SELFENGINE_NVIDIA_DLSS_SDK_DIR "$ENV{SE_NVIDIA_DLSS_SDK_DIR}" CACHE PATH "Optional NVIDIA DLSS SDK root")
if (SELFENGINE_NVIDIA_DLSS_SDK_DIR STREQUAL "" AND NOT "$ENV{SE_DLSS_SDK_DIR}" STREQUAL "")
    set(SELFENGINE_NVIDIA_DLSS_SDK_DIR "$ENV{SE_DLSS_SDK_DIR}" CACHE PATH "Optional NVIDIA DLSS SDK root" FORCE)
endif()
if (SELFENGINE_NVIDIA_DLSS_SDK_DIR STREQUAL "")
    set(SELFENGINE_NVIDIA_DLSS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/nvidia_dlss" CACHE PATH "Optional NVIDIA DLSS SDK root" FORCE)
endif()

set(SELFENGINE_NVIDIA_DLSS_ENABLED FALSE)
set(SELFENGINE_NVIDIA_DLSS_LIBRARY_RELEASE "${SELFENGINE_NVIDIA_DLSS_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib")
set(SELFENGINE_NVIDIA_DLSS_LIBRARY_DEBUG "${SELFENGINE_NVIDIA_DLSS_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_d_dbg.lib")
if (NOT EXISTS "${SELFENGINE_NVIDIA_DLSS_LIBRARY_RELEASE}")
    set(SELFENGINE_NVIDIA_DLSS_LIBRARY_RELEASE "${SELFENGINE_NVIDIA_DLSS_SDK_DIR}/lib/Windows_x86_64/khr/x64/nvsdk_ngx_khr_d.lib")
    set(SELFENGINE_NVIDIA_DLSS_LIBRARY_DEBUG "${SELFENGINE_NVIDIA_DLSS_SDK_DIR}/lib/Windows_x86_64/khr/x64/nvsdk_ngx_khr_d_dbg.lib")
endif()
set(SELFENGINE_NVIDIA_DLSS_RUNTIME_DLL "${SELFENGINE_NVIDIA_DLSS_SDK_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll")
if (WIN32 AND
    EXISTS "${SELFENGINE_NVIDIA_DLSS_SDK_DIR}/include/nvsdk_ngx_vk.h" AND
    EXISTS "${SELFENGINE_NVIDIA_DLSS_SDK_DIR}/include/nvsdk_ngx_helpers.h" AND
    EXISTS "${SELFENGINE_NVIDIA_DLSS_LIBRARY_RELEASE}" AND
    EXISTS "${SELFENGINE_NVIDIA_DLSS_LIBRARY_DEBUG}" AND
    EXISTS "${SELFENGINE_NVIDIA_DLSS_RUNTIME_DLL}")
    set(SELFENGINE_NVIDIA_DLSS_ENABLED TRUE)
endif()

add_executable(SelfEngine
    src/main.cpp
    ${SELFENGINE_COMMON_SOURCES}
)

add_executable(SelfEngineBlackHole
    src/black_hole.cpp
    ${SELFENGINE_COMMON_SOURCES}
)

add_executable(SelfEngineForward3D
    src/forward_3d.cpp
    ${SELFENGINE_COMMON_SOURCES}
)

add_executable(SelfEngineLightingShowcase
    src/forward_3d.cpp
    ${SELFENGINE_COMMON_SOURCES}
)

add_dependencies(SelfEngine SelfEngineShaders)
add_dependencies(SelfEngineBlackHole SelfEngineShaders)
add_dependencies(SelfEngineForward3D SelfEngineShaders)
add_dependencies(SelfEngineLightingShowcase SelfEngineShaders)

foreach(SELFENGINE_TARGET SelfEngine SelfEngineBlackHole SelfEngineForward3D SelfEngineLightingShowcase)
    if (WIN32)
        set_target_properties(${SELFENGINE_TARGET} PROPERTIES
            WIN32_EXECUTABLE TRUE
        )
        if (MSVC)
            target_link_options(${SELFENGINE_TARGET} PRIVATE /ENTRY:mainCRTStartup)
        endif()
    endif()

    target_compile_definitions(${SELFENGINE_TARGET} PRIVATE
        SE_SHADER_DIR="${SHADER_OUTPUT_DIR}"
        SE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        SE_ENABLE_FIDELITYFX_SSSR=1
        SE_FIDELITYFX_SSSR_SHADER_COUNT=8
    )
    set_target_properties(${SELFENGINE_TARGET} PROPERTIES
        CXX_SCAN_FOR_MODULES OFF
    )

    target_include_directories(${SELFENGINE_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/include
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/renderdoc
        $ENV{VULKAN_SDK}/Include
    )

    target_link_libraries(${SELFENGINE_TARGET} PRIVATE
        assimp
        nlohmann_json::nlohmann_json
        meshoptimizer
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/lib/glfw3.lib
        $ENV{VULKAN_SDK}/Lib/vulkan-1.lib
        user32
        gdi32
        shell32
    )

    if (SELFENGINE_NVIDIA_DLSS_ENABLED)
        target_compile_definitions(${SELFENGINE_TARGET} PRIVATE
            SE_ENABLE_NVIDIA_DLSS=1
            SE_NVIDIA_DLSS_SDK_DIR="${SELFENGINE_NVIDIA_DLSS_SDK_DIR}"
        )
        target_include_directories(${SELFENGINE_TARGET} PRIVATE
            ${SELFENGINE_NVIDIA_DLSS_SDK_DIR}/include
        )
        target_link_libraries(${SELFENGINE_TARGET} PRIVATE
            $<$<CONFIG:Debug>:${SELFENGINE_NVIDIA_DLSS_LIBRARY_DEBUG}>
            $<$<NOT:$<CONFIG:Debug>>:${SELFENGINE_NVIDIA_DLSS_LIBRARY_RELEASE}>
        )
        add_custom_command(TARGET ${SELFENGINE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SELFENGINE_NVIDIA_DLSS_RUNTIME_DLL}
                $<TARGET_FILE_DIR:${SELFENGINE_TARGET}>
            VERBATIM
        )
    endif()
endforeach()

if (MSVC)
    target_compile_options(SelfEngine PRIVATE /W4 /permissive- /FS)
    target_compile_options(SelfEngineBlackHole PRIVATE /W4 /permissive- /FS)
    target_compile_options(SelfEngineForward3D PRIVATE /W4 /permissive- /FS)
    target_compile_options(SelfEngineLightingShowcase PRIVATE /W4 /permissive- /FS)
endif()

target_compile_definitions(SelfEngineLightingShowcase PRIVATE
    SE_FORCE_LIGHTING_SHOWCASE=1
)
