[CmdletBinding()]
param(
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$ShowcaseExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [switch]$SkipBuild,
    [switch]$UseShowcaseForwardControl,
    [switch]$UseForwardForShowcaseControl,
    [ValidateSet(2, 3, 4, 5, 6)]
    [uint32]$ExpectedRayQueryConsumerContractVersion = 6,
    [switch]$Strict,
    [string]$OutputDirectory = "tmp\hybrid_reflections_capability"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (![IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Resolve-ProjectPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $candidate = if ([IO.Path]::IsPathRooted($Path)) {
        $Path
    } else {
        Join-Path $projectRoot $Path
    }
    return (Resolve-Path $candidate).Path
}

function New-Check {
    param(
        [string]$Name,
        [bool]$Passed,
        [object]$Actual,
        [object]$Expected
    )

    return [pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }
}

function Get-UIntValue {
    param($Row, [string]$Name)

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV column: $Name"
    }
    return [uint32]$property.Value
}

function Get-UInt64Value {
    param($Row, [string]$Name)

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV column: $Name"
    }
    return [uint64]$property.Value
}

function Set-LaneEnvironment {
    param([hashtable]$Values, [string[]]$ManagedKeys)

    $previous = @{}
    foreach ($key in $ManagedKeys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, $null, "Process")
    }
    foreach ($entry in $Values.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable(
            $entry.Key,
            [string]$entry.Value,
            "Process"
        )
    }
    return $previous
}

function Restore-LaneEnvironment {
    param([hashtable]$Previous)

    foreach ($entry in $Previous.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable(
            $entry.Key,
            $entry.Value,
            "Process"
        )
    }
}

