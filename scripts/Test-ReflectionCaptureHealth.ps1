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

function New-ReflectionCaptureReport {
    param(
        [Parameter(Mandatory = $true)][string]$LaneName,
        [Parameter(Mandatory = $true)][string]$Mode,
        [Parameter(Mandatory = $true)][string]$CsvPath,
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
        "reflection_probe_captured_scene_face_count",
        "reflection_probe_captured_scene_faces_rendered",
        "reflection_probe_captured_scene_faces_pending",
        "reflection_probe_captured_scene_capture_pass_count",
        "reflection_probe_captured_scene_capture_draw_count",
        "reflection_probe_captured_scene_capture_visible_count",
        "reflection_probe_captured_scene_capture_culled_count",
        "reflection_probe_captured_scene_mip_generation_count",
        "reflection_probe_captured_scene_ggx_prefilter_dispatch_count",
        "reflection_probe_captured_scene_ggx_prefilter_sample_count",
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
        "reflection_probe_captured_scene_last_captured_face",
        "reflection_probe_captured_scene_rasterized_geometry",
        "reflection_probe_captured_scene_gpu_resources_allocated",
        "reflection_probe_captured_scene_gpu_capture_in_progress",
        "reflection_probe_captured_scene_mip_chain_ready",
        "reflection_probe_captured_scene_ggx_prefilter_ready",
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
        "reflection_probe_captured_scene_render_revision"
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
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "rasterized geometry is explicit" `
        -Passed ($metrics["reflection_probe_captured_scene_rasterized_geometry"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_rasterized_geometry"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "GPU capture resources allocate" `
        -Passed ($metrics["reflection_probe_captured_scene_gpu_resources_allocated"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_gpu_resources_allocated"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "six faces reach a completed mip chain" `
        -Passed ($metrics["reflection_probe_captured_scene_mip_chain_ready"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_mip_chain_ready"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "filtering" -Name "GPU GGX prefilter completes" `
        -Passed ($metrics["reflection_probe_captured_scene_ggx_prefilter_ready"].max -eq 1) `
        -Actual $metrics["reflection_probe_captured_scene_ggx_prefilter_ready"].max -Expected 1
    Add-BooleanCheck -Checks $checks -Area "filtering" -Name "GPU GGX prefilter records mip dispatches" `
        -Passed ($metrics["reflection_probe_captured_scene_ggx_prefilter_dispatch_count"].max -gt 0) `
        -Actual $metrics["reflection_probe_captured_scene_ggx_prefilter_dispatch_count"].max -Expected "> 0"
    Add-BooleanCheck -Checks $checks -Area "filtering" -Name "GPU GGX prefilter has a sample budget" `
        -Passed ($metrics["reflection_probe_captured_scene_ggx_prefilter_sample_count"].max -ge 32) `
        -Actual $metrics["reflection_probe_captured_scene_ggx_prefilter_sample_count"].max -Expected ">= 32"
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
    Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "every cubemap face records a directional shadow pass" `
        -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].max -ge 6) `
        -Actual $metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].max -Expected ">= 6"
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
    $captureUploadEvidence = if ($Mode -eq "multi") {
        $metrics["reflection_probe_captured_scene_upload_count"].max
    } else {
        $metrics["reflection_probe_captured_scene_upload_count"].min
    }
    Add-BooleanCheck -Checks $checks -Area "backend" -Name "capture resource uploaded" `
        -Passed ($captureUploadEvidence -ge 1) `
        -Actual $captureUploadEvidence -Expected ">= 1"

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
        Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "light refresh records new directional shadow passes" `
            -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].delta -gt 0) `
            -Actual $metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].delta -Expected "> 0"
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
        Add-BooleanCheck -Checks $checks -Area "capture-shadow" -Name "object refresh records new directional shadow passes" `
            -Passed ($metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].delta -gt 0) `
            -Actual $metrics["reflection_probe_captured_scene_directional_shadow_pass_count"].delta -Expected "> 0"
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
    "SE_BENCHMARK_OBJECT_MOTION",
    "SE_REFLECTION_CAPTURE_CAMERA_INVARIANT_CONTROL",
    "SE_REFLECTION_PROBE_CAPTURE_SOURCE",
    "SE_REFLECTION_PROBE_CAPTURE_BACKEND",
    "SE_REFLECTION_PROBE_REFRESH_POLICY",
    "SE_REFLECTION_PROBE_FORCE_REFRESH",
    "SE_REFLECTION_PROBE_SCENE_DIRTY",
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
        name = "moving-light-refresh"
        mode = "light"
        environment = @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_SCENE_REFLECTION_PROBE = "1"
            SE_SCENE_REFLECTION_PROBE_CAPTURED = "1"
            SE_BENCHMARK_PARTIAL_LOCAL_SHADOW_CACHE = "1"
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
        name = "multi-probe-identity"
        mode = "multi"
        environment = @{
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
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
            & $resolvedExecutable | Out-Host
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
