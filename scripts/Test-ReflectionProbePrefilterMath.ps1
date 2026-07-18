[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$shaderPath = Join-Path $repoRoot "assets\shaders\reflection_probe_prefilter.comp"
$irradianceShaderPath = Join-Path $repoRoot "assets\shaders\reflection_probe_diffuse_irradiance.comp"
$resourcePath = Join-Path $repoRoot "src\renderer\vulkan\reflection_probe_resources.cpp"
$headerPath = Join-Path $repoRoot "src\renderer\vulkan\reflection_probe_resources.h"

$shader = Get-Content -Raw -LiteralPath $shaderPath
$irradianceShader = Get-Content -Raw -LiteralPath $irradianceShaderPath
$resource = Get-Content -Raw -LiteralPath $resourcePath
$header = Get-Content -Raw -LiteralPath $headerPath
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

Add-Check "captured radiance and GGX output use separate images" `
    ($header -match "sourceRadianceImage") `
    ($header -match "sourceRadianceImage") "sourceRadianceImage resource"
Add-Check "prefilter source cube exposes its full mip chain" `
    ($resource -match "CreateGpuCapturedSceneCubeView" -and
        $resource -match "sourceRadianceImage->MipLevels\(\)") `
    "full source view=$($resource -match 'sourceRadianceImage->MipLevels\(\)')" `
    "full source mip count"
Add-Check "GGX shader receives source dimensions" `
    ($shader -match "sourceFaceSize" -and $shader -match "sourceMipCount") `
    "faceSize=$($shader -match 'sourceFaceSize'),mips=$($shader -match 'sourceMipCount')" `
    "faceSize/mips=true/true"
Add-Check "GGX shader computes a PDF-derived source LOD" `
    ($shader -match "sampleSolidAngle" -and
        $shader -match "texelSolidAngle" -and
        $shader -match "sourceLod") `
    "sample/pdf/LOD symbols present" "sampleSolidAngle/texelSolidAngle/sourceLod"
Add-Check "GGX shader no longer fixes radiance samples to mip zero" `
    ($shader -notmatch "textureLod\(sourceCubemap,\s*light,\s*0\.0\)") `
    ($shader -match "textureLod\(sourceCubemap,\s*light,\s*0\.0\)") `
    "false"
Add-Check "diffuse convolution uses the filtered source mip chain" `
    ($irradianceShader -match "sourceFaceSize" -and
        $irradianceShader -match "sourceMipCount" -and
        $irradianceShader -match "sourceLod") `
    "source LOD contract present=$($irradianceShader -match 'sourceLod')" "true"

function Get-PrefilterSourceLod {
    param(
        [double]$Roughness,
        [double]$NdotH,
        [double]$HdotV,
        [int]$SampleCount = 64,
        [int]$FaceSize = 256,
        [int]$MipCount = 9
    )

    if ($Roughness -le 0.0) {
        return 0.0
    }
    $alpha = $Roughness * $Roughness
    $alphaSquared = $alpha * $alpha
    $denominator = [Math]::PI * [Math]::Pow(
        [Math]::Max($NdotH * $NdotH * ($alphaSquared - 1.0) + 1.0, 1.0e-4),
        2.0
    )
    $distribution = $alphaSquared / [Math]::Max($denominator, 1.0e-6)
    $pdf = [Math]::Max($distribution * $NdotH / [Math]::Max(4.0 * $HdotV, 1.0e-5), 1.0e-6)
    $sampleSolidAngle = 1.0 / ($SampleCount * $pdf)
    $texelSolidAngle = 4.0 * [Math]::PI / (6.0 * $FaceSize * $FaceSize)
    return [Math]::Min(
        [Math]::Max(0.5 * ([Math]::Log($sampleSolidAngle / $texelSolidAngle) / [Math]::Log(2.0)), 0.0),
        $MipCount - 1.0
    )
}

$lodSamples = @(0.0, 0.15, 0.25, 0.5, 1.0) | ForEach-Object {
    Get-PrefilterSourceLod -Roughness $_ -NdotH 1.0 -HdotV 1.0
}
$finite = @($lodSamples | Where-Object { [double]::IsNaN($_) -or [double]::IsInfinity($_) }).Count -eq 0
$bounded = @($lodSamples | Where-Object { $_ -lt 0.0 -or $_ -gt 8.0 }).Count -eq 0
$roughLodIncreases = $lodSamples[2] -ge $lodSamples[1] -and
    $lodSamples[3] -gt $lodSamples[2] -and
    $lodSamples[4] -gt $lodSamples[3]
Add-Check "reference PDF LOD remains finite and bounded" ($finite -and $bounded) `
    ($lodSamples -join ",") "all values in [0,8]"
Add-Check "reference PDF LOD broadens with roughness" $roughLodIncreases `
    ($lodSamples -join ",") "peak-lobe source LOD is nondecreasing with roughness"

$passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
$failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
$summary = [pscustomobject]@{
    verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
    passCount = $passCount
    failCount = $failCount
    checks = @($checks)
}
$checks | Format-Table -AutoSize
$summary

if ($failCount -gt 0) {
    exit 1
}
