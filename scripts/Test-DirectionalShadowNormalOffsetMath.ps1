[CmdletBinding()]
param(
    [string]$OutputPath = "tmp\directional_shadow_normal_offset_math.json",
    [switch]$Strict
)

$ErrorActionPreference = "Stop"

$normalOffsetTexels = 2.0
$slopeOffsetTexels = 0.5
$texelWorldSizes = @(0.01, 0.02, 0.05)
$cosines = @(0.0, 0.25, 0.5, 0.75, 1.0)
$rows = @()
$mathPass = $true

foreach ($texelWorldSize in $texelWorldSizes) {
    $previousOffset = [double]::PositiveInfinity
    $previousSlopeOffset = [double]::PositiveInfinity
    foreach ($cosAlpha in $cosines) {
        $sinAlpha = [Math]::Sqrt([Math]::Max(1.0 - $cosAlpha * $cosAlpha, 0.0))
        $offset = $normalOffsetTexels * $texelWorldSize * $sinAlpha
        $tanAlpha = $sinAlpha / [Math]::Max($cosAlpha, 0.0001)
        $slopeOffset = $slopeOffsetTexels * $texelWorldSize *
            [Math]::Min($tanAlpha, 2.0)
        $bounded = $offset -ge -0.0000001 -and
            $offset -le $normalOffsetTexels * $texelWorldSize + 0.0000001
        $slopeBounded = $slopeOffset -ge -0.0000001 -and
            $slopeOffset -le $slopeOffsetTexels * $texelWorldSize * 2.0 + 0.0000001
        $monotonic = $offset -le $previousOffset + 0.0000001
        $slopeMonotonic = $slopeOffset -le $previousSlopeOffset + 0.0000001
        $finite = -not [double]::IsNaN($offset) -and
            -not [double]::IsInfinity($offset) -and
            -not [double]::IsNaN($slopeOffset) -and
            -not [double]::IsInfinity($slopeOffset)
        $casePass = $bounded -and $slopeBounded -and $monotonic -and
            $slopeMonotonic -and $finite
        $mathPass = $mathPass -and $casePass
        $rows += [pscustomobject]@{
            texelWorldSize = $texelWorldSize
            cosAlpha = $cosAlpha
            sinAlpha = $sinAlpha
            offsetWorld = $offset
            slopeOffsetWorld = $slopeOffset
            bounded = $bounded
            slopeBounded = $slopeBounded
            monotonic = $monotonic
            slopeMonotonic = $slopeMonotonic
            finite = $finite
            pass = $casePass
        }
        $previousOffset = $offset
        $previousSlopeOffset = $slopeOffset
    }
}

$zeroAtNormalIncidence = [Math]::Abs(
    @($rows | Where-Object { $_.cosAlpha -eq 1.0 })[0].offsetWorld
) -le 0.0000001
$configuredScaleAtGrazing = @(
    $rows | Where-Object { $_.cosAlpha -eq 0.0 } | ForEach-Object {
        [Math]::Abs(
            $_.offsetWorld - $normalOffsetTexels * $_.texelWorldSize
        ) -le 0.0000001
    }
) -notcontains $false
$disabledOffset = 0.0 * $texelWorldSizes[0] *
    [Math]::Sqrt(1.0 - $cosines[2] * $cosines[2])
$disabledIsIdentity = [Math]::Abs($disabledOffset) -le 0.0000001
$disabledSlopeOffset = 0.0 * $texelWorldSizes[0] *
    [Math]::Min(
        [Math]::Sqrt(1.0 - $cosines[2] * $cosines[2]) / $cosines[2],
        2.0
    )