if (-not $SkipBuild) {
    $buildCommand = @(
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1',
        "cd /d `"$projectRoot\build`"",
        'MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo',
        'MSBuild SelfEngineLightingShowcase.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo'
    ) -join ' && '
    & cmd.exe /d /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Hybrid reflections capability build failed with exit code $LASTEXITCODE"
    }
}

$forwardExecutable = Resolve-ProjectPath $ForwardExecutablePath
$showcaseExecutable = Resolve-ProjectPath $ShowcaseExecutablePath
if ($UseShowcaseForwardControl) {
    $forwardExecutable = $showcaseExecutable
}
if ($UseForwardForShowcaseControl) {
    $showcaseExecutable = $forwardExecutable
}
$managedKeys = @(
    "SE_HYBRID_REFLECTIONS_RT",
    "SE_HYBRID_REFLECTIONS_RT_OFF",
    "SE_HYBRID_REFLECTIONS_RAY_QUERY_OFF",
    "SE_HYBRID_REFLECTIONS_HIT_ATTRIBUTES_OFF",
    "SE_HYBRID_REFLECTIONS_MATERIAL_TEXTURES_OFF",
    "SE_HYBRID_REFLECTIONS_HIT_LIGHTING_OFF",
    "SE_HYBRID_REFLECTIONS_SHADOW_VISIBILITY_OFF",
    "SE_HYBRID_REFLECTIONS_DNSR_INJECTION_OFF",
    "SE_HYBRID_REFLECTIONS_DIRECT_MIRROR_OFF",
    "SE_HYBRID_REFLECTIONS_SKINNED_BLAS_OFF",
    "SE_HYBRID_REFLECTIONS_DIAGNOSTICS",
    "SE_SSR",
    "SE_SSR_BACKEND",
    "SE_SSR_FFX",
    "SE_SSR_FFX_OFF",
    "SE_FORWARD3D_AA_MODE",
    "SE_DLSS_QUALITY",
    "SE_DLSS_PRESET",
    "SE_DLSS_PRESENT",
    "SE_RENDER_SCALE",
    "SE_UPSCALER_PLUGIN",
    "SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT",
    "SE_BENCHMARK_SCENE",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_LIGHTING_SHOWCASE_FORCE_OFF",
    "SE_SCENE_UPDATE_FREEZE",
    "SE_FBX_ANIMATION_FREEZE",
    "SE_VISUAL_QA_HIDE_IMGUI",
    "SE_WINDOW_HIDDEN",
    "SE_AUTO_EXIT_FRAMES",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_BENCHMARK_CSV"
)
$commonEnvironment = @{
    SE_WINDOW_HIDDEN = "1"
    SE_FORWARD3D_AA_MODE = "taa"
    SE_SSR = "1"
    SE_SSR_BACKEND = "ffx-sssr"
    SE_HYBRID_REFLECTIONS_DIAGNOSTICS = "1"
    SE_SCENE_UPDATE_FREEZE = "1"
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_BENCHMARK_WARMUP_FRAMES = "2"
    SE_BENCHMARK_FRAMES = "7"
    SE_AUTO_EXIT_FRAMES = "14"
}
$laneSpecs = @(
    [pscustomobject]@{
        name = "lighting-showcase-default-presentation"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        defaultPresentationProfile = "lighting-showcase"
        removeCommonKeys = @(
            "SE_FORWARD3D_AA_MODE",
            "SE_SSR",
            "SE_SSR_BACKEND",
            "SE_HYBRID_REFLECTIONS_RT"
        )
        environment = @{
            SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-requested"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
        }
    },
    [pscustomobject]@{
        name = "forward3d-fbx-requested"
        executable = $forwardExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_LIGHTING_SHOWCASE_FORCE_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-direct-mirror-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_DIRECT_MIRROR_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-consumer-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 1
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_RAY_QUERY_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-hit-attributes-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 1
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_HIT_ATTRIBUTES_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-material-textures-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 1
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_MATERIAL_TEXTURES_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-hit-lighting-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 1
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_HIT_LIGHTING_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-shadow-visibility-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 1
        denoiserInjectionDisabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_SHADOW_VISIBILITY_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-denoiser-injection-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 1
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_DNSR_INJECTION_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-rt-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 1
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_RT_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "forward3d-default-presentation-control"
        executable = $forwardExecutable
        requested = 0
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        defaultPresentationProfile = "forward3d-control"
        removeCommonKeys = @(
            "SE_FORWARD3D_AA_MODE",
            "SE_SSR",
            "SE_SSR_BACKEND",
            "SE_HYBRID_REFLECTIONS_RT"
        )
        environment = @{
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_LIGHTING_SHOWCASE_FORCE_OFF = "1"
            SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT = "1"
        }
    },
    [pscustomobject]@{
        name = "forward3d-not-requested-control"
        executable = $forwardExecutable
        requested = 0
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        materialTexturesDisabled = 0
        hitLightingDisabled = 0
        shadowVisibilityDisabled = 0
        denoiserInjectionDisabled = 0
        environment = @{
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_LIGHTING_SHOWCASE_FORCE_OFF = "1"
        }
    }
)
if ($UseForwardForShowcaseControl) {
    foreach ($lane in $laneSpecs) {
        if ($lane.name -like "lighting-showcase-*") {
            $lane.environment["SE_BENCHMARK_SCENE"] = "lighting-showcase"
        }
    }
}

$reports = [Collections.Generic.List[object]]::new()
foreach ($lane in $laneSpecs) {
    $laneDirectory = Join-Path $OutputDirectory $lane.name
    New-Item -ItemType Directory -Force -Path $laneDirectory | Out-Null
    $csvPath = Join-Path $laneDirectory "hybrid_reflections_capability.csv"
    $stdoutPath = Join-Path $laneDirectory "process.stdout.log"
    $stderrPath = Join-Path $laneDirectory "process.stderr.log"
    Remove-Item -LiteralPath $csvPath, $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    $environment = $commonEnvironment.Clone()
    if ($lane.PSObject.Properties.Name -contains "removeCommonKeys") {
        foreach ($key in $lane.removeCommonKeys) {
            $environment.Remove($key)
        }
    }
    foreach ($entry in $lane.environment.GetEnumerator()) {
        $environment[$entry.Key] = $entry.Value
    }
    $environment["SE_BENCHMARK_CSV"] = $csvPath
    $previous = Set-LaneEnvironment -Values $environment -ManagedKeys $managedKeys
    try {
        $executableDirectory = Split-Path -Parent $lane.executable
        $commandLine =
            "cd /d `"$executableDirectory`" && `"$($lane.executable)`" 1> `"$stdoutPath`" 2> `"$stderrPath`""
        & cmd.exe /d /c $commandLine
        $exitCode = $LASTEXITCODE
    } finally {
        Restore-LaneEnvironment -Previous $previous
    }

    $checks = [Collections.Generic.List[object]]::new()
    $checks.Add((New-Check "$($lane.name) process exits" `
        ($exitCode -eq 0) $exitCode 0)) | Out-Null
    $checks.Add((New-Check "$($lane.name) writes CSV" `
        (Test-Path -LiteralPath $csvPath) (Test-Path -LiteralPath $csvPath) $true)) | Out-Null

    $metrics = $null
    if ($exitCode -eq 0 -and (Test-Path -LiteralPath $csvPath)) {
        $rows = @(Import-Csv -LiteralPath $csvPath)
        $checks.Add((New-Check "$($lane.name) captures rows" `
            ($rows.Count -eq 7) $rows.Count 7)) | Out-Null
        if ($rows.Count -gt 0) {
            $last = $rows[-1]
            $contract = Get-UIntValue $last "hybrid_reflections_capability_contract_version"
            $requested = Get-UIntValue $last "hybrid_reflections_requested"
            $disabled = Get-UIntValue $last "hybrid_reflections_control_disabled"
            $rayQueryConsumerContract = Get-UIntValue $last "hybrid_reflections_ray_query_consumer_contract_version"
            $rayQueryHitAttributeContract = Get-UIntValue $last "hybrid_reflections_ray_query_hit_attribute_contract_version"
            $rayQueryMaterialTableContract = Get-UIntValue $last "hybrid_reflections_ray_query_material_table_contract_version"
            $rayQueryHitLightingContract = Get-UIntValue $last "hybrid_reflections_ray_query_hit_lighting_contract_version"
            $rayQueryShadowVisibilityContract = Get-UIntValue $last "hybrid_reflections_ray_query_shadow_visibility_contract_version"
            $rayQueryDenoiserBridgeContract = Get-UIntValue $last "hybrid_reflections_ray_query_denoiser_bridge_contract_version"
            $rayQueryConsumerRequested = Get-UIntValue $last "hybrid_reflections_ray_query_consumer_requested"
            $rayQueryConsumerDisabled = Get-UIntValue $last "hybrid_reflections_ray_query_consumer_control_disabled"
            $rayQueryHitAttributeDisabled = Get-UIntValue $last "hybrid_reflections_ray_query_hit_attribute_control_disabled"
            $rayQueryMaterialTexturesDisabled = Get-UIntValue $last "hybrid_reflections_ray_query_material_texture_control_disabled"
            $rayQueryHitLightingDisabled = Get-UIntValue $last "hybrid_reflections_ray_query_hit_lighting_control_disabled"
            $rayQueryShadowVisibilityDisabled = Get-UIntValue $last "hybrid_reflections_ray_query_shadow_visibility_control_disabled"
            $rayQueryDenoiserInjectionDisabled = Get-UIntValue $last "hybrid_reflections_ray_query_denoiser_injection_control_disabled"
            $bdaExtension = Get-UIntValue $last "hybrid_reflections_buffer_device_address_extension_supported"
            $deferredExtension = Get-UIntValue $last "hybrid_reflections_deferred_host_operations_extension_supported"
            $asExtension = Get-UIntValue $last "hybrid_reflections_acceleration_structure_extension_supported"
            $rayQueryExtension = Get-UIntValue $last "hybrid_reflections_ray_query_extension_supported"
            $bdaFeature = Get-UIntValue $last "hybrid_reflections_buffer_device_address_feature_supported"
            $shaderInt64Feature = Get-UIntValue $last "hybrid_reflections_shader_int64_feature_supported"
            $nonUniformSampledImageFeature = Get-UIntValue $last "hybrid_reflections_sampled_image_array_non_uniform_indexing_feature_supported"
            $asFeature = Get-UIntValue $last "hybrid_reflections_acceleration_structure_feature_supported"
            $rayQueryFeature = Get-UIntValue $last "hybrid_reflections_ray_query_feature_supported"
            $hardwareReady = Get-UIntValue $last "hybrid_reflections_ray_query_hardware_ready"
            $shaderInt64Enabled = Get-UIntValue $last "hybrid_reflections_shader_int64_device_enabled"
            $nonUniformSampledImageEnabled = Get-UIntValue $last "hybrid_reflections_sampled_image_array_non_uniform_indexing_device_enabled"
            $deviceEnabled = Get-UIntValue $last "hybrid_reflections_ray_query_device_enabled"
            $accelerationStructureContract = Get-UIntValue $last "hybrid_reflections_acceleration_structure_contract_version"
            $fullSceneCommands = Get-UIntValue $last "hybrid_reflections_full_scene_command_count"
            $opaqueRigidCommands = Get-UIntValue $last "hybrid_reflections_opaque_rigid_command_count"
            $skinnedFallback = Get-UIntValue $last "hybrid_reflections_skinned_fallback_count"
            $skinnedBlasControlDisabled = Get-UIntValue $last "hybrid_reflections_skinned_blas_control_disabled"
            $skinnedCandidateCount = Get-UIntValue $last "hybrid_reflections_skinned_candidate_count"
            $skinnedEligibleCount = Get-UIntValue $last "hybrid_reflections_skinned_eligible_count"
            $skinnedTlasInstanceCount = Get-UIntValue $last "hybrid_reflections_skinned_tlas_instance_count"
            $skinnedDynamicBlasCount = Get-UIntValue $last "hybrid_reflections_skinned_dynamic_blas_count"
            $skinnedDynamicBlasUpdateCount = Get-UIntValue $last "hybrid_reflections_skinned_dynamic_blas_update_count"
            $skinnedSkinningDispatchCount = Get-UIntValue $last "hybrid_reflections_skinned_skinning_dispatch_count"
            $skinnedPoseRevision = Get-UInt64Value $last "hybrid_reflections_skinned_pose_revision_min"
            $skinnedOutputRevision = Get-UInt64Value $last "hybrid_reflections_skinned_output_revision_min"
            $skinnedBlasRevision = Get-UInt64Value $last "hybrid_reflections_skinned_blas_revision_min"
            $skinnedRevisionMismatch = Get-UIntValue $last "hybrid_reflections_skinned_pose_blas_revision_mismatch_count"
            $skinnedInvalidPalette = Get-UIntValue $last "hybrid_reflections_skinned_invalid_palette_count"
            $skinnedReadbackValid = Get-UIntValue $last "hybrid_reflections_skinned_skinning_readback_valid"
            $skinnedReadbackVertexCount = Get-UIntValue $last "hybrid_reflections_skinned_skinning_readback_vertex_count"
            $skinnedReadbackSkinnedVertexCount = Get-UIntValue $last "hybrid_reflections_skinned_skinning_readback_skinned_vertex_count"
            $skinnedReadbackInvalidBoneIndexCount = Get-UIntValue $last "hybrid_reflections_skinned_skinning_readback_invalid_bone_index_count"
            $skinnedReadbackNonFiniteVertexCount = Get-UIntValue $last "hybrid_reflections_skinned_skinning_readback_non_finite_vertex_count"
            $alphaFallback = Get-UIntValue $last "hybrid_reflections_alpha_fallback_count"
            $invalidGeometry = Get-UIntValue $last "hybrid_reflections_invalid_geometry_count"
            $instanceOverflow = Get-UIntValue $last "hybrid_reflections_instance_overflow_count"
            $blasCacheCount = Get-UIntValue $last "hybrid_reflections_blas_cache_count"
            $blasReadyCount = Get-UIntValue $last "hybrid_reflections_blas_ready_count"
            $tlasInstanceCount = Get-UIntValue $last "hybrid_reflections_tlas_instance_count"
            $tlasInstanceCapacity = Get-UIntValue $last "hybrid_reflections_tlas_instance_capacity"
            $tlasAddressReady = Get-UIntValue $last "hybrid_reflections_tlas_address_ready"
            $accelerationStructureResourcesReady = Get-UIntValue $last "hybrid_reflections_acceleration_structure_resources_ready"
            $runtimeReady = Get-UIntValue $last "hybrid_reflections_runtime_resources_ready"
            $rayQueryResourcesReady = Get-UIntValue $last "hybrid_reflections_ray_query_resources_ready"
            $rayQueryTlasDescriptorReady = Get-UIntValue $last "hybrid_reflections_ray_query_tlas_descriptor_ready"
            $rayQueryDispatchReady = Get-UIntValue $last "hybrid_reflections_ray_query_dispatch_ready"
            $rayQueryDispatchCount = Get-UIntValue $last "hybrid_reflections_ray_query_dispatch_count"
            $rayQueryDescriptorBindCount = Get-UIntValue $last "hybrid_reflections_ray_query_descriptor_bind_count"
            $rayQueryResultClearCount = Get-UIntValue $last "hybrid_reflections_ray_query_result_clear_count"
            $rayQueryResultWidth = Get-UIntValue $last "hybrid_reflections_ray_query_result_width"
            $rayQueryResultHeight = Get-UIntValue $last "hybrid_reflections_ray_query_result_height"
            $rayQueryResultFormat = Get-UIntValue $last "hybrid_reflections_ray_query_result_format"
            $rayQueryMemoryBytes = Get-UInt64Value $last "hybrid_reflections_ray_query_memory_bytes"
            $rayQueryMetadataResourcesReady = Get-UIntValue $last "hybrid_reflections_ray_query_instance_metadata_resources_ready"
            $rayQueryMetadataCount = Get-UIntValue $last "hybrid_reflections_ray_query_instance_metadata_count"
            $rayQueryMetadataCapacity = Get-UIntValue $last "hybrid_reflections_ray_query_instance_metadata_capacity"
            $rayQueryMaterialCount = Get-UIntValue $last "hybrid_reflections_ray_query_instance_material_count"
            $rayQueryAddressReadyCount = Get-UIntValue $last "hybrid_reflections_ray_query_instance_address_ready_count"
            $rayQueryMetadataUploadCount = Get-UIntValue $last "hybrid_reflections_ray_query_instance_metadata_upload_count"
            $rayQueryMetadataBytes = Get-UInt64Value $last "hybrid_reflections_ray_query_instance_metadata_bytes"
            $materialTableResourcesReady = Get-UIntValue $last "hybrid_reflections_ray_query_material_table_resources_ready"
            $materialTableCount = Get-UIntValue $last "hybrid_reflections_ray_query_material_table_count"
            $materialTableCapacity = Get-UIntValue $last "hybrid_reflections_ray_query_material_table_capacity"
            $materialTableOverflow = Get-UIntValue $last "hybrid_reflections_ray_query_material_table_overflow_count"
            $materialBufferReady = Get-UIntValue $last "hybrid_reflections_ray_query_material_buffer_ready"
            $materialBufferUploadCount = Get-UIntValue $last "hybrid_reflections_ray_query_material_buffer_upload_count"
            $materialBufferBytes = Get-UInt64Value $last "hybrid_reflections_ray_query_material_buffer_bytes"
            $textureDescriptorCount = Get-UIntValue $last "hybrid_reflections_ray_query_texture_descriptor_count"
            $textureDescriptorCapacity = Get-UIntValue $last "hybrid_reflections_ray_query_texture_descriptor_capacity"
            $samplerDescriptorCount = Get-UIntValue $last "hybrid_reflections_ray_query_sampler_descriptor_count"
            $samplerDescriptorCapacity = Get-UIntValue $last "hybrid_reflections_ray_query_sampler_descriptor_capacity"
            $distinctTextureCount = Get-UIntValue $last "hybrid_reflections_ray_query_distinct_texture_count"
            $distinctSamplerCount = Get-UIntValue $last "hybrid_reflections_ray_query_distinct_sampler_count"
            $duplicateTextureCount = Get-UIntValue $last "hybrid_reflections_ray_query_duplicate_texture_count"
            $duplicateSamplerCount = Get-UIntValue $last "hybrid_reflections_ray_query_duplicate_sampler_count"
            $fallbackDescriptorCount = Get-UIntValue $last "hybrid_reflections_ray_query_fallback_descriptor_count"
            $hitSurfaceWidth = Get-UIntValue $last "hybrid_reflections_ray_query_hit_surface_width"
            $hitSurfaceHeight = Get-UIntValue $last "hybrid_reflections_ray_query_hit_surface_height"
            $hitSurfaceFormat = Get-UIntValue $last "hybrid_reflections_ray_query_hit_surface_format"
            $hitLightingResourcesReady = Get-UIntValue $last "hybrid_reflections_ray_query_hit_lighting_resources_ready"
            $lightBufferDescriptorReady = Get-UIntValue $last "hybrid_reflections_ray_query_light_buffer_descriptor_ready"
            $iblBrdfDescriptorReady = Get-UIntValue $last "hybrid_reflections_ray_query_ibl_brdf_descriptor_ready"
            $iblIrradianceDescriptorReady = Get-UIntValue $last "hybrid_reflections_ray_query_ibl_irradiance_descriptor_ready"
            $iblPrefilteredDescriptorReady = Get-UIntValue $last "hybrid_reflections_ray_query_ibl_prefiltered_descriptor_ready"
            $iblSamplerDescriptorReady = Get-UIntValue $last "hybrid_reflections_ray_query_ibl_sampler_descriptor_ready"
            $iblPrefilteredMipCount = Get-UIntValue $last "hybrid_reflections_ray_query_ibl_prefiltered_mip_count"
            $localProbeIblContractVersion = Get-UIntValue $last `
                "hybrid_reflections_ray_query_local_probe_ibl_contract_version"
            $localProbeIblResourcesReady = Get-UIntValue $last `
                "hybrid_reflections_ray_query_local_probe_ibl_resources_ready"
            $localProbeIblEnabled = Get-UIntValue $last `
                "hybrid_reflections_ray_query_local_probe_ibl_enabled"
            $localProbeCount = Get-UIntValue $last `
                "hybrid_reflections_ray_query_local_probe_count"
            $localProbePrefilteredReadyMask = Get-UIntValue $last `
                "hybrid_reflections_ray_query_local_probe_prefiltered_ready_mask"
            $localProbeDiffuseReadyMask = Get-UIntValue $last `
                "hybrid_reflections_ray_query_local_probe_diffuse_ready_mask"
            $localProbeDescriptorWriteCount = Get-UIntValue $last `
                "hybrid_reflections_ray_query_local_probe_descriptor_write_count"
            $sourceFusionEnabled = Get-UIntValue $last `
                "hybrid_reflections_ray_query_source_fusion_enabled"
            $directMirrorEnabled = Get-UIntValue $last `
                "hybrid_reflections_ray_query_direct_mirror_enabled"
            $screenHitConfidenceThreshold = Get-UIntValue $last `
                "hybrid_reflections_ray_query_screen_hit_confidence_threshold_permille"
            $confidenceSpatialFilterEnabled = Get-UIntValue $last `
                "ssr_ffx_sssr_confidence_spatial_filter_enabled"
            $directionalLightCount = Get-UIntValue $last "hybrid_reflections_ray_query_directional_light_count"
            $localLightCount = Get-UIntValue $last "hybrid_reflections_ray_query_local_light_count"
            $hitLightingVisibilityMode = Get-UIntValue $last "hybrid_reflections_ray_query_hit_lighting_visibility_mode"
            $hitLightingVisibilityFallback = Get-UIntValue $last "hybrid_reflections_ray_query_hit_lighting_visibility_fallback_reason"
            $shadowVisibilityResourcesReady = Get-UIntValue $last "hybrid_reflections_ray_query_shadow_visibility_resources_ready"
            $shadowMaxLocalLightCount = Get-UIntValue $last "hybrid_reflections_ray_query_shadow_max_local_light_count"
            $shadowRectangleSampleCount = Get-UIntValue $last "hybrid_reflections_ray_query_shadow_rectangle_sample_count"
            $shadowMaxRaysPerHit = Get-UIntValue $last "hybrid_reflections_ray_query_shadow_max_rays_per_hit"
            $denoiserResourcesReady = Get-UIntValue $last "hybrid_reflections_ray_query_denoiser_resources_ready"
            $denoiserRadianceDescriptorReady = Get-UIntValue $last "hybrid_reflections_ray_query_denoiser_radiance_descriptor_ready"
            $denoiserConfidenceDescriptorReady = Get-UIntValue $last "hybrid_reflections_ray_query_denoiser_confidence_descriptor_ready"
            $denoiserInjectionEnabled = Get-UIntValue $last "hybrid_reflections_ray_query_denoiser_injection_enabled"
            $ffxReprojectDispatches = Get-UIntValue $last "ssr_ffx_sssr_reproject_dispatches"
            $ffxPrefilterDispatches = Get-UIntValue $last "ssr_ffx_sssr_prefilter_dispatches"
            $ffxResolveDispatches = Get-UIntValue $last "ssr_ffx_sssr_resolve_temporal_dispatches"
            $ffxSameFrameActive = Get-UIntValue $last "ssr_ffx_sssr_same_frame_composite_active"
            $ffxApplyDraws = Get-UIntValue $last "ssr_ffx_sssr_same_frame_composite_apply_draws"
            $ffxHitConfidenceApplyBound = Get-UIntValue $last "ssr_ffx_sssr_hit_confidence_apply_bound"
            $ffxMirrorDnsrPassthroughRequested = Get-UIntValue $last `
                "ssr_ffx_sssr_mirror_dnsr_passthrough_requested"
            $ffxMirrorDnsrPassthroughResourcesReady = Get-UIntValue $last `
                "ssr_ffx_sssr_mirror_dnsr_passthrough_resources_ready"
            $ffxMirrorDnsrPassthroughActive = Get-UIntValue $last `
                "ssr_ffx_sssr_mirror_dnsr_passthrough_active"
            $ffxMirrorDnsrRoughnessThreshold = Get-UIntValue $last `
                "ssr_ffx_sssr_mirror_dnsr_roughness_threshold_milliunits"
            $ffxMirrorDnsrConfidenceThreshold = Get-UIntValue $last `
                "ssr_ffx_sssr_mirror_dnsr_confidence_threshold_permille"
            $ssrEnabled = Get-UIntValue $last "ssr_enabled"
            $ssrBackendRequestedProvider = Get-UIntValue $last `
                "ssr_backend_requested_provider"
            $ssrBackendActiveProvider = Get-UIntValue $last `
                "ssr_backend_active_provider"
            $antialiasingMode = Get-UIntValue $last "temporal_antialiasing_mode"
            $dlssOutputReady = Get-UIntValue $last `
                "temporal_upscaler_dlss_output_ready"
            $temporalPostSourceRequested = Get-UIntValue $last `
                "temporal_upscale_post_source_requested"
            $temporalPostSourceActive = Get-UIntValue $last `
                "temporal_upscale_post_source_active"
            $temporalPostSourceFallback = Get-UIntValue $last `
                "temporal_upscale_post_source_fallback_reason"
            $active = Get-UIntValue $last "hybrid_reflections_active"
            $fallback = Get-UIntValue $last "hybrid_reflections_fallback_reason"
            $rayQueryReadbackRows = @(
                $rows | Where-Object {
                    (Get-UIntValue $_ "hybrid_reflections_ray_query_readback_valid") -eq 1
                }
            )
            $rayQueryReadback = if ($rayQueryReadbackRows.Count -gt 0) {
                $rayQueryReadbackRows[-1]
            } else {
                $last
            }
            $rayQueryReadbackValid = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_readback_valid"
            $rayQueryCandidateCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_candidate_ray_count"
            $rayQueryScreenAcceptedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_screen_hit_accepted_count"
            $rayQueryTraceCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_trace_count"
            $rayQueryCommittedHitCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_committed_hit_count"
            $rayQueryMissCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_miss_count"
            $rayQueryInvalidCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_invalid_ray_count"
            $rayQueryDistanceMin = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_distance_min_millimeters"
            $rayQueryDistanceMax = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_distance_max_millimeters"
            $rayQueryResultWriteCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_result_pixel_write_count"
            $hitAttributeResolvedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_resolved_count"
            $hitAttributeInvalidInstanceCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_invalid_instance_count"
            $hitAttributeInvalidPrimitiveCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_invalid_primitive_count"
            $hitAttributeInvalidVertexCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_invalid_vertex_count"
            $hitAttributeInvalidBarycentricCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_invalid_barycentric_count"
            $hitAttributeInvalidValueCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_invalid_value_count"
            $hitAttributeMaterialResolvedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_material_resolved_count"
            $hitAttributeMaterialFallbackCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_material_fallback_count"
            $hitAttributePositionMismatchCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_position_mismatch_count"
            $hitAttributePositionErrorMax = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_position_error_max_micrometers"
            $hitAttributeNormalLengthMin = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_normal_length_min_permille"
            $hitAttributeNormalLengthMax = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_normal_length_max_permille"
            $hitAttributeBarycentricSumMin = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_barycentric_sum_min_permille"
            $hitAttributeBarycentricSumMax = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_barycentric_sum_max_permille"
            $hitAttributeIdentityChecksum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_identity_checksum"
            $hitAttributePrimitiveChecksum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_primitive_checksum"
            $hitAttributeMaterialChecksum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_attribute_material_checksum"
            $materialRecordResolvedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_material_record_resolved_count"
            $materialRecordFallbackCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_material_record_fallback_count"
            $textureSampleResolvedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_texture_sample_resolved_count"
            $textureSampleFallbackCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_texture_sample_fallback_count"
            $textureSampleInvalidCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_texture_sample_invalid_count"
            $finiteSampledColorCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_finite_sampled_color_count"
            $sampleLodMin = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_sample_lod_min_millilevels"
            $sampleLodMax = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_sample_lod_max_millilevels"
            $hitSurfacePayloadWriteCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_surface_payload_write_count"
            $hitSurfacePayloadChecksum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_surface_payload_checksum"
            $hitSurfaceLuminanceMin = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_surface_luminance_min_milliunits"
            $hitSurfaceLuminanceMax = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_surface_luminance_max_milliunits"
            $hitLightingResolvedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_lighting_resolved_count"
            $hitLightingInvalidCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_hit_lighting_invalid_count"
            $directionalLightEvaluationCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_directional_light_evaluation_count"
            $directionalLightContributionCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_directional_light_contribution_count"
            $pointLightEvaluationCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_point_light_evaluation_count"
            $pointLightContributionCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_point_light_contribution_count"
            $spotLightEvaluationCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_spot_light_evaluation_count"
            $spotLightContributionCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_spot_light_contribution_count"
            $rectLightEvaluationCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_rect_light_evaluation_count"
            $rectLightContributionCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_rect_light_contribution_count"
            $finiteDirectRadianceCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_finite_direct_radiance_count"
            $finiteIblRadianceCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_finite_ibl_radiance_count"
            $localProbeIblResolvedCount = Get-UIntValue $rayQueryReadback `
                "hybrid_reflections_ray_query_local_probe_ibl_resolved_count"
            $globalIblFallbackCount = Get-UIntValue $rayQueryReadback `
                "hybrid_reflections_ray_query_global_ibl_fallback_count"
            $localProbeIblInvalidCount = Get-UIntValue $rayQueryReadback `
                "hybrid_reflections_ray_query_local_probe_ibl_invalid_count"
            $localProbeIblLuminanceSum = Get-UIntValue $rayQueryReadback `
                "hybrid_reflections_ray_query_local_probe_ibl_luminance_sum_milliunits"
            $sourceFusionCount = Get-UIntValue $rayQueryReadback `
                "hybrid_reflections_ray_query_source_fusion_count"
            $sourceFusionConfidenceSum = Get-UIntValue $rayQueryReadback `
                "hybrid_reflections_ray_query_source_fusion_confidence_sum_permille"
            $sourceFusionScreenWeightSum = Get-UIntValue $rayQueryReadback `
                "hybrid_reflections_ray_query_source_fusion_screen_weight_sum_permille"
            $directMirrorCandidateCount = Get-UIntValue $rayQueryReadback `
                "hybrid_reflections_ray_query_direct_mirror_candidate_count"
            $directMirrorHitCount = Get-UIntValue $rayQueryReadback `
                "hybrid_reflections_ray_query_direct_mirror_hit_count"
            $directMirrorFallbackCount = Get-UIntValue $rayQueryReadback `
                "hybrid_reflections_ray_query_direct_mirror_fallback_count"
            $finiteEmissiveRadianceCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_finite_emissive_radiance_count"
            $finiteRadianceCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_finite_radiance_count"
            $directLuminanceSum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_direct_luminance_sum_milliunits"
            $iblLuminanceSum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_ibl_luminance_sum_milliunits"
            $emissiveLuminanceSum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_emissive_luminance_sum_milliunits"
            $radianceLuminanceMin = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_radiance_luminance_min_milliunits"
            $radianceLuminanceMax = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_radiance_luminance_max_milliunits"
            $radianceChecksum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_radiance_checksum"
            $shadowVisibilityResolvedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_shadow_visibility_resolved_count"
            $shadowRayCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_shadow_ray_count"
            $shadowVisibleCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_shadow_visible_count"
            $shadowOccludedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_shadow_occluded_count"
            $shadowInvalidCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_shadow_invalid_count"
            $directionalShadowRayCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_directional_shadow_ray_count"
            $pointShadowRayCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_point_shadow_ray_count"
            $spotShadowRayCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_spot_shadow_ray_count"
            $rectShadowRayCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_rect_shadow_ray_count"
            $localShadowCandidateCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_local_shadow_candidate_count"
            $localShadowSelectedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_local_shadow_selected_count"
            $localShadowDroppedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_local_shadow_dropped_count"
            $unshadowedDirectLuminanceSum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_unshadowed_direct_luminance_sum_milliunits"
            $visibleDirectLuminanceSum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_visible_direct_luminance_sum_milliunits"
            $shadowSelfIntersectionCandidateCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_shadow_self_intersection_candidate_count"
            $shadowHitDistanceMin = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_shadow_hit_distance_min_millimeters"
            $shadowHitDistanceMax = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_shadow_hit_distance_max_millimeters"
            $shadowVisibilityMin = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_shadow_visibility_min_permille"
            $shadowVisibilityMax = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_shadow_visibility_max_permille"
            $localShadowDroppedLuminanceSum = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_local_shadow_dropped_luminance_sum_milliunits"
            $denoiserInjectionResolvedCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_denoiser_injection_resolved_count"
            $denoiserRadiancePixelWriteCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_denoiser_radiance_pixel_write_count"
            $denoiserConfidencePixelWriteCount = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_denoiser_confidence_pixel_write_count"
            $denoiserConfidenceSumPermille = Get-UIntValue $rayQueryReadback "hybrid_reflections_ray_query_denoiser_confidence_sum_permille"
            $invalidHitAttributeCount =
                [uint64]$hitAttributeInvalidInstanceCount +
                [uint64]$hitAttributeInvalidPrimitiveCount +
                [uint64]$hitAttributeInvalidVertexCount +
                [uint64]$hitAttributeInvalidBarycentricCount +
                [uint64]$hitAttributeInvalidValueCount
            $hitLightingDiagnosticsSuppressed = @(@(
                $hitLightingResolvedCount,
                $hitLightingInvalidCount,
                $directionalLightEvaluationCount,
                $directionalLightContributionCount,
                $pointLightEvaluationCount,
                $pointLightContributionCount,
                $spotLightEvaluationCount,
                $spotLightContributionCount,
                $rectLightEvaluationCount,
                $rectLightContributionCount,
                $finiteDirectRadianceCount,
                $finiteIblRadianceCount,
                $finiteEmissiveRadianceCount,
                $finiteRadianceCount,
                $directLuminanceSum,
                $iblLuminanceSum,
                $emissiveLuminanceSum,
                $radianceLuminanceMin,
                $radianceLuminanceMax,
                $radianceChecksum,
                $hitSurfacePayloadWriteCount,
                $hitSurfacePayloadChecksum,
                $hitSurfaceLuminanceMin,
                $hitSurfaceLuminanceMax
            ) | Where-Object { $_ -ne 0 })
            $hitLightingDiagnosticsSuppressed =
                $hitLightingDiagnosticsSuppressed.Count -eq 0
            $shadowVisibilityDiagnosticsSuppressed = @(@(
                $shadowVisibilityResolvedCount,
                $shadowRayCount,
                $shadowVisibleCount,
                $shadowOccludedCount,
                $shadowInvalidCount,
                $directionalShadowRayCount,
                $pointShadowRayCount,
                $spotShadowRayCount,
                $rectShadowRayCount,
                $localShadowCandidateCount,
                $localShadowSelectedCount,
                $localShadowDroppedCount,
                $unshadowedDirectLuminanceSum,
                $visibleDirectLuminanceSum,
                $shadowSelfIntersectionCandidateCount,
                $shadowHitDistanceMin,
                $shadowHitDistanceMax,
                $shadowVisibilityMin,
                $shadowVisibilityMax,
                $localShadowDroppedLuminanceSum
            ) | Where-Object { $_ -ne 0 })
            $shadowVisibilityDiagnosticsSuppressed =
                $shadowVisibilityDiagnosticsSuppressed.Count -eq 0
            $denoiserDiagnosticsSuppressed = @(@(
                $denoiserInjectionResolvedCount,
                $denoiserRadiancePixelWriteCount,
                $denoiserConfidencePixelWriteCount,
                $denoiserConfidenceSumPermille
            ) | Where-Object { $_ -ne 0 }).Count -eq 0
            $maxBlasBuildCount = [uint32](($rows | ForEach-Object {
                    Get-UIntValue $_ "hybrid_reflections_blas_build_count"
                } | Measure-Object -Maximum).Maximum)
            $maxTlasBuildCount = [uint32](($rows | ForEach-Object {
                    Get-UIntValue $_ "hybrid_reflections_tlas_build_count"
                } | Measure-Object -Maximum).Maximum)
            $maxTlasUpdateCount = [uint32](($rows | ForEach-Object {
                    Get-UIntValue $_ "hybrid_reflections_tlas_update_count"
                } | Measure-Object -Maximum).Maximum)
            $maxAsResourceReady = [uint32](($rows | ForEach-Object {
                    Get-UIntValue $_ "hybrid_reflections_acceleration_structure_resources_ready"
                } | Measure-Object -Maximum).Maximum)
            $maxSkinnedFallback = [uint32](($rows | ForEach-Object {
                    Get-UIntValue $_ "hybrid_reflections_skinned_fallback_count"
                } | Measure-Object -Maximum).Maximum)
            $extensionReady =
                $bdaExtension -eq 1 -and $deferredExtension -eq 1 -and
                $asExtension -eq 1 -and $rayQueryExtension -eq 1
            $featureReady =
                $bdaFeature -eq 1 -and $shaderInt64Feature -eq 1 -and
                $nonUniformSampledImageFeature -eq 1 -and
                $asFeature -eq 1 -and
                $rayQueryFeature -eq 1
            $expectedDeviceEnabled =
                $lane.requested -eq 1 -and $lane.disabled -eq 0 -and
                $hardwareReady -eq 1
            $runtimeResourcesExpected =
                $lane.requested -eq 1 -and
                $lane.disabled -eq 0 -and
                $hardwareReady -eq 1
            $expectedAccelerationStructureContract = if ($runtimeResourcesExpected) {
                3
            } else {
                0
            }
            $rayQueryDispatchExpected =
                $runtimeResourcesExpected -and
                $lane.consumerDisabled -eq 0
            $hitAttributeResolutionExpected =
                $rayQueryDispatchExpected -and
                $lane.hitAttributesDisabled -eq 0
            $materialTextureResolutionExpected =
                $hitAttributeResolutionExpected -and
                $lane.materialTexturesDisabled -eq 0
            $hitLightingResolutionExpected =
                $materialTextureResolutionExpected -and
                $lane.hitLightingDisabled -eq 0
            $shadowVisibilityResolutionExpected =
                $hitLightingResolutionExpected -and
                $lane.shadowVisibilityDisabled -eq 0
            $denoiserInjectionExpected =
                $shadowVisibilityResolutionExpected -and
                $lane.denoiserInjectionDisabled -eq 0
            $directMirrorExpected =
                -not $lane.environment.ContainsKey(
                    "SE_HYBRID_REFLECTIONS_DIRECT_MIRROR_OFF"
                )
            $expectedRayQueryConsumerContract = if ($runtimeResourcesExpected) {
                $ExpectedRayQueryConsumerContractVersion
            } else {
                0
            }
            $expectedHitAttributeContract = if ($runtimeResourcesExpected) {
                1
            } else {
                0
            }
            $expectedMaterialTableContract = if ($runtimeResourcesExpected) {
                1
            } else {
                0
            }
            $expectedHitLightingContract = if ($runtimeResourcesExpected) {
                1
            } else {
                0
            }
            $expectedShadowVisibilityContract = if ($runtimeResourcesExpected) {
                1
            } else {
                0
            }
            $expectedDenoiserBridgeContract = if ($runtimeResourcesExpected) {
                1
            } else {
                0
            }
            $fallbackMatches = if ($lane.requested -eq 0) {
                $fallback -eq 1
            } elseif ($lane.disabled -eq 1) {
                $fallback -eq 2
            } elseif ($rayQueryDispatchExpected) {
                if ($denoiserInjectionExpected) {
                    $fallback -eq 0
                } else {
                    $fallback -eq 8
                }
            } elseif ($runtimeResourcesExpected) {
                $fallback -eq 7
            } else {
                $fallback -in @(3, 4, 5)
            }
            $defaultPresentationProfile = if (
                $lane.PSObject.Properties.Name -contains
                    "defaultPresentationProfile"
            ) {
                [string]$lane.defaultPresentationProfile
            } else {
                ""
            }

            if ($defaultPresentationProfile -eq "lighting-showcase") {
                $showcasePresentationDefaultsActive =
                    $antialiasingMode -eq 5 -and
                    $dlssOutputReady -eq 1 -and
                    $temporalPostSourceRequested -eq 1 -and
                    $temporalPostSourceActive -eq 1 -and
                    $temporalPostSourceFallback -eq 0 -and
                    $ssrEnabled -eq 1 -and
                    $ssrBackendRequestedProvider -eq 1 -and
                    $ssrBackendActiveProvider -eq 1 -and
                    $active -eq 1 -and
                    $fallback -eq 0
                $checks.Add((New-Check "$($lane.name) resolves the accepted presentation defaults" `
                    $showcasePresentationDefaultsActive `
                    "aa=$antialiasingMode,dlss=$dlssOutputReady,post=$temporalPostSourceRequested/$temporalPostSourceActive/$temporalPostSourceFallback,ssr=$ssrEnabled/$ssrBackendRequestedProvider/$ssrBackendActiveProvider,hybrid=$active/$fallback" `
                    "aa=5,dlss=1,post=1/1/0,ssr=1/1/1,hybrid=1/0")) | Out-Null
            } elseif ($defaultPresentationProfile -eq "forward3d-control") {
                $forward3dReflectionDefaultsUnchanged =
                    $ssrBackendRequestedProvider -eq 0 -and
                    $ssrBackendActiveProvider -eq 0 -and
                    $requested -eq 0 -and
                    $active -eq 0 -and
                    $ffxSameFrameActive -eq 0 -and
                    $ffxApplyDraws -eq 0
                $checks.Add((New-Check "$($lane.name) keeps non-showcase reflection defaults" `
                    $forward3dReflectionDefaultsUnchanged `
                    "ssrBackend=$ssrBackendRequestedProvider/$ssrBackendActiveProvider,hybrid=$requested/$active,ffxApply=$ffxSameFrameActive/$ffxApplyDraws" `
                    "ssrBackend=0/0,hybrid=0/0,ffxApply=0/0")) | Out-Null
            }

            $checks.Add((New-Check "$($lane.name) contract version" `
                ($contract -eq 2) $contract 2)) | Out-Null
            $checks.Add((New-Check "$($lane.name) request state" `
                ($requested -eq $lane.requested -and $disabled -eq $lane.disabled) `
                "$requested/$disabled" "$($lane.requested)/$($lane.disabled)")) | Out-Null
            $checks.Add((New-Check "$($lane.name) Ray Query consumer control state" `
                ($rayQueryConsumerRequested -eq $lane.requested -and `
                    $rayQueryConsumerDisabled -eq $lane.consumerDisabled) `
                "$rayQueryConsumerRequested/$rayQueryConsumerDisabled" `
                "$($lane.requested)/$($lane.consumerDisabled)")) | Out-Null
            $checks.Add((New-Check "$($lane.name) hit-attribute control state" `
                ($rayQueryHitAttributeDisabled -eq $lane.hitAttributesDisabled) `
                $rayQueryHitAttributeDisabled $lane.hitAttributesDisabled)) | Out-Null
            $checks.Add((New-Check "$($lane.name) material-texture control state" `
                ($rayQueryMaterialTexturesDisabled -eq $lane.materialTexturesDisabled) `
                $rayQueryMaterialTexturesDisabled $lane.materialTexturesDisabled)) | Out-Null
            $checks.Add((New-Check "$($lane.name) hit-lighting control state" `
                ($rayQueryHitLightingDisabled -eq $lane.hitLightingDisabled) `
                $rayQueryHitLightingDisabled $lane.hitLightingDisabled)) | Out-Null
            $checks.Add((New-Check "$($lane.name) shadow-visibility control state" `
                ($rayQueryShadowVisibilityDisabled -eq `
                    $lane.shadowVisibilityDisabled) `
                $rayQueryShadowVisibilityDisabled `
                $lane.shadowVisibilityDisabled)) | Out-Null
            $checks.Add((New-Check "$($lane.name) denoiser-injection control state" `
                ($rayQueryDenoiserInjectionDisabled -eq `
                    $lane.denoiserInjectionDisabled) `
                $rayQueryDenoiserInjectionDisabled `
                $lane.denoiserInjectionDisabled)) | Out-Null
            $checks.Add((New-Check "$($lane.name) hardware readiness is coherent" `
                ($hardwareReady -eq [uint32]($extensionReady -and $featureReady)) `
                "hardware=$hardwareReady,extensions=$extensionReady,features=$featureReady" `
                "hardware=extensions&&features")) | Out-Null
            $checks.Add((New-Check "$($lane.name) logical device state" `
                ($deviceEnabled -eq [uint32]$expectedDeviceEnabled -and `
                    $shaderInt64Enabled -eq [uint32]$expectedDeviceEnabled -and `
                    $nonUniformSampledImageEnabled -eq [uint32]$expectedDeviceEnabled) `
                "$deviceEnabled/$shaderInt64Enabled/$nonUniformSampledImageEnabled" `
                "$([uint32]$expectedDeviceEnabled)/$([uint32]$expectedDeviceEnabled)/$([uint32]$expectedDeviceEnabled)")) | Out-Null
            $checks.Add((New-Check "$($lane.name) acceleration-structure contract" `
                ($accelerationStructureContract -eq
                    $expectedAccelerationStructureContract) `
                $accelerationStructureContract $expectedAccelerationStructureContract)) |
                Out-Null
            $checks.Add((New-Check "$($lane.name) Ray Query consumer contract" `
                ($rayQueryConsumerContract -eq $expectedRayQueryConsumerContract) `
                $rayQueryConsumerContract $expectedRayQueryConsumerContract)) | Out-Null
            $checks.Add((New-Check "$($lane.name) hit-attribute contract" `
                ($rayQueryHitAttributeContract -eq $expectedHitAttributeContract) `
                $rayQueryHitAttributeContract $expectedHitAttributeContract)) | Out-Null
            $checks.Add((New-Check "$($lane.name) material-table contract" `
                ($rayQueryMaterialTableContract -eq $expectedMaterialTableContract) `
                $rayQueryMaterialTableContract $expectedMaterialTableContract)) | Out-Null
            $checks.Add((New-Check "$($lane.name) hit-lighting contract" `
                ($rayQueryHitLightingContract -eq $expectedHitLightingContract) `
                $rayQueryHitLightingContract $expectedHitLightingContract)) | Out-Null
            $checks.Add((New-Check "$($lane.name) shadow-visibility contract" `
                ($rayQueryShadowVisibilityContract -eq `
                    $expectedShadowVisibilityContract) `
                $rayQueryShadowVisibilityContract `
                $expectedShadowVisibilityContract)) | Out-Null
            $checks.Add((New-Check "$($lane.name) denoiser bridge contract" `
                ($rayQueryDenoiserBridgeContract -eq `
                    $expectedDenoiserBridgeContract) `
                $rayQueryDenoiserBridgeContract `
                $expectedDenoiserBridgeContract)) | Out-Null
            if ($runtimeResourcesExpected) {
                $resourcesReady =
                    $accelerationStructureResourcesReady -eq 1 -and
                    $runtimeReady -eq 1 -and
                    $active -eq [uint32]$denoiserInjectionExpected -and
                    $tlasAddressReady -eq 1 -and
                    $tlasInstanceCount -gt 0 -and
                    $tlasInstanceCapacity -ge $tlasInstanceCount -and
                    $blasCacheCount -gt 0 -and
                    $blasReadyCount -gt 0 -and
                    $opaqueRigidCommands -gt 0 -and
                    $fullSceneCommands -ge $opaqueRigidCommands -and
                    $instanceOverflow -eq 0
                $checks.Add((New-Check "$($lane.name) BLAS/TLAS resources ready" `
                    $resourcesReady `
                    "asReady=$accelerationStructureResourcesReady,runtime=$runtimeReady,address=$tlasAddressReady,instances=$tlasInstanceCount,blas=$blasReadyCount" `
                    "resources=1,address=1,instances>0,blas>0")) | Out-Null
                $checks.Add((New-Check "$($lane.name) acceleration builds recorded" `
                    ($maxBlasBuildCount -gt 0 -and ($maxTlasBuildCount -gt 0 -or $maxTlasUpdateCount -gt 0)) `
                    "blasBuild=$maxBlasBuildCount,tlasBuild=$maxTlasBuildCount,tlasUpdate=$maxTlasUpdateCount" `
                    "blasBuild>0 and (tlasBuild>0 or tlasUpdate>0)")) | Out-Null
                $rayQueryResourcesValid =
                    $rayQueryResourcesReady -eq 1 -and
                    $rayQueryTlasDescriptorReady -eq 1 -and
                    $rayQueryResultWidth -gt 0 -and
                    $rayQueryResultHeight -gt 0 -and
                    $rayQueryResultFormat -eq 101 -and
                    $rayQueryMemoryBytes -gt 0
                $checks.Add((New-Check "$($lane.name) Ray Query resources ready" `
                    $rayQueryResourcesValid `
                    "resources=$rayQueryResourcesReady,tlas=$rayQueryTlasDescriptorReady,extent=$($rayQueryResultWidth)x$rayQueryResultHeight,format=$rayQueryResultFormat,memory=$rayQueryMemoryBytes" `
                    "resources=1,tlas=1,extent>0,format=101,memory>0")) | Out-Null
                $metadataContractValid =
                    $rayQueryMetadataResourcesReady -eq 1 -and
                    $rayQueryMetadataCount -eq $tlasInstanceCount -and
                    $rayQueryMetadataCapacity -ge $rayQueryMetadataCount -and
                    $rayQueryMaterialCount -gt 0 -and
                    $rayQueryAddressReadyCount -eq $rayQueryMetadataCount -and
                    $rayQueryMetadataUploadCount -eq 1 -and
                    $rayQueryMetadataBytes -eq
                        ([uint64]$rayQueryMetadataCount * 40)
                $checks.Add((New-Check "$($lane.name) instance metadata contract" `
                    $metadataContractValid `
                    "ready=$rayQueryMetadataResourcesReady,count=$rayQueryMetadataCount,capacity=$rayQueryMetadataCapacity,materials=$rayQueryMaterialCount,addresses=$rayQueryAddressReadyCount,uploads=$rayQueryMetadataUploadCount,bytes=$rayQueryMetadataBytes" `
                    "ready=1,count=tlas,capacity>=count,materials>0,addresses=count,uploads=1,bytes=count*40")) | Out-Null
                $materialTableContractValid =
                    $materialTableResourcesReady -eq 1 -and
                    $materialTableCount -eq $rayQueryMaterialCount -and
                    $materialTableCapacity -eq 256 -and
                    $materialTableOverflow -eq 0 -and
                    $materialBufferReady -eq 1 -and
                    $materialBufferUploadCount -eq 1 -and
                    $materialBufferBytes -eq
                        ([uint64]$materialTableCount * 112) -and
                    $textureDescriptorCount -eq $materialTableCount -and
                    $textureDescriptorCapacity -eq 256 -and
                    $samplerDescriptorCount -eq $materialTableCount -and
                    $samplerDescriptorCapacity -eq 256 -and
                    $distinctTextureCount -gt 0 -and
                    $distinctTextureCount -le $materialTableCount -and
                    $distinctSamplerCount -gt 0 -and
                    $distinctSamplerCount -le $materialTableCount -and
                    $duplicateTextureCount -eq
                        ($materialTableCount - $distinctTextureCount) -and
                    $duplicateSamplerCount -eq
                        ($materialTableCount - $distinctSamplerCount) -and
                    $fallbackDescriptorCount -eq
                        ((256 - $materialTableCount) * 2) -and
                    $hitSurfaceWidth -eq $rayQueryResultWidth -and
                    $hitSurfaceHeight -eq $rayQueryResultHeight -and
                    $hitSurfaceFormat -eq 97
                $checks.Add((New-Check "$($lane.name) material table and payload resources" `
                    $materialTableContractValid `
                    "ready=$materialTableResourcesReady,count=$materialTableCount/$materialTableCapacity,overflow=$materialTableOverflow,buffer=$materialBufferReady/$materialBufferUploadCount/$materialBufferBytes,descriptors=$textureDescriptorCount/$samplerDescriptorCount,distinct=$distinctTextureCount/$distinctSamplerCount,duplicates=$duplicateTextureCount/$duplicateSamplerCount,fallback=$fallbackDescriptorCount,payload=$($hitSurfaceWidth)x$hitSurfaceHeight/$hitSurfaceFormat" `
                    "ready=1,count=instance materials,capacity=256,overflow=0,bytes=count*112,descriptor identities conserved,payload=ray extent/RGBA16F")) | Out-Null
                $hitLightingResourceContractValid =
                    $hitLightingResourcesReady -eq 1 -and
                    $lightBufferDescriptorReady -eq 1 -and
                    $iblBrdfDescriptorReady -eq 1 -and
                    $iblIrradianceDescriptorReady -eq 1 -and
                    $iblPrefilteredDescriptorReady -eq 1 -and
                    $iblSamplerDescriptorReady -eq 1 -and
                    $iblPrefilteredMipCount -gt 0 -and
                    $directionalLightCount -le 1 -and
                    $localLightCount -le 64
                $checks.Add((New-Check "$($lane.name) hit-lighting descriptors and bounded lights" `
                    $hitLightingResourceContractValid `
                    "resources=$hitLightingResourcesReady,light=$lightBufferDescriptorReady,ibl=$iblBrdfDescriptorReady/$iblIrradianceDescriptorReady/$iblPrefilteredDescriptorReady/$iblSamplerDescriptorReady,mips=$iblPrefilteredMipCount,lights=$directionalLightCount+$localLightCount" `
                    "resources/light/ibl=1,mips>0,directional<=1,local<=64")) | Out-Null
                $localProbeMaskLimit = if ($localProbeCount -eq 0) {
                    0
                } else {
                    (1 -shl $localProbeCount) - 1
                }
                $localProbeIblContractValid =
                    $localProbeIblContractVersion -eq 1 -and
                    $localProbeIblResourcesReady -eq 1 -and
                    $localProbeIblEnabled -eq 1 -and
                    $sourceFusionEnabled -eq 1 -and
                    $screenHitConfidenceThreshold -eq 950 -and
                    $confidenceSpatialFilterEnabled -eq 1 -and
                    $localProbeCount -le 4 -and
                    $localProbeDescriptorWriteCount -eq 3 -and
                    ($localProbePrefilteredReadyMask -band
                        (-bnot $localProbeMaskLimit)) -eq 0 -and
                    ($localProbeDiffuseReadyMask -band
                        (-bnot $localProbeMaskLimit)) -eq 0
                $checks.Add((New-Check "$($lane.name) Ray Query hit IBL consumes bounded scene Probe inputs" `
                    $localProbeIblContractValid `
                    "contract/resources/enabled/fusion/filter=$localProbeIblContractVersion/$localProbeIblResourcesReady/$localProbeIblEnabled/$sourceFusionEnabled/$confidenceSpatialFilterEnabled,count=$localProbeCount,masks=$localProbePrefilteredReadyMask/$localProbeDiffuseReadyMask,writes=$localProbeDescriptorWriteCount,threshold=$screenHitConfidenceThreshold" `
                    "contract/resources/enabled/fusion/filter=1/1/1/1/1,count<=4,masks within count,writes=3,threshold=950")) | Out-Null
                $shadowBudgetContractValid =
                    $shadowVisibilityResourcesReady -eq 1 -and
                    $shadowMaxLocalLightCount -eq 2 -and
                    $shadowRectangleSampleCount -eq 2 -and
                    $shadowMaxRaysPerHit -eq
                        ($directionalLightCount + 4)
                $checks.Add((New-Check "$($lane.name) shadow-ray budget is bounded" `
                    $shadowBudgetContractValid `
                    "resources=$shadowVisibilityResourcesReady,local=$shadowMaxLocalLightCount,rectSamples=$shadowRectangleSampleCount,maxRays=$shadowMaxRaysPerHit" `
                    "resources=1,local=2,rectSamples=2,maxRays=directional+4")) | Out-Null
                $denoiserResourceContractValid =
                    $denoiserResourcesReady -eq 1 -and
                    $denoiserRadianceDescriptorReady -eq 1 -and
                    $denoiserConfidenceDescriptorReady -eq 1 -and
                    $denoiserInjectionEnabled -eq
                        [uint32]$denoiserInjectionExpected
                $checks.Add((New-Check "$($lane.name) DNSR bridge resources and mode" `
                    $denoiserResourceContractValid `
                    "resources=$denoiserResourcesReady,radiance=$denoiserRadianceDescriptorReady,confidence=$denoiserConfidenceDescriptorReady,injection=$denoiserInjectionEnabled,active=$active" `
                    "resources/descriptors=1,injection/active=$([uint32]$denoiserInjectionExpected)")) | Out-Null
                if ($denoiserInjectionExpected -or
                    $lane.denoiserInjectionDisabled -eq 1) {
                    $ffxDenoiserChainValid =
                        $ffxReprojectDispatches -eq 1 -and
                        $ffxPrefilterDispatches -eq 1 -and
                        $ffxResolveDispatches -eq 1 -and
                        $ffxSameFrameActive -eq 1 -and
                        $ffxApplyDraws -eq 1 -and
                        $ffxHitConfidenceApplyBound -eq 1
                    $checks.Add((New-Check "$($lane.name) existing FFX DNSR and Apply chain remains active" `
                        $ffxDenoiserChainValid `
                        "dnsr=$ffxReprojectDispatches/$ffxPrefilterDispatches/$ffxResolveDispatches,apply=$ffxSameFrameActive/$ffxApplyDraws,confidence=$ffxHitConfidenceApplyBound" `
                        "dnsr=1/1/1,apply=1/1,confidence=1")) | Out-Null
                    $mirrorDnsrFusionValid =
                        $ffxMirrorDnsrPassthroughRequested -eq 1 -and
                        $ffxMirrorDnsrPassthroughResourcesReady -eq 1 -and
                        $ffxMirrorDnsrPassthroughActive -eq 1 -and
                        $ffxMirrorDnsrRoughnessThreshold -eq 80 -and
                        $ffxMirrorDnsrConfidenceThreshold -eq 995
                    $checks.Add((New-Check "$($lane.name) full-rate mirror pixels use the selective DNSR fusion contract" `
                        $mirrorDnsrFusionValid `
                        "requested/resources/active=$ffxMirrorDnsrPassthroughRequested/$ffxMirrorDnsrPassthroughResourcesReady/$ffxMirrorDnsrPassthroughActive,thresholds=$ffxMirrorDnsrRoughnessThreshold/$ffxMirrorDnsrConfidenceThreshold" `
                        "requested/resources/active=1/1/1,thresholds=80/995")) | Out-Null
                }
                $expectedVisibilityMode = if ($shadowVisibilityResolutionExpected) {
                    2
                } elseif ($hitLightingResolutionExpected) {
                    1
                } else {
                    0
                }
                $expectedVisibilityFallback = if (
                    $hitLightingResolutionExpected -and
                    !$shadowVisibilityResolutionExpected
                ) { 1 } else { 0 }
                $checks.Add((New-Check "$($lane.name) off-screen visibility mode is explicit" `
                    ($hitLightingVisibilityMode -eq $expectedVisibilityMode -and `
                        $hitLightingVisibilityFallback -eq $expectedVisibilityFallback) `
                    "$hitLightingVisibilityMode/$hitLightingVisibilityFallback" `
                    "$expectedVisibilityMode/$expectedVisibilityFallback")) | Out-Null
                if ($rayQueryDispatchExpected) {
                    $checks.Add((New-Check "$($lane.name) direct mirror Ray Query mode" `
                        ($directMirrorEnabled -eq [uint32]$directMirrorExpected) `
                        $directMirrorEnabled `
                        ([uint32]$directMirrorExpected))) | Out-Null
                    $dispatchRecorded =
                        $rayQueryDispatchReady -eq 1 -and
                        $rayQueryDispatchCount -eq 1 -and
                        $rayQueryDescriptorBindCount -eq 2 -and
                        $rayQueryResultClearCount -eq 1
                    $checks.Add((New-Check "$($lane.name) Ray Query dispatch recorded" `
                        $dispatchRecorded `
                        "ready=$rayQueryDispatchReady,dispatch=$rayQueryDispatchCount,binds=$rayQueryDescriptorBindCount,clears=$rayQueryResultClearCount" `
                        "ready=1,dispatch=1,binds=2,clears=1")) | Out-Null
                    $candidateEquation =
                        [uint64]$rayQueryCandidateCount -eq
                        ([uint64]$rayQueryScreenAcceptedCount +
                            [uint64]$rayQueryTraceCount +
                            [uint64]$rayQueryInvalidCount)
                    $traceEquation =
                        [uint64]$rayQueryTraceCount -eq
                        ([uint64]$rayQueryCommittedHitCount +
                            [uint64]$rayQueryMissCount)
                    $checks.Add((New-Check "$($lane.name) Ray Query readback is populated" `
                        ($rayQueryReadbackValid -eq 1 -and `
                            $rayQueryCandidateCount -gt 0 -and `
                            $rayQueryTraceCount -gt 0 -and `
                            $rayQueryCommittedHitCount -gt 0 -and `
                            $rayQueryResultWriteCount -ge $rayQueryTraceCount) `
                        "valid=$rayQueryReadbackValid,candidates=$rayQueryCandidateCount,traces=$rayQueryTraceCount,hits=$rayQueryCommittedHitCount,writes=$rayQueryResultWriteCount" `
                        "valid=1,candidates>0,traces>0,hits>0,writes>=traces")) | Out-Null
                    $checks.Add((New-Check "$($lane.name) Ray Query candidate accounting" `
                        $candidateEquation `
                        "$rayQueryCandidateCount=$rayQueryScreenAcceptedCount+$rayQueryTraceCount+$rayQueryInvalidCount" `
                        "candidate=screenAccepted+trace+invalid")) | Out-Null
                    $checks.Add((New-Check "$($lane.name) Ray Query trace accounting" `
                        $traceEquation `
                        "$rayQueryTraceCount=$rayQueryCommittedHitCount+$rayQueryMissCount" `
                        "trace=committedHit+miss")) | Out-Null
                    if ($directMirrorExpected) {
                        $directMirrorAccounting =
                            [uint64]$directMirrorCandidateCount -eq
                            ([uint64]$directMirrorHitCount +
                                [uint64]$directMirrorFallbackCount)
                        $checks.Add((New-Check "$($lane.name) direct mirror ownership is conserved" `
                            $directMirrorAccounting `
                            "$directMirrorCandidateCount=$directMirrorHitCount+$directMirrorFallbackCount" `
                            "candidate=hit+fallback")) | Out-Null
                        if ($lane.name -in @(
                            "lighting-showcase-default-presentation",
                            "lighting-showcase-requested"
                        )) {
                            $checks.Add((New-Check "$($lane.name) direct mirror Ray Query is exercised" `
                                ($directMirrorCandidateCount -gt 0 -and `
                                    $directMirrorHitCount -gt 0) `
                                "candidates=$directMirrorCandidateCount,hits=$directMirrorHitCount,fallbacks=$directMirrorFallbackCount" `
                                "candidates>0,hits>0")) | Out-Null
                        }
                    } else {
                        $directMirrorSuppressed =
                            $directMirrorCandidateCount -eq 0 -and
                            $directMirrorHitCount -eq 0 -and
                            $directMirrorFallbackCount -eq 0
                        $checks.Add((New-Check "$($lane.name) direct mirror diagnostics are suppressed" `
                            $directMirrorSuppressed `
                            "$directMirrorCandidateCount/$directMirrorHitCount/$directMirrorFallbackCount" `
                            "0/0/0")) | Out-Null
                    }
                    $checks.Add((New-Check "$($lane.name) Ray Query hit distances are bounded" `
                        ($rayQueryDistanceMin -gt 0 -and `
                            $rayQueryDistanceMax -ge $rayQueryDistanceMin) `
                        "$rayQueryDistanceMin..$rayQueryDistanceMax" `
                        "0<min<=max")) | Out-Null
                    $hitAttributeEquation =
                        [uint64]$rayQueryCommittedHitCount -eq
                        ([uint64]$hitAttributeResolvedCount +
                            $invalidHitAttributeCount)
                    $materialEquation =
                        [uint64]$hitAttributeResolvedCount -eq
                        ([uint64]$hitAttributeMaterialResolvedCount +
                            [uint64]$hitAttributeMaterialFallbackCount)
                    if ($hitAttributeResolutionExpected) {
                        $checks.Add((New-Check "$($lane.name) hit attributes resolve all committed hits" `
                            ($hitAttributeResolvedCount -gt 0 -and `
                                $invalidHitAttributeCount -eq 0 -and `
                                $hitAttributeEquation) `
                            "hits=$rayQueryCommittedHitCount,resolved=$hitAttributeResolvedCount,invalid=$invalidHitAttributeCount" `
                            "hits=resolved+invalid,resolved>0,invalid=0")) | Out-Null
                        $checks.Add((New-Check "$($lane.name) hit material identity resolves" `
                            ($materialEquation -and `
                                $hitAttributeMaterialResolvedCount -gt 0) `
                            "resolved=$hitAttributeResolvedCount,material=$hitAttributeMaterialResolvedCount,fallback=$hitAttributeMaterialFallbackCount" `
                            "resolved=material+fallback,material>0")) | Out-Null
                        $attributeRangesValid =
                            $hitAttributePositionMismatchCount -eq 0 -and
                            $hitAttributeNormalLengthMin -ge 999 -and
                            $hitAttributeNormalLengthMax -le 1001 -and
                            $hitAttributeNormalLengthMax -ge
                                $hitAttributeNormalLengthMin -and
                            $hitAttributeBarycentricSumMin -ge 999 -and
                            $hitAttributeBarycentricSumMax -le 1001 -and
                            $hitAttributeBarycentricSumMax -ge
                                $hitAttributeBarycentricSumMin
                        $checks.Add((New-Check "$($lane.name) hit attribute values are coherent" `
                            $attributeRangesValid `
                            "positionMismatch=$hitAttributePositionMismatchCount,maxErrorUm=$hitAttributePositionErrorMax,normal=$hitAttributeNormalLengthMin..$hitAttributeNormalLengthMax,barycentric=$hitAttributeBarycentricSumMin..$hitAttributeBarycentricSumMax" `
                            "positionMismatch=0,normal=999..1001,barycentric=999..1001")) | Out-Null
                        $checks.Add((New-Check "$($lane.name) hit identities are non-degenerate" `
                            ($hitAttributeIdentityChecksum -ne 0 -and `
                                $hitAttributePrimitiveChecksum -ne 0 -and `
                                $hitAttributeMaterialChecksum -ne 0) `
                            "$hitAttributeIdentityChecksum/$hitAttributePrimitiveChecksum/$hitAttributeMaterialChecksum" `
                            "all nonzero")) | Out-Null
                        if ($materialTextureResolutionExpected) {
                            $materialRecordEquation =
                                [uint64]$hitAttributeResolvedCount -eq
                                ([uint64]$materialRecordResolvedCount +
                                    [uint64]$materialRecordFallbackCount)
                            $textureSampleEquation =
                                [uint64]$hitAttributeResolvedCount -eq
                                ([uint64]$textureSampleResolvedCount +
                                    [uint64]$textureSampleFallbackCount +
                                    [uint64]$textureSampleInvalidCount)
                            $materialSamplingContractValid =
                                $materialRecordEquation -and
                                $textureSampleEquation -and
                                $materialRecordResolvedCount -eq
                                    $hitAttributeResolvedCount -and
                                $materialRecordFallbackCount -eq 0 -and
                                $textureSampleResolvedCount -eq
                                    $hitAttributeResolvedCount -and
                                $textureSampleFallbackCount -eq 0 -and
                                $textureSampleInvalidCount -eq 0 -and
                                $finiteSampledColorCount -eq
                                    $hitAttributeResolvedCount -and
                                $sampleLodMax -ge $sampleLodMin
                            $checks.Add((New-Check "$($lane.name) material samples are finite" `
                                $materialSamplingContractValid `
                                "attributes=$hitAttributeResolvedCount,records=$materialRecordResolvedCount+$materialRecordFallbackCount,samples=$textureSampleResolvedCount+$textureSampleFallbackCount+$textureSampleInvalidCount,finite=$finiteSampledColorCount,lod=$sampleLodMin..$sampleLodMax" `
                                "attributes=records=samples=finite,resolved>0,fallback=invalid=0,finite LOD")) | Out-Null
                            if ($hitLightingResolutionExpected) {
                                $hitLightingContractValid =
                                    $hitLightingResolvedCount -eq
                                        $textureSampleResolvedCount -and
                                    $hitLightingInvalidCount -eq 0 -and
                                    $finiteDirectRadianceCount -eq
                                        $hitLightingResolvedCount -and
                                    $finiteIblRadianceCount -eq
                                        $hitLightingResolvedCount -and
                                    $finiteEmissiveRadianceCount -eq
                                        $hitLightingResolvedCount -and
                                    $finiteRadianceCount -eq
                                        $hitLightingResolvedCount -and
                                    $hitSurfacePayloadWriteCount -eq
                                        $hitLightingResolvedCount -and
                                    $hitSurfacePayloadChecksum -eq
                                        $radianceChecksum -and
                                    $radianceChecksum -ne 0 -and
                                    $radianceLuminanceMax -ge
                                        $radianceLuminanceMin -and
                                    $radianceLuminanceMax -gt 0 -and
                                    $hitSurfaceLuminanceMin -eq
                                        $radianceLuminanceMin -and
                                    $hitSurfaceLuminanceMax -eq
                                        $radianceLuminanceMax -and
                                    ($directLuminanceSum -gt 0 -or
                                        $iblLuminanceSum -gt 0 -or
                                        $emissiveLuminanceSum -gt 0)
                                $checks.Add((New-Check "$($lane.name) hit lighting produces finite radiance" `
                                    $hitLightingContractValid `
                                    "samples=$textureSampleResolvedCount,resolved=$hitLightingResolvedCount,invalid=$hitLightingInvalidCount,finite=$finiteDirectRadianceCount/$finiteIblRadianceCount/$finiteEmissiveRadianceCount/$finiteRadianceCount,payload=$hitSurfacePayloadWriteCount,energy=$directLuminanceSum/$iblLuminanceSum/$emissiveLuminanceSum,range=$radianceLuminanceMin..$radianceLuminanceMax,checksum=$radianceChecksum" `
                                    "samples=resolved=all finite=payload,invalid=0,energy>0,valid range/checksum")) | Out-Null
                                $localProbeIblDiagnosticsValid =
                                    $localProbeIblInvalidCount -eq 0 -and
                                    ($localProbeIblResolvedCount +
                                        $globalIblFallbackCount) -eq
                                        $hitLightingResolvedCount -and
                                    ($localProbeIblResolvedCount -eq 0 -or
                                        $localProbeIblLuminanceSum -gt 0)
                                $checks.Add((New-Check "$($lane.name) local Probe and global IBL attribution is conserved" `
                                    $localProbeIblDiagnosticsValid `
                                    "local/global=$localProbeIblResolvedCount/$globalIblFallbackCount,resolved=$hitLightingResolvedCount,invalid=$localProbeIblInvalidCount,energy=$localProbeIblLuminanceSum" `
                                    "local+global=resolved,invalid=0,local energy>0 when used")) | Out-Null
                                $lightEvaluationContractValid =
                                    ($directionalLightCount -eq 0 -or
                                        $directionalLightEvaluationCount -gt 0) -and
                                    ($localLightCount -eq 0 -or
                                        ($pointLightEvaluationCount +
                                            $spotLightEvaluationCount +
                                            $rectLightEvaluationCount) -gt 0) -and
                                    $directionalLightContributionCount -le
                                        $directionalLightEvaluationCount -and
                                    $pointLightContributionCount -le
                                        $pointLightEvaluationCount -and
                                    $spotLightContributionCount -le
                                        $spotLightEvaluationCount -and
                                    $rectLightContributionCount -le
                                        $rectLightEvaluationCount
                                $checks.Add((New-Check "$($lane.name) bounded light kinds are evaluated" `
                                    $lightEvaluationContractValid `
                                    "lights=$directionalLightCount+$localLightCount,evaluations=$directionalLightEvaluationCount/$pointLightEvaluationCount/$spotLightEvaluationCount/$rectLightEvaluationCount,contributions=$directionalLightContributionCount/$pointLightContributionCount/$spotLightContributionCount/$rectLightContributionCount" `
                                    "configured kinds evaluated,contributions<=evaluations")) | Out-Null
                                if ($shadowVisibilityResolutionExpected) {
                                    $shadowRayEquation =
                                        [uint64]$shadowRayCount -eq
                                        ([uint64]$shadowVisibleCount +
                                            [uint64]$shadowOccludedCount +
                                            [uint64]$shadowInvalidCount)
                                    $shadowKindEquation =
                                        [uint64]$shadowRayCount -eq
                                        ([uint64]$directionalShadowRayCount +
                                            [uint64]$pointShadowRayCount +
                                            [uint64]$spotShadowRayCount +
                                            [uint64]$rectShadowRayCount)
                                    $shadowBudgetValid =
                                        $shadowVisibilityResolvedCount -eq
                                            $hitLightingResolvedCount -and
                                        $shadowRayCount -gt 0 -and
                                        [uint64]$shadowRayCount -le
                                            ([uint64]$hitLightingResolvedCount *
                                                [uint64]$shadowMaxRaysPerHit) -and
                                        $shadowRayEquation -and
                                        $shadowKindEquation -and
                                        $localShadowCandidateCount -eq
                                            ($localShadowSelectedCount +
                                                $localShadowDroppedCount) -and
                                        [uint64]$localShadowSelectedCount -le
                                            ([uint64]$hitLightingResolvedCount *
                                                [uint64]$shadowMaxLocalLightCount)
                                    $checks.Add((New-Check "$($lane.name) shadow rays obey the per-hit budget" `
                                        $shadowBudgetValid `
                                        "resolved=$shadowVisibilityResolvedCount,rays=$shadowRayCount=$shadowVisibleCount+$shadowOccludedCount+$shadowInvalidCount,kinds=$directionalShadowRayCount+$pointShadowRayCount+$spotShadowRayCount+$rectShadowRayCount,local=$localShadowCandidateCount=$localShadowSelectedCount+$localShadowDroppedCount,max=$shadowMaxRaysPerHit" `
                                        "resolved=lighting,rays=visible+occluded+invalid=kinds,0<rays<=resolved*max,candidates=selected+dropped")) | Out-Null
                                    $shadowVisibilityValuesValid =
                                        $shadowInvalidCount -eq 0 -and
                                        $shadowSelfIntersectionCandidateCount -eq 0 -and
                                        ($shadowVisibleCount +
                                            $shadowOccludedCount) -gt 0 -and
                                        $visibleDirectLuminanceSum -le
                                            $unshadowedDirectLuminanceSum -and
                                        ($localShadowDroppedCount -eq 0 -or
                                            $localShadowDroppedLuminanceSum -gt 0) -and
                                        $visibleDirectLuminanceSum -ge
                                            $localShadowDroppedLuminanceSum -and
                                        $shadowVisibilityMin -le
                                            $shadowVisibilityMax -and
                                        $shadowVisibilityMax -le 1000 -and
                                        ($shadowOccludedCount -eq 0 -or
                                            $shadowHitDistanceMax -ge
                                                $shadowHitDistanceMin)
                                    $checks.Add((New-Check "$($lane.name) shadow visibility is finite and non-self-intersecting" `
                                        $shadowVisibilityValuesValid `
                                        "invalid=$shadowInvalidCount,self=$shadowSelfIntersectionCandidateCount,visible/occluded=$shadowVisibleCount/$shadowOccludedCount,direct=$visibleDirectLuminanceSum<=$unshadowedDirectLuminanceSum,visibility=$shadowVisibilityMin..$shadowVisibilityMax,hitMm=$shadowHitDistanceMin..$shadowHitDistanceMax,droppedEnergy=$localShadowDroppedLuminanceSum" `
                                        "invalid/self=0,visible+occluded>0,dropped energy retained,visibleDirect<=unshadowed,visibility=0..1000,occluded hit distance ordered")) | Out-Null
                                } else {
                                    $checks.Add((New-Check "$($lane.name) shadow visibility is suppressed" `
                                        $shadowVisibilityDiagnosticsSuppressed `
                                        "resolved=$shadowVisibilityResolvedCount,rays=$shadowRayCount,visible/occluded/invalid=$shadowVisibleCount/$shadowOccludedCount/$shadowInvalidCount" `
                                        "all=0 while hit lighting remains independently controlled")) | Out-Null
                                }
                                if ($denoiserInjectionExpected) {
                                    $expectedDenoiserInjectionCount =
                                        [uint64]$hitLightingResolvedCount +
                                        [uint64]$directMirrorFallbackCount
                                    $denoiserInjectionContractValid =
                                        [uint64]$denoiserInjectionResolvedCount -eq
                                            $expectedDenoiserInjectionCount -and
                                        $denoiserRadiancePixelWriteCount -ge
                                            $denoiserInjectionResolvedCount -and
                                        $denoiserRadiancePixelWriteCount -le
                                            $rayQueryResultWriteCount -and
                                        $denoiserConfidencePixelWriteCount -eq
                                            $denoiserRadiancePixelWriteCount -and
                                        $denoiserConfidenceSumPermille -gt 0 -and
                                        [uint64]$denoiserConfidenceSumPermille -le
                                            ([uint64]$denoiserConfidencePixelWriteCount *
                                                1000) -and
                                        $sourceFusionCount -le
                                            $denoiserInjectionResolvedCount -and
                                        ($sourceFusionCount -eq 0 -or
                                            ($sourceFusionConfidenceSum -gt 0 -and
                                                $sourceFusionScreenWeightSum -gt 0 -and
                                                [uint64]$sourceFusionConfidenceSum -le
                                                    ([uint64]$sourceFusionCount * 1000) -and
                                                [uint64]$sourceFusionScreenWeightSum -le
                                                    ([uint64]$sourceFusionCount * 1000)))
                                    $checks.Add((New-Check "$($lane.name) hardware radiance enters the existing DNSR carrier" `
                                        $denoiserInjectionContractValid `
                                        "lighting=$hitLightingResolvedCount,fallback=$directMirrorFallbackCount,injected=$denoiserInjectionResolvedCount,radianceWrites=$denoiserRadiancePixelWriteCount,confidenceWrites=$denoiserConfidencePixelWriteCount,confidenceSum=$denoiserConfidenceSumPermille,fusion=$sourceFusionCount/$sourceFusionConfidenceSum/$sourceFusionScreenWeightSum,resultWrites=$rayQueryResultWriteCount" `
                                        "lighting+directMirrorFallback=injected,radiance=confidence,injected<=writes<=result,0<=confidence<=1000 per pixel,fusion bounded")) | Out-Null
                                } else {
                                    $checks.Add((New-Check "$($lane.name) DNSR injection is suppressed" `
                                        $denoiserDiagnosticsSuppressed `
                                        "resolved=$denoiserInjectionResolvedCount,radiance=$denoiserRadiancePixelWriteCount,confidence=$denoiserConfidencePixelWriteCount/$denoiserConfidenceSumPermille" `
                                        "all=0 while independent hit diagnostics remain available")) | Out-Null
                                }
                            } else {
                                $checks.Add((New-Check "$($lane.name) hit lighting is suppressed" `
                                    ($hitLightingDiagnosticsSuppressed -and `
                                        $shadowVisibilityDiagnosticsSuppressed -and `
                                        $denoiserDiagnosticsSuppressed) `
                                    "resolved=$hitLightingResolvedCount,invalid=$hitLightingInvalidCount,payload=$hitSurfacePayloadWriteCount,checksum=$radianceChecksum" `
                                    "all lighting/payload diagnostics=0 while material sampling remains active")) | Out-Null
                            }
                        } else {
                            $materialSamplingSuppressed =
                                $materialRecordResolvedCount -eq 0 -and
                                $materialRecordFallbackCount -eq 0 -and
                                $textureSampleResolvedCount -eq 0 -and
                                $textureSampleFallbackCount -eq 0 -and
                                $textureSampleInvalidCount -eq 0 -and
                                $finiteSampledColorCount -eq 0 -and
                                $sampleLodMin -eq 0 -and
                                $sampleLodMax -eq 0 -and
                                $hitLightingDiagnosticsSuppressed -and
                                $shadowVisibilityDiagnosticsSuppressed -and
                                $denoiserDiagnosticsSuppressed
                            $checks.Add((New-Check "$($lane.name) material sampling is suppressed" `
                                $materialSamplingSuppressed `
                                "records=$materialRecordResolvedCount/$materialRecordFallbackCount,samples=$textureSampleResolvedCount/$textureSampleFallbackCount/$textureSampleInvalidCount,payload=$hitSurfacePayloadWriteCount/$hitSurfacePayloadChecksum" `
                                "all=0 while hit attributes remain active")) | Out-Null
                        }
                    } else {
                        $hitAttributesSuppressed =
                            $hitAttributeResolvedCount -eq 0 -and
                            $invalidHitAttributeCount -eq 0 -and
                            $hitAttributeMaterialResolvedCount -eq 0 -and
                            $hitAttributeMaterialFallbackCount -eq 0 -and
                            $hitAttributePositionMismatchCount -eq 0 -and
                            $hitAttributePositionErrorMax -eq 0 -and
                            $hitAttributeNormalLengthMin -eq 0 -and
                            $hitAttributeNormalLengthMax -eq 0 -and
                            $hitAttributeBarycentricSumMin -eq 0 -and
                            $hitAttributeBarycentricSumMax -eq 0 -and
                            $hitAttributeIdentityChecksum -eq 0 -and
                            $hitAttributePrimitiveChecksum -eq 0 -and
                            $hitAttributeMaterialChecksum -eq 0 -and
                            $materialRecordResolvedCount -eq 0 -and
                            $materialRecordFallbackCount -eq 0 -and
                            $textureSampleResolvedCount -eq 0 -and
                            $textureSampleFallbackCount -eq 0 -and
                            $textureSampleInvalidCount -eq 0 -and
                            $finiteSampledColorCount -eq 0 -and
                            $sampleLodMin -eq 0 -and
                            $sampleLodMax -eq 0 -and
                            $hitSurfacePayloadWriteCount -eq 0 -and
                            $hitSurfacePayloadChecksum -eq 0 -and
                            $hitSurfaceLuminanceMin -eq 0 -and
                            $hitSurfaceLuminanceMax -eq 0 -and
                            $hitLightingDiagnosticsSuppressed -and
                            $shadowVisibilityDiagnosticsSuppressed -and
                            $denoiserDiagnosticsSuppressed
                        $checks.Add((New-Check "$($lane.name) hit attributes are suppressed" `
                            $hitAttributesSuppressed `
                            "resolved=$hitAttributeResolvedCount,invalid=$invalidHitAttributeCount,material=$hitAttributeMaterialResolvedCount/$hitAttributeMaterialFallbackCount,checksums=$hitAttributeIdentityChecksum/$hitAttributePrimitiveChecksum/$hitAttributeMaterialChecksum" `
                            "all=0 while Ray Query remains active")) | Out-Null
                    }
                } else {
                    $consumerSuppressed =
                        $rayQueryDispatchReady -eq 0 -and
                        $rayQueryDispatchCount -eq 0 -and
                        $rayQueryDescriptorBindCount -eq 0 -and
                        $rayQueryResultClearCount -eq 0 -and
                        $rayQueryReadbackRows.Count -eq 0
                    $checks.Add((New-Check "$($lane.name) Ray Query consumer is suppressed" `
                        $consumerSuppressed `
                        "ready=$rayQueryDispatchReady,dispatch=$rayQueryDispatchCount,binds=$rayQueryDescriptorBindCount,clears=$rayQueryResultClearCount,readbacks=$($rayQueryReadbackRows.Count)" `
                        "all=0")) | Out-Null
                }
                $checks.Add((New-Check "$($lane.name) scene geometry accounting" `
                    ($invalidGeometry -eq 0 -and $alphaFallback -ge 0) `
                    "invalid=$invalidGeometry,alphaFallback=$alphaFallback" `
                    "invalid=0")) | Out-Null
                if ($lane.name -eq "forward3d-fbx-requested") {
                    $skinnedDynamicContract =
                        $maxSkinnedFallback -eq 0 -and
                        $skinnedBlasControlDisabled -eq 0 -and
                        $skinnedCandidateCount -gt 0 -and
                        $skinnedEligibleCount -eq $skinnedCandidateCount -and
                        $skinnedTlasInstanceCount -eq $skinnedEligibleCount -and
                        $skinnedDynamicBlasCount -gt 0 -and
                        $skinnedDynamicBlasUpdateCount -gt 0 -and
                        $skinnedSkinningDispatchCount -gt 0 -and
                        $skinnedPoseRevision -gt 0 -and
                        $skinnedPoseRevision -eq $skinnedOutputRevision -and
                        $skinnedOutputRevision -eq $skinnedBlasRevision -and
                        $skinnedRevisionMismatch -eq 0 -and
                        $skinnedInvalidPalette -eq 0
                    $checks.Add((New-Check "$($lane.name) skinned geometry owns a revision-aligned dynamic BLAS" `
                        $skinnedDynamicContract `
                        "candidate/eligible/tlas/blas=$skinnedCandidateCount/$skinnedEligibleCount/$skinnedTlasInstanceCount/$skinnedDynamicBlasCount,fallback=$maxSkinnedFallback,update/dispatch=$skinnedDynamicBlasUpdateCount/$skinnedSkinningDispatchCount,revision=$skinnedPoseRevision/$skinnedOutputRevision/$skinnedBlasRevision,invalid=$skinnedInvalidPalette" `
                        "candidate=eligible=tlas>0,blas/update/dispatch>0,fallback=0,pose=output=blas,invalid=0")) | Out-Null
                }
            } else {
                $noResources = @(
                    $rows | Where-Object {
                        (Get-UIntValue $_ "hybrid_reflections_acceleration_structure_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_runtime_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_instance_metadata_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_instance_metadata_count") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_material_table_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_material_table_count") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_texture_descriptor_count") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_hit_surface_width") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_hit_lighting_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_denoiser_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_dispatch_count") -ne 0 -or
                        (Get-UInt64Value $_ "hybrid_reflections_ray_query_memory_bytes") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_blas_cache_count") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_skinned_dynamic_blas_count") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_skinned_skinning_dispatch_count") -ne 0 -or
                        (Get-UInt64Value $_ "hybrid_reflections_skinned_skinning_buffer_bytes") -ne 0 -or
                        (Get-UInt64Value $_ "hybrid_reflections_skinned_palette_snapshot_bytes") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_tlas_instance_count") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_tlas_address_ready") -ne 0
                    }
                ).Count -eq 0
                $checks.Add((New-Check "$($lane.name) no AS resources when inactive" `
                    $noResources `
                    "asReady=$maxAsResourceReady,blas=$blasCacheCount,instances=$tlasInstanceCount,address=$tlasAddressReady" `
                    "all=0")) | Out-Null
            }
            $checks.Add((New-Check "$($lane.name) fallback is explicit" `
                $fallbackMatches $fallback "lane-specific")) | Out-Null
            $frameGraphIssues =
                Get-UIntValue $last "framegraph_validation_issues"
            $checks.Add((New-Check "$($lane.name) framegraph validation" `
                ($frameGraphIssues -eq 0) $frameGraphIssues 0)) | Out-Null

            $metrics = [ordered]@{
                requested = $requested
                disabled = $disabled
                rayQueryConsumerContract = $rayQueryConsumerContract
                rayQueryHitAttributeContract = $rayQueryHitAttributeContract
                rayQueryMaterialTableContract = $rayQueryMaterialTableContract
                rayQueryHitLightingContract = $rayQueryHitLightingContract
                rayQueryShadowVisibilityContract = $rayQueryShadowVisibilityContract
                rayQueryDenoiserBridgeContract = $rayQueryDenoiserBridgeContract
                rayQueryConsumerRequested = $rayQueryConsumerRequested
                rayQueryConsumerDisabled = $rayQueryConsumerDisabled
                rayQueryHitAttributeDisabled = $rayQueryHitAttributeDisabled
                rayQueryMaterialTexturesDisabled = $rayQueryMaterialTexturesDisabled
                rayQueryHitLightingDisabled = $rayQueryHitLightingDisabled
                rayQueryShadowVisibilityDisabled = $rayQueryShadowVisibilityDisabled
                rayQueryDenoiserInjectionDisabled = $rayQueryDenoiserInjectionDisabled
                hardwareReady = $hardwareReady
                shaderInt64FeatureSupported = $shaderInt64Feature
                shaderInt64DeviceEnabled = $shaderInt64Enabled
                nonUniformSampledImageFeatureSupported = $nonUniformSampledImageFeature
                nonUniformSampledImageDeviceEnabled = $nonUniformSampledImageEnabled
                deviceEnabled = $deviceEnabled
                accelerationStructureContract = $accelerationStructureContract
                fullSceneCommands = $fullSceneCommands
                opaqueRigidCommands = $opaqueRigidCommands
                skinnedFallback = $skinnedFallback
                skinnedDynamic = "$skinnedCandidateCount/$skinnedEligibleCount/$skinnedTlasInstanceCount/$skinnedDynamicBlasCount"
                skinnedUpdateDispatch = "$skinnedDynamicBlasUpdateCount/$skinnedSkinningDispatchCount"
                skinnedRevisions = "$skinnedPoseRevision/$skinnedOutputRevision/$skinnedBlasRevision/$skinnedRevisionMismatch"
                skinnedReadback = "$skinnedReadbackSkinnedVertexCount/$skinnedReadbackVertexCount/$skinnedReadbackInvalidBoneIndexCount/$skinnedReadbackNonFiniteVertexCount"
                alphaFallback = $alphaFallback
                blasCacheCount = $blasCacheCount
                blasReadyCount = $blasReadyCount
                maxBlasBuildCount = $maxBlasBuildCount
                tlasInstanceCount = $tlasInstanceCount
                tlasInstanceCapacity = $tlasInstanceCapacity
                maxTlasBuildCount = $maxTlasBuildCount
                maxTlasUpdateCount = $maxTlasUpdateCount
                tlasAddressReady = $tlasAddressReady
                accelerationStructureResourcesReady = $accelerationStructureResourcesReady
                runtimeReady = $runtimeReady
                rayQueryResourcesReady = $rayQueryResourcesReady
                rayQueryTlasDescriptorReady = $rayQueryTlasDescriptorReady
                rayQueryMetadataResourcesReady = $rayQueryMetadataResourcesReady
                rayQueryMetadataCount = $rayQueryMetadataCount
                rayQueryMetadataCapacity = $rayQueryMetadataCapacity
                rayQueryMaterialCount = $rayQueryMaterialCount
                rayQueryAddressReadyCount = $rayQueryAddressReadyCount
                rayQueryMetadataUploadCount = $rayQueryMetadataUploadCount
                rayQueryMetadataBytes = $rayQueryMetadataBytes
                materialTableCount = $materialTableCount
                materialTableCapacity = $materialTableCapacity
                materialTableOverflow = $materialTableOverflow
                materialBufferBytes = $materialBufferBytes
                textureDescriptors = "$textureDescriptorCount/$textureDescriptorCapacity"
                samplerDescriptors = "$samplerDescriptorCount/$samplerDescriptorCapacity"
                distinctTextureCount = $distinctTextureCount
                distinctSamplerCount = $distinctSamplerCount
                fallbackDescriptorCount = $fallbackDescriptorCount
                hitSurface = "$($hitSurfaceWidth)x$hitSurfaceHeight/$hitSurfaceFormat"
                hitLightingResourcesReady = $hitLightingResourcesReady
                hitLightingDescriptors = "$lightBufferDescriptorReady/$iblBrdfDescriptorReady/$iblIrradianceDescriptorReady/$iblPrefilteredDescriptorReady/$iblSamplerDescriptorReady"
                localProbeIbl = "$localProbeIblContractVersion/$localProbeIblResourcesReady/$localProbeIblEnabled/$localProbeCount/$localProbePrefilteredReadyMask/$localProbeDiffuseReadyMask/$localProbeDescriptorWriteCount/$sourceFusionEnabled/$directMirrorEnabled/$screenHitConfidenceThreshold/$confidenceSpatialFilterEnabled"
                hitLightingLightCounts = "$directionalLightCount+$localLightCount"
                hitLightingVisibility = "$hitLightingVisibilityMode/$hitLightingVisibilityFallback"
                shadowVisibilityResourcesReady = $shadowVisibilityResourcesReady
                shadowRayBudget = "$shadowMaxLocalLightCount/$shadowRectangleSampleCount/$shadowMaxRaysPerHit"
                denoiserBridge = "$denoiserResourcesReady/$denoiserRadianceDescriptorReady/$denoiserConfidenceDescriptorReady/$denoiserInjectionEnabled"
                denoiserChain = "$ffxReprojectDispatches/$ffxPrefilterDispatches/$ffxResolveDispatches/$ffxSameFrameActive/$ffxApplyDraws/$ffxHitConfidenceApplyBound"
                mirrorDnsrFusion = "$ffxMirrorDnsrPassthroughRequested/$ffxMirrorDnsrPassthroughResourcesReady/$ffxMirrorDnsrPassthroughActive/$ffxMirrorDnsrRoughnessThreshold/$ffxMirrorDnsrConfidenceThreshold"
                presentationProfile = $defaultPresentationProfile
                antialiasingMode = $antialiasingMode
                dlssOutputReady = $dlssOutputReady
                temporalPostSource = "$temporalPostSourceRequested/$temporalPostSourceActive/$temporalPostSourceFallback"
                ssrBackend = "$ssrEnabled/$ssrBackendRequestedProvider/$ssrBackendActiveProvider"
                rayQueryDispatchReady = $rayQueryDispatchReady
                rayQueryDispatchCount = $rayQueryDispatchCount
                rayQueryReadbackValid = $rayQueryReadbackValid
                rayQueryCandidates = $rayQueryCandidateCount
                rayQueryScreenAccepted = $rayQueryScreenAcceptedCount
                rayQueryTraces = $rayQueryTraceCount
                rayQueryCommittedHits = $rayQueryCommittedHitCount
                rayQueryMisses = $rayQueryMissCount
                rayQueryInvalid = $rayQueryInvalidCount
                rayQueryResultWrites = $rayQueryResultWriteCount
                hitAttributeResolved = $hitAttributeResolvedCount
                hitAttributeInvalid = $invalidHitAttributeCount
                hitAttributeMaterialResolved = $hitAttributeMaterialResolvedCount
                hitAttributeMaterialFallback = $hitAttributeMaterialFallbackCount
                hitAttributePositionMismatch = $hitAttributePositionMismatchCount
                hitAttributePositionErrorMaxMicrometers = $hitAttributePositionErrorMax
                hitAttributeNormalLengthPermille = "$hitAttributeNormalLengthMin..$hitAttributeNormalLengthMax"
                hitAttributeBarycentricSumPermille = "$hitAttributeBarycentricSumMin..$hitAttributeBarycentricSumMax"
                hitAttributeIdentityChecksum = $hitAttributeIdentityChecksum
                hitAttributePrimitiveChecksum = $hitAttributePrimitiveChecksum
                hitAttributeMaterialChecksum = $hitAttributeMaterialChecksum
                materialRecordResolved = $materialRecordResolvedCount
                materialRecordFallback = $materialRecordFallbackCount
                textureSampleResolved = $textureSampleResolvedCount
                textureSampleFallback = $textureSampleFallbackCount
                textureSampleInvalid = $textureSampleInvalidCount
                finiteSampledColor = $finiteSampledColorCount
                sampleLodMillilevels = "$sampleLodMin..$sampleLodMax"
                hitSurfacePayloadWrites = $hitSurfacePayloadWriteCount
                hitSurfacePayloadChecksum = $hitSurfacePayloadChecksum
                hitSurfaceLuminanceMilliunits = "$hitSurfaceLuminanceMin..$hitSurfaceLuminanceMax"
                hitLightingResolved = $hitLightingResolvedCount
                hitLightingInvalid = $hitLightingInvalidCount
                hitLightingEvaluations = "$directionalLightEvaluationCount/$pointLightEvaluationCount/$spotLightEvaluationCount/$rectLightEvaluationCount"
                hitLightingContributions = "$directionalLightContributionCount/$pointLightContributionCount/$spotLightContributionCount/$rectLightContributionCount"
                hitLightingFinite = "$finiteDirectRadianceCount/$finiteIblRadianceCount/$finiteEmissiveRadianceCount/$finiteRadianceCount"
                localProbeIblAttribution = "$localProbeIblResolvedCount/$globalIblFallbackCount/$localProbeIblInvalidCount/$localProbeIblLuminanceSum"
                sourceFusion = "$sourceFusionCount/$sourceFusionConfidenceSum/$sourceFusionScreenWeightSum"
                directMirror = "$directMirrorCandidateCount/$directMirrorHitCount/$directMirrorFallbackCount"
                hitLightingEnergyMilliunits = "$directLuminanceSum/$iblLuminanceSum/$emissiveLuminanceSum"
                hitLightingRadianceRange = "$radianceLuminanceMin..$radianceLuminanceMax"
                hitLightingRadianceChecksum = $radianceChecksum
                shadowVisibilityResolved = $shadowVisibilityResolvedCount
                shadowRays = $shadowRayCount
                shadowOutcomes = "$shadowVisibleCount/$shadowOccludedCount/$shadowInvalidCount"
                shadowRaysByKind = "$directionalShadowRayCount/$pointShadowRayCount/$spotShadowRayCount/$rectShadowRayCount"
                localShadowSelection = "$localShadowCandidateCount/$localShadowSelectedCount/$localShadowDroppedCount"
                shadowDirectEnergyMilliunits = "$unshadowedDirectLuminanceSum/$visibleDirectLuminanceSum"
                shadowSelfIntersectionCandidates = $shadowSelfIntersectionCandidateCount
                shadowHitDistanceMillimeters = "$shadowHitDistanceMin..$shadowHitDistanceMax"
                shadowVisibilityPermille = "$shadowVisibilityMin..$shadowVisibilityMax"
                localShadowDroppedEnergyMilliunits = $localShadowDroppedLuminanceSum
                denoiserInjectionResolved = $denoiserInjectionResolvedCount
                denoiserInjectionWrites = "$denoiserRadiancePixelWriteCount/$denoiserConfidencePixelWriteCount/$denoiserConfidenceSumPermille"
                active = $active
                fallbackReason = $fallback
            }
        }
    }

    $stdout = Get-Content -Raw -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
    $stderr = Get-Content -Raw -LiteralPath $stderrPath -ErrorAction SilentlyContinue
    $processLog = @($stdout, $stderr) -join [Environment]::NewLine
    $validationMessages = @(
        $processLog -split "`r?`n" |
            Where-Object { $_ -match '\[Vulkan Validation\]|\bVUID-' }
    )
    $checks.Add((New-Check "$($lane.name) Vulkan validation" `
        ($validationMessages.Count -eq 0) $validationMessages.Count 0)) | Out-Null

    $laneFailCount = @($checks | Where-Object status -eq "fail").Count
    $reports.Add([pscustomobject]@{
        lane = $lane.name
        executable = $lane.executable
        csv = $csvPath
        verdict = if ($laneFailCount -eq 0) { "pass" } else { "fail" }
        passCount = @($checks | Where-Object status -eq "pass").Count
        failCount = $laneFailCount
        metrics = $metrics
        checks = $checks
    }) | Out-Null
}

$passCount = [int](($reports | Measure-Object passCount -Sum).Sum)
$failCount = [int](($reports | Measure-Object failCount -Sum).Sum)
$summary = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    outputDirectory = $OutputDirectory
    verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
    passCount = $passCount
    failCount = $failCount
    reports = $reports
}
$summaryPath = Join-Path $OutputDirectory "summary.json"
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding utf8
[pscustomobject]$summary

if ($Strict -and $failCount -ne 0) {
    throw "Hybrid reflections capability gate failed: $failCount check(s)"
}
