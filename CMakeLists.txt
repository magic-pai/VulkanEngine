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

find_program(GLSLC_EXECUTABLE glslc
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

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
    ${SHADER_SOURCE_DIR}/deferred_lighting.vert
    ${SHADER_SOURCE_DIR}/deferred_lighting.frag
    ${SHADER_SOURCE_DIR}/hdr_composite.vert
    ${SHADER_SOURCE_DIR}/hdr_composite.frag
    ${SHADER_SOURCE_DIR}/gbuffer_debug.vert
    ${SHADER_SOURCE_DIR}/gbuffer_debug.frag
    ${SHADER_SOURCE_DIR}/light_tile_cull.comp
    ${SHADER_SOURCE_DIR}/light_cluster_cull.comp
    ${SHADER_SOURCE_DIR}/depth_prefill_3d.vert
    ${SHADER_SOURCE_DIR}/shadow_depth.vert
    ${SHADER_SOURCE_DIR}/black_hole.frag
)

set(SHADER_OUTPUTS)
foreach(SHADER_SOURCE ${SHADER_SOURCES})
    get_filename_component(SHADER_FILE_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_FILE_NAME}.spv)

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        VERBATIM
    )

    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(SelfEngineShaders DEPENDS ${SHADER_OUTPUTS})

set(SELFENGINE_COMMON_SOURCES
    src/app/application.cpp
    src/app/application_2d.cpp
    src/app/benchmark_recorder.cpp
    src/assets/model_importer.cpp
    src/assets/unreal_project_bridge.cpp
    src/renderer/render_queue.cpp
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
    src/renderer/vulkan/depth_buffer.cpp
    src/renderer/vulkan/surface.cpp
    src/renderer/vulkan/pipeline_spec.cpp
    src/renderer/vulkan/physical_device.cpp
    src/renderer/vulkan/device.cpp
    src/renderer/vulkan/swapchain.cpp
    src/renderer/vulkan/render_pass.cpp
    src/renderer/vulkan/framebuffer.cpp
    src/renderer/vulkan/frame_graph.cpp
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
    src/renderer/vulkan/graphics_pipeline.cpp
    src/renderer/vulkan/compute_pipeline.cpp
    src/renderer/vulkan/uniform_buffer.cpp
    src/renderer/vulkan/vertex_buffer.cpp
    src/renderer/vulkan/index_buffer.cpp
    src/renderer/vulkan/mesh.cpp
    src/renderer/vulkan/texture_cache.cpp
)

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

add_dependencies(SelfEngine SelfEngineShaders)
add_dependencies(SelfEngineBlackHole SelfEngineShaders)
add_dependencies(SelfEngineForward3D SelfEngineShaders)

foreach(SELFENGINE_TARGET SelfEngine SelfEngineBlackHole SelfEngineForward3D)
    target_compile_definitions(${SELFENGINE_TARGET} PRIVATE
        SE_SHADER_DIR="${SHADER_OUTPUT_DIR}"
        SE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )

    target_include_directories(${SELFENGINE_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/include
        $ENV{VULKAN_SDK}/Include
    )

    target_link_libraries(${SELFENGINE_TARGET} PRIVATE
        assimp
        nlohmann_json::nlohmann_json
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/lib/glfw3.lib
        $ENV{VULKAN_SDK}/Lib/vulkan-1.lib
        user32
        gdi32
        shell32
    )
endforeach()

if (MSVC)
    target_compile_options(SelfEngine PRIVATE /W4 /permissive-)
    target_compile_options(SelfEngineBlackHole PRIVATE /W4 /permissive-)
    target_compile_options(SelfEngineForward3D PRIVATE /W4 /permissive-)
endif()
