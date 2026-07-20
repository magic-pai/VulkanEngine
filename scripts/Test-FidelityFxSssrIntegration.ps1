[CmdletBinding()]
param(
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$ShowcaseExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [switch]$SkipBuild,
    [switch]$StaticOnly,
    [switch]$Strict,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot "tmp\ffx_sssr_integration_health"
}
if (![System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $projectRoot $Path))
}

function New-Check {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Actual,
        [string]$Expected
    )

    return [pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }
}

function Get-UIntMetric {
    param([pscustomobject]$Row, [string]$Name)

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV metric: $Name"
    }
    return [uint32]$property.Value
}

function Get-FloatMetric {
    param([pscustomobject]$Row, [string]$Name)

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV metric: $Name"
    }
    return [double]::Parse(
        [string]$property.Value,
        [System.Globalization.CultureInfo]::InvariantCulture
    )
}

function Set-ProcessEnvironment {
    param([hashtable]$Values)

    $previous = @{}
    foreach ($entry in $Values.GetEnumerator()) {
        $previous[$entry.Key] =
            [Environment]::GetEnvironmentVariable($entry.Key, "Process")
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
    return $previous
}

function Restore-ProcessEnvironment {
    param([hashtable]$Values)

    foreach ($entry in $Values.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
}

function Invoke-Build {
    $buildDirectory = Join-Path $projectRoot "build"
    $cmakeCommand = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if ([string]::IsNullOrWhiteSpace($cmakeCommand)) {
        $vsCmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path -LiteralPath $vsCmake) {
            $cmakeCommand = $vsCmake
        }
    }
    if ([string]::IsNullOrWhiteSpace($cmakeCommand)) {
        throw "Unable to locate cmake.exe"
    }
    & $cmakeCommand -S $projectRoot -B $buildDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE"
    }

    $vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    foreach ($project in @(
            "SelfEngineShaders.vcxproj",
            "SelfEngineForward3D.vcxproj",
            "SelfEngineLightingShowcase.vcxproj"
        )) {
        & cmd.exe /c "call `"$vcvars`" >nul 2>&1 && cd /d `"$buildDirectory`" && MSBuild $project /p:Configuration=Debug /v:minimal /nologo"
        if ($LASTEXITCODE -ne 0) {
            throw "$project build failed with exit code $LASTEXITCODE"
        }
    }
}

