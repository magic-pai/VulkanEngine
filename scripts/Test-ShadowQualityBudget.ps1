param(
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$LightingExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [string]$OutputDirectory = "tmp\shadow_quality_budget",
    [int]$WarmupFrames = 5,
    [int]$CaptureFrames = 4,
    [int]$AutoExitFrames = 13,
    [switch]$SkipBuild,
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Get-Number {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value -or "$($property.Value)" -eq "") {
        return $null
    }
    return [double]::Parse("$($property.Value)", [Globalization.CultureInfo]::InvariantCulture)
}

function Add-Check {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[object]]$Checks,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Passed,
        $Actual,
        $Expected,
        [string]$Detail = ""
    )

    $Checks.Add([pscustomobject]@{
        area = $Area
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
        detail = $Detail
    }) | Out-Null
}

function Invoke-WithEnvironment {
    param(
        [Parameter(Mandatory = $true)][string[]]$ManagedKeys,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)][scriptblock]$Script
    )

    $previous = @{}
    foreach ($name in $ManagedKeys) {
        $previous[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
        $value = if ($Environment.ContainsKey($name)) { $Environment[$name] } else { $null }
        [Environment]::SetEnvironmentVariable($name, $value, "Process")
    }
    try {
        & $Script
    } finally {
        foreach ($name in $ManagedKeys) {
            [Environment]::SetEnvironmentVariable($name, $previous[$name], "Process")
        }
    }
}

