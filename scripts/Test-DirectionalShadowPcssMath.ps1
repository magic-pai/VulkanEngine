[CmdletBinding()]
param(
    [string]$OutputPath = "tmp\directional_shadow_pcss_math.json",
    [switch]$Strict
)

$ErrorActionPreference = "Stop"

$angularRadius = 0.00464258
$strength = 1.0
$baseRadiusTexels = 2.0
$maxPenumbraTexels = 8.0
$cases = @(
    [pscustomobject]@{ depthSpan = 40.0; texelWorld = 0.01; depthDelta = 0.00025 },
    [pscustomobject]@{ depthSpan = 80.0; texelWorld = 0.02; depthDelta = 0.000125 },
    [pscustomobject]@{ depthSpan = 160.0; texelWorld = 0.04; depthDelta = 0.0000625 },
    [pscustomobject]@{ depthSpan = 80.0; texelWorld = 0.02; depthDelta = 0.0 },
    [pscustomobject]@{ depthSpan = 400.0; texelWorld = 0.01; depthDelta = 0.10 }
)

$rows = foreach ($case in $cases) {
    $separationWorld = [Math]::Max($case.depthDelta, 0.0) * $case.depthSpan
    $penumbraWorld = $separationWorld * [Math]::Tan($angularRadius) * $strength
    $unboundedRadius = $baseRadiusTexels + $penumbraWorld / $case.texelWorld
    $filterRadius = [Math]::Min(
        [Math]::Max($unboundedRadius, $baseRadiusTexels),
        $maxPenumbraTexels
    )
    $finite = -not [double]::IsNaN($filterRadius) -and
        -not [double]::IsInfinity($filterRadius)
    [pscustomobject]@{
        depthSpan = $case.depthSpan
        texelWorld = $case.texelWorld
        depthDelta = $case.depthDelta
        separationWorld = $separationWorld
        penumbraWorld = $penumbraWorld
        filterRadiusTexels = $filterRadius
        finite = $finite
        bounded = $filterRadius -ge $baseRadiusTexels -and
            $filterRadius -le $maxPenumbraTexels
    }
}

$crossCascadeRadii = @($rows | Select-Object -First 3 | ForEach-Object {
    [double]$_.filterRadiusTexels
})
$crossCascadeWorldPenumbrae = @($rows | Select-Object -First 3 | ForEach-Object {
    ([double]$_.filterRadiusTexels - $baseRadiusTexels) * [double]$_.texelWorld
})
$crossCascadeWorldInvariant =
    (($crossCascadeWorldPenumbrae | Measure-Object -Maximum).Maximum -
        ($crossCascadeWorldPenumbrae | Measure-Object -Minimum).Minimum) -le 0.0000001
$contactPreservesTentBaseline = [Math]::Abs(
    [double]$rows[3].filterRadiusTexels - $baseRadiusTexels
) -le 0.0000001
$largeSeparationIsBounded = [Math]::Abs(
    [double]$rows[4].filterRadiusTexels - $maxPenumbraTexels
) -le 0.0000001
$mathPass = @($rows | Where-Object { -not $_.finite -or -not $_.bounded }).Count -eq 0 -and
    $crossCascadeWorldInvariant -and $contactPreservesTentBaseline -and
    $largeSeparationIsBounded

$shaderPaths = @(
    "assets/shaders/forward_3d.frag",
    "assets/shaders/deferred_lighting.frag",
    "assets/shaders/gbuffer_debug.frag",
    "assets/shaders/weighted_translucency_3d.frag"
)
$shaderContract = foreach ($path in $shaderPaths) {
    $source = Get-Content -Raw -LiteralPath $path
    [pscustomobject]@{
        path = $path
        rawDepthBinding = $source.Contains(
            "layout(set = 1, binding = 13) uniform sampler2D shadowRawDepthSampler;"
        )
        worldDepthSpan = $source.Contains(
            "shadowCascades.lightDepthWorldSpans[cascadeIndex]"
        )
        worldTexelScale = $source.Contains(
            "shadowCascades.texelWorldSizes[cascadeIndex]"
        )
        blockerSearch = $source.Contains(
            "textureLod(shadowRawDepthSampler, sampleUv, 0.0).r"
        )
        comparisonFilter = $source.Contains(
            "vec3(sampleUv, sampleComparisonDepth)"
        )
        boundedPenumbra = $source.Contains(
            "maxPenumbraTexels"
        ) -and $source.Contains(
            "penumbraWorld / texelWorldSize"
        )
        stablePattern = $source.Contains(
            "float(cascadeIndex) * 2.39996323"
        )
        tentFallbackBlend = $source.Contains(
            "mix(filteredVisibility, pcssVisibility, pcssBlend)"
        )
    }
}
$shaderPass = @($shaderContract | Where-Object {
    -not $_.rawDepthBinding -or -not $_.worldDepthSpan -or
    -not $_.worldTexelScale -or -not $_.blockerSearch -or
    -not $_.comparisonFilter -or -not $_.boundedPenumbra -or
    -not $_.stablePattern -or -not $_.tentFallbackBlend
}).Count -eq 0

$settingsSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/shadow_settings.h"
$forwardSource = Get-Content -Raw -LiteralPath "src/forward_3d.cpp"
$bufferSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/uniform_buffer.h"
$rendererSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/renderer.cpp"
$atlasSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/shadow_cascade_atlas.cpp"
$shadowMapSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/shadow_map.cpp"
$layoutSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/descriptor_set_layout.cpp"
$descriptorSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/descriptor_sets.cpp"
$sceneSource = Get-Content -Raw -LiteralPath "src/scene/scene_3d.h"
$cpuContract = [pscustomobject]@{
    ultraDefault = $settingsSource.Contains(
        "VulkanShadowQuality quality = VulkanShadowQuality::Ultra;"
    )
    ultraPcssEnabled = $settingsSource.Contains("settings.pcssStrength = 1.0f;")
    sampleBudgets = $settingsSource.Contains(
        "directionalPcssBlockerSampleCount = 12"
    ) -and $settingsSource.Contains(
        "directionalPcssFilterSampleCount = 16"
    )
    worldDepthBuffer = $bufferSource.Contains("lightDepthWorldSpans")
    pcssControlBuffer = $bufferSource.Contains("directionalPcssControls") -and
        $bufferSource.Contains("directionalPcssGeometry")
    depthSpanProduced = $rendererSource.Contains(
        "*lightDepthWorldSpan = farPlane - nearPlane;"
    )
    sceneAngularRadius = $sceneSource.Contains(
        "kDefaultDirectionalLightAngularRadiusRadians"
    )
    cascadeRawSampler = $atlasSource.Contains("compareEnable = VK_FALSE") -and
        $atlasSource.Contains("m_RawDepthSampler")
    fallbackRawSampler = $shadowMapSource.Contains("compareEnable = VK_FALSE") -and
        $shadowMapSource.Contains("m_RawDepthSampler")
    descriptorBinding = $layoutSource.Contains(
        "shadowRawDepthSamplerLayoutBinding.binding = 13;"
    ) -and $descriptorSource.Contains("shadowRawDepthImageInfo")
    environmentControls = $rendererSource.Contains(
        'EnvironmentFloatOverride("SE_DIRECTIONAL_PCSS_BLOCKER_SAMPLES")'
    ) -and $rendererSource.Contains(
        'EnvironmentFloatOverride("SE_DIRECTIONAL_PCSS_FILTER_SAMPLES")'
    )
    forwardProductionProfileControls = $forwardSource.Contains(
        '"SE_SHADOW_PCSS_STRENGTH"'
    ) -and $forwardSource.Contains(
        '"SE_DIRECTIONAL_PCSS_OFF"'
    ) -and $forwardSource.Contains(
        '"SE_DIRECTIONAL_PCSS_BLOCKER_SAMPLES"'
    ) -and $forwardSource.Contains(
        '"SE_DIRECTIONAL_PCSS_FILTER_SAMPLES"'
    ) -and $forwardSource.Contains(
        '"SE_DIRECTIONAL_PCSS_SEARCH_RADIUS_TEXELS"'
    ) -and $forwardSource.Contains(
        '"SE_DIRECTIONAL_PCSS_MAX_PENUMBRA_TEXELS"'
    )
}
$cpuPass = @($cpuContract.PSObject.Properties | Where-Object {
    -not [bool]$_.Value
}).Count -eq 0

$passed = $mathPass -and $shaderPass -and $cpuPass
$report = [pscustomobject]@{
    verdict = if ($passed) { "pass" } else { "fail" }
    algorithm = "world-space directional blocker-search PCSS with stable Poisson filtering"
    angularRadiusRadians = $angularRadius
    math = [pscustomobject]@{
        pass = $mathPass
        crossCascadeWorldInvariant = $crossCascadeWorldInvariant
        contactPreservesTentBaseline = $contactPreservesTentBaseline
        largeSeparationIsBounded = $largeSeparationIsBounded
        rows = $rows
    }
    shaderContract = $shaderContract
    cpuContract = $cpuContract
}

$directory = Split-Path -Parent $OutputPath
if ($directory) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
$report

if ($Strict -and -not $passed) {
    throw "Directional shadow PCSS math health check failed. See $OutputPath"
}
