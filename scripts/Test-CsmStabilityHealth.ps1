param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "out\csm_stability",
    [ValidateSet("low", "medium", "high", "ultra")]
    [string]$ShadowQuality = "ultra",
    [ValidateSet("production", "legacy")]
    [string]$DirectionalShadowFilter = "production",
    [ValidateRange(0.0, 4.0)]
    [double]$DirectionalNormalOffsetBiasTexels = 2.0,
    [ValidateRange(0.0, 2.0)]
    [double]$DirectionalSlopeOffsetBiasTexels = 0.5,
    [ValidateRange(-1.0, 1.0)]
    [double]$DirectionalPcssStrength = -1.0,
    [ValidateRange(0.0, 2000.0)]
    [double]$CascadeMaxDistanceOverride = 0.0,
    [switch]$UseConfiguredNormalOffsetDefault,
    [switch]$UseConfiguredSlopeOffsetDefault,
    [switch]$UseConfiguredShadowQualityDefault,
    [double]$ExpectedCascadeMaxDistance = 0.0,
    [int]$WarmupFrames = 2,
    [int]$CaptureFrames = 8,
    [int]$AutoExitFrames = 14,
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

function Test-Column {
    param(
        [Parameter(Mandatory = $true)]$Rows,
        [Parameter(Mandatory = $true)][string]$Name
    )

    return $Rows.Count -gt 0 -and $null -ne $Rows[0].PSObject.Properties[$Name]
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

    if (-not (Test-Column -Rows $Rows -Name $Name)) {
        return [pscustomobject]@{
            present = $false
            min = $null
            max = $null
            first = $null
            last = $null
            delta = $null
            maxStepAbs = $null
            maxStepRel = $null
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
            maxStepAbs = $null
            maxStepRel = $null
        }
    }

    $maxStepAbs = 0.0
    $maxStepRel = 0.0
    for ($i = 1; $i -lt $values.Count; ++$i) {
        $stepAbs = [Math]::Abs([double]$values[$i] - [double]$values[$i - 1])
        $denom = [Math]::Max([Math]::Abs([double]$values[$i - 1]), 0.000001)
        $stepRel = $stepAbs / $denom
        if ($stepAbs -gt $maxStepAbs) {
            $maxStepAbs = $stepAbs
        }
        if ($stepRel -gt $maxStepRel) {
            $maxStepRel = $stepRel
        }
    }

    $minimum = ($values | Measure-Object -Minimum).Minimum
    $maximum = ($values | Measure-Object -Maximum).Maximum
    return [pscustomobject]@{
        present = $true
        min = $minimum
        max = $maximum
        first = $values[0]
        last = $values[$values.Count - 1]
        delta = [double]$maximum - [double]$minimum
        maxStepAbs = $maxStepAbs
        maxStepRel = $maxStepRel
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

function Add-MinCheck {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)]$Metrics,
        [Parameter(Mandatory = $true)][string]$Column,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$Minimum,
        [string]$MissingStatus = "fail"
    )

    $metric = $Metrics[$Column]
    if (-not $metric.present) {
        Add-Check -Checks $Checks -Area $Area -Name $Name -Status $MissingStatus `
            -Actual "missing:$Column" -Expected ">= $Minimum"
        return
    }

    $status = if ($null -ne $metric.max -and [double]$metric.max -ge $Minimum) { "pass" } else { "fail" }
    Add-Check -Checks $Checks -Area $Area -Name $Name -Status $status `
        -Actual $metric.max -Expected ">= $Minimum"
}

function Add-MaxCheck {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)]$Metrics,
        [Parameter(Mandatory = $true)][string]$Column,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$Maximum,
        [string]$MissingStatus = "fail"
    )

    $metric = $Metrics[$Column]
    if (-not $metric.present) {
        Add-Check -Checks $Checks -Area $Area -Name $Name -Status $MissingStatus `
            -Actual "missing:$Column" -Expected "<= $Maximum"
        return
    }

    $status = if ($null -ne $metric.max -and [double]$metric.max -le $Maximum) { "pass" } else { "fail" }
    Add-Check -Checks $Checks -Area $Area -Name $Name -Status $status `
        -Actual $metric.max -Expected "<= $Maximum"
}

