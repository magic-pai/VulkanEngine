[CmdletBinding()]
param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [uint32]$TargetSubmissionIndex = 8,
    [switch]$Strict,
    [string]$OutputDirectory = "tmp\hybrid_reflection_target_attribution"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (![IO.Path]::IsPathRooted($ExecutablePath)) {
    $ExecutablePath = Join-Path $projectRoot $ExecutablePath
}
$ExecutablePath = (Resolve-Path $ExecutablePath).Path
if (![IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function New-Check {
    param([string]$Name, [bool]$Passed, [object]$Actual, [object]$Expected)
    [pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }
}

function Get-UInt {
    param($Row, [string]$Name)
    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV column: $Name"
    }
    return [uint32]$property.Value
}

$managedKeys = @(
    "SE_HYBRID_REFLECTIONS_RT",
    "SE_HYBRID_REFLECTIONS_DIAGNOSTICS",
    "SE_HYBRID_REFLECTIONS_DIAGNOSTIC_TARGET_SUBMISSION",
    "SE_HYBRID_REFLECTIONS_FORCE_ALL_RAY_QUERIES",
    "SE_HYBRID_REFLECTIONS_CULL_BACK_FACES_OFF",
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
    [pscustomobject]@{ name = "hybrid-default"; force = 0; cull = 1 },
    [pscustomobject]@{ name = "force-ray-query-cull"; force = 1; cull = 1 },
    [pscustomobject]@{ name = "force-ray-query-no-cull"; force = 1; cull = 0 }
)

$reports = [Collections.Generic.List[object]]::new()
foreach ($lane in $lanes) {
    $laneDirectory = Join-Path $OutputDirectory $lane.name
    New-Item -ItemType Directory -Force -Path $laneDirectory | Out-Null
    $csvPath = Join-Path $laneDirectory "target-attribution.csv"
    $stdoutPath = Join-Path $laneDirectory "stdout.log"
    $stderrPath = Join-Path $laneDirectory "stderr.log"
    Remove-Item -LiteralPath $csvPath, $stdoutPath, $stderrPath `
        -Force -ErrorAction SilentlyContinue

    $previous = @{}
    foreach ($key in $managedKeys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, $null, "Process")
    }
    $environment = @{
        SE_HYBRID_REFLECTIONS_RT = "1"
        SE_HYBRID_REFLECTIONS_DIAGNOSTICS = "1"
        SE_HYBRID_REFLECTIONS_DIAGNOSTIC_TARGET_SUBMISSION =
            [string]$TargetSubmissionIndex
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
    if ($lane.force -ne 0) {
        $environment.SE_HYBRID_REFLECTIONS_FORCE_ALL_RAY_QUERIES = "1"
    }
    if ($lane.cull -eq 0) {
        $environment.SE_HYBRID_REFLECTIONS_CULL_BACK_FACES_OFF = "1"
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

    $checks = [Collections.Generic.List[object]]::new()
    $checks.Add((New-Check "$($lane.name) process exits" `
        ($exitCode -eq 0) $exitCode 0)) | Out-Null
    $checks.Add((New-Check "$($lane.name) writes CSV" `
        (Test-Path -LiteralPath $csvPath) `
        (Test-Path -LiteralPath $csvPath) $true)) | Out-Null

    $metrics = $null
    if ($exitCode -eq 0 -and (Test-Path -LiteralPath $csvPath)) {
        $rows = @(Import-Csv -LiteralPath $csvPath)
        $checks.Add((New-Check "$($lane.name) captures rows" `
            ($rows.Count -eq 7) $rows.Count 7)) | Out-Null
        if ($rows.Count -gt 0) {
            $last = $rows[-1]
            $target = Get-UInt $last `
                "hybrid_reflections_ray_query_diagnostic_target_submission_index"
            $targetMatches = Get-UInt $last `
                "hybrid_reflections_ray_query_diagnostic_target_match_count"
            $targetTlas = Get-UInt $last `
                "hybrid_reflections_ray_query_diagnostic_target_tlas_index"
            $targetMaterial = Get-UInt $last `
                "hybrid_reflections_ray_query_diagnostic_target_material_index"
            $force = Get-UInt $last `
                "hybrid_reflections_ray_query_force_all_ray_queries"
            $cull = Get-UInt $last `
                "hybrid_reflections_ray_query_cull_back_facing_triangles"
            $readback = Get-UInt $last `
                "hybrid_reflections_ray_query_readback_valid"
            $candidate = Get-UInt $last `
                "hybrid_reflections_ray_query_candidate_ray_count"
            $screenAccepted = Get-UInt $last `
                "hybrid_reflections_ray_query_screen_hit_accepted_count"
            $trace = Get-UInt $last `
                "hybrid_reflections_ray_query_trace_count"
            $invalidRay = Get-UInt $last `
                "hybrid_reflections_ray_query_invalid_ray_count"
            $targetHits = Get-UInt $last `
                "hybrid_reflections_ray_query_diagnostic_target_committed_hit_count"
            $targetAttributes = Get-UInt $last `
                "hybrid_reflections_ray_query_diagnostic_target_attribute_resolved_count"
            $targetWrites = Get-UInt $last `
                "hybrid_reflections_ray_query_diagnostic_target_denoiser_write_count"

            $checks.Add((New-Check "$($lane.name) target identity" `
                ($target -eq $TargetSubmissionIndex) $target $TargetSubmissionIndex)) | Out-Null
            $checks.Add((New-Check "$($lane.name) target maps once" `
                ($targetMatches -eq 1) $targetMatches 1)) | Out-Null
            $checks.Add((New-Check "$($lane.name) target TLAS index valid" `
                ($targetTlas -ne [uint32]::MaxValue) $targetTlas "!= uint32::max")) | Out-Null
            $checks.Add((New-Check "$($lane.name) target material valid" `
                ($targetMaterial -gt 0) $targetMaterial ">0")) | Out-Null
            $checks.Add((New-Check "$($lane.name) force control" `
                ($force -eq $lane.force) $force $lane.force)) | Out-Null
            $checks.Add((New-Check "$($lane.name) cull control" `
                ($cull -eq $lane.cull) $cull $lane.cull)) | Out-Null
            $checks.Add((New-Check "$($lane.name) diagnostics readback" `
                ($readback -eq 1) $readback 1)) | Out-Null
            $checks.Add((New-Check "$($lane.name) has candidates" `
                ($candidate -gt 0) $candidate ">0")) | Out-Null
            if ($lane.force -ne 0) {
                $checks.Add((New-Check "$($lane.name) bypasses screen hits" `
                    ($screenAccepted -eq 0) $screenAccepted 0)) | Out-Null
                $checks.Add((New-Check "$($lane.name) accounts candidates" `
                    ($trace + $invalidRay -eq $candidate) `
                    ($trace + $invalidRay) $candidate)) | Out-Null
            }
            $checks.Add((New-Check "$($lane.name) target attributes complete" `
                ($targetAttributes -eq $targetHits) `
                $targetAttributes $targetHits)) | Out-Null
            $checks.Add((New-Check "$($lane.name) target writes complete" `
                ($targetWrites -eq $targetHits) $targetWrites $targetHits)) | Out-Null

            $metrics = [pscustomobject]@{
                targetSubmissionIndex = $target
                targetTlasIndex = $targetTlas
                targetMaterialIndex = $targetMaterial
                candidateRayCount = $candidate
                screenHitAcceptedCount = $screenAccepted
                traceCount = $trace
                invalidRayCount = $invalidRay
                targetCommittedHitCount = $targetHits
                targetAttributeResolvedCount = $targetAttributes
                targetDenoiserWriteCount = $targetWrites
            }
        }
    }
    $reports.Add([pscustomobject]@{
        name = $lane.name
        forceAllRayQueries = $lane.force
        cullBackFacingTriangles = $lane.cull
        metrics = $metrics
        checks = @($checks)
    }) | Out-Null
}

$forcedReports = @($reports | Where-Object { $_.forceAllRayQueries -ne 0 })
$forcedTargetHitMaximum = [uint32](($forcedReports | ForEach-Object {
    if ($null -eq $_.metrics) { 0 } else { $_.metrics.targetCommittedHitCount }
} | Measure-Object -Maximum).Maximum)
$globalChecks = @(
    (New-Check "forced traversal reaches target" `
        ($forcedTargetHitMaximum -gt 0) $forcedTargetHitMaximum ">0")
)
$allChecks = @($reports | ForEach-Object { $_.checks }) + $globalChecks
$passCount = @($allChecks | Where-Object status -eq "pass").Count
$failCount = @($allChecks | Where-Object status -eq "fail").Count
$summary = [pscustomobject]@{
    targetSubmissionIndex = $TargetSubmissionIndex
    passCount = $passCount
    failCount = $failCount
    globalChecks = $globalChecks
    lanes = @($reports)
}
$summaryPath = Join-Path $OutputDirectory "summary.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath
Write-Host "Hybrid reflection target attribution: $passCount pass / $failCount fail"
foreach ($report in $reports) {
    $metrics = $report.metrics
    if ($null -ne $metrics) {
        Write-Host (
            "$($report.name): targetTLAS=$($metrics.targetTlasIndex), " +
            "screen=$($metrics.screenHitAcceptedCount), " +
            "trace=$($metrics.traceCount), targetHits=" +
            "$($metrics.targetCommittedHitCount), targetWrites=" +
            "$($metrics.targetDenoiserWriteCount)"
        )
    }
}
Write-Host "Summary: $summaryPath"

if ($Strict -and $failCount -gt 0) {
    exit 1
}
