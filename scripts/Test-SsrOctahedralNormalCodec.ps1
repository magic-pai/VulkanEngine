[CmdletBinding()]
param(
    [switch]$Strict
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function New-Check {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Actual,
        [string]$Expected
    )

    return [pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }
}

function Normalize-Vector3 {
    param([System.Numerics.Vector3]$Value)

    return [System.Numerics.Vector3]::Normalize($Value)
}

function Get-SignNotZero {
    param([double]$Value)

    return $(if ($Value -ge 0.0) { 1.0 } else { -1.0 })
}

function Encode-OctahedralNormal {
    param(
        [System.Numerics.Vector3]$Value,
        [switch]$LegacyZeroSign
    )

    $normal = Normalize-Vector3 $Value
    $l1 = [Math]::Abs($normal.X) +
        [Math]::Abs($normal.Y) +
        [Math]::Abs($normal.Z)
    $x = $normal.X / $l1
    $y = $normal.Y / $l1
    $z = $normal.Z / $l1
    if ($z -lt 0.0) {
        $sourceX = $x
        $sourceY = $y
        $signX = if ($LegacyZeroSign -and $sourceX -eq 0.0) {
            0.0
        } else {
            Get-SignNotZero $sourceX
        }
        $signY = if ($LegacyZeroSign -and $sourceY -eq 0.0) {
            0.0
        } else {
            Get-SignNotZero $sourceY
        }
        $x = (1.0 - [Math]::Abs($sourceY)) * $signX
        $y = (1.0 - [Math]::Abs($sourceX)) * $signY
    }
    return [System.Numerics.Vector2]::new([single]$x, [single]$y)
}

function Decode-OctahedralNormal {
    param(
        [System.Numerics.Vector2]$Encoded,
        [switch]$LegacyZeroSign
    )

    $x = $Encoded.X
    $y = $Encoded.Y
    $z = 1.0 - [Math]::Abs($x) - [Math]::Abs($y)
    if ($z -lt 0.0) {
        $sourceX = $x
        $sourceY = $y
        $signX = if ($LegacyZeroSign -and $sourceX -eq 0.0) {
            0.0
        } else {
            Get-SignNotZero $sourceX
        }
        $signY = if ($LegacyZeroSign -and $sourceY -eq 0.0) {
            0.0
        } else {
            Get-SignNotZero $sourceY
        }
        $x = (1.0 - [Math]::Abs($sourceY)) * $signX
        $y = (1.0 - [Math]::Abs($sourceX)) * $signY
    }
    return Normalize-Vector3 ([System.Numerics.Vector3]::new(
        [single]$x,
        [single]$y,
        [single]$z
    ))
}

function Get-VectorDot {
    param(
        [System.Numerics.Vector3]$Left,
        [System.Numerics.Vector3]$Right
    )

    return [System.Numerics.Vector3]::Dot($Left, $Right)
}

$deferredShader = Get-Content -Raw -LiteralPath (
    Join-Path $projectRoot "assets\shaders\deferred_lighting.frag"
)
$temporalShader = Get-Content -Raw -LiteralPath (
    Join-Path $projectRoot "assets\shaders\ssr_temporal.comp"
)

$checks = @()
$checks += New-Check `
    "Deferred SSR oct codec uses sign-not-zero" `
    ($deferredShader -match "vec2\s+SsrOctSignNotZero" -and
        $deferredShader -match "SsrOctSignNotZero\(encoded\.xy\)" -and
        $deferredShader -match "SsrOctSignNotZero\(value\.xy\)") `
    "helper/encode/decode" `
    "present/present/present"
$checks += New-Check `
    "Temporal SSR oct codec uses sign-not-zero" `
    ($temporalShader -match "vec2\s+OctSignNotZero" -and
        $temporalShader -match "OctSignNotZero\(encoded\.xy\)" -and
        $temporalShader -match "OctSignNotZero\(value\.xy\)") `
    "helper/encode/decode" `
    "present/present/present"
$checks += New-Check `
    "SSR oct codecs do not fold with GLSL sign zero" `
    ($deferredShader -notmatch "\*\s*sign\((encoded|value)\.xy\)" -and
        $temporalShader -notmatch "\*\s*sign\((encoded|value)\.xy\)") `
    "legacy fold absent" `
    "true"
$checks += New-Check `
    "Deferred SSR reconstructs validated 2x2 receiver history" `
    ($deferredShader -match "SsrDeferredReceiverValidatedHistory" -and
        $deferredShader -match "texelFetch\(\s*ssrHistoryMetadata" -and
        $deferredShader -match "texelFetch\(\s*ssrResolvedReflection" -and
        $deferredShader -match "validWeightSum") `
    "validatedHistory/metadataFetch/radianceFetch/weight" `
    "present/present/present/present"

$testNormals = @(
    [System.Numerics.Vector3]::new(0.0, 0.6, -0.8)
    [System.Numerics.Vector3]::new(0.0, -0.6, -0.8)
    [System.Numerics.Vector3]::new(0.6, 0.0, -0.8)
    [System.Numerics.Vector3]::new(-0.6, 0.0, -0.8)
    [System.Numerics.Vector3]::new(0.0, 0.0, -1.0)
    [System.Numerics.Vector3]::new(1.0, 0.0, 0.0)
    [System.Numerics.Vector3]::new(0.0, 1.0, 0.0)
    [System.Numerics.Vector3]::new(0.3, 0.4, 0.8660254038)
    [System.Numerics.Vector3]::new(-0.3, 0.4, -0.8660254038)
)

$minimumRoundTripDot = 1.0
$minimumLegacyAxisDot = 1.0
foreach ($testNormal in $testNormals) {
    $normal = Normalize-Vector3 $testNormal
    $encoded = Encode-OctahedralNormal $normal
    $decoded = Decode-OctahedralNormal $encoded
    $minimumRoundTripDot = [Math]::Min(
        $minimumRoundTripDot,
        (Get-VectorDot $normal $decoded)
    )

    $legacyEncoded = Encode-OctahedralNormal $normal -LegacyZeroSign
    $legacyDecoded = Decode-OctahedralNormal $legacyEncoded -LegacyZeroSign
    $minimumLegacyAxisDot = [Math]::Min(
        $minimumLegacyAxisDot,
        (Get-VectorDot $normal $legacyDecoded)
    )
}

$checks += New-Check `
    "Sign-not-zero oct codec round-trips axis and lower-hemisphere normals" `
    ($minimumRoundTripDot -gt 0.999999) `
    ([string]::Format(
        [System.Globalization.CultureInfo]::InvariantCulture,
        "minimumDot={0:F9}",
        $minimumRoundTripDot
    )) `
    "minimumDot>0.999999"
$checks += New-Check `
    "Regression corpus detects the legacy zero-sign defect" `
    ($minimumLegacyAxisDot -lt 0.0) `
    ([string]::Format(
        [System.Globalization.CultureInfo]::InvariantCulture,
        "minimumLegacyDot={0:F9}",
        $minimumLegacyAxisDot
    )) `
    "minimumLegacyDot<0"

$passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
$failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
$result = [pscustomobject]@{
    verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
    passCount = $passCount
    failCount = $failCount
    minimumRoundTripDot = $minimumRoundTripDot
    minimumLegacyAxisDot = $minimumLegacyAxisDot
    checks = $checks
}
$result

if ($Strict -and $failCount -gt 0) {
    throw "SSR octahedral normal codec health failed: $failCount checks"
}
