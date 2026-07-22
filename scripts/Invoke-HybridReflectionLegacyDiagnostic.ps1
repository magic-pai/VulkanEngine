[CmdletBinding()]
param(
    [string]$ExecutablePath = "build\Release\SelfEngineForward3D.exe",
    [string]$ProductionShaderPath =
        "build\shaders\hybrid_reflection_ray_query.hlsl.spv",
    [string]$HybridTargetShaderPath =
        "tmp\hybrid_reflection_legacy_diagnostics\hybrid-target.spv",
    [string]$ForceCullShaderPath =
        "tmp\hybrid_reflection_legacy_diagnostics\force-cull.spv",
    [string]$ForceNoCullShaderPath =
        "tmp\hybrid_reflection_legacy_diagnostics\force-no-cull.spv",
    [switch]$Strict,
    [string]$OutputDirectory =
        "tmp\hybrid_reflection_legacy_diagnostics\runtime"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
function Resolve-ProjectPath([string]$Path) {
    if (![IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path $projectRoot $Path
    }
    return (Resolve-Path $Path).Path
}

$ExecutablePath = Resolve-ProjectPath $ExecutablePath
$ProductionShaderPath = Resolve-ProjectPath $ProductionShaderPath
$HybridTargetShaderPath = Resolve-ProjectPath $HybridTargetShaderPath
$ForceCullShaderPath = Resolve-ProjectPath $ForceCullShaderPath
$ForceNoCullShaderPath = Resolve-ProjectPath $ForceNoCullShaderPath
if (![IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Get-UInt($Row, [string]$Name) {
    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV column: $Name"
    }
    return [uint32]$property.Value
}

$managedKeys = @(
    "SE_HYBRID_REFLECTIONS_RT",
    "SE_HYBRID_REFLECTIONS_DIAGNOSTICS",
    "SE_SSR",
    "SE_SSR_BACKEND",
    "SE_FORWARD3D_AA_MODE",
    "SE_BENCHMARK_SCENE",
    "SE_SCENE_UPDATE_FREEZE",
    "SE_VISUAL_QA_HIDE_IMGUI",
    "SE_WINDOW_HIDDEN",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_AUTO_EXIT_FRAMES",
    "SE_BENCHMARK_CSV"
)
$lanes = @(
    [pscustomobject]@{
        name = "production"
        shader = $ProductionShaderPath
        forced = $false
    },
    [pscustomobject]@{
        name = "hybrid-target"
        shader = $HybridTargetShaderPath
        forced = $false
    },
    [pscustomobject]@{
        name = "force-cull"
        shader = $ForceCullShaderPath
        forced = $true
    },
    [pscustomobject]@{
        name = "force-no-cull"
        shader = $ForceNoCullShaderPath
        forced = $true
    }
)

$backupPath = Join-Path $OutputDirectory "production-shader-backup.spv"
Copy-Item -LiteralPath $ProductionShaderPath -Destination $backupPath -Force
$reports = [Collections.Generic.List[object]]::new()
try {
    foreach ($lane in $lanes) {
        if ($lane.shader -ne $ProductionShaderPath) {
            Copy-Item -LiteralPath $lane.shader `
                -Destination $ProductionShaderPath -Force
        }
        $csvPath = Join-Path $OutputDirectory "$($lane.name).csv"
        $stdoutPath = Join-Path $OutputDirectory "$($lane.name).stdout.log"
        $stderrPath = Join-Path $OutputDirectory "$($lane.name).stderr.log"
        Remove-Item -LiteralPath $csvPath, $stdoutPath, $stderrPath `
            -Force -ErrorAction SilentlyContinue

        $previous = @{}
        foreach ($key in $managedKeys) {
            $previous[$key] =
                [Environment]::GetEnvironmentVariable($key, "Process")
            [Environment]::SetEnvironmentVariable($key, $null, "Process")
        }
        $environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_DIAGNOSTICS = "1"
            SE_SSR = "1"
            SE_SSR_BACKEND = "ffx-sssr"
            SE_FORWARD3D_AA_MODE = "taa"
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_SCENE_UPDATE_FREEZE = "1"
            SE_VISUAL_QA_HIDE_IMGUI = "1"
            SE_WINDOW_HIDDEN = "1"
            SE_BENCHMARK_WARMUP_FRAMES = "2"
            SE_BENCHMARK_FRAMES = "7"
            SE_AUTO_EXIT_FRAMES = "14"
            SE_BENCHMARK_CSV = $csvPath
        }
        foreach ($entry in $environment.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable(
                $entry.Key,
                [string]$entry.Value,
                "Process"
            )
        }
        try {
            $executableDirectory = Split-Path -Parent $ExecutablePath
            $commandLine =
                "cd /d `"$executableDirectory`" && `"$ExecutablePath`"" +
                " 1> `"$stdoutPath`" 2> `"$stderrPath`""
            & cmd.exe /d /c $commandLine
            $exitCode = $LASTEXITCODE
        } finally {
            foreach ($entry in $previous.GetEnumerator()) {
                [Environment]::SetEnvironmentVariable(
                    $entry.Key,
                    $entry.Value,
                    "Process"
                )
            }
        }

        $metrics = $null
        if ($exitCode -eq 0 -and (Test-Path -LiteralPath $csvPath)) {
            $rows = @(Import-Csv -LiteralPath $csvPath)
            if ($rows.Count -gt 0) {
                $last = $rows[-1]
                $metrics = [pscustomobject]@{
                    rowCount = $rows.Count
                    readbackValid = Get-UInt $last `
                        "hybrid_reflections_ray_query_readback_valid"
                    candidateRayCount = Get-UInt $last `
                        "hybrid_reflections_ray_query_candidate_ray_count"
                    screenHitAcceptedCount = Get-UInt $last `
                        "hybrid_reflections_ray_query_screen_hit_accepted_count"
                    traceCount = Get-UInt $last `
                        "hybrid_reflections_ray_query_trace_count"
                    committedHitCount = Get-UInt $last `
                        "hybrid_reflections_ray_query_committed_hit_count"
                    invalidRayCount = Get-UInt $last `
                        "hybrid_reflections_ray_query_invalid_ray_count"
                    targetMaterialHitCount = Get-UInt $last `
                        "hybrid_reflections_ray_query_hit_attribute_position_mismatch_count"
                }
            }
        }
        $reports.Add([pscustomobject]@{
            name = $lane.name
            forced = $lane.forced
            exitCode = $exitCode
            metrics = $metrics
        }) | Out-Null
    }
} finally {
    Copy-Item -LiteralPath $backupPath `
        -Destination $ProductionShaderPath -Force
}

$failures = [Collections.Generic.List[string]]::new()
foreach ($report in $reports) {
    if ($report.exitCode -ne 0) {
        $failures.Add("$($report.name): exit=$($report.exitCode)")
        continue
    }
    if ($null -eq $report.metrics) {
        $failures.Add("$($report.name): no metrics")
        continue
    }
    if ($report.metrics.rowCount -ne 7) {
        $failures.Add("$($report.name): rows=$($report.metrics.rowCount)")
    }
    if ($report.metrics.readbackValid -ne 1) {
        $failures.Add("$($report.name): readback invalid")
    }
    if ($report.metrics.candidateRayCount -eq 0) {
        $failures.Add("$($report.name): no candidates")
    }
    if ($report.forced -and
        $report.metrics.screenHitAcceptedCount -ne 0) {
        $failures.Add(
            "$($report.name): screen accepted=" +
            $report.metrics.screenHitAcceptedCount
        )
    }
}
$forcedTargetHitMaximum = [uint32](($reports | Where-Object forced |
    ForEach-Object { $_.metrics.targetMaterialHitCount } |
    Measure-Object -Maximum).Maximum)
if ($forcedTargetHitMaximum -eq 0) {
    $failures.Add("forced traversal did not reach silver target material")
}

$summary = [pscustomobject]@{
    passCount = if ($failures.Count -eq 0) { 1 } else { 0 }
    failCount = $failures.Count
    failures = @($failures)
    lanes = @($reports)
}
$summaryPath = Join-Path $OutputDirectory "summary.json"
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath
foreach ($report in $reports) {
    if ($null -ne $report.metrics) {
        Write-Host (
            "$($report.name): candidates=$($report.metrics.candidateRayCount), " +
            "screen=$($report.metrics.screenHitAcceptedCount), " +
            "trace=$($report.metrics.traceCount), " +
            "hits=$($report.metrics.committedHitCount), " +
            "silver=$($report.metrics.targetMaterialHitCount)"
        )
    }
}
Write-Host "Hybrid legacy diagnostic: $($summary.passCount) pass / $($summary.failCount) fail"
Write-Host "Summary: $summaryPath"
if ($Strict -and $failures.Count -gt 0) {
    exit 1
}
