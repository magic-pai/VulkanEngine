[CmdletBinding()]
param(
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$ShowcaseExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [switch]$SkipBuild,
    [switch]$SkipSigning,
    [switch]$VerifyHoleDiagnostics,
    [switch]$Strict,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot "tmp\ssr_hiz_health"
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

$forwardExecutable = Resolve-FullPath $ForwardExecutablePath
$showcaseExecutable = Resolve-FullPath $ShowcaseExecutablePath

function Invoke-SsrBuild {
    param([string]$Root)

    $buildDirectory = Join-Path $Root "build"
    $vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    foreach ($project in @("SelfEngineForward3D.vcxproj", "SelfEngineLightingShowcase.vcxproj")) {
        & cmd.exe /c "call `"$vcvars`" >nul 2>&1 && cd /d `"$buildDirectory`" && MSBuild $project /p:Configuration=Debug /v:minimal /nologo"
        if ($LASTEXITCODE -ne 0) {
            throw "$project Debug build failed with exit code $LASTEXITCODE"
        }
    }
}

function Invoke-SsrSigning {
    param([string]$Root, [string]$TargetPath)

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root "scripts\Sign-SelfEngineDevBinary.ps1") -TargetPath $TargetPath
    if ($LASTEXITCODE -ne 0) {
        throw "Debug signing failed for $TargetPath with exit code $LASTEXITCODE"
    }
    # Smart App Control may refresh the local publisher trust asynchronously.
    Start-Sleep -Milliseconds 1000
}

