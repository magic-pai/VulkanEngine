param(
    [string]$ExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [string]$OutputDirectory = "tmp\lighting_showcase_ceiling_lights_health",
    [int]$WarmupFrames = 8,
    [int]$CaptureFrames = 12,
    [int]$AutoExitFrames = 24,
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

function Add-Check {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[object]]$Checks,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Passed,
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)]$Expected
    )

    $Checks.Add([pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }) | Out-Null
}

function Measure-Column {
    param(
        [Parameter(Mandatory = $true)][object[]]$Rows,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $values = [System.Collections.Generic.List[double]]::new()
    foreach ($row in $Rows) {
        $property = $row.PSObject.Properties[$Name]
        if ($null -eq $property) {
            return [pscustomobject]@{ present = $false; min = $null; max = $null }
        }
        $value = 0.0
        if (-not [double]::TryParse(
                [string]$property.Value,
                [Globalization.NumberStyles]::Float,
                [Globalization.CultureInfo]::InvariantCulture,
                [ref]$value
            )) {
            return [pscustomobject]@{ present = $false; min = $null; max = $null }
        }
        $values.Add($value) | Out-Null
    }

    return [pscustomobject]@{
        present = $values.Count -gt 0
        min = if ($values.Count -gt 0) { ($values | Measure-Object -Minimum).Minimum } else { $null }
        max = if ($values.Count -gt 0) { ($values | Measure-Object -Maximum).Maximum } else { $null }
    }
}

if (-not $SkipBuild) {
    $buildCommand =
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cd /d "{0}\build" && MSBuild SelfEngineLightingShowcase.vcxproj /p:Configuration=Debug /v:minimal /nologo' -f $repoRoot
    cmd /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "SelfEngineLightingShowcase build failed with exit code $LASTEXITCODE"
    }
}

$executable = Resolve-FullPath -Path $ExecutablePath
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Executable not found: $executable"
}
$output = Resolve-FullPath -Path $OutputDirectory
New-Item -ItemType Directory -Force -Path $output | Out-Null
$csvPath = Join-Path $output "lighting_showcase_ceiling_lights.csv"
Remove-Item -LiteralPath $csvPath -ErrorAction SilentlyContinue

$environment = @{
    SE_WINDOW_WIDTH = "1280"
    SE_WINDOW_HEIGHT = "720"
    SE_WINDOW_BORDERLESS = "0"
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_HIDE_IMGUI = "1"
    SE_BENCHMARK_SCENE = "lighting-showcase"
    SE_FORCE_LIGHTING_SHOWCASE = "1"
    SE_FORWARD3D_SHADOW_PROFILE = "production"
    SE_SHADOW_QUALITY = "high"
    SE_FORWARD3D_AA_MODE = "taa"
    SE_BENCHMARK_WARMUP_FRAMES = [string]$WarmupFrames
    SE_BENCHMARK_FRAMES = [string]$CaptureFrames
    SE_AUTO_EXIT_FRAMES = [string]$AutoExitFrames
    SE_BENCHMARK_CSV = $csvPath
}
$previous = @{}
foreach ($name in $environment.Keys) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
    [Environment]::SetEnvironmentVariable($name, $environment[$name], "Process")
}
try {
    & $executable | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "SelfEngineLightingShowcase exited with code $LASTEXITCODE"
    }
} finally {
    foreach ($name in $environment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], "Process")
    }
}

$checks = [System.Collections.Generic.List[object]]::new()
$rows = @(Import-Csv -LiteralPath $csvPath)
Add-Check -Checks $checks -Name "combined Lighting Showcase captured rows" `
    -Passed ($rows.Count -gt 0) -Actual $rows.Count -Expected "> 0"

$columns = @(
    "framegraph_validation_issues",
    "local_shadow_atlas_allocated",
    "local_shadow_shadowable_light_count",
    "local_shadow_point_light_count",
    "local_shadow_spot_light_count",
    "local_shadow_rect_light_count",
    "local_shadow_point_face_tiles",
    "local_shadow_spot_tiles",
    "local_shadow_requested_tiles",
    "local_shadow_assigned_tiles",
    "local_shadow_dropped_tiles",
    "local_shadow_recorded_tile_passes",
    "local_shadow_recorded_draws",
    "local_shadow_cache_hit_tiles",
    "local_shadow_point_enabled",
    "local_shadow_spot_enabled",
    "local_shadow_rect_enabled",
    "local_shadow_rect_extra_sample_tiles",
    "local_shadow_rect_budget_limited_sample_tiles"
)
$metrics = @{}
foreach ($column in $columns) {
    $metrics[$column] = Measure-Column -Rows $rows -Name $column
}
$missing = @($columns | Where-Object { -not $metrics[$_].present })
Add-Check -Checks $checks -Name "combined local-light audit columns" `
    -Passed ($missing.Count -eq 0) -Actual $missing -Expected "all columns present"
