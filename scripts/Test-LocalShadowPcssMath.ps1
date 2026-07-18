[CmdletBinding()]
param(
    [string]$OutputPath = "tmp\local_shadow_pcss_math.json",
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-GlslFunction {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Signature
    )

    $start = $Source.IndexOf($Signature, [StringComparison]::Ordinal)
    if ($start -lt 0) {
        throw "Missing GLSL function: $Signature"
    }
    $brace = $Source.IndexOf('{', $start)
    if ($brace -lt 0) {
        throw "Missing GLSL function body: $Signature"
    }
    $depth = 0
    for ($index = $brace; $index -lt $Source.Length; ++$index) {
        if ($Source[$index] -eq '{') {
            ++$depth
        } elseif ($Source[$index] -eq '}') {
            --$depth
            if ($depth -eq 0) {
                $functionText = $Source.Substring($start, $index - $start + 1)
                return $functionText.Replace("`r`n", "`n")
            }
        }
    }
    throw "Unterminated GLSL function: $Signature"
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Text)

    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return [BitConverter]::ToString($algorithm.ComputeHash($bytes)).
            Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function PerspectiveDepth {
    param([double]$Distance, [double]$Near, [double]$Far)
    return $Far * ($Distance - $Near) / ($Distance * ($Far - $Near))
}

function LinearizeDepth {
    param([double]$Depth, [double]$Near, [double]$Far)
    return $Near * $Far / ($Far - $Depth * ($Far - $Near))
}

$mathCases = @(
    [pscustomobject]@{ receiver = 2.0; blocker = 1.8; source = 0.05 },
    [pscustomobject]@{ receiver = 6.0; blocker = 4.0; source = 0.05 },
    [pscustomobject]@{ receiver = 12.0; blocker = 4.0; source = 0.05 },
    [pscustomobject]@{ receiver = 12.0; blocker = 4.0; source = 0.15 }
)
$nearPlane = 0.05
$farPlane = 24.0
$tileSize = 1024.0
$tanHalfFov = 1.0
$baseRadius = 1.8
$maxPenumbra = 8.0

$mathRows = foreach ($case in $mathCases) {
    $receiverDepth = PerspectiveDepth $case.receiver $nearPlane $farPlane
    $blockerDepth = PerspectiveDepth $case.blocker $nearPlane $farPlane
    $receiverDistance = LinearizeDepth $receiverDepth $nearPlane $farPlane
    $blockerDistance = LinearizeDepth $blockerDepth $nearPlane $farPlane
    $worldTexel = 2.0 * $receiverDistance * $tanHalfFov / $tileSize
    $separation = [Math]::Max($receiverDistance - $blockerDistance, 0.0)
    $penumbraWorld = $case.source * $separation / $blockerDistance
    $filterRadius = [Math]::Min(
        [Math]::Max($baseRadius + $penumbraWorld / $worldTexel, $baseRadius),
        $maxPenumbra
    )
    [pscustomobject]@{
        receiver = $case.receiver
        blocker = $case.blocker
        sourceRadius = $case.source
        receiverRoundTripError = [Math]::Abs($receiverDistance - $case.receiver)
        blockerRoundTripError = [Math]::Abs($blockerDistance - $case.blocker)
        worldTexel = $worldTexel
        penumbraWorld = $penumbraWorld
        filterRadiusTexels = $filterRadius
        finite = -not [double]::IsNaN($filterRadius) -and
            -not [double]::IsInfinity($filterRadius)
        bounded = $filterRadius -ge $baseRadius -and
            $filterRadius -le $maxPenumbra
    }
}
$mathPass = @($mathRows | Where-Object {
    -not $_.finite -or -not $_.bounded -or
    $_.receiverRoundTripError -gt 0.000001 -or
    $_.blockerRoundTripError -gt 0.000001
}).Count -eq 0 -and
    $mathRows[2].filterRadiusTexels -gt $mathRows[1].filterRadiusTexels -and
    $mathRows[3].filterRadiusTexels -ge $mathRows[2].filterRadiusTexels

$shaderPaths = @(
    "assets/shaders/forward_3d.frag",
    "assets/shaders/deferred_lighting.frag",
    "assets/shaders/weighted_translucency_3d.frag"
)
$functionSignatures = @(
    "bool GetLocalShadowTileRange(",
    "bool FindLocalShadowTile(",
    "vec4 LocalShadowFilterControls(",
    "float LocalShadowPcssStrength(",
    "vec4 LocalShadowPcssControls(",
    "float LinearizeLocalShadowDepth(",
    "float LocalShadowStableDiskAngle(",
    "mat2 LocalShadowDiskRotation(",
    "vec2 LocalShadowDiskOffset(",
    "float RectAreaShadowSoftness(",
    "float SampleLocalShadowTileVisibility(",
    "float LocalShadowVisibility("
)