function Add-RangeStableCheck {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)]$Metrics,
        [Parameter(Mandatory = $true)][string]$Column,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$MaximumDelta,
        [string]$MissingStatus = "fail"
    )

    $metric = $Metrics[$Column]
    if (-not $metric.present) {
        Add-Check -Checks $Checks -Area $Area -Name $Name -Status $MissingStatus `
            -Actual "missing:$Column" -Expected "delta <= $MaximumDelta"
        return
    }

    $status = if ($null -ne $metric.delta -and [double]$metric.delta -le $MaximumDelta) {
        "pass"
    } else {
        "fail"
    }
    Add-Check -Checks $Checks -Area $Area -Name $Name -Status $status `
        -Actual $metric.delta -Expected "<= $MaximumDelta" `
        -Detail "first=$($metric.first) last=$($metric.last) min=$($metric.min) max=$($metric.max)"
}

function Add-ApproxCheck {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)]$Metrics,
        [Parameter(Mandatory = $true)][string]$Column,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$Expected,
        [double]$Tolerance = 0.001,
        [string]$MissingStatus = "fail"
    )

    $metric = $Metrics[$Column]
    if (-not $metric.present) {
        Add-Check -Checks $Checks -Area $Area -Name $Name -Status $MissingStatus `
            -Actual "missing:$Column" -Expected "$Expected +/- $Tolerance"
        return
    }

    $delta = if ($null -ne $metric.max) { [Math]::Abs([double]$metric.max - $Expected) } else { [double]::PositiveInfinity }
    $status = if ($delta -le $Tolerance) { "pass" } else { "fail" }
    Add-Check -Checks $Checks -Area $Area -Name $Name -Status $status `
        -Actual $metric.max -Expected "$Expected +/- $Tolerance"
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

function Get-ExpectedCascadeCount {
    param([Parameter(Mandatory = $true)][string]$Quality)

    switch ($Quality) {
        "low" { return 1 }
        "medium" { return 3 }
        "high" { return 4 }
        "ultra" { return 4 }
        default { throw "Unexpected shadow quality '$Quality'" }
    }
}

function Get-ExpectedCascadeDistance {
    param([Parameter(Mandatory = $true)][string]$Quality)

    switch ($Quality) {
        "low" { return 45.0 }
        "medium" { return 55.0 }
        "high" { return 60.0 }
        "ultra" { return 75.0 }
        default { throw "Unexpected shadow quality '$Quality'" }
    }
}

function New-CsmStabilityReport {
    param(
        [Parameter(Mandatory = $true)][string]$LaneName,
        [Parameter(Mandatory = $true)][string]$CsvPath,
        [Parameter(Mandatory = $true)][string]$Quality,
        [Parameter(Mandatory = $true)][bool]$ExpectCameraMotion,
        [AllowNull()]$RunError
    )

    $checks = [System.Collections.Generic.List[object]]::new()
    if ($RunError) {
        Add-Check -Checks $checks -Area "run" -Name "Forward3D executable" `
            -Status "fail" -Actual $RunError -Expected "exit code 0"
        return [pscustomobject]@{
            lane = $LaneName
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
        Add-Check -Checks $checks -Area "run" -Name "captured rows" `
            -Status "fail" -Actual 0 -Expected "> 0"
        return [pscustomobject]@{
            lane = $LaneName
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
        "framegraph_validation_missing_resource_refs",
        "framegraph_validation_read_before_first_write",
        "framegraph_validation_duplicate_pass_ids",
        "framegraph_validation_duplicate_resource_ids",
        "framegraph_validation_active_writes_planned_resources",
        "framegraph_validation_write_only_roadmap_resources",
        "framegraph_validation_unused_physical_resources",
        "benchmark_camera_motion_time_seconds",
        "main_visible",
        "shadow_visible",
        "shadow_cascade_configured_count",
        "shadow_cascade_active_count",
        "shadow_cascade_stable_snapping",
        "shadow_quality",
        "shadow_pcf_kernel_radius",
        "shadow_pcss_strength",
        "directional_shadow_pcss_enabled",
        "directional_shadow_pcss_blocker_samples",
        "directional_shadow_pcss_filter_samples",
        "directional_shadow_pcss_raw_depth_sampler_ready",
        "directional_shadow_pcss_fallback_reason",
        "directional_shadow_pcss_search_radius_texels",
        "directional_shadow_pcss_max_penumbra_texels",
        "directional_shadow_pcss_light_angular_radius_radians",
        "directional_shadow_filter_mode",
        "directional_shadow_filter_samples",
        "directional_shadow_filter_kernel_width",
        "directional_shadow_filter_max_depth_samples",
        "directional_shadow_filter_hardware_compare_enabled",
        "directional_shadow_filter_receiver_bias_extent_texels",
        "directional_shadow_filter_fallback_reason",
        "shadow_receiver_plane_bias_enabled",
        "shadow_receiver_plane_bias_scale",
        "shadow_normal_offset_bias_enabled",
        "shadow_normal_offset_bias_texels",
        "shadow_slope_offset_bias_enabled",
        "shadow_slope_offset_bias_texels",
        "shadow_caster_depth_bias_enabled",
        "shadow_caster_depth_bias_constant",
        "shadow_caster_depth_bias_clamp",
        "shadow_caster_depth_bias_slope",
        "shadow_cascade_split_lambda",
        "shadow_cascade_blend_ratio",
        "shadow_cascade_fade_ratio",
        "shadow_cascade_receiver_guard",
        "shadow_cascade_max_distance",
        "shadow_cascade_near_depth",
        "shadow_cascade_far_depth",
        "shadow_cascade_split0",
        "shadow_cascade_split1",
        "shadow_cascade_split2",
        "shadow_cascade_split3",
        "shadow_cascade_texel0",
        "shadow_cascade_texel1",
        "shadow_cascade_texel2",
        "shadow_cascade_texel3",
        "shadow_cascade_light_depth_span0",
        "shadow_cascade_light_depth_span1",
        "shadow_cascade_light_depth_span2",
        "shadow_cascade_light_depth_span3",
        "shadow_cascade_atlas_allocated",
        "shadow_cascade_atlas_tile_size",
        "shadow_cascade_atlas_width",
        "shadow_cascade_atlas_height",
        "shadow_cascade_atlas_tile_columns",
        "shadow_cascade_atlas_tile_rows",
        "shadow_cascade_atlas_capacity",
        "shadow_cascade_atlas_passes",
        "shadow_cascade_atlas_draws",
        "shadow_cascade_atlas_mesh_binds",
        "shadow_cascade_buffer_updates"
    )

    $metrics = @{}
    foreach ($column in $columns) {
        $metrics[$column] = Measure-Number -Rows $rows -Name $column
    }

    $expectedCascades = Get-ExpectedCascadeCount -Quality $Quality
    $expectedDistance = if ($ExpectedCascadeMaxDistance -gt 0.0) {
        $ExpectedCascadeMaxDistance
    } else {
        Get-ExpectedCascadeDistance -Quality $Quality
    }
    $expectedFilterMode = if ($DirectionalShadowFilter -eq "production") { 1 } else { 0 }
    $expectedFilterFallback = if ($DirectionalShadowFilter -eq "production") { 0 } else { 1 }
    $expectedFilterKernelWidth = if ($Quality -in @("high", "ultra")) { 5 } else { 3 }
    $expectedQuality = @{ low = 1; medium = 2; high = 3; ultra = 4 }[$Quality]
    $expectedPcssStrength = if ($DirectionalPcssStrength -ge 0.0) {
        $DirectionalPcssStrength
    } elseif ($Quality -eq "ultra") {
        1.0
    } else {
        0.0
    }
    $expectedPcssEnabled = if ($Quality -eq "ultra" -and $expectedPcssStrength -gt 0.0001) {
        1.0
    } else {
        0.0
    }

    Add-Check -Checks $checks -Area "run" -Name "captured frame rows" `
        -Status ($(if ($rows.Count -gt 0) { "pass" } else { "fail" })) `
        -Actual $rows.Count -Expected "> 0"

    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_missing_resource_refs" `
        -Area "framegraph" -Name "no missing framegraph resource refs" -Maximum 0 -MissingStatus "warn"
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_read_before_first_write" `
        -Area "framegraph" -Name "no framegraph read before first write" -Maximum 0 -MissingStatus "warn"
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_duplicate_pass_ids" `
        -Area "framegraph" -Name "no duplicate framegraph pass ids" -Maximum 0 -MissingStatus "warn"
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_duplicate_resource_ids" `
        -Area "framegraph" -Name "no duplicate framegraph resource ids" -Maximum 0 -MissingStatus "warn"
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_active_writes_planned_resources" `
        -Area "framegraph" -Name "active passes do not write planned resources" -Maximum 0 -MissingStatus "warn"
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_write_only_roadmap_resources" `
        -Area "framegraph" -Name "roadmap resources are not write-only active leftovers" -Maximum 0 -MissingStatus "warn"

    $unusedPhysical = $metrics["framegraph_validation_unused_physical_resources"]
    if ($unusedPhysical.present) {
        Add-Check -Checks $checks -Area "framegraph" -Name "unused physical resources are only a hygiene warning" `
            -Status ($(if ($null -ne $unusedPhysical.max -and [double]$unusedPhysical.max -le 0) { "pass" } else { "warn" })) `
            -Actual $unusedPhysical.max -Expected "<= 0 for a fully tidy graph"
    } else {
        Add-Check -Checks $checks -Area "framegraph" -Name "unused physical resource signal exists" `
            -Status "warn" -Actual "missing:framegraph_validation_unused_physical_resources" `
            -Expected "CSV column present"
    }

    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_visible" `
        -Area "csm" -Name "shadow caster queue is populated" -Minimum 1
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_configured_count" `
        -Area "csm" -Name "configured cascade count matches quality" `
        -Expected $expectedCascades -Tolerance 0.001
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_active_count" `
        -Area "csm" -Name "active cascade count matches quality" `
        -Expected $expectedCascades -Tolerance 0.001
    Add-RangeStableCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_active_count" `
        -Area "csm" -Name "active cascade count stays stable" -MaximumDelta 0
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_stable_snapping" `
        -Area "csm" -Name "stable cascade snapping is enabled" -Minimum 1
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "shadow_quality" `
        -Area "csm" -Name "resolved shadow quality matches the requested tier" `
        -Expected $expectedQuality -Tolerance 0.001
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "directional_shadow_filter_mode" `
        -Area "filter" -Name "directional shadow filter mode matches lane" `
        -Expected $expectedFilterMode -Tolerance 0.001
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "directional_shadow_filter_samples" `
        -Area "filter" -Name "directional filter resolves the optimized hardware tap budget" `
        -Expected 9 -Tolerance 0.001
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "directional_shadow_filter_kernel_width" `
        -Area "filter" -Name "directional filter kernel width matches quality" `
        -Expected $expectedFilterKernelWidth -Tolerance 0.001
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "directional_shadow_filter_hardware_compare_enabled" `
        -Area "filter" -Name "directional filter uses hardware comparison sampling" -Minimum 1
    $filterSamples = if ($expectedPcssEnabled -gt 0.5) { 28.0 } else {
        $metrics["directional_shadow_filter_samples"].max
    }
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "directional_shadow_filter_max_depth_samples" `
        -Area "filter" -Name "directional filter depth-sample budget matches the active algorithm" `
        -Expected $filterSamples -Tolerance 0.001
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "directional_shadow_filter_fallback_reason" `
        -Area "filter" -Name "directional filter fallback state matches lane" `
        -Expected $expectedFilterFallback -Tolerance 0.001
    if ($DirectionalShadowFilter -eq "production") {
        Add-MinCheck -Checks $checks -Metrics $metrics -Column "directional_shadow_filter_receiver_bias_extent_texels" `
            -Area "filter" -Name "production filter resolves a bounded receiver-plane extent" -Minimum 0.001
    }
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "shadow_pcss_strength" `
        -Area "pcss" -Name "directional PCSS strength matches the requested control" `
        -Expected $expectedPcssStrength -Tolerance 0.001
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "directional_shadow_pcss_enabled" `
        -Area "pcss" -Name "blocker-search PCSS activation matches the quality tier" `
        -Expected $expectedPcssEnabled -Tolerance 0.001
    if ($expectedPcssEnabled -gt 0.5) {
        Add-ApproxCheck -Checks $checks -Metrics $metrics `
            -Column "directional_shadow_pcss_blocker_samples" -Area "pcss" `
            -Name "Ultra blocker-search sample budget" -Expected 12 -Tolerance 0.001
        Add-ApproxCheck -Checks $checks -Metrics $metrics `
            -Column "directional_shadow_pcss_filter_samples" -Area "pcss" `
            -Name "Ultra variable-penumbra filter sample budget" -Expected 16 -Tolerance 0.001
        Add-MinCheck -Checks $checks -Metrics $metrics `
            -Column "directional_shadow_pcss_raw_depth_sampler_ready" -Area "pcss" `
            -Name "raw directional depth sampler is bound" -Minimum 1
        Add-ApproxCheck -Checks $checks -Metrics $metrics `
            -Column "directional_shadow_pcss_fallback_reason" -Area "pcss" `
            -Name "Ultra PCSS has no runtime fallback" -Expected 0 -Tolerance 0.001
        Add-ApproxCheck -Checks $checks -Metrics $metrics `
            -Column "directional_shadow_pcss_search_radius_texels" -Area "pcss" `
            -Name "Ultra blocker search radius" -Expected 8 -Tolerance 0.001
        Add-ApproxCheck -Checks $checks -Metrics $metrics `
            -Column "directional_shadow_pcss_max_penumbra_texels" -Area "pcss" `
            -Name "Ultra maximum penumbra radius" -Expected 8 -Tolerance 0.001
        Add-MinCheck -Checks $checks -Metrics $metrics `
            -Column "directional_shadow_pcss_light_angular_radius_radians" -Area "pcss" `
            -Name "scene directional light exposes a physical angular radius" -Minimum 0.0001
        foreach ($cascadeIndex in 0..($expectedCascades - 1)) {
            Add-MinCheck -Checks $checks -Metrics $metrics `
                -Column "shadow_cascade_light_depth_span$cascadeIndex" -Area "pcss" `
                -Name "cascade $cascadeIndex has a world-space light depth span" -Minimum 0.001
        }
    } else {
        Add-MinCheck -Checks $checks -Metrics $metrics `
            -Column "directional_shadow_pcss_fallback_reason" -Area "pcss" `
            -Name "disabled PCSS reports an explicit fallback reason" -Minimum 1
    }
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_receiver_plane_bias_enabled" `
        -Area "csm" -Name "receiver-plane bias path is enabled" -Minimum 1
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "shadow_receiver_plane_bias_scale" `
        -Area "csm" -Name "receiver-plane bias scale resolves to the production default" `
        -Expected 1.0 -Tolerance 0.001
    $expectedNormalOffsetEnabled = if ($DirectionalNormalOffsetBiasTexels -gt 0.0001) { 1.0 } else { 0.0 }
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "shadow_normal_offset_bias_enabled" `
        -Area "csm" -Name "normal-offset receiver path matches the requested control" `
        -Expected $expectedNormalOffsetEnabled -Tolerance 0.001
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "shadow_normal_offset_bias_texels" `
        -Area "csm" -Name "normal-offset receiver scale resolves in cascade texels" `
        -Expected $DirectionalNormalOffsetBiasTexels -Tolerance 0.001
    $expectedSlopeOffsetEnabled = if ($DirectionalSlopeOffsetBiasTexels -gt 0.0001) { 1.0 } else { 0.0 }
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "shadow_slope_offset_bias_enabled" `
        -Area "csm" -Name "slope-offset receiver path matches the requested control" `
        -Expected $expectedSlopeOffsetEnabled -Tolerance 0.001
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "shadow_slope_offset_bias_texels" `
        -Area "csm" -Name "slope-offset receiver scale resolves in cascade texels" `
        -Expected $DirectionalSlopeOffsetBiasTexels -Tolerance 0.001
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_caster_depth_bias_enabled" `
        -Area "csm" -Name "caster depth-bias path is enabled" -Minimum 1
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_caster_depth_bias_constant" `
        -Area "csm" -Name "caster depth-bias constant is positive" -Minimum 0.001
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_caster_depth_bias_slope" `
        -Area "csm" -Name "caster depth-bias slope is positive" -Minimum 0.001
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_receiver_guard" `
        -Area "csm" -Name "receiver UV guard protects split-line coverage" -Minimum 0.25
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_max_distance" `
        -Area "csm" -Name "quality profile CSM range matches expected" `
        -Expected $expectedDistance -Tolerance 0.01
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_allocated" `
        -Area "atlas" -Name "directional cascade atlas allocated" -Minimum 1
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_tile_size" `
        -Area "atlas" -Name "cascade atlas tile size is positive" -Minimum 1
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_capacity" `
        -Area "atlas" -Name "cascade atlas capacity covers active cascades" -Minimum $expectedCascades
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_passes" `
        -Area "atlas" -Name "cascade atlas records one pass per active cascade" -Minimum $expectedCascades
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_draws" `
        -Area "atlas" -Name "cascade atlas records caster draws" -Minimum 1

    $nearDepth = $metrics["shadow_cascade_near_depth"].max
    $farDepth = $metrics["shadow_cascade_far_depth"].max
    Add-Check -Checks $checks -Area "csm" -Name "cascade depth range is ordered" `
        -Status ($(if ($null -ne $nearDepth -and $null -ne $farDepth -and [double]$nearDepth -lt [double]$farDepth) { "pass" } else { "fail" })) `
        -Actual "near=$nearDepth far=$farDepth" -Expected "near < far"

    $lastSplitColumn = "shadow_cascade_split$($expectedCascades - 1)"
    Add-ApproxCheck -Checks $checks -Metrics $metrics -Column $lastSplitColumn `
        -Area "csm" -Name "last active cascade split reaches max distance" `
        -Expected $expectedDistance -Tolerance 0.01

    for ($i = 0; $i -lt $expectedCascades; ++$i) {
        $splitColumn = "shadow_cascade_split$i"
        $texelColumn = "shadow_cascade_texel$i"
        Add-MinCheck -Checks $checks -Metrics $metrics -Column $splitColumn `
            -Area "splits" -Name "split $i is positive and finite" -Minimum 0.000001
        Add-MinCheck -Checks $checks -Metrics $metrics -Column $texelColumn `
            -Area "texels" -Name "texel world size $i is positive and finite" -Minimum 0.000001
        if ($i -gt 0) {
            $prevSplit = $metrics["shadow_cascade_split$($i - 1)"].min
            $thisSplit = $metrics[$splitColumn].min
            Add-Check -Checks $checks -Area "splits" -Name "split $i is greater than split $($i - 1)" `
                -Status ($(if ($null -ne $prevSplit -and $null -ne $thisSplit -and [double]$thisSplit -gt [double]$prevSplit) { "pass" } else { "fail" })) `
                -Actual "prev=$prevSplit current=$thisSplit" -Expected "current > previous"

            $prevTexel = $metrics["shadow_cascade_texel$($i - 1)"].min
            $thisTexel = $metrics[$texelColumn].min
            Add-Check -Checks $checks -Area "texels" -Name "texel world size $i is not smaller than previous cascade" `
                -Status ($(if ($null -ne $prevTexel -and $null -ne $thisTexel -and [double]$thisTexel -ge [double]$prevTexel) { "pass" } else { "fail" })) `
                -Actual "prev=$prevTexel current=$thisTexel" -Expected "current >= previous"
        }
    }

    if ($ExpectCameraMotion) {
        $motion = $metrics["benchmark_camera_motion_time_seconds"]
        Add-Check -Checks $checks -Area "motion" -Name "benchmark camera motion time advances" `
            -Status ($(if ($motion.present -and $null -ne $motion.delta -and [double]$motion.delta -gt 0.001) { "pass" } else { "fail" })) `
            -Actual "first=$($motion.first) last=$($motion.last) delta=$($motion.delta)" `
            -Expected "delta > 0.001"

        for ($i = 0; $i -lt $expectedCascades; ++$i) {
            $splitMetric = $metrics["shadow_cascade_split$i"]
            $texelMetric = $metrics["shadow_cascade_texel$i"]
            Add-Check -Checks $checks -Area "motion" -Name "moving split $i has no sudden jump" `
                -Status ($(if ($splitMetric.present -and $null -ne $splitMetric.maxStepRel -and [double]$splitMetric.maxStepRel -le 0.01) { "pass" } else { "fail" })) `
                -Actual $splitMetric.maxStepRel -Expected "<= 0.01 relative per captured frame"
            Add-Check -Checks $checks -Area "motion" -Name "stable projection keeps moving texel $i scale invariant" `
                -Status ($(if ($texelMetric.present -and $null -ne $texelMetric.maxStepRel -and [double]$texelMetric.maxStepRel -le 0.00001) { "pass" } else { "fail" })) `
                -Actual $texelMetric.maxStepRel -Expected "<= 0.00001 relative per captured frame"
        }
    } else {
        Add-RangeStableCheck -Checks $checks -Metrics $metrics -Column "benchmark_camera_motion_time_seconds" `
            -Area "static" -Name "static lane has no benchmark camera motion" -MaximumDelta 0 -MissingStatus "warn"
        Add-RangeStableCheck -Checks $checks -Metrics $metrics -Column "shadow_visible" `
            -Area "static" -Name "shadow visible count is stable" -MaximumDelta 0
        Add-RangeStableCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_draws" `
            -Area "static" -Name "cascade draw count is stable" -MaximumDelta 0
        for ($i = 0; $i -lt $expectedCascades; ++$i) {
            Add-RangeStableCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_split$i" `
                -Area "static" -Name "static split $i is frame-stable" -MaximumDelta 0.000001
            Add-RangeStableCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_texel$i" `
                -Area "static" -Name "static texel $i is frame-stable" -MaximumDelta 0.000001
        }
    }

    $splitSummaries = [System.Collections.Generic.List[object]]::new()
    $texelSummaries = [System.Collections.Generic.List[object]]::new()
    for ($i = 0; $i -lt 4; ++$i) {
        $split = $metrics["shadow_cascade_split$i"]
        $texel = $metrics["shadow_cascade_texel$i"]
        $splitSummaries.Add([pscustomobject]@{
            index = $i
            present = $split.present
            min = $split.min
            max = $split.max
            delta = $split.delta
            maxStepAbs = $split.maxStepAbs
            maxStepRel = $split.maxStepRel
        }) | Out-Null
        $texelSummaries.Add([pscustomobject]@{
            index = $i
            present = $texel.present
            min = $texel.min
            max = $texel.max
            normalOffsetMaxWorldMin = if ($null -ne $texel.min) {
                [double]$texel.min * $DirectionalNormalOffsetBiasTexels
            } else {
                $null
            }
            normalOffsetMaxWorldMax = if ($null -ne $texel.max) {
                [double]$texel.max * $DirectionalNormalOffsetBiasTexels
            } else {
                $null
            }
            slopeOffsetMaxWorldMax = if ($null -ne $texel.max) {
                [double]$texel.max * $DirectionalSlopeOffsetBiasTexels * 2.0
            } else {
                $null
            }
            delta = $texel.delta
            maxStepAbs = $texel.maxStepAbs
            maxStepRel = $texel.maxStepRel
        }) | Out-Null
    }

    $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
    $warnCount = @($checks | Where-Object { $_.status -eq "warn" }).Count
    $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
    $verdict = if ($failCount -gt 0) {
        "fail"
    } elseif ($warnCount -gt 0) {
        "warn"
    } else {
        "pass"
    }

    return [pscustomobject]@{
        lane = $LaneName
        csv = $CsvPath
        verdict = $verdict
        passCount = $passCount
        warnCount = $warnCount
        failCount = $failCount
        metrics = [pscustomobject]@{
            rows = $rows.Count
            expectedCascadeCount = $expectedCascades
            expectedMaxDistance = $expectedDistance
            cameraMotion = $metrics["benchmark_camera_motion_time_seconds"]
            activeCascadeCount = $metrics["shadow_cascade_active_count"]
            configuredCascadeCount = $metrics["shadow_cascade_configured_count"]
            receiverGuard = $metrics["shadow_cascade_receiver_guard"]
            receiverPlaneBiasScale = $metrics["shadow_receiver_plane_bias_scale"]
            normalOffsetBiasEnabled = $metrics["shadow_normal_offset_bias_enabled"]
            normalOffsetBiasTexels = $metrics["shadow_normal_offset_bias_texels"]
            slopeOffsetBiasEnabled = $metrics["shadow_slope_offset_bias_enabled"]
            slopeOffsetBiasTexels = $metrics["shadow_slope_offset_bias_texels"]
            shadowQuality = $metrics["shadow_quality"]
            pcssStrength = $metrics["shadow_pcss_strength"]
            pcssEnabled = $metrics["directional_shadow_pcss_enabled"]
            pcssBlockerSamples = $metrics["directional_shadow_pcss_blocker_samples"]
            pcssFilterSamples = $metrics["directional_shadow_pcss_filter_samples"]
            pcssRawDepthSamplerReady =
                $metrics["directional_shadow_pcss_raw_depth_sampler_ready"]
            pcssFallbackReason = $metrics["directional_shadow_pcss_fallback_reason"]
            filterMode = $metrics["directional_shadow_filter_mode"]
            filterSamples = $metrics["directional_shadow_filter_samples"]
            filterKernelWidth = $metrics["directional_shadow_filter_kernel_width"]
            filterMaxDepthSamples = $metrics["directional_shadow_filter_max_depth_samples"]
            filterHardwareCompareEnabled = $metrics["directional_shadow_filter_hardware_compare_enabled"]
            filterFallbackReason = $metrics["directional_shadow_filter_fallback_reason"]
            casterDepthBiasConstant = $metrics["shadow_caster_depth_bias_constant"]
            casterDepthBiasSlope = $metrics["shadow_caster_depth_bias_slope"]
            atlasDraws = $metrics["shadow_cascade_atlas_draws"]
            shadowVisible = $metrics["shadow_visible"]
            splits = @($splitSummaries)
            texels = @($texelSummaries)
            columns = $metrics
        }
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
    "SE_FORWARD3D_DEBUG_DEFAULT_SCENE",
    "SE_FORWARD3D_SHADOW_PROFILE",
    "SE_SHADOW_QUALITY",
    "SE_DIRECTIONAL_SHADOW_FILTER_MODE",
    "SE_DIRECTIONAL_SHADOW_FILTER_KERNEL_WIDTH",
    "SE_DIRECTIONAL_SHADOW_FILTER_RECEIVER_BIAS_EXTENT_TEXELS",
    "SE_SHADOW_PCSS_STRENGTH",
    "SE_DIRECTIONAL_PCSS_OFF",
    "SE_DIRECTIONAL_PCSS_BLOCKER_SAMPLES",
    "SE_DIRECTIONAL_PCSS_FILTER_SAMPLES",
    "SE_DIRECTIONAL_PCSS_SEARCH_RADIUS_TEXELS",
    "SE_DIRECTIONAL_PCSS_MAX_PENUMBRA_TEXELS",
    "SE_FORWARD3D_AA_MODE",
    "SE_RENDER_VIEW",
    "SE_GLOBAL_IBL_QUALITY",
    "SE_GLOBAL_IBL_SOURCE",
    "SE_GLOBAL_IBL_CACHE_POLICY",
    "SE_GLOBAL_IBL_CACHE",
    "SE_GLOBAL_IBL_CACHE_DIR",
    "SE_GLOBAL_IBL_ASSET",
    "SE_GLOBAL_IBL_SOURCE_ASSET",
    "SE_GLOBAL_IBL_SOURCE_PATH",
    "SE_IBL_QUALITY",
    "SE_IBL_SOURCE",
    "SE_IBL_CACHE_POLICY",
    "SE_IBL_CACHE",
    "SE_IBL_CACHE_DIR",
    "SE_IBL_ASSET",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_SHADOW_REGRESSION_CAMERA_CONTROLS",
    "SE_SHADOW_REGRESSION_RECT_LIGHT_ONLY",
    "SE_DIRECTIONAL_RECEIVER_PLANE_BIAS_SCALE",
    "SE_DIRECTIONAL_NORMAL_OFFSET_BIAS_TEXELS",
    "SE_DIRECTIONAL_SLOPE_OFFSET_BIAS_TEXELS",
    "SE_SHADOW_CASCADE_MAX_DISTANCE",
    "SE_CAMERA_FREEZE",
    "SE_FREEZE_CAMERA",
    "SE_SCENE_UPDATE_FREEZE",
    "SE_FREEZE_SCENE_UPDATE",
    "SE_BENCHMARK_CAMERA_MOTION",
    "SE_BENCHMARK_CAMERA_MOTION_SPEED",
    "SE_BENCHMARK_CAMERA_MOTION_YAW",
    "SE_BENCHMARK_CAMERA_MOTION_PITCH",
    "SE_BENCHMARK_CAMERA_MOTION_DISTANCE",
    "SE_BENCHMARK_OBJECT_MOTION",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_AUTO_EXIT_FRAMES",
    "SE_BENCHMARK_CSV"
)

