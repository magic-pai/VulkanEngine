param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "out\reflection_capture_health",
    [int]$WarmupFrames = 8,
    [int]$CaptureFrames = 12,
    [int]$AutoExitFrames = 30,
    [switch]$SkipBuild,
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Get-Number {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Name,
        [double]$Default = [double]::NaN
    )

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value -or "$($property.Value)" -eq "") {
        return $Default
    }

    return [double]::Parse("$($property.Value)", [Globalization.CultureInfo]::InvariantCulture)
}

function Measure-Number {
    param(
        [Parameter(Mandatory = $true)]$Rows,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($Rows.Count -eq 0 -or $null -eq $Rows[0].PSObject.Properties[$Name]) {
        return [pscustomobject]@{
            present = $false
            min = $null
            max = $null
            first = $null
            last = $null
            delta = $null
        }
    }

    $values = @(
        foreach ($row in $Rows) {
            $value = Get-Number -Row $row -Name $Name
            if (-not [double]::IsNaN($value) -and -not [double]::IsInfinity($value)) {
                $value
            }
        }
    )
    if ($values.Count -eq 0) {
        return [pscustomobject]@{
            present = $true
            min = $null
            max = $null
            first = $null
            last = $null
            delta = $null
        }
    }

    return [pscustomobject]@{
        present = $true
        min = ($values | Measure-Object -Minimum).Minimum
        max = ($values | Measure-Object -Maximum).Maximum
        first = $values[0]
        last = $values[$values.Count - 1]
        delta = [double](($values | Measure-Object -Maximum).Maximum) -
            [double](($values | Measure-Object -Minimum).Minimum)
    }
}

function Add-Check {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Status,
        $Actual,
        $Expected,
        [string]$Detail = ""
    )

    $Checks.Add([pscustomobject]@{
        area = $Area
        name = $Name
        status = $Status
        actual = $Actual
        expected = $Expected
        detail = $Detail
    }) | Out-Null
}

function Add-BooleanCheck {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Passed,
        $Actual,
        $Expected,
        [string]$Detail = ""
    )

    Add-Check -Checks $Checks -Area $Area -Name $Name `
        -Status $(if ($Passed) { "pass" } else { "fail" }) `
        -Actual $Actual -Expected $Expected -Detail $Detail
}

function Invoke-WithEnvironment {
    param(
        [Parameter(Mandatory = $true)][string[]]$ManagedKeys,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)][scriptblock]$Script
    )

    $previous = @{}
    foreach ($key in $ManagedKeys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, $null, "Process")
    }

    try {
        foreach ($key in $Environment.Keys) {
            if ($null -ne $Environment[$key]) {
                [Environment]::SetEnvironmentVariable(
                    [string]$key,
                    [string]$Environment[$key],
                    "Process"
                )
            }
        }
        & $Script
    } finally {
        foreach ($key in $ManagedKeys) {
            [Environment]::SetEnvironmentVariable($key, $previous[$key], "Process")
        }
    }
}

function Test-AnyValue {
    param(
        [Parameter(Mandatory = $true)]$Rows,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$Expected
    )

    foreach ($row in $Rows) {
        $value = Get-Number -Row $row -Name $Name
        if ($value -eq $Expected) {
            return $true
        }
    }
    return $false
}

function Test-AnyGreater {
    param(
        [Parameter(Mandatory = $true)]$Rows,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$Threshold
    )

    foreach ($row in $Rows) {
        $value = Get-Number -Row $row -Name $Name
        if (-not [double]::IsNaN($value) -and $value -gt $Threshold) {
            return $true
        }
    }
    return $false
}

function Test-AnyMask {
    param(
        [Parameter(Mandatory = $true)]$Rows,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][int]$Mask
    )

    foreach ($row in $Rows) {
        $value = Get-Number -Row $row -Name $Name
        if (-not [double]::IsNaN($value) -and (([int]$value -band $Mask) -ne 0)) {
            return $true
        }
    }
    return $false
}

function Test-AllMasksSubset {
    param(
        [Parameter(Mandatory = $true)]$Rows,
        [Parameter(Mandatory = $true)][string]$SubsetName,
        [Parameter(Mandatory = $true)][string]$SupersetName
    )

    foreach ($row in $Rows) {
        $subset = Get-Number -Row $row -Name $SubsetName
        $superset = Get-Number -Row $row -Name $SupersetName
        if ([double]::IsNaN($subset) -or [double]::IsNaN($superset) -or
            (([int]$subset -band (-bnot [int]$superset)) -ne 0)) {
            return $false
        }
    }
    return $true
}

function Test-AllReceiverAuditWeightSums {
    param(
        [Parameter(Mandatory = $true)]$Rows,
        [double]$Tolerance = 0.001
    )

    foreach ($row in $Rows) {
        if ((Get-Number -Row $row -Name "reflection_probe_receiver_audit_requested") -ne 1) {
            continue
        }

        $totalWeight = Get-Number -Row $row -Name "reflection_probe_receiver_audit_total_weight"
        if ([double]::IsNaN($totalWeight) -or $totalWeight -le 0.0001) {
            return $false
        }

        $rawWeightSum = 0.0
        $normalizedWeightSum = 0.0
        for ($index = 0; $index -lt 4; ++$index) {
            $rawWeightSum += Get-Number -Row $row -Name "reflection_probe_receiver_audit_weight_$index"
            $normalizedWeightSum += Get-Number -Row $row -Name "reflection_probe_receiver_audit_normalized_weight_$index"
        }

        if ([Math]::Abs($rawWeightSum - $totalWeight) -gt $Tolerance) {
            return $false
        }
        if ([Math]::Abs($normalizedWeightSum - 1.0) -gt $Tolerance) {
            return $false
        }
    }

    return $true
}

function Test-AnyPersistentShadowCacheHit {
    param([Parameter(Mandatory = $true)]$Rows)

    foreach ($row in $Rows) {
        if (
            (Get-Number -Row $row -Name "reflection_probe_captured_scene_shadow_snapshot_persistent_enabled") -eq 1 -and
            (Get-Number -Row $row -Name "reflection_probe_captured_scene_shadow_snapshot_persistent_hit") -eq 1 -and
            (Get-Number -Row $row -Name "reflection_probe_captured_scene_shadow_snapshot_persistent_cache_slot") -ge 0 -and
            (Get-Number -Row $row -Name "reflection_probe_captured_scene_shadow_snapshot_persistent_cache_resource_count") -ge 1 -and
            (Get-Number -Row $row -Name "reflection_probe_captured_scene_shadow_snapshot_persistent_cache_resource_count") -le 2 -and
            (Get-Number -Row $row -Name "reflection_probe_captured_scene_shadow_snapshot_input_signature") -gt 0 -and
            (Get-Number -Row $row -Name "reflection_probe_captured_scene_shadow_snapshot_build_count") -eq 0 -and
            (Get-Number -Row $row -Name "reflection_probe_captured_scene_directional_shadow_pass_count") -eq 0 -and
            (Get-Number -Row $row -Name "reflection_probe_captured_scene_local_shadow_pass_count") -eq 0
        ) {
            return $true
        }
    }
    return $false
}