Add-Check -Checks $checks -Name "combined framegraph is valid" `
    -Passed ($metrics["framegraph_validation_issues"].max -eq 0) `
    -Actual $metrics["framegraph_validation_issues"].max -Expected 0
Add-Check -Checks $checks -Name "local shadow atlas is allocated" `
    -Passed ($metrics["local_shadow_atlas_allocated"].max -eq 1) `
    -Actual $metrics["local_shadow_atlas_allocated"].max -Expected 1
Add-Check -Checks $checks -Name "ceiling point and spots enter the frame light set" `
    -Passed (
        $metrics["local_shadow_point_light_count"].max -eq 1 -and
        $metrics["local_shadow_spot_light_count"].max -eq 2 -and
        $metrics["local_shadow_rect_light_count"].max -eq 8 -and
        $metrics["local_shadow_shadowable_light_count"].max -eq 11
    ) `
    -Actual "point=$($metrics['local_shadow_point_light_count'].max),spot=$($metrics['local_shadow_spot_light_count'].max),rect=$($metrics['local_shadow_rect_light_count'].max),total=$($metrics['local_shadow_shadowable_light_count'].max)" `
    -Expected "1/2/8/11"
Add-Check -Checks $checks -Name "point and spot shadow footprints are scheduled" `
    -Passed (
        $metrics["local_shadow_point_face_tiles"].max -eq 6 -and
        $metrics["local_shadow_spot_tiles"].max -eq 2
    ) `
    -Actual "pointFaces=$($metrics['local_shadow_point_face_tiles'].max),spots=$($metrics['local_shadow_spot_tiles'].max)" `
    -Expected "6/2"
Add-Check -Checks $checks -Name "all local light kinds retain shadow sampling" `
    -Passed (
        $metrics["local_shadow_point_enabled"].min -eq 1 -and
        $metrics["local_shadow_spot_enabled"].min -eq 1 -and
        $metrics["local_shadow_rect_enabled"].min -eq 1
    ) `
    -Actual "point=$($metrics['local_shadow_point_enabled'].min),spot=$($metrics['local_shadow_spot_enabled'].min),rect=$($metrics['local_shadow_rect_enabled'].min)" `
    -Expected "1/1/1"
Add-Check -Checks $checks -Name "combined shadow atlas stays within its budget" `
    -Passed (
        $metrics["local_shadow_requested_tiles"].max -eq 32 -and
        $metrics["local_shadow_assigned_tiles"].min -eq 32 -and
        $metrics["local_shadow_dropped_tiles"].max -eq 0
    ) `
    -Actual "requested=$($metrics['local_shadow_requested_tiles'].max),assigned=$($metrics['local_shadow_assigned_tiles'].min),dropped=$($metrics['local_shadow_dropped_tiles'].max)" `
    -Expected "32/32/0"
Add-Check -Checks $checks -Name "combined local-light shadows draw casters or reuse valid cached tiles" `
    -Passed (
        (
            $metrics["local_shadow_recorded_tile_passes"].max -gt 0 -and
            $metrics["local_shadow_recorded_draws"].max -gt 0
        ) -or $metrics["local_shadow_cache_hit_tiles"].max -gt 0
    ) `
    -Actual "passes=$($metrics['local_shadow_recorded_tile_passes'].max),draws=$($metrics['local_shadow_recorded_draws'].max),cacheHits=$($metrics['local_shadow_cache_hit_tiles'].max)" `
    -Expected "draws > 0 or cache hits > 0"
Add-Check -Checks $checks -Name "rect-light adaptive budget remains explicit" `
    -Passed (
        $metrics["local_shadow_rect_extra_sample_tiles"].max -gt 0 -and
        $metrics["local_shadow_rect_budget_limited_sample_tiles"].max -gt 0
    ) `
    -Actual "extra=$($metrics['local_shadow_rect_extra_sample_tiles'].max),limited=$($metrics['local_shadow_rect_budget_limited_sample_tiles'].max)" `
    -Expected "both > 0"

$passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
$failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
$summary = [pscustomobject]@{
    generatedAt = (Get-Date).ToString("o")
    executable = $executable
    csv = $csvPath
    verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
    passCount = $passCount
    failCount = $failCount
    metrics = [pscustomobject]$metrics
    checks = @($checks)
}
$jsonPath = Join-Path $output "lighting_showcase_ceiling_lights_health.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
$summary

if ($Strict -and $summary.verdict -ne "pass") {
    throw "Lighting Showcase ceiling-light health failed. See $jsonPath"
}