$directionalShadowFilterMode = if ($DirectionalShadowFilter -eq "production") { "1" } else { "0" }

$laneSpecs = @(
    [pscustomobject]@{
        name = "static-shadow-regression"
        csvName = "csm_static_shadow_regression.csv"
        expectCameraMotion = $false
        environment = @{
            SE_WINDOW_WIDTH = "1280"
            SE_WINDOW_HEIGHT = "720"
            SE_WINDOW_BORDERLESS = "0"
            SE_VISUAL_QA_HIDE_IMGUI = "1"
            SE_HIDE_IMGUI = "1"
            SE_BENCHMARK_SCENE = "shadow-regression"
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "shadow-regression"
            SE_FORWARD3D_SHADOW_PROFILE = "production"
            SE_SHADOW_QUALITY = $(
                if ($UseConfiguredShadowQualityDefault) { $null } else { $ShadowQuality }
            )
            SE_SHADOW_PCSS_STRENGTH = $(
                if ($DirectionalPcssStrength -ge 0.0) {
                    [string]$DirectionalPcssStrength
                } else {
                    $null
                }
            )
            SE_DIRECTIONAL_SHADOW_FILTER_MODE = $directionalShadowFilterMode
            SE_DIRECTIONAL_NORMAL_OFFSET_BIAS_TEXELS = $(
                if ($UseConfiguredNormalOffsetDefault) {
                    $null
                } else {
                    [string]$DirectionalNormalOffsetBiasTexels
                }
            )
            SE_DIRECTIONAL_SLOPE_OFFSET_BIAS_TEXELS = $(
                if ($UseConfiguredSlopeOffsetDefault) {
                    $null
                } else {
                    [string]$DirectionalSlopeOffsetBiasTexels
                }
            )
            SE_SHADOW_CASCADE_MAX_DISTANCE = $(
                if ($CascadeMaxDistanceOverride -gt 0.0) {
                    [string]$CascadeMaxDistanceOverride
                } else {
                    $null
                }
            )
            SE_FORWARD3D_AA_MODE = "taa"
            SE_RENDER_VIEW = "lit"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_SHADOW_REGRESSION_CAMERA_CONTROLS = "0"
            SE_BENCHMARK_WARMUP_FRAMES = [string]$WarmupFrames
            SE_BENCHMARK_FRAMES = [string]$CaptureFrames
            SE_AUTO_EXIT_FRAMES = [string]$AutoExitFrames
        }
    },
    [pscustomobject]@{
        name = "moving-default-orbit"
        csvName = "csm_moving_default_orbit.csv"
        expectCameraMotion = $true
        environment = @{
            SE_WINDOW_WIDTH = "1280"
            SE_WINDOW_HEIGHT = "720"
            SE_WINDOW_BORDERLESS = "0"
            SE_VISUAL_QA_HIDE_IMGUI = "1"
            SE_HIDE_IMGUI = "1"
            SE_BENCHMARK_SCENE = $null
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_FORWARD3D_SHADOW_PROFILE = "production"
            SE_SHADOW_QUALITY = $(
                if ($UseConfiguredShadowQualityDefault) { $null } else { $ShadowQuality }
            )
            SE_SHADOW_PCSS_STRENGTH = $(
                if ($DirectionalPcssStrength -ge 0.0) {
                    [string]$DirectionalPcssStrength
                } else {
                    $null
                }
            )
            SE_DIRECTIONAL_SHADOW_FILTER_MODE = $directionalShadowFilterMode
            SE_DIRECTIONAL_NORMAL_OFFSET_BIAS_TEXELS = $(
                if ($UseConfiguredNormalOffsetDefault) {
                    $null
                } else {
                    [string]$DirectionalNormalOffsetBiasTexels
                }
            )
            SE_DIRECTIONAL_SLOPE_OFFSET_BIAS_TEXELS = $(
                if ($UseConfiguredSlopeOffsetDefault) {
                    $null
                } else {
                    [string]$DirectionalSlopeOffsetBiasTexels
                }
            )
            SE_SHADOW_CASCADE_MAX_DISTANCE = $(
                if ($CascadeMaxDistanceOverride -gt 0.0) {
                    [string]$CascadeMaxDistanceOverride
                } else {
                    $null
                }
            )
            SE_FORWARD3D_AA_MODE = "taa"
            SE_RENDER_VIEW = "lit"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_BENCHMARK_CAMERA_MOTION = "orbit"
            SE_BENCHMARK_CAMERA_MOTION_SPEED = "0.65"
            SE_BENCHMARK_CAMERA_MOTION_YAW = "0.18"
            SE_BENCHMARK_CAMERA_MOTION_PITCH = "0.035"
            SE_BENCHMARK_CAMERA_MOTION_DISTANCE = "5.0"
            SE_BENCHMARK_WARMUP_FRAMES = [string]$WarmupFrames
            SE_BENCHMARK_FRAMES = [string]$CaptureFrames
            SE_AUTO_EXIT_FRAMES = [string]$AutoExitFrames
        }
    }
)

