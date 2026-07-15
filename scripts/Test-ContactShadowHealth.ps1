param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "out\contact_shadow_health",
    [ValidateSet("low", "medium", "high", "ultra")]
    [string]$ShadowQuality = "high",
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

    $minimum = ($values | Measure-Object -Minimum).Minimum
    $maximum = ($values | Measure-Object -Maximum).Maximum
    return [pscustomobject]@{
        present = $true
        min = $minimum
        max = $maximum
        first = $values[0]
        last = $values[$values.Count - 1]
        delta = [double]$maximum - [double]$minimum
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

function New-ContactShadowReport {
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
        Add-Check -Checks $checks -Area "run" -Name "captured rows" `
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
        "runtime_import_animation_playback_changed_bone_palette_entry_count",
        "render_debug_forward_view",
        "render_debug_deferred_pbr_view",
        "render_debug_temporal_reconstruction_bypassed",
        "temporal_jitter_applied",
        "temporal_upscale_post_source_requested",
        "temporal_upscaler_evaluate_requested",
        "temporal_upscaler_dlss_evaluate_attempted",
        "shadow_contact_strength",
        "shadow_contact_length",
        "shadow_contact_thickness",
        "shadow_contact_steps",
        "shadow_contact_jitter_strength",
        "shadow_contact_edge_fade_pixels",
        "contact_shadow_debug_draws",
        "contact_shadow_debug_frame_binds",
        "contact_shadow_debug_gbuffer_binds"
    )
    $metrics = @{}
    foreach ($column in $columns) {
        $metrics[$column] = Measure-Number -Rows $rows -Name $column
    }

    Add-Check -Checks $checks -Area "run" -Name "captured frame rows" `
        -Status "pass" -Actual $rows.Count -Expected "> 0"
    Add-Check -Checks $checks -Area "framegraph" -Name "framegraph aggregate validation" `
        -Status ($(if ($metrics["framegraph_validation_issues"].present -and [double]$metrics["framegraph_validation_issues"].max -le 0) { "pass" } else { "warn" })) `
        -Actual $metrics["framegraph_validation_issues"].max -Expected "0 preferred"

    foreach ($column in @(
        "shadow_contact_strength",
        "shadow_contact_length",
        "shadow_contact_thickness",
        "shadow_contact_steps",
        "shadow_contact_jitter_strength",
        "shadow_contact_edge_fade_pixels"
    )) {
        $metric = $metrics[$column]
        Add-Check -Checks $checks -Area "controls" -Name "$column remains frame-stable" `
            -Status ($(if ($metric.present -and $null -ne $metric.delta -and [double]$metric.delta -le 0.000001) { "pass" } else { "fail" })) `
            -Actual $metric.delta -Expected "0"
    }

    switch ($Mode) {
        "lit" {
            Add-Check -Checks $checks -Area "normal path" -Name "normal lit view remains active" `
                -Status ($(if ([double]$metrics["render_debug_forward_view"].max -eq 0) { "pass" } else { "fail" })) `
                -Actual $metrics["render_debug_forward_view"].max -Expected "0"
            Add-Check -Checks $checks -Area "normal path" -Name "contact shadow controls are active" `
                -Status ($(if ([double]$metrics["shadow_contact_strength"].min -gt 0 -and [double]$metrics["shadow_contact_steps"].min -gt 0) { "pass" } else { "fail" })) `
                -Actual "strength=$($metrics['shadow_contact_strength'].min) steps=$($metrics['shadow_contact_steps'].min)" `
                -Expected "strength > 0 and steps > 0"
        }
        "debug" {
            Add-Check -Checks $checks -Area "debug view" -Name "contact shadow debug route is active" `
                -Status ($(if ([double]$metrics["render_debug_forward_view"].min -eq 29 -and [double]$metrics["render_debug_deferred_pbr_view"].min -eq 9) { "pass" } else { "fail" })) `
                -Actual "forward=$($metrics['render_debug_forward_view'].min) deferred=$($metrics['render_debug_deferred_pbr_view'].min)" `
                -Expected "forward 29, deferred 9"
            Add-Check -Checks $checks -Area "debug view" -Name "contact debug bypasses temporal reconstruction" `
                -Status ($(if ([double]$metrics["render_debug_temporal_reconstruction_bypassed"].min -ge 1) { "pass" } else { "fail" })) `
                -Actual $metrics["render_debug_temporal_reconstruction_bypassed"].min -Expected "1"
            Add-Check -Checks $checks -Area "debug view" -Name "contact debug does not apply temporal jitter" `
                -Status ($(if ([double]$metrics["temporal_jitter_applied"].max -le 0) { "pass" } else { "fail" })) `
                -Actual $metrics["temporal_jitter_applied"].max -Expected "0"
            Add-Check -Checks $checks -Area "debug view" -Name "contact debug does not request temporal upscale" `
                -Status ($(if ([double]$metrics["temporal_upscale_post_source_requested"].max -le 0 -and [double]$metrics["temporal_upscaler_evaluate_requested"].max -le 0 -and [double]$metrics["temporal_upscaler_dlss_evaluate_attempted"].max -le 0) { "pass" } else { "fail" })) `
                -Actual "post=$($metrics['temporal_upscale_post_source_requested'].max) requested=$($metrics['temporal_upscaler_evaluate_requested'].max) evaluate=$($metrics['temporal_upscaler_dlss_evaluate_attempted'].max)" `
                -Expected "0 / 0 / 0"
            Add-Check -Checks $checks -Area "debug view" -Name "contact debug pass binds expected inputs" `
                -Status ($(if ([double]$metrics["contact_shadow_debug_draws"].min -ge 1 -and [double]$metrics["contact_shadow_debug_frame_binds"].min -ge 1 -and [double]$metrics["contact_shadow_debug_gbuffer_binds"].min -ge 1) { "pass" } else { "fail" })) `
                -Actual "draws=$($metrics['contact_shadow_debug_draws'].min) frame=$($metrics['contact_shadow_debug_frame_binds'].min) gbuffer=$($metrics['contact_shadow_debug_gbuffer_binds'].min)" `
                -Expected ">= 1 / >= 1 / >= 1"
        }
        "camera" {
            Add-Check -Checks $checks -Area "motion" -Name "benchmark camera motion advances" `
                -Status ($(if ([double]$metrics["benchmark_camera_motion_time_seconds"].delta -gt 0.001) { "pass" } else { "fail" })) `
                -Actual $metrics["benchmark_camera_motion_time_seconds"].delta -Expected "> 0.001"
            Add-Check -Checks $checks -Area "motion" -Name "moving contact debug stays outside temporal reconstruction" `
                -Status ($(if ([double]$metrics["render_debug_temporal_reconstruction_bypassed"].min -ge 1 -and [double]$metrics["temporal_jitter_applied"].max -le 0) { "pass" } else { "fail" })) `
                -Actual "bypass=$($metrics['render_debug_temporal_reconstruction_bypassed'].min) jitter=$($metrics['temporal_jitter_applied'].max)" `
                -Expected "1 / 0"
        }
        "skinned" {
            Add-Check -Checks $checks -Area "animation" -Name "skinned animation changes shadow input" `
                -Status ($(if ([double]$metrics["runtime_import_animation_playback_changed_bone_palette_entry_count"].max -gt 0) { "pass" } else { "fail" })) `
                -Actual $metrics["runtime_import_animation_playback_changed_bone_palette_entry_count"].max -Expected "> 0"
            Add-Check -Checks $checks -Area "animation" -Name "skinned contact debug stays outside temporal reconstruction" `
                -Status ($(if ([double]$metrics["render_debug_temporal_reconstruction_bypassed"].min -ge 1 -and [double]$metrics["temporal_jitter_applied"].max -le 0) { "pass" } else { "fail" })) `
                -Actual "bypass=$($metrics['render_debug_temporal_reconstruction_bypassed'].min) jitter=$($metrics['temporal_jitter_applied'].max)" `
                -Expected "1 / 0"
        }
        "off" {
            Add-Check -Checks $checks -Area "control" -Name "contact shadow strength is disabled" `
                -Status ($(if ([double]$metrics["shadow_contact_strength"].max -le 0.000001) { "pass" } else { "fail" })) `
                -Actual $metrics["shadow_contact_strength"].max -Expected "0"
            Add-Check -Checks $checks -Area "control" -Name "disabled contact debug path remains inspectable" `
                -Status ($(if ([double]$metrics["contact_shadow_debug_draws"].min -ge 1) { "pass" } else { "fail" })) `
                -Actual $metrics["contact_shadow_debug_draws"].min -Expected ">= 1"
        }
        default {
            throw "Unexpected contact shadow lane mode '$Mode'"
        }
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
        mode = $Mode
        csv = $CsvPath
        verdict = $verdict
        passCount = $passCount
        warnCount = $warnCount
        failCount = $failCount
        metrics = [pscustomobject]@{
            rows = $rows.Count
            contactStrength = $metrics["shadow_contact_strength"]
            contactLength = $metrics["shadow_contact_length"]
            contactThickness = $metrics["shadow_contact_thickness"]
            contactSteps = $metrics["shadow_contact_steps"]
            contactJitterStrength = $metrics["shadow_contact_jitter_strength"]
            contactEdgeFadePixels = $metrics["shadow_contact_edge_fade_pixels"]
            cameraMotion = $metrics["benchmark_camera_motion_time_seconds"]
            skinnedBoneChanges = $metrics["runtime_import_animation_playback_changed_bone_palette_entry_count"]
            temporalBypass = $metrics["render_debug_temporal_reconstruction_bypassed"]
            temporalJitterApplied = $metrics["temporal_jitter_applied"]
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
    "SE_BENCHMARK_GRID_SIZE",
    "SE_FORWARD3D_DEBUG_DEFAULT_SCENE",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_SCENE_UPDATE_FREEZE",
    "SE_FORWARD3D_SHADOW_PROFILE",
    "SE_SHADOW_QUALITY",
    "SE_FORWARD3D_AA_MODE",
    "SE_RENDER_VIEW",
    "SE_CONTACT_SHADOW_STRENGTH",
    "SE_CONTACT_SHADOW_LENGTH",
    "SE_CONTACT_SHADOW_THICKNESS",
    "SE_CONTACT_SHADOW_STEPS",
    "SE_CONTACT_SHADOW_JITTER_STRENGTH",
    "SE_CONTACT_SHADOW_EDGE_FADE_PIXELS",
    "SE_SHADOW_REGRESSION_CAMERA_CONTROLS",
    "SE_BENCHMARK_CAMERA_MOTION",
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
    SE_FORWARD3D_SHADOW_PROFILE = "production"
    SE_SHADOW_QUALITY = $ShadowQuality
    SE_BENCHMARK_WARMUP_FRAMES = [string]$WarmupFrames
    SE_BENCHMARK_FRAMES = [string]$CaptureFrames
    SE_AUTO_EXIT_FRAMES = [string]$AutoExitFrames
}

$laneSpecs = @(
    [pscustomobject]@{
        name = "lit-taa-contact-active"
        mode = "lit"
        environment = @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_FORWARD3D_AA_MODE = "taa"
            SE_RENDER_VIEW = "lit"
        }
    },
    [pscustomobject]@{
        name = "contact-debug-dlss-bypass"
        mode = "debug"
        environment = @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_FORWARD3D_AA_MODE = "sr-performance"
            SE_RENDER_VIEW = "contact-shadow"
        }
    },
    [pscustomobject]@{
        name = "moving-camera-contact-debug"
        mode = "camera"
        environment = @{
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_BENCHMARK_CAMERA_MOTION = "orbit"
            SE_FORWARD3D_AA_MODE = "taa"
            SE_RENDER_VIEW = "contact-shadow"
        }
    },
    [pscustomobject]@{
        name = "skinned-fbx-contact-debug"
        mode = "skinned"
        environment = @{
            SE_BENCHMARK_SCENE = "shadow-regression"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_SHADOW_REGRESSION_CAMERA_CONTROLS = "0"
            SE_FORWARD3D_AA_MODE = "taa"
            SE_RENDER_VIEW = "contact-shadow"
        }
    },
    [pscustomobject]@{
        name = "contact-disabled-debug-control"
        mode = "off"
        environment = @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_FORWARD3D_AA_MODE = "taa"
            SE_RENDER_VIEW = "contact-shadow"
            SE_CONTACT_SHADOW_STRENGTH = "0"
        }
    }
)

$reports = [System.Collections.Generic.List[object]]::new()
foreach ($lane in $laneSpecs) {
    $laneOutput = Join-Path $resolvedOutput $lane.name
    New-Item -ItemType Directory -Force -Path $laneOutput | Out-Null
    $csvPath = Join-Path $laneOutput "contact_shadow_health.csv"
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

    $reports.Add((New-ContactShadowReport `
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
    shadowQuality = $ShadowQuality
    verdict = $overall
    passCount = [int]$totalPass
    warnCount = [int]$totalWarn
    failCount = [int]$totalFail
    reports = @($reports)
}

$jsonPath = Join-Path $resolvedOutput "contact_shadow_health.json"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$summary

if ($Strict -and $overall -ne "pass") {
    throw "Contact shadow health verdict is $overall. See $jsonPath"
}