function Set-ProcessEnvironment {
    param([hashtable]$Values)

    $previous = @{}
    foreach ($entry in $Values.GetEnumerator()) {
        $previous[$entry.Key] = [Environment]::GetEnvironmentVariable(
            $entry.Key,
            "Process"
        )
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

function Get-UIntMetric {
    param([pscustomobject]$Row, [string]$Name)

    return [uint32]$Row.PSObject.Properties[$Name].Value
}

function Get-UIntRangeMetric {
    param([object[]]$Rows, [string]$Name)

    $values = @()
    foreach ($row in $Rows) {
        $property = $row.PSObject.Properties[$Name]
        if ($null -eq $property) {
            continue
        }
        $values += [uint32]$property.Value
    }
    if ($values.Count -eq 0) {
        return [pscustomobject]@{
            Count = 0
            Minimum = [uint32]0
            Maximum = [uint32]0
            Delta = [uint32]0
            Average = 0.0
        }
    }

    $range = $values | Measure-Object -Minimum -Maximum -Average
    return [pscustomobject]@{
        Count = [int]$values.Count
        Minimum = [uint32]$range.Minimum
        Maximum = [uint32]$range.Maximum
        Delta = [uint32]([uint64]$range.Maximum - [uint64]$range.Minimum)
        Average = [double]$range.Average
    }
}

function Get-PermilleDelta {
    param([uint32]$Delta, [uint32]$Total)

    if ($Total -eq 0) {
        return [uint32]0
    }
    return [uint32][Math]::Round(
        ([double]$Delta * 1000.0) / [double]$Total
    )
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

function Invoke-SsrShaderContract {
    $shaderPath = Join-Path $projectRoot "assets\shaders\ssr_temporal.comp"
    $shaderSource = Get-Content -Raw -LiteralPath $shaderPath
    $usesRawTraceRgbAsRadiance =
        $shaderSource -match "RgbToYCoCg\s*\(\s*sampleValue\.rgb\s*\)"
    $samplesNeighborHitRadiance =
        $shaderSource -match
            "SampleCurrentHitRadiance\s*\(\s*sampleHitUv\s*,\s*sampleRoughness\s*\)"
    $checks = @(
        (New-Check "SSR temporal neighborhood samples hit radiance" `
            ($samplesNeighborHitRadiance -and -not $usesRawTraceRgbAsRadiance) `
            "samplesNeighborHitRadiance=$samplesNeighborHitRadiance,usesRawTraceRgbAsRadiance=$usesRawTraceRgbAsRadiance" `
            "true/false")
    )
    $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
    $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
    return [pscustomobject]@{
        lane = "ssr-temporal-shader-contract"
        scene = "Shader static contract"
        executable = ""
        mode = "static"
        csv = ""
        log = ""
        verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
        passCount = $passCount
        failCount = $failCount
        metrics = [pscustomobject]@{
            samplesNeighborHitRadiance = [int]$samplesNeighborHitRadiance
            usesRawTraceRgbAsRadiance = [int]$usesRawTraceRgbAsRadiance
        }
        checks = $checks
    }
}

function Invoke-SsrLane {
    param(
        [string]$Name,
        [string]$Executable,
        [string]$Scene,
        [hashtable]$Environment,
        [string]$Mode
    )

    $laneDirectory = Join-Path $OutputDirectory $Name
    New-Item -ItemType Directory -Force -Path $laneDirectory | Out-Null
    $csvPath = Join-Path $laneDirectory "ssr_refinement_health.csv"
    $logPath = Join-Path $laneDirectory "process.log"
    $stdoutPath = Join-Path $laneDirectory "process.stdout.log"
    $stderrPath = Join-Path $laneDirectory "process.stderr.log"
    Remove-Item -LiteralPath $csvPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stdoutPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue

    $environment["SE_BENCHMARK_CSV"] = $csvPath
    $previous = Set-ProcessEnvironment -Values $environment
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
            if ($exitCode -ne 0) {
                $startupStderr =
                    Get-Content -Raw -LiteralPath $stderrPath -ErrorAction SilentlyContinue
                if ($exitCode -eq 4551 -or
                    $startupStderr -match "Device Guard" -or
                    $startupStderr -match "was blocked") {
                    $startProcessBlocked = $true
                }
            }
        } catch [System.InvalidOperationException] {
            # Some development hosts block Start-Process for freshly signed local binaries.
            # Keep the lane synchronous and use the documented cmd launch fallback.
            $startProcessBlocked = $true
        }
        if ($startProcessBlocked) {
            Remove-Item -LiteralPath $stdoutPath -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
            $executableDirectory = Split-Path -Parent $Executable
            $commandLine = "cd /d `"$executableDirectory`" && `"$Executable`" 1> `"$stdoutPath`" 2> `"$stderrPath`""
            & cmd.exe /d /c $commandLine
            $exitCode = $LASTEXITCODE
        }
        if ($exitCode -ne 0) {
            throw "$Name exited with code $exitCode"
        }
    } finally {
        Restore-ProcessEnvironment -Values $previous
    }
    $stdout = Get-Content -Raw -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
    $stderr = Get-Content -Raw -LiteralPath $stderrPath -ErrorAction SilentlyContinue
    $combinedLog = (
        @($stdout, $stderr) |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    ) -join [Environment]::NewLine
    Set-Content -LiteralPath $logPath -Value $combinedLog -Encoding utf8

    $expectedCapturedFrames = 0
    if ($Environment.ContainsKey("SE_BENCHMARK_FRAMES")) {
        [void][int]::TryParse($Environment["SE_BENCHMARK_FRAMES"], [ref]$expectedCapturedFrames)
    }
    $rows = @()
    $csvDeadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $csvDeadline) {
        if (Test-Path -LiteralPath $csvPath) {
            try {
                $rows = @(Import-Csv -LiteralPath $csvPath)
            } catch {
                $rows = @()
            }
            if ($rows.Count -gt 0 -and (
                    $expectedCapturedFrames -le 0 -or
                    $rows.Count -ge $expectedCapturedFrames
                )) {
                break
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if ($rows.Count -eq 0) {
        throw "$Name did not emit a populated benchmark CSV: $csvPath"
    }
    $last = $rows[-1]
    $ssrEnabled = Get-UIntMetric $last "ssr_enabled"
    $colorResolve = Get-UIntMetric $last "ssr_color_resolve_enabled"
    $inputsReady = Get-UIntMetric $last "ssr_trace_inputs_ready"
    $hizRequested = Get-UIntMetric $last "ssr_hiz_requested"
    $hizActive = Get-UIntMetric $last "ssr_hiz_active"
    $hizFallbackReason = Get-UIntMetric $last "ssr_hiz_fallback_reason"
    $fixedFallback = Get-UIntMetric $last "ssr_fixed_step_fallback_active"
    $pyramidAllocated = Get-UIntMetric $last "ssr_depth_pyramid_allocated"
    $pyramidReady = Get-UIntMetric $last "ssr_depth_pyramid_ready"
    $pyramidWidth = Get-UIntMetric $last "ssr_depth_pyramid_width"
    $pyramidHeight = Get-UIntMetric $last "ssr_depth_pyramid_height"
    $pyramidMipCount = Get-UIntMetric $last "ssr_depth_pyramid_mip_count"
    $pyramidImageCount = Get-UIntMetric $last "ssr_depth_pyramid_image_count"
    $pyramidFormat = Get-UIntMetric $last "ssr_depth_pyramid_format"
    $pyramidMemoryBytes = [uint64]$last.ssr_depth_pyramid_memory_bytes
    $buildDispatchCount = Get-UIntMetric $last "ssr_depth_pyramid_build_dispatch_count"
    $generatedMipMask = [uint64]$last.ssr_depth_pyramid_generated_mip_mask
    $traversalMaxMip = Get-UIntMetric $last "ssr_hiz_traversal_max_mip"
    $buildDispatchRange = $rows |
        ForEach-Object { [uint32]$_.ssr_hiz_build_dispatches } |
        Measure-Object -Minimum -Maximum
    $descriptorBindRange = $rows |
        ForEach-Object { [uint32]$_.ssr_hiz_build_descriptor_binds } |
        Measure-Object -Minimum -Maximum
    $consumerDrawRange = $rows |
        ForEach-Object { [uint32]$_.ssr_hiz_consumer_draws } |
        Measure-Object -Minimum -Maximum
    $actualBuildDispatches = [uint32]$buildDispatchRange.Maximum
    $actualBuildDispatchMinimum = [uint32]$buildDispatchRange.Minimum
    $actualDescriptorBinds = [uint32]$descriptorBindRange.Maximum
    $actualDescriptorBindMinimum = [uint32]$descriptorBindRange.Minimum
    $actualConsumerDraws = [uint32]$consumerDrawRange.Maximum
    $actualConsumerDrawMinimum = [uint32]$consumerDrawRange.Minimum
    $refinement = Get-UIntMetric $last "ssr_refinement_enabled"
    $refinementSteps = Get-UIntMetric $last "ssr_refinement_step_count"
    $hitValidationRequested = Get-UIntMetric $last "ssr_hit_validation_requested"
    $hitValidationActive = Get-UIntMetric $last "ssr_hit_validation_active"
    $hitValidationContractVersion = Get-UIntMetric $last "ssr_hit_validation_contract_version"
    $hitNormalValidationEnabled = Get-UIntMetric $last "ssr_hit_normal_validation_enabled"
    $hitFootprintTapCount = Get-UIntMetric $last "ssr_hit_footprint_tap_count"
    $signedDepthValidationEnabled = Get-UIntMetric $last "ssr_signed_depth_validation_enabled"
    $originBiasMinimumPixels = [double]$last.ssr_origin_bias_minimum_pixels
    $originBiasMaximumPixels = [double]$last.ssr_origin_bias_maximum_pixels
    $reconstructionRequested = Get-UIntMetric $last "ssr_reconstruction_requested"
    $reconstructionActive = Get-UIntMetric $last "ssr_reconstruction_active"
    $reconstructionTargetsAllocated = Get-UIntMetric $last "ssr_reconstruction_targets_allocated"
    $reconstructionDescriptorSetsReady = Get-UIntMetric $last "ssr_reconstruction_descriptor_sets_ready"
    $reconstructionTraceDispatches = Get-UIntMetric $last "ssr_reconstruction_bind_trace_dispatches"
    $reconstructionTemporalDispatches = Get-UIntMetric $last "ssr_reconstruction_bind_temporal_dispatches"
    $reconstructionSpatialDispatches = Get-UIntMetric $last "ssr_reconstruction_bind_spatial_dispatches"
    $reconstructionHistoryCopies = Get-UIntMetric $last "ssr_reconstruction_bind_history_copies"
    $reconstructionHistoryReset = Get-UIntMetric $last "ssr_reconstruction_history_reset"
    $reconstructionImageCount = Get-UIntMetric $last "ssr_reconstruction_image_count"
    $reconstructionMemoryBytes = [uint64]$last.ssr_reconstruction_memory_bytes
    $reconstructionTemporalContractVersion = Get-UIntMetric $last "ssr_reconstruction_temporal_contract_version"
    $reconstructionTemporalMissHistoryRejectEnabled = Get-UIntMetric $last "ssr_reconstruction_temporal_miss_history_reject_enabled"
    $reconstructionTemporalPreviousViewDepthEnabled = Get-UIntMetric $last "ssr_reconstruction_temporal_previous_view_depth_enabled"
    $reconstructionTemporalHistoryLockEnabled = Get-UIntMetric $last "ssr_reconstruction_temporal_history_lock_enabled"
    $reconstructionSpatialCenterHitGateEnabled = Get-UIntMetric $last "ssr_reconstruction_spatial_center_hit_gate_enabled"
    $reconstructionSpatialVarianceClampEnabled = Get-UIntMetric $last "ssr_reconstruction_spatial_variance_clamp_enabled"
    $reconstructionSpatialSupportTapCount = Get-UIntMetric $last "ssr_reconstruction_spatial_support_tap_count"
    $reconstructionRawResolvedAliased = Get-UIntMetric $last "ssr_reconstruction_raw_resolved_aliased"
    $reconstructionCurrentHdrSourceEnabled = Get-UIntMetric $last "ssr_reconstruction_current_hdr_source_enabled"
    $reconstructionCurrentHdrRadianceFilterEnabled = Get-UIntMetric $last "ssr_reconstruction_current_hdr_radiance_filter_enabled"
    $reconstructionCurrentHdrMipLevels = Get-UIntMetric $last "ssr_reconstruction_current_hdr_mip_levels"
    $reconstructionCurrentHdrMipChainReady = Get-UIntMetric $last "ssr_reconstruction_current_hdr_mip_chain_ready"
    $fallbackBlendRequested = Get-UIntMetric $last "ssr_fallback_blend_requested"
    $fallbackBlendActive = Get-UIntMetric $last "ssr_fallback_blend_active"
    $fallbackBlendContractVersion = Get-UIntMetric $last "ssr_fallback_blend_contract_version"
    $fallbackBlendResolvedPixels = Get-UIntMetric $last "ssr_fallback_blend_resolved_pixels"
    $fallbackBlendPartialPixels = Get-UIntMetric $last "ssr_fallback_blend_partial_pixels"
    $fallbackBlendHighTrustPixels = Get-UIntMetric $last "ssr_fallback_blend_high_trust_pixels"
    $fallbackBlendAveragePermille = Get-UIntMetric $last "ssr_fallback_blend_average_permille"
    $reconstructionDeferredConsumerContractVersion = Get-UIntMetric $last "ssr_reconstruction_deferred_consumer_contract_version"
    $reconstructionDeferredReceiverReprojectionEnabled = Get-UIntMetric $last "ssr_reconstruction_deferred_receiver_reprojection_enabled"
    $reconstructionDeferredValidatedBilinearEnabled = Get-UIntMetric $last "ssr_reconstruction_deferred_validated_bilinear_enabled"
    $reconstructionDeferredHistoryTapCount = Get-UIntMetric $last "ssr_reconstruction_deferred_history_tap_count"
    $reconstructionDeferredDepthRejectEnabled = Get-UIntMetric $last "ssr_reconstruction_deferred_depth_reject_enabled"
    $reconstructionDeferredNormalRejectEnabled = Get-UIntMetric $last "ssr_reconstruction_deferred_normal_reject_enabled"
    $reconstructionDeferredRoughnessRejectEnabled = Get-UIntMetric $last "ssr_reconstruction_deferred_roughness_reject_enabled"
    $reconstructionDeferredMetadataDescriptorBound = Get-UIntMetric $last "ssr_reconstruction_deferred_metadata_descriptor_bound"
    $reconstructionResolvedMetadataAliased = Get-UIntMetric $last "ssr_reconstruction_resolved_metadata_aliased"
    $probeFallback = Get-UIntMetric $last "ssr_reflection_probe_fallback_enabled"
    $sceneColorHistoryRequested = Get-UIntMetric $last "ssr_scene_color_history_requested"
    $sceneColorHistoryDescriptorBound = Get-UIntMetric $last "ssr_scene_color_history_descriptor_bound"
    $sceneColorHistoryReady = Get-UIntMetric $last "ssr_scene_color_history_ready"
    $sceneColorHistoryActive = Get-UIntMetric $last "ssr_scene_color_history_active"
    $sceneColorHistoryFallbackReason = Get-UIntMetric $last "ssr_scene_color_history_fallback_reason"
    $sceneColorHistorySourceValid = Get-UIntMetric $last "ssr_scene_color_history_source_valid"
    $sceneColorHistoryCurrentImageIndex = Get-UIntMetric $last "ssr_scene_color_history_current_image_index"
    $sceneColorHistorySourceImageIndex = Get-UIntMetric $last "ssr_scene_color_history_source_image_index"
    $sceneColorHistoryFrameAge = Get-UIntMetric $last "ssr_scene_color_history_frame_age"
    $radianceSource = Get-UIntMetric $last "ssr_radiance_source"
    $temporalSsrReady = Get-UIntMetric $last "temporal_consumer_ssr_ready"
    $temporalSsrActive = Get-UIntMetric $last "temporal_consumer_ssr_active"
    $ssrStrength = [double]$last.ssr_strength
    $ssrRayLength = [double]$last.ssr_ray_length
    $ssrThickness = [double]$last.ssr_thickness
    $ssrStepCount = Get-UIntMetric $last "ssr_step_count"
    $holeDiagnosticsRequested = Get-UIntMetric $last "ssr_hole_diagnostics_requested"
    $holeDiagnosticsActive = Get-UIntMetric $last "ssr_hole_diagnostics_active"
    $holeDiagnosticsReadbackValid = Get-UIntMetric $last "ssr_hole_diagnostics_readback_valid"
    $holeDiagnosticsContractVersion = Get-UIntMetric $last "ssr_hole_diagnostics_contract_version"
    $holeDiagnosticsPixelCount = Get-UIntMetric $last "ssr_hole_diagnostics_pixel_count"
    $holeDiagnosticsRawHitPixels = Get-UIntMetric $last "ssr_hole_diagnostics_raw_hit_pixels"
    $holeDiagnosticsRawHighConfidencePixels = Get-UIntMetric $last "ssr_hole_diagnostics_raw_high_confidence_pixels"
    $holeDiagnosticsTemporalValidPixels = Get-UIntMetric $last "ssr_hole_diagnostics_temporal_valid_pixels"
    $holeDiagnosticsResolvedValidPixels = Get-UIntMetric $last "ssr_hole_diagnostics_resolved_valid_pixels"
    $holeDiagnosticsIsolatedRawHitPixels = Get-UIntMetric $last "ssr_hole_diagnostics_isolated_raw_hit_pixels"
    $holeDiagnosticsCenterMissNeighborHitPixels = Get-UIntMetric $last "ssr_hole_diagnostics_center_miss_neighbor_hit_pixels"
    $holeDiagnosticsResolvedHolePixels = Get-UIntMetric $last "ssr_hole_diagnostics_resolved_hole_pixels"
    $holeDiagnosticsRawHitTemporalRejectedPixels = Get-UIntMetric $last "ssr_hole_diagnostics_raw_hit_temporal_rejected_pixels"
    $holeDiagnosticsRawHitSpatialRejectedPixels = Get-UIntMetric $last "ssr_hole_diagnostics_raw_hit_spatial_rejected_pixels"
    $holeDiagnosticsTemporalMissCarriedPixels = Get-UIntMetric $last "ssr_hole_diagnostics_temporal_miss_carried_pixels"
    $validStaticDiagnosticRows = @(
        $rows | Where-Object {
            [uint32]$_.ssr_hole_diagnostics_readback_valid -eq 1 -and
                [uint32]$_.ssr_hole_diagnostics_pixel_count -gt 0
        }
    )
    $staticPixelCountRange =
        Get-UIntRangeMetric $validStaticDiagnosticRows "ssr_hole_diagnostics_pixel_count"
    $staticRawHitRange =
        Get-UIntRangeMetric $validStaticDiagnosticRows "ssr_hole_diagnostics_raw_hit_pixels"
    $staticHighConfidenceRange =
        Get-UIntRangeMetric $validStaticDiagnosticRows "ssr_hole_diagnostics_raw_high_confidence_pixels"
    $staticTemporalValidRange =
        Get-UIntRangeMetric $validStaticDiagnosticRows "ssr_hole_diagnostics_temporal_valid_pixels"
    $staticResolvedValidRange =
        Get-UIntRangeMetric $validStaticDiagnosticRows "ssr_hole_diagnostics_resolved_valid_pixels"
    $staticRawHitDeltaPermille =
        Get-PermilleDelta $staticRawHitRange.Delta $staticPixelCountRange.Maximum
    $staticHighConfidenceDeltaPermille =
        Get-PermilleDelta $staticHighConfidenceRange.Delta $staticPixelCountRange.Maximum
    $staticTemporalValidDeltaPermille =
        Get-PermilleDelta $staticTemporalValidRange.Delta $staticPixelCountRange.Maximum
    $staticResolvedValidDeltaPermille =
        Get-PermilleDelta $staticResolvedValidRange.Delta $staticPixelCountRange.Maximum
    $receiverAuditRequested = Get-UIntMetric $last "reflection_probe_receiver_audit_requested"
    $receiverAuditProductionBlend = Get-UIntMetric $last "reflection_probe_receiver_audit_production_blend"
    $receiverAuditIndependentIblEnergy = Get-UIntMetric $last "reflection_probe_receiver_audit_independent_ibl_energy"
    $receiverAuditPositiveWeightMask = Get-UIntMetric $last "reflection_probe_receiver_audit_positive_weight_mask"
    $receiverAuditReadyCubemapMask = Get-UIntMetric $last "reflection_probe_receiver_audit_ready_cubemap_mask"
    $receiverAuditBoxProjectionHitMask = Get-UIntMetric $last "reflection_probe_receiver_audit_box_projection_hit_mask"
    $receiverAuditDominantSlot = [int32]$last.reflection_probe_receiver_audit_dominant_slot
    $receiverAuditTotalWeight = [double]$last.reflection_probe_receiver_audit_total_weight
    $receiverAuditLocalCoverage = [double]$last.reflection_probe_receiver_audit_local_coverage
    $receiverAuditDominantWeight = [double]$last.reflection_probe_receiver_audit_dominant_normalized_weight
    $receiverAuditCubemapWeight = [double]$last.reflection_probe_receiver_audit_local_cubemap_weight
    $receiverAuditRoughness = [double]$last.reflection_probe_receiver_audit_roughness
    $receiverAuditLods = @(
        [double]$last.reflection_probe_receiver_audit_lod_0,
        [double]$last.reflection_probe_receiver_audit_lod_1,
        [double]$last.reflection_probe_receiver_audit_lod_2,
        [double]$last.reflection_probe_receiver_audit_lod_3
    )
    $capturedSceneNeutralTintMask = Get-UIntMetric $last "reflection_probe_captured_scene_neutral_tint_mask"
    $frameGraphIssues = Get-UIntMetric $last "framegraph_validation_issues"
    $gpuTotalAverage = [double](
        $rows | Measure-Object gpu_total_recorded_ms -Average
    ).Average
    $gpuTotalMaximum = [double](
        $rows | Measure-Object gpu_total_recorded_ms -Maximum
    ).Maximum
    $gpuMainAverage = [double](
        $rows | Measure-Object gpu_main_ms -Average
    ).Average
    $validationDiagnostics = @()
    foreach ($path in @($stdoutPath, $stderrPath)) {
        if (Test-Path -LiteralPath $path) {
            $validationDiagnostics += @(
                Select-String `
                    -LiteralPath $path `
                    -Pattern "\[Vulkan Validation\]|VUID-|validation error" `
                    -CaseSensitive:$false
            )
        }
    }

    $checks = @(
        (New-Check "frame graph validation" ($frameGraphIssues -eq 0) $frameGraphIssues "0"),
        (New-Check "Vulkan validation diagnostics" ($validationDiagnostics.Count -eq 0) $validationDiagnostics.Count "0"),
        (New-Check "GPU timestamp coverage" ($gpuTotalMaximum -gt 0.0) $gpuTotalMaximum ">0 ms"),
        (New-Check "reflection probe fallback remains explicit" ($probeFallback -eq 1) $probeFallback "1")
    )
    $hitValidationExpected = !(
        $Environment.ContainsKey("SE_SSR_HIT_VALIDATION") -and
        $Environment["SE_SSR_HIT_VALIDATION"] -eq "0"
    )
    $hitValidationActiveExpected = $hitValidationExpected -and $ssrEnabled -eq 1
    $checks += New-Check "SSR hit-validation request resolves" `
        ($hitValidationRequested -eq $(if ($hitValidationExpected) { 1 } else { 0 })) `
        $hitValidationRequested $(if ($hitValidationExpected) { "1" } else { "0" })
    $checks += New-Check "SSR hit-validation contract resolves" `
        (
            $hitValidationActive -eq $(if ($hitValidationActiveExpected) { 1 } else { 0 }) -and
            $hitValidationContractVersion -eq $(if ($hitValidationActiveExpected) { 1 } else { 0 }) -and
            $hitNormalValidationEnabled -eq $(if ($hitValidationActiveExpected) { 1 } else { 0 }) -and
            $signedDepthValidationEnabled -eq $(if ($hitValidationActiveExpected) { 1 } else { 0 }) -and
            $hitFootprintTapCount -eq $(if ($hitValidationActiveExpected) { 8 } else { 0 })
        ) `
        "active/version/normal/depth/taps=$hitValidationActive/$hitValidationContractVersion/$hitNormalValidationEnabled/$signedDepthValidationEnabled/$hitFootprintTapCount" `
        $(if ($hitValidationActiveExpected) { "1/1/1/1/8" } else { "0/0/0/0/0" })
    $checks += New-Check "SSR origin bias budget resolves" `
        (
            (!$hitValidationActiveExpected -and $originBiasMinimumPixels -eq 0.0 -and $originBiasMaximumPixels -eq 0.0) -or
            ($hitValidationActiveExpected -and $originBiasMinimumPixels -eq 2.0 -and $originBiasMaximumPixels -eq 6.0)
        ) `
        "$originBiasMinimumPixels/$originBiasMaximumPixels" `
        $(if ($hitValidationActiveExpected) { "2/6" } else { "0/0" })
    $reconstructionExpected = $ssrEnabled -eq 1 -and $hizActive -eq 1
    $checks += New-Check "SSR reconstruction contract resolves" `
        (
            $reconstructionRequested -eq $(if ($ssrEnabled -eq 1) { 1 } else { 0 }) -and
            $reconstructionActive -eq $(if ($reconstructionExpected) { 1 } else { 0 }) -and
            $reconstructionTargetsAllocated -eq $(if ($pyramidImageCount -gt 1) { 1 } else { 0 }) -and
            $reconstructionDescriptorSetsReady -eq $(if ($pyramidImageCount -gt 1) { 1 } else { 0 }) -and
            $reconstructionImageCount -eq $pyramidImageCount
        ) `
        "requested/active=$reconstructionRequested/$reconstructionActive,targets/descriptors=$reconstructionTargetsAllocated/$reconstructionDescriptorSetsReady,images=$reconstructionImageCount" `
        $(if ($pyramidImageCount -gt 1) { "1/$([int]$reconstructionExpected),1/1,images=$pyramidImageCount" } else { "0/0,0/0,images=$pyramidImageCount" })
    if ($reconstructionExpected) {
        $spatialVarianceClampExpected = !(
            ($Environment.ContainsKey("SE_SSR_SPATIAL_VARIANCE_CLAMP") -and
                $Environment["SE_SSR_SPATIAL_VARIANCE_CLAMP"] -eq "0") -or
            ($Environment.ContainsKey("SE_SSR_SPATIAL_VARIANCE_CLAMP_OFF") -and
                $Environment["SE_SSR_SPATIAL_VARIANCE_CLAMP_OFF"] -eq "1")
        )
        $temporalHistoryLockExpected = !(
            ($Environment.ContainsKey("SE_SSR_TEMPORAL_HISTORY_LOCK") -and
                $Environment["SE_SSR_TEMPORAL_HISTORY_LOCK"] -eq "0") -or
            ($Environment.ContainsKey("SE_SSR_TEMPORAL_HISTORY_LOCK_OFF") -and
                $Environment["SE_SSR_TEMPORAL_HISTORY_LOCK_OFF"] -eq "1")
        )
        $currentHdrSourceExpected =
            $Environment.ContainsKey("SE_SSR_CURRENT_HDR_SOURCE") -and
            $Environment["SE_SSR_CURRENT_HDR_SOURCE"] -eq "1" -and
            !(
                ($Environment.ContainsKey("SE_SSR_CURRENT_HDR_SOURCE_OFF") -and
                    $Environment["SE_SSR_CURRENT_HDR_SOURCE_OFF"] -eq "1") -or
                ($Environment.ContainsKey("SE_SSR_SCENE_COLOR_OFF") -and
                    $Environment["SE_SSR_SCENE_COLOR_OFF"] -eq "1")
            )
        $temporalMissRejectExpected = !(
            ($Environment.ContainsKey("SE_SSR_TEMPORAL_MISS_HISTORY_REJECT") -and
                $Environment["SE_SSR_TEMPORAL_MISS_HISTORY_REJECT"] -eq "0") -or
            ($Environment.ContainsKey("SE_SSR_TEMPORAL_MISS_HISTORY_REJECT_OFF") -and
                $Environment["SE_SSR_TEMPORAL_MISS_HISTORY_REJECT_OFF"] -eq "1")
        )
        $checks += New-Check "SSR temporal reconstruction contract" `
            ($reconstructionTemporalContractVersion -eq 13 -and $reconstructionTemporalMissHistoryRejectEnabled -eq [int]$temporalMissRejectExpected -and $reconstructionTemporalPreviousViewDepthEnabled -eq 1 -and $reconstructionTemporalHistoryLockEnabled -eq [int]$temporalHistoryLockExpected -and $reconstructionSpatialCenterHitGateEnabled -eq 1 -and $reconstructionRawResolvedAliased -eq 0 -and $reconstructionCurrentHdrSourceEnabled -eq [int]$currentHdrSourceExpected -and $reconstructionCurrentHdrRadianceFilterEnabled -eq [int]$currentHdrSourceExpected -and $reconstructionCurrentHdrMipLevels -gt 1 -and $reconstructionCurrentHdrMipChainReady -eq 1) `
            "version=$reconstructionTemporalContractVersion,missReject=$reconstructionTemporalMissHistoryRejectEnabled,previousViewDepth=$reconstructionTemporalPreviousViewDepthEnabled,historyLock=$reconstructionTemporalHistoryLockEnabled,centerGate=$reconstructionSpatialCenterHitGateEnabled,rawResolvedAliased=$reconstructionRawResolvedAliased,currentHdr=$reconstructionCurrentHdrSourceEnabled,filter=$reconstructionCurrentHdrRadianceFilterEnabled,mipLevels=$reconstructionCurrentHdrMipLevels,mipReady=$reconstructionCurrentHdrMipChainReady" `
            "13/$([int]$temporalMissRejectExpected)/1/$([int]$temporalHistoryLockExpected)/1/0/$([int]$currentHdrSourceExpected)/$([int]$currentHdrSourceExpected)/mip>1/1"
        $checks += New-Check "SSR spatial variance clamp contract" `
            ($reconstructionSpatialCenterHitGateEnabled -eq 1 -and
                $reconstructionSpatialVarianceClampEnabled -eq [int]$spatialVarianceClampExpected -and
                $reconstructionSpatialSupportTapCount -eq 13) `
            "centerGate=$reconstructionSpatialCenterHitGateEnabled,varianceClamp=$reconstructionSpatialVarianceClampEnabled,supportTaps=$reconstructionSpatialSupportTapCount" `
            "1/$([int]$spatialVarianceClampExpected)/13"
        $fallbackBlendExpected = !(
            ($Environment.ContainsKey("SE_SSR_PROBE_FALLBACK_BLEND") -and
                $Environment["SE_SSR_PROBE_FALLBACK_BLEND"] -eq "0") -or
            ($Environment.ContainsKey("SE_SSR_PROBE_FALLBACK_BLEND_OFF") -and
                $Environment["SE_SSR_PROBE_FALLBACK_BLEND_OFF"] -eq "1")
        )
        $checks += New-Check "SSR probe fallback blend contract" `
            ($fallbackBlendRequested -eq [int]$fallbackBlendExpected -and
                $fallbackBlendActive -eq [int]$fallbackBlendExpected -and
                $fallbackBlendContractVersion -eq [int]$fallbackBlendExpected) `
            "requested/active/version=$fallbackBlendRequested/$fallbackBlendActive/$fallbackBlendContractVersion" `
            "$([int]$fallbackBlendExpected)/$([int]$fallbackBlendExpected)/$([int]$fallbackBlendExpected)"
        if ($fallbackBlendExpected -and $currentHdrSourceExpected) {
            $checks += New-Check "SSR fallback blend diagnostics recorded" `
                ($fallbackBlendAveragePermille -le 1000 -and
                    $fallbackBlendResolvedPixels -ge 0 -and
                    $fallbackBlendPartialPixels -ge 0 -and
                    $fallbackBlendHighTrustPixels -ge 0 -and
                    $fallbackBlendPartialPixels -le $fallbackBlendResolvedPixels -and
                    $fallbackBlendHighTrustPixels -le $fallbackBlendResolvedPixels) `
                "resolved=$fallbackBlendResolvedPixels,partial=$fallbackBlendPartialPixels,highTrust=$fallbackBlendHighTrustPixels,avgPermille=$fallbackBlendAveragePermille" `
                "non-negative counts, avg<=1000"
        }
        $deferredReceiverExpected = !(
            ($Environment.ContainsKey("SE_SSR_DEFERRED_REPROJECTION") -and
                $Environment["SE_SSR_DEFERRED_REPROJECTION"] -eq "0") -or
            ($Environment.ContainsKey("SE_SSR_DEFERRED_REPROJECTION_OFF") -and
                $Environment["SE_SSR_DEFERRED_REPROJECTION_OFF"] -eq "1")
        )
        $deferredReceiverActiveExpected = $reconstructionExpected -and $deferredReceiverExpected
        $checks += New-Check "SSR Deferred receiver reprojection contract" `
            ($reconstructionDeferredConsumerContractVersion -eq 7 -and
                $reconstructionDeferredReceiverReprojectionEnabled -eq [int]$deferredReceiverActiveExpected -and
                $reconstructionDeferredValidatedBilinearEnabled -eq [int]$deferredReceiverActiveExpected -and
                $reconstructionDeferredHistoryTapCount -eq $(if ($deferredReceiverActiveExpected) { 4 } else { 0 }) -and
                $reconstructionDeferredDepthRejectEnabled -eq [int]$deferredReceiverActiveExpected -and
                $reconstructionDeferredNormalRejectEnabled -eq [int]$deferredReceiverActiveExpected -and
                $reconstructionDeferredRoughnessRejectEnabled -eq [int]$deferredReceiverActiveExpected -and
                $reconstructionDeferredMetadataDescriptorBound -eq 1 -and
                $reconstructionResolvedMetadataAliased -eq 0) `
            "version=$reconstructionDeferredConsumerContractVersion,reproject=$reconstructionDeferredReceiverReprojectionEnabled,validatedBilinear=$reconstructionDeferredValidatedBilinearEnabled,taps=$reconstructionDeferredHistoryTapCount,depth/normal/roughness=$reconstructionDeferredDepthRejectEnabled/$reconstructionDeferredNormalRejectEnabled/$reconstructionDeferredRoughnessRejectEnabled,metadataBound=$reconstructionDeferredMetadataDescriptorBound,alias=$reconstructionResolvedMetadataAliased" `
            "7/$([int]$deferredReceiverActiveExpected)/$([int]$deferredReceiverActiveExpected)/$(if ($deferredReceiverActiveExpected) { 4 } else { 0 })/$([int]$deferredReceiverActiveExpected)/$([int]$deferredReceiverActiveExpected)/$([int]$deferredReceiverActiveExpected)/1/0"
        $checks += New-Check "SSR reconstruction dispatches recorded" `
            ($reconstructionTraceDispatches -eq 1 -and $reconstructionTemporalDispatches -eq 1 -and $reconstructionSpatialDispatches -eq 1 -and $reconstructionHistoryCopies -eq ($pyramidImageCount - 1)) `
            "trace/temporal/spatial=$reconstructionTraceDispatches/$reconstructionTemporalDispatches/$reconstructionSpatialDispatches,copies=$reconstructionHistoryCopies" `
            "1/1/1/$($pyramidImageCount - 1)"
        $expectedReconstructionMemoryBytes = [uint64]$pyramidWidth * $pyramidHeight * 8 * 4 * $pyramidImageCount
        $checks += New-Check "SSR reconstruction memory contract" `
            ($reconstructionMemoryBytes -eq $expectedReconstructionMemoryBytes -and $reconstructionMemoryBytes -gt 0) `
            $reconstructionMemoryBytes $expectedReconstructionMemoryBytes
        if ($holeDiagnosticsRequested -eq 1) {
            $checks += New-Check "SSR hole diagnostics counts are bounded" `
                (
                    $holeDiagnosticsReadbackValid -eq 1 -and
                    $holeDiagnosticsPixelCount -gt 0 -and
                    $holeDiagnosticsRawHitPixels -le $holeDiagnosticsPixelCount -and
                    $holeDiagnosticsRawHighConfidencePixels -le $holeDiagnosticsRawHitPixels -and
                    $holeDiagnosticsTemporalValidPixels -le $holeDiagnosticsPixelCount -and
                    $holeDiagnosticsResolvedValidPixels -le $holeDiagnosticsPixelCount -and
                    $holeDiagnosticsIsolatedRawHitPixels -le $holeDiagnosticsRawHitPixels -and
                    $holeDiagnosticsCenterMissNeighborHitPixels -le $holeDiagnosticsPixelCount -and
                    $holeDiagnosticsResolvedHolePixels -le $holeDiagnosticsPixelCount
                ) `
                "pixels=$holeDiagnosticsPixelCount,raw=$holeDiagnosticsRawHitPixels,high=$holeDiagnosticsRawHighConfidencePixels,temporal=$holeDiagnosticsTemporalValidPixels,resolved=$holeDiagnosticsResolvedValidPixels,isolated=$holeDiagnosticsIsolatedRawHitPixels,missNeighbor=$holeDiagnosticsCenterMissNeighborHitPixels,holes=$holeDiagnosticsResolvedHolePixels" `
                "all within frame/raw bounds"
            $checks += New-Check "SSR reliability diagnostics are per-pixel categories" `
                (
                    $holeDiagnosticsRawHitTemporalRejectedPixels -le $holeDiagnosticsRawHitPixels -and
                    $holeDiagnosticsRawHitSpatialRejectedPixels -le $holeDiagnosticsRawHitPixels -and
                    $holeDiagnosticsRawHitSpatialRejectedPixels -le (
                        [int64]$holeDiagnosticsRawHitPixels -
                        [int64]$holeDiagnosticsRawHitTemporalRejectedPixels
                    ) -and
                    $holeDiagnosticsTemporalMissCarriedPixels -le $holeDiagnosticsTemporalValidPixels
                ) `
                "temporalRejected=$holeDiagnosticsRawHitTemporalRejectedPixels,spatialRejected=$holeDiagnosticsRawHitSpatialRejectedPixels,missCarried=$holeDiagnosticsTemporalMissCarriedPixels,raw=$holeDiagnosticsRawHitPixels,temporal=$holeDiagnosticsTemporalValidPixels" `
                "category counts bounded by matching producer stage"
            $checks += New-Check "SSR fallback diagnostics match resolved coverage" `
                (
                    $fallbackBlendResolvedPixels -eq $holeDiagnosticsResolvedValidPixels -and
                    $fallbackBlendPartialPixels -le $fallbackBlendResolvedPixels -and
                    $fallbackBlendHighTrustPixels -le $fallbackBlendResolvedPixels -and
                    $fallbackBlendAveragePermille -le 1000
                ) `
                "resolved=$holeDiagnosticsResolvedValidPixels,fallbackResolved=$fallbackBlendResolvedPixels,partial=$fallbackBlendPartialPixels,highTrust=$fallbackBlendHighTrustPixels,avg=$fallbackBlendAveragePermille" `
                "resolved coverage and fallback counts agree"
            $checks += New-Check "SSR static diagnostic readback has repeated frames" `
                ($staticRawHitRange.Count -ge 4) `
                "validFrames=$($staticRawHitRange.Count)" `
                ">=4"
            $checks += New-Check "SSR static raw-hit coverage drift is bounded" `
                ($staticRawHitDeltaPermille -le 5) `
                "raw=$($staticRawHitRange.Minimum)..$($staticRawHitRange.Maximum),delta=$($staticRawHitRange.Delta),permille=$staticRawHitDeltaPermille" `
                "<=5 permille of pixels"
            $checks += New-Check "SSR static resolved coverage drift is bounded" `
                ($staticResolvedValidDeltaPermille -le 1) `
                "resolved=$($staticResolvedValidRange.Minimum)..$($staticResolvedValidRange.Maximum),delta=$($staticResolvedValidRange.Delta),permille=$staticResolvedValidDeltaPermille" `
                "<=1 permille of pixels"
            if ($currentHdrSourceExpected) {
                $checks += New-Check "current-HDR source with miss-reject remains bounded" `
                    (
                        $holeDiagnosticsRawHitPixels -gt 0 -and
                        $holeDiagnosticsRawHitTemporalRejectedPixels -le
                            $holeDiagnosticsRawHitPixels
                    ) `
                    "raw=$holeDiagnosticsRawHitPixels,temporal=$holeDiagnosticsTemporalValidPixels,resolved=$holeDiagnosticsResolvedValidPixels,missReject=$reconstructionTemporalMissHistoryRejectEnabled,temporalRejected=$holeDiagnosticsRawHitTemporalRejectedPixels" `
                    "raw>0 and temporalRejected<=raw; current-HDR is an experimental radiance source"
            } else {
                $checks += New-Check "production SSR excludes current-HDR radiance" `
                    ($reconstructionCurrentHdrSourceEnabled -eq 0 -and
                        $radianceSource -ne 3) `
                    "currentHdr=$reconstructionCurrentHdrSourceEnabled,source=$radianceSource" `
                    "0/source!=3"
                $checks += New-Check "current-HDR disabled does not seed temporal SSR radiance" `
                    (
                        $holeDiagnosticsTemporalValidPixels -eq 0 -and
                        $holeDiagnosticsResolvedValidPixels -eq 0 -and
                        $holeDiagnosticsTemporalMissCarriedPixels -eq 0 -and
                        $holeDiagnosticsRawHitTemporalRejectedPixels -eq
                            $holeDiagnosticsRawHitPixels
                    ) `
                    "raw=$holeDiagnosticsRawHitPixels,temporal=$holeDiagnosticsTemporalValidPixels,resolved=$holeDiagnosticsResolvedValidPixels,missCarried=$holeDiagnosticsTemporalMissCarriedPixels,temporalRejected=$holeDiagnosticsRawHitTemporalRejectedPixels" `
                    "temporal/resolved/miss-carried=0 and temporalRejected=raw"
            }
        }
    } else {
        $checks += New-Check "SSR reconstruction disabled path submits no dispatches" `
            ($reconstructionTraceDispatches -eq 0 -and $reconstructionTemporalDispatches -eq 0 -and $reconstructionSpatialDispatches -eq 0) `
            "trace/temporal/spatial=$reconstructionTraceDispatches/$reconstructionTemporalDispatches/$reconstructionSpatialDispatches" "0/0/0"
    }
    if ($Environment.ContainsKey("SE_REFLECTION_RECEIVER_AUDIT")) {
        $legacyBlendExpected = $Environment.ContainsKey(
            "SE_REFLECTION_PROBE_LEGACY_BLEND"
        ) -and $Environment["SE_REFLECTION_PROBE_LEGACY_BLEND"] -eq "1"
        $expectedProductionBlend = if ($legacyBlendExpected) { 0 } else { 1 }
        $legacyEnergyExpected = $Environment.ContainsKey(
            "SE_REFLECTION_PROBE_LEGACY_ENERGY_SCALE"
        ) -and $Environment["SE_REFLECTION_PROBE_LEGACY_ENERGY_SCALE"] -eq "1"
        $expectedIndependentEnergy = if ($legacyEnergyExpected) { 0 } else { 1 }
        $checks += New-Check "receiver reflection audit active" ($receiverAuditRequested -eq 1) $receiverAuditRequested "1"
        $checks += New-Check "receiver reflection blend mode" ($receiverAuditProductionBlend -eq $expectedProductionBlend) $receiverAuditProductionBlend $expectedProductionBlend
        $checks += New-Check "receiver reflection IBL energy mode" ($receiverAuditIndependentIblEnergy -eq $expectedIndependentEnergy) $receiverAuditIndependentIblEnergy $expectedIndependentEnergy
        $checks += New-Check "receiver has weighted local probes" ($receiverAuditPositiveWeightMask -ne 0 -and $receiverAuditTotalWeight -gt 0.0) "mask=$receiverAuditPositiveWeightMask,total=$receiverAuditTotalWeight" "nonzero"
        $checks += New-Check "receiver local probes have cubemap resources" (($receiverAuditReadyCubemapMask -band $receiverAuditPositiveWeightMask) -eq $receiverAuditPositiveWeightMask) "weighted=$receiverAuditPositiveWeightMask,ready=$receiverAuditReadyCubemapMask" "all weighted probes ready"
        $checks += New-Check "receiver box projection resolves" (($receiverAuditBoxProjectionHitMask -band $receiverAuditPositiveWeightMask) -eq $receiverAuditPositiveWeightMask) "weighted=$receiverAuditPositiveWeightMask,hits=$receiverAuditBoxProjectionHitMask" "all weighted probes hit"
        $checks += New-Check "receiver dominant probe resolves" ($receiverAuditDominantSlot -ge 0 -and $receiverAuditDominantWeight -gt 0.5) "slot=$receiverAuditDominantSlot,weight=$receiverAuditDominantWeight" "valid slot and >0.5"
        $checks += New-Check "receiver cubemap roughness filter active" ($receiverAuditCubemapWeight -gt 0.99 -and $receiverAuditRoughness -gt 0.0 -and ($receiverAuditLods | Measure-Object -Maximum).Maximum -gt 0.0) "cubemap=$receiverAuditCubemapWeight,roughness=$receiverAuditRoughness,lods=$($receiverAuditLods -join '/')" "cubemap >0.99, roughness/lod >0"
        if ($legacyBlendExpected) {
            $checks += New-Check "legacy receiver control remains diluted" ($receiverAuditLocalCoverage -gt 0.0 -and $receiverAuditLocalCoverage -lt 0.9) $receiverAuditLocalCoverage "0 < coverage < 0.9"
        } else {
            $checks += New-Check "production receiver gets full local coverage" ($receiverAuditLocalCoverage -gt 0.99 -and $receiverAuditLocalCoverage -le 1.0) $receiverAuditLocalCoverage "0.99 < coverage <= 1"
        }
        if ($Scene -like "LightingShowcase*") {
            $checks += New-Check "captured-scene probe avoids double tint" ($capturedSceneNeutralTintMask -ne 0) $capturedSceneNeutralTintMask "nonzero"
        }
    }
    switch ($Mode) {
    "hiz" {
        $checks += New-Check "SSR resolve active" ($ssrEnabled -eq 1 -and $colorResolve -eq 1) "enabled=$ssrEnabled,color=$colorResolve" "1/1"
        $checks += New-Check "SSR trace parameters active" ($ssrStrength -gt 0.0 -and $ssrRayLength -gt 0.0 -and $ssrThickness -gt 0.0 -and $ssrStepCount -gt 0) "strength=$ssrStrength,ray=$ssrRayLength,thickness=$ssrThickness,steps=$ssrStepCount" "all > 0"
        if ($VerifyHoleDiagnostics) {
            $checks += New-Check "SSR hole diagnostics contract active" ($holeDiagnosticsRequested -eq 1 -and $holeDiagnosticsActive -eq 1 -and $holeDiagnosticsContractVersion -eq 2) "requested/active/version=$holeDiagnosticsRequested/$holeDiagnosticsActive/$holeDiagnosticsContractVersion" "1/1/2"
            $checks += New-Check "SSR hole diagnostics readback valid" ($holeDiagnosticsReadbackValid -eq 1 -and $holeDiagnosticsPixelCount -gt 0) "valid=$holeDiagnosticsReadbackValid,pixels=$holeDiagnosticsPixelCount" "1,pixels>0"
        }
        if ($currentHdrSourceExpected) {
            $checks += New-Check "current-HDR source radiance active" `
                ($sceneColorHistoryRequested -eq 1 -and
                    $sceneColorHistoryDescriptorBound -eq 1 -and
                    $sceneColorHistoryReady -eq 1 -and
                    $sceneColorHistoryActive -eq 1 -and
                    $sceneColorHistoryFallbackReason -eq 0 -and
                    $reconstructionCurrentHdrSourceEnabled -eq 1 -and
                    $reconstructionCurrentHdrRadianceFilterEnabled -eq 1 -and
                    $reconstructionCurrentHdrMipChainReady -eq 1 -and
                    $radianceSource -eq 3) `
                "requested=$sceneColorHistoryRequested,bound=$sceneColorHistoryDescriptorBound,ready=$sceneColorHistoryReady,active=$sceneColorHistoryActive,fallback=$sceneColorHistoryFallbackReason,currentHdr=$reconstructionCurrentHdrSourceEnabled/filter=$reconstructionCurrentHdrRadianceFilterEnabled,mipReady=$reconstructionCurrentHdrMipChainReady,source=$radianceSource" `
                "1/1/1/1/0/1/1/1/3"
        } else {
            $checks += New-Check "completed scene-color history radiance active" `
                ($sceneColorHistoryRequested -eq 1 -and
                    $sceneColorHistoryDescriptorBound -eq 1 -and
                    $sceneColorHistoryReady -eq 1 -and
                    $sceneColorHistoryActive -eq 1 -and
                    $sceneColorHistoryFallbackReason -eq 0 -and
                    $radianceSource -eq 2) `
                "requested=$sceneColorHistoryRequested,bound=$sceneColorHistoryDescriptorBound,ready=$sceneColorHistoryReady,active=$sceneColorHistoryActive,fallback=$sceneColorHistoryFallbackReason,source=$radianceSource" `
                "1/1/1/1/0/2"
        }
        $checks += New-Check "scene-color history is previous submitted frame" ($sceneColorHistorySourceValid -eq 1 -and $sceneColorHistoryFrameAge -eq 1 -and $sceneColorHistoryCurrentImageIndex -lt $pyramidImageCount -and $sceneColorHistorySourceImageIndex -lt $pyramidImageCount) "valid=$sceneColorHistorySourceValid,current=$sceneColorHistoryCurrentImageIndex,source=$sceneColorHistorySourceImageIndex,age=$sceneColorHistoryFrameAge" "valid indices, age=1"
        $checks += New-Check "SSR temporal consumer active" ($temporalSsrReady -eq 1 -and $temporalSsrActive -eq 1) "ready=$temporalSsrReady,active=$temporalSsrActive" "1/1"
        $checks += New-Check "SSR inputs ready" ($inputsReady -eq 1) $inputsReady "1"
        $checks += New-Check "Hi-Z path active" ($hizRequested -eq 1 -and $hizActive -eq 1 -and $hizFallbackReason -eq 0 -and $fixedFallback -eq 0) "requested=$hizRequested,active=$hizActive,reason=$hizFallbackReason,fixed=$fixedFallback" "1/1/0/0"
        $checks += New-Check "depth pyramid contract" ($pyramidAllocated -eq 1 -and $pyramidReady -eq 1 -and $pyramidWidth -gt 0 -and $pyramidHeight -gt 0 -and $pyramidMipCount -gt 1 -and $pyramidImageCount -gt 0 -and $pyramidFormat -ne 0) "allocated=$pyramidAllocated,ready=$pyramidReady,size=${pyramidWidth}x${pyramidHeight},mips=$pyramidMipCount,images=$pyramidImageCount,format=$pyramidFormat" "allocated/ready, valid size, >1 mips, >0 images, defined format"
        [uint64]$expectedMipPixels = 0
        [uint32]$mipWidth = $pyramidWidth
        [uint32]$mipHeight = $pyramidHeight
        for ($mipIndex = 0; $mipIndex -lt $pyramidMipCount; ++$mipIndex) {
            $expectedMipPixels += [uint64]$mipWidth * [uint64]$mipHeight
            $mipWidth = [Math]::Max([uint32]1, $mipWidth -shr 1)
            $mipHeight = [Math]::Max([uint32]1, $mipHeight -shr 1)
        }
        [uint64]$expectedMemoryBytes = $expectedMipPixels * 4 * $pyramidImageCount
        $checks += New-Check "depth pyramid memory budget" ($pyramidMemoryBytes -eq $expectedMemoryBytes -and $pyramidMemoryBytes -gt 0) $pyramidMemoryBytes $expectedMemoryBytes
        $expectedMipMask = if ($pyramidMipCount -ge 64) {
            [uint64]::MaxValue
        } else {
            [uint64](([uint64]1 -shl $pyramidMipCount) - 1)
        }
        $checks += New-Check "all pyramid mips generated" ($buildDispatchCount -eq $pyramidMipCount -and $generatedMipMask -eq $expectedMipMask) "dispatches=$buildDispatchCount,mask=$generatedMipMask" "dispatches=$pyramidMipCount,mask=$expectedMipMask"
        $checks += New-Check "recorded producer commands" ($actualBuildDispatchMinimum -eq $pyramidMipCount -and $actualBuildDispatches -eq $pyramidMipCount -and $actualDescriptorBindMinimum -eq $pyramidMipCount -and $actualDescriptorBinds -eq $pyramidMipCount) "dispatches=$actualBuildDispatchMinimum..$actualBuildDispatches,binds=$actualDescriptorBindMinimum..$actualDescriptorBinds" "$pyramidMipCount..$pyramidMipCount/$pyramidMipCount..$pyramidMipCount"
        $checks += New-Check "deferred consumer draw" ($actualConsumerDrawMinimum -eq 1 -and $actualConsumerDraws -eq 1) "$actualConsumerDrawMinimum..$actualConsumerDraws" "1..1"
        $checks += New-Check "hierarchical traversal bounds" ($traversalMaxMip -eq ($pyramidMipCount - 1)) $traversalMaxMip ($pyramidMipCount - 1)
        $checks += New-Check "four-step crossing refinement active" ($refinement -eq 1 -and $refinementSteps -eq 4) "enabled=$refinement,steps=$refinementSteps" "1/4"
    }
    "fixed-step" {
        $checks += New-Check "SSR resolve remains active" ($ssrEnabled -eq 1 -and $colorResolve -eq 1) "enabled=$ssrEnabled,color=$colorResolve" "1/1"
        $checks += New-Check "fixed-step scene-color history radiance active" ($sceneColorHistoryRequested -eq 1 -and $sceneColorHistoryDescriptorBound -eq 1 -and $sceneColorHistoryReady -eq 1 -and $sceneColorHistoryActive -eq 1 -and $sceneColorHistoryFallbackReason -eq 0 -and $radianceSource -eq 2) "requested=$sceneColorHistoryRequested,bound=$sceneColorHistoryDescriptorBound,ready=$sceneColorHistoryReady,active=$sceneColorHistoryActive,fallback=$sceneColorHistoryFallbackReason,source=$radianceSource" "1/1/1/1/0/2"
        $checks += New-Check "fixed-step history age contract" ($sceneColorHistorySourceValid -eq 1 -and $sceneColorHistoryFrameAge -eq 1) "valid=$sceneColorHistorySourceValid,age=$sceneColorHistoryFrameAge" "1/1"
        $checks += New-Check "Hi-Z disabled fallback" ($hizRequested -eq 0 -and $hizActive -eq 0 -and $hizFallbackReason -eq 1 -and $fixedFallback -eq 1) "requested=$hizRequested,active=$hizActive,reason=$hizFallbackReason,fixed=$fixedFallback" "0/0/1/1"
        $checks += New-Check "fallback skips pyramid build" ($buildDispatchCount -eq 0 -and $generatedMipMask -eq 0) "dispatches=$buildDispatchCount,mask=$generatedMipMask" "0/0"
        $checks += New-Check "fallback records no Hi-Z commands" ($actualBuildDispatches -eq 0 -and $actualDescriptorBinds -eq 0 -and $actualConsumerDraws -eq 0) "dispatches=$actualBuildDispatches,binds=$actualDescriptorBinds,consumer=$actualConsumerDraws" "0/0/0"
        $checks += New-Check "refined fixed-step fallback retained" ($refinement -eq 1 -and $refinementSteps -eq 4) "enabled=$refinement,steps=$refinementSteps" "1/4"
    }
    "hiz-gbuffer" {
        $checks += New-Check "SSR GBuffer radiance control active" ($ssrEnabled -eq 1 -and $colorResolve -eq 1 -and $hizRequested -eq 1 -and $hizActive -eq 1) "enabled=$ssrEnabled,color=$colorResolve,hiz=$hizRequested/$hizActive" "1/1/1/1"
        if ($VerifyHoleDiagnostics) {
            $checks += New-Check "SSR hole diagnostics contract active" ($holeDiagnosticsRequested -eq 1 -and $holeDiagnosticsActive -eq 1 -and $holeDiagnosticsContractVersion -eq 2) "requested/active/version=$holeDiagnosticsRequested/$holeDiagnosticsActive/$holeDiagnosticsContractVersion" "1/1/2"
            $checks += New-Check "SSR hole diagnostics readback valid" ($holeDiagnosticsReadbackValid -eq 1 -and $holeDiagnosticsPixelCount -gt 0) "valid=$holeDiagnosticsReadbackValid,pixels=$holeDiagnosticsPixelCount" "1,pixels>0"
        }
        $checks += New-Check "scene-color history control disabled" ($sceneColorHistoryRequested -eq 0 -and $sceneColorHistoryDescriptorBound -eq 1 -and $sceneColorHistoryActive -eq 0 -and $sceneColorHistoryFallbackReason -eq 1 -and $radianceSource -eq 1) "requested=$sceneColorHistoryRequested,bound=$sceneColorHistoryDescriptorBound,active=$sceneColorHistoryActive,fallback=$sceneColorHistoryFallbackReason,source=$radianceSource" "0/1/0/1/1"
        $checks += New-Check "SSR temporal consumer inactive in control" ($temporalSsrActive -eq 0) $temporalSsrActive "0"
        $checks += New-Check "GBuffer control keeps Hi-Z commands" ($actualBuildDispatchMinimum -eq $pyramidMipCount -and $actualBuildDispatches -eq $pyramidMipCount -and $actualConsumerDrawMinimum -eq 1 -and $actualConsumerDraws -eq 1) "dispatches=$actualBuildDispatchMinimum..$actualBuildDispatches,consumer=$actualConsumerDrawMinimum..$actualConsumerDraws" "$pyramidMipCount..$pyramidMipCount/1..1"
    }
    "disabled" {
        $checks += New-Check "SSR disabled fallback" ($ssrEnabled -eq 0 -and $colorResolve -eq 0 -and $refinement -eq 0 -and $refinementSteps -eq 0) "enabled=$ssrEnabled,color=$colorResolve,refinement=$refinement,steps=$refinementSteps" "0/0/0/0"
        $checks += New-Check "disabled SSR has zero trace parameters" ($ssrStrength -eq 0.0 -and $ssrRayLength -eq 0.0 -and $ssrStepCount -eq 0) "strength=$ssrStrength,ray=$ssrRayLength,steps=$ssrStepCount" "0/0/0"
        $checks += New-Check "disabled SSR has no radiance consumer" ($sceneColorHistoryRequested -eq 0 -and $sceneColorHistoryActive -eq 0 -and $radianceSource -eq 0 -and $temporalSsrActive -eq 0) "requested=$sceneColorHistoryRequested,active=$sceneColorHistoryActive,source=$radianceSource,temporal=$temporalSsrActive" "0/0/0/0"
        $checks += New-Check "disabled SSR records no trace or build" ($hizRequested -eq 0 -and $hizActive -eq 0 -and $fixedFallback -eq 0 -and $buildDispatchCount -eq 0 -and $generatedMipMask -eq 0) "requested=$hizRequested,active=$hizActive,fixed=$fixedFallback,dispatches=$buildDispatchCount,mask=$generatedMipMask" "0/0/0/0/0"
        $checks += New-Check "disabled SSR submits no Hi-Z commands" ($actualBuildDispatches -eq 0 -and $actualDescriptorBinds -eq 0 -and $actualConsumerDraws -eq 0) "dispatches=$actualBuildDispatches,binds=$actualDescriptorBinds,consumer=$actualConsumerDraws" "0/0/0"
        $checks += New-Check "disabled SSR hole diagnostics inactive" ($holeDiagnosticsActive -eq 0 -and $holeDiagnosticsContractVersion -eq 0) "active/version=$holeDiagnosticsActive/$holeDiagnosticsContractVersion" "0/0"
    }
    default {
        throw "Unknown SSR lane mode: $Mode"
    }
    }

    $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
    $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
    return [pscustomobject]@{
        lane = $Name
        scene = $Scene
        executable = $Executable
        mode = $Mode
        csv = $csvPath
        log = $logPath
        verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
        passCount = $passCount
        failCount = $failCount
        metrics = [pscustomobject]@{
            ssrEnabled = $ssrEnabled
            colorResolve = $colorResolve
            inputsReady = $inputsReady
            hizRequested = $hizRequested
            hizActive = $hizActive
            hizFallbackReason = $hizFallbackReason
            fixedStepFallback = $fixedFallback
            pyramidAllocated = $pyramidAllocated
            pyramidReady = $pyramidReady
            pyramidWidth = $pyramidWidth
            pyramidHeight = $pyramidHeight
            pyramidMipCount = $pyramidMipCount
            pyramidImageCount = $pyramidImageCount
            pyramidFormat = $pyramidFormat
            pyramidMemoryBytes = $pyramidMemoryBytes
            buildDispatchCount = $buildDispatchCount
            generatedMipMask = $generatedMipMask
            traversalMaxMip = $traversalMaxMip
            actualBuildDispatches = $actualBuildDispatches
            actualBuildDispatchMinimum = $actualBuildDispatchMinimum
            actualDescriptorBinds = $actualDescriptorBinds
            actualDescriptorBindMinimum = $actualDescriptorBindMinimum
            actualConsumerDraws = $actualConsumerDraws
            actualConsumerDrawMinimum = $actualConsumerDrawMinimum
            refinement = $refinement
            refinementSteps = $refinementSteps
            hitValidationRequested = $hitValidationRequested
            hitValidationActive = $hitValidationActive
            hitValidationContractVersion = $hitValidationContractVersion
            hitNormalValidationEnabled = $hitNormalValidationEnabled
            hitFootprintTapCount = $hitFootprintTapCount
            signedDepthValidationEnabled = $signedDepthValidationEnabled
            originBiasMinimumPixels = $originBiasMinimumPixels
            originBiasMaximumPixels = $originBiasMaximumPixels
            reconstructionRequested = $reconstructionRequested
            reconstructionActive = $reconstructionActive
            reconstructionTargetsAllocated = $reconstructionTargetsAllocated
            reconstructionDescriptorSetsReady = $reconstructionDescriptorSetsReady
            reconstructionTraceDispatches = $reconstructionTraceDispatches
            reconstructionTemporalDispatches = $reconstructionTemporalDispatches
            reconstructionSpatialDispatches = $reconstructionSpatialDispatches
            reconstructionHistoryCopies = $reconstructionHistoryCopies
            reconstructionHistoryReset = $reconstructionHistoryReset
            reconstructionImageCount = $reconstructionImageCount
            reconstructionMemoryBytes = $reconstructionMemoryBytes
            reconstructionTemporalContractVersion = $reconstructionTemporalContractVersion
            reconstructionTemporalMissHistoryRejectEnabled = $reconstructionTemporalMissHistoryRejectEnabled
            reconstructionTemporalPreviousViewDepthEnabled = $reconstructionTemporalPreviousViewDepthEnabled
            reconstructionTemporalHistoryLockEnabled = $reconstructionTemporalHistoryLockEnabled
            reconstructionSpatialCenterHitGateEnabled = $reconstructionSpatialCenterHitGateEnabled
            reconstructionSpatialVarianceClampEnabled = $reconstructionSpatialVarianceClampEnabled
            reconstructionSpatialSupportTapCount = $reconstructionSpatialSupportTapCount
            reconstructionRawResolvedAliased = $reconstructionRawResolvedAliased
            reconstructionCurrentHdrSourceEnabled = $reconstructionCurrentHdrSourceEnabled
            reconstructionCurrentHdrRadianceFilterEnabled = $reconstructionCurrentHdrRadianceFilterEnabled
            reconstructionCurrentHdrMipLevels = $reconstructionCurrentHdrMipLevels
            reconstructionCurrentHdrMipChainReady = $reconstructionCurrentHdrMipChainReady
            staticDiagnosticFrameCount = $staticRawHitRange.Count
            staticDiagnosticPixelMinimum = $staticPixelCountRange.Minimum
            staticDiagnosticPixelMaximum = $staticPixelCountRange.Maximum
            staticRawHitMinimum = $staticRawHitRange.Minimum
            staticRawHitMaximum = $staticRawHitRange.Maximum
            staticRawHitDelta = $staticRawHitRange.Delta
            staticRawHitDeltaPermille = $staticRawHitDeltaPermille
            staticHighConfidenceMinimum = $staticHighConfidenceRange.Minimum
            staticHighConfidenceMaximum = $staticHighConfidenceRange.Maximum
            staticHighConfidenceDelta = $staticHighConfidenceRange.Delta
            staticHighConfidenceDeltaPermille = $staticHighConfidenceDeltaPermille
            staticTemporalValidMinimum = $staticTemporalValidRange.Minimum
            staticTemporalValidMaximum = $staticTemporalValidRange.Maximum
            staticTemporalValidDelta = $staticTemporalValidRange.Delta
            staticTemporalValidDeltaPermille = $staticTemporalValidDeltaPermille
            staticResolvedValidMinimum = $staticResolvedValidRange.Minimum
            staticResolvedValidMaximum = $staticResolvedValidRange.Maximum
            staticResolvedValidDelta = $staticResolvedValidRange.Delta
            staticResolvedValidDeltaPermille = $staticResolvedValidDeltaPermille
            fallbackBlendRequested = $fallbackBlendRequested
            fallbackBlendActive = $fallbackBlendActive
            fallbackBlendContractVersion = $fallbackBlendContractVersion
            fallbackBlendResolvedPixels = $fallbackBlendResolvedPixels
            fallbackBlendPartialPixels = $fallbackBlendPartialPixels
            fallbackBlendHighTrustPixels = $fallbackBlendHighTrustPixels
            fallbackBlendAveragePermille = $fallbackBlendAveragePermille
            reconstructionDeferredConsumerContractVersion = $reconstructionDeferredConsumerContractVersion
            reconstructionDeferredReceiverReprojectionEnabled = $reconstructionDeferredReceiverReprojectionEnabled
            reconstructionDeferredValidatedBilinearEnabled = $reconstructionDeferredValidatedBilinearEnabled
            reconstructionDeferredHistoryTapCount = $reconstructionDeferredHistoryTapCount
            reconstructionDeferredDepthRejectEnabled = $reconstructionDeferredDepthRejectEnabled
            reconstructionDeferredNormalRejectEnabled = $reconstructionDeferredNormalRejectEnabled
            reconstructionDeferredRoughnessRejectEnabled = $reconstructionDeferredRoughnessRejectEnabled
            reconstructionDeferredMetadataDescriptorBound = $reconstructionDeferredMetadataDescriptorBound
            reconstructionResolvedMetadataAliased = $reconstructionResolvedMetadataAliased
            probeFallback = $probeFallback
            sceneColorHistoryRequested = $sceneColorHistoryRequested
            sceneColorHistoryDescriptorBound = $sceneColorHistoryDescriptorBound
            sceneColorHistoryReady = $sceneColorHistoryReady
            sceneColorHistoryActive = $sceneColorHistoryActive
            sceneColorHistoryFallbackReason = $sceneColorHistoryFallbackReason
            sceneColorHistorySourceValid = $sceneColorHistorySourceValid
            sceneColorHistoryCurrentImageIndex = $sceneColorHistoryCurrentImageIndex
            sceneColorHistorySourceImageIndex = $sceneColorHistorySourceImageIndex
            sceneColorHistoryFrameAge = $sceneColorHistoryFrameAge
            radianceSource = $radianceSource
            temporalSsrReady = $temporalSsrReady
            temporalSsrActive = $temporalSsrActive
            ssrStrength = $ssrStrength
            ssrRayLength = $ssrRayLength
            ssrThickness = $ssrThickness
            ssrStepCount = $ssrStepCount
            holeDiagnosticsRequested = $holeDiagnosticsRequested
            holeDiagnosticsActive = $holeDiagnosticsActive
            holeDiagnosticsReadbackValid = $holeDiagnosticsReadbackValid
            holeDiagnosticsContractVersion = $holeDiagnosticsContractVersion
            holeDiagnosticsPixelCount = $holeDiagnosticsPixelCount
            holeDiagnosticsRawHitPixels = $holeDiagnosticsRawHitPixels
            holeDiagnosticsRawHighConfidencePixels = $holeDiagnosticsRawHighConfidencePixels
            holeDiagnosticsTemporalValidPixels = $holeDiagnosticsTemporalValidPixels
            holeDiagnosticsResolvedValidPixels = $holeDiagnosticsResolvedValidPixels
            holeDiagnosticsIsolatedRawHitPixels = $holeDiagnosticsIsolatedRawHitPixels
            holeDiagnosticsCenterMissNeighborHitPixels = $holeDiagnosticsCenterMissNeighborHitPixels
            holeDiagnosticsResolvedHolePixels = $holeDiagnosticsResolvedHolePixels
            holeDiagnosticsRawHitTemporalRejectedPixels = $holeDiagnosticsRawHitTemporalRejectedPixels
            holeDiagnosticsRawHitSpatialRejectedPixels = $holeDiagnosticsRawHitSpatialRejectedPixels
            holeDiagnosticsTemporalMissCarriedPixels = $holeDiagnosticsTemporalMissCarriedPixels
            receiverAuditRequested = $receiverAuditRequested
            receiverAuditProductionBlend = $receiverAuditProductionBlend
            receiverAuditIndependentIblEnergy = $receiverAuditIndependentIblEnergy
            receiverAuditPositiveWeightMask = $receiverAuditPositiveWeightMask
            receiverAuditReadyCubemapMask = $receiverAuditReadyCubemapMask
            receiverAuditBoxProjectionHitMask = $receiverAuditBoxProjectionHitMask
            receiverAuditDominantSlot = $receiverAuditDominantSlot
            receiverAuditTotalWeight = $receiverAuditTotalWeight
            receiverAuditLocalCoverage = $receiverAuditLocalCoverage
            receiverAuditDominantWeight = $receiverAuditDominantWeight
            receiverAuditCubemapWeight = $receiverAuditCubemapWeight
            receiverAuditLods = $receiverAuditLods
            capturedSceneNeutralTintMask = $capturedSceneNeutralTintMask
            frameGraphIssues = $frameGraphIssues
            validationDiagnostics = $validationDiagnostics.Count
            gpuTotalAverageMs = $gpuTotalAverage
            gpuTotalMaximumMs = $gpuTotalMaximum
            gpuMainAverageMs = $gpuMainAverage
        }
        checks = $checks
    }
}

if (!$SkipBuild) {
    Invoke-SsrBuild -Root $projectRoot
}
foreach ($executable in @($forwardExecutable, $showcaseExecutable)) {
    if (!(Test-Path -LiteralPath $executable)) {
        throw "Missing Debug executable: $executable"
    }
    if (-not $SkipSigning) {
        Invoke-SsrSigning -Root $projectRoot -TargetPath $executable
    }
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$common = @{
    SE_SHADOW_QUALITY = "medium"
    SE_FORWARD3D_AA_MODE = "taa"
    SE_SCENE_UPDATE_FREEZE = "1"
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_HIDE_IMGUI = "1"
    SE_WINDOW_HIDDEN = "1"
    SE_RENDER_VIEW = "ssr"
    SE_ENABLE_GPU_TIMESTAMPS = "1"
    SE_BENCHMARK_WARMUP_FRAMES = "8"
    SE_BENCHMARK_FRAMES = "12"
    SE_AUTO_EXIT_FRAMES = "20"
    SE_REFLECTION_RECEIVER_AUDIT = "1"
    SE_REFLECTION_RECEIVER_AUDIT_X = "-2.15"
    SE_REFLECTION_RECEIVER_AUDIT_Y = "0.18"
    SE_REFLECTION_RECEIVER_AUDIT_Z = "0.15"
    SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_X = "0.35"
    SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_Y = "0.18"
    SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_Z = "-0.92"
    SE_REFLECTION_RECEIVER_AUDIT_ROUGHNESS = "0.24"
}
if ($VerifyHoleDiagnostics) {
    $common["SE_SSR_HOLE_DIAGNOSTICS"] = "1"
}
$normalShowcase = $common.Clone()
$normalShowcase.Remove("SE_SHADOW_QUALITY")
$explicitReenable = $common.Clone()

$lanes = @(
    [pscustomobject]@{
        name = "lighting-showcase-default-hiz"
        executable = $showcaseExecutable
        scene = "LightingShowcase normal defaults"
        mode = "hiz"
        environment = $normalShowcase + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-explicit-reenable"
        executable = $showcaseExecutable
        scene = "LightingShowcase explicit SSR re-enable"
        mode = "hiz"
        environment = $explicitReenable + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR_STRENGTH = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-hiz"
        executable = $showcaseExecutable
        scene = "LightingShowcase"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
        }
    },
    [pscustomobject]@{
        name = "forward3d-fbx-hiz"
        executable = $forwardExecutable
        scene = "Forward3D animated FBX"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = $null
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
        }
    },
    [pscustomobject]@{
        name = "forward3d-fbx-current-hdr-source-enabled"
        executable = $forwardExecutable
        scene = "Forward3D animated FBX current-HDR radiance source control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = $null
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_CURRENT_HDR_SOURCE = "1"
        }
    },
    [pscustomobject]@{
        name = "forward3d-fbx-current-hdr-miss-history-reject-disabled"
        executable = $forwardExecutable
        scene = "Forward3D animated FBX current-HDR miss-history reject control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = $null
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_CURRENT_HDR_SOURCE = "1"
            SE_SSR_TEMPORAL_MISS_HISTORY_REJECT = "0"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-legacy-probe-blend"
        executable = $showcaseExecutable
        scene = "LightingShowcase legacy reflection blend control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_REFLECTION_PROBE_LEGACY_BLEND = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-legacy-reflection-energy"
        executable = $showcaseExecutable
        scene = "LightingShowcase legacy ambient-scaled reflection control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_REFLECTION_PROBE_LEGACY_ENERGY_SCALE = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-hiz-disabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase"
        mode = "fixed-step"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "0"
            SE_SSR_REFINEMENT = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-hit-validation-disabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase hit-validation control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_HIT_VALIDATION = "0"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-deferred-reprojection-disabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase Deferred SSR receiver reprojection control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_DEFERRED_REPROJECTION = "0"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-current-hdr-source-disabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase current-HDR radiance source control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_CURRENT_HDR_SOURCE = "0"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-current-hdr-source-enabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase current-HDR radiance source opt-in control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_CURRENT_HDR_SOURCE = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-current-hdr-history-lock-disabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase current-HDR radiance with history lock disabled"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_CURRENT_HDR_SOURCE = "1"
            SE_SSR_TEMPORAL_HISTORY_LOCK = "0"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-temporal-history-lock-disabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase SSR temporal history lock control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_TEMPORAL_HISTORY_LOCK = "0"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-spatial-variance-clamp-disabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase SSR spatial variance clamp control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_SPATIAL_VARIANCE_CLAMP = "0"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-probe-fallback-blend-disabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase SSR probe fallback blend control"
        mode = "hiz"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_PROBE_FALLBACK_BLEND = "0"
        }
    },
    [pscustomobject]@{
        name = "ssr-disabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase"
        mode = "disabled"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "0"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-scene-color-history-disabled"
        executable = $showcaseExecutable
        scene = "LightingShowcase GBuffer radiance control"
        mode = "hiz-gbuffer"
        environment = $common + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SSR = "1"
            SE_SSR_HIZ = "1"
            SE_SSR_REFINEMENT = "1"
            SE_SSR_SCENE_COLOR = "0"
        }
    }
)

$reports = @()
$reports += Invoke-SsrShaderContract
foreach ($lane in $lanes) {
    $reports += Invoke-SsrLane `
        -Name $lane.name `
        -Executable $lane.executable `
        -Scene $lane.scene `
        -Environment $lane.environment `
        -Mode $lane.mode
}

$fallbackBlendDefault = $reports |
    Where-Object { $_.lane -eq "lighting-showcase-hiz" } |
    Select-Object -First 1
$fallbackBlendControl = $reports |
    Where-Object { $_.lane -eq "lighting-showcase-probe-fallback-blend-disabled" } |
    Select-Object -First 1
if ($null -ne $fallbackBlendDefault -and $null -ne $fallbackBlendControl) {
    $defaultAverage = [int]$fallbackBlendDefault.metrics.fallbackBlendAveragePermille
    $controlAverage = [int]$fallbackBlendControl.metrics.fallbackBlendAveragePermille
    $comparisonChecks = @(
        (New-Check "fallback blend default/control active state differs" `
            ($fallbackBlendDefault.metrics.fallbackBlendActive -eq 1 -and
                $fallbackBlendControl.metrics.fallbackBlendActive -eq 0) `
            "default=$($fallbackBlendDefault.metrics.fallbackBlendActive),control=$($fallbackBlendControl.metrics.fallbackBlendActive)" `
            "1/0"),
        (New-Check "fallback blend does not raise average SSR trust" `
            ($defaultAverage -le $controlAverage) `
            "defaultAvg=$defaultAverage,controlAvg=$controlAverage" `
            "default <= control")
    )
    $comparisonPassCount = @(
        $comparisonChecks | Where-Object { $_.status -eq "pass" }
    ).Count
    $comparisonFailCount = @(
        $comparisonChecks | Where-Object { $_.status -eq "fail" }
    ).Count
    $reports += [pscustomobject]@{
        lane = "lighting-showcase-probe-fallback-blend-comparison"
        scene = "LightingShowcase SSR probe fallback blend data comparison"
        executable = $showcaseExecutable
        mode = "comparison"
        csv = ""
        log = ""
        verdict = if ($comparisonFailCount -eq 0) { "pass" } else { "fail" }
        passCount = $comparisonPassCount
        failCount = $comparisonFailCount
        metrics = [pscustomobject]@{
            defaultAveragePermille = $defaultAverage
            controlAveragePermille = $controlAverage
        }
        checks = $comparisonChecks
    }
}
if ($VerifyHoleDiagnostics) {
    $historyLockDefault = $reports |
        Where-Object { $_.lane -eq "lighting-showcase-current-hdr-source-enabled" } |
        Select-Object -First 1
    $historyLockControl = $reports |
        Where-Object { $_.lane -eq "lighting-showcase-current-hdr-history-lock-disabled" } |
        Select-Object -First 1
    if ($null -ne $historyLockDefault -and $null -ne $historyLockControl) {
        $defaultMissCarried = [int]$historyLockDefault.metrics.holeDiagnosticsTemporalMissCarriedPixels
        $controlMissCarried = [int]$historyLockControl.metrics.holeDiagnosticsTemporalMissCarriedPixels
        $comparisonChecks = @(
            (New-Check "SSR history-lock default/control active state differs" `
                ($historyLockDefault.metrics.reconstructionTemporalHistoryLockEnabled -eq 1 -and
                    $historyLockControl.metrics.reconstructionTemporalHistoryLockEnabled -eq 0) `
                "default=$($historyLockDefault.metrics.reconstructionTemporalHistoryLockEnabled),control=$($historyLockControl.metrics.reconstructionTemporalHistoryLockEnabled)" `
                "1/0"),
            (New-Check "SSR history lock does not reduce miss-history coverage" `
                ($defaultMissCarried -ge $controlMissCarried) `
                "defaultMissCarried=$defaultMissCarried,controlMissCarried=$controlMissCarried" `
                "default >= control")
        )
        $comparisonPassCount = @(
            $comparisonChecks | Where-Object { $_.status -eq "pass" }
        ).Count
        $comparisonFailCount = @(
            $comparisonChecks | Where-Object { $_.status -eq "fail" }
        ).Count
        $reports += [pscustomobject]@{
            lane = "lighting-showcase-current-hdr-history-lock-comparison"
            scene = "LightingShowcase SSR temporal history-lock data comparison"
            executable = $showcaseExecutable
            mode = "comparison"
            csv = ""
            log = ""
            verdict = if ($comparisonFailCount -eq 0) { "pass" } else { "fail" }
            passCount = $comparisonPassCount
            failCount = $comparisonFailCount
            metrics = [pscustomobject]@{
                defaultMissCarried = $defaultMissCarried
                controlMissCarried = $controlMissCarried
            }
            checks = $comparisonChecks
        }
    }
    $missRejectDefault = $reports |
        Where-Object { $_.lane -eq "forward3d-fbx-current-hdr-source-enabled" } |
        Select-Object -First 1
    $missRejectControl = $reports |
        Where-Object { $_.lane -eq "forward3d-fbx-current-hdr-miss-history-reject-disabled" } |
        Select-Object -First 1
    if ($null -ne $missRejectDefault -and $null -ne $missRejectControl) {
        $defaultMissCarried = [int]$missRejectDefault.metrics.holeDiagnosticsTemporalMissCarriedPixels
        $controlMissCarried = [int]$missRejectControl.metrics.holeDiagnosticsTemporalMissCarriedPixels
        $defaultTemporalValid = [int]$missRejectDefault.metrics.holeDiagnosticsTemporalValidPixels
        $controlTemporalValid = [int]$missRejectControl.metrics.holeDiagnosticsTemporalValidPixels
        $defaultResolvedValid = [int]$missRejectDefault.metrics.holeDiagnosticsResolvedValidPixels
        $controlResolvedValid = [int]$missRejectControl.metrics.holeDiagnosticsResolvedValidPixels
        $comparisonChecks = @(
            (New-Check "SSR miss-reject default/control active state differs" `
                ($missRejectDefault.metrics.reconstructionTemporalMissHistoryRejectEnabled -eq 1 -and
                    $missRejectControl.metrics.reconstructionTemporalMissHistoryRejectEnabled -eq 0) `
                "default=$($missRejectDefault.metrics.reconstructionTemporalMissHistoryRejectEnabled),control=$($missRejectControl.metrics.reconstructionTemporalMissHistoryRejectEnabled)" `
                "1/0"),
            (New-Check "SSR miss reject does not increase temporal carry or coverage" `
                (
                    $defaultMissCarried -le $controlMissCarried -and
                    $defaultTemporalValid -le $controlTemporalValid -and
                    $defaultResolvedValid -le $controlResolvedValid
                ) `
                "defaultMissCarried=$defaultMissCarried,controlMissCarried=$controlMissCarried,defaultTemporal=$defaultTemporalValid,controlTemporal=$controlTemporalValid,defaultResolved=$defaultResolvedValid,controlResolved=$controlResolvedValid" `
                "default <= control for miss-carried, temporal, and resolved coverage")
        )
        $comparisonPassCount = @(
            $comparisonChecks | Where-Object { $_.status -eq "pass" }
        ).Count
        $comparisonFailCount = @(
            $comparisonChecks | Where-Object { $_.status -eq "fail" }
        ).Count
        $reports += [pscustomobject]@{
            lane = "forward3d-fbx-temporal-miss-history-reject-comparison"
            scene = "Forward3D animated FBX SSR temporal miss-history reject data comparison"
            executable = $forwardExecutable
            mode = "comparison"
            csv = ""
            log = ""
            verdict = if ($comparisonFailCount -eq 0) { "pass" } else { "fail" }
            passCount = $comparisonPassCount
            failCount = $comparisonFailCount
            metrics = [pscustomobject]@{
                defaultMissCarried = $defaultMissCarried
                controlMissCarried = $controlMissCarried
                defaultTemporalValid = $defaultTemporalValid
                controlTemporalValid = $controlTemporalValid
                defaultResolvedValid = $defaultResolvedValid
                controlResolvedValid = $controlResolvedValid
            }
            checks = $comparisonChecks
        }
    }
}
$passCount = ($reports | ForEach-Object { $_.passCount } | Measure-Object -Sum).Sum
$failCount = ($reports | ForEach-Object { $_.failCount } | Measure-Object -Sum).Sum
$summary = [pscustomobject]@{
    generatedAt = (Get-Date).ToString("o")
    executables = @($showcaseExecutable, $forwardExecutable)
    outputDirectory = (Resolve-Path $OutputDirectory).Path
    verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
    passCount = [int]$passCount
    failCount = [int]$failCount
    reports = $reports
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory "summary.json") -Encoding utf8
$summary

if ($Strict -and $failCount -gt 0) {
    exit 1
}