function Invoke-LightingBenchmark {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$CsvPath,
        [AllowNull()][string]$Quality,
        [Parameter(Mandatory = $true)][int]$Warmup,
        [Parameter(Mandatory = $true)][int]$Capture,
        [Parameter(Mandatory = $true)][int]$AutoExit
    )

    $managedKeys = @(
        "SE_WINDOW_WIDTH", "SE_WINDOW_HEIGHT", "SE_WINDOW_BORDERLESS",
        "SE_VISUAL_QA_HIDE_IMGUI", "SE_HIDE_IMGUI", "SE_BENCHMARK_SCENE",
        "SE_FORCE_LIGHTING_SHOWCASE", "SE_FORWARD3D_SHADOW_PROFILE",
        "SE_SHADOW_QUALITY", "SE_FORWARD3D_AA_MODE", "SE_RENDER_VIEW",
        "SE_ENABLE_GPU_TIMESTAMPS", "SE_BENCHMARK_WARMUP_FRAMES",
        "SE_BENCHMARK_FRAMES", "SE_AUTO_EXIT_FRAMES", "SE_BENCHMARK_CSV",
        "SE_SHADOW_MAP_SIZE", "SE_SHADOW_CASCADE_COUNT",
        "SE_SHADOW_CASCADE_MAX_DISTANCE", "SE_SHADOW_CASCADE_SPLIT_LAMBDA",
        "SE_SHADOW_CASCADE_BLEND", "SE_SHADOW_CASCADE_FADE",
        "SE_SHADOW_BIAS_MIN", "SE_SHADOW_BIAS_SLOPE",
        "SE_SHADOW_PCF_RADIUS", "SE_SHADOW_PCF_KERNEL_RADIUS",
        "SE_SHADOW_PCSS_STRENGTH", "SE_DIRECTIONAL_SHADOW_FILTER_MODE",
        "SE_DIRECTIONAL_SHADOW_FILTER_KERNEL_WIDTH",
        "SE_DIRECTIONAL_SHADOW_FILTER_RECEIVER_BIAS_EXTENT_TEXELS",
        "SE_LOCAL_SHADOW_BIAS_MIN", "SE_LOCAL_SHADOW_BIAS_SLOPE",
        "SE_LOCAL_SHADOW_PCF_RADIUS", "SE_LOCAL_SHADOW_PCF_KERNEL_RADIUS",
        "SE_LOCAL_SHADOW_PCSS_STRENGTH", "SE_LOCAL_SHADOW_FACE_BLEND",
        "SE_LOCAL_SHADOW_POINT_BIAS_MIN", "SE_LOCAL_SHADOW_POINT_BIAS_SLOPE",
        "SE_LOCAL_SHADOW_POINT_PCF_RADIUS", "SE_LOCAL_SHADOW_POINT_PCF_KERNEL_RADIUS",
        "SE_LOCAL_SHADOW_POINT_PCSS_STRENGTH", "SE_LOCAL_SHADOW_SPOT_BIAS_MIN",
        "SE_LOCAL_SHADOW_SPOT_BIAS_SLOPE", "SE_LOCAL_SHADOW_SPOT_PCF_RADIUS",
        "SE_LOCAL_SHADOW_SPOT_PCF_KERNEL_RADIUS", "SE_LOCAL_SHADOW_SPOT_PCSS_STRENGTH",
        "SE_LOCAL_SHADOW_RECT_BIAS_MIN", "SE_LOCAL_SHADOW_RECT_BIAS_SLOPE",
        "SE_LOCAL_SHADOW_RECT_PCF_RADIUS", "SE_LOCAL_SHADOW_RECT_PCF_KERNEL_RADIUS",
        "SE_LOCAL_SHADOW_RECT_PCSS_STRENGTH", "SE_LOCAL_SHADOW_RECT_SAMPLE_TILES",
        "SE_RECT_SHADOW_BIAS_SCALE", "SE_LOCAL_SHADOW_RECT_BIAS_SCALE",
        "SE_POINT_LIGHT_SHADOWS_OFF", "SE_SPOT_LIGHT_SHADOWS_OFF",
        "SE_RECT_LIGHT_SHADOWS_OFF", "SE_LOCAL_SHADOW_POINT_OFF",
        "SE_LOCAL_SHADOW_SPOT_OFF", "SE_LOCAL_SHADOW_RECT_OFF",
        "SE_CONTACT_SHADOW_STRENGTH", "SE_CONTACT_SHADOW_LENGTH",
        "SE_CONTACT_SHADOW_THICKNESS", "SE_CONTACT_SHADOW_STEPS",
        "SE_CONTACT_SHADOW_JITTER_STRENGTH", "SE_CONTACT_SHADOW_EDGE_FADE_PIXELS"
    )
    $environment = @{
        SE_WINDOW_WIDTH = "1280"
        SE_WINDOW_HEIGHT = "720"
        SE_WINDOW_BORDERLESS = "0"
        SE_VISUAL_QA_HIDE_IMGUI = "1"
        SE_HIDE_IMGUI = "1"
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_FORCE_LIGHTING_SHOWCASE = "1"
        SE_FORWARD3D_SHADOW_PROFILE = "production"
        SE_SHADOW_QUALITY = $Quality
        SE_FORWARD3D_AA_MODE = "taa"
        SE_RENDER_VIEW = "lit"
        SE_ENABLE_GPU_TIMESTAMPS = "1"
        SE_BENCHMARK_WARMUP_FRAMES = [string]$Warmup
        SE_BENCHMARK_FRAMES = [string]$Capture
        SE_AUTO_EXIT_FRAMES = [string]$AutoExit
        SE_BENCHMARK_CSV = $CsvPath
    }

    Remove-Item -LiteralPath $CsvPath -ErrorAction SilentlyContinue
    Invoke-WithEnvironment -ManagedKeys $managedKeys -Environment $environment -Script {
        & $Executable | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "SelfEngineLightingShowcase exited with code $LASTEXITCODE"
        }
    }
    if (-not (Test-Path -LiteralPath $CsvPath)) {
        throw "LightingShowcase benchmark CSV was not written: $CsvPath"
    }
    return @(Import-Csv -LiteralPath $CsvPath)
}

$resolvedOutput = Resolve-FullPath -Path $OutputDirectory
New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null
$forwardExecutable = Resolve-FullPath -Path $ForwardExecutablePath
$lightingExecutable = Resolve-FullPath -Path $LightingExecutablePath

if (-not $SkipBuild) {
    $buildCommand =
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cd /d "{0}\build" && MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo && MSBuild SelfEngineLightingShowcase.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo' -f $repoRoot
    cmd /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Shadow quality budget build failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path -LiteralPath $forwardExecutable)) {
    throw "Forward3D executable not found: $forwardExecutable"
}
if (-not (Test-Path -LiteralPath $lightingExecutable)) {
    throw "LightingShowcase executable not found: $lightingExecutable"
}

