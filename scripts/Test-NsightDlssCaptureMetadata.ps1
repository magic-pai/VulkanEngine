param(
    [Parameter(Mandatory = $true)]
    [string]$CapturePath,
    [string]$NsightHostPath = "C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.2.0\host\windows-desktop-nomad-x64",
    [string]$ExpectedResolution = "2560x1440",
    [string]$ExpectedDlssQuality = "dlaa",
    [string]$ExpectedDlssPreset = "m",
    [string]$ExpectedDlssPresetValue = "",
    [string]$BenchmarkCsvPath = "",
    [string]$ScreenshotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$resolvedCapture = Resolve-Path $CapturePath
$replayPath = Join-Path $NsightHostPath "ngfx-replay.exe"
if (-not (Test-Path -LiteralPath $replayPath)) {
    $command = Get-Command ngfx-replay.exe -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw "ngfx-replay.exe was not found. Pass -NsightHostPath or add it to PATH."
    }
    $replayPath = $command.Source
}

function Invoke-NgfxReplay {
    param([string[]]$Arguments)

    $output = & $replayPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ngfx-replay failed with exit code $LASTEXITCODE for arguments: $($Arguments -join ' ')"
    }
    return $output
}

function Require-CsvValue {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Expected
    )

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Benchmark CSV is missing required column '$Name'"
    }
    $actual = "$($property.Value)"
    if ($actual -ne $Expected) {
        throw "Benchmark CSV column '$Name' expected '$Expected' but found '$actual'"
    }
}

function Resolve-DlssPresetValue {
    param([Parameter(Mandatory = $true)][string]$Preset)

    switch ($Preset.Trim().ToLowerInvariant()) {
        "k" { return "11" }
        "l" { return "12" }
        "m" { return "13" }
        default { return "" }
    }
}

function Read-LastCsvRow {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolvedCsv = Resolve-Path $Path
    $rows = @(Import-Csv -LiteralPath $resolvedCsv.Path)
    if ($rows.Count -eq 0) {
        throw "Benchmark CSV has no rows: $($resolvedCsv.Path)"
    }
    return $rows[-1]
}

$metadata = Invoke-NgfxReplay @("--metadata", $resolvedCapture.Path) | ConvertFrom-Json
$objects = Invoke-NgfxReplay @("--metadata-objects", $resolvedCapture.Path) | ConvertFrom-Json
$errorLogText = (Invoke-NgfxReplay @("--metadata-logs-errors", $resolvedCapture.Path)) -join "`n"

if ($ExpectedResolution.Length -gt 0 -and $metadata.resolution -ne $ExpectedResolution) {
    throw "Expected capture resolution $ExpectedResolution but found $($metadata.resolution)"
}

$expectedEnvironment = [ordered]@{
    "SE_UPSCALER_PLUGIN" = "dlss"
    "SE_DLSS_QUALITY" = $ExpectedDlssQuality
    "SE_DLSS_PRESET" = $ExpectedDlssPreset
    "SE_DLSS_PRESENT" = "1"
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION" = "1"
    "SE_DLSS_CREATE_FLAG_MV_JITTERED" = "1"
    "SE_TEMPORAL_VELOCITY_JITTER_POLICY" = "jittered"
}
$environment = @{}
foreach ($entry in @($metadata.process_environment)) {
    $separator = $entry.IndexOf("=")
    if ($separator -le 0) {
        continue
    }
    $environment[$entry.Substring(0, $separator)] = $entry.Substring($separator + 1)
}

$missingEnvironment = @()
foreach ($key in $expectedEnvironment.Keys) {
    if (-not $environment.ContainsKey($key) -or
        $environment[$key] -ne $expectedEnvironment[$key]) {
        $actual = if ($environment.ContainsKey($key)) { $environment[$key] } else { "<missing>" }
        $missingEnvironment += "$key expected=$($expectedEnvironment[$key]) actual=$actual"
    }
}
if ($missingEnvironment.Count -gt 0) {
    throw "Capture environment mismatch: $($missingEnvironment -join '; ')"
}