function New-ReflectionCaptureReport {
    param(
        [Parameter(Mandatory = $true)][string]$LaneName,
        [Parameter(Mandatory = $true)][string]$Mode,
        [Parameter(Mandatory = $true)][string]$CsvPath,
        [hashtable]$Environment = @{},
        [AllowNull()]$RunError
    )

    $checks = [System.Collections.Generic.List[object]]::new()
    if ($RunError) {
        Add-Check -Checks $checks -Area "run" -Name "Forward3D executable" `
            -Status "fail" -Actual $RunError -Expected "exit code 0"
        return [pscustomobject]@{
            lane = $LaneName
            mode = $Mode
            csv = $CsvPath
            verdict = "fail"
            passCount = 0
            warnCount = 0
            failCount = 1
            metrics = [pscustomobject]@{}
            checks = @($checks)
        }
    }
    if (-not (Test-Path -LiteralPath $CsvPath)) {
        Add-Check -Checks $checks -Area "run" -Name "benchmark csv" `
            -Status "fail" -Actual "missing" -Expected $CsvPath
        return [pscustomobject]@{
            lane = $LaneName
            mode = $Mode
            csv = $CsvPath
            verdict = "fail"
            passCount = 0
            warnCount = 0
            failCount = 1
            metrics = [pscustomobject]@{}
            checks = @($checks)
        }
    }

    $rows = @(Import-Csv -LiteralPath $CsvPath)
    if ($rows.Count -eq 0) {
        Add-Check -Checks $checks -Area "run" -Name "captured frame rows" `
            -Status "fail" -Actual 0 -Expected "> 0"
        return [pscustomobject]@{
            lane = $LaneName
            mode = $Mode
            csv = $CsvPath
            verdict = "fail"
            passCount = 0
            warnCount = 0
            failCount = 1
            metrics = [pscustomobject]@{}
            checks = @($checks)
        }
    }

    $columns = @(
        "framegraph_validation_issues",
        "benchmark_camera_motion_time_seconds",
        "benchmark_object_motion_time_seconds",
        "reflection_probe_capture_source_type",
        "reflection_probe_refresh_policy",
        "reflection_probe_capture_resource_ready",
        "reflection_probe_capture_descriptor_bound",
        "reflection_probe_captured_scene_capture_backend",
        "reflection_probe_force_mip0_sampling",
        "reflection_probe_captured_scene_face_count",
        "reflection_probe_captured_scene_faces_rendered",
        "reflection_probe_captured_scene_faces_pending",
        "reflection_probe_captured_scene_capture_pass_count",
        "reflection_probe_captured_scene_capture_draw_count",
        "reflection_probe_captured_scene_capture_visible_count",
        "reflection_probe_captured_scene_capture_culled_count",
        "reflection_probe_captured_scene_self_capture_excluded_count",
        "reflection_probe_captured_scene_capture_face_orientation_mask",
        "reflection_probe_cubemap_face_size",
        "reflection_probe_cubemap_mip_count",
        "reflection_probe_captured_scene_mip_generation_count",
        "reflection_probe_captured_scene_source_mip_generation_count",
        "reflection_probe_captured_scene_source_mip_count",
        "reflection_probe_captured_scene_source_mip_memory_bytes",
        "reflection_probe_captured_scene_source_mip_chain_ready",
        "reflection_probe_captured_scene_ggx_prefilter_source_image_separated",
        "reflection_probe_captured_scene_ggx_prefilter_pdf_lod_enabled",
        "reflection_probe_captured_scene_ggx_prefilter_dispatch_count",
        "reflection_probe_captured_scene_ggx_prefilter_sample_count",
        "reflection_probe_captured_scene_ggx_prefilter_quality",
        "reflection_probe_captured_scene_diffuse_irradiance_dispatch_count",
        "reflection_probe_captured_scene_diffuse_irradiance_sample_count",
        "reflection_probe_captured_scene_diffuse_irradiance_face_size",
        "reflection_probe_captured_scene_directional_shadow_requested",
        "reflection_probe_captured_scene_directional_shadow_ready",
        "reflection_probe_captured_scene_directional_shadow_pass_count",
        "reflection_probe_captured_scene_directional_shadow_draw_count",
        "reflection_probe_captured_scene_directional_shadow_caster_count",
        "reflection_probe_captured_scene_directional_shadow_map_size",
        "reflection_probe_captured_scene_directional_shadow_face_mask",
        "reflection_probe_captured_scene_directional_shadow_camera_independent",
        "reflection_probe_captured_scene_directional_shadow_local_tiles_suppressed",
        "reflection_probe_captured_scene_directional_shadow_probe_scene_index",
        "reflection_probe_captured_scene_local_shadow_requested",
        "reflection_probe_captured_scene_local_shadow_ready",
        "reflection_probe_captured_scene_local_shadow_pass_count",
        "reflection_probe_captured_scene_local_shadow_draw_count",
        "reflection_probe_captured_scene_local_shadow_caster_count",
        "reflection_probe_captured_scene_local_shadow_tile_count",
        "reflection_probe_captured_scene_local_shadow_point_face_tile_count",
        "reflection_probe_captured_scene_local_shadow_spot_tile_count",
        "reflection_probe_captured_scene_local_shadow_rect_tile_count",
        "reflection_probe_captured_scene_local_shadow_requested_tile_count",
        "reflection_probe_captured_scene_local_shadow_dropped_tile_count",
        "reflection_probe_captured_scene_local_shadow_rect_requested_tile_count",
        "reflection_probe_captured_scene_local_shadow_rect_maximum_tile_count",
        "reflection_probe_captured_scene_local_shadow_rect_extra_sample_tile_count",
        "reflection_probe_captured_scene_local_shadow_rect_budget_limited_sample_tile_count",
        "reflection_probe_captured_scene_local_shadow_rect_dropped_tile_count",
        "reflection_probe_captured_scene_local_shadow_map_tile_size",
        "reflection_probe_captured_scene_local_shadow_face_mask",
        "reflection_probe_captured_scene_local_shadow_supported_kind_mask",
        "reflection_probe_captured_scene_local_shadow_suppressed_kind_mask",
        "reflection_probe_captured_scene_local_shadow_camera_independent",
        "reflection_probe_captured_scene_local_shadow_probe_scene_index",
        "reflection_probe_captured_scene_shadow_snapshot_build_count",
        "reflection_probe_captured_scene_shadow_snapshot_reuse_face_count",
        "reflection_probe_captured_scene_shadow_snapshot_saved_directional_pass_count",
        "reflection_probe_captured_scene_shadow_snapshot_saved_local_tile_pass_count",
        "reflection_probe_captured_scene_shadow_snapshot_saved_local_draw_count",
        "reflection_probe_captured_scene_shadow_snapshot_build_face_mask",
        "reflection_probe_captured_scene_shadow_snapshot_reuse_face_mask",
        "reflection_probe_captured_scene_shadow_snapshot_probe_scene_index",
        "reflection_probe_captured_scene_shadow_snapshot_persistent_cache_slot",
        "reflection_probe_captured_scene_shadow_snapshot_persistent_hit_count",
        "reflection_probe_captured_scene_shadow_snapshot_persistent_cache_resource_count",
        "reflection_probe_captured_scene_shadow_snapshot_persistent_cache_eviction_count",
        "reflection_probe_captured_scene_shadow_snapshot_input_signature",
        "reflection_probe_captured_scene_shadow_snapshot_ready",
        "reflection_probe_captured_scene_shadow_snapshot_camera_independent",
        "reflection_probe_captured_scene_shadow_snapshot_enabled",
        "reflection_probe_captured_scene_shadow_snapshot_fallback_active",
        "reflection_probe_captured_scene_shadow_snapshot_persistent_enabled",
        "reflection_probe_captured_scene_shadow_snapshot_persistent_hit",
        "reflection_probe_captured_scene_persistent_shadow_cache_capacity",
        "reflection_probe_captured_scene_persistent_shadow_cache_resource_count",
        "reflection_probe_captured_scene_persistent_shadow_cache_eviction_count",
        "reflection_probe_captured_scene_persistent_shadow_cache_probe_scene_index_0",
        "reflection_probe_captured_scene_persistent_shadow_cache_probe_scene_index_1",
        "reflection_probe_captured_scene_persistent_shadow_cache_input_signature_0",
        "reflection_probe_captured_scene_persistent_shadow_cache_input_signature_1",
        "reflection_probe_captured_scene_last_captured_face",
        "reflection_probe_captured_scene_rasterized_geometry",
        "reflection_probe_captured_scene_gpu_resources_allocated",
        "reflection_probe_captured_scene_gpu_capture_in_progress",
        "reflection_probe_captured_scene_capture_face_orientation_valid",
        "reflection_probe_captured_scene_mip_chain_ready",
        "reflection_probe_captured_scene_ggx_prefilter_ready",
        "reflection_probe_captured_scene_ggx_prefilter_fallback_active",
        "reflection_probe_captured_scene_diffuse_irradiance_ready",
        "reflection_probe_captured_scene_probe_scene_index",
        "reflection_probe_selected_captured_scene_map_matches_active_mask",
        "reflection_probe_selected_captured_scene_duplicate_active_view_mask",
        "reflection_probe_selected_captured_scene_diffuse_irradiance_map_matches_active_mask",
        "reflection_probe_selected_captured_scene_diffuse_irradiance_duplicate_active_view_mask",
        "reflection_probe_captured_scene_probe_resource_count",
        "reflection_probe_captured_scene_ready_probe_count",
        "reflection_probe_captured_scene_in_flight_probe_count",
        "reflection_probe_captured_scene_distinct_active_view_count",
        "reflection_probe_captured_scene_diffuse_irradiance_ready_probe_count",
        "reflection_probe_captured_scene_distinct_active_diffuse_irradiance_view_count",
        "reflection_probe_selected_captured_scene_diffuse_irradiance_ready_mask",
        "reflection_probe_selected_capture_mip_count_0",
        "reflection_probe_selected_capture_mip_count_1",
        "reflection_probe_selected_capture_mip_count_2",
        "reflection_probe_selected_probe_count",
        "reflection_probe_max_blend_weight",
        "reflection_probe_normalized_blend_weight_sum",
        "reflection_probe_normalized_blend_weight_error",
        "reflection_probe_selected_probe_mask",
        "reflection_probe_selected_box_projection_mask",
        "reflection_probe_selected_captured_scene_box_projection_mask",
        "reflection_probe_selected_box_projection_ray_hit_mask",
        "reflection_probe_selected_box_projection_direction_changed_mask",
        "reflection_probe_selected_box_projection_outside_fallback_mask",
        "reflection_probe_selected_probe_duplicate_index_mask",
        "reflection_probe_selected_capture_mip_ready_mask",
        "reflection_probe_spatial_contract_failure_mask",
        "reflection_probe_spatial_contract_valid",
        "reflection_probe_receiver_audit_requested",
        "reflection_probe_receiver_audit_production_blend",
        "reflection_probe_receiver_audit_independent_ibl_energy",
        "reflection_probe_receiver_audit_position_x",
        "reflection_probe_receiver_audit_position_y",
        "reflection_probe_receiver_audit_position_z",
        "reflection_probe_receiver_audit_direction_x",
        "reflection_probe_receiver_audit_direction_y",
        "reflection_probe_receiver_audit_direction_z",
        "reflection_probe_receiver_audit_roughness",
        "reflection_probe_receiver_audit_positive_weight_mask",
        "reflection_probe_receiver_audit_ready_cubemap_mask",
        "reflection_probe_receiver_audit_box_projection_hit_mask",
        "reflection_probe_receiver_audit_dominant_slot",
        "reflection_probe_receiver_audit_total_weight",
        "reflection_probe_receiver_audit_local_coverage",
        "reflection_probe_receiver_audit_dominant_normalized_weight",
        "reflection_probe_receiver_audit_local_cubemap_weight",
        "reflection_probe_receiver_audit_weight_0",
        "reflection_probe_receiver_audit_weight_1",
        "reflection_probe_receiver_audit_weight_2",
        "reflection_probe_receiver_audit_weight_3",
        "reflection_probe_receiver_audit_normalized_weight_0",
        "reflection_probe_receiver_audit_normalized_weight_1",
        "reflection_probe_receiver_audit_normalized_weight_2",
        "reflection_probe_receiver_audit_normalized_weight_3",
        "reflection_probe_receiver_audit_lod_0",
        "reflection_probe_receiver_audit_lod_1",
        "reflection_probe_receiver_audit_lod_2",
        "reflection_probe_receiver_audit_lod_3",
        "reflection_probe_captured_scene_neutral_tint_mask",
        "reflection_probe_captured_scene_upload_count",
        "reflection_probe_captured_scene_refresh_check_count",
        "reflection_probe_captured_scene_refresh_performed",
        "reflection_probe_captured_scene_refresh_reason",
        "reflection_probe_captured_scene_last_refresh_reason",
        "reflection_probe_captured_scene_dirty_mask",
        "reflection_probe_captured_scene_active_signature",
        "reflection_probe_captured_scene_requested_signature",
        "reflection_probe_captured_scene_radiance_signature",
        "reflection_probe_captured_scene_membership_revision",
        "reflection_probe_captured_scene_light_revision",
        "reflection_probe_captured_scene_render_revision",
        "reflection_probe_captured_scene_scheduler_frame",
        "reflection_probe_captured_scene_last_refresh_completed_frame",
        "reflection_probe_captured_scene_local_light_signature",
        "reflection_probe_captured_scene_geometry_signature",
        "reflection_probe_captured_scene_affected_local_light_count",
        "reflection_probe_captured_scene_affected_renderable_count",
        "reflection_probe_captured_scene_local_light_identity_mask",
        "reflection_probe_captured_scene_geometry_identity_mask",
        "reflection_probe_captured_scene_local_light_region_mask",
        "reflection_probe_captured_scene_geometry_region_mask",
        "reflection_probe_captured_scene_dirty_local_light_count",
        "reflection_probe_captured_scene_dirty_renderable_count",
        "reflection_probe_captured_scene_refresh_priority",
        "reflection_probe_captured_scene_minimum_refresh_interval_frames",
        "reflection_probe_captured_scene_refresh_deferred_count",
        "reflection_probe_captured_scene_selective_invalidation_enabled",
        "reflection_probe_captured_scene_refresh_deferred_by_budget",
        "reflection_probe_captured_scene_local_light_dirty",
        "reflection_probe_captured_scene_geometry_dirty",
        "reflection_probe_captured_scene_locality_ignored_light_revision",
        "reflection_probe_captured_scene_locality_ignored_geometry_revision",
        "reflection_probe_captured_scene_locality_ignored_light_revision_count",
        "reflection_probe_captured_scene_locality_ignored_geometry_revision_count",
        "reflection_probe_captured_scene_dirty_local_light_probe_count",
        "reflection_probe_captured_scene_dirty_geometry_probe_count"
    )
    $metrics = @{}
    foreach ($column in $columns) {
        $metrics[$column] = Measure-Number -Rows $rows -Name $column
    }

    $missingColumns = @(
        $columns | Where-Object { -not $metrics[$_].present }
    )
    Add-BooleanCheck -Checks $checks -Area "contract" -Name "capture audit columns" `
        -Passed ($missingColumns.Count -eq 0) -Actual $missingColumns `
        -Expected "all audit columns present"
    Add-BooleanCheck -Checks $checks -Area "contract" -Name "framegraph validation" `
        -Passed ($metrics["framegraph_validation_issues"].max -eq 0) `
        -Actual $metrics["framegraph_validation_issues"].max -Expected 0
    Add-BooleanCheck -Checks $checks -Area "capture" -Name "self-capture exclusion accounting is nonnegative" `
        -Passed ($metrics["reflection_probe_captured_scene_self_capture_excluded_count"].min -ge 0) `
        -Actual $metrics["reflection_probe_captured_scene_self_capture_excluded_count"].min -Expected ">= 0"
    if ($Mode -eq "filter-high" -or $Mode -eq "filter-medium") {
        Add-BooleanCheck -Checks $checks -Area "capture" -Name "showcase reflective receiver is excluded from probe capture" `
            -Passed ($metrics["reflection_probe_captured_scene_self_capture_excluded_count"].max -gt 0) `
            -Actual $metrics["reflection_probe_captured_scene_self_capture_excluded_count"].max -Expected "> 0"
    }
    $expectedForceMip0 = if (
        $Environment.ContainsKey("SE_REFLECTION_PROBE_FORCE_MIP0") -and
        "$($Environment['SE_REFLECTION_PROBE_FORCE_MIP0'])" -eq "1"
    ) { 1 } else { 0 }
    Add-BooleanCheck -Checks $checks -Area "sampling" -Name "probe MIP0 control is explicit" `
        -Passed ($metrics["reflection_probe_force_mip0_sampling"].max -eq $expectedForceMip0) `
        -Actual $metrics["reflection_probe_force_mip0_sampling"].max -Expected $expectedForceMip0
    $selectiveExpected = if ($Mode -eq "selective-fallback") { 0 } else { 1 }
    Add-BooleanCheck -Checks $checks -Area "selective-refresh" -Name "selective invalidation mode is explicit" `
        -Passed ($metrics["reflection_probe_captured_scene_selective_invalidation_enabled"].max -eq $selectiveExpected) `
        -Actual $metrics["reflection_probe_captured_scene_selective_invalidation_enabled"].max -Expected $selectiveExpected
    Add-BooleanCheck -Checks $checks -Area "selective-refresh" -Name "capture refresh priority resolves" `
        -Passed ($metrics["reflection_probe_captured_scene_refresh_priority"].max -gt 0) `
        -Actual $metrics["reflection_probe_captured_scene_refresh_priority"].max -Expected "> 0"
    Add-BooleanCheck -Checks $checks -Area "contract" -Name "captured-scene source selected" `
        -Passed (Test-AnyValue -Rows $rows -Name "reflection_probe_capture_source_type" -Expected 3) `
        -Actual $metrics["reflection_probe_capture_source_type"].max -Expected 3
    Add-BooleanCheck -Checks $checks -Area "contract" -Name "captured-scene resource ready" `
        -Passed ($metrics["reflection_probe_capture_resource_ready"].max -eq 1) `
        -Actual $metrics["reflection_probe_capture_resource_ready"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "contract" -Name "captured-scene descriptor bound" `
        -Passed ($metrics["reflection_probe_capture_descriptor_bound"].max -eq 1) `
        -Actual $metrics["reflection_probe_capture_descriptor_bound"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "GPU raster backend is active" `
        -Passed ($metrics["reflection_probe_captured_scene_capture_backend"].max -eq 2) `
        -Actual $metrics["reflection_probe_captured_scene_capture_backend"].max -Expected 2
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "six cubemap faces declared" `
        -Passed ($metrics["reflection_probe_captured_scene_face_count"].max -eq 6) `
        -Actual $metrics["reflection_probe_captured_scene_face_count"].max -Expected 6
    Add-BooleanCheck -Checks $checks -Area "spatial" -Name "all cubemap faces use canonical capture orientation" `
        -Passed (
            $metrics["reflection_probe_captured_scene_capture_face_orientation_mask"].max -eq 63 -and
            $metrics["reflection_probe_captured_scene_capture_face_orientation_valid"].max -eq 1
        ) `
        -Actual "mask=0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_capture_face_orientation_mask'].max, 16)),valid=$($metrics['reflection_probe_captured_scene_capture_face_orientation_valid'].max)" `
        -Expected "mask=0x3F,valid=1"
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "rasterized geometry is explicit" `
        -Passed ($metrics["reflection_probe_captured_scene_rasterized_geometry"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_rasterized_geometry"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "GPU capture resources allocate" `
        -Passed ($metrics["reflection_probe_captured_scene_gpu_resources_allocated"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_gpu_resources_allocated"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "six faces reach a completed mip chain" `
        -Passed ($metrics["reflection_probe_captured_scene_mip_chain_ready"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_mip_chain_ready"].max -Expected 1
    $expectedCaptureFaceSize = switch ($Mode) {
        "filter-off" { 128 }
        "filter-medium" { 256 }
        default { 512 }
    }
    $expectedSourceMipCount = switch ($expectedCaptureFaceSize) {
        128 { 8 }
        256 { 9 }
        512 { 10 }
        1024 { 11 }
        default { 0 }
    }
    $expectedSourceMipMemoryBytes = switch ($expectedCaptureFaceSize) {
        128 { 1048560 }
        256 { 4194288 }
        512 { 16777200 }
        1024 { 67108848 }
        default { 0 }
    }
    Add-BooleanCheck -Checks $checks -Area "budget" -Name "captured cubemap face size resolves" `
        -Passed ($metrics["reflection_probe_cubemap_face_size"].max -eq $expectedCaptureFaceSize) `
        -Actual $metrics["reflection_probe_cubemap_face_size"].max -Expected $expectedCaptureFaceSize
    Add-BooleanCheck -Checks $checks -Area "budget" -Name "captured cubemap mip count resolves" `
        -Passed ($metrics["reflection_probe_cubemap_mip_count"].max -eq $expectedSourceMipCount) `
        -Actual $metrics["reflection_probe_cubemap_mip_count"].max -Expected $expectedSourceMipCount
    Add-BooleanCheck -Checks $checks -Area "filtering" -Name "captured radiance builds a complete source mip chain" `
        -Passed (
            $metrics["reflection_probe_captured_scene_source_mip_generation_count"].max -eq 1 -and
            $metrics["reflection_probe_captured_scene_source_mip_count"].max -eq $expectedSourceMipCount -and
            $metrics["reflection_probe_captured_scene_source_mip_chain_ready"].max -eq 1
        ) `
        -Actual "builds/mips/ready=$($metrics['reflection_probe_captured_scene_source_mip_generation_count'].max)/$($metrics['reflection_probe_captured_scene_source_mip_count'].max)/$($metrics['reflection_probe_captured_scene_source_mip_chain_ready'].max)" `
        -Expected "1/$expectedSourceMipCount/1"
    Add-BooleanCheck -Checks $checks -Area "budget" -Name "captured radiance source mip memory is bounded" `
        -Passed ($metrics["reflection_probe_captured_scene_source_mip_memory_bytes"].max -eq $expectedSourceMipMemoryBytes) `
        -Actual $metrics["reflection_probe_captured_scene_source_mip_memory_bytes"].max `
        -Expected $expectedSourceMipMemoryBytes
    Add-BooleanCheck -Checks $checks -Area "filtering" -Name "GGX output never aliases its source image" `
        -Passed ($metrics["reflection_probe_captured_scene_ggx_prefilter_source_image_separated"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_ggx_prefilter_source_image_separated"].max -Expected 1
    $expectedCapturedFilterQuality = switch ($Mode) {
        "filter-off" { 0 }
        "filter-medium" { 2 }
        default { 3 }
    }
    $expectedCapturedFilterSamples = switch ($Mode) {
        "filter-off" { 1 }
        "filter-medium" { 64 }
        default { 128 }
    }
    $expectedCapturedFilterReady = if ($Mode -eq "filter-off") { 0 } else { 1 }
    $expectedCapturedFilterFallback = if ($Mode -eq "filter-off") { 1 } else { 0 }
    $expectedPdfLodEnabled = if ($Mode -eq "filter-off") { 0 } else { 1 }
    $expectedGgxDispatches = if ($Mode -eq "filter-off") { 0 } else { $expectedSourceMipCount - 1 }
    Add-BooleanCheck -Checks $checks -Area "filtering" -Name "captured radiance filter quality resolves" `
        -Passed ($metrics["reflection_probe_captured_scene_ggx_prefilter_quality"].max -eq $expectedCapturedFilterQuality) `
        -Actual $metrics["reflection_probe_captured_scene_ggx_prefilter_quality"].max -Expected $expectedCapturedFilterQuality
    Add-BooleanCheck -Checks $checks -Area "filtering" -Name "captured radiance filter sample budget resolves" `
        -Passed ($metrics["reflection_probe_captured_scene_ggx_prefilter_sample_count"].max -eq $expectedCapturedFilterSamples) `
        -Actual $metrics["reflection_probe_captured_scene_ggx_prefilter_sample_count"].max -Expected $expectedCapturedFilterSamples
    Add-BooleanCheck -Checks $checks -Area "filtering" -Name "captured radiance filter readiness/fallback is explicit" `
        -Passed (
            $metrics["reflection_probe_captured_scene_ggx_prefilter_ready"].max -eq $expectedCapturedFilterReady -and
            $metrics["reflection_probe_captured_scene_ggx_prefilter_fallback_active"].max -eq $expectedCapturedFilterFallback
        ) `
        -Actual "ready/fallback=$($metrics['reflection_probe_captured_scene_ggx_prefilter_ready'].max)/$($metrics['reflection_probe_captured_scene_ggx_prefilter_fallback_active'].max)" `
        -Expected "$expectedCapturedFilterReady/$expectedCapturedFilterFallback"
    Add-BooleanCheck -Checks $checks -Area "filtering" -Name "GGX PDF LOD and dispatch path resolve together" `
        -Passed (
            $metrics["reflection_probe_captured_scene_ggx_prefilter_pdf_lod_enabled"].max -eq $expectedPdfLodEnabled -and
            $metrics["reflection_probe_captured_scene_ggx_prefilter_dispatch_count"].max -eq $expectedGgxDispatches
        ) `
        -Actual "pdf/dispatches=$($metrics['reflection_probe_captured_scene_ggx_prefilter_pdf_lod_enabled'].max)/$($metrics['reflection_probe_captured_scene_ggx_prefilter_dispatch_count'].max)" `
        -Expected "$expectedPdfLodEnabled/$expectedGgxDispatches"
    Add-BooleanCheck -Checks $checks -Area "diffuse" -Name "GPU diffuse irradiance completes" `
        -Passed ($metrics["reflection_probe_captured_scene_diffuse_irradiance_ready"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_diffuse_irradiance_ready"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "diffuse" -Name "GPU diffuse irradiance records one convolution dispatch" `
        -Passed ($metrics["reflection_probe_captured_scene_diffuse_irradiance_dispatch_count"].max -gt 0) `
        -Actual $metrics["reflection_probe_captured_scene_diffuse_irradiance_dispatch_count"].max -Expected "> 0"
    Add-BooleanCheck -Checks $checks -Area "diffuse" -Name "GPU diffuse irradiance has a sample budget" `
        -Passed ($metrics["reflection_probe_captured_scene_diffuse_irradiance_sample_count"].max -ge 32) `
        -Actual $metrics["reflection_probe_captured_scene_diffuse_irradiance_sample_count"].max -Expected ">= 32"
    Add-BooleanCheck -Checks $checks -Area "diffuse" -Name "GPU diffuse irradiance has a usable cubemap resolution" `
        -Passed ($metrics["reflection_probe_captured_scene_diffuse_irradiance_face_size"].max -ge 16) `
        -Actual $metrics["reflection_probe_captured_scene_diffuse_irradiance_face_size"].max -Expected ">= 16"
    Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "capture-side directional shadow is requested" `
        -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_requested"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_directional_shadow_requested"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "capture-side directional shadow completes" `
        -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_ready"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_directional_shadow_ready"].max -Expected 1
    $expectedDirectionalPassCount = if ($Mode -eq "snapshot-off") { 6 } else { 1 }
    Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "directional shadow depth pass count matches snapshot policy" `
        -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].max -ge $expectedDirectionalPassCount) `
        -Actual $metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].max -Expected ">= $expectedDirectionalPassCount"
    Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "capture-side directional shadow draws casters" `
        -Passed (
            $metrics["reflection_probe_captured_scene_directional_shadow_draw_count"].max -gt 0 -and
            $metrics["reflection_probe_captured_scene_directional_shadow_caster_count"].max -gt 0
        ) `
        -Actual "draws=$($metrics['reflection_probe_captured_scene_directional_shadow_draw_count'].max),casters=$($metrics['reflection_probe_captured_scene_directional_shadow_caster_count'].max)" `
        -Expected "both > 0"
    Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "capture-side directional shadow uses a full map" `
        -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_map_size"].max -ge 512) `
        -Actual $metrics["reflection_probe_captured_scene_directional_shadow_map_size"].max -Expected ">= 512"
    Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "capture-side directional shadow covers six faces" `
        -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_face_mask"].max -eq 63) `
        -Actual $metrics["reflection_probe_captured_scene_directional_shadow_face_mask"].max -Expected "0x3F"
    Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "capture-side shadow projection is camera independent" `
        -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_camera_independent"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_directional_shadow_camera_independent"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "capture suppresses camera local-shadow tiles" `
        -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_local_tiles_suppressed"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_directional_shadow_local_tiles_suppressed"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "capture-side shadow keeps its producer identity" `
        -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_probe_scene_index"].max -ge 0) `
        -Actual $metrics["reflection_probe_captured_scene_directional_shadow_probe_scene_index"].max -Expected ">= 0"
    if ($Mode -eq "snapshot-off") {
        Add-BooleanCheck -Checks $checks -Area "snapshot" -Name "snapshot fallback is explicit" `
            -Passed (
                $metrics["reflection_probe_captured_scene_shadow_snapshot_enabled"].max -eq 0 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_fallback_active"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_build_count"].max -eq 0 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_reuse_face_count"].max -eq 0
            ) `
            -Actual "enabled=$($metrics['reflection_probe_captured_scene_shadow_snapshot_enabled'].max),fallback=$($metrics['reflection_probe_captured_scene_shadow_snapshot_fallback_active'].max),build=$($metrics['reflection_probe_captured_scene_shadow_snapshot_build_count'].max),reuse=$($metrics['reflection_probe_captured_scene_shadow_snapshot_reuse_face_count'].max)" `
            -Expected "enabled=0,fallback=1,build=0,reuse=0"
    } elseif ($Mode -eq "persistent-hit") {
        Add-BooleanCheck -Checks $checks -Area "snapshot" -Name "persistent snapshot cache serves a repeated capture" `
            -Passed (
                $metrics["reflection_probe_captured_scene_shadow_snapshot_enabled"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_fallback_active"].max -eq 0 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_ready"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_enabled"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_hit"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_hit_count"].max -ge 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_cache_resource_count"].max -ge 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_cache_resource_count"].max -le 2 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_cache_slot"].max -ge 0 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_input_signature"].max -gt 0
            ) `
            -Actual "enabled/hit/hits=$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_enabled'].max)/$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_hit'].max)/$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_hit_count'].max),slot/resources/evictions=$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_cache_slot'].max)/$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_cache_resource_count'].max)/$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_cache_eviction_count'].max),signature=$($metrics['reflection_probe_captured_scene_shadow_snapshot_input_signature'].max)" `
            -Expected "enabled=1, hit>=1, slot>=0, resources=1..2, signature>0"
        Add-BooleanCheck -Checks $checks -Area "snapshot" -Name "persistent hit records no new depth passes" `
            -Passed (Test-AnyPersistentShadowCacheHit -Rows $rows) `
            -Actual "hit=$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_hit'].max),directional passes=$($metrics['reflection_probe_captured_scene_directional_shadow_pass_count'].min)..$($metrics['reflection_probe_captured_scene_directional_shadow_pass_count'].max),local passes=$($metrics['reflection_probe_captured_scene_local_shadow_pass_count'].min)..$($metrics['reflection_probe_captured_scene_local_shadow_pass_count'].max)" `
            -Expected "one hit row with build/directional/local depth passes all zero"
    } else {
        Add-BooleanCheck -Checks $checks -Area "snapshot" -Name "single probe snapshot serves all cubemap faces" `
            -Passed (
                $metrics["reflection_probe_captured_scene_shadow_snapshot_enabled"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_fallback_active"].max -eq 0 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_ready"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_build_count"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_reuse_face_count"].max -ge 5 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_build_face_mask"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_reuse_face_mask"].max -eq 62 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_probe_scene_index"].max -ge 0 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_camera_independent"].max -eq 1
            ) `
            -Actual "enabled=$($metrics['reflection_probe_captured_scene_shadow_snapshot_enabled'].max),ready=$($metrics['reflection_probe_captured_scene_shadow_snapshot_ready'].max),build/reuse=$($metrics['reflection_probe_captured_scene_shadow_snapshot_build_count'].max)/$($metrics['reflection_probe_captured_scene_shadow_snapshot_reuse_face_count'].max),faces=0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_shadow_snapshot_build_face_mask'].max, 16))/0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_shadow_snapshot_reuse_face_mask'].max, 16)),probe=$($metrics['reflection_probe_captured_scene_shadow_snapshot_probe_scene_index'].max),camera=$($metrics['reflection_probe_captured_scene_shadow_snapshot_camera_independent'].max)" `
            -Expected "enabled=1,ready=1,build/reuse=1/>=5,faces=0x1/0x3E,probe>=0,camera=1"
        Add-BooleanCheck -Checks $checks -Area "snapshot" -Name "snapshot saves five directional shadow passes" `
            -Passed ($metrics["reflection_probe_captured_scene_shadow_snapshot_saved_directional_pass_count"].max -ge 5) `
            -Actual $metrics["reflection_probe_captured_scene_shadow_snapshot_saved_directional_pass_count"].max -Expected ">= 5"
        $localShadowRequested =
            $metrics["reflection_probe_captured_scene_local_shadow_requested"].max -eq 1
        Add-BooleanCheck -Checks $checks -Area "snapshot" -Name "snapshot saves local shadow work when local tiles are requested" `
            -Passed (
                (-not $localShadowRequested) -or
                ($metrics["reflection_probe_captured_scene_shadow_snapshot_saved_local_tile_pass_count"].max -gt 0 -and
                 $metrics["reflection_probe_captured_scene_shadow_snapshot_saved_local_draw_count"].max -gt 0)
            ) `
            -Actual "requested=$($metrics['reflection_probe_captured_scene_local_shadow_requested'].max),passes=$($metrics['reflection_probe_captured_scene_shadow_snapshot_saved_local_tile_pass_count'].max),draws=$($metrics['reflection_probe_captured_scene_shadow_snapshot_saved_local_draw_count'].max)" `
            -Expected "local off or saved passes/draws > 0"
    }
    $expectedLocalShadowSupportedKinds = if ($Mode -eq "rect-off") { 3 } else { 7 }
    $expectedLocalShadowSuppressedKinds = if ($Mode -eq "rect-off") { 4 } else { 0 }
    Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "capture-local shadow kind contract is explicit" `
        -Passed (
            $metrics["reflection_probe_captured_scene_local_shadow_supported_kind_mask"].max -eq $expectedLocalShadowSupportedKinds -and
            $metrics["reflection_probe_captured_scene_local_shadow_suppressed_kind_mask"].max -eq $expectedLocalShadowSuppressedKinds
        ) `
        -Actual "supported=0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_local_shadow_supported_kind_mask'].max, 16)),suppressed=0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_local_shadow_suppressed_kind_mask'].max, 16))" `
        -Expected "supported=0x$([Convert]::ToString($expectedLocalShadowSupportedKinds, 16)),suppressed=0x$([Convert]::ToString($expectedLocalShadowSuppressedKinds, 16))"
    if ($Mode -eq "multi") {
        Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "LightingShowcase requests capture-side local shadows" `
            -Passed ($metrics["reflection_probe_captured_scene_local_shadow_requested"].max -eq 1) `
            -Actual $metrics["reflection_probe_captured_scene_local_shadow_requested"].max -Expected 1
        Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "LightingShowcase completes capture-side local shadows" `
            -Passed ($metrics["reflection_probe_captured_scene_local_shadow_ready"].max -eq 1) `
            -Actual $metrics["reflection_probe_captured_scene_local_shadow_ready"].max -Expected 1
        Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "capture snapshot records point/spot local shadow tiles once" `
            -Passed ($metrics["reflection_probe_captured_scene_local_shadow_pass_count"].max -ge 1) `
            -Actual $metrics["reflection_probe_captured_scene_local_shadow_pass_count"].max -Expected ">= 1"
        Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "point and spot tiles draw real casters" `
            -Passed (
                $metrics["reflection_probe_captured_scene_local_shadow_draw_count"].max -gt 0 -and
                $metrics["reflection_probe_captured_scene_local_shadow_point_face_tile_count"].max -gt 0 -and
                $metrics["reflection_probe_captured_scene_local_shadow_spot_tile_count"].max -gt 0
            ) `
            -Actual "draws=$($metrics['reflection_probe_captured_scene_local_shadow_draw_count'].max),point=$($metrics['reflection_probe_captured_scene_local_shadow_point_face_tile_count'].max),spot=$($metrics['reflection_probe_captured_scene_local_shadow_spot_tile_count'].max)" `
            -Expected "all > 0"
        Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "rect area lights receive capture-local sample tiles" `
            -Passed ($metrics["reflection_probe_captured_scene_local_shadow_rect_tile_count"].max -gt 0) `
            -Actual $metrics["reflection_probe_captured_scene_local_shadow_rect_tile_count"].max -Expected "> 0"
        Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "rect capture budget accounts for every assigned tile" `
            -Passed (
                $metrics["reflection_probe_captured_scene_local_shadow_rect_requested_tile_count"].max -ge
                    $metrics["reflection_probe_captured_scene_local_shadow_rect_tile_count"].max -and
                $metrics["reflection_probe_captured_scene_local_shadow_rect_maximum_tile_count"].max -ge
                    $metrics["reflection_probe_captured_scene_local_shadow_rect_requested_tile_count"].max -and
                $metrics["reflection_probe_captured_scene_local_shadow_rect_dropped_tile_count"].max -eq 0
            ) `
            -Actual "assigned=$($metrics['reflection_probe_captured_scene_local_shadow_rect_tile_count'].max),requested=$($metrics['reflection_probe_captured_scene_local_shadow_rect_requested_tile_count'].max),maximum=$($metrics['reflection_probe_captured_scene_local_shadow_rect_maximum_tile_count'].max),dropped=$($metrics['reflection_probe_captured_scene_local_shadow_rect_dropped_tile_count'].max)" `
            -Expected "assigned <= requested <= maximum, dropped=0"
        Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "capture-local atlas covers all faces independently" `
            -Passed (
                $metrics["reflection_probe_captured_scene_local_shadow_face_mask"].max -eq 63 -and
                $metrics["reflection_probe_captured_scene_local_shadow_camera_independent"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_local_shadow_probe_scene_index"].max -ge 0
            ) `
            -Actual "faces=0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_local_shadow_face_mask'].max, 16)),camera=$($metrics['reflection_probe_captured_scene_local_shadow_camera_independent'].max),probe=$($metrics['reflection_probe_captured_scene_local_shadow_probe_scene_index'].max)" `
            -Expected "faces=0x3F,camera=1,probe>=0"
    }
    if ($Mode -eq "rect-off") {
        Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "rect capture-shadow fallback is explicit" `
            -Passed (
                $metrics["reflection_probe_captured_scene_local_shadow_ready"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_local_shadow_rect_tile_count"].max -eq 0 -and
                $metrics["reflection_probe_captured_scene_local_shadow_rect_requested_tile_count"].max -eq 0 -and
                $metrics["reflection_probe_captured_scene_local_shadow_rect_dropped_tile_count"].max -eq 0
            ) `
            -Actual "ready=$($metrics['reflection_probe_captured_scene_local_shadow_ready'].max),assigned=$($metrics['reflection_probe_captured_scene_local_shadow_rect_tile_count'].max),requested=$($metrics['reflection_probe_captured_scene_local_shadow_rect_requested_tile_count'].max),dropped=$($metrics['reflection_probe_captured_scene_local_shadow_rect_dropped_tile_count'].max)" `
            -Expected "ready=1, rect tiles/requested/dropped=0"
    }
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "GPU capture draws real scene geometry" `
        -Passed ($metrics["reflection_probe_captured_scene_capture_draw_count"].max -gt 0) `
        -Actual $metrics["reflection_probe_captured_scene_capture_draw_count"].max -Expected "> 0"
    Add-BooleanCheck -Checks $checks -Area "mapping" -Name "capture producer has a scene probe identity" `
        -Passed ($metrics["reflection_probe_captured_scene_probe_scene_index"].max -ge 0) `
        -Actual $metrics["reflection_probe_captured_scene_probe_scene_index"].max -Expected ">= 0"
    Add-BooleanCheck -Checks $checks -Area "mapping" -Name "sampled capture map matches its producer probe" `
        -Passed ($metrics["reflection_probe_selected_captured_scene_map_matches_active_mask"].max -gt 0) `
        -Actual $metrics["reflection_probe_selected_captured_scene_map_matches_active_mask"].max -Expected "non-zero mask"
    Add-BooleanCheck -Checks $checks -Area "diffuse" -Name "sampled diffuse irradiance map matches its producer probe" `
        -Passed ($metrics["reflection_probe_selected_captured_scene_diffuse_irradiance_map_matches_active_mask"].max -gt 0) `
        -Actual $metrics["reflection_probe_selected_captured_scene_diffuse_irradiance_map_matches_active_mask"].max -Expected "non-zero mask"
    Add-BooleanCheck -Checks $checks -Area "spatial" -Name "selected probes have unique identities" `
        -Passed ($metrics["reflection_probe_selected_probe_duplicate_index_mask"].max -eq 0) `
        -Actual "0x$([Convert]::ToString([int]$metrics['reflection_probe_selected_probe_duplicate_index_mask'].max, 16))" `
        -Expected 0
    Add-BooleanCheck -Checks $checks -Area "spatial" -Name "reflection blend weights remain normalized" `
        -Passed ($metrics["reflection_probe_normalized_blend_weight_error"].max -le 0.001) `
        -Actual $metrics["reflection_probe_normalized_blend_weight_error"].max -Expected "<= 0.001"
    Add-BooleanCheck -Checks $checks -Area "spatial" -Name "box-projection mask is a selected-probe subset" `
        -Passed (Test-AllMasksSubset -Rows $rows -SubsetName "reflection_probe_selected_box_projection_mask" -SupersetName "reflection_probe_selected_probe_mask") `
        -Actual "box=0x$([Convert]::ToString([int]$metrics['reflection_probe_selected_box_projection_mask'].max, 16)),selected=0x$([Convert]::ToString([int]$metrics['reflection_probe_selected_probe_mask'].max, 16))" `
        -Expected "box mask subset of selected mask"
    Add-BooleanCheck -Checks $checks -Area "spatial" -Name "reflection spatial contract is valid" `
        -Passed (
            $metrics["reflection_probe_spatial_contract_valid"].min -eq 1 -and
            $metrics["reflection_probe_spatial_contract_failure_mask"].max -eq 0
        ) `
        -Actual "valid=$($metrics['reflection_probe_spatial_contract_valid'].min)..$($metrics['reflection_probe_spatial_contract_valid'].max),failure=0x$([Convert]::ToString([int]$metrics['reflection_probe_spatial_contract_failure_mask'].max, 16))" `
        -Expected "valid=1,failure=0"
    $captureUploadEvidence = if ($Mode -eq "multi" -or $Mode -eq "rect-off" -or $Mode -eq "persistent-hit" -or $Mode -eq "persistent-off") {
        $metrics["reflection_probe_captured_scene_upload_count"].max
    } else {
        $metrics["reflection_probe_captured_scene_upload_count"].min
    }
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "capture resource uploaded" `
        -Passed ($captureUploadEvidence -ge 1) `
        -Actual $captureUploadEvidence -Expected ">= 1"

    $receiverAuditExpected = (
        $Environment.ContainsKey("SE_REFLECTION_RECEIVER_AUDIT") -and
        "$($Environment['SE_REFLECTION_RECEIVER_AUDIT'])" -ne "0"
    )
    if (-not $receiverAuditExpected) {
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver audit remains opt-in" `
            -Passed ($metrics["reflection_probe_receiver_audit_requested"].max -eq 0) `
            -Actual $metrics["reflection_probe_receiver_audit_requested"].max -Expected 0
    } else {
        $legacyBlendExpected = (
            $Environment.ContainsKey("SE_REFLECTION_PROBE_LEGACY_BLEND") -and
            "$($Environment['SE_REFLECTION_PROBE_LEGACY_BLEND'])" -eq "1"
        )
        $expectedProductionBlend = if ($legacyBlendExpected) { 0 } else { 1 }
        $legacyEnergyExpected = (
            $Environment.ContainsKey("SE_REFLECTION_PROBE_LEGACY_ENERGY_SCALE") -and
            "$($Environment['SE_REFLECTION_PROBE_LEGACY_ENERGY_SCALE'])" -eq "1"
        )
        $expectedIndependentEnergy = if ($legacyEnergyExpected) { 0 } else { 1 }
        $receiverAuditPositiveWeightMask =
            [int]$metrics["reflection_probe_receiver_audit_positive_weight_mask"].last
        $receiverAuditReadyCubemapMask =
            [int]$metrics["reflection_probe_receiver_audit_ready_cubemap_mask"].last
        $receiverAuditBoxProjectionHitMask =
            [int]$metrics["reflection_probe_receiver_audit_box_projection_hit_mask"].last
        $receiverAuditRawWeights = @(
            [double]$metrics["reflection_probe_receiver_audit_weight_0"].last,
            [double]$metrics["reflection_probe_receiver_audit_weight_1"].last,
            [double]$metrics["reflection_probe_receiver_audit_weight_2"].last,
            [double]$metrics["reflection_probe_receiver_audit_weight_3"].last
        )
        $receiverAuditNormalizedWeights = @(
            [double]$metrics["reflection_probe_receiver_audit_normalized_weight_0"].last,
            [double]$metrics["reflection_probe_receiver_audit_normalized_weight_1"].last,
            [double]$metrics["reflection_probe_receiver_audit_normalized_weight_2"].last,
            [double]$metrics["reflection_probe_receiver_audit_normalized_weight_3"].last
        )
        $receiverAuditResolvedLods = @(
            [double]$metrics["reflection_probe_receiver_audit_lod_0"].last,
            [double]$metrics["reflection_probe_receiver_audit_lod_1"].last,
            [double]$metrics["reflection_probe_receiver_audit_lod_2"].last,
            [double]$metrics["reflection_probe_receiver_audit_lod_3"].last
        )
        $receiverAuditRawWeightSum =
            [double]($receiverAuditRawWeights | Measure-Object -Sum).Sum
        $receiverAuditNormalizedWeightSum =
            [double]($receiverAuditNormalizedWeights | Measure-Object -Sum).Sum
        $receiverAuditMaxLod =
            [double]($receiverAuditResolvedLods | Measure-Object -Maximum).Maximum
        $receiverAuditDirectionLength = [Math]::Sqrt(
            [Math]::Pow([double]$metrics["reflection_probe_receiver_audit_direction_x"].last, 2.0) +
            [Math]::Pow([double]$metrics["reflection_probe_receiver_audit_direction_y"].last, 2.0) +
            [Math]::Pow([double]$metrics["reflection_probe_receiver_audit_direction_z"].last, 2.0)
        )

        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver audit is active" `
            -Passed (
                $metrics["reflection_probe_receiver_audit_requested"].min -eq 1 -and
                $metrics["reflection_probe_receiver_audit_requested"].max -eq 1
            ) `
            -Actual "$($metrics['reflection_probe_receiver_audit_requested'].min)..$($metrics['reflection_probe_receiver_audit_requested'].max)" `
            -Expected "1..1"
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver blend mode resolves" `
            -Passed (
                $metrics["reflection_probe_receiver_audit_production_blend"].min -eq $expectedProductionBlend -and
                $metrics["reflection_probe_receiver_audit_production_blend"].max -eq $expectedProductionBlend
            ) `
            -Actual "$($metrics['reflection_probe_receiver_audit_production_blend'].min)..$($metrics['reflection_probe_receiver_audit_production_blend'].max)" `
            -Expected $expectedProductionBlend
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver IBL energy mode resolves" `
            -Passed (
                $metrics["reflection_probe_receiver_audit_independent_ibl_energy"].min -eq $expectedIndependentEnergy -and
                $metrics["reflection_probe_receiver_audit_independent_ibl_energy"].max -eq $expectedIndependentEnergy
            ) `
            -Actual "$($metrics['reflection_probe_receiver_audit_independent_ibl_energy'].min)..$($metrics['reflection_probe_receiver_audit_independent_ibl_energy'].max)" `
            -Expected $expectedIndependentEnergy
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver audit input is normalized" `
            -Passed (
                [Math]::Abs($receiverAuditDirectionLength - 1.0) -le 0.001 -and
                $metrics["reflection_probe_receiver_audit_roughness"].last -gt 0.0 -and
                $metrics["reflection_probe_receiver_audit_roughness"].last -le 1.0
            ) `
            -Actual "dirLen=$receiverAuditDirectionLength,roughness=$($metrics['reflection_probe_receiver_audit_roughness'].last)" `
            -Expected "unit direction,0<roughness<=1"
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver local probe weights resolve" `
            -Passed (
                $metrics["reflection_probe_receiver_audit_positive_weight_mask"].max -ne 0 -and
                $metrics["reflection_probe_receiver_audit_total_weight"].min -gt 0.0 -and
                $metrics["reflection_probe_receiver_audit_dominant_slot"].min -ge 0 -and
                $metrics["reflection_probe_receiver_audit_dominant_normalized_weight"].min -gt 0.5
            ) `
            -Actual "mask=0x$([Convert]::ToString([int]$metrics['reflection_probe_receiver_audit_positive_weight_mask'].max, 16)),total=$($metrics['reflection_probe_receiver_audit_total_weight'].min)..$($metrics['reflection_probe_receiver_audit_total_weight'].max),dominant=$($metrics['reflection_probe_receiver_audit_dominant_slot'].min)..$($metrics['reflection_probe_receiver_audit_dominant_slot'].max),weight=$($metrics['reflection_probe_receiver_audit_dominant_normalized_weight'].min)..$($metrics['reflection_probe_receiver_audit_dominant_normalized_weight'].max)" `
            -Expected "nonzero weights, dominant slot >=0 and >0.5"
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "steady receiver weighted probes have cubemap resources" `
            -Passed (
                ($receiverAuditPositiveWeightMask -band (-bnot $receiverAuditReadyCubemapMask)) -eq 0
            ) `
            -Actual "last weighted=0x$([Convert]::ToString($receiverAuditPositiveWeightMask, 16)),ready=0x$([Convert]::ToString($receiverAuditReadyCubemapMask, 16))" `
            -Expected "last weighted mask subset of ready mask"
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver box-projected probes hit their volumes" `
            -Passed (Test-AllMasksSubset -Rows $rows -SubsetName "reflection_probe_receiver_audit_positive_weight_mask" -SupersetName "reflection_probe_receiver_audit_box_projection_hit_mask") `
            -Actual "last weighted=0x$([Convert]::ToString($receiverAuditPositiveWeightMask, 16)),hits=0x$([Convert]::ToString($receiverAuditBoxProjectionHitMask, 16))" `
            -Expected "weighted mask subset of box-projection hit mask"
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver weight fields are internally consistent" `
            -Passed (Test-AllReceiverAuditWeightSums -Rows $rows) `
            -Actual "last rawSum=$receiverAuditRawWeightSum,total=$($metrics['reflection_probe_receiver_audit_total_weight'].last),normalizedSum=$receiverAuditNormalizedWeightSum" `
            -Expected "raw sum ~= total, normalized sum ~= 1"
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver roughness resolves to filtered cubemap LODs" `
            -Passed (
                $metrics["reflection_probe_receiver_audit_local_cubemap_weight"].min -gt 0.99 -and
                $receiverAuditMaxLod -gt 0.0
            ) `
            -Actual "cubemapWeight=$($metrics['reflection_probe_receiver_audit_local_cubemap_weight'].min)..$($metrics['reflection_probe_receiver_audit_local_cubemap_weight'].max),lods=$($receiverAuditResolvedLods -join '/')" `
            -Expected "cubemap weight > 0.99 and any lod > 0"
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver local coverage stays bounded" `
            -Passed (
                $metrics["reflection_probe_receiver_audit_local_coverage"].max -gt 0.0 -and
                $metrics["reflection_probe_receiver_audit_local_coverage"].min -ge 0.0 -and
                $metrics["reflection_probe_receiver_audit_local_coverage"].max -le 1.0001
            ) `
            -Actual "$($metrics['reflection_probe_receiver_audit_local_coverage'].min)..$($metrics['reflection_probe_receiver_audit_local_coverage'].max)" `
            -Expected "0 < coverage <= 1"
        Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "captured probes use neutral receiver tint" `
            -Passed ($metrics["reflection_probe_captured_scene_neutral_tint_mask"].max -ne 0) `
            -Actual "0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_neutral_tint_mask'].max, 16))" `
            -Expected "nonzero"

        if ($Mode -eq "spatial" -or $Mode -eq "parallax") {
            Add-BooleanCheck -Checks $checks -Area "receiver-audit" -Name "receiver traversal changes blend state" `
                -Passed (
                    $metrics["reflection_probe_receiver_audit_dominant_slot"].delta -gt 0 -or
                    $metrics["reflection_probe_receiver_audit_total_weight"].delta -gt 0.0001 -or
                    $metrics["reflection_probe_receiver_audit_local_coverage"].delta -gt 0.0001
                ) `
                -Actual "dominantDelta=$($metrics['reflection_probe_receiver_audit_dominant_slot'].delta),totalDelta=$($metrics['reflection_probe_receiver_audit_total_weight'].delta),coverageDelta=$($metrics['reflection_probe_receiver_audit_local_coverage'].delta)" `
                -Expected "dominant slot, total weight, or coverage changes"
        }
    }

    switch ($Mode) {
    "static" {
        Add-BooleanCheck -Checks $checks -Area "policy" -Name "static capture is reused" `
            -Passed ($metrics["reflection_probe_captured_scene_upload_count"].delta -eq 0) `
            -Actual $metrics["reflection_probe_captured_scene_upload_count"].delta -Expected 0
        Add-BooleanCheck -Checks $checks -Area "policy" -Name "static lane performs no later refresh" `
            -Passed ($metrics["reflection_probe_captured_scene_refresh_performed"].max -eq 0) `
            -Actual $metrics["reflection_probe_captured_scene_refresh_performed"].max -Expected 0
        Add-BooleanCheck -Checks $checks -Area "policy" -Name "initial refresh remains auditable" `
            -Passed (Test-AnyValue -Rows $rows -Name "reflection_probe_captured_scene_last_refresh_reason" -Expected 1) `
            -Actual $metrics["reflection_probe_captured_scene_last_refresh_reason"].last -Expected 1
        Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "static capture reuses directional shadow data" `
            -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].delta -eq 0) `
            -Actual $metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].delta -Expected 0
        Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "static capture reuses local shadow data" `
            -Passed ($metrics["reflection_probe_captured_scene_local_shadow_pass_count"].delta -eq 0) `
            -Actual $metrics["reflection_probe_captured_scene_local_shadow_pass_count"].delta -Expected 0
    }
    "persistent-hit" {
        Add-BooleanCheck -Checks $checks -Area "snapshot" -Name "scene-dirty control completes more than one capture" `
            -Passed ($metrics["reflection_probe_captured_scene_upload_count"].delta -ge 1) `
            -Actual $metrics["reflection_probe_captured_scene_upload_count"].delta -Expected ">= 1"
    }
    "persistent-off" {
        Add-BooleanCheck -Checks $checks -Area "fallback" -Name "persistent cache disable is explicit" `
            -Passed (
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_enabled"].max -eq 0 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_hit"].max -eq 0 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_hit_count"].max -eq 0 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_cache_resource_count"].max -eq 0 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_cache_slot"].max -eq -1
            ) `
            -Actual "enabled/hit/hits=$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_enabled'].max)/$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_hit'].max)/$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_hit_count'].max),slot/resources=$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_cache_slot'].max)/$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_cache_resource_count'].max)" `
            -Expected "enabled=0, hit/hits=0, slot=-1, resources=0"
        Add-BooleanCheck -Checks $checks -Area "fallback" -Name "disabled persistent cache rebuilds each repeated capture" `
            -Passed ($metrics["reflection_probe_captured_scene_upload_count"].delta -ge 1) `
            -Actual $metrics["reflection_probe_captured_scene_upload_count"].delta -Expected ">= 1"
    }
    "light" {
        Add-BooleanCheck -Checks $checks -Area "invalidation" -Name "light revision advances" `
            -Passed ($metrics["reflection_probe_captured_scene_light_revision"].delta -gt 0) `
            -Actual $metrics["reflection_probe_captured_scene_light_revision"].delta -Expected "> 0"
        Add-BooleanCheck -Checks $checks -Area "invalidation" -Name "light motion uploads a new capture" `
            -Passed ($metrics["reflection_probe_captured_scene_upload_count"].delta -gt 0) `
            -Actual $metrics["reflection_probe_captured_scene_upload_count"].delta -Expected "> 0"
        Add-BooleanCheck -Checks $checks -Area "invalidation" -Name "light reason recorded" `
            -Passed (Test-AnyValue -Rows $rows -Name "reflection_probe_captured_scene_refresh_reason" -Expected 6) `
            -Actual $metrics["reflection_probe_captured_scene_refresh_reason"].max -Expected 6
        Add-BooleanCheck -Checks $checks -Area "invalidation" -Name "light dirty flag recorded" `
            -Passed (Test-AnyMask -Rows $rows -Name "reflection_probe_captured_scene_dirty_mask" -Mask 2) `
            -Actual $metrics["reflection_probe_captured_scene_dirty_mask"].max -Expected "mask 0x2"
        Add-BooleanCheck -Checks $checks -Area "snapshot" -Name "light refresh completes a fresh shadow snapshot" `
            -Passed (
                $metrics["reflection_probe_captured_scene_shadow_snapshot_build_count"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_ready"].max -eq 1
            ) `
            -Actual "build=$($metrics['reflection_probe_captured_scene_shadow_snapshot_build_count'].max),ready=$($metrics['reflection_probe_captured_scene_shadow_snapshot_ready'].max)" `
            -Expected "build=1,ready=1"
    }
    "budget" {
        Add-BooleanCheck -Checks $checks -Area "budget" -Name "moving light advances the scene revision" `
            -Passed ($metrics["reflection_probe_captured_scene_light_revision"].delta -gt 0) `
            -Actual $metrics["reflection_probe_captured_scene_light_revision"].delta -Expected "> 0"
        Add-BooleanCheck -Checks $checks -Area "budget" -Name "minimum refresh interval is applied" `
            -Passed ($metrics["reflection_probe_captured_scene_minimum_refresh_interval_frames"].max -eq 64) `
            -Actual $metrics["reflection_probe_captured_scene_minimum_refresh_interval_frames"].max -Expected 64
        Add-BooleanCheck -Checks $checks -Area "budget" -Name "cooldown defers refresh work" `
            -Passed (
                $metrics["reflection_probe_captured_scene_refresh_deferred_count"].delta -gt 0 -and
                $metrics["reflection_probe_captured_scene_refresh_deferred_by_budget"].max -eq 1
            ) `
            -Actual "count delta=$($metrics['reflection_probe_captured_scene_refresh_deferred_count'].delta),flag=$($metrics['reflection_probe_captured_scene_refresh_deferred_by_budget'].max)" `
            -Expected "count delta > 0, flag=1"
        Add-BooleanCheck -Checks $checks -Area "budget" -Name "cooldown prevents an additional completed upload" `
            -Passed ($metrics["reflection_probe_captured_scene_upload_count"].delta -eq 0) `
            -Actual $metrics["reflection_probe_captured_scene_upload_count"].delta -Expected 0
    }
    "locality" {
        Add-BooleanCheck -Checks $checks -Area "locality" -Name "near and distant probe resources allocate" `
            -Passed ($metrics["reflection_probe_captured_scene_probe_resource_count"].max -ge 2) `
            -Actual $metrics["reflection_probe_captured_scene_probe_resource_count"].max -Expected ">= 2"
        Add-BooleanCheck -Checks $checks -Area "locality" -Name "moving light advances the global revision" `
            -Passed ($metrics["reflection_probe_captured_scene_light_revision"].delta -gt 0) `
            -Actual $metrics["reflection_probe_captured_scene_light_revision"].delta -Expected "> 0"
        Add-BooleanCheck -Checks $checks -Area "locality" -Name "out-of-range probe ignores the global light revision" `
            -Passed ($metrics["reflection_probe_captured_scene_locality_ignored_light_revision_count"].max -ge 1) `
            -Actual $metrics["reflection_probe_captured_scene_locality_ignored_light_revision_count"].max -Expected ">= 1"
        Add-BooleanCheck -Checks $checks -Area "locality" -Name "near probe records local light attribution" `
            -Passed (
                (Test-AnyValue -Rows $rows -Name "reflection_probe_captured_scene_local_light_dirty" -Expected 1) -and
                (Test-AnyGreater -Rows $rows -Name "reflection_probe_captured_scene_dirty_local_light_count" -Threshold 0) -and
                (Test-AnyGreater -Rows $rows -Name "reflection_probe_captured_scene_local_light_identity_mask" -Threshold 0) -and
                (Test-AnyGreater -Rows $rows -Name "reflection_probe_captured_scene_local_light_region_mask" -Threshold 0)
            ) `
            -Actual "dirty=$($metrics['reflection_probe_captured_scene_local_light_dirty'].max),count=$($metrics['reflection_probe_captured_scene_dirty_local_light_count'].max),identity=0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_local_light_identity_mask'].max, 16)),region=0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_local_light_region_mask'].max, 16))" `
            -Expected "dirty/count/identity/region > 0"
    }
    "selective-fallback" {
        Add-BooleanCheck -Checks $checks -Area "fallback" -Name "global fallback receives moving-light invalidation" `
            -Passed (
                $metrics["reflection_probe_captured_scene_light_revision"].delta -gt 0 -and
                $metrics["reflection_probe_captured_scene_upload_count"].delta -gt 0
            ) `
            -Actual "light=$($metrics['reflection_probe_captured_scene_light_revision'].delta),uploads=$($metrics['reflection_probe_captured_scene_upload_count'].delta)" `
            -Expected "both > 0"
    }
    "object" {
        Add-BooleanCheck -Checks $checks -Area "invalidation" -Name "render revision advances" `
            -Passed ($metrics["reflection_probe_captured_scene_render_revision"].delta -gt 0) `
            -Actual $metrics["reflection_probe_captured_scene_render_revision"].delta -Expected "> 0"
        Add-BooleanCheck -Checks $checks -Area "invalidation" -Name "object motion uploads a new capture" `
            -Passed ($metrics["reflection_probe_captured_scene_upload_count"].delta -gt 0) `
            -Actual $metrics["reflection_probe_captured_scene_upload_count"].delta -Expected "> 0"
        Add-BooleanCheck -Checks $checks -Area "invalidation" -Name "render reason recorded" `
            -Passed (Test-AnyValue -Rows $rows -Name "reflection_probe_captured_scene_refresh_reason" -Expected 7) `
            -Actual $metrics["reflection_probe_captured_scene_refresh_reason"].max -Expected 7
        Add-BooleanCheck -Checks $checks -Area "invalidation" -Name "render dirty flag recorded" `
            -Passed (Test-AnyMask -Rows $rows -Name "reflection_probe_captured_scene_dirty_mask" -Mask 4) `
            -Actual $metrics["reflection_probe_captured_scene_dirty_mask"].max -Expected "mask 0x4"
        Add-BooleanCheck -Checks $checks -Area "invalidation" -Name "object motion records geometry attribution" `
            -Passed (
                (Test-AnyValue -Rows $rows -Name "reflection_probe_captured_scene_geometry_dirty" -Expected 1) -and
                (Test-AnyGreater -Rows $rows -Name "reflection_probe_captured_scene_dirty_renderable_count" -Threshold 0) -and
                (Test-AnyGreater -Rows $rows -Name "reflection_probe_captured_scene_geometry_identity_mask" -Threshold 0) -and
                (Test-AnyGreater -Rows $rows -Name "reflection_probe_captured_scene_geometry_region_mask" -Threshold 0)
            ) `
            -Actual "dirty=$($metrics['reflection_probe_captured_scene_geometry_dirty'].max),count=$($metrics['reflection_probe_captured_scene_dirty_renderable_count'].max),identity=0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_geometry_identity_mask'].max, 16)),region=0x$([Convert]::ToString([int]$metrics['reflection_probe_captured_scene_geometry_region_mask'].max, 16))" `
            -Expected "dirty/count/identity/region > 0"
        Add-BooleanCheck -Checks $checks -Area "snapshot" -Name "object refresh completes a fresh shadow snapshot" `
            -Passed (
                $metrics["reflection_probe_captured_scene_shadow_snapshot_build_count"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_shadow_snapshot_ready"].max -eq 1
            ) `
            -Actual "build=$($metrics['reflection_probe_captured_scene_shadow_snapshot_build_count'].max),ready=$($metrics['reflection_probe_captured_scene_shadow_snapshot_ready'].max)" `
            -Expected "build=1,ready=1"
    }
    "camera" {
        Add-BooleanCheck -Checks $checks -Area "invariance" -Name "camera orbit advances" `
            -Passed ($metrics["benchmark_camera_motion_time_seconds"].max -gt 0) `
            -Actual $metrics["benchmark_camera_motion_time_seconds"].max -Expected "> 0"
        Add-BooleanCheck -Checks $checks -Area "invariance" -Name "camera does not upload capture" `
            -Passed ($metrics["reflection_probe_captured_scene_upload_count"].delta -eq 0) `
            -Actual $metrics["reflection_probe_captured_scene_upload_count"].delta -Expected 0
        Add-BooleanCheck -Checks $checks -Area "invariance" -Name "camera does not request refresh" `
            -Passed ($metrics["reflection_probe_captured_scene_refresh_performed"].max -eq 0) `
            -Actual $metrics["reflection_probe_captured_scene_refresh_performed"].max -Expected 0
        Add-BooleanCheck -Checks $checks -Area "invariance" -Name "camera preserves scene revisions" `
            -Passed (
                $metrics["reflection_probe_captured_scene_light_revision"].delta -eq 0 -and
                $metrics["reflection_probe_captured_scene_render_revision"].delta -eq 0
            ) `
            -Actual "light=$($metrics['reflection_probe_captured_scene_light_revision'].delta),render=$($metrics['reflection_probe_captured_scene_render_revision'].delta)" `
            -Expected "0/0"
        Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "camera motion does not redraw capture directional shadows" `
            -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].delta -eq 0) `
            -Actual $metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].delta -Expected 0
        Add-BooleanCheck -Checks $checks -Area "capture-local-shadow" -Name "camera motion does not redraw capture local shadows" `
            -Passed ($metrics["reflection_probe_captured_scene_local_shadow_pass_count"].delta -eq 0) `
            -Actual $metrics["reflection_probe_captured_scene_local_shadow_pass_count"].delta -Expected 0
    }
    "multi" {
        Add-BooleanCheck -Checks $checks -Area "mapping" -Name "three captured probe resources allocate" `
            -Passed ($metrics["reflection_probe_captured_scene_probe_resource_count"].max -ge 3) `
            -Actual $metrics["reflection_probe_captured_scene_probe_resource_count"].max -Expected ">= 3"
        Add-BooleanCheck -Checks $checks -Area "mapping" -Name "three captured probe resources become ready" `
            -Passed ($metrics["reflection_probe_captured_scene_ready_probe_count"].max -ge 3) `
            -Actual $metrics["reflection_probe_captured_scene_ready_probe_count"].max -Expected ">= 3"
        Add-BooleanCheck -Checks $checks -Area "mapping" -Name "active views remain probe-distinct" `
            -Passed ($metrics["reflection_probe_captured_scene_distinct_active_view_count"].max -ge 3) `
            -Actual $metrics["reflection_probe_captured_scene_distinct_active_view_count"].max -Expected ">= 3"
        Add-BooleanCheck -Checks $checks -Area "mapping" -Name "selected probes do not share active image views" `
            -Passed ($metrics["reflection_probe_selected_captured_scene_duplicate_active_view_mask"].max -eq 0) `
            -Actual $metrics["reflection_probe_selected_captured_scene_duplicate_active_view_mask"].max -Expected 0
        Add-BooleanCheck -Checks $checks -Area "diffuse" -Name "three captured probe irradiance maps become ready" `
            -Passed ($metrics["reflection_probe_captured_scene_diffuse_irradiance_ready_probe_count"].max -ge 3) `
            -Actual $metrics["reflection_probe_captured_scene_diffuse_irradiance_ready_probe_count"].max -Expected ">= 3"
        Add-BooleanCheck -Checks $checks -Area "diffuse" -Name "active irradiance views remain probe-distinct" `
            -Passed ($metrics["reflection_probe_captured_scene_distinct_active_diffuse_irradiance_view_count"].max -ge 3) `
            -Actual $metrics["reflection_probe_captured_scene_distinct_active_diffuse_irradiance_view_count"].max -Expected ">= 3"
        Add-BooleanCheck -Checks $checks -Area "diffuse" -Name "selected probes do not share irradiance image views" `
            -Passed ($metrics["reflection_probe_selected_captured_scene_diffuse_irradiance_duplicate_active_view_mask"].max -eq 0) `
            -Actual $metrics["reflection_probe_selected_captured_scene_diffuse_irradiance_duplicate_active_view_mask"].max -Expected 0
        $selectedMipCounts = @(
            $metrics["reflection_probe_selected_capture_mip_count_0"].max,
            $metrics["reflection_probe_selected_capture_mip_count_1"].max,
            $metrics["reflection_probe_selected_capture_mip_count_2"].max
        )
        Add-BooleanCheck -Checks $checks -Area "sampling" -Name "selected probes publish their real mip counts" `
            -Passed (@($selectedMipCounts | Where-Object { $_ -ge 2 }).Count -eq 3) `
            -Actual $selectedMipCounts -Expected "three counts >= 2"
        Add-BooleanCheck -Checks $checks -Area "snapshot-cache" -Name "three probes stay within the bounded persistent shadow cache" `
            -Passed (
                $metrics["reflection_probe_captured_scene_shadow_snapshot_persistent_enabled"].max -eq 1 -and
                $metrics["reflection_probe_captured_scene_persistent_shadow_cache_capacity"].max -eq 2 -and
                $metrics["reflection_probe_captured_scene_persistent_shadow_cache_resource_count"].max -eq 2 -and
                $metrics["reflection_probe_captured_scene_persistent_shadow_cache_eviction_count"].max -ge 1 -and
                $metrics["reflection_probe_captured_scene_persistent_shadow_cache_probe_scene_index_0"].max -ge 0 -and
                $metrics["reflection_probe_captured_scene_persistent_shadow_cache_probe_scene_index_1"].max -ge 0 -and
                $metrics["reflection_probe_captured_scene_persistent_shadow_cache_probe_scene_index_0"].max -ne
                    $metrics["reflection_probe_captured_scene_persistent_shadow_cache_probe_scene_index_1"].max -and
                $metrics["reflection_probe_captured_scene_persistent_shadow_cache_input_signature_0"].max -gt 0 -and
                $metrics["reflection_probe_captured_scene_persistent_shadow_cache_input_signature_1"].max -gt 0
            ) `
            -Actual "enabled=$($metrics['reflection_probe_captured_scene_shadow_snapshot_persistent_enabled'].max),capacity/resources/evictions=$($metrics['reflection_probe_captured_scene_persistent_shadow_cache_capacity'].max)/$($metrics['reflection_probe_captured_scene_persistent_shadow_cache_resource_count'].max)/$($metrics['reflection_probe_captured_scene_persistent_shadow_cache_eviction_count'].max),probes=$($metrics['reflection_probe_captured_scene_persistent_shadow_cache_probe_scene_index_0'].max)/$($metrics['reflection_probe_captured_scene_persistent_shadow_cache_probe_scene_index_1'].max),signatures=$($metrics['reflection_probe_captured_scene_persistent_shadow_cache_input_signature_0'].max)/$($metrics['reflection_probe_captured_scene_persistent_shadow_cache_input_signature_1'].max)" `
            -Expected "enabled=1, capacity/resources=2, evictions>=1, distinct probe owners, nonzero signatures"
    }
    "spatial" {
        Add-BooleanCheck -Checks $checks -Area "spatial" -Name "LightingShowcase traversal advances camera time" `
            -Passed ($metrics["benchmark_camera_motion_time_seconds"].max -gt 0.0) `
            -Actual $metrics["benchmark_camera_motion_time_seconds"].max -Expected "> 0"
        Add-BooleanCheck -Checks $checks -Area "spatial" -Name "traversal keeps three captured probes ready" `
            -Passed (
                $metrics["reflection_probe_selected_probe_count"].max -ge 3 -and
                $metrics["reflection_probe_captured_scene_ready_probe_count"].max -ge 3 -and
                (($metrics["reflection_probe_selected_capture_mip_ready_mask"].max -band 7) -eq 7)
            ) `
            -Actual "selected=$($metrics['reflection_probe_selected_probe_count'].max),ready=$($metrics['reflection_probe_captured_scene_ready_probe_count'].max),mips=0x$([Convert]::ToString([int]$metrics['reflection_probe_selected_capture_mip_ready_mask'].max, 16))" `
            -Expected "selected/ready>=3,mip-ready mask includes 0x7"
        Add-BooleanCheck -Checks $checks -Area "spatial" -Name "camera traversal changes resolved probe blend" `
            -Passed ($metrics["reflection_probe_max_blend_weight"].delta -gt 0.0001) `
            -Actual $metrics["reflection_probe_max_blend_weight"].delta -Expected "> 0.0001"
    }
    "parallax" {
        $expectedBoxMask = 7
        Add-BooleanCheck -Checks $checks -Area "parallax" -Name "captured probes enable box projection" `
            -Passed (
                (($metrics["reflection_probe_selected_box_projection_mask"].max -band $expectedBoxMask) -eq $expectedBoxMask) -and
                (($metrics["reflection_probe_selected_captured_scene_box_projection_mask"].max -band $expectedBoxMask) -eq $expectedBoxMask)
            ) `
            -Actual "box=0x$([Convert]::ToString([int]$metrics['reflection_probe_selected_box_projection_mask'].max, 16)),captured=0x$([Convert]::ToString([int]$metrics['reflection_probe_selected_captured_scene_box_projection_mask'].max, 16))" `
            -Expected "box/captured mask includes 0x7"
        Add-BooleanCheck -Checks $checks -Area "parallax" -Name "box-projection reference rays hit and change direction" `
            -Passed (
                (($metrics["reflection_probe_selected_box_projection_ray_hit_mask"].max -band $expectedBoxMask) -eq $expectedBoxMask) -and
                (($metrics["reflection_probe_selected_box_projection_direction_changed_mask"].max -band $expectedBoxMask) -eq $expectedBoxMask)
            ) `
            -Actual "hit=0x$([Convert]::ToString([int]$metrics['reflection_probe_selected_box_projection_ray_hit_mask'].max, 16)),changed=0x$([Convert]::ToString([int]$metrics['reflection_probe_selected_box_projection_direction_changed_mask'].max, 16))" `
            -Expected "hit/changed mask includes 0x7"
        Add-BooleanCheck -Checks $checks -Area "parallax" -Name "outward box rays use explicit fallback" `
            -Passed (($metrics["reflection_probe_selected_box_projection_outside_fallback_mask"].max -band $expectedBoxMask) -eq $expectedBoxMask) `
            -Actual "0x$([Convert]::ToString([int]$metrics['reflection_probe_selected_box_projection_outside_fallback_mask'].max, 16))" `
            -Expected "fallback mask includes 0x7"
        Add-BooleanCheck -Checks $checks -Area "parallax" -Name "parallax traversal advances camera time and blends" `
            -Passed (
                $metrics["benchmark_camera_motion_time_seconds"].max -gt 0.0 -and
                $metrics["reflection_probe_max_blend_weight"].delta -gt 0.0001
            ) `
            -Actual "camera=$($metrics['benchmark_camera_motion_time_seconds'].max),blend delta=$($metrics['reflection_probe_max_blend_weight'].delta)" `
            -Expected "camera > 0, blend delta > 0.0001"
    }
    }

    $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
    $warnCount = @($checks | Where-Object { $_.status -eq "warn" }).Count
    $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
    return [pscustomobject]@{
        lane = $LaneName
        mode = $Mode
        csv = $CsvPath
        verdict = if ($failCount -gt 0) { "fail" } elseif ($warnCount -gt 0) { "warn" } else { "pass" }
        passCount = $passCount
        warnCount = $warnCount
        failCount = $failCount
        metrics = [pscustomobject]$metrics
        checks = @($checks)
    }
}

if (-not $SkipBuild) {
    $buildCommand =
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cd /d "{0}\build" && MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo' -f $repoRoot
    cmd /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "SelfEngineForward3D build failed with exit code $LASTEXITCODE"
    }
}