$disabledSlopeIsIdentity = [Math]::Abs($disabledSlopeOffset) -le 0.0000001
$mathPass = $mathPass -and $zeroAtNormalIncidence -and
    $configuredScaleAtGrazing -and $disabledIsIdentity -and
    $disabledSlopeIsIdentity

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
        readsNormalOffsetControl = $source.Contains(
            "shadowCascades.receiverPlaneBiasControls.y"
        )
        scalesByCascadeWorldTexel = $source.Contains(
            "shadowCascades.texelWorldSizes[cascadeIndex]"
        )
        usesBoundedSinAngle = $source.Contains(
            "float sinAlpha = sqrt(max(1.0 - cosAlpha * cosAlpha, 0.0));"
        )
        usesBoundedSlopeOffset = $source.Contains(
            "float tanAlpha = sinAlpha / max(cosAlpha, 0.0001);"
        ) -and $source.Contains(
            "shadowCascades.receiverPlaneBiasControls.z"
        ) -and $source.Contains(
            "min(tanAlpha, 2.0)"
        )
        controlsAreIndependent = $source.Contains(
            "normalOffsetTexels <= 0.0001 && slopeOffsetTexels <= 0.0001"
        )
        projectsOffsetReceiver = $source.Contains(
            "vec3 shadowReceiverPosition = DirectionalShadowReceiverPosition("
        ) -and $source.Contains(
            "ProjectShadowCascade(`n            shadowReceiverPosition,"
        )
    }
}
$shaderPass = @($shaderContract | Where-Object {
    -not $_.readsNormalOffsetControl -or
    -not $_.scalesByCascadeWorldTexel -or
    -not $_.usesBoundedSinAngle -or
    -not $_.usesBoundedSlopeOffset -or
    -not $_.controlsAreIndependent -or
    -not $_.projectsOffsetReceiver
}).Count -eq 0

$settingsSource = Get-Content -LiteralPath "src/renderer/vulkan/shadow_settings.h" -Raw
$rendererSource = Get-Content -LiteralPath "src/renderer/vulkan/renderer.cpp" -Raw
$bufferSource = Get-Content -LiteralPath "src/renderer/vulkan/uniform_buffer.h" -Raw
$cpuContract = [pscustomobject]@{
    settingExists = $settingsSource.Contains(
        "f32 directionalNormalOffsetBiasTexels = 2.0f;"
    )
    environmentControlExists = $rendererSource.Contains(
        'EnvironmentFloatOverride("SE_DIRECTIONAL_NORMAL_OFFSET_BIAS_TEXELS")'
    )
    slopeOffsetSettingExists = $settingsSource.Contains(
        "f32 directionalSlopeOffsetBiasTexels = 0.5f;"
    )
    slopeOffsetEnvironmentControlExists = $rendererSource.Contains(
        'EnvironmentFloatOverride("SE_DIRECTIONAL_SLOPE_OFFSET_BIAS_TEXELS")'
    )
    packedIntoShaderBuffer = $rendererSource.Contains(
        "m_ShadowSettings.directionalNormalOffsetBiasTexels, 0.0f, 4.0f"
    )
    bufferContractDocumentsTexels = $bufferSource.Contains(
        "y: normal offset in cascade texels"
    )
    bufferContractDocumentsSlopeOffset = $bufferSource.Contains(
        "z: bounded light-direction slope offset in cascade texels"
    )
    fourthCascadeTexelPreserved = -not $rendererSource.Contains(
        "cascadeData.texelWorldSizes.w = cascades.maxDistance;"
    )
}
$cpuPass = -not (@($cpuContract.PSObject.Properties | Where-Object {
    -not [bool]$_.Value
}).Count -gt 0)

$passed = $mathPass -and $shaderPass -and $cpuPass
$report = [pscustomobject]@{
    verdict = if ($passed) { "pass" } else { "fail" }
    algorithm = "cascade world-texel normal and bounded slope receiver offsets"
    normalOffsetTexels = $normalOffsetTexels
    slopeOffsetTexels = $slopeOffsetTexels
    math = [pscustomobject]@{
        pass = $mathPass
        zeroAtNormalIncidence = $zeroAtNormalIncidence
        configuredScaleAtGrazing = $configuredScaleAtGrazing
        disabledIsIdentity = $disabledIsIdentity
        disabledSlopeIsIdentity = $disabledSlopeIsIdentity
        rows = $rows
    }
    shaderContract = $shaderContract
    cpuContract = $cpuContract
}

$directory = Split-Path -Parent $OutputPath
if ($directory) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
$report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $OutputPath -Encoding utf8
$report

if ($Strict -and -not $passed) {
    throw "Directional shadow normal-offset math health check failed. See $OutputPath"
}
