param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "out\local_shadow_cache_health",
    [ValidateSet("low", "medium", "high", "ultra")]
    [string]$ShadowQuality = "high",
    [int]$WarmupFrames = 3,
    [int]$CaptureFrames = 9,
    [int]$AutoExitFrames = 15,
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

function Get-Text {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return ""
    }

    return "$($property.Value)"
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

function New-LocalShadowCacheReport {
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
        "local_shadow_assigned_tiles",
        "local_shadow_dropped_tiles",
        "local_shadow_recorded_tile_passes",
        "local_shadow_cache_hit_tiles",
        "local_shadow_cache_miss_tiles",
        "local_shadow_cache_skipped_tiles",
        "local_shadow_cache_cold_tiles",
        "local_shadow_cache_tile_layout_changed_tiles",
        "local_shadow_cache_light_changed_tiles",
        "local_shadow_cache_caster_changed_tiles",
        "local_shadow_cache_dynamic_skinned_caster_tiles"
    )
    $metrics = @{}
    foreach ($column in $columns) {
        $metrics[$column] = Measure-Number -Rows $rows -Name $column
    }

    $requiredReasonColumn = "local_shadow_cache_reason_summary"
    $summaryPresent = $null -ne $rows[0].PSObject.Properties[$requiredReasonColumn]
    $steadyRows = @(
        $rows | Where-Object {
            (Get-Number -Row $_ -Name "local_shadow_assigned_tiles") -gt 0 -and
            (Get-Number -Row $_ -Name "local_shadow_cache_cold_tiles") -eq 0
        }
    )
    $balancedRows = @(
        $steadyRows | Where-Object {
            $assigned = Get-Number -Row $_ -Name "local_shadow_assigned_tiles"
            $hits = Get-Number -Row $_ -Name "local_shadow_cache_hit_tiles"
            $misses = Get-Number -Row $_ -Name "local_shadow_cache_miss_tiles"
            [Math]::Abs(($hits + $misses) - $assigned) -gt 0.001
        }
    )
    $lastSummary = Get-Text -Row $rows[$rows.Count - 1] -Name $requiredReasonColumn

    Add-Check -Checks $checks -Area "run" -Name "captured frame rows" `
        -Status "pass" -Actual $rows.Count -Expected "> 0"
    Add-Check -Checks $checks -Area "framegraph" -Name "framegraph aggregate validation" `
        -Status ($(if ($metrics["framegraph_validation_issues"].present -and [double]$metrics["framegraph_validation_issues"].max -le 0) { "pass" } else { "warn" })) `
        -Actual $metrics["framegraph_validation_issues"].max -Expected "0 preferred"
    Add-Check -Checks $checks -Area "tiles" -Name "assigned local shadow tiles exist" `
        -Status ($(if ($metrics["local_shadow_assigned_tiles"].present -and [double]$metrics["local_shadow_assigned_tiles"].max -gt 0) { "pass" } else { "fail" })) `
        -Actual $metrics["local_shadow_assigned_tiles"].max -Expected "> 0"
    Add-Check -Checks $checks -Area "tiles" -Name "local shadow atlas does not drop tiles" `
        -Status ($(if ($metrics["local_shadow_dropped_tiles"].present -and [double]$metrics["local_shadow_dropped_tiles"].max -le 0) { "pass" } else { "fail" })) `
        -Actual $metrics["local_shadow_dropped_tiles"].max -Expected "0"
    Add-Check -Checks $checks -Area "cache" -Name "steady rows expose per-tile cache accounting" `
        -Status ($(if ($steadyRows.Count -gt 0 -and $balancedRows.Count -eq 0) { "pass" } else { "fail" })) `
        -Actual "steady=$($steadyRows.Count) unbalanced=$($balancedRows.Count)" `
        -Expected "hits + misses = assigned"
    Add-Check -Checks $checks -Area "cache" -Name "Debug tile reason summary is present" `
        -Status ($(if ($summaryPresent -and $lastSummary -match "=") { "pass" } else { "fail" })) `
        -Actual $lastSummary -Expected "t#:l#:f#=reason"

    $fullCacheRows = @(
        $steadyRows | Where-Object {
            $assigned = Get-Number -Row $_ -Name "local_shadow_assigned_tiles"
            $hits = Get-Number -Row $_ -Name "local_shadow_cache_hit_tiles"
            $skipped = Get-Number -Row $_ -Name "local_shadow_cache_skipped_tiles"
            $hits -eq $assigned -and $skipped -eq $assigned
        }
    )
    $partialLightRows = @(
        $steadyRows | Where-Object {
            $hits = Get-Number -Row $_ -Name "local_shadow_cache_hit_tiles"
            $misses = Get-Number -Row $_ -Name "local_shadow_cache_miss_tiles"
            $light = Get-Number -Row $_ -Name "local_shadow_cache_light_changed_tiles"
            $caster = Get-Number -Row $_ -Name "local_shadow_cache_caster_changed_tiles"
            $skinned = Get-Number -Row $_ -Name "local_shadow_cache_dynamic_skinned_caster_tiles"
            $layout = Get-Number -Row $_ -Name "local_shadow_cache_tile_layout_changed_tiles"
            $hits -gt 0 -and $misses -gt 0 -and $light -gt 0 -and $caster -eq 0 -and $skinned -eq 0 -and $layout -eq 0
        }
    )
    $partialCasterRows = @(
        $steadyRows | Where-Object {
            $hits = Get-Number -Row $_ -Name "local_shadow_cache_hit_tiles"
            $misses = Get-Number -Row $_ -Name "local_shadow_cache_miss_tiles"
            $light = Get-Number -Row $_ -Name "local_shadow_cache_light_changed_tiles"
            $caster = Get-Number -Row $_ -Name "local_shadow_cache_caster_changed_tiles"
            $skinned = Get-Number -Row $_ -Name "local_shadow_cache_dynamic_skinned_caster_tiles"
            $layout = Get-Number -Row $_ -Name "local_shadow_cache_tile_layout_changed_tiles"
            $hits -gt 0 -and $misses -gt 0 -and $caster -gt 0 -and $light -eq 0 -and $skinned -eq 0 -and $layout -eq 0
        }
    )
    $partialSkinnedRows = @(
        $steadyRows | Where-Object {
            $assigned = Get-Number -Row $_ -Name "local_shadow_assigned_tiles"
            $hits = Get-Number -Row $_ -Name "local_shadow_cache_hit_tiles"
            $skinned = Get-Number -Row $_ -Name "local_shadow_cache_dynamic_skinned_caster_tiles"
            $hits -gt 0 -and $skinned -gt 0 -and [Math]::Abs(($hits + $skinned) - $assigned) -le 0.001
        }
    )

    switch ($Mode) {
        "static" {
            Add-Check -Checks $checks -Area "cache" -Name "static scene reuses every local shadow tile" `
                -Status ($(if ($fullCacheRows.Count -gt 0) { "pass" } else { "fail" })) `
                -Actual "fullCacheRows=$($fullCacheRows.Count)" -Expected "> 0"
            foreach ($column in @(
                "local_shadow_cache_tile_layout_changed_tiles",
                "local_shadow_cache_light_changed_tiles",
                "local_shadow_cache_caster_changed_tiles",
                "local_shadow_cache_dynamic_skinned_caster_tiles"
            )) {
                Add-Check -Checks $checks -Area "cache" -Name "static lane has no $column" `
                    -Status ($(if ([double]$metrics[$column].max -le 0) { "pass" } else { "fail" })) `
                    -Actual $metrics[$column].max -Expected "0"
            }
        }
        "light" {
            Add-Check -Checks $checks -Area "cache" -Name "moving light invalidates only a partial tile set" `
                -Status ($(if ($partialLightRows.Count -gt 0) { "pass" } else { "fail" })) `
                -Actual "partialLightRows=$($partialLightRows.Count)" -Expected "> 0"
        }
        "caster" {
            Add-Check -Checks $checks -Area "cache" -Name "moving caster invalidates only its affected tiles" `
                -Status ($(if ($partialCasterRows.Count -gt 0) { "pass" } else { "fail" })) `
                -Actual "partialCasterRows=$($partialCasterRows.Count)" -Expected "> 0"
        }
        "camera" {
            Add-Check -Checks $checks -Area "camera" -Name "benchmark camera motion advances" `
                -Status ($(if ($metrics["benchmark_camera_motion_time_seconds"].present -and [double]$metrics["benchmark_camera_motion_time_seconds"].delta -gt 0.001) { "pass" } else { "fail" })) `
                -Actual $metrics["benchmark_camera_motion_time_seconds"].delta -Expected "> 0.001"
            Add-Check -Checks $checks -Area "cache" -Name "camera motion preserves every local shadow cache tile" `
                -Status ($(if ($fullCacheRows.Count -gt 0) { "pass" } else { "fail" })) `
                -Actual "fullCacheRows=$($fullCacheRows.Count)" -Expected "> 0"
            foreach ($column in @(
                "local_shadow_cache_tile_layout_changed_tiles",
                "local_shadow_cache_light_changed_tiles",
                "local_shadow_cache_caster_changed_tiles",
                "local_shadow_cache_dynamic_skinned_caster_tiles"
            )) {
                Add-Check -Checks $checks -Area "cache" -Name "camera lane has no $column" `
                    -Status ($(if ([double]$metrics[$column].max -le 0) { "pass" } else { "fail" })) `
                    -Actual $metrics[$column].max -Expected "0"
            }
        }
        "skinned" {
            Add-Check -Checks $checks -Area "animation" -Name "skinned animation changes bone palette input" `
                -Status ($(if ($metrics["runtime_import_animation_playback_changed_bone_palette_entry_count"].present -and [double]$metrics["runtime_import_animation_playback_changed_bone_palette_entry_count"].max -gt 0) { "pass" } else { "fail" })) `
                -Actual $metrics["runtime_import_animation_playback_changed_bone_palette_entry_count"].max -Expected "> 0"
            Add-Check -Checks $checks -Area "cache" -Name "dynamic skinned casters bypass only their affected tiles" `
                -Status ($(if ($partialSkinnedRows.Count -gt 0) { "pass" } else { "fail" })) `
                -Actual "partialSkinnedRows=$($partialSkinnedRows.Count)" -Expected "> 0"
        }
        default {
            throw "Unexpected cache health lane mode '$Mode'"
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
            steadyRows = $steadyRows.Count
            assignedTiles = $metrics["local_shadow_assigned_tiles"]
            cacheHits = $metrics["local_shadow_cache_hit_tiles"]
            cacheMisses = $metrics["local_shadow_cache_miss_tiles"]
            cacheSkipped = $metrics["local_shadow_cache_skipped_tiles"]
            coldTiles = $metrics["local_shadow_cache_cold_tiles"]
            layoutChangedTiles = $metrics["local_shadow_cache_tile_layout_changed_tiles"]
            lightChangedTiles = $metrics["local_shadow_cache_light_changed_tiles"]
            casterChangedTiles = $metrics["local_shadow_cache_caster_changed_tiles"]
            dynamicSkinnedCasterTiles = $metrics["local_shadow_cache_dynamic_skinned_caster_tiles"]
            cameraMotion = $metrics["benchmark_camera_motion_time_seconds"]
            lastReasonSummary = $lastSummary
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
    "SE_BENCHMARK_PARTIAL_LOCAL_SHADOW_CACHE",
    "SE_FORWARD3D_DEBUG_DEFAULT_SCENE",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_SCENE_UPDATE_FREEZE",
    "SE_FORWARD3D_SHADOW_PROFILE",
    "SE_SHADOW_QUALITY",
    "SE_FORWARD3D_AA_MODE",
    "SE_RENDER_VIEW",
    "SE_SHADOW_REGRESSION_CAMERA_CONTROLS",
    "SE_BENCHMARK_CAMERA_MOTION",
    "SE_BENCHMARK_OBJECT_MOTION",
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
    SE_FORWARD3D_AA_MODE = "taa"
    SE_RENDER_VIEW = "lit"
    SE_BENCHMARK_WARMUP_FRAMES = [string]$WarmupFrames
    SE_BENCHMARK_FRAMES = [string]$CaptureFrames
    SE_AUTO_EXIT_FRAMES = [string]$AutoExitFrames
}

$laneSpecs = @(
    [pscustomobject]@{
        name = "static-grid-reuse"
        mode = "static"
        environment = @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
        }
    },
    [pscustomobject]@{
        name = "moving-light-partial-invalidation"
        mode = "light"
        environment = @{
            SE_BENCHMARK_SCENE = "grid"
            SE_BENCHMARK_GRID_SIZE = "4"
            SE_BENCHMARK_PARTIAL_LOCAL_SHADOW_CACHE = "1"
        }
    },
    [pscustomobject]@{
        name = "moving-rigid-caster-partial-invalidation"
        mode = "caster"
        environment = @{
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_BENCHMARK_OBJECT_MOTION = "orbit"
        }
    },
    [pscustomobject]@{
        name = "moving-camera-cache-preservation"
        mode = "camera"
        environment = @{
            SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "default"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_BENCHMARK_CAMERA_MOTION = "orbit"
        }
    },
    [pscustomobject]@{
        name = "skinned-caster-safe-bypass"
        mode = "skinned"
        environment = @{
            SE_BENCHMARK_SCENE = "shadow-regression"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_SHADOW_REGRESSION_CAMERA_CONTROLS = "0"
        }
    }
)

$reports = [System.Collections.Generic.List[object]]::new()
foreach ($lane in $laneSpecs) {
    $laneOutput = Join-Path $resolvedOutput $lane.name
    New-Item -ItemType Directory -Force -Path $laneOutput | Out-Null
    $csvPath = Join-Path $laneOutput "local_shadow_cache_health.csv"
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

    $reports.Add((New-LocalShadowCacheReport `
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

$jsonPath = Join-Path $resolvedOutput "local_shadow_cache_health.json"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$summary

if ($Strict -and $overall -ne "pass") {
    throw "Local shadow cache health verdict is $overall. See $jsonPath"
}