$resolvedExecutable = Resolve-FullPath -Path $ExecutablePath
if (-not (Test-Path -LiteralPath $resolvedExecutable)) {
    throw "Executable not found: $resolvedExecutable"
}
$resolvedOutput = Resolve-FullPath -Path $OutputDirectory
New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null

$managedKeys = @(
    "SE_WINDOW_WIDTH",
    "SE_WINDOW_HEIGHT",
    "SE_WINDOW_BORDERLESS",
    "SE_VISUAL_QA_HIDE_IMGUI",
    "SE_HIDE_IMGUI",
    "SE_BENCHMARK_SCENE",
    "SELFENGINE_MODEL_PATH",
    "SE_UE_BRIDGE_MANIFEST",
    "SE_BENCHMARK_GRID_SIZE",
    "SE_BENCHMARK_PARTIAL_LOCAL_SHADOW_CACHE",
    "SE_SCENE_REFLECTION_PROBE",
    "SE_SCENE_REFLECTION_PROBE_CAPTURED",
    "SE_FORWARD3D_DEBUG_DEFAULT_SCENE",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_SCENE_UPDATE_FREEZE",
    "SE_FORWARD3D_AA_MODE",
    "SE_RENDER_VIEW",
    "SE_BENCHMARK_CAMERA_MOTION",
    "SE_BENCHMARK_CAMERA_MOTION_SPEED",
    "SE_BENCHMARK_CAMERA_MOTION_YAW",
    "SE_BENCHMARK_CAMERA_MOTION_PITCH",
    "SE_BENCHMARK_CAMERA_MOTION_DISTANCE",
    "SE_BENCHMARK_OBJECT_MOTION",
    "SE_REFLECTION_CAPTURE_CAMERA_INVARIANT_CONTROL",
    "SE_REFLECTION_PROBE_CAPTURE_SOURCE",
    "SE_REFLECTION_PROBE_CAPTURE_BACKEND",
    "SE_REFLECTION_PROBE_REFRESH_POLICY",
    "SE_REFLECTION_PROBE_FORCE_REFRESH",
    "SE_REFLECTION_PROBE_SCENE_DIRTY",
    "SE_REFLECTION_CAPTURE_RECT_SHADOWS_OFF",
    "SE_REFLECTION_CAPTURE_SHADOW_SNAPSHOT_OFF",
    "SE_REFLECTION_CAPTURE_PERSISTENT_SHADOW_CACHE_OFF",
    "SE_REFLECTION_CAPTURE_FILTER_QUALITY",
    "SE_REFLECTION_CAPTURE_FACE_SIZE",
    "SE_REFLECTION_PROBE_FORCE_MIP0",
    "SE_REFLECTION_PROBE_DOMINANT_MIRROR_HARD_SWITCH",
    "SE_REFLECTION_PROBE_DOMINANT_MIRROR_OFF",
    "SE_RECT_LIGHT_ANALYTIC_SPECULAR_OFF",
    "SE_REFLECTION_CAPTURE_REFRESH_MIN_FRAMES",
    "SE_REFLECTION_CAPTURE_SELECTIVE_REFRESH_OFF",
    "SE_REFLECTION_CAPTURE_LOCALITY_CONTROL",
    "SE_REFLECTION_RECEIVER_AUDIT",
    "SE_REFLECTION_RECEIVER_AUDIT_X",
    "SE_REFLECTION_RECEIVER_AUDIT_Y",
    "SE_REFLECTION_RECEIVER_AUDIT_Z",
    "SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_X",
    "SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_Y",
    "SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_Z",
    "SE_REFLECTION_RECEIVER_AUDIT_ROUGHNESS",
    "SE_REFLECTION_PROBE_LEGACY_BLEND",
    "SE_REFLECTION_PROBE_LEGACY_ENERGY_SCALE",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_AUTO_EXIT_FRAMES",
    "SE_BENCHMARK_CSV"
)

