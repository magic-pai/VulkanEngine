[CmdletBinding()]
param(
    [string]$CaptureManifest = "",
    [string]$OutputPath = "tmp\renderdoc_integration_static.json",
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (![IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = Join-Path $projectRoot $OutputPath
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)

$checks = [Collections.Generic.List[object]]::new()
function Add-Check([string]$Name, [bool]$Passed, $Actual, $Expected) {
    $checks.Add([pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }) | Out-Null
}

$headerPath = Join-Path $projectRoot "thirdParty\renderdoc\renderdoc_app.h"
$controllerPath = Join-Path $projectRoot "src\app\renderdoc_capture.cpp"
$applicationPath = Join-Path $projectRoot "src\app\application.cpp"
$commandBufferPath = Join-Path $projectRoot `
    "src\renderer\vulkan\command_buffer.cpp"
$ffxAdapterPath = Join-Path $projectRoot `
    "src\renderer\vulkan\fidelityfx_sssr_adapter.cpp"
$cmakePath = Join-Path $projectRoot "CMakeLists.txt"
$captureScriptPath = Join-Path $projectRoot "scripts\Capture-RenderDocFrame.ps1"
$expectedHeaderHash =
    "B7005E7DC34C3635046868BBD76D81B9B055AEDE0F56DAA0BD39FEDEE0639FFB"

Add-Check "official RenderDoc header exists" (Test-Path $headerPath) `
    (Test-Path $headerPath) $true
if (Test-Path $headerPath) {
    $headerHash = (Get-FileHash -Algorithm SHA256 $headerPath).Hash
    Add-Check "official RenderDoc header hash" `
        ($headerHash -eq $expectedHeaderHash) $headerHash $expectedHeaderHash
}

$controller = Get-Content -Raw $controllerPath
$application = Get-Content -Raw $applicationPath
$commandBuffer = Get-Content -Raw $commandBufferPath
$ffxAdapter = Get-Content -Raw $ffxAdapterPath
$cmake = Get-Content -Raw $cmakePath
foreach ($needle in @(
    "RENDERDOC_GetAPI",
    "eRENDERDOC_API_Version_1_6_0",
    "StartFrameCapture",
    "EndFrameCapture",
    "SE_RENDERDOC_CAPTURE_FRAME",
    "SE_RENDERDOC_STATUS_JSON",
    "GetModuleHandleW"
)) {
    Add-Check "controller contains $needle" ($controller.Contains($needle)) `
        ($controller.Contains($needle)) $true
}
Add-Check "application brackets DrawFrame start" `
    ($application -match "BeginFrame\(nextRenderedFrame\)[\s\S]*DrawFrame\(\)") `
    "source-order" "BeginFrame before DrawFrame"
Add-Check "application brackets DrawFrame end" `
    ($application -match "DrawFrame\(\)[\s\S]*EndFrame\(nextRenderedFrame\)") `
    "source-order" "EndFrame after DrawFrame"
Add-Check "application capture calls are Debug-only" `
    ($application -match "#if defined\(_DEBUG\)[\s\S]*RenderDocCaptureController") `
    "_DEBUG" "_DEBUG"
Add-Check "CMake includes controller source" `
    ($cmake.Contains("src/app/renderdoc_capture.cpp")) `
    ($cmake.Contains("src/app/renderdoc_capture.cpp")) $true
Add-Check "CMake includes official header directory" `
    ($cmake.Contains("thirdParty/renderdoc")) `
    ($cmake.Contains("thirdParty/renderdoc")) $true
foreach ($label in @(
    "SelfEngine.Reflection.FFX.ClassifyTiles",
    "SelfEngine.Reflection.FFX.Intersect",
    "SelfEngine.Reflection.DNSR.Reproject",
    "SelfEngine.Reflection.DNSR.Prefilter",
    "SelfEngine.Reflection.DNSR.ResolveTemporal",
    "SelfEngine.Reflection.Apply"
)) {
    Add-Check "capture event label $label" ($commandBuffer.Contains($label)) `
        ($commandBuffer.Contains($label)) $true
}
foreach ($resource in @(
    '"RayList"',
    '"IntersectRadiance"',
    '"IntersectConfidence"',
    '"RadianceHistory"',
    '"HitConfidenceHistory"',
    '"ReprojectedRadiance"',
    '"PrefilterRadiance"'
)) {
    Add-Check "capture resource name $resource" `
        ($ffxAdapter.Contains($resource)) ($ffxAdapter.Contains($resource)) $true
}

$tokens = $null
$parseErrors = $null
[Management.Automation.Language.Parser]::ParseFile(
    $captureScriptPath,
    [ref]$tokens,
    [ref]$parseErrors
) | Out-Null
Add-Check "capture wrapper syntax" ($parseErrors.Count -eq 0) `
    $parseErrors.Count 0

if (![string]::IsNullOrWhiteSpace($CaptureManifest)) {
    if (![IO.Path]::IsPathRooted($CaptureManifest)) {
        $CaptureManifest = Join-Path $projectRoot $CaptureManifest
    }
    Add-Check "capture manifest exists" (Test-Path $CaptureManifest) `
        (Test-Path $CaptureManifest) $true
    if (Test-Path $CaptureManifest) {
        $manifest = Get-Content -Raw $CaptureManifest | ConvertFrom-Json
        Add-Check "runtime capture contract" ([bool]$manifest.contractReady) `
            ([bool]$manifest.contractReady) $true
        Add-Check "runtime capture file exists" `
            (Test-Path ([string]$manifest.capturePath)) `
            ([string]$manifest.capturePath) "existing .rdc"
        Add-Check "runtime capture thumbnail decoded" `
            ([bool]$manifest.thumbnailReady -and
             (Test-Path ([string]$manifest.thumbnailPath)) -and
             [uint32]$manifest.thumbnailWidth -gt 0 -and
             [uint32]$manifest.thumbnailHeight -gt 0) `
            "$($manifest.thumbnailWidth)x$($manifest.thumbnailHeight)" `
            "non-empty decoded image"
    }
}

$passCount = @($checks | Where-Object status -eq "pass").Count
$failCount = @($checks | Where-Object status -eq "fail").Count
$summary = [ordered]@{
    passCount = $passCount
    failCount = $failCount
    runtimeManifestRequested =
        ![string]::IsNullOrWhiteSpace($CaptureManifest)
    checks = @($checks)
}
New-Item -ItemType Directory -Force -Path (Split-Path $OutputPath -Parent) |
    Out-Null
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath
Write-Host "RenderDoc integration: $passCount pass / $failCount fail"
Write-Host "Report: $OutputPath"
if ($Strict -and $failCount -gt 0) {
    exit 1
}