$shaderSources = @{}
$shaderContracts = foreach ($path in $shaderPaths) {
    $source = Get-Content -Raw -LiteralPath $path
    $shaderSources[$path] = $source
    $functionHashText = ($functionSignatures | ForEach-Object {
        Get-GlslFunction -Source $source -Signature $_
    }) -join "`n"
    [pscustomobject]@{
        path = $path
        functionHash = Get-Sha256 $functionHashText
        comparisonBinding = $source.Contains(
            "layout(set = 1, binding = 12) uniform sampler2DShadow localShadowComparisonSampler;"
        )
        rawDepthBinding = $source.Contains(
            "layout(set = 1, binding = 14) uniform sampler2D localShadowRawDepthSampler;"
        )
        hardwareComparison = $source.Contains(
            "localShadowComparisonSampler,"
        ) -and $source.Contains("vec3(sampleUv, comparisonDepth)")
        rawBlockerSearch = $source -match
            'textureLod\s*\(\s*localShadowRawDepthSampler'
        worldSpacePenumbra = $source.Contains(
            "float penumbraWorld = sourceRadius * blockerSeparationWorld"
        ) -and $source.Contains("penumbraWorld * clamp(pcssStrength")
        stablePoisson = $source.Contains(
            "const vec2 LOCAL_SHADOW_POISSON_DISK[16]"
        ) -and $source.Contains("LocalShadowStableDiskAngle(tile.tileInfo)")
        boundedLoops = ([regex]::Matches(
            $source,
            '\[\[dont_unroll\]\] for \(int sampleIndex'
        ).Count -ge 2)
        tileRangeContract = $source.Contains("uvec4 tileRanges[64];") -and
            $source.Contains("localShadows.atlasInfo2.w < 3u") -and
            $source.Contains("MAX_LOCAL_SHADOW_TILES_PER_LIGHT = 6")
    }
}
$functionHashes = @($shaderContracts.functionHash | Select-Object -Unique)
$shaderPass = $functionHashes.Count -eq 1 -and
    @($shaderContracts | Where-Object {
        -not $_.comparisonBinding -or -not $_.rawDepthBinding -or
        -not $_.hardwareComparison -or -not $_.rawBlockerSearch -or
        -not $_.worldSpacePenumbra -or -not $_.stablePoisson -or
        -not $_.boundedLoops -or -not $_.tileRangeContract
    }).Count -eq 0

$settingsSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/shadow_settings.h"
$rendererSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/renderer.cpp"
$bufferSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/uniform_buffer.h"
$layoutSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/descriptor_set_layout.h"
$descriptorSource = Get-Content -Raw -LiteralPath "src/renderer/vulkan/descriptor_sets.cpp"
$sceneSource = Get-Content -Raw -LiteralPath "src/scene/scene_3d.h"
$bridgeSource = Get-Content -Raw -LiteralPath "src/forward_3d.cpp"
$cmakeSource = Get-Content -Raw -LiteralPath "CMakeLists.txt"
$spirvScript = Get-Content -Raw -LiteralPath "scripts/Set-SpirvShadowFunctionControl.ps1"
$cpuContract = [pscustomobject]@{
    ultraDefault = $settingsSource.Contains(
        "VulkanShadowQuality quality = VulkanShadowQuality::Ultra;"
    )
    tierSamples = $settingsSource.Contains(
        "settings.localPcssBlockerSampleCount = 0u;"
    ) -and $settingsSource.Contains(
        "settings.localPcssFilterSampleCount = 4u;"
    ) -and $settingsSource.Contains(
        "settings.localPcssBlockerSampleCount = 12u;"
    ) -and $settingsSource.Contains(
        "settings.localPcssFilterSampleCount = 16u;"
    )
    environmentIsolation = $rendererSource.Contains(
        'EnvironmentFlagOverride("SE_LOCAL_SHADOW_PRODUCTION_FILTER")'
    ) -and $rendererSource.Contains(
        'EnvironmentFloatOverride("SE_LOCAL_SHADOW_PCSS_FILTER_SAMPLES")'
    )
    contractVersion = $rendererSource.Contains(
        "kLocalShadowFilterContractVersion = 3u"
    )
    tileRangesUploaded = $bufferSource.Contains("tileRanges") -and
        $rendererSource.Contains("firstAssignedTileByLocalLight")
    descriptorBudget = $layoutSource.Contains(
        "kMaterialDescriptorCombinedImageSamplerCount = 17"
    ) -and $descriptorSource.Contains(
        "count * kMaterialDescriptorCombinedImageSamplerCount"
    )
    authoredSourceRadius = $sceneSource.Contains("f32 sourceRadius = 0.05f") -and
        $bridgeSource.Contains("PositiveOrFallback(light.sourceRadius, 0.05f)")
    spirvFunctionControl = $cmakeSource.Contains(
        "Set-SpirvShadowFunctionControl.ps1"
    ) -and $spirvScript.Contains("DontInline")
}
$cpuPass = @($cpuContract.PSObject.Properties | Where-Object {
    -not [bool]$_.Value
}).Count -eq 0

$passed = $mathPass -and $shaderPass -and $cpuPass
$report = [pscustomobject]@{
    verdict = if ($passed) { "pass" } else { "fail" }
    algorithm = "world-space local-light PCSS with stable Poisson hardware comparison filtering"
    mathPass = $mathPass
    shaderPass = $shaderPass
    cpuPass = $cpuPass
    mathRows = $mathRows
    shaderContracts = $shaderContracts
    cpuContract = $cpuContract
}

$directory = Split-Path -Parent $OutputPath
if ($directory) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
$report | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $OutputPath -Encoding utf8
$report

if ($Strict -and -not $passed) {
    throw "Local shadow PCSS math health check failed. See $OutputPath"
}