$commonEnvironment = @{
    SE_WINDOW_WIDTH = "1280"
    SE_WINDOW_HEIGHT = "720"
    SE_WINDOW_BORDERLESS = "0"
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_HIDE_IMGUI = "1"
    SE_FORWARD3D_AA_MODE = "taa"
    SE_RENDER_VIEW = "lit"
    SE_REFLECTION_PROBE_CAPTURE_SOURCE = "captured"
    SE_REFLECTION_PROBE_CAPTURE_BACKEND = "gpu"
    SE_REFLECTION_PROBE_REFRESH_POLICY = "scene-dirty"
    SE_BENCHMARK_WARMUP_FRAMES = [string]$WarmupFrames
    SE_BENCHMARK_FRAMES = [string]$CaptureFrames
    SE_AUTO_EXIT_FRAMES = [string]$AutoExitFrames
}

$receiverAuditEnvironment = @{
    SE_REFLECTION_RECEIVER_AUDIT = "1"
    SE_REFLECTION_RECEIVER_AUDIT_X = "-2.15"
    SE_REFLECTION_RECEIVER_AUDIT_Y = "0.18"
    SE_REFLECTION_RECEIVER_AUDIT_Z = "0.15"
    SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_X = "0.35"
    SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_Y = "0.18"
    SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_Z = "-0.92"
    SE_REFLECTION_RECEIVER_AUDIT_ROUGHNESS = "0.24"
}

