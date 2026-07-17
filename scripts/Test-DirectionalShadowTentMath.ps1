[CmdletBinding()]
param(
    [string]$OutputPath = "tmp\\directional_shadow_tent_math.json",
    [switch]$Strict
)

$ErrorActionPreference = "Stop"

function Add-LinearSampleContribution {
    param(
        [hashtable]$Weights,
        [double]$Coordinate,
        [double]$Weight
    )

    [int]$left = [Math]::Floor($Coordinate)
    $fraction = $Coordinate - $left
    $right = $left + 1
    $leftWeight = if ($Weights.ContainsKey($left)) { $Weights[$left] } else { 0.0 }
    $rightWeight = if ($Weights.ContainsKey($right)) { $Weights[$right] } else { 0.0 }
    $null = $Weights[$left] = $leftWeight + (1.0 - $fraction) * $Weight
    $null = $Weights[$right] = $rightWeight + $fraction * $Weight
}

function Get-OptimizedTentWeights {
    param(
        [double]$SubTexel,
        [switch]$LegacyUnaligned
    )

    $weights = @{}
    $uWeights = @(
        (4.0 - 3.0 * $SubTexel),
        7.0,
        (1.0 + 3.0 * $SubTexel)
    )
    [double]$u0 = ((3.0 - 2.0 * $SubTexel) / $uWeights[0]) - 2.0
    [double]$u1 = (3.0 + $SubTexel) / $uWeights[1]
    [double]$u2 = ($SubTexel / $uWeights[2]) + 2.0
    $offsets = @($u0, $u1, $u2)

    for ($index = 0; $index -lt 3; ++$index) {
        $coordinate = $offsets[$index]
        if ($LegacyUnaligned) {
            # A texel-center lookup has atlasUv/texelSize = n + 0.5.
            $coordinate += 0.5
        }
        Add-LinearSampleContribution -Weights $weights -Coordinate $coordinate -Weight $uWeights[$index]
    }
    Write-Output -NoEnumerate $weights
}

$expected = @{
    -2 = 1.0
    -1 = 3.0
    0 = 4.0
    1 = 3.0
    2 = 1.0
}
$corrected = Get-OptimizedTentWeights -SubTexel 0.0
$legacy = Get-OptimizedTentWeights -SubTexel 0.5 -LegacyUnaligned
$correctedMaxError = 0.0
$legacyMaxError = 0.0

foreach ($index in -2..3) {
    $target = if ($expected.ContainsKey($index)) { $expected[$index] } else { 0.0 }
    $correctedActual = if ($corrected.ContainsKey($index)) { $corrected[$index] } else { 0.0 }
    $legacyActual = if ($legacy.ContainsKey($index)) { $legacy[$index] } else { 0.0 }
    $correctedMaxError = [Math]::Max($correctedMaxError, [Math]::Abs($correctedActual - $target))
    $legacyMaxError = [Math]::Max($legacyMaxError, [Math]::Abs($legacyActual - $target))
}

$shaderPaths = @(
    "assets/shaders/forward_3d.frag",
    "assets/shaders/deferred_lighting.frag",
    "assets/shaders/gbuffer_debug.frag",
    "assets/shaders/weighted_translucency_3d.frag"
)
$shaderContract = @()
foreach ($path in $shaderPaths) {
    $source = Get-Content -LiteralPath $path -Raw
    $shaderContract += [pscustomobject]@{
        path = $path
        texelCenterAligned = $source.Contains("vec2 texelPosition = atlasUv / texelSize - vec2(0.5);") -and
            $source.Contains("vec2 baseAtlasUv = (baseTexel + vec2(0.5)) * texelSize;") -and
            $source.Contains("baseAtlasUv + vec2(u[x], v[y]) * texelSize")
        hardwareComparisonSampler = $source.Contains("uniform sampler2DShadow shadowSampler;")
    }
}

$contractsPass = @($shaderContract | Where-Object {
    -not $_.texelCenterAligned -or -not $_.hardwareComparisonSampler
}).Count -eq 0
$passed = $correctedMaxError -le 0.000001 -and $legacyMaxError -gt 0.1 -and $contractsPass
$correctedWeightRows = @(
    -2..3 | ForEach-Object {
        [pscustomobject]@{
            index = $_
            weight = if ($corrected.ContainsKey($_)) { $corrected[$_] } else { 0.0 }
        }
    }
)
$expectedWeightRows = @(
    -2..3 | ForEach-Object {
        [pscustomobject]@{
            index = $_
            weight = if ($expected.ContainsKey($_)) { $expected[$_] } else { 0.0 }
        }
    }
)
$report = [pscustomobject]@{
    verdict = if ($passed) { "pass" } else { "fail" }
    correctedMaxWeightError = $correctedMaxError
    legacyUnalignedMaxWeightError = $legacyMaxError
    correctedWeights = $correctedWeightRows
    expectedWeights = $expectedWeightRows
    shaderContract = $shaderContract
}

$directory = Split-Path -Parent $OutputPath
if ($directory) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
$report

if ($Strict -and -not $passed) {
    throw "Directional shadow tent math health check failed. See $OutputPath"
}
