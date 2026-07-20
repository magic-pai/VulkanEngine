[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$shaderPath = Join-Path $repoRoot "assets\shaders\deferred_lighting.frag"
$rendererPath = Join-Path $repoRoot "src\renderer\vulkan\renderer.cpp"
$settingsPath = Join-Path $repoRoot "src\renderer\vulkan\shadow_settings.h"
$descriptorLayoutPath = Join-Path $repoRoot "src\renderer\vulkan\descriptor_set_layout.cpp"
$descriptorSetsPath = Join-Path $repoRoot "src\renderer\vulkan\descriptor_sets.cpp"
$frameGraphPath = Join-Path $repoRoot "src\renderer\vulkan\frame_graph.cpp"
$shader = Get-Content -Raw -LiteralPath $shaderPath
$traceShader = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "assets\shaders\ssr_trace.comp")
$temporalShader = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "assets\shaders\ssr_temporal.comp")
$spatialShader = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "assets\shaders\ssr_spatial.comp")
$renderer = Get-Content -Raw -LiteralPath $rendererPath
$settings = Get-Content -Raw -LiteralPath $settingsPath
$descriptorLayout = Get-Content -Raw -LiteralPath $descriptorLayoutPath
$descriptorSets = Get-Content -Raw -LiteralPath $descriptorSetsPath
$frameGraph = Get-Content -Raw -LiteralPath $frameGraphPath
$checks = [System.Collections.Generic.List[object]]::new()

function Add-Check {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Passed,
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)]$Expected
    )
    $checks.Add([pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }) | Out-Null
}

function Get-SmoothStep {
    param([double]$Minimum, [double]$Maximum, [double]$Value)
    $t = [Math]::Min([Math]::Max(($Value - $Minimum) / ($Maximum - $Minimum), 0.0), 1.0)
    return $t * $t * (3.0 - 2.0 * $t)
}

function Get-SsrValidationConfidence {
    param(
        [double]$ReceiverNdotV,
        [double]$HitFacing,
        [double]$DepthDelta,
        [double]$Thickness,
        [double]$StableHitFootprintRatio,
        [double]$StableReceiverFootprintRatio,
        [double]$TravelPixels
    )
    $receiver = Get-SmoothStep 0.06 0.30 $ReceiverNdotV
    $hit = Get-SmoothStep 0.05 0.25 $HitFacing
    $frontError = [Math]::Max($DepthDelta, 0.0)
    $behindError = [Math]::Max(-$DepthDelta, 0.0)
    $front = 1.0 - (Get-SmoothStep ($Thickness * 0.50) $Thickness $frontError)
    $behind = 1.0 - (Get-SmoothStep $Thickness ($Thickness * 2.0) $behindError)
    $hitFootprint = Get-SmoothStep 0.55 0.95 $StableHitFootprintRatio
    $receiverFootprint = Get-SmoothStep 0.75 0.98 $StableReceiverFootprintRatio
    $minimumTravel = 2.0 + (1.0 - $receiver) * 4.0
    $travel = Get-SmoothStep $minimumTravel ($minimumTravel + 2.0) $TravelPixels
    return $receiver * $hit * $front * $behind * $hitFootprint * $receiverFootprint * $travel
}

$stable = Get-SsrValidationConfidence 0.75 0.8 0.0 0.08 1.0 1.0 12.0
$grazing = Get-SsrValidationConfidence 0.02 0.8 0.0 0.08 1.0 1.0 12.0
$backFacing = Get-SsrValidationConfidence 0.75 -0.1 0.0 0.08 1.0 1.0 12.0
$behind = Get-SsrValidationConfidence 0.75 0.8 -0.24 0.08 1.0 1.0 12.0
$edge = Get-SsrValidationConfidence 0.75 0.8 0.0 0.08 0.4 1.0 12.0
$receiverEdge = Get-SsrValidationConfidence 0.75 0.8 0.0 0.08 1.0 0.4 12.0
$selfHit = Get-SsrValidationConfidence 0.08 0.8 0.0 0.08 1.0 1.0 1.0

Add-Check "stable front-facing hit remains usable" ($stable -gt 0.95) $stable "> 0.95"
Add-Check "grazing receiver falls back" ($grazing -lt 0.05) $grazing "< 0.05"
Add-Check "back-facing hit falls back" ($backFacing -lt 0.05) $backFacing "< 0.05"
Add-Check "deep-behind hit falls back" ($behind -lt 0.05) $behind "< 0.05"
Add-Check "discontinuous hit footprint falls back" ($edge -lt 0.05) $edge "< 0.05"
Add-Check "discontinuous receiver footprint falls back" ($receiverEdge -lt 0.05) $receiverEdge "< 0.05"
Add-Check "near-origin silhouette self-hit falls back" ($selfHit -lt 0.05) $selfHit "< 0.05"