$laneSpecs = @(
    [pscustomobject]@{
        name = "static-scene-reuse"
        mode = "static"
        environment = @{
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
        }
    },
    [pscustomobject]@{
        name = "persistent-shadow-reuse"
        mode = "persistent-hit"
        environment = @{
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_REFLECTION_PROBE_SCENE_DIRTY = "1"
            SE_REFLECTION_CAPTURE_REFRESH_MIN_FRAMES = "0"
            SE_BENCHMARK_WARMUP_FRAMES = "0"
            SE_BENCHMARK_FRAMES = "18"
            SE_AUTO_EXIT_FRAMES = "26"
        }
    },
    [pscustomobject]@{
        name = "persistent-shadow-cache-disabled"
        mode = "persistent-off"
        environment = @{
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_REFLECTION_PROBE_SCENE_DIRTY = "1"
            SE_REFLECTION_CAPTURE_REFRESH_MIN_FRAMES = "0"
            SE_REFLECTION_CAPTURE_PERSISTENT_SHADOW_CACHE_OFF = "1"
            SE_BENCHMARK_WARMUP_FRAMES = "0"
            SE_BENCHMARK_FRAMES = "18"
            SE_AUTO_EXIT_FRAMES = "26"
        }
    },
    [pscustomobject]@{
        name = "capture-filter-high"
        mode = "filter-high"
        environment = @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_REFLECTION_CAPTURE_FILTER_QUALITY = "high"
        }
    },
    [pscustomobject]@{
        name = "capture-filter-medium-control"
        mode = "filter-medium"
        environment = @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_REFLECTION_CAPTURE_FILTER_QUALITY = "medium"
        }
    },
    [pscustomobject]@{
        name = "capture-filter-fallback"
        mode = "filter-off"
        environment = @{
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_REFLECTION_CAPTURE_FILTER_QUALITY = "off"
        }
    },
    [pscustomobject]@{
        name = "probe-force-mip0-control"
        mode = "filter-high"
        environment = @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_REFLECTION_PROBE_FORCE_MIP0 = "1"
        }
    },
    [pscustomobject]@{
        name = "moving-light-refresh"
        mode = "light"
        environment = @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_SCENE_REFLECTION_PROBE = "1"
            SE_SCENE_REFLECTION_PROBE_CAPTURED = "1"
            SE_BENCHMARK_PARTIAL_LOCAL_SHADOW_CACHE = "1"
            SE_REFLECTION_CAPTURE_REFRESH_MIN_FRAMES = "0"
        }
    },
    [pscustomobject]@{
        name = "moving-rigid-refresh"
        mode = "object"
        environment = @{
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_BENCHMARK_OBJECT_MOTION = "orbit"
            SE_REFLECTION_CAPTURE_REFRESH_MIN_FRAMES = "0"
        }
    },
    [pscustomobject]@{
        name = "refresh-budget-cooldown"
        mode = "budget"
        environment = @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_SCENE_REFLECTION_PROBE = "1"
            SE_SCENE_REFLECTION_PROBE_CAPTURED = "1"
            SE_BENCHMARK_PARTIAL_LOCAL_SHADOW_CACHE = "1"
            SE_REFLECTION_CAPTURE_REFRESH_MIN_FRAMES = "64"
        }
    },
    [pscustomobject]@{
        name = "selective-locality-control"
        mode = "locality"
        environment = @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_SCENE_REFLECTION_PROBE = "1"
            SE_SCENE_REFLECTION_PROBE_CAPTURED = "1"
            SE_BENCHMARK_PARTIAL_LOCAL_SHADOW_CACHE = "1"
            SE_REFLECTION_CAPTURE_LOCALITY_CONTROL = "1"
        }
    },
    [pscustomobject]@{
        name = "selective-refresh-fallback"
        mode = "selective-fallback"
        environment = @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_SCENE_REFLECTION_PROBE = "1"
            SE_SCENE_REFLECTION_PROBE_CAPTURED = "1"
            SE_BENCHMARK_PARTIAL_LOCAL_SHADOW_CACHE = "1"
            SE_REFLECTION_CAPTURE_REFRESH_MIN_FRAMES = "0"
            SE_REFLECTION_CAPTURE_SELECTIVE_REFRESH_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "camera-invariant"
        mode = "camera"
        environment = @{
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_BENCHMARK_CAMERA_MOTION = "orbit"
            SE_REFLECTION_CAPTURE_CAMERA_INVARIANT_CONTROL = "1"
        }
    },
    [pscustomobject]@{
        name = "receiver-audit-grid-camera"
        mode = "camera"
        environment = $receiverAuditEnvironment + @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_SCENE_REFLECTION_PROBE = "1"
            SE_SCENE_REFLECTION_PROBE_CAPTURED = "1"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_BENCHMARK_CAMERA_MOTION = "orbit"
            SE_REFLECTION_CAPTURE_CAMERA_INVARIANT_CONTROL = "1"
        }
    },
    [pscustomobject]@{
        name = "receiver-audit-grid-legacy"
        mode = "camera"
        environment = $receiverAuditEnvironment + @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_SCENE_REFLECTION_PROBE = "1"
            SE_SCENE_REFLECTION_PROBE_CAPTURED = "1"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_BENCHMARK_CAMERA_MOTION = "orbit"
            SE_REFLECTION_CAPTURE_CAMERA_INVARIANT_CONTROL = "1"
            SE_REFLECTION_PROBE_LEGACY_BLEND = "1"
            SE_REFLECTION_PROBE_LEGACY_ENERGY_SCALE = "1"
        }
    },
    [pscustomobject]@{
        name = "multi-probe-identity"
        mode = "multi"
        environment = @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
        }
    },
    [pscustomobject]@{
        name = "spatial-blend-traversal"
        mode = "spatial"
        environment = $receiverAuditEnvironment + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_BENCHMARK_CAMERA_MOTION = "1"
            SE_BENCHMARK_CAMERA_MOTION_SPEED = "5.0"
            SE_BENCHMARK_CAMERA_MOTION_YAW = "1.35"
            SE_BENCHMARK_CAMERA_MOTION_PITCH = "0.18"
            SE_BENCHMARK_CAMERA_MOTION_DISTANCE = "4.5"
            SE_BENCHMARK_WARMUP_FRAMES = "8"
            SE_BENCHMARK_FRAMES = "60"
            SE_AUTO_EXIT_FRAMES = "74"
        }
    },
    [pscustomobject]@{
        name = "box-parallax-traversal"
        mode = "parallax"
        environment = $receiverAuditEnvironment + @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_BENCHMARK_CAMERA_MOTION = "1"
            SE_BENCHMARK_CAMERA_MOTION_SPEED = "5.0"
            SE_BENCHMARK_CAMERA_MOTION_YAW = "1.35"
            SE_BENCHMARK_CAMERA_MOTION_PITCH = "0.18"
            SE_BENCHMARK_CAMERA_MOTION_DISTANCE = "4.5"
            SE_BENCHMARK_WARMUP_FRAMES = "8"
            SE_BENCHMARK_FRAMES = "60"
            SE_AUTO_EXIT_FRAMES = "74"
        }
    },
    [pscustomobject]@{
        name = "rect-capture-disabled"
        mode = "rect-off"
        environment = @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_REFLECTION_CAPTURE_RECT_SHADOWS_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "shadow-snapshot-disabled"
        mode = "snapshot-off"
        environment = @{
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_REFLECTION_CAPTURE_SHADOW_SNAPSHOT_OFF = "1"
        }
    }
)