$requiredObjectPrefixes = @(
    "SelfEngine.DLSS.InputColor.image",
    "SelfEngine.DLSS.InputDepth.image",
    "SelfEngine.DLSS.InputMotionVectors.image",
    "SelfEngine.DLSS.OutputColor.image",
    "SelfEngine.DLSS.BiasCurrentColorMask.image",
    "SelfEngine.DLSS.TransparencyMask.image",
    "SelfEngine.Temporal.HistoryColor.image"
)
$objectNames = @($objects | ForEach-Object { $_.object_name })
$missingObjectPrefixes = @(
    foreach ($prefix in $requiredObjectPrefixes) {
        if (-not (@($objectNames | Where-Object { $_ -like "$prefix*" }).Count -gt 0)) {
            $prefix
        }
    }
)
if ($missingObjectPrefixes.Count -gt 0) {
    throw "Missing Nsight object names: $($missingObjectPrefixes -join ', ')"
}

if ($errorLogText.Trim().Length -gt 0 -and
    $errorLogText -notmatch "No log messages found with severity >= 2") {
    throw "Capture contains error-level embedded logs: $errorLogText"
}

if ($ScreenshotPath.Length -gt 0) {
    Invoke-NgfxReplay @(
        "--metadata-screenshot",
        $ScreenshotPath,
        $resolvedCapture.Path
    ) | Out-Null
}

