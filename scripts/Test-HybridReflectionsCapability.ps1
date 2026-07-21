[CmdletBinding()]
param(
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$ShowcaseExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [switch]$SkipBuild,
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
$managedKeys = @(
    "SE_HYBRID_REFLECTIONS_RT",
    "SE_HYBRID_REFLECTIONS_RT_OFF",
    "SE_HYBRID_REFLECTIONS_RAY_QUERY_OFF",
    "SE_HYBRID_REFLECTIONS_HIT_ATTRIBUTES_OFF",
    "SE_SSR",
    "SE_SSR_BACKEND",
    "SE_FORWARD3D_AA_MODE",
    "SE_BENCHMARK_SCENE",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_SCENE_UPDATE_FREEZE",
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
    SE_SCENE_UPDATE_FREEZE = "1"
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_BENCHMARK_WARMUP_FRAMES = "2"
    SE_BENCHMARK_FRAMES = "7"
    SE_AUTO_EXIT_FRAMES = "14"
}
$laneSpecs = @(
    [pscustomobject]@{
        name = "lighting-showcase-requested"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
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
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-consumer-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        consumerDisabled = 1
        hitAttributesDisabled = 0
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
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_HIT_ATTRIBUTES_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-rt-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 1
        consumerDisabled = 0
        hitAttributesDisabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_RT_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "forward3d-not-requested-control"
        executable = $forwardExecutable
        requested = 0
        disabled = 0
        consumerDisabled = 0
        hitAttributesDisabled = 0
        environment = @{
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
        }
    }
)

$reports = [Collections.Generic.List[object]]::new()
foreach ($lane in $laneSpecs) {
    $laneDirectory = Join-Path $OutputDirectory $lane.name
    New-Item -ItemType Directory -Force -Path $laneDirectory | Out-Null
    $csvPath = Join-Path $laneDirectory "hybrid_reflections_capability.csv"
    $stdoutPath = Join-Path $laneDirectory "process.stdout.log"
    $stderrPath = Join-Path $laneDirectory "process.stderr.log"
    Remove-Item -LiteralPath $csvPath, $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    $environment = $commonEnvironment.Clone()
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
            $rayQueryConsumerRequested = Get-UIntValue $last "hybrid_reflections_ray_query_consumer_requested"
            $rayQueryConsumerDisabled = Get-UIntValue $last "hybrid_reflections_ray_query_consumer_control_disabled"
            $rayQueryHitAttributeDisabled = Get-UIntValue $last "hybrid_reflections_ray_query_hit_attribute_control_disabled"
            $bdaExtension = Get-UIntValue $last "hybrid_reflections_buffer_device_address_extension_supported"
            $deferredExtension = Get-UIntValue $last "hybrid_reflections_deferred_host_operations_extension_supported"
            $asExtension = Get-UIntValue $last "hybrid_reflections_acceleration_structure_extension_supported"
            $rayQueryExtension = Get-UIntValue $last "hybrid_reflections_ray_query_extension_supported"
            $bdaFeature = Get-UIntValue $last "hybrid_reflections_buffer_device_address_feature_supported"
            $shaderInt64Feature = Get-UIntValue $last "hybrid_reflections_shader_int64_feature_supported"
            $asFeature = Get-UIntValue $last "hybrid_reflections_acceleration_structure_feature_supported"
            $rayQueryFeature = Get-UIntValue $last "hybrid_reflections_ray_query_feature_supported"
            $hardwareReady = Get-UIntValue $last "hybrid_reflections_ray_query_hardware_ready"
            $shaderInt64Enabled = Get-UIntValue $last "hybrid_reflections_shader_int64_device_enabled"
            $deviceEnabled = Get-UIntValue $last "hybrid_reflections_ray_query_device_enabled"
            $accelerationStructureContract = Get-UIntValue $last "hybrid_reflections_acceleration_structure_contract_version"
            $fullSceneCommands = Get-UIntValue $last "hybrid_reflections_full_scene_command_count"
            $opaqueRigidCommands = Get-UIntValue $last "hybrid_reflections_opaque_rigid_command_count"
            $skinnedFallback = Get-UIntValue $last "hybrid_reflections_skinned_fallback_count"
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
            $invalidHitAttributeCount =
                [uint64]$hitAttributeInvalidInstanceCount +
                [uint64]$hitAttributeInvalidPrimitiveCount +
                [uint64]$hitAttributeInvalidVertexCount +
                [uint64]$hitAttributeInvalidBarycentricCount +
                [uint64]$hitAttributeInvalidValueCount
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
                $asFeature -eq 1 -and
                $rayQueryFeature -eq 1
            $expectedDeviceEnabled =
                $lane.requested -eq 1 -and $lane.disabled -eq 0 -and
                $hardwareReady -eq 1
            $runtimeResourcesExpected =
                $lane.requested -eq 1 -and
                $lane.disabled -eq 0 -and
                $hardwareReady -eq 1
            $rayQueryDispatchExpected =
                $runtimeResourcesExpected -and
                $lane.consumerDisabled -eq 0
            $hitAttributeResolutionExpected =
                $rayQueryDispatchExpected -and
                $lane.hitAttributesDisabled -eq 0
            $expectedRayQueryConsumerContract = if ($runtimeResourcesExpected) {
                2
            } else {
                0
            }
            $expectedHitAttributeContract = if ($runtimeResourcesExpected) {
                1
            } else {
                0
            }
            $fallbackMatches = if ($lane.requested -eq 0) {
                $fallback -eq 1
            } elseif ($lane.disabled -eq 1) {
                $fallback -eq 2
            } elseif ($rayQueryDispatchExpected) {
                $fallback -eq 8
            } elseif ($runtimeResourcesExpected) {
                $fallback -eq 7
            } else {
                $fallback -in @(3, 4, 5)
            }

            $checks.Add((New-Check "$($lane.name) contract version" `
                ($contract -eq 1) $contract 1)) | Out-Null
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
            $checks.Add((New-Check "$($lane.name) hardware readiness is coherent" `
                ($hardwareReady -eq [uint32]($extensionReady -and $featureReady)) `
                "hardware=$hardwareReady,extensions=$extensionReady,features=$featureReady" `
                "hardware=extensions&&features")) | Out-Null
            $checks.Add((New-Check "$($lane.name) logical device state" `
                ($deviceEnabled -eq [uint32]$expectedDeviceEnabled -and `
                    $shaderInt64Enabled -eq [uint32]$expectedDeviceEnabled) `
                "$deviceEnabled/$shaderInt64Enabled" `
                "$([uint32]$expectedDeviceEnabled)/$([uint32]$expectedDeviceEnabled)")) | Out-Null
            $checks.Add((New-Check "$($lane.name) acceleration-structure contract" `
                ($accelerationStructureContract -eq [uint32]$runtimeResourcesExpected) `
                $accelerationStructureContract ([uint32]$runtimeResourcesExpected))) | Out-Null
            $checks.Add((New-Check "$($lane.name) Ray Query consumer contract" `
                ($rayQueryConsumerContract -eq $expectedRayQueryConsumerContract) `
                $rayQueryConsumerContract $expectedRayQueryConsumerContract)) | Out-Null
            $checks.Add((New-Check "$($lane.name) hit-attribute contract" `
                ($rayQueryHitAttributeContract -eq $expectedHitAttributeContract) `
                $rayQueryHitAttributeContract $expectedHitAttributeContract)) | Out-Null
            if ($runtimeResourcesExpected) {
                $resourcesReady =
                    $accelerationStructureResourcesReady -eq 1 -and
                    $runtimeReady -eq 1 -and
                    $active -eq 0 -and
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
                        ([uint64]$rayQueryMetadataCount * 32)
                $checks.Add((New-Check "$($lane.name) instance metadata contract" `
                    $metadataContractValid `
                    "ready=$rayQueryMetadataResourcesReady,count=$rayQueryMetadataCount,capacity=$rayQueryMetadataCapacity,materials=$rayQueryMaterialCount,addresses=$rayQueryAddressReadyCount,uploads=$rayQueryMetadataUploadCount,bytes=$rayQueryMetadataBytes" `
                    "ready=1,count=tlas,capacity>=count,materials>0,addresses=count,uploads=1,bytes=count*32")) | Out-Null
                if ($rayQueryDispatchExpected) {
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
                            $hitAttributeMaterialChecksum -eq 0
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
                    $checks.Add((New-Check "$($lane.name) skinned geometry uses fallback" `
                        ($maxSkinnedFallback -gt 0) `
                        $maxSkinnedFallback ">0")) | Out-Null
                }
            } else {
                $noResources = @(
                    $rows | Where-Object {
                        (Get-UIntValue $_ "hybrid_reflections_acceleration_structure_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_runtime_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_instance_metadata_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_instance_metadata_count") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_ray_query_dispatch_count") -ne 0 -or
                        (Get-UInt64Value $_ "hybrid_reflections_ray_query_memory_bytes") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_blas_cache_count") -ne 0 -or
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
                rayQueryConsumerRequested = $rayQueryConsumerRequested
                rayQueryConsumerDisabled = $rayQueryConsumerDisabled
                rayQueryHitAttributeDisabled = $rayQueryHitAttributeDisabled
                hardwareReady = $hardwareReady
                shaderInt64FeatureSupported = $shaderInt64Feature
                shaderInt64DeviceEnabled = $shaderInt64Enabled
                deviceEnabled = $deviceEnabled
                accelerationStructureContract = $accelerationStructureContract
                fullSceneCommands = $fullSceneCommands
                opaqueRigidCommands = $opaqueRigidCommands
                skinnedFallback = $skinnedFallback
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
