[CmdletBinding()]
param(
    [string]$ExecutablePath = "build\Release\SelfEngineForward3D.exe",
    [int]$AutoExitFrames = 90,
    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$executable = [IO.Path]::GetFullPath((Join-Path $projectRoot $ExecutablePath))
$monitorPath = Join-Path $projectRoot ".selfengine\scene_builder\runtime_monitor.json"
$scenePath = Join-Path $projectRoot ".selfengine\scene_builder\scene.json"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Scene Builder executable not found: $executable"
}
if (-not (Test-Path -LiteralPath $scenePath -PathType Leaf)) {
    throw "Current Scene Builder document not found: $scenePath"
}
if ($AutoExitFrames -lt 2 -or $TimeoutSeconds -lt 10) {
    throw "AutoExitFrames must be at least 2 and TimeoutSeconds at least 10."
}

$beforeWriteUtc = if (Test-Path -LiteralPath $monitorPath) {
    (Get-Item -LiteralPath $monitorPath).LastWriteTimeUtc
} else {
    [datetime]::MinValue
}
$managedEnvironment = [ordered]@{
    SE_WINDOW_HIDDEN = "1"
    SE_AUTO_EXIT_FRAMES = $AutoExitFrames.ToString()
    SE_WINDOW_WIDTH = "1280"
    SE_WINDOW_HEIGHT = "720"
    SE_FORWARD3D_AA_MODE = "sr-performance"
    SE_HYBRID_REFLECTIONS_RT = "1"
    SE_HYBRID_REFLECTIONS_RT_OFF = "0"
    SE_SSR = "0"
    SE_SCENE_BUILDER_SELF_TEST = $null
    SE_BENCHMARK_SCENE = $null
}
$previousEnvironment = @{}
foreach ($name in $managedEnvironment.Keys) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
    [Environment]::SetEnvironmentVariable($name, $managedEnvironment[$name], "Process")
}

try {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $executable
    $startInfo.WorkingDirectory = $projectRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Scene Builder health process did not start."
    }
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Scene Builder health run timed out after $TimeoutSeconds seconds."
    }
    $exitCode = $process.ExitCode
    if ($exitCode -ne 0) {
        throw "Scene Builder health run exited with code $exitCode."
    }
} finally {
    foreach ($name in $managedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], "Process")
    }
}

if (-not (Test-Path -LiteralPath $monitorPath -PathType Leaf)) {
    throw "Scene Builder did not write runtime monitor: $monitorPath"
}
$monitorItem = Get-Item -LiteralPath $monitorPath
$monitor = Get-Content -Raw -Encoding UTF8 -LiteralPath $monitorPath | ConvertFrom-Json

$checks = [Collections.Generic.List[object]]::new()
function Add-HealthCheck([string]$Name, [bool]$Passed, $Actual) {
    $checks.Add([pscustomobject]@{
        name = $Name
        passed = $Passed
        actual = $Actual
    })
}

$builder = $monitor.scene.builder
$draw = $monitor.pipeline.draw
$hybrid = $monitor.pipeline.hybridReflections
$ssr = $monitor.pipeline.ssr
$frameGraph = $monitor.pipeline.frameGraph
Add-HealthCheck "fresh current-run monitor" ($monitorItem.LastWriteTimeUtc -gt $beforeWriteUtc) $monitorItem.LastWriteTimeUtc
Add-HealthCheck "saved scene objects are loaded" ($builder.objectCount -gt 0 -and $builder.sceneRenderableCount -eq $builder.objectCount) "$($builder.objectCount)/$($builder.sceneRenderableCount)"
Add-HealthCheck "main and shadow queues execute" ($draw.mainDraws -gt 0 -and $draw.shadowDraws -gt 0) "$($draw.mainDraws)/$($draw.shadowDraws)"
$rayQueryExecuted = $hybrid.active -eq 1 -and $hybrid.rayQueryDispatchCount -gt 0 -and $hybrid.rayQueryDispatchReady -eq 1 -and $hybrid.rayQueryResourcesReady -eq 1 -and $hybrid.rayQueryTlasDescriptorReady -eq 1 -and $hybrid.tlasInstanceCount -gt 0 -and $hybrid.rayQueryHitIblEnabled -eq 1 -and $hybrid.rayQueryHitIblSpecularIntensityMilliunits -gt 0
$rayQueryReadbackConsistent = $hybrid.rayQueryReadbackValid -ne 1 -or $hybrid.rayQueryHitLightingResolvedCount -gt 0
$rayQueryHealthy = $rayQueryExecuted -and $rayQueryReadbackConsistent
Add-HealthCheck "Ray Query executes with hit IBL" $rayQueryHealthy "$($hybrid.active)/$($hybrid.rayQueryDispatchCount)/$($hybrid.rayQueryReadbackValid)/$($hybrid.rayQueryHitIblEnabled)/$($hybrid.rayQueryHitLightingResolvedCount)"
$ssrDisabled = $ssr.enabled -eq 0 -and $ssr.backendActiveProvider -eq 0 -and $ssr.reconstructionTraceDispatches -eq 0
Add-HealthCheck "SSR remains disabled" $ssrDisabled "$($ssr.enabled)/$($ssr.backendActiveProvider)/$($ssr.reconstructionTraceDispatches)"
Add-HealthCheck "Frame Graph is valid" ($frameGraph.validation.issueCount -eq 0) $frameGraph.validation.issueCount
Add-HealthCheck "monitor writer succeeds" ($monitor.writer.previousWriteSucceeded -and $monitor.writer.writeFailureCount -eq 0) "$($monitor.writer.previousWriteSucceeded)/$($monitor.writer.writeFailureCount)"

$failed = @($checks | Where-Object { -not $_.passed })
$checks | Format-Table -AutoSize
if ($failed.Count -ne 0) {
    throw "Scene Builder health failed: $($failed.name -join ', ')"
}
Write-Host "Scene Builder health passed: $($checks.Count) checks, frame $($monitor.frame.renderedIndex)."