$reports = [System.Collections.Generic.List[object]]::new()
foreach ($lane in $laneSpecs) {
    $laneOutput = Join-Path $resolvedOutput $lane.name
    New-Item -ItemType Directory -Force -Path $laneOutput | Out-Null
    $csvPath = Join-Path $laneOutput "reflection_capture_health.csv"
    Remove-Item -LiteralPath $csvPath -ErrorAction SilentlyContinue

    $environment = @{}
    foreach ($entry in $commonEnvironment.GetEnumerator()) {
        $environment[$entry.Key] = $entry.Value
    }
    foreach ($entry in $lane.environment.GetEnumerator()) {
        $environment[$entry.Key] = $entry.Value
    }
    $environment["SE_BENCHMARK_CSV"] = $csvPath

    $runError = $null
    try {
        Invoke-WithEnvironment -ManagedKeys $managedKeys -Environment $environment -Script {
            # Some development hosts allow signed local binaries through cmd.exe
            # while blocking PowerShell's direct invocation operator.
            $executableDirectory = Split-Path -Parent $resolvedExecutable
            $commandLine = "cd /d `"$executableDirectory`" && `"$resolvedExecutable`""
            & cmd.exe /d /c $commandLine | Out-Host
            if ($LASTEXITCODE -ne 0) {
                throw "SelfEngineForward3D exited with code $LASTEXITCODE"
            }
        }
    } catch {
        $runError = $_.Exception.Message
    }

    $reports.Add((New-ReflectionCaptureReport `
        -LaneName $lane.name `
        -Mode $lane.mode `
        -CsvPath $csvPath `
        -Environment $environment `
        -RunError $runError)) | Out-Null
}

$totalPass = @($reports | ForEach-Object { $_.passCount } | Measure-Object -Sum).Sum
$totalWarn = @($reports | ForEach-Object { $_.warnCount } | Measure-Object -Sum).Sum
$totalFail = @($reports | ForEach-Object { $_.failCount } | Measure-Object -Sum).Sum
$overall = if ([int]$totalFail -gt 0) {
    "fail"
} elseif ([int]$totalWarn -gt 0) {
    "warn"
} else {
    "pass"
}

$summary = [pscustomobject]@{
    generatedAt = (Get-Date).ToString("o")
    executable = $resolvedExecutable
    outputDirectory = $resolvedOutput
    verdict = $overall
    passCount = [int]$totalPass
    warnCount = [int]$totalWarn
    failCount = [int]$totalFail
    reports = @($reports)
}

$jsonPath = Join-Path $resolvedOutput "reflection_capture_health.json"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
$summary

if ($Strict -and $overall -ne "pass") {
    exit 1
}