$requiredShaderSymbols = @(
    "SsrHitValidationEnabled",
    "SsrSignedDepthConfidence",
    "SsrHitFootprintConfidence",
    "SsrReceiverFootprintConfidence",
    "SsrHitValidationConfidence",
    "SsrFetchDepthNearest",
    "SsrFetchNormalRoughnessNearest",
    "hitFacing",
    "footprintConfidence",
    "receiverFootprintConfidence",
    "validationConfidence",
    "SsrDeferredReceiverReprojectionEnabled",
    "SsrDeferredReceiverHistoryConfidence",
    "ssrHistoryMetadata",
    "fragUv - velocity",
    "frame.previousView"
)
foreach ($symbol in $requiredShaderSymbols) {
    Add-Check "shader contains $symbol" ($shader -match [regex]::Escape($symbol)) `
        ($shader -match [regex]::Escape($symbol)) "true"
}
Add-Check "runtime exposes hit validation control" `
    ($renderer -match "SE_SSR_HIT_VALIDATION" -and $settings -match "ssrHitValidationEnabled") `
    "env=$($renderer -match 'SE_SSR_HIT_VALIDATION'),setting=$($settings -match 'ssrHitValidationEnabled')" `
    "true/true"
Add-Check "runtime exposes deferred receiver reprojection control" `
    ($renderer -match "SE_SSR_DEFERRED_REPROJECTION" -and
        $settings -match "ssrDeferredReceiverReprojectionEnabled") `
    "env=$($renderer -match 'SE_SSR_DEFERRED_REPROJECTION'),setting=$($settings -match 'ssrDeferredReceiverReprojectionEnabled')" `
    "true/true"
Add-Check "Deferred SSR metadata binding is declared and written" `
    ($shader -match "layout\(set = 1, binding = 18\).*ssrHistoryMetadata" -and
        $descriptorLayout -match "ssrHistoryMetadataSamplerLayoutBinding\.binding = 18" -and
        $descriptorSets -match "SsrHistoryMetadataView" -and
        $descriptorSets -match "dstBinding = bindings\[index\]") `
    "shader/layout/descriptor=$($shader -match 'binding = 18'),$($descriptorLayout -match 'binding = 18'),$($descriptorSets -match 'SsrHistoryMetadataView')" `
    "true/true/true"
Add-Check "FrameGraph models SSR consumer history" `
    ($frameGraph -match 'PersistentHistory,\s*"SSRResolved"' -and
        $frameGraph -match "SSRResolved, SSRHistoryMetadata") `
    "persistent=$($frameGraph -match 'PersistentHistory,\s*"SSRResolved"'),consumer=$($frameGraph -match 'SSRResolved, SSRHistoryMetadata')" `
    "true/true"
$nearestDepthFetch = $shader -match "texelFetch\(sceneDepth"
$nearestCoordHelper = $shader -match "SsrNearestTexel"
Add-Check "SSR hit tests use nearest depth texels" `
    ($nearestDepthFetch -and $nearestCoordHelper) `
    "texelFetch=$nearestDepthFetch,coord=$nearestCoordHelper" "true/true"
Add-Check "SSR traversal is deterministic without hit history" `
    ($shader -match "const float jitter = 0\.5") `
    ($shader -match "const float jitter = 0.5") "true"
Add-Check "SSR packed controls decode positional bits without high-bit leakage" `
    ($shader -match "floor\(abs\(frame\.ssrControls\.w\) / 1024\.0\)" -and
        $shader -match "mod\(encodedControl, 2\.0\)" -and
        $traceShader -match "mod\(floor\(abs\(frame\.ssrControls\.w\)\), 64\.0\)" -and
        $temporalShader -match "floor\(abs\(frame\.ssrControls\.w\) / 2048\.0\)" -and
        $temporalShader -match "floor\(abs\(frame\.ssrControls\.w\) / 65536\.0\)" -and
        $spatialShader -match "floor\(abs\(frame\.ssrControls\.w\) / 8192\.0\)" -and
        $shader -match "floor\(abs\(frame\.ssrControls\.w\) / 262144\.0\)") `
    "deferred1024=$($shader -match '1024\.0'),traceLow6=$($traceShader -match '64\.0'),temporal2048=$($temporalShader -match '2048\.0'),temporal65536=$($temporalShader -match '65536\.0'),spatial8192=$($spatialShader -match '8192\.0'),sameFrame262144=$($shader -match '262144\.0')" `
    "true/true/true/true/true/true"

$passCount = @($checks | Where-Object status -eq "pass").Count
$failCount = @($checks | Where-Object status -eq "fail").Count
$checks | Format-Table -AutoSize
[pscustomobject]@{
    verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
    passCount = $passCount
    failCount = $failCount
}
if ($failCount -gt 0) {
    exit 1
}