$checks = [System.Collections.Generic.List[object]]::new()
$forwardOutput = Join-Path $resolvedOutput "forward"
$forwardHealthScript = Join-Path $PSScriptRoot "Test-Forward3DShadowHealth.ps1"
$forwardRunError = $null
try {
    & $forwardHealthScript -ExecutablePath $forwardExecutable -OutputDirectory $forwardOutput `
        -ShadowQuality "low,medium,high,ultra" -WarmupFrames $WarmupFrames `
        -CaptureFrames $CaptureFrames -AutoExitFrames $AutoExitFrames -SkipBuild | Out-Host
} catch {
    $forwardRunError = $_.Exception.Message
}
$forwardSummaryPath = Join-Path $forwardOutput "forward3d_shadow_health.json"
$forwardSummary = if (Test-Path -LiteralPath $forwardSummaryPath) {
    Get-Content -Raw -LiteralPath $forwardSummaryPath | ConvertFrom-Json
} else {
    $null
}
Add-Check -Checks $checks -Area "forward" -Name "Forward3D four-tier strict data contract" `
    -Passed ($null -eq $forwardRunError -and $null -ne $forwardSummary -and $forwardSummary.verdict -eq "pass") `
    -Actual ($(if ($forwardRunError) { $forwardRunError } elseif ($forwardSummary) { $forwardSummary.verdict } else { "missing summary" })) `
    -Expected "pass"

$lightingOutput = Join-Path $resolvedOutput "lighting"
New-Item -ItemType Directory -Force -Path $lightingOutput | Out-Null
$lightingRuns = @{}
$lightingErrors = @{}
foreach ($lane in @(
    [pscustomobject]@{ name = "low"; quality = "low" },
    [pscustomobject]@{ name = "default-ultra"; quality = $null }
)) {
    $csvPath = Join-Path $lightingOutput "$($lane.name).csv"
    try {
        $lightingRuns[$lane.name] = @(Invoke-LightingBenchmark `
            -Executable $lightingExecutable -CsvPath $csvPath -Quality $lane.quality `
            -Warmup $WarmupFrames -Capture $CaptureFrames -AutoExit $AutoExitFrames)
    } catch {
        $lightingErrors[$lane.name] = $_.Exception.Message
        $lightingRuns[$lane.name] = @()
    }
    Add-Check -Checks $checks -Area "lighting" -Name "$($lane.name) benchmark captured rows" `
        -Passed (-not $lightingErrors.ContainsKey($lane.name) -and $lightingRuns[$lane.name].Count -gt 0) `
        -Actual ($(if ($lightingErrors.ContainsKey($lane.name)) { $lightingErrors[$lane.name] } else { $lightingRuns[$lane.name].Count })) `
        -Expected "> 0"
}

$budgetColumns = @(
    "shadow_budget_contract_version",
    "shadow_budget_resource_contract_valid",
    "shadow_budget_fallback_reason",
    "shadow_budget_swapchain_images",
    "shadow_budget_generation_max_passes",
    "shadow_budget_directional_receiver_samples",
    "shadow_budget_point_projection_samples",
    "shadow_budget_spot_projection_samples",
    "shadow_budget_rect_projection_samples",
    "shadow_budget_rect_projection_count",
    "shadow_budget_gpu_generation_scope",
    "shadow_budget_legacy_depth_bytes",
    "shadow_budget_directional_depth_bytes",
    "shadow_budget_local_depth_bytes",
    "shadow_budget_main_depth_bytes",
    "shadow_cascade_atlas_tile_size",
    "shadow_cascade_atlas_width",
    "shadow_cascade_atlas_height",
    "shadow_cascade_atlas_capacity",
    "local_shadow_atlas_tile_size",
    "local_shadow_atlas_width",
    "local_shadow_atlas_height",
    "local_shadow_atlas_capacity"
)
$typedFrameGraphColumns = @(
    "framegraph_validation_missing_resource_refs",
    "framegraph_validation_read_before_first_write",
    "framegraph_validation_duplicate_pass_ids",
    "framegraph_validation_duplicate_resource_ids",
    "framegraph_validation_active_writes_planned_resources",
    "framegraph_validation_write_only_roadmap_resources"
)