$reports = [System.Collections.Generic.List[object]]::new()
foreach ($lane in $laneSpecs) {
    $laneOutput = Join-Path $resolvedOutput $lane.name
    New-Item -ItemType Directory -Force -Path $laneOutput | Out-Null
    $csvPath = Join-Path $laneOutput $lane.csvName
    Remove-Item -LiteralPath $csvPath -ErrorAction SilentlyContinue

    $environment = @{}
    foreach ($entry in $lane.environment.GetEnumerator()) {
        $environment[$entry.Key] = $entry.Value
    }
    $environment["SE_BENCHMARK_CSV"] = $csvPath

    foreach ($key in @(
        "SE_GLOBAL_IBL_QUALITY",
        "SE_GLOBAL_IBL_SOURCE",
        "SE_GLOBAL_IBL_CACHE_POLICY",
        "SE_GLOBAL_IBL_CACHE",
        "SE_GLOBAL_IBL_CACHE_DIR",
        "SE_GLOBAL_IBL_ASSET",
        "SE_GLOBAL_IBL_SOURCE_ASSET",
        "SE_GLOBAL_IBL_SOURCE_PATH",
        "SE_IBL_QUALITY",
        "SE_IBL_SOURCE",
        "SE_IBL_CACHE_POLICY",
        "SE_IBL_CACHE",
        "SE_IBL_CACHE_DIR",
        "SE_IBL_ASSET",
        "SE_SHADOW_REGRESSION_RECT_LIGHT_ONLY",
        "SE_CAMERA_FREEZE",
        "SE_FREEZE_CAMERA",
        "SE_SCENE_UPDATE_FREEZE",
        "SE_FREEZE_SCENE_UPDATE",
        "SE_BENCHMARK_OBJECT_MOTION"
    )) {
        if (-not $environment.ContainsKey($key)) {
            $environment[$key] = $null
        }
    }

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

    $reports.Add((New-CsmStabilityReport `
        -LaneName $lane.name `
        -CsvPath $csvPath `
        -Quality $ShadowQuality `
        -ExpectCameraMotion $lane.expectCameraMotion `
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
    shadowQuality = $ShadowQuality
    directionalShadowFilter = $DirectionalShadowFilter
    directionalNormalOffsetBiasTexels = $DirectionalNormalOffsetBiasTexels
    directionalSlopeOffsetBiasTexels = $DirectionalSlopeOffsetBiasTexels
    directionalPcssStrength = $DirectionalPcssStrength
    cascadeMaxDistanceOverride = $CascadeMaxDistanceOverride
    useConfiguredNormalOffsetDefault = [bool]$UseConfiguredNormalOffsetDefault
    useConfiguredSlopeOffsetDefault = [bool]$UseConfiguredSlopeOffsetDefault
    useConfiguredShadowQualityDefault = [bool]$UseConfiguredShadowQualityDefault
    expectedCascadeMaxDistance = $ExpectedCascadeMaxDistance
    verdict = $overall
    passCount = [int]$totalPass
    warnCount = [int]$totalWarn
    failCount = [int]$totalFail
    reports = @($reports)
}

$jsonPath = Join-Path $resolvedOutput "csm_stability_health.json"
$summary | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$summary

if ($Strict -and $overall -ne "pass") {
    throw "CSM stability health verdict is $overall. See $jsonPath"
}