$csvContractReady = $false
$csvContract = [ordered]@{}
if ($BenchmarkCsvPath.Length -gt 0) {
    $csvRow = Read-LastCsvRow $BenchmarkCsvPath
    $resolutionParts = $ExpectedResolution.Split("x")
    if ($resolutionParts.Count -ne 2) {
        throw "Expected resolution must use WIDTHxHEIGHT format: $ExpectedResolution"
    }
    $expectedWidth = $resolutionParts[0]
    $expectedHeight = $resolutionParts[1]

    Require-CsvValue $csvRow "framegraph_validation_issues" "0"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_evaluate_result" "1"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_output_ready" "1"
    Require-CsvValue $csvRow "temporal_upscale_post_source_active" "1"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_quality_gate_ready" "1"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_quality_required_mask" "255"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_quality_ready_mask" "255"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_quality_blocker_mask" "0"
    Require-CsvValue $csvRow "runtime_import_skinned_animation_unsupported" "0"
    Require-CsvValue $csvRow "temporal_velocity_object_motion_ready" "1"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_quality_scene_content_motion_supported" "1"
    Require-CsvValue $csvRow "temporal_jitter_applied" "1"
    Require-CsvValue $csvRow "temporal_velocity_jittered_history_policy" "1"
    Require-CsvValue $csvRow "temporal_velocity_previous_jitter_applied" "1"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_create_flag_mv_jittered" "1"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_motion_vector_scale_pixel_space" "1"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_motion_vector_scale_unit_space" "0"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_motion_vector_scale_matches_render_extent" "1"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_input_color_width" $expectedWidth
    Require-CsvValue $csvRow "temporal_upscaler_dlss_input_color_height" $expectedHeight
    Require-CsvValue $csvRow "temporal_upscaler_dlss_input_depth_width" $expectedWidth
    Require-CsvValue $csvRow "temporal_upscaler_dlss_input_depth_height" $expectedHeight
    Require-CsvValue $csvRow "temporal_upscaler_dlss_input_motion_vector_width" $expectedWidth
    Require-CsvValue $csvRow "temporal_upscaler_dlss_input_motion_vector_height" $expectedHeight
    Require-CsvValue $csvRow "temporal_upscaler_dlss_render_width" $expectedWidth
    Require-CsvValue $csvRow "temporal_upscaler_dlss_render_height" $expectedHeight
    Require-CsvValue $csvRow "temporal_upscaler_dlss_output_width" $expectedWidth
    Require-CsvValue $csvRow "temporal_upscaler_dlss_output_height" $expectedHeight
    Require-CsvValue $csvRow "temporal_upscaler_dlss_motion_vector_scale_x" "-$expectedWidth"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_motion_vector_scale_y" "-$expectedHeight"
    $expectedPresetValue = $ExpectedDlssPresetValue.Trim()
    if ($expectedPresetValue.Length -eq 0) {
        $expectedPresetValue = Resolve-DlssPresetValue $ExpectedDlssPreset
    }
    if ($expectedPresetValue.Length -gt 0) {
        Require-CsvValue $csvRow "temporal_upscaler_dlss_recommended_preset" $expectedPresetValue
    }

    $jitterX = "$($csvRow.temporal_jitter_pixels_x)"
    $jitterY = "$($csvRow.temporal_jitter_pixels_y)"
    Require-CsvValue $csvRow "temporal_upscaler_dlss_jitter_offset_x" $jitterX
    Require-CsvValue $csvRow "temporal_upscaler_dlss_jitter_offset_y" $jitterY

    $csvContractReady = $true
    $csvContract = [ordered]@{
        benchmarkCsv = (Resolve-Path $BenchmarkCsvPath).Path
        sampleFrame = "$($csvRow.sample_frame)"
        renderedFrame = "$($csvRow.rendered_frame)"
        dlssPreset = "$($csvRow.temporal_upscaler_dlss_recommended_preset)"
        dlssEvaluateOutput = "$($csvRow.temporal_upscaler_dlss_evaluate_result)/$($csvRow.temporal_upscaler_dlss_output_ready)"
        qualityGate = "$($csvRow.temporal_upscaler_dlss_quality_gate_ready)/$($csvRow.temporal_upscaler_dlss_quality_blocker_mask)"
        qualityMasks = "$($csvRow.temporal_upscaler_dlss_quality_required_mask)/$($csvRow.temporal_upscaler_dlss_quality_ready_mask)/$($csvRow.temporal_upscaler_dlss_quality_blocker_mask)"
        dlssInputExtents = "$($csvRow.temporal_upscaler_dlss_input_color_width)x$($csvRow.temporal_upscaler_dlss_input_color_height)/$($csvRow.temporal_upscaler_dlss_input_depth_width)x$($csvRow.temporal_upscaler_dlss_input_depth_height)/$($csvRow.temporal_upscaler_dlss_input_motion_vector_width)x$($csvRow.temporal_upscaler_dlss_input_motion_vector_height)"
        dlssOutputExtent = "$($csvRow.temporal_upscaler_dlss_output_width)x$($csvRow.temporal_upscaler_dlss_output_height)"
        dlssMotionVectorScale = "$($csvRow.temporal_upscaler_dlss_motion_vector_scale_x)/$($csvRow.temporal_upscaler_dlss_motion_vector_scale_y)"
        dlssJitter = "$($csvRow.temporal_upscaler_dlss_jitter_offset_x)/$($csvRow.temporal_upscaler_dlss_jitter_offset_y)"
        temporalJitter = "$($csvRow.temporal_jitter_pixels_x)/$($csvRow.temporal_jitter_pixels_y)"
        jitteredHistory = "$($csvRow.temporal_velocity_jittered_history_policy)/$($csvRow.temporal_velocity_previous_jitter_applied)"
        skinnedProduction = "$($csvRow.runtime_import_skinned_animation_unsupported)/$($csvRow.temporal_upscaler_dlss_quality_scene_content_motion_supported)/$($csvRow.temporal_velocity_object_motion_ready)"
    }
}

$selfEngineObjects = @($objects | Where-Object { $_.object_name -like "SelfEngine.*" })
[ordered]@{
    capture = $resolvedCapture.Path
    capturedFrame = $metadata.captured_frame
    api = $metadata.primary_api
    gpu = $metadata.primary_gpu
    resolution = $metadata.resolution
    requiredEnvironmentReady = $true
    requiredObjectNamesReady = $true
    embeddedErrorLogsReady = $true
    csvContractReady = $csvContractReady
    csvContract = $csvContract
    selfEngineObjectCount = $selfEngineObjects.Count
    selfEngineImageCount =
        @($selfEngineObjects | Where-Object { $_.type_name -eq "Image" }).Count
    selfEngineImageViewCount =
        @($selfEngineObjects | Where-Object { $_.type_name -eq "ImageView" }).Count
    screenshot = $ScreenshotPath
} | ConvertTo-Json -Depth 4
