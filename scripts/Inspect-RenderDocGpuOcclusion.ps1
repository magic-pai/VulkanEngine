[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CapturePath,
    [string]$OutputPath = "tmp\renderdoc_gpu_occlusion_inspection.json",
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (![IO.Path]::IsPathRooted($CapturePath)) {
    $CapturePath = Join-Path $projectRoot $CapturePath
}
if (![IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = Join-Path $projectRoot $OutputPath
}
$CapturePath = [IO.Path]::GetFullPath($CapturePath)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (!(Test-Path -LiteralPath $CapturePath)) {
    throw "RenderDoc capture does not exist: $CapturePath"
}

$qrenderdoc = Get-Command qrenderdoc.exe -ErrorAction SilentlyContinue
$qrenderdocPath = if ($null -ne $qrenderdoc) {
    $qrenderdoc.Source
} else {
    "C:\Program Files\RenderDoc\qrenderdoc.exe"
}
if (!(Test-Path -LiteralPath $qrenderdocPath)) {
    throw "qrenderdoc.exe was not found"
}
if (Get-Process qrenderdoc -ErrorAction SilentlyContinue) {
    throw "Close the existing qrenderdoc window before scripted inspection"
}

New-Item -ItemType Directory -Force -Path (Split-Path $OutputPath -Parent) |
    Out-Null
Remove-Item -LiteralPath $OutputPath -ErrorAction SilentlyContinue
$pythonScript = Join-Path $PSScriptRoot "Inspect-RenderDocGpuOcclusion.py"
$oldCapturePath = $env:SE_RENDERDOC_CAPTURE_PATH
$oldInspectionPath = $env:SE_RENDERDOC_INSPECTION_PATH
try {
    $env:SE_RENDERDOC_CAPTURE_PATH = $CapturePath
    $env:SE_RENDERDOC_INSPECTION_PATH = $OutputPath
    $process = Start-Process -FilePath $qrenderdocPath `
        -ArgumentList @("--python", $pythonScript) `
        -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds(120)
    while (!$process.HasExited) {
        if ((Get-Date) -ge $deadline) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            throw "RenderDoc GPU occlusion inspection timed out"
        }
        Start-Sleep -Milliseconds 250
    }
} finally {
    $env:SE_RENDERDOC_CAPTURE_PATH = $oldCapturePath
    $env:SE_RENDERDOC_INSPECTION_PATH = $oldInspectionPath
}

if (!(Test-Path -LiteralPath $OutputPath)) {
    throw "qrenderdoc exited without a GPU occlusion inspection report"
}
$report = Get-Content -Raw $OutputPath | ConvertFrom-Json
if ($report.status -ne "complete") {
    throw "RenderDoc GPU occlusion inspection failed: $($report.error)"
}
Write-Host (
    "RenderDoc GPU occlusion: {0} pass / {1} fail" -f
        $report.passCount,
        $report.failCount
)
Write-Host "Report: $OutputPath"
if ($Strict -and [uint32]$report.failCount -gt 0) {
    exit 1
}