foreach ($laneName in @("low", "default-ultra")) {
    $qualityName = if ($laneName -eq "low") { "low" } else { "ultra" }
    $rows = @($lightingRuns[$laneName])
    if ($rows.Count -eq 0 -or $null -eq $forwardSummary) {
        continue
    }
    $forwardReport = $forwardSummary.reports | Where-Object { $_.quality -eq $qualityName } | Select-Object -First 1
    $forwardRow = Import-Csv -LiteralPath $forwardReport.csv | Select-Object -First 1
    $lightingRow = $rows[0]

    foreach ($column in $budgetColumns) {
        $forwardValue = Get-Number -Row $forwardRow -Name $column
        $lightingValues = @($rows | ForEach-Object { Get-Number -Row $_ -Name $column })
        $mismatchCount = @($lightingValues | Where-Object { $null -eq $_ -or $_ -ne $forwardValue }).Count
        Add-Check -Checks $checks -Area "portability" `
            -Name "$laneName $column matches Forward3D" `
            -Passed ($null -ne $forwardValue -and $mismatchCount -eq 0) `
            -Actual "forward=$forwardValue lighting=$($lightingValues -join '/')" `
            -Expected "identical resolved tier contract"
    }

    $forwardContactSamples = Get-Number -Row $forwardRow -Name "shadow_budget_contact_samples"
    $contactOverBudgetRows = @(
        $rows | Where-Object {
            $samples = Get-Number -Row $_ -Name "shadow_budget_contact_samples"
            $null -eq $samples -or $samples -lt 0 -or $samples -gt $forwardContactSamples
        }
    )
    Add-Check -Checks $checks -Area "portability" `
        -Name "$laneName contact samples stay within the tier ceiling" `
        -Passed ($null -ne $forwardContactSamples -and $contactOverBudgetRows.Count -eq 0) `
        -Actual "tierCeiling=$forwardContactSamples invalidRows=$($contactOverBudgetRows.Count)" `
        -Expected "0 <= scene samples <= tier ceiling"
    $implicitContactDisableRows = @(
        $rows | Where-Object {
            $samples = Get-Number -Row $_ -Name "shadow_budget_contact_samples"
            $strength = Get-Number -Row $_ -Name "shadow_contact_strength"
            ($samples -lt $forwardContactSamples) -and ($samples -ne 0 -or $strength -ne 0)
        }
    )
    Add-Check -Checks $checks -Area "portability" `
        -Name "$laneName contact-shadow budget reduction is an explicit scene opt-out" `
        -Passed ($implicitContactDisableRows.Count -eq 0) `
        -Actual "implicitRows=$($implicitContactDisableRows.Count)" `
        -Expected "reduced tier budget resolves to samples=0 and strength=0"

    foreach ($column in $typedFrameGraphColumns) {
        $invalidRows = @($rows | Where-Object { (Get-Number -Row $_ -Name $column) -ne 0 })
        Add-Check -Checks $checks -Area "portability" `
            -Name "$laneName $column is zero" -Passed ($invalidRows.Count -eq 0) `
            -Actual "invalidRows=$($invalidRows.Count)" -Expected "0"
    }

    $invalidTimingRows = @(
        $rows | Where-Object {
            (Get-Number -Row $_ -Name "gpu_available") -ne 1 -or
            (Get-Number -Row $_ -Name "gpu_shadow_ms") -le 0
        }
    )
    Add-Check -Checks $checks -Area "portability" `
        -Name "$laneName GPU shadow generation timing is available" `
        -Passed ($invalidTimingRows.Count -eq 0) `
        -Actual "invalidRows=$($invalidTimingRows.Count)" -Expected "0"
}

if ($lightingRuns["default-ultra"].Count -gt 0) {
    $defaultQualityIds = @(
        $lightingRuns["default-ultra"] | ForEach-Object { Get-Number -Row $_ -Name "shadow_quality" }
    )
    Add-Check -Checks $checks -Area "defaults" -Name "unoverridden scenes resolve Ultra shadows" `
        -Passed (@($defaultQualityIds | Where-Object { $_ -ne 4 }).Count -eq 0) `
        -Actual ($defaultQualityIds -join "/") -Expected "4"
}

$shadowSettingsSource = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "src\renderer\vulkan\shadow_settings.h")
Add-Check -Checks $checks -Area "defaults" -Name "renderer shadow settings default to Ultra" `
    -Passed ($shadowSettingsSource -match 'VulkanShadowQuality quality\s*=\s*VulkanShadowQuality::Ultra;') `
    -Actual "shadow_settings.h" -Expected "VulkanShadowQuality::Ultra"

$commandBufferSource = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "src\renderer\vulkan\command_buffer.cpp")
$timestampMarkers = [ordered]@{
    shadowStart = $commandBufferSource.IndexOf("GpuTimestamp::ShadowStart")
    legacyDepth = $commandBufferSource.IndexOf("VkRenderPassBeginInfo shadowPassInfo{}")
    cascadeDepth = $commandBufferSource.IndexOf("VkRenderPassBeginInfo cascadePassInfo{}")
    localDepth = $commandBufferSource.IndexOf("VkRenderPassBeginInfo localShadowPassInfo{}")
    shadowEnd = $commandBufferSource.IndexOf("GpuTimestamp::ShadowEnd")
    gBuffer = $commandBufferSource.IndexOf("if (gBufferRenderPass != nullptr && gBufferFramebuffer != nullptr)")
}
$markerValues = @($timestampMarkers.Values)
$timestampOrderValid = @($markerValues | Where-Object { $_ -lt 0 }).Count -eq 0
for ($index = 1; $timestampOrderValid -and $index -lt $markerValues.Count; ++$index) {
    $timestampOrderValid = $markerValues[$index] -gt $markerValues[$index - 1]
}
Add-Check -Checks $checks -Area "timestamp" -Name "GPU shadow timing covers legacy, CSM, and local generation" `
    -Passed $timestampOrderValid -Actual ($timestampMarkers | ConvertTo-Json -Compress) `
    -Expected "ShadowStart < legacy < CSM < local < ShadowEnd < GBuffer"
Add-Check -Checks $checks -Area "timestamp" -Name "shadow timestamp boundaries are unique" `
    -Passed (
        ([regex]::Matches($commandBufferSource, 'GpuTimestamp::ShadowStart')).Count -eq 1 -and
        ([regex]::Matches($commandBufferSource, 'GpuTimestamp::ShadowEnd')).Count -eq 1
    ) `
    -Actual "start=$(([regex]::Matches($commandBufferSource, 'GpuTimestamp::ShadowStart')).Count),end=$(([regex]::Matches($commandBufferSource, 'GpuTimestamp::ShadowEnd')).Count)" `
    -Expected "1/1"

$lightingMetrics = @{}
foreach ($laneName in @("low", "default-ultra")) {
    $rows = @($lightingRuns[$laneName])
    if ($rows.Count -eq 0) {
        continue
    }
    $gpuTimes = @($rows | ForEach-Object { Get-Number -Row $_ -Name "gpu_shadow_ms" })
    $lightingMetrics[$laneName] = [pscustomobject]@{
        rows = $rows.Count
        quality = Get-Number -Row $rows[0] -Name "shadow_quality"
        logicalDepthMiB = [Math]::Round(
            (Get-Number -Row $rows[0] -Name "shadow_budget_main_depth_bytes") / 1MB,
            3
        )
        gpuShadowAverageMs = [Math]::Round(
            ($gpuTimes | Measure-Object -Average).Average,
            4
        )
    }
}

$passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
$failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
$summary = [pscustomobject]@{
    generatedAt = (Get-Date).ToString("o")
    forwardExecutable = $forwardExecutable
    lightingExecutable = $lightingExecutable
    outputDirectory = $resolvedOutput
    verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
    passCount = $passCount
    warnCount = 0
    failCount = $failCount
    forward = $forwardSummary
    lightingMetrics = $lightingMetrics
    checks = @($checks)
}
$jsonPath = Join-Path $resolvedOutput "shadow_quality_budget.json"
$summary | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
$summary

if ($Strict -and $summary.verdict -ne "pass") {
    throw "Shadow quality budget verdict is $($summary.verdict). See $jsonPath"
}