function Invoke-StaticChecks {
    $vendorRoot = Join-Path $projectRoot "thirdParty\fidelityfx_sssr"
    $cmakePath = Join-Path $projectRoot "CMakeLists.txt"
    $cmakeSource = Get-Content -Raw -LiteralPath $cmakePath
    $adapterSource = Get-Content -Raw -LiteralPath (
        Join-Path $projectRoot "src\renderer\vulkan\fidelityfx_sssr_adapter.cpp")
    $adapterHeader = Get-Content -Raw -LiteralPath (
        Join-Path $projectRoot "src\renderer\vulkan\fidelityfx_sssr_adapter.h")
    $rendererSource = Get-Content -Raw -LiteralPath (
        Join-Path $projectRoot "src\renderer\vulkan\renderer.cpp")
    $renderFeatureSource = Get-Content -Raw -LiteralPath (
        Join-Path $projectRoot "src\renderer\vulkan\render_feature.h")
    $commandBufferSource = Get-Content -Raw -LiteralPath (
        Join-Path $projectRoot "src\renderer\vulkan\command_buffer.cpp")
    $descriptorSetsSource = Get-Content -Raw -LiteralPath (
        Join-Path $projectRoot "src\renderer\vulkan\descriptor_sets.cpp")
    $deferredLightingShader = Get-Content -Raw -LiteralPath (
        Join-Path $projectRoot "assets\shaders\deferred_lighting.frag")
    $benchmarkRecorderSource = Get-Content -Raw -LiteralPath (
        Join-Path $projectRoot "src\app\benchmark_recorder.cpp")
    $ssrFeatureSource = Get-Content -Raw -LiteralPath (
        Join-Path $projectRoot "src\renderer\vulkan\features\ssr_feature.cpp")
    $classifyShader = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "shaders\ClassifyTiles.hlsl")
    $blueNoiseShader = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "shaders\PrepareBlueNoiseTexture.hlsl")
    $intersectShader = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "shaders\Intersect.hlsl")
    $reprojectShader = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "shaders\Reproject.hlsl")
    $prefilterShader = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "shaders\Prefilter.hlsl")
    $resolveTemporalShader = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "shaders\ResolveTemporal.hlsl")
    $commonShader = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "shaders\Common.hlsl")
    $sssrHeader = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "include\ffx-sssr\ffx_sssr.h")
    $dnsrCommon = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "include\ffx-dnsr\ffx_denoiser_reflections_common.h")
    $checks = @()

    $requiredFiles = @(
        "LICENSE.txt",
        "NOTICES.txt",
        "SELFENGINE_INTEGRATION.md",
        "include\ffx-sssr\ffx_sssr.h",
        "include\ffx-dnsr\ffx_denoiser_reflections_common.h",
        "include\ffx-dnsr\ffx_denoiser_reflections_prefilter.h",
        "include\ffx-dnsr\ffx_denoiser_reflections_reproject.h",
        "include\ffx-dnsr\ffx_denoiser_reflections_resolve_temporal.h",
        "include\ffx-spd\ffx_a.h",
        "include\ffx-spd\ffx_spd.h",
        "data\blue_noise_tables_128x128_1spp.inl",
        "shaders\ClassifyTiles.hlsl",
        "shaders\PrepareBlueNoiseTexture.hlsl",
        "shaders\Intersect.hlsl",
        "shaders\Reproject.hlsl",
        "shaders\Prefilter.hlsl",
        "shaders\ResolveTemporal.hlsl",
        "shaders\DepthDownsample.hlsl"
    )
    foreach ($relativePath in $requiredFiles) {
        $fullPath = Join-Path $vendorRoot $relativePath
        $checks += New-Check `
            "FFX SSSR vendor file exists: $relativePath" `
            (Test-Path -LiteralPath $fullPath) `
            $fullPath `
            "exists"
    }

    $shaderOutputDirectory = Join-Path $projectRoot "build\shaders"
    $shaderNames = @(
        "ClassifyTiles",
        "PrepareBlueNoiseTexture",
        "PrepareIndirectArgs",
        "Intersect",
        "Reproject",
        "Prefilter",
        "ResolveTemporal",
        "DepthDownsample"
    )
    foreach ($shaderName in $shaderNames) {
        $spvPath = Join-Path $shaderOutputDirectory (
            "ffx_sssr_$shaderName.hlsl.spv")
        $spv = Get-Item -LiteralPath $spvPath -ErrorAction SilentlyContinue
        $spvActual = "missing"
        if ($null -ne $spv) {
            $spvActual = "$($spv.Length) bytes"
        }
        $checks += New-Check `
            "FFX SSSR SPIR-V exists: $shaderName" `
            ($null -ne $spv -and $spv.Length -gt 0) `
            $spvActual `
            "non-empty SPIR-V"
    }

    $checks += New-Check `
        "FFX SSSR core header present" `
        ($sssrHeader -match "FFX_SSSR_HierarchicalRaymarch") `
        "contains=$($sssrHeader -match 'FFX_SSSR_HierarchicalRaymarch')" `
        "true"
    $sssrSelectPatchPresent = $sssrHeader -match "select\(direction != 0"
    $dnsrSelectPatchPresent =
        $dnsrCommon -match "select\(\(round_down == value\)"
    $checks += New-Check `
        "FFX SSSR DXC vector select patch present" `
        ($sssrSelectPatchPresent -and $dnsrSelectPatchPresent) `
        "sssrSelect=$sssrSelectPatchPresent,dnsrSelect=$dnsrSelectPatchPresent" `
        "true/true"
    $checks += New-Check `
        "CMake compiles FFX SSSR shaders with DXC" `
        ($cmakeSource -match "DXC_EXECUTABLE" -and
            $cmakeSource -match "FFX_SSSR_HLSL_SHADERS" -and
            $cmakeSource -match "ffx_sssr_") `
        "dxc=$($cmakeSource -match 'DXC_EXECUTABLE'),list=$($cmakeSource -match 'FFX_SSSR_HLSL_SHADERS'),prefix=$($cmakeSource -match 'ffx_sssr_')" `
        "true/true/true"
    $checks += New-Check `
        "FFX SSSR Common constants cbuffer is vendor-shaped" `
        ($commonShader -match "g_inv_view_proj" -and
            $commonShader -match "g_buffer_dimensions" -and
            $commonShader -match "g_samples_per_quad") `
        "invViewProj=$($commonShader -match 'g_inv_view_proj'),dims=$($commonShader -match 'g_buffer_dimensions'),samples=$($commonShader -match 'g_samples_per_quad')" `
        "true/true/true"
    $checks += New-Check `
        "FFX SSSR ClassifyTiles shader contract present" `
        ($classifyShader -match "Texture2D<float4> g_roughness" -and
            $classifyShader -match "RWBuffer<uint> g_ray_list" -and
            $classifyShader -match "RWTexture2D<float> g_extracted_roughness" -and
            $classifyShader -match "numthreads\(8, 8, 1\)") `
        "roughness=$($classifyShader -match 'Texture2D<float4> g_roughness'),rayList=$($classifyShader -match 'RWBuffer<uint> g_ray_list'),roughOut=$($classifyShader -match 'RWTexture2D<float> g_extracted_roughness')" `
        "true/true/true"
    $checks += New-Check `
        "FFX SSSR PrepareBlueNoiseTexture shader contract present" `
        ($blueNoiseShader -match "Buffer<uint> g_sobol_buffer" -and
            $blueNoiseShader -match "Buffer<uint> g_ranking_tile_buffer" -and
            $blueNoiseShader -match "Buffer<uint> g_scrambling_tile_buffer" -and
            $blueNoiseShader -match "RWTexture2D<float2> g_blue_noise_texture" -and
            $blueNoiseShader -match "numthreads\(8, 8, 1\)") `
        "sobol=$($blueNoiseShader -match 'Buffer<uint> g_sobol_buffer'),ranking=$($blueNoiseShader -match 'Buffer<uint> g_ranking_tile_buffer'),scrambling=$($blueNoiseShader -match 'Buffer<uint> g_scrambling_tile_buffer'),blueNoise=$($blueNoiseShader -match 'RWTexture2D<float2> g_blue_noise_texture')" `
        "true/true/true/true"
    $checks += New-Check `
        "FFX SSSR Intersect shader contract present" `
        ($intersectShader -match "Texture2D<float4> g_lit_scene" -and
            $intersectShader -match "Texture2D<float> g_depth_buffer_hierarchy" -and
            $intersectShader -match "Texture2D<float4> g_normal" -and
            $intersectShader -match "Texture2D<float> g_roughness" -and
            $intersectShader -match "TextureCube g_environment_map" -and
            $intersectShader -match "Texture2D<float2> g_blue_noise_texture" -and
            $intersectShader -match "Buffer<uint> g_ray_list" -and
            $intersectShader -match "RWTexture2D<float4> g_intersection_output" -and
            $intersectShader -match "RWBuffer<uint> g_ray_counter") `
        "lit=$($intersectShader -match 'Texture2D<float4> g_lit_scene'),depth=$($intersectShader -match 'Texture2D<float> g_depth_buffer_hierarchy'),rayList=$($intersectShader -match 'Buffer<uint> g_ray_list'),output=$($intersectShader -match 'RWTexture2D<float4> g_intersection_output')" `
        "true/true/true/true"
    $checks += New-Check `
        "FFX SSSR Reproject shader contract present" `
        ($reprojectShader -match "Texture2D<float> g_depth_buffer" -and
            $reprojectShader -match "Texture2D<float> g_roughness" -and
            $reprojectShader -match "Texture2D<float3> g_normal" -and
            $reprojectShader -match "Texture2D<float4> g_in_radiance" -and
            $reprojectShader -match "Texture2D<float2> g_motion_vector" -and
            $reprojectShader -match "Texture2D<float2> g_blue_noise_texture" -and
            $reprojectShader -match "RWTexture2D<float4> g_out_reprojected_radiance" -and
            $reprojectShader -match "RWTexture2D<float4> g_out_average_radiance" -and
            $reprojectShader -match "RWTexture2D<float> g_out_variance" -and
            $reprojectShader -match "RWTexture2D<float> g_out_sample_count" -and
            $reprojectShader -match "Buffer<uint> g_denoiser_tile_list" -and
            $commonShader -match "uint g_motion_vector_mode" -and
            $commonShader -match "uint g_motion_vector_contract_ready" -and
            $commonShader -match "uint g_hit_reprojection_enabled" -and
            $commonShader -match "uint g_reprojection_contract_ready" -and
            $reprojectShader -match "SelfEngine_FfxSssrMotionVectorScale" -and
            $reprojectShader -match "FFX_DNSR_Reflections_HitPositionReprojectionEnabled" -and
            $reprojectShader -match "g_out_reprojected_radiance\[pixel_coordinate\] = value\.xyzz" -and
            $reprojectShader -match "g_out_average_radiance\[pixel_coordinate\] = value\.xyzz" -and
            $reprojectShader -match "numthreads\(8, 8, 1\)") `
        "depth=$($reprojectShader -match 'Texture2D<float> g_depth_buffer'),radiance=$($reprojectShader -match 'Texture2D<float4> g_in_radiance'),outputs=$($reprojectShader -match 'RWTexture2D<float4> g_out_reprojected_radiance')/$($reprojectShader -match 'RWTexture2D<float4> g_out_average_radiance'),mv=$($commonShader -match 'uint g_motion_vector_mode')/$($reprojectShader -match 'SelfEngine_FfxSssrMotionVectorScale'),hit=$($commonShader -match 'uint g_hit_reprojection_enabled')/$($reprojectShader -match 'FFX_DNSR_Reflections_HitPositionReprojectionEnabled'),rgbaStores=$($reprojectShader -match 'g_out_reprojected_radiance\[pixel_coordinate\] = value\.xyzz')/$($reprojectShader -match 'g_out_average_radiance\[pixel_coordinate\] = value\.xyzz'),tiles=$($reprojectShader -match 'Buffer<uint> g_denoiser_tile_list')" `
        "true/true/true/true/true"
    $checks += New-Check `
        "FFX SSSR Prefilter shader contract present" `
        ($prefilterShader -match "Texture2D<float> g_depth_buffer" -and
            $prefilterShader -match "Texture2D<float> g_roughness" -and
            $prefilterShader -match "Texture2D<float3> g_normal" -and
            $prefilterShader -match "Texture2D<float3> g_average_radiance" -and
            $prefilterShader -match "Texture2D<float4> g_in_radiance" -and
            $prefilterShader -match "Texture2D<float> g_in_variance" -and
            $prefilterShader -match "Texture2D<float> g_in_sample_count" -and
            $prefilterShader -match "RWTexture2D<float4> g_out_radiance" -and
            $prefilterShader -match "RWTexture2D<float> g_out_variance" -and
            $prefilterShader -match "RWTexture2D<float> g_out_sample_count" -and
            $prefilterShader -match "Buffer<uint> g_denoiser_tile_list" -and
            $prefilterShader -match "numthreads\(8, 8, 1\)") `
        "depth=$($prefilterShader -match 'Texture2D<float> g_depth_buffer'),avg=$($prefilterShader -match 'Texture2D<float3> g_average_radiance'),inRadiance=$($prefilterShader -match 'Texture2D<float4> g_in_radiance'),outputs=$($prefilterShader -match 'RWTexture2D<float4> g_out_radiance')/$($prefilterShader -match 'RWTexture2D<float> g_out_variance'),tiles=$($prefilterShader -match 'Buffer<uint> g_denoiser_tile_list')" `
        "true/true/true/true/true"
    $checks += New-Check `
        "FFX SSSR ResolveTemporal shader contract present" `
        ($resolveTemporalShader -match "Texture2D<float> g_roughness" -and
            $resolveTemporalShader -match "Texture2D<float3> g_average_radiance" -and
            $resolveTemporalShader -match "Texture2D<float4> g_in_radiance" -and
            $resolveTemporalShader -match "Texture2D<float4> g_in_reprojected_radiance" -and
            $resolveTemporalShader -match "Texture2D<float> g_in_variance" -and
            $resolveTemporalShader -match "Texture2D<float> g_in_sample_count" -and
            $resolveTemporalShader -match "RWTexture2D<float4> g_out_radiance" -and
            $resolveTemporalShader -match "RWTexture2D<float> g_out_variance" -and
            $resolveTemporalShader -match "RWTexture2D<float> g_out_sample_count" -and
            $resolveTemporalShader -match "Buffer<uint> g_denoiser_tile_list" -and
            $resolveTemporalShader -match "numthreads\(8, 8, 1\)") `
        "roughness=$($resolveTemporalShader -match 'Texture2D<float> g_roughness'),avg=$($resolveTemporalShader -match 'Texture2D<float3> g_average_radiance'),in=$($resolveTemporalShader -match 'Texture2D<float4> g_in_radiance'),reprojected=$($resolveTemporalShader -match 'Texture2D<float4> g_in_reprojected_radiance'),outputs=$($resolveTemporalShader -match 'RWTexture2D<float4> g_out_radiance')/$($resolveTemporalShader -match 'RWTexture2D<float> g_out_variance')/$($resolveTemporalShader -match 'RWTexture2D<float> g_out_sample_count'),tiles=$($resolveTemporalShader -match 'Buffer<uint> g_denoiser_tile_list')" `
        "true/true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX ClassifyTiles descriptors match typed-buffer contract" `
        ($adapterSource -match "VulkanFfxSssrClassifyTilesDescriptorSetLayout" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_SAMPLER" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE" -and
            $adapterSource -match "VK_FORMAT_R32_SFLOAT") `
        "classifyLayout=$($adapterSource -match 'VulkanFfxSssrClassifyTilesDescriptorSetLayout'),typed=$($adapterSource -match 'VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER'),r32=$($adapterSource -match 'VK_FORMAT_R32_SFLOAT')" `
        "true/true/true"
    $checks += New-Check `
        "SelfEngine FFX blue-noise descriptors match AMD table contract" `
        ($adapterSource -match "VulkanFfxSssrBlueNoiseDescriptorSetLayout" -and
            $adapterSource -match "data/blue_noise_tables_128x128_1spp.inl" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER" -and
            $adapterSource -match "VK_FORMAT_R32G32_SFLOAT" -and
            $adapterHeader -match "kSobolEntryCount = 256u \* 256u" -and
            $adapterHeader -match "kTileEntryCount = kTextureSize \* kTextureSize \* 8u") `
        "layout=$($adapterSource -match 'VulkanFfxSssrBlueNoiseDescriptorSetLayout'),tables=$($adapterSource -match 'data/blue_noise_tables_128x128_1spp.inl'),typed=$($adapterSource -match 'VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER'),format=$($adapterSource -match 'VK_FORMAT_R32G32_SFLOAT')" `
        "true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX Intersect descriptors match official shader contract" `
        ($adapterSource -match "VulkanFfxSssrIntersectDescriptorSetLayout" -and
            $adapterSource -match "VulkanFfxSssrIntersectResources" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER" -and
            $adapterSource -match "IntersectionOutputView" -and
            $adapterSource -match "ExtractedRoughnessView" -and
            $rendererSource -match "ffx_sssr_Intersect.hlsl.spv" -and
            $rendererSource -match "ffx_sssr_PrepareBlueNoiseTexture.hlsl.spv") `
        "layout=$($adapterSource -match 'VulkanFfxSssrIntersectDescriptorSetLayout'),resources=$($adapterSource -match 'VulkanFfxSssrIntersectResources'),rayListTyped=$($adapterSource -match 'VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER'),intersectSpv=$($rendererSource -match 'ffx_sssr_Intersect.hlsl.spv')" `
        "true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX Reproject descriptors match official shader contract" `
        ($adapterSource -match "VulkanFfxSssrReprojectDescriptorSetLayout" -and
            $adapterSource -match "VulkanFfxSssrReprojectResources" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE" -and
            $adapterSource -match "VK_FORMAT_R32G32B32A32_SFLOAT" -and
            $adapterSource -match "DenoiserTileListBufferView" -and
            $rendererSource -match "ffx_sssr_Reproject.hlsl.spv" -and
            $commandBufferSource -match "sizeof\(u32\) \* 3u") `
        "layout=$($adapterSource -match 'VulkanFfxSssrReprojectDescriptorSetLayout'),resources=$($adapterSource -match 'VulkanFfxSssrReprojectResources'),rgba32=$($adapterSource -match 'VK_FORMAT_R32G32B32A32_SFLOAT'),typedTiles=$($adapterSource -match 'DenoiserTileListBufferView'),reprojectSpv=$($rendererSource -match 'ffx_sssr_Reproject.hlsl.spv'),offset=$($commandBufferSource -match 'sizeof\(u32\) \* 3u')" `
        "true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX Prefilter descriptors match official shader contract" `
        ($adapterSource -match "VulkanFfxSssrPrefilterDescriptorSetLayout" -and
            $adapterSource -match "VulkanFfxSssrPrefilterResources" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER" -and
            $adapterSource -match "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE" -and
            $adapterSource -match "VK_FORMAT_R32G32B32A32_SFLOAT" -and
            $adapterSource -match "reprojectResources\.AverageRadianceView" -and
            $adapterSource -match "classifyResources\.IntersectionOutputView" -and
            $adapterSource -match "reprojectResources\.VarianceView" -and
            $adapterSource -match "reprojectResources\.SampleCountView" -and
            $rendererSource -match "ffx_sssr_Prefilter.hlsl.spv" -and
            $commandBufferSource -match "ffxSssrPrefilterDispatches" -and
            $commandBufferSource -match "sizeof\(u32\) \* 3u") `
        "layout=$($adapterSource -match 'VulkanFfxSssrPrefilterDescriptorSetLayout'),resources=$($adapterSource -match 'VulkanFfxSssrPrefilterResources'),avgInput=$($adapterSource -match 'reprojectResources\.AverageRadianceView'),currentRadiance=$($adapterSource -match 'classifyResources\.IntersectionOutputView'),prefilterSpv=$($rendererSource -match 'ffx_sssr_Prefilter.hlsl.spv'),dispatch=$($commandBufferSource -match 'ffxSssrPrefilterDispatches')" `
        "true/true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX ResolveTemporal descriptors write audited history" `
        ($adapterSource -match "VulkanFfxSssrResolveTemporalDescriptorSetLayout" -and
            $adapterSource -match "VulkanFfxSssrResolveTemporalResources" -and
            $adapterSource -match "reprojectResources\.AverageRadianceView" -and
            $adapterSource -match "prefilterResources\.RadianceView" -and
            $adapterSource -match "reprojectResources\.ReprojectedRadianceView" -and
            $adapterSource -match "prefilterResources\.VarianceView" -and
            $adapterSource -match "prefilterResources\.SampleCountView" -and
            $adapterSource -match "reprojectResources\.RadianceHistoryView" -and
            $adapterSource -match "reprojectResources\.VarianceHistoryView" -and
            $adapterSource -match "reprojectResources\.SampleCountHistoryView" -and
            $rendererSource -match "ffx_sssr_ResolveTemporal.hlsl.spv" -and
            $commandBufferSource -match "CopyFfxSssrCurrentDenoiserStateToHistory" -and
            $commandBufferSource -match "CopyFfxSssrHistoryToOtherImages" -and
            $commandBufferSource -match "ffxSssrResolveTemporalDispatches" -and
            $commandBufferSource -match "sizeof\(u32\) \* 3u") `
        "layout=$($adapterSource -match 'VulkanFfxSssrResolveTemporalDescriptorSetLayout'),resources=$($adapterSource -match 'VulkanFfxSssrResolveTemporalResources'),prefilterInput=$($adapterSource -match 'prefilterResources\.RadianceView'),historyOut=$($adapterSource -match 'reprojectResources\.RadianceHistoryView'),resolveSpv=$($rendererSource -match 'ffx_sssr_ResolveTemporal.hlsl.spv'),dispatch=$($commandBufferSource -match 'ffxSssrResolveTemporalDispatches'),copies=$($commandBufferSource -match 'CopyFfxSssrHistoryToOtherImages')" `
        "true/true/true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX Reproject consumes packed previous receiver metadata" `
        ($adapterSource -match "renderTargets\.SsrHistoryMetadataView\(imageIndex\)" -and
            $adapterSource -notmatch "sampledImages\[3\] = sampledImages\[0\]" -and
            $reprojectShader -match "g_receiver_history_metadata" -and
            $reprojectShader -match "SelfEngine_DecodeHistoryNormal" -and
            $commonShader -match "depth > 1\.0f" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_reproject_motion_vector_mode" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_reproject_hit_reprojection_enabled" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_reproject_reprojection_contract_ready" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_reproject_history_metadata_source") `
        "metadataView=$($adapterSource -match 'renderTargets\.SsrHistoryMetadataView\(imageIndex\)'),currentAliasGone=$($adapterSource -notmatch 'sampledImages\[3\] = sampledImages\[0\]'),shaderMetadata=$($reprojectShader -match 'g_receiver_history_metadata'),normalDecode=$($reprojectShader -match 'SelfEngine_DecodeHistoryNormal'),linearDepth=$($commonShader -match 'depth > 1\.0f'),mvCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_reproject_motion_vector_mode'),hitCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_reproject_hit_reprojection_enabled'),readyCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_reproject_reprojection_contract_ready'),csv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_reproject_history_metadata_source')" `
        "true/true/true/true/true/true/true/true/true"
    $ffxPrefilterSampleCountStores =
        $prefilterShader -match "g_out_sample_count\[pixel_coordinate\]"
    $ffxResolveSampleConfidence =
        $resolveTemporalShader -match "sample_confidence"
    $ffxResolveVarianceConfidence =
        $resolveTemporalShader -match "variance_confidence"
    $ffxResolveSampleCountStores =
        $resolveTemporalShader -match "g_out_sample_count\[pixel_coordinate\]"
    $ffxDeferredAlphaConfidence =
        $deferredLightingShader -match "ffxCompositeConfidence = clamp\(resolvedSsr\.a"
    $ffxDeferredReceiverValidation =
        $deferredLightingShader -match "SsrDeferredReceiverHistoryConfidence\(" -and
        $deferredLightingShader -match "SsrDeferredReceiverValidatedHistory\(" -and
        $deferredLightingShader -match "texelFetch\(\s*ssrHistoryMetadata" -and
        $deferredLightingShader -match "texelFetch\(\s*ssrResolvedReflection" -and
        $deferredLightingShader -notmatch "ffxRadianceHistorySource\s*\?\s*\(historyUvValid\s*\?\s*1\.0\s*:\s*0\.0\)"
    $ffxFrameGraphMetadataInput =
        $ssrFeatureSource -match "FFX RadianceHistory, Velocity, SSRHistoryMetadata"
    $ffxCompositeConfidenceCsv =
        $benchmarkRecorderSource -match "ssr_ffx_sssr_deferred_composite_confidence_source"
    $checks += New-Check `
        "SelfEngine FFX composite uses sample-count variance confidence and receiver validation" `
        ($ffxPrefilterSampleCountStores -and
            $ffxResolveSampleConfidence -and
            $ffxResolveVarianceConfidence -and
            $ffxResolveSampleCountStores -and
            $ffxDeferredAlphaConfidence -and
            $ffxDeferredReceiverValidation -and
            $ffxFrameGraphMetadataInput -and
            $ffxCompositeConfidenceCsv) `
        "prefilterSample=$ffxPrefilterSampleCountStores,sampleConfidence=$ffxResolveSampleConfidence,varianceConfidence=$ffxResolveVarianceConfidence,resolveSample=$ffxResolveSampleCountStores,deferredAlpha=$ffxDeferredAlphaConfidence,receiverValidation=$ffxDeferredReceiverValidation,frameGraphMetadata=$ffxFrameGraphMetadataInput,csv=$ffxCompositeConfidenceCsv" `
        "true/true/true/true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX Deferred composite consumes ResolveTemporal history" `
        ($descriptorSetsSource -match "resolvedReflectionOverride" -and
            $rendererSource -match "RadianceHistoryView" -and
            $rendererSource -match "fidelityFxSssrDeferredCompositeActive" -and
            $rendererSource -match "m_FfxSssrRadianceHistoryValid" -and
            $deferredLightingShader -match "SsrFidelityFxRadianceHistorySourceEnabled" -and
            $deferredLightingShader -match "131072\.0" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_deferred_composite_active" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_deferred_composite_quality_gate") `
        "descriptorOverride=$($descriptorSetsSource -match 'resolvedReflectionOverride'),radianceHistory=$($rendererSource -match 'RadianceHistoryView'),active=$($rendererSource -match 'fidelityFxSssrDeferredCompositeActive'),historyValid=$($rendererSource -match 'm_FfxSssrRadianceHistoryValid'),shaderBit=$($deferredLightingShader -match 'SsrFidelityFxRadianceHistorySourceEnabled'),activeCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_deferred_composite_active'),qualityCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_deferred_composite_quality_gate')" `
        "true/true/true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX temporal stability is an audited runtime control" `
        ($rendererSource -match "SE_SSR_FFX_TEMPORAL_STABILITY_FACTOR" -and
            $rendererSource -match "FfxSssrTemporalStabilityFactorFromEnvironment" -and
            $rendererSource -match "constants\.temporalStabilityFactor" -and
            $renderFeatureSource -match "ffxSssrTemporalStabilityFactor" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_temporal_stability_factor" -and
            $commonShader -match "g_temporal_stability_factor") `
        "env=$($rendererSource -match 'SE_SSR_FFX_TEMPORAL_STABILITY_FACTOR'),source=$($rendererSource -match 'FfxSssrTemporalStabilityFactorFromEnvironment'),constant=$($rendererSource -match 'constants\.temporalStabilityFactor'),context=$($renderFeatureSource -match 'ffxSssrTemporalStabilityFactor'),csv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_temporal_stability_factor'),shader=$($commonShader -match 'g_temporal_stability_factor')" `
        "true/true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX samples-per-quad quality control is auditable" `
        ($rendererSource -match "SE_SSR_FFX_SAMPLES_PER_QUAD" -and
            $rendererSource -match "FfxSssrSamplesPerQuadFromEnvironment" -and
            $rendererSource -match "constants\.samplesPerQuad" -and
            $renderFeatureSource -match "ffxSssrSamplesPerQuad" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_samples_per_quad" -and
            $commonShader -match "g_samples_per_quad") `
        "env=$($rendererSource -match 'SE_SSR_FFX_SAMPLES_PER_QUAD'),source=$($rendererSource -match 'FfxSssrSamplesPerQuadFromEnvironment'),constant=$($rendererSource -match 'constants\.samplesPerQuad'),context=$($renderFeatureSource -match 'ffxSssrSamplesPerQuad'),csv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_samples_per_quad'),shader=$($commonShader -match 'g_samples_per_quad')" `
        "true/true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX environment fallback is stable, roughness-prefiltered, and LOD0-isolatable" `
        ($rendererSource -match "SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK" -and
            $rendererSource -match "SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK_OFF" -and
            $rendererSource -match "SE_SSR_FFX_ENVIRONMENT_LOD0" -and
            $rendererSource -match "SE_SSR_FFX_CONSTANT_ENVIRONMENT" -and
            $rendererSource -match "SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS" -and
            $rendererSource -match "SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS_OFF" -and
            $rendererSource -match "m_IblPrefilteredImage->MipLevels\(\)" -and
            $rendererSource -match "ffxSssrEnvironmentLod0Enabled && ffxSssrIblEnvironmentMipCount > 0u" -and
            $adapterHeader -match "environmentFallbackControl" -and
            $commonShader -match "g_environment_fallback_control" -and
            $intersectShader -match "StableEnvironmentFallbackEnabled" -and
            $intersectShader -match "ConstantEnvironmentFallbackEnabled" -and
            $intersectShader -match "PerfectReflectionDirectionsEnabled" -and
            $intersectShader -match "reflect\(view_space_ray_direction, view_space_surface_normal\)" -and
            $intersectShader -match "float3\(0\.6, 0\.6, 0\.6\)" -and
            $intersectShader -match "EnvironmentMipCount" -and
            $intersectShader -match "view_space_environment_direction" -and
            $intersectShader -match "saturate\(roughness\) \* max_mip" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_stable_environment_fallback_enabled" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_constant_environment_fallback_enabled" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_perfect_reflection_directions_enabled" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_environment_mip_count") `
        "stable=$($rendererSource -match 'SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK'),stableOff=$($rendererSource -match 'SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK_OFF'),lod0=$($rendererSource -match 'SE_SSR_FFX_ENVIRONMENT_LOD0'),constantEnv=$($rendererSource -match 'SE_SSR_FFX_CONSTANT_ENVIRONMENT'),perfectDirections=$($rendererSource -match 'SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS'),perfectOff=$($rendererSource -match 'SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS_OFF'),mips=$($rendererSource -match 'm_IblPrefilteredImage->MipLevels\(\)'),resolve=$($rendererSource -match 'ffxSssrEnvironmentLod0Enabled && ffxSssrIblEnvironmentMipCount > 0u'),constant=$($adapterHeader -match 'environmentFallbackControl'),cbuffer=$($commonShader -match 'g_environment_fallback_control'),stableMode=$($intersectShader -match 'StableEnvironmentFallbackEnabled'),constantMode=$($intersectShader -match 'ConstantEnvironmentFallbackEnabled'),perfectMode=$($intersectShader -match 'PerfectReflectionDirectionsEnabled'),perfectReflect=$($intersectShader -match 'reflect\(view_space_ray_direction, view_space_surface_normal\)'),constantColor=$($intersectShader -match 'float3\(0\.6, 0\.6, 0\.6\)'),mipFn=$($intersectShader -match 'EnvironmentMipCount'),stableDir=$($intersectShader -match 'view_space_environment_direction'),roughnessLod=$($intersectShader -match 'saturate\(roughness\) \* max_mip'),stableCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_stable_environment_fallback_enabled'),constantCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_constant_environment_fallback_enabled'),perfectCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_perfect_reflection_directions_enabled'),mipCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_environment_mip_count')" `
        "true/true/true/true/true/true/true/true/true/true/true/true/true/true/true/true/true/true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX visible output clear is a default-on audited control" `
        ($rendererSource -match "SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT" -and
            $rendererSource -match "SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT_OFF" -and
            $rendererSource -match "ssrFidelityFxBackendRequested" -and
            $commandBufferSource -match "ClearFfxSssrVisibleOutput" -and
            $commandBufferSource -match "vkCmdClearColorImage" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_visible_output_clear_enabled" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_visible_output_clears") `
        "on=$($rendererSource -match 'SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT'),off=$($rendererSource -match 'SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT_OFF'),backend=$($rendererSource -match 'ssrFidelityFxBackendRequested'),clear=$($commandBufferSource -match 'ClearFfxSssrVisibleOutput'),gpu=$($commandBufferSource -match 'vkCmdClearColorImage'),enabledCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_visible_output_clear_enabled'),countCsv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_visible_output_clears')" `
        "true/true/true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX spatial prefilter has an audited bypass" `
        ($rendererSource -match "SE_SSR_FFX_PREFILTER_BYPASS" -and
            $prefilterShader -match "SelfEngine_FfxSssrPrefilterBypassEnabled" -and
            $prefilterShader -match "g_out_radiance\[remapped_dispatch_thread_id\] = input_radiance" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_prefilter_bypass_enabled") `
        "env=$($rendererSource -match 'SE_SSR_FFX_PREFILTER_BYPASS'),branch=$($prefilterShader -match 'SelfEngine_FfxSssrPrefilterBypassEnabled'),copy=$($prefilterShader -match 'g_out_radiance\[remapped_dispatch_thread_id\] = input_radiance'),csv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_prefilter_bypass_enabled')" `
        "true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX temporal resolve has an audited bypass" `
        ($rendererSource -match "SE_SSR_FFX_RESOLVE_TEMPORAL_BYPASS" -and
            $resolveTemporalShader -match "SelfEngine_FfxSssrResolveTemporalBypassEnabled" -and
            $resolveTemporalShader -match "g_out_radiance\[remapped_dispatch_thread_id\] = float4" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_resolve_temporal_bypass_enabled") `
        "env=$($rendererSource -match 'SE_SSR_FFX_RESOLVE_TEMPORAL_BYPASS'),branch=$($resolveTemporalShader -match 'SelfEngine_FfxSssrResolveTemporalBypassEnabled'),copy=$($resolveTemporalShader -match 'g_out_radiance\[remapped_dispatch_thread_id\] = float4'),csv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_resolve_temporal_bypass_enabled')" `
        "true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX Classify surface coverage seed is audited" `
        ($rendererSource -match "SE_SSR_FFX_CLASSIFY_SURFACE_SEED" -and
            $classifyShader -match "SelfEngine_FfxSssrClassifySurfaceSeedEnabled" -and
            $classifyShader -match "float4\(0\.6, 0\.6, 0\.6, 1\.0\)" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_classify_surface_seed_enabled") `
        "env=$($rendererSource -match 'SE_SSR_FFX_CLASSIFY_SURFACE_SEED'),branch=$($classifyShader -match 'SelfEngine_FfxSssrClassifySurfaceSeedEnabled'),seed=$($classifyShader -match 'float4\(0\.6, 0\.6, 0\.6, 1\.0\)'),csv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_classify_surface_seed_enabled')" `
        "true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX Intersect ray coverage marker is audited" `
        ($rendererSource -match "SE_SSR_FFX_INTERSECT_COVERAGE_MARKER" -and
            $intersectShader -match "SelfEngine_FfxSssrIntersectCoverageMarkerEnabled" -and
            $intersectShader -match "float4\(1\.0, 0\.0, 1\.0, 1\.0\)" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_intersect_coverage_marker_enabled") `
        "env=$($rendererSource -match 'SE_SSR_FFX_INTERSECT_COVERAGE_MARKER'),branch=$($intersectShader -match 'SelfEngine_FfxSssrIntersectCoverageMarkerEnabled'),marker=$($intersectShader -match 'float4\(1\.0, 0\.0, 1\.0, 1\.0\)'),csv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_intersect_coverage_marker_enabled')" `
        "true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX vendor confidence default and experimental mode are auditable" `
        ($rendererSource -match "SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE" -and
            $rendererSource -match "SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF" -and
            $commonShader -match "g_composite_confidence_mode" -and
            $resolveTemporalShader -match "glossy_validity" -and
            $resolveTemporalShader -match "g_composite_confidence_mode == 0u" -and
            $benchmarkRecorderSource -match "ssr_ffx_sssr_composite_confidence_mode") `
        "on=$($rendererSource -match 'SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE'),off=$($rendererSource -match 'SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF'),constant=$($commonShader -match 'g_composite_confidence_mode'),glossy=$($resolveTemporalShader -match 'glossy_validity'),branch=$($resolveTemporalShader -match 'g_composite_confidence_mode == 0u'),csv=$($benchmarkRecorderSource -match 'ssr_ffx_sssr_composite_confidence_mode')" `
        "true/true/true/true/true/true"
    $contractVersionEleven =
        $ssrFeatureSource -match 'fidelityFxSssrContractVersion\s*=\s*11u'
    $adapterDefaultsLocked =
        $adapterHeader -match 'samplesPerQuad\s*=\s*4u' -and
        $adapterHeader -match 'compositeConfidenceMode\s*=\s*0u'
    $fourRaysDefault = $rendererSource -match
        '(?s)u32\s+FfxSssrSamplesPerQuadFromEnvironment\(\).*?return\s+4u;\s*\}'
    $stableEnvironmentDefault = $rendererSource -match
        '(?s)bool\s+FfxSssrStableEnvironmentFallbackEnabledFromEnvironment\(\).*?return\s+!EnvironmentFlagEnabled\(\s*"SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK_OFF"\s*\);'
    $perfectDirectionsDefault = $rendererSource -match
        '(?s)bool\s+FfxSssrPerfectReflectionDirectionsEnabledFromEnvironment\(\).*?return\s+!EnvironmentFlagEnabled\(\s*"SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS_OFF"\s*\);'
    $vendorConfidenceDefault = $rendererSource -match
        '(?s)u32\s+FfxSssrCompositeConfidenceModeFromEnvironment\(\).*?"SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE"\s*\)\s*\?\s*1u\s*:\s*0u;'
    $visibleOutputClearDefault = $rendererSource -match
        '(?s)ffxSssrVisibleOutputClearRequested\s*=\s*EnvironmentFlagEnabled\("SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT"\)\s*\|\|\s*!EnvironmentFlagEnabled\("SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT_OFF"\);'
    $checks += New-Check `
        "SelfEngine FFX production visual defaults are locked by contract v11" `
        ($contractVersionEleven -and
            $adapterDefaultsLocked -and
            $fourRaysDefault -and
            $stableEnvironmentDefault -and
            $perfectDirectionsDefault -and
            $vendorConfidenceDefault -and
            $visibleOutputClearDefault) `
        "contract11=$contractVersionEleven,adapter=$adapterDefaultsLocked,rays4=$fourRaysDefault,stable=$stableEnvironmentDefault,perfect=$perfectDirectionsDefault,confidence0=$vendorConfidenceDefault,clear=$visibleOutputClearDefault" `
        "true/true/true/true/true/true/true"
    $checks += New-Check `
        "SelfEngine FFX pipelines use AMD constants set zero" `
        ($adapterSource -match "VulkanFfxSssrConstantsDescriptorSetLayout" -and
            $rendererSource -match "m_FfxSssrConstantsDescriptorSetLayout->Handle\(\)" -and
            $rendererSource -match "ffx_sssr_ClassifyTiles.hlsl.spv") `
        "constantsLayout=$($adapterSource -match 'VulkanFfxSssrConstantsDescriptorSetLayout'),pipelineLayout=$($rendererSource -match 'm_FfxSssrConstantsDescriptorSetLayout->Handle\(\)'),classifySpv=$($rendererSource -match 'ffx_sssr_ClassifyTiles.hlsl.spv')" `
        "true/true/true"

    $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
    $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
    return [pscustomobject]@{
        lane = "ffx-sssr-static-contract"
        verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
        passCount = $passCount
        failCount = $failCount
        checks = $checks
    }
}

function Invoke-RuntimeLane {
    param(
        [string]$Name,
        [string]$Executable,
        [hashtable]$Environment,
        [uint32]$ExpectedRequestedProvider,
        [uint32]$ExpectedActiveProvider,
        [uint32]$ExpectedDispatchReady,
        [uint32]$ExpectedRuntimeActive,
        [bool]$ExpectPrepareDispatch,
        [uint32]$ExpectedFallbackReason,
        [bool]$ExpectDeferredComposite = $false,
        [uint32]$ExpectedMotionVectorMode = 1,
        [double]$ExpectedMotionVectorScaleX = 1.0,
        [double]$ExpectedMotionVectorScaleY = 1.0,
        [uint32]$ExpectedHitReprojectionEnabled = 1,
        [double]$ExpectedTemporalStabilityFactor = 0.95,
        [uint32]$ExpectedSamplesPerQuad = 4,
        [uint32]$ExpectedStableEnvironmentFallbackEnabled = 1,
        [uint32]$ExpectedConstantEnvironmentFallbackEnabled = 0,
        [uint32]$ExpectedPerfectReflectionDirectionsEnabled = 1,
        [uint32]$ExpectedPrefilterBypassEnabled = 0,
        [uint32]$ExpectedResolveTemporalBypassEnabled = 0,
        [uint32]$ExpectedClassifySurfaceSeedEnabled = 0,
        [uint32]$ExpectedIntersectCoverageMarkerEnabled = 0,
        [uint32]$ExpectedEnvironmentMipCount = 0,
        [uint32]$ExpectedDeferredReceiverReprojectionEnabled = 1,
        [uint32]$ExpectedVisibleOutputClearEnabled = 1,
        [uint32]$ExpectedVisibleOutputClears = 1,
        [uint32]$ExpectedCompositeConfidenceMode = 0
    )

    $laneDirectory = Join-Path $OutputDirectory $Name
    New-Item -ItemType Directory -Force -Path $laneDirectory | Out-Null
    $csvPath = Join-Path $laneDirectory "ffx_sssr_backend.csv"
    $stdoutPath = Join-Path $laneDirectory "stdout.log"
    $stderrPath = Join-Path $laneDirectory "stderr.log"
    Remove-Item -LiteralPath $csvPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stdoutPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue

    $Environment["SE_BENCHMARK_CSV"] = $csvPath
    $previous = Set-ProcessEnvironment -Values $Environment
    try {
        $exitCode = 0
        $startProcessBlocked = $false
        try {
            $process = Start-Process `
                -FilePath $Executable `
                -PassThru `
                -Wait `
                -WindowStyle Hidden `
                -RedirectStandardOutput $stdoutPath `
                -RedirectStandardError $stderrPath
            $exitCode = $process.ExitCode
            if ($exitCode -eq 4551) {
                $startProcessBlocked = $true
            }
        } catch [System.InvalidOperationException] {
            $startProcessBlocked = $true
        }
        if ($startProcessBlocked) {
            Remove-Item -LiteralPath $stdoutPath -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
            & cmd.exe /d /c "`"$Executable`" > `"$stdoutPath`" 2> `"$stderrPath`""
            $exitCode = $LASTEXITCODE
        }
        if ($exitCode -ne 0) {
            throw "$Name exited with code $exitCode"
        }
    } finally {
        Restore-ProcessEnvironment -Values $previous
    }

    $rows = @()
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $csvPath) {
            $rows = @(Import-Csv -LiteralPath $csvPath)
            if ($rows.Count -gt 0) {
                break
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if ($rows.Count -eq 0) {
        throw "$Name did not emit benchmark CSV: $csvPath"
    }

    $last = $rows[-1]
    $requestedProvider = Get-UIntMetric $last "ssr_backend_requested_provider"
    $activeProvider = Get-UIntMetric $last "ssr_backend_active_provider"
    $contractVersion = Get-UIntMetric $last "ssr_ffx_sssr_contract_version"
    $sourceReady = Get-UIntMetric $last "ssr_ffx_sssr_source_ready"
    $shaderBuild = Get-UIntMetric $last "ssr_ffx_sssr_shader_build_integrated"
    $shaderCount = Get-UIntMetric $last "ssr_ffx_sssr_shader_count"
    $denoiserReady = Get-UIntMetric $last "ssr_ffx_sssr_denoiser_dependency_ready"
    $spdReady = Get-UIntMetric $last "ssr_ffx_sssr_spd_dependency_ready"
    $constantsResourcesReady = Get-UIntMetric $last "ssr_ffx_sssr_constants_resources_ready"
    $constantsDescriptorSetsReady = Get-UIntMetric $last "ssr_ffx_sssr_constants_descriptor_sets_ready"
    $temporalStabilityFactor = Get-FloatMetric $last "ssr_ffx_sssr_temporal_stability_factor"
    $samplesPerQuad = Get-UIntMetric $last "ssr_ffx_sssr_samples_per_quad"
    $stableEnvironmentFallbackEnabled = Get-UIntMetric $last "ssr_ffx_sssr_stable_environment_fallback_enabled"
    $constantEnvironmentFallbackEnabled = Get-UIntMetric $last "ssr_ffx_sssr_constant_environment_fallback_enabled"
    $perfectReflectionDirectionsEnabled = Get-UIntMetric $last "ssr_ffx_sssr_perfect_reflection_directions_enabled"
    $prefilterBypassEnabled = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_bypass_enabled"
    $resolveTemporalBypassEnabled = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_bypass_enabled"
    $classifySurfaceSeedEnabled = Get-UIntMetric $last "ssr_ffx_sssr_classify_surface_seed_enabled"
    $intersectCoverageMarkerEnabled = Get-UIntMetric $last "ssr_ffx_sssr_intersect_coverage_marker_enabled"
    $environmentMipCount = Get-UIntMetric $last "ssr_ffx_sssr_environment_mip_count"
    $iblPrefilteredMipCount = Get-UIntMetric $last "ibl_prefiltered_mip_count"
    $deferredReceiverReprojectionEnabled = Get-UIntMetric $last "ssr_reconstruction_deferred_receiver_reprojection_enabled"
    $deferredValidatedBilinearEnabled = Get-UIntMetric $last "ssr_reconstruction_deferred_validated_bilinear_enabled"
    $deferredHistoryTapCount = Get-UIntMetric $last "ssr_reconstruction_deferred_history_tap_count"
    $prepareResourcesReady = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_resources_ready"
    $prepareDescriptorSetsReady = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_descriptor_sets_ready"
    $preparePipelineReady = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_pipeline_ready"
    $prepareDispatches = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_dispatches"
    $prepareDescriptorBinds = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_descriptor_binds"
    $prepareBindDispatches = Get-UIntMetric $last "ffx_sssr_prepare_indirect_args_dispatches"
    $prepareBindDescriptorBinds = Get-UIntMetric $last "ffx_sssr_prepare_indirect_args_descriptor_binds"
    $prepareBufferBytes = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_buffer_bytes"
    $classifyResourcesReady = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_resources_ready"
    $classifyDescriptorSetsReady = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_descriptor_sets_ready"
    $classifyPipelineReady = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_pipeline_ready"
    $classifyInputContractReady = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_input_contract_ready"
    $classifyDispatches = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_dispatches"
    $classifyDescriptorBinds = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_descriptor_binds"
    $classifyWidth = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_width"
    $classifyHeight = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_height"
    $classifyGroupCountX = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_group_count_x"
    $classifyGroupCountY = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_group_count_y"
    $classifyRayListCapacity = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_ray_list_capacity"
    $classifyDenoiserTileListCapacity = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_denoiser_tile_list_capacity"
    $classifyMemoryBytes = Get-UIntMetric $last "ssr_ffx_sssr_classify_tiles_memory_bytes"
    $classifyReadbackValid = Get-UIntMetric $last "ssr_ffx_sssr_ray_counter_readback_valid"
    $classifyRayCount = Get-UIntMetric $last "ssr_ffx_sssr_classified_ray_count"
    $classifyDenoiserTileCount = Get-UIntMetric $last "ssr_ffx_sssr_classified_denoiser_tile_count"
    $classifyBindDispatches = Get-UIntMetric $last "ffx_sssr_classify_tiles_dispatches"
    $classifyBindDescriptorBinds = Get-UIntMetric $last "ffx_sssr_classify_tiles_descriptor_binds"
    $classifyBindGroupCountX = Get-UIntMetric $last "ffx_sssr_classify_tiles_groups_x"
    $classifyBindGroupCountY = Get-UIntMetric $last "ffx_sssr_classify_tiles_groups_y"
    $blueNoiseResourcesReady = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_resources_ready"
    $blueNoiseDescriptorSetsReady = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_descriptor_sets_ready"
    $blueNoisePipelineReady = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_pipeline_ready"
    $blueNoiseDispatches = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_dispatches"
    $blueNoiseDescriptorBinds = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_descriptor_binds"
    $blueNoiseWidth = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_width"
    $blueNoiseHeight = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_height"
    $blueNoiseGroupCountX = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_group_count_x"
    $blueNoiseGroupCountY = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_group_count_y"
    $blueNoiseSobolEntryCount = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_sobol_entry_count"
    $blueNoiseRankingTileEntryCount = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_ranking_tile_entry_count"
    $blueNoiseScramblingTileEntryCount = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_scrambling_tile_entry_count"
    $blueNoiseMemoryBytes = Get-UIntMetric $last "ssr_ffx_sssr_blue_noise_memory_bytes"
    $blueNoiseBindDispatches = Get-UIntMetric $last "ffx_sssr_blue_noise_dispatches"
    $blueNoiseBindDescriptorBinds = Get-UIntMetric $last "ffx_sssr_blue_noise_descriptor_binds"
    $blueNoiseBindGroupCountX = Get-UIntMetric $last "ffx_sssr_blue_noise_groups_x"
    $blueNoiseBindGroupCountY = Get-UIntMetric $last "ffx_sssr_blue_noise_groups_y"
    $intersectResourcesReady = Get-UIntMetric $last "ssr_ffx_sssr_intersect_resources_ready"
    $intersectDescriptorSetsReady = Get-UIntMetric $last "ssr_ffx_sssr_intersect_descriptor_sets_ready"
    $intersectPipelineReady = Get-UIntMetric $last "ssr_ffx_sssr_intersect_pipeline_ready"
    $intersectInputContractReady = Get-UIntMetric $last "ssr_ffx_sssr_intersect_input_contract_ready"
    $intersectDispatches = Get-UIntMetric $last "ssr_ffx_sssr_intersect_dispatches"
    $intersectDescriptorBinds = Get-UIntMetric $last "ssr_ffx_sssr_intersect_descriptor_binds"
    $intersectWidth = Get-UIntMetric $last "ssr_ffx_sssr_intersect_width"
    $intersectHeight = Get-UIntMetric $last "ssr_ffx_sssr_intersect_height"
    $intersectDepthPyramidMipCount = Get-UIntMetric $last "ssr_ffx_sssr_intersect_depth_pyramid_mip_count"
    $intersectBindDispatches = Get-UIntMetric $last "ffx_sssr_intersect_dispatches"
    $intersectBindDescriptorBinds = Get-UIntMetric $last "ffx_sssr_intersect_descriptor_binds"
    $reprojectResourcesReady = Get-UIntMetric $last "ssr_ffx_sssr_reproject_resources_ready"
    $reprojectDescriptorSetsReady = Get-UIntMetric $last "ssr_ffx_sssr_reproject_descriptor_sets_ready"
    $reprojectPipelineReady = Get-UIntMetric $last "ssr_ffx_sssr_reproject_pipeline_ready"
    $reprojectInputContractReady = Get-UIntMetric $last "ssr_ffx_sssr_reproject_input_contract_ready"
    $reprojectDispatches = Get-UIntMetric $last "ssr_ffx_sssr_reproject_dispatches"
    $reprojectDescriptorBinds = Get-UIntMetric $last "ssr_ffx_sssr_reproject_descriptor_binds"
    $reprojectWidth = Get-UIntMetric $last "ssr_ffx_sssr_reproject_width"
    $reprojectHeight = Get-UIntMetric $last "ssr_ffx_sssr_reproject_height"
    $reprojectAverageWidth = Get-UIntMetric $last "ssr_ffx_sssr_reproject_average_width"
    $reprojectAverageHeight = Get-UIntMetric $last "ssr_ffx_sssr_reproject_average_height"
    $reprojectHistoryReady = Get-UIntMetric $last "ssr_ffx_sssr_reproject_history_ready"
    $reprojectHistorySource = Get-UIntMetric $last "ssr_ffx_sssr_reproject_history_source"
    $reprojectHistoryMetadataSource = Get-UIntMetric $last "ssr_ffx_sssr_reproject_history_metadata_source"
    $reprojectMemoryBytes = Get-UIntMetric $last "ssr_ffx_sssr_reproject_memory_bytes"
    $reprojectIndirectArgsOffsetBytes = Get-UIntMetric $last "ssr_ffx_sssr_reproject_indirect_args_offset_bytes"
    $reprojectMotionVectorMode = Get-UIntMetric $last "ssr_ffx_sssr_reproject_motion_vector_mode"
    $reprojectMotionVectorScaleX = Get-FloatMetric $last "ssr_ffx_sssr_reproject_motion_vector_scale_x"
    $reprojectMotionVectorScaleY = Get-FloatMetric $last "ssr_ffx_sssr_reproject_motion_vector_scale_y"
    $reprojectMotionVectorContractReady = Get-UIntMetric $last "ssr_ffx_sssr_reproject_motion_vector_contract_ready"
    $reprojectHitReprojectionEnabled = Get-UIntMetric $last "ssr_ffx_sssr_reproject_hit_reprojection_enabled"
    $reprojectReprojectionContractReady = Get-UIntMetric $last "ssr_ffx_sssr_reproject_reprojection_contract_ready"
    $reprojectBindDispatches = Get-UIntMetric $last "ffx_sssr_reproject_dispatches"
    $reprojectBindDescriptorBinds = Get-UIntMetric $last "ffx_sssr_reproject_descriptor_binds"
    $prefilterResourcesReady = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_resources_ready"
    $prefilterDescriptorSetsReady = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_descriptor_sets_ready"
    $prefilterPipelineReady = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_pipeline_ready"
    $prefilterInputContractReady = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_input_contract_ready"
    $prefilterDispatches = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_dispatches"
    $prefilterDescriptorBinds = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_descriptor_binds"
    $prefilterWidth = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_width"
    $prefilterHeight = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_height"
    $prefilterMemoryBytes = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_memory_bytes"
    $prefilterIndirectArgsOffsetBytes = Get-UIntMetric $last "ssr_ffx_sssr_prefilter_indirect_args_offset_bytes"
    $prefilterBindDispatches = Get-UIntMetric $last "ffx_sssr_prefilter_dispatches"
    $prefilterBindDescriptorBinds = Get-UIntMetric $last "ffx_sssr_prefilter_descriptor_binds"
    $resolveTemporalResourcesReady = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_resources_ready"
    $resolveTemporalDescriptorSetsReady = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_descriptor_sets_ready"
    $resolveTemporalPipelineReady = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_pipeline_ready"
    $resolveTemporalInputContractReady = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_input_contract_ready"
    $resolveTemporalHistoryWritebackReady = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_history_writeback_ready"
    $resolveTemporalDispatches = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_dispatches"
    $resolveTemporalDescriptorBinds = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_descriptor_binds"
    $resolveTemporalWidth = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_width"
    $resolveTemporalHeight = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_height"
    $resolveTemporalMemoryBytes = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_memory_bytes"
    $resolveTemporalIndirectArgsOffsetBytes = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_indirect_args_offset_bytes"
    $resolveTemporalHistoryCopies = Get-UIntMetric $last "ssr_ffx_sssr_resolve_temporal_history_copies"
    $visibleOutputClearEnabled = Get-UIntMetric $last "ssr_ffx_sssr_visible_output_clear_enabled"
    $visibleOutputClears = Get-UIntMetric $last "ssr_ffx_sssr_visible_output_clears"
    $compositeConfidenceMode = Get-UIntMetric $last "ssr_ffx_sssr_composite_confidence_mode"
    $sampleCountWritebackReady = Get-UIntMetric $last "ssr_ffx_sssr_sample_count_writeback_ready"
    $resolveTemporalBindDispatches = Get-UIntMetric $last "ffx_sssr_resolve_temporal_dispatches"
    $resolveTemporalBindDescriptorBinds = Get-UIntMetric $last "ffx_sssr_resolve_temporal_descriptor_binds"
    $resolveTemporalBindHistoryCopies = Get-UIntMetric $last "ffx_sssr_resolve_temporal_history_copies"
    $deferredCompositeRequested = Get-UIntMetric $last "ssr_ffx_sssr_deferred_composite_requested"
    $deferredCompositeActive = Get-UIntMetric $last "ssr_ffx_sssr_deferred_composite_active"
    $deferredCompositeDescriptorBound = Get-UIntMetric $last "ssr_ffx_sssr_deferred_composite_descriptor_bound"
    $deferredCompositeHistoryValid = Get-UIntMetric $last "ssr_ffx_sssr_deferred_composite_history_valid"
    $deferredCompositeSourceImageIndex = Get-UIntMetric $last "ssr_ffx_sssr_deferred_composite_source_image_index"
    $deferredCompositeSource = Get-UIntMetric $last "ssr_ffx_sssr_deferred_composite_source"
    $deferredCompositeQualityGate = Get-UIntMetric $last "ssr_ffx_sssr_deferred_composite_quality_gate"
    $deferredCompositeConfidenceSource = Get-UIntMetric $last "ssr_ffx_sssr_deferred_composite_confidence_source"
    $radianceSource = Get-UIntMetric $last "ssr_radiance_source"
    $dispatchReady = Get-UIntMetric $last "ssr_ffx_sssr_runtime_dispatch_ready"
    $runtimeActive = Get-UIntMetric $last "ssr_ffx_sssr_runtime_active"
    $fallbackReason = Get-UIntMetric $last "ssr_ffx_sssr_fallback_reason"
    $frameGraphIssues = Get-UIntMetric $last "framegraph_validation_issues"
    $validationDiagnostics = @()
    foreach ($path in @($stdoutPath, $stderrPath)) {
        if (Test-Path -LiteralPath $path) {
            $validationDiagnostics += @(
                Select-String `
                    -LiteralPath $path `
                    -Pattern "\[Vulkan Validation\]|VUID-|validation error"
            )
        }
    }
    $prepareDispatchStateMatches = $false
    $prepareExpectedLabel = "0/0 mirrored"
    if ($ExpectPrepareDispatch) {
        $prepareDispatchStateMatches =
            $prepareDispatches -gt 0 -and
            $prepareDescriptorBinds -gt 0 -and
            $prepareBindDispatches -eq $prepareDispatches -and
            $prepareBindDescriptorBinds -eq $prepareDescriptorBinds
        $prepareExpectedLabel = ">0/>0 mirrored"
    } else {
        $prepareDispatchStateMatches =
            $prepareDispatches -eq 0 -and
            $prepareDescriptorBinds -eq 0 -and
            $prepareBindDispatches -eq 0 -and
            $prepareBindDescriptorBinds -eq 0
    }
    $classifyDispatchStateMatches = $false
    $classifyExpectedLabel = "0/0 mirrored"
    if ($ExpectPrepareDispatch) {
        $classifyDispatchStateMatches =
            $classifyDispatches -gt 0 -and
            $classifyDescriptorBinds -gt 0 -and
            $classifyBindDispatches -eq $classifyDispatches -and
            $classifyBindDescriptorBinds -eq $classifyDescriptorBinds -and
            $classifyBindGroupCountX -eq $classifyGroupCountX -and
            $classifyBindGroupCountY -eq $classifyGroupCountY
        $classifyExpectedLabel = ">0/>0 mirrored with matching groups"
    } else {
        $classifyDispatchStateMatches =
            $classifyDispatches -eq 0 -and
            $classifyDescriptorBinds -eq 0 -and
            $classifyBindDispatches -eq 0 -and
            $classifyBindDescriptorBinds -eq 0
    }
    $blueNoiseDispatchStateMatches = $false
    $blueNoiseExpectedLabel = "0/0 mirrored"
    if ($ExpectPrepareDispatch) {
        $blueNoiseDispatchStateMatches =
            $blueNoiseDispatches -gt 0 -and
            $blueNoiseDescriptorBinds -gt 0 -and
            $blueNoiseBindDispatches -eq $blueNoiseDispatches -and
            $blueNoiseBindDescriptorBinds -eq $blueNoiseDescriptorBinds -and
            $blueNoiseBindGroupCountX -eq $blueNoiseGroupCountX -and
            $blueNoiseBindGroupCountY -eq $blueNoiseGroupCountY
        $blueNoiseExpectedLabel = ">0/>0 mirrored with matching 16x16 groups"
    } else {
        $blueNoiseDispatchStateMatches =
            $blueNoiseDispatches -eq 0 -and
            $blueNoiseDescriptorBinds -eq 0 -and
            $blueNoiseBindDispatches -eq 0 -and
            $blueNoiseBindDescriptorBinds -eq 0
    }
    $intersectDispatchStateMatches = $false
    $intersectExpectedLabel = "0/0 mirrored"
    if ($ExpectPrepareDispatch) {
        $intersectDispatchStateMatches =
            $intersectDispatches -gt 0 -and
            $intersectDescriptorBinds -gt 0 -and
            $intersectBindDispatches -eq $intersectDispatches -and
            $intersectBindDescriptorBinds -eq $intersectDescriptorBinds
        $intersectExpectedLabel = ">0/>0 mirrored"
    } else {
        $intersectDispatchStateMatches =
            $intersectDispatches -eq 0 -and
            $intersectDescriptorBinds -eq 0 -and
            $intersectBindDispatches -eq 0 -and
            $intersectBindDescriptorBinds -eq 0
    }
    $reprojectDispatchStateMatches = $false
    $reprojectExpectedLabel = "0/0 mirrored"
    if ($ExpectPrepareDispatch) {
        $reprojectDispatchStateMatches =
            $reprojectDispatches -gt 0 -and
            $reprojectDescriptorBinds -gt 0 -and
            $reprojectBindDispatches -eq $reprojectDispatches -and
            $reprojectBindDescriptorBinds -eq $reprojectDescriptorBinds
        $reprojectExpectedLabel = ">0/>0 mirrored"
    } else {
        $reprojectDispatchStateMatches =
            $reprojectDispatches -eq 0 -and
            $reprojectDescriptorBinds -eq 0 -and
            $reprojectBindDispatches -eq 0 -and
            $reprojectBindDescriptorBinds -eq 0
    }
    $prefilterDispatchStateMatches = $false
    $prefilterExpectedLabel = "0/0 mirrored"
    if ($ExpectPrepareDispatch) {
        $prefilterDispatchStateMatches =
            $prefilterDispatches -gt 0 -and
            $prefilterDescriptorBinds -gt 0 -and
            $prefilterBindDispatches -eq $prefilterDispatches -and
            $prefilterBindDescriptorBinds -eq $prefilterDescriptorBinds
        $prefilterExpectedLabel = ">0/>0 mirrored"
    } else {
        $prefilterDispatchStateMatches =
            $prefilterDispatches -eq 0 -and
            $prefilterDescriptorBinds -eq 0 -and
            $prefilterBindDispatches -eq 0 -and
            $prefilterBindDescriptorBinds -eq 0
    }
    $resolveTemporalDispatchStateMatches = $false
    $resolveTemporalExpectedLabel = "0/0/0 mirrored"
    if ($ExpectPrepareDispatch) {
        $resolveTemporalDispatchStateMatches =
            $resolveTemporalDispatches -gt 0 -and
            $resolveTemporalDescriptorBinds -gt 0 -and
            $resolveTemporalHistoryCopies -gt 0 -and
            $resolveTemporalBindDispatches -eq $resolveTemporalDispatches -and
            $resolveTemporalBindDescriptorBinds -eq $resolveTemporalDescriptorBinds -and
            $resolveTemporalBindHistoryCopies -eq $resolveTemporalHistoryCopies
        $resolveTemporalExpectedLabel = ">0/>0/>0 mirrored"
    } else {
        $resolveTemporalDispatchStateMatches =
            $resolveTemporalDispatches -eq 0 -and
            $resolveTemporalDescriptorBinds -eq 0 -and
            $resolveTemporalHistoryCopies -eq 0 -and
            $resolveTemporalBindDispatches -eq 0 -and
            $resolveTemporalBindDescriptorBinds -eq 0 -and
            $resolveTemporalBindHistoryCopies -eq 0
    }
    $expectedRayCapacity = $classifyWidth * $classifyHeight
    $expectedTileCapacity =
        [uint32]([Math]::Ceiling($classifyWidth / 8.0) *
            [Math]::Ceiling($classifyHeight / 8.0))
    $expectedClassifyGroupsX = [uint32][Math]::Ceiling($classifyWidth / 8.0)
    $expectedClassifyGroupsY = [uint32][Math]::Ceiling($classifyHeight / 8.0)
    $classifyGroupStateMatches = $false
    $classifyGroupExpectedLabel = "resource ceil groups, dispatch binds suppressed"
    if ($ExpectPrepareDispatch) {
        $classifyGroupStateMatches =
            $classifyGroupCountX -eq $expectedClassifyGroupsX -and
            $classifyGroupCountY -eq $expectedClassifyGroupsY
        $classifyGroupExpectedLabel = "ceil groups"
    } else {
        $classifyGroupStateMatches =
            $classifyGroupCountX -eq 0 -and
            $classifyGroupCountY -eq 0
    }
    $classifyCapacityMatches =
        $classifyWidth -gt 0 -and
        $classifyHeight -gt 0 -and
        $classifyGroupStateMatches -and
        $classifyRayListCapacity -ge $expectedRayCapacity -and
        $classifyDenoiserTileListCapacity -ge $expectedTileCapacity -and
        $classifyMemoryBytes -gt $prepareBufferBytes
    $classifyReadbackMatches = $true
    $classifyReadbackExpected = "not required for disabled lane"
    if ($ExpectPrepareDispatch) {
        $classifyReadbackMatches =
            $classifyReadbackValid -eq 1 -and
            $classifyRayCount -le $classifyRayListCapacity -and
            $classifyDenoiserTileCount -le $classifyDenoiserTileListCapacity
        $classifyReadbackExpected = "valid and counts within capacity"
    } else {
        $classifyReadbackMatches =
            $classifyReadbackValid -eq 0 -and
            $classifyRayCount -eq 0 -and
            $classifyDenoiserTileCount -eq 0
    }
    $expectedBlueNoiseGroupCountX = 0
    $expectedBlueNoiseGroupCountY = 0
    if ($ExpectPrepareDispatch) {
        $expectedBlueNoiseGroupCountX = 16
        $expectedBlueNoiseGroupCountY = 16
    }
    $blueNoiseGroupExpectedLabel =
        "${expectedBlueNoiseGroupCountX}x${expectedBlueNoiseGroupCountY}"
    $blueNoiseResourceContractMatches =
        $blueNoiseResourcesReady -eq 1 -and
        $blueNoiseDescriptorSetsReady -eq 1 -and
        $blueNoisePipelineReady -eq 1 -and
        $blueNoiseWidth -eq 128 -and
        $blueNoiseHeight -eq 128 -and
        $blueNoiseGroupCountX -eq $expectedBlueNoiseGroupCountX -and
        $blueNoiseGroupCountY -eq $expectedBlueNoiseGroupCountY -and
        $blueNoiseSobolEntryCount -eq (256 * 256) -and
        $blueNoiseRankingTileEntryCount -eq (128 * 128 * 8) -and
        $blueNoiseScramblingTileEntryCount -eq (128 * 128 * 8) -and
        $blueNoiseMemoryBytes -gt 0
    $intersectResourceContractMatches =
        $intersectResourcesReady -eq 1 -and
        $intersectDescriptorSetsReady -eq 1 -and
        $intersectPipelineReady -eq 1 -and
        $intersectInputContractReady -eq 1 -and
        $intersectWidth -eq $classifyWidth -and
        $intersectHeight -eq $classifyHeight -and
        $intersectDepthPyramidMipCount -gt 1
    $reprojectResourceContractMatches =
        $reprojectResourcesReady -eq 1 -and
        $reprojectDescriptorSetsReady -eq 1 -and
        $reprojectPipelineReady -eq 1 -and
        $reprojectInputContractReady -eq 1 -and
        $reprojectWidth -eq $classifyWidth -and
        $reprojectHeight -eq $classifyHeight -and
        $reprojectWidth -eq $intersectWidth -and
        $reprojectHeight -eq $intersectHeight -and
        $reprojectAverageWidth -eq $expectedClassifyGroupsX -and
        $reprojectAverageHeight -eq $expectedClassifyGroupsY -and
        $reprojectHistoryReady -eq 1 -and
        $reprojectHistorySource -eq 1 -and
        $reprojectHistoryMetadataSource -eq 1 -and
        $reprojectMemoryBytes -gt $classifyMemoryBytes -and
        $reprojectIndirectArgsOffsetBytes -eq 12
    $reprojectMotionVectorContractMatches =
        $reprojectMotionVectorContractReady -eq 1 -and
        $reprojectMotionVectorMode -eq $ExpectedMotionVectorMode -and
        [Math]::Abs($reprojectMotionVectorScaleX - $ExpectedMotionVectorScaleX) -lt 0.0001 -and
        [Math]::Abs($reprojectMotionVectorScaleY - $ExpectedMotionVectorScaleY) -lt 0.0001
    $reprojectHitReprojectionContractMatches =
        $reprojectReprojectionContractReady -eq 1 -and
        $reprojectHitReprojectionEnabled -eq $ExpectedHitReprojectionEnabled
    $prefilterResourceContractMatches =
        $prefilterResourcesReady -eq 1 -and
        $prefilterDescriptorSetsReady -eq 1 -and
        $prefilterPipelineReady -eq 1 -and
        $prefilterInputContractReady -eq 1 -and
        $prefilterWidth -eq $classifyWidth -and
        $prefilterHeight -eq $classifyHeight -and
        $prefilterWidth -eq $reprojectWidth -and
        $prefilterHeight -eq $reprojectHeight -and
        $prefilterMemoryBytes -gt 0 -and
        $prefilterMemoryBytes -lt $reprojectMemoryBytes -and
        $prefilterIndirectArgsOffsetBytes -eq 12
    $resolveTemporalResourceContractMatches =
        $resolveTemporalResourcesReady -eq 1 -and
        $resolveTemporalDescriptorSetsReady -eq 1 -and
        $resolveTemporalPipelineReady -eq 1 -and
        $resolveTemporalInputContractReady -eq 1 -and
        $resolveTemporalHistoryWritebackReady -eq 1 -and
        $resolveTemporalWidth -eq $classifyWidth -and
        $resolveTemporalHeight -eq $classifyHeight -and
        $resolveTemporalWidth -eq $reprojectWidth -and
        $resolveTemporalHeight -eq $reprojectHeight -and
        $resolveTemporalWidth -eq $prefilterWidth -and
        $resolveTemporalHeight -eq $prefilterHeight -and
        $resolveTemporalMemoryBytes -eq 0 -and
        $resolveTemporalIndirectArgsOffsetBytes -eq 12 -and
        $sampleCountWritebackReady -eq 1
    $deferredCompositeMatches = $false
    $deferredCompositeExpectedLabel = "0/0/0/0/source0/radiance!=4"
    if ($ExpectDeferredComposite) {
        $deferredCompositeMatches =
            $deferredCompositeRequested -eq 1 -and
            $deferredCompositeActive -eq 1 -and
            $deferredCompositeDescriptorBound -eq 1 -and
            $deferredCompositeHistoryValid -eq 1 -and
            $deferredCompositeSource -eq 1 -and
            $deferredCompositeQualityGate -eq 1 -and
            $deferredCompositeConfidenceSource -eq 1 -and
            $radianceSource -eq 4 -and
            $deferredCompositeSourceImageIndex -lt 16
        $deferredCompositeExpectedLabel =
            "1/1/1/1/source1/quality1/confidence1/radiance4,sourceIndex<16"
    } else {
        $deferredCompositeMatches =
            $deferredCompositeRequested -eq 0 -and
            $deferredCompositeActive -eq 0 -and
            $deferredCompositeDescriptorBound -eq 0 -and
            $deferredCompositeSource -eq 0 -and
            $deferredCompositeQualityGate -eq 0 -and
            $deferredCompositeConfidenceSource -eq 0 -and
            $radianceSource -ne 4
    }

    $environmentMipContractMatches =
        $environmentMipCount -gt 1 -and
        $environmentMipCount -eq $iblPrefilteredMipCount
    $environmentMipExpectedLabel = ">1/equal"
    if ($ExpectedEnvironmentMipCount -gt 0) {
        $environmentMipContractMatches =
            $environmentMipCount -eq $ExpectedEnvironmentMipCount -and
            $iblPrefilteredMipCount -gt $environmentMipCount
        $environmentMipExpectedLabel =
            "$ExpectedEnvironmentMipCount/iblMips>resolved"
    }

    $checks = @(
        (New-Check "$Name requested provider" `
            ($requestedProvider -eq $ExpectedRequestedProvider) `
            "$requestedProvider" "$ExpectedRequestedProvider"),
        (New-Check "$Name active provider" `
            ($activeProvider -eq $ExpectedActiveProvider) `
            "$activeProvider" "$ExpectedActiveProvider"),
        (New-Check "$Name FFX source contract ready" `
            ($contractVersion -eq 11 -and $sourceReady -eq 1) `
            "contract=$contractVersion,source=$sourceReady" "11/1"),
        (New-Check "$Name FFX shader build integrated" `
            ($shaderBuild -eq 1 -and $shaderCount -eq 8) `
            "build=$shaderBuild,count=$shaderCount" "1/8"),
        (New-Check "$Name FFX dependencies ready" `
            ($denoiserReady -eq 1 -and $spdReady -eq 1) `
            "dnsr=$denoiserReady,spd=$spdReady" "1/1"),
        (New-Check "$Name FFX constants ready" `
            ($constantsResourcesReady -eq 1 -and
                $constantsDescriptorSetsReady -eq 1) `
            "resources=$constantsResourcesReady,sets=$constantsDescriptorSetsReady" `
            "1/1"),
        (New-Check "$Name temporal stability factor contract" `
            ([Math]::Abs($temporalStabilityFactor - $ExpectedTemporalStabilityFactor) -lt 0.0001) `
            "$temporalStabilityFactor" "$ExpectedTemporalStabilityFactor"),
        (New-Check "$Name samples-per-quad contract" `
            ($samplesPerQuad -eq $ExpectedSamplesPerQuad) `
            "$samplesPerQuad" "$ExpectedSamplesPerQuad"),
        (New-Check "$Name stable environment fallback contract" `
            ($stableEnvironmentFallbackEnabled -eq $ExpectedStableEnvironmentFallbackEnabled -and
                $environmentMipContractMatches) `
            "enabled=$stableEnvironmentFallbackEnabled,mips=$environmentMipCount,iblMips=$iblPrefilteredMipCount" `
            "$ExpectedStableEnvironmentFallbackEnabled/$environmentMipExpectedLabel"),
        (New-Check "$Name constant environment fallback control" `
            ($constantEnvironmentFallbackEnabled -eq $ExpectedConstantEnvironmentFallbackEnabled) `
            "$constantEnvironmentFallbackEnabled" `
            "$ExpectedConstantEnvironmentFallbackEnabled"),
        (New-Check "$Name perfect reflection directions control" `
            ($perfectReflectionDirectionsEnabled -eq $ExpectedPerfectReflectionDirectionsEnabled) `
            "$perfectReflectionDirectionsEnabled" `
            "$ExpectedPerfectReflectionDirectionsEnabled"),
        (New-Check "$Name spatial prefilter bypass control" `
            ($prefilterBypassEnabled -eq $ExpectedPrefilterBypassEnabled) `
            "$prefilterBypassEnabled" `
            "$ExpectedPrefilterBypassEnabled"),
        (New-Check "$Name temporal resolve bypass control" `
            ($resolveTemporalBypassEnabled -eq $ExpectedResolveTemporalBypassEnabled) `
            "$resolveTemporalBypassEnabled" `
            "$ExpectedResolveTemporalBypassEnabled"),
        (New-Check "$Name Classify surface coverage seed control" `
            ($classifySurfaceSeedEnabled -eq $ExpectedClassifySurfaceSeedEnabled) `
            "$classifySurfaceSeedEnabled" `
            "$ExpectedClassifySurfaceSeedEnabled"),
        (New-Check "$Name Intersect ray coverage marker control" `
            ($intersectCoverageMarkerEnabled -eq $ExpectedIntersectCoverageMarkerEnabled) `
            "$intersectCoverageMarkerEnabled" `
            "$ExpectedIntersectCoverageMarkerEnabled"),
        (New-Check "$Name Deferred receiver reprojection control" `
            ($deferredReceiverReprojectionEnabled -eq $ExpectedDeferredReceiverReprojectionEnabled -and
                $deferredValidatedBilinearEnabled -eq $ExpectedDeferredReceiverReprojectionEnabled -and
                $deferredHistoryTapCount -eq (4 * $ExpectedDeferredReceiverReprojectionEnabled)) `
            "reproject=$deferredReceiverReprojectionEnabled,validatedBilinear=$deferredValidatedBilinearEnabled,taps=$deferredHistoryTapCount" `
            "$ExpectedDeferredReceiverReprojectionEnabled/$ExpectedDeferredReceiverReprojectionEnabled/$(4 * $ExpectedDeferredReceiverReprojectionEnabled)"),
        (New-Check "$Name visible output clear control" `
            ($visibleOutputClearEnabled -eq $ExpectedVisibleOutputClearEnabled -and
                $visibleOutputClears -eq $ExpectedVisibleOutputClears) `
            "enabled=$visibleOutputClearEnabled,clears=$visibleOutputClears" `
            "$ExpectedVisibleOutputClearEnabled/$ExpectedVisibleOutputClears"),
        (New-Check "$Name composite confidence mode" `
            ($compositeConfidenceMode -eq $ExpectedCompositeConfidenceMode) `
            "$compositeConfidenceMode" `
            "$ExpectedCompositeConfidenceMode"),
        (New-Check "$Name prepare-args resources ready" `
            ($prepareResourcesReady -eq 1 -and
                $prepareDescriptorSetsReady -eq 1 -and
                $preparePipelineReady -eq 1 -and
                $prepareBufferBytes -gt 0) `
            "resources=$prepareResourcesReady,sets=$prepareDescriptorSetsReady,pipeline=$preparePipelineReady,bytes=$prepareBufferBytes" `
            "1/1/1/>0"),
        (New-Check "$Name prepare-args dispatch/bind state" `
            $prepareDispatchStateMatches `
            "stats=$prepareDispatches/$prepareDescriptorBinds,binds=$prepareBindDispatches/$prepareBindDescriptorBinds" `
            $prepareExpectedLabel),
        (New-Check "$Name classify-tiles resources ready" `
            ($classifyResourcesReady -eq 1 -and
                $classifyDescriptorSetsReady -eq 1 -and
                $classifyPipelineReady -eq 1 -and
                $classifyInputContractReady -eq 1) `
            "resources=$classifyResourcesReady,sets=$classifyDescriptorSetsReady,pipeline=$classifyPipelineReady,input=$classifyInputContractReady" `
            "1/1/1/1"),
        (New-Check "$Name classify-tiles capacity contract" `
            $classifyCapacityMatches `
            "extent=${classifyWidth}x${classifyHeight},groups=${classifyGroupCountX}x${classifyGroupCountY},capacity=$classifyRayListCapacity/$classifyDenoiserTileListCapacity,bytes=$classifyMemoryBytes" `
            "extent>0,$classifyGroupExpectedLabel,capacity>=pixels/tiles,bytes>prepare"),
        (New-Check "$Name classify-tiles dispatch/bind state" `
            $classifyDispatchStateMatches `
            "stats=$classifyDispatches/$classifyDescriptorBinds,binds=$classifyBindDispatches/$classifyBindDescriptorBinds,groups=${classifyBindGroupCountX}x${classifyBindGroupCountY}" `
            $classifyExpectedLabel),
        (New-Check "$Name classify-tiles counter readback" `
            $classifyReadbackMatches `
            "valid=$classifyReadbackValid,rays=$classifyRayCount/$classifyRayListCapacity,tiles=$classifyDenoiserTileCount/$classifyDenoiserTileListCapacity" `
            $classifyReadbackExpected),
        (New-Check "$Name blue-noise resource contract" `
            $blueNoiseResourceContractMatches `
            "resources=$blueNoiseResourcesReady,sets=$blueNoiseDescriptorSetsReady,pipeline=$blueNoisePipelineReady,extent=${blueNoiseWidth}x${blueNoiseHeight},groups=${blueNoiseGroupCountX}x${blueNoiseGroupCountY},tables=$blueNoiseSobolEntryCount/$blueNoiseRankingTileEntryCount/$blueNoiseScramblingTileEntryCount,bytes=$blueNoiseMemoryBytes" `
            "1/1/1,128x128,$blueNoiseGroupExpectedLabel,65536/131072/131072,bytes>0"),
        (New-Check "$Name blue-noise dispatch/bind state" `
            $blueNoiseDispatchStateMatches `
            "stats=$blueNoiseDispatches/$blueNoiseDescriptorBinds,binds=$blueNoiseBindDispatches/$blueNoiseBindDescriptorBinds,groups=${blueNoiseBindGroupCountX}x${blueNoiseBindGroupCountY}" `
            $blueNoiseExpectedLabel),
        (New-Check "$Name intersect resource contract" `
            $intersectResourceContractMatches `
            "resources=$intersectResourcesReady,sets=$intersectDescriptorSetsReady,pipeline=$intersectPipelineReady,input=$intersectInputContractReady,extent=${intersectWidth}x${intersectHeight},depthMips=$intersectDepthPyramidMipCount" `
            "1/1/1/1,extent==classify,mips>1"),
        (New-Check "$Name intersect dispatch/bind state" `
            $intersectDispatchStateMatches `
            "stats=$intersectDispatches/$intersectDescriptorBinds,binds=$intersectBindDispatches/$intersectBindDescriptorBinds" `
            $intersectExpectedLabel),
        (New-Check "$Name reproject resource contract" `
            $reprojectResourceContractMatches `
            "resources=$reprojectResourcesReady,sets=$reprojectDescriptorSetsReady,pipeline=$reprojectPipelineReady,input=$reprojectInputContractReady,extent=${reprojectWidth}x${reprojectHeight},average=${reprojectAverageWidth}x${reprojectAverageHeight},history=$reprojectHistoryReady/source$reprojectHistorySource/metadata$reprojectHistoryMetadataSource,bytes=$reprojectMemoryBytes,offset=$reprojectIndirectArgsOffsetBytes" `
            "1/1/1/1,extent==classify/intersect,average==ceil8,history=1/source1/metadata1,bytes>classify,offset=12"),
        (New-Check "$Name reproject motion-vector contract" `
            $reprojectMotionVectorContractMatches `
            "ready=$reprojectMotionVectorContractReady,mode=$reprojectMotionVectorMode,scale=$reprojectMotionVectorScaleX/$reprojectMotionVectorScaleY" `
            "ready=1,mode=$ExpectedMotionVectorMode,scale=$ExpectedMotionVectorScaleX/$ExpectedMotionVectorScaleY"),
        (New-Check "$Name reproject hit-position contract" `
            $reprojectHitReprojectionContractMatches `
            "ready=$reprojectReprojectionContractReady,hit=$reprojectHitReprojectionEnabled" `
            "ready=1,hit=$ExpectedHitReprojectionEnabled"),
        (New-Check "$Name reproject dispatch/bind state" `
            $reprojectDispatchStateMatches `
            "stats=$reprojectDispatches/$reprojectDescriptorBinds,binds=$reprojectBindDispatches/$reprojectBindDescriptorBinds" `
            $reprojectExpectedLabel),
        (New-Check "$Name prefilter resource contract" `
            $prefilterResourceContractMatches `
            "resources=$prefilterResourcesReady,sets=$prefilterDescriptorSetsReady,pipeline=$prefilterPipelineReady,input=$prefilterInputContractReady,extent=${prefilterWidth}x${prefilterHeight},bytes=$prefilterMemoryBytes,offset=$prefilterIndirectArgsOffsetBytes" `
            "1/1/1/1,extent==classify/reproject,0<bytes<reproject,offset=12"),
        (New-Check "$Name prefilter dispatch/bind state" `
            $prefilterDispatchStateMatches `
            "stats=$prefilterDispatches/$prefilterDescriptorBinds,binds=$prefilterBindDispatches/$prefilterBindDescriptorBinds" `
            $prefilterExpectedLabel),
        (New-Check "$Name resolve-temporal resource/history contract" `
            $resolveTemporalResourceContractMatches `
            "resources=$resolveTemporalResourcesReady,sets=$resolveTemporalDescriptorSetsReady,pipeline=$resolveTemporalPipelineReady,input=$resolveTemporalInputContractReady,history=$resolveTemporalHistoryWritebackReady,sampleCount=$sampleCountWritebackReady,extent=${resolveTemporalWidth}x${resolveTemporalHeight},bytes=$resolveTemporalMemoryBytes,offset=$resolveTemporalIndirectArgsOffsetBytes" `
            "1/1/1/1/1,sampleCount=1,extent==classify/reproject/prefilter,bytes=0,offset=12"),
        (New-Check "$Name resolve-temporal dispatch/bind/history-copy state" `
            $resolveTemporalDispatchStateMatches `
            "stats=$resolveTemporalDispatches/$resolveTemporalDescriptorBinds/$resolveTemporalHistoryCopies,binds=$resolveTemporalBindDispatches/$resolveTemporalBindDescriptorBinds/$resolveTemporalBindHistoryCopies" `
            $resolveTemporalExpectedLabel),
        (New-Check "$Name deferred composite consumes FFX history" `
            $deferredCompositeMatches `
            "requested=$deferredCompositeRequested,active=$deferredCompositeActive,descriptor=$deferredCompositeDescriptorBound,history=$deferredCompositeHistoryValid,source=$deferredCompositeSource,quality=$deferredCompositeQualityGate,confidence=$deferredCompositeConfidenceSource,sourceIndex=$deferredCompositeSourceImageIndex,radiance=$radianceSource" `
            $deferredCompositeExpectedLabel),
        (New-Check "$Name runtime dispatch state" `
            ($dispatchReady -eq $ExpectedDispatchReady -and
                $runtimeActive -eq $ExpectedRuntimeActive) `
            "dispatch=$dispatchReady,active=$runtimeActive" `
            "$ExpectedDispatchReady/$ExpectedRuntimeActive"),
        (New-Check "$Name frame graph validation clean" `
            ($frameGraphIssues -eq 0) `
            "$frameGraphIssues" "0"),
        (New-Check "$Name Vulkan validation diagnostics clean" `
            ($validationDiagnostics.Count -eq 0) `
            "$($validationDiagnostics.Count)" "0"),
        (New-Check "$Name fallback reason" `
            ($fallbackReason -eq $ExpectedFallbackReason) `
            "$fallbackReason" "$ExpectedFallbackReason")
    )
    $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
    $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
    return [pscustomobject]@{
        lane = $Name
        executable = $Executable
        csv = $csvPath
        verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
        passCount = $passCount
        failCount = $failCount
        metrics = [pscustomobject]@{
            requestedProvider = $requestedProvider
            activeProvider = $activeProvider
            contractVersion = $contractVersion
            sourceReady = $sourceReady
            shaderBuildIntegrated = $shaderBuild
            shaderCount = $shaderCount
            denoiserDependencyReady = $denoiserReady
            spdDependencyReady = $spdReady
            constantsResourcesReady = $constantsResourcesReady
            constantsDescriptorSetsReady = $constantsDescriptorSetsReady
            temporalStabilityFactor = $temporalStabilityFactor
            samplesPerQuad = $samplesPerQuad
            stableEnvironmentFallbackEnabled = $stableEnvironmentFallbackEnabled
            constantEnvironmentFallbackEnabled = $constantEnvironmentFallbackEnabled
            perfectReflectionDirectionsEnabled = $perfectReflectionDirectionsEnabled
            prefilterBypassEnabled = $prefilterBypassEnabled
            resolveTemporalBypassEnabled = $resolveTemporalBypassEnabled
            classifySurfaceSeedEnabled = $classifySurfaceSeedEnabled
            intersectCoverageMarkerEnabled = $intersectCoverageMarkerEnabled
            environmentMipCount = $environmentMipCount
            iblPrefilteredMipCount = $iblPrefilteredMipCount
            deferredReceiverReprojectionEnabled = $deferredReceiverReprojectionEnabled
            deferredValidatedBilinearEnabled = $deferredValidatedBilinearEnabled
            deferredHistoryTapCount = $deferredHistoryTapCount
            prepareIndirectArgsResourcesReady = $prepareResourcesReady
            prepareIndirectArgsDescriptorSetsReady = $prepareDescriptorSetsReady
            prepareIndirectArgsPipelineReady = $preparePipelineReady
            prepareIndirectArgsDispatches = $prepareDispatches
            prepareIndirectArgsDescriptorBinds = $prepareDescriptorBinds
            prepareIndirectArgsBindDispatches = $prepareBindDispatches
            prepareIndirectArgsBindDescriptorBinds = $prepareBindDescriptorBinds
            prepareIndirectArgsBufferBytes = $prepareBufferBytes
            classifyTilesResourcesReady = $classifyResourcesReady
            classifyTilesDescriptorSetsReady = $classifyDescriptorSetsReady
            classifyTilesPipelineReady = $classifyPipelineReady
            classifyTilesInputContractReady = $classifyInputContractReady
            classifyTilesDispatches = $classifyDispatches
            classifyTilesDescriptorBinds = $classifyDescriptorBinds
            classifyTilesWidth = $classifyWidth
            classifyTilesHeight = $classifyHeight
            classifyTilesGroupCountX = $classifyGroupCountX
            classifyTilesGroupCountY = $classifyGroupCountY
            classifyTilesRayListCapacity = $classifyRayListCapacity
            classifyTilesDenoiserTileListCapacity = $classifyDenoiserTileListCapacity
            classifyTilesMemoryBytes = $classifyMemoryBytes
            classifyTilesReadbackValid = $classifyReadbackValid
            classifyTilesRayCount = $classifyRayCount
            classifyTilesDenoiserTileCount = $classifyDenoiserTileCount
            classifyTilesBindDispatches = $classifyBindDispatches
            classifyTilesBindDescriptorBinds = $classifyBindDescriptorBinds
            classifyTilesBindGroupCountX = $classifyBindGroupCountX
            classifyTilesBindGroupCountY = $classifyBindGroupCountY
            blueNoiseResourcesReady = $blueNoiseResourcesReady
            blueNoiseDescriptorSetsReady = $blueNoiseDescriptorSetsReady
            blueNoisePipelineReady = $blueNoisePipelineReady
            blueNoiseDispatches = $blueNoiseDispatches
            blueNoiseDescriptorBinds = $blueNoiseDescriptorBinds
            blueNoiseWidth = $blueNoiseWidth
            blueNoiseHeight = $blueNoiseHeight
            blueNoiseGroupCountX = $blueNoiseGroupCountX
            blueNoiseGroupCountY = $blueNoiseGroupCountY
            blueNoiseSobolEntryCount = $blueNoiseSobolEntryCount
            blueNoiseRankingTileEntryCount = $blueNoiseRankingTileEntryCount
            blueNoiseScramblingTileEntryCount = $blueNoiseScramblingTileEntryCount
            blueNoiseMemoryBytes = $blueNoiseMemoryBytes
            blueNoiseBindDispatches = $blueNoiseBindDispatches
            blueNoiseBindDescriptorBinds = $blueNoiseBindDescriptorBinds
            blueNoiseBindGroupCountX = $blueNoiseBindGroupCountX
            blueNoiseBindGroupCountY = $blueNoiseBindGroupCountY
            intersectResourcesReady = $intersectResourcesReady
            intersectDescriptorSetsReady = $intersectDescriptorSetsReady
            intersectPipelineReady = $intersectPipelineReady
            intersectInputContractReady = $intersectInputContractReady
            intersectDispatches = $intersectDispatches
            intersectDescriptorBinds = $intersectDescriptorBinds
            intersectWidth = $intersectWidth
            intersectHeight = $intersectHeight
            intersectDepthPyramidMipCount = $intersectDepthPyramidMipCount
            intersectBindDispatches = $intersectBindDispatches
            intersectBindDescriptorBinds = $intersectBindDescriptorBinds
            reprojectResourcesReady = $reprojectResourcesReady
            reprojectDescriptorSetsReady = $reprojectDescriptorSetsReady
            reprojectPipelineReady = $reprojectPipelineReady
            reprojectInputContractReady = $reprojectInputContractReady
            reprojectDispatches = $reprojectDispatches
            reprojectDescriptorBinds = $reprojectDescriptorBinds
            reprojectWidth = $reprojectWidth
            reprojectHeight = $reprojectHeight
            reprojectAverageWidth = $reprojectAverageWidth
            reprojectAverageHeight = $reprojectAverageHeight
            reprojectHistoryReady = $reprojectHistoryReady
            reprojectHistorySource = $reprojectHistorySource
            reprojectHistoryMetadataSource = $reprojectHistoryMetadataSource
            reprojectMemoryBytes = $reprojectMemoryBytes
            reprojectIndirectArgsOffsetBytes = $reprojectIndirectArgsOffsetBytes
            reprojectMotionVectorMode = $reprojectMotionVectorMode
            reprojectMotionVectorScaleX = $reprojectMotionVectorScaleX
            reprojectMotionVectorScaleY = $reprojectMotionVectorScaleY
            reprojectMotionVectorContractReady = $reprojectMotionVectorContractReady
            reprojectHitReprojectionEnabled = $reprojectHitReprojectionEnabled
            reprojectReprojectionContractReady = $reprojectReprojectionContractReady
            reprojectBindDispatches = $reprojectBindDispatches
            reprojectBindDescriptorBinds = $reprojectBindDescriptorBinds
            prefilterResourcesReady = $prefilterResourcesReady
            prefilterDescriptorSetsReady = $prefilterDescriptorSetsReady
            prefilterPipelineReady = $prefilterPipelineReady
            prefilterInputContractReady = $prefilterInputContractReady
            prefilterDispatches = $prefilterDispatches
            prefilterDescriptorBinds = $prefilterDescriptorBinds
            prefilterWidth = $prefilterWidth
            prefilterHeight = $prefilterHeight
            prefilterMemoryBytes = $prefilterMemoryBytes
            prefilterIndirectArgsOffsetBytes = $prefilterIndirectArgsOffsetBytes
            prefilterBindDispatches = $prefilterBindDispatches
            prefilterBindDescriptorBinds = $prefilterBindDescriptorBinds
            resolveTemporalResourcesReady = $resolveTemporalResourcesReady
            resolveTemporalDescriptorSetsReady = $resolveTemporalDescriptorSetsReady
            resolveTemporalPipelineReady = $resolveTemporalPipelineReady
            resolveTemporalInputContractReady = $resolveTemporalInputContractReady
            resolveTemporalHistoryWritebackReady = $resolveTemporalHistoryWritebackReady
            resolveTemporalDispatches = $resolveTemporalDispatches
            resolveTemporalDescriptorBinds = $resolveTemporalDescriptorBinds
            resolveTemporalWidth = $resolveTemporalWidth
            resolveTemporalHeight = $resolveTemporalHeight
            resolveTemporalMemoryBytes = $resolveTemporalMemoryBytes
            resolveTemporalIndirectArgsOffsetBytes = $resolveTemporalIndirectArgsOffsetBytes
            resolveTemporalHistoryCopies = $resolveTemporalHistoryCopies
            visibleOutputClearEnabled = $visibleOutputClearEnabled
            visibleOutputClears = $visibleOutputClears
            compositeConfidenceMode = $compositeConfidenceMode
            sampleCountWritebackReady = $sampleCountWritebackReady
            resolveTemporalBindDispatches = $resolveTemporalBindDispatches
            resolveTemporalBindDescriptorBinds = $resolveTemporalBindDescriptorBinds
            resolveTemporalBindHistoryCopies = $resolveTemporalBindHistoryCopies
            deferredCompositeRequested = $deferredCompositeRequested
            deferredCompositeActive = $deferredCompositeActive
            deferredCompositeDescriptorBound = $deferredCompositeDescriptorBound
            deferredCompositeHistoryValid = $deferredCompositeHistoryValid
            deferredCompositeSourceImageIndex = $deferredCompositeSourceImageIndex
            deferredCompositeSource = $deferredCompositeSource
            deferredCompositeQualityGate = $deferredCompositeQualityGate
            deferredCompositeConfidenceSource = $deferredCompositeConfidenceSource
            radianceSource = $radianceSource
            runtimeDispatchReady = $dispatchReady
            runtimeActive = $runtimeActive
            frameGraphValidationIssues = $frameGraphIssues
            validationDiagnostics = $validationDiagnostics.Count
            fallbackReason = $fallbackReason
        }
        checks = $checks
    }
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

if (!$SkipBuild) {
    Invoke-Build
}

$reports = @()
$reports += Invoke-StaticChecks

if ($StaticOnly) {
    $passCount = ($reports | ForEach-Object { $_.passCount } |
        Measure-Object -Sum).Sum
    $failCount = ($reports | ForEach-Object { $_.failCount } |
        Measure-Object -Sum).Sum
    $summary = [pscustomobject]@{
        generatedAt = (Get-Date).ToString("o")
        outputDirectory = $OutputDirectory
        staticOnly = $true
        verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
        passCount = [int]$passCount
        failCount = [int]$failCount
        reports = $reports
    }
    $summary | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $OutputDirectory "summary.json") `
            -Encoding utf8
    $summary

    if ($Strict -and $failCount -gt 0) {
        exit 1
    }
    return
}

$forwardExecutable = Resolve-FullPath $ForwardExecutablePath
$showcaseExecutable = Resolve-FullPath $ShowcaseExecutablePath
foreach ($executable in @($forwardExecutable, $showcaseExecutable)) {
    if (!(Test-Path -LiteralPath $executable)) {
        throw "Missing executable: $executable"
    }
}

$common = @{
    SE_WINDOW_HIDDEN = "1"
    SE_HIDE_IMGUI = "1"
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_BENCHMARK_WARMUP_FRAMES = "4"
    SE_BENCHMARK_FRAMES = "4"
    SE_AUTO_EXIT_FRAMES = "10"
    SE_FORWARD3D_AA_MODE = "taa"
    SE_SSR = "1"
}

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-backend-contract" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-legacy-motion-vector-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_MOTION_VECTOR_MODE = "legacy-ndc"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedMotionVectorMode 2 `
    -ExpectedMotionVectorScaleX 0.5 `
    -ExpectedMotionVectorScaleY -0.5

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-hit-reprojection-off-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_HIT_REPROJECTION_OFF = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedHitReprojectionEnabled 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-temporal-stability-075-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_TEMPORAL_STABILITY_FACTOR = "0.75"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedTemporalStabilityFactor 0.75

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-temporal-stability-000-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_TEMPORAL_STABILITY_FACTOR = "0.0"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedTemporalStabilityFactor 0.0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-samples-per-quad-1-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT = "1"
        SE_SSR_FFX_SAMPLES_PER_QUAD = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedSamplesPerQuad 1 `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-samples-4-official-confidence-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT = "1"
        SE_SSR_FFX_SAMPLES_PER_QUAD = "4"
        SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedSamplesPerQuad 4 `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1 `
    -ExpectedCompositeConfidenceMode 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-stable-environment-fallback-off-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT = "1"
        SE_SSR_FFX_SAMPLES_PER_QUAD = "4"
        SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF = "1"
        SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK_OFF = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedSamplesPerQuad 4 `
    -ExpectedStableEnvironmentFallbackEnabled 0 `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1 `
    -ExpectedCompositeConfidenceMode 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-environment-lod0-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT = "1"
        SE_SSR_FFX_SAMPLES_PER_QUAD = "4"
        SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF = "1"
        SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK = "1"
        SE_SSR_FFX_ENVIRONMENT_LOD0 = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedSamplesPerQuad 4 `
    -ExpectedStableEnvironmentFallbackEnabled 1 `
    -ExpectedEnvironmentMipCount 1 `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1 `
    -ExpectedCompositeConfidenceMode 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-constant-environment-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT = "1"
        SE_SSR_FFX_SAMPLES_PER_QUAD = "4"
        SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF = "1"
        SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK = "1"
        SE_SSR_FFX_ENVIRONMENT_LOD0 = "1"
        SE_SSR_FFX_CONSTANT_ENVIRONMENT = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedSamplesPerQuad 4 `
    -ExpectedStableEnvironmentFallbackEnabled 1 `
    -ExpectedConstantEnvironmentFallbackEnabled 1 `
    -ExpectedEnvironmentMipCount 1 `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1 `
    -ExpectedCompositeConfidenceMode 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-perfect-reflection-directions-off-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT = "1"
        SE_SSR_FFX_SAMPLES_PER_QUAD = "4"
        SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF = "1"
        SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK = "1"
        SE_SSR_FFX_ENVIRONMENT_LOD0 = "1"
        SE_SSR_FFX_CONSTANT_ENVIRONMENT = "1"
        SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS_OFF = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedSamplesPerQuad 4 `
    -ExpectedStableEnvironmentFallbackEnabled 1 `
    -ExpectedConstantEnvironmentFallbackEnabled 1 `
    -ExpectedPerfectReflectionDirectionsEnabled 0 `
    -ExpectedEnvironmentMipCount 1 `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1 `
    -ExpectedCompositeConfidenceMode 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-prefilter-bypass-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT = "1"
        SE_SSR_FFX_SAMPLES_PER_QUAD = "4"
        SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF = "1"
        SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK = "1"
        SE_SSR_FFX_ENVIRONMENT_LOD0 = "1"
        SE_SSR_FFX_CONSTANT_ENVIRONMENT = "1"
        SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS = "1"
        SE_SSR_FFX_PREFILTER_BYPASS = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedSamplesPerQuad 4 `
    -ExpectedStableEnvironmentFallbackEnabled 1 `
    -ExpectedConstantEnvironmentFallbackEnabled 1 `
    -ExpectedPerfectReflectionDirectionsEnabled 1 `
    -ExpectedPrefilterBypassEnabled 1 `
    -ExpectedEnvironmentMipCount 1 `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1 `
    -ExpectedCompositeConfidenceMode 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-resolve-temporal-bypass-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT = "1"
        SE_SSR_FFX_SAMPLES_PER_QUAD = "4"
        SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF = "1"
        SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK = "1"
        SE_SSR_FFX_ENVIRONMENT_LOD0 = "1"
        SE_SSR_FFX_CONSTANT_ENVIRONMENT = "1"
        SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS = "1"
        SE_SSR_FFX_PREFILTER_BYPASS = "1"
        SE_SSR_FFX_RESOLVE_TEMPORAL_BYPASS = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedSamplesPerQuad 4 `
    -ExpectedStableEnvironmentFallbackEnabled 1 `
    -ExpectedConstantEnvironmentFallbackEnabled 1 `
    -ExpectedPerfectReflectionDirectionsEnabled 1 `
    -ExpectedPrefilterBypassEnabled 1 `
    -ExpectedResolveTemporalBypassEnabled 1 `
    -ExpectedEnvironmentMipCount 1 `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1 `
    -ExpectedCompositeConfidenceMode 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-classify-surface-seed-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT = "1"
        SE_SSR_FFX_SAMPLES_PER_QUAD = "4"
        SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF = "1"
        SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK = "1"
        SE_SSR_FFX_ENVIRONMENT_LOD0 = "1"
        SE_SSR_FFX_CONSTANT_ENVIRONMENT = "1"
        SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS = "1"
        SE_SSR_FFX_PREFILTER_BYPASS = "1"
        SE_SSR_FFX_RESOLVE_TEMPORAL_BYPASS = "1"
        SE_SSR_FFX_CLASSIFY_SURFACE_SEED = "1"
        SE_SSR_DEFERRED_REPROJECTION = "0"
        SE_SSR_FFX_TEMPORAL_STABILITY_FACTOR = "0.0"
        SE_SSR_FFX_HIT_REPROJECTION_OFF = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedHitReprojectionEnabled 0 `
    -ExpectedTemporalStabilityFactor 0.0 `
    -ExpectedSamplesPerQuad 4 `
    -ExpectedStableEnvironmentFallbackEnabled 1 `
    -ExpectedConstantEnvironmentFallbackEnabled 1 `
    -ExpectedPerfectReflectionDirectionsEnabled 1 `
    -ExpectedPrefilterBypassEnabled 1 `
    -ExpectedResolveTemporalBypassEnabled 1 `
    -ExpectedClassifySurfaceSeedEnabled 1 `
    -ExpectedEnvironmentMipCount 1 `
    -ExpectedDeferredReceiverReprojectionEnabled 0 `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1 `
    -ExpectedCompositeConfidenceMode 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-intersect-coverage-marker-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT = "1"
        SE_SSR_FFX_SAMPLES_PER_QUAD = "4"
        SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE_OFF = "1"
        SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK = "1"
        SE_SSR_FFX_ENVIRONMENT_LOD0 = "1"
        SE_SSR_FFX_CONSTANT_ENVIRONMENT = "1"
        SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS = "1"
        SE_SSR_FFX_PREFILTER_BYPASS = "1"
        SE_SSR_FFX_RESOLVE_TEMPORAL_BYPASS = "1"
        SE_SSR_FFX_CLASSIFY_SURFACE_SEED = "1"
        SE_SSR_FFX_INTERSECT_COVERAGE_MARKER = "1"
        SE_SSR_DEFERRED_REPROJECTION = "0"
        SE_SSR_FFX_TEMPORAL_STABILITY_FACTOR = "0.0"
        SE_SSR_FFX_HIT_REPROJECTION_OFF = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedHitReprojectionEnabled 0 `
    -ExpectedTemporalStabilityFactor 0.0 `
    -ExpectedSamplesPerQuad 4 `
    -ExpectedStableEnvironmentFallbackEnabled 1 `
    -ExpectedConstantEnvironmentFallbackEnabled 1 `
    -ExpectedPerfectReflectionDirectionsEnabled 1 `
    -ExpectedPrefilterBypassEnabled 1 `
    -ExpectedResolveTemporalBypassEnabled 1 `
    -ExpectedClassifySurfaceSeedEnabled 1 `
    -ExpectedIntersectCoverageMarkerEnabled 1 `
    -ExpectedEnvironmentMipCount 1 `
    -ExpectedDeferredReceiverReprojectionEnabled 0 `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1 `
    -ExpectedCompositeConfidenceMode 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-composite-off-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_COMPOSITE_OFF = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $false `
    -ExpectedTemporalStabilityFactor 0.95

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-deferred-reprojection-off-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_DEFERRED_REPROJECTION = "0"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedDeferredReceiverReprojectionEnabled 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-visible-output-clear-off-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT_OFF = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedVisibleOutputClearEnabled 0 `
    -ExpectedVisibleOutputClears 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-sample-variance-confidence-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
        SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE = "1"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true `
    -ExpectedVisibleOutputClearEnabled 1 `
    -ExpectedVisibleOutputClears 1 `
    -ExpectedCompositeConfidenceMode 1

$reports += Invoke-RuntimeLane `
    -Name "forward3d-fbx-ffx-backend-contract" `
    -Executable $forwardExecutable `
    -Environment ($common.Clone() + @{
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
        SE_SSR_BACKEND = "ffx-sssr"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0 `
    -ExpectDeferredComposite $true

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-internal-backend-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "selfengine"
    }) `
    -ExpectedRequestedProvider 0 `
    -ExpectedActiveProvider 0 `
    -ExpectedDispatchReady 0 `
    -ExpectedRuntimeActive 0 `
    -ExpectPrepareDispatch $false `
    -ExpectedFallbackReason 1 `
    -ExpectedVisibleOutputClearEnabled 0 `
    -ExpectedVisibleOutputClears 0

$passCount = ($reports | ForEach-Object { $_.passCount } | Measure-Object -Sum).Sum
$failCount = ($reports | ForEach-Object { $_.failCount } | Measure-Object -Sum).Sum
$summary = [pscustomobject]@{
    generatedAt = (Get-Date).ToString("o")
    outputDirectory = $OutputDirectory
    verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
    passCount = [int]$passCount
    failCount = [int]$failCount
    reports = $reports
}
$summary | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $OutputDirectory "summary.json") -Encoding utf8
$summary

if ($Strict -and $failCount -gt 0) {
    exit 1
}
