param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "out\aa_mode_contracts",
    [int]$Width = 960,
    [int]$Height = 540,
    [int]$BenchmarkFrames = 6,
    [int]$AutoExitFrames = 10,
    [int]$TimeoutSeconds = 20,
    [switch]$Build
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

function Invoke-WithEnvironment {
    param(
        [Parameter(Mandatory = $true)][string[]]$ManagedKeys,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)][scriptblock]$Script
    )

    $previous = @{}
    foreach ($key in $ManagedKeys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, $null, "Process")
    }

    try {
        foreach ($key in $Environment.Keys) {
            [Environment]::SetEnvironmentVariable(
                [string]$key,
                [string]$Environment[$key],
                "Process"
            )
        }
        & $Script
    } finally {
        foreach ($key in $ManagedKeys) {
            [Environment]::SetEnvironmentVariable($key, $previous[$key], "Process")
        }
    }
}

function Require-CsvValue {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Expected
    )

    $actual = "$($Row.$Name)"
    if ($actual -ne $Expected) {
        throw "$Name expected '$Expected' but was '$actual'"
    }
}

function Require-NumericInverse {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$SourceName,
        [Parameter(Mandatory = $true)][string]$InverseName,
        [double]$Tolerance = 0.00001
    )

    $source = [double]::Parse(
        "$($Row.$SourceName)",
        [Globalization.CultureInfo]::InvariantCulture
    )
    $inverse = [double]::Parse(
        "$($Row.$InverseName)",
        [Globalization.CultureInfo]::InvariantCulture
    )
    if ([Math]::Abs($source + $inverse) -gt $Tolerance) {
        throw "$InverseName expected inverse of $SourceName, but $SourceName=$source and $InverseName=$inverse"
    }
}

function Require-NumericClose {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$Expected,
        [double]$Tolerance = 0.0002
    )

    $actual = [double]::Parse(
        "$($Row.$Name)",
        [Globalization.CultureInfo]::InvariantCulture
    )
    if ([Math]::Abs($actual - $Expected) -gt $Tolerance) {
        throw "$Name expected $Expected +/- $Tolerance but was $actual"
    }
}

function Require-DlssCommon {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$ExpectedMode,
        [Parameter(Mandatory = $true)][string]$ExpectedQualityMode,
        [Parameter(Mandatory = $true)][string]$ExpectedRequestedScale,
        [Parameter(Mandatory = $true)][string]$ExpectedActiveScale,
        [Parameter(Mandatory = $true)][string]$ExpectedWidth,
        [Parameter(Mandatory = $true)][string]$ExpectedHeight,
        [Parameter(Mandatory = $true)][string]$ExpectedPreset,
        [Parameter(Mandatory = $true)][double]$ExpectedMaterialMipLodBias
    )

    Require-CsvValue -Row $Row -Name "temporal_antialiasing_mode" -Expected $ExpectedMode
    Require-CsvValue -Row $Row -Name "temporal_taa_resolve_enabled" -Expected "0"
    Require-CsvValue -Row $Row -Name "temporal_taa_resolve_suppressed_for_upscaler" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_evaluate_attempted" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_output_ready" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscale_post_source_active" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_jitter_applied" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_velocity_jittered_history_policy" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_velocity_previous_jitter_applied" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_recommended_preset" -Expected $ExpectedPreset
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_evaluate_sharpness" -Expected "0"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_create_flag_is_hdr" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_create_flag_mv_low_res" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_create_flag_mv_jittered" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_create_flag_depth_inverted" -Expected "0"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_create_flag_auto_exposure" -Expected "0"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_quality_mode" -Expected $ExpectedQualityMode
    Require-CsvValue -Row $Row -Name "temporal_render_scale_requested" -Expected $ExpectedRequestedScale
    Require-CsvValue -Row $Row -Name "temporal_render_scale_active" -Expected $ExpectedActiveScale
    Require-NumericClose `
        -Row $Row `
        -Name "frame_material_texture_mip_lod_bias" `
        -Expected $ExpectedMaterialMipLodBias
    Require-CsvValue -Row $Row -Name "temporal_upscale_active_width" -Expected $ExpectedWidth
    Require-CsvValue -Row $Row -Name "temporal_upscale_active_height" -Expected $ExpectedHeight
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_input_color_width" -Expected $ExpectedWidth
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_input_color_height" -Expected $ExpectedHeight
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_input_depth_width" -Expected $ExpectedWidth
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_input_depth_height" -Expected $ExpectedHeight
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_input_motion_vector_width" -Expected $ExpectedWidth
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_input_motion_vector_height" -Expected $ExpectedHeight
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_input_depth_matches_render_extent" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_input_motion_vector_matches_render_extent" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_motion_vector_scale_pixel_space" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_motion_vector_scale_unit_space" -Expected "0"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_motion_vector_scale_matches_render_extent" -Expected "1"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_motion_vector_scale_x" -Expected "-$ExpectedWidth"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_motion_vector_scale_y" -Expected "-$ExpectedHeight"
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_render_width" -Expected $ExpectedWidth
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_render_height" -Expected $ExpectedHeight
    Require-CsvValue -Row $Row -Name "temporal_upscale_output_width" -Expected $fullWidth
    Require-CsvValue -Row $Row -Name "temporal_upscale_output_height" -Expected $fullHeight
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_output_width" -Expected $fullWidth
    Require-CsvValue -Row $Row -Name "temporal_upscaler_dlss_output_height" -Expected $fullHeight
    Require-NumericInverse `
        -Row $Row `
        -SourceName "temporal_jitter_pixels_x" `
        -InverseName "temporal_upscaler_dlss_jitter_offset_x"
    Require-NumericInverse `
        -Row $Row `
        -SourceName "temporal_jitter_pixels_y" `
        -InverseName "temporal_upscaler_dlss_jitter_offset_y"
}

function Get-ScaledExtentText {
    param(
        [Parameter(Mandatory = $true)][int]$BaseSize,
        [Parameter(Mandatory = $true)][double]$Scale
    )

    return [string][Math]::Max(1, [int][Math]::Round($BaseSize * $Scale))
}

function Get-ActiveScaleText {
    param(
        [Parameter(Mandatory = $true)][int]$Width,
        [Parameter(Mandatory = $true)][int]$Height,
        [Parameter(Mandatory = $true)][string]$ScaledWidth,
        [Parameter(Mandatory = $true)][string]$ScaledHeight
    )

    $active = [Math]::Min(
        ([double]$ScaledWidth) / [double]$Width,
        ([double]$ScaledHeight) / [double]$Height
    )
    return $active.ToString("G6", [Globalization.CultureInfo]::InvariantCulture)
}

function Invoke-AaProbe {
    param(
        [Parameter(Mandatory = $true)][string]$Mode,
        [Parameter(Mandatory = $true)][string]$ExePath,
        [Parameter(Mandatory = $true)][string]$RunRoot,
        [int]$AutoToggleFrame = 0,
        [int]$WarmupFramesOverride = -1,
        [int]$BenchmarkFramesOverride = -1,
        [int]$AutoExitFramesOverride = -1,
        [string]$WorkingDirectory = $repoRoot,
        [string]$ProbeSuffix = ""
    )

    $probeName = $Mode
    if ($AutoToggleFrame -gt 0) {
        $probeName = "$Mode-auto-toggle-$AutoToggleFrame"
    }
    if (-not [string]::IsNullOrWhiteSpace($ProbeSuffix)) {
        $probeName = "$probeName-$ProbeSuffix"
    }
    $csvPath = Join-Path $RunRoot "$probeName.csv"
    $stdoutPath = Join-Path $RunRoot "$probeName.stdout.log"
    $stderrPath = Join-Path $RunRoot "$probeName.stderr.log"
    Remove-Item $csvPath, $stdoutPath, $stderrPath -ErrorAction SilentlyContinue

    $environment = @{
        "SE_FORWARD3D_AA_MODE" = $Mode
        "SE_WINDOW_WIDTH" = "$Width"
        "SE_WINDOW_HEIGHT" = "$Height"
        "SE_VISUAL_QA_HIDE_IMGUI" = "1"
        "SE_HIDE_IMGUI" = "1"
        "SE_RENDER_VIEW" = "deferred-hdr"
        "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION" = "1"
        "SE_BENCHMARK_CSV" = $csvPath
        "SE_BENCHMARK_WARMUP_FRAMES" = "$(
            if ($WarmupFramesOverride -ge 0) { $WarmupFramesOverride } else { 0 }
        )"
        "SE_BENCHMARK_FRAMES" = "$(
            if ($BenchmarkFramesOverride -ge 0) { $BenchmarkFramesOverride } else { $BenchmarkFrames }
        )"
        "SE_AUTO_EXIT_FRAMES" = "$(
            if ($AutoExitFramesOverride -ge 0) { $AutoExitFramesOverride } else { $AutoExitFrames }
        )"
    }
    if ($AutoToggleFrame -gt 0) {
        $environment["SE_FORWARD3D_AA_AUTO_TOGGLE_FRAME"] = "$AutoToggleFrame"
    }

    if ($Mode -eq "dlss") {
        $environment["SE_UPSCALER_PLUGIN"] = "dlss"
        $environment["SE_DLSS_QUALITY"] = "dlaa"
        $environment["SE_DLSS_PRESET"] = "l"
        $environment["SE_DLSS_SHARPNESS"] = "0.0"
        $environment["SE_DLSS_PRESENT"] = "1"
        $environment["SE_TAA"] = "1"
        $environment["SE_TEMPORAL_JITTER"] = "1"
        $environment["SE_TAA_APPLY_JITTER"] = "1"
        $environment["SE_RENDER_SCALE"] = "1.0"
        $environment["SE_RENDER_SCALE_APPLY"] = "1"
        $environment["SE_TEMPORAL_VELOCITY_JITTER_POLICY"] = "jittered"
    }

    $managedKeys = @(
        "SE_FORWARD3D_AA_MODE",
        "SE_FORWARD3D_AA_AUTO_TOGGLE_FRAME",
        "SE_WINDOW_WIDTH",
        "SE_WINDOW_HEIGHT",
        "SE_VISUAL_QA_HIDE_IMGUI",
        "SE_HIDE_IMGUI",
        "SE_RENDER_VIEW",
        "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
        "SE_BENCHMARK_CSV",
        "SE_BENCHMARK_WARMUP_FRAMES",
        "SE_BENCHMARK_FRAMES",
        "SE_AUTO_EXIT_FRAMES",
        "SE_UPSCALER_PLUGIN",
        "SE_DLSS_QUALITY",
        "SE_DLSS_PRESET",
        "SE_DLSS_SHARPNESS",
        "SE_DLSS_PRESENT",
        "SE_TAA",
        "SE_TEMPORAL_JITTER",
        "SE_TAA_APPLY_JITTER",
        "SE_RENDER_SCALE",
        "SE_RENDER_SCALE_APPLY",
        "SE_TEMPORAL_VELOCITY_JITTER_POLICY",
        "SE_ENABLE_DLSS_VULKAN_EXTENSIONS",
        "SE_DLSS_VULKAN_EXTENSIONS",
        "SE_TEXTURE_MIP_LOD_BIAS",
        "SE_MATERIAL_TEXTURE_MIP_BIAS",
        "SE_TEXTURE_MIP_BIAS"
    )

    Invoke-WithEnvironment -ManagedKeys $managedKeys -Environment $environment -Script {
        $process = Start-Process `
            -FilePath $ExePath `
            -WorkingDirectory $WorkingDirectory `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath `
            -PassThru

        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "$Mode probe timed out after $TimeoutSeconds seconds"
        }
        if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
            throw "$Mode probe exited with code $($process.ExitCode)"
        }
    }

    if (-not (Test-Path -LiteralPath $csvPath)) {
        throw "$Mode probe did not write CSV: $csvPath"
    }

    return Import-Csv $csvPath | Select-Object -Last 1
}

if ($Build) {
    & .\_quick_build.bat | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "_quick_build.bat failed with exit code $LASTEXITCODE"
    }
}

$resolvedExePath = Resolve-FullPath $ExecutablePath
if (-not (Test-Path -LiteralPath $resolvedExePath)) {
    throw "Executable not found: $resolvedExePath"
}

$runRoot = Resolve-FullPath $OutputDirectory
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

$dlssSrQualityScale = 2.0 / 3.0
$dlssSrBalancedScale = 0.58
$dlssSrPerformanceScale = 0.5
$dlssSrQualityMipLodBias = -1.58496
$dlssSrBalancedMipLodBias = -1.78588
$dlssSrPerformanceMipLodBias = -2.0
$fullWidth = [string]$Width
$fullHeight = [string]$Height
$qualityWidth = Get-ScaledExtentText -BaseSize $Width -Scale $dlssSrQualityScale
$qualityHeight = Get-ScaledExtentText -BaseSize $Height -Scale $dlssSrQualityScale
$balancedWidth = Get-ScaledExtentText -BaseSize $Width -Scale $dlssSrBalancedScale
$balancedHeight = Get-ScaledExtentText -BaseSize $Height -Scale $dlssSrBalancedScale
$performanceWidth = Get-ScaledExtentText -BaseSize $Width -Scale $dlssSrPerformanceScale
$performanceHeight = Get-ScaledExtentText -BaseSize $Height -Scale $dlssSrPerformanceScale
$fullActiveScale = Get-ActiveScaleText -Width $Width -Height $Height -ScaledWidth $fullWidth -ScaledHeight $fullHeight
$qualityActiveScale = Get-ActiveScaleText -Width $Width -Height $Height -ScaledWidth $qualityWidth -ScaledHeight $qualityHeight
$balancedActiveScale = Get-ActiveScaleText -Width $Width -Height $Height -ScaledWidth $balancedWidth -ScaledHeight $balancedHeight
$performanceActiveScale = Get-ActiveScaleText -Width $Width -Height $Height -ScaledWidth $performanceWidth -ScaledHeight $performanceHeight

$taaRow = Invoke-AaProbe -Mode "taa" -ExePath $resolvedExePath -RunRoot $runRoot
Require-CsvValue -Row $taaRow -Name "temporal_antialiasing_mode" -Expected "1"
Require-CsvValue -Row $taaRow -Name "temporal_taa_resolve_enabled" -Expected "1"
Require-CsvValue -Row $taaRow -Name "temporal_taa_resolve_suppressed_for_upscaler" -Expected "0"
Require-CsvValue -Row $taaRow -Name "temporal_upscaler_dlss_evaluate_attempted" -Expected "0"
if ("$($taaRow.temporal_jitter_applied)" -eq "1") {
    Require-CsvValue -Row $taaRow -Name "temporal_velocity_jittered_history_policy" -Expected "0"
    Require-CsvValue -Row $taaRow -Name "temporal_velocity_previous_jitter_applied" -Expected "0"
}

$taaRuntimeDlaaRow = Invoke-AaProbe `
    -Mode "taa" `
    -ExePath $resolvedExePath `
    -RunRoot $runRoot `
    -AutoToggleFrame 2 `
    -WarmupFramesOverride 18 `
    -BenchmarkFramesOverride 8 `
    -AutoExitFramesOverride 34 `
    -ProbeSuffix "hot-dlaa"
Require-DlssCommon `
    -Row $taaRuntimeDlaaRow `
    -ExpectedMode "2" `
    -ExpectedQualityMode "6" `
    -ExpectedRequestedScale "1" `
    -ExpectedActiveScale $fullActiveScale `
    -ExpectedWidth $fullWidth `
    -ExpectedHeight $fullHeight `
    -ExpectedPreset "12" `
    -ExpectedMaterialMipLodBias 0.0

$dlssRow = Invoke-AaProbe -Mode "dlss" -ExePath $resolvedExePath -RunRoot $runRoot
Require-DlssCommon `
    -Row $dlssRow `
    -ExpectedMode "2" `
    -ExpectedQualityMode "6" `
    -ExpectedRequestedScale "1" `
    -ExpectedActiveScale $fullActiveScale `
    -ExpectedWidth $fullWidth `
    -ExpectedHeight $fullHeight `
    -ExpectedPreset "12" `
    -ExpectedMaterialMipLodBias 0.0

$dlssQualityRow = Invoke-AaProbe -Mode "dlss-sr-quality" -ExePath $resolvedExePath -RunRoot $runRoot
Require-DlssCommon `
    -Row $dlssQualityRow `
    -ExpectedMode "3" `
    -ExpectedQualityMode "3" `
    -ExpectedRequestedScale "0.666667" `
    -ExpectedActiveScale $qualityActiveScale `
    -ExpectedWidth $qualityWidth `
    -ExpectedHeight $qualityHeight `
    -ExpectedPreset "12" `
    -ExpectedMaterialMipLodBias $dlssSrQualityMipLodBias

$dlssRuntimeQualityRow = Invoke-AaProbe `
    -Mode "dlss" `
    -ExePath $resolvedExePath `
    -RunRoot $runRoot `
    -AutoToggleFrame 2 `
    -WarmupFramesOverride 18 `
    -BenchmarkFramesOverride 8 `
    -AutoExitFramesOverride 34
Require-DlssCommon `
    -Row $dlssRuntimeQualityRow `
    -ExpectedMode "3" `
    -ExpectedQualityMode "3" `
    -ExpectedRequestedScale "0.666667" `
    -ExpectedActiveScale $qualityActiveScale `
    -ExpectedWidth $qualityWidth `
    -ExpectedHeight $qualityHeight `
    -ExpectedPreset "12" `
    -ExpectedMaterialMipLodBias $dlssSrQualityMipLodBias

$dlssDebugCwdQualityRow = Invoke-AaProbe `
    -Mode "dlss" `
    -ExePath $resolvedExePath `
    -RunRoot $runRoot `
    -AutoToggleFrame 2 `
    -WarmupFramesOverride 18 `
    -BenchmarkFramesOverride 8 `
    -AutoExitFramesOverride 34 `
    -WorkingDirectory (Split-Path -Parent $resolvedExePath) `
    -ProbeSuffix "debug-cwd"
Require-DlssCommon `
    -Row $dlssDebugCwdQualityRow `
    -ExpectedMode "3" `
    -ExpectedQualityMode "3" `
    -ExpectedRequestedScale "0.666667" `
    -ExpectedActiveScale $qualityActiveScale `
    -ExpectedWidth $qualityWidth `
    -ExpectedHeight $qualityHeight `
    -ExpectedPreset "12" `
    -ExpectedMaterialMipLodBias $dlssSrQualityMipLodBias

$dlssBalancedRow = Invoke-AaProbe -Mode "dlss-sr-balanced" -ExePath $resolvedExePath -RunRoot $runRoot
Require-DlssCommon `
    -Row $dlssBalancedRow `
    -ExpectedMode "4" `
    -ExpectedQualityMode "2" `
    -ExpectedRequestedScale "0.58" `
    -ExpectedActiveScale $balancedActiveScale `
    -ExpectedWidth $balancedWidth `
    -ExpectedHeight $balancedHeight `
    -ExpectedPreset "12" `
    -ExpectedMaterialMipLodBias $dlssSrBalancedMipLodBias

$dlssPerformanceRow = Invoke-AaProbe -Mode "dlss-sr-performance" -ExePath $resolvedExePath -RunRoot $runRoot
Require-DlssCommon `
    -Row $dlssPerformanceRow `
    -ExpectedMode "5" `
    -ExpectedQualityMode "1" `
    -ExpectedRequestedScale "0.5" `
    -ExpectedActiveScale $performanceActiveScale `
    -ExpectedWidth $performanceWidth `
    -ExpectedHeight $performanceHeight `
    -ExpectedPreset "12" `
    -ExpectedMaterialMipLodBias $dlssSrPerformanceMipLodBias

[ordered]@{
    nativeTaa = @{
        jitterApplied = "$($taaRow.temporal_jitter_applied)"
        jitteredHistoryPolicy = "$($taaRow.temporal_velocity_jittered_history_policy)"
        previousJitterApplied = "$($taaRow.temporal_velocity_previous_jitter_applied)"
        taaResolveEnabled = "$($taaRow.temporal_taa_resolve_enabled)"
    }
    dlss = @{
        outputReady = "$($dlssRow.temporal_upscaler_dlss_output_ready)"
        postSourceActive = "$($dlssRow.temporal_upscale_post_source_active)"
        taaResolveSuppressed = "$($dlssRow.temporal_taa_resolve_suppressed_for_upscaler)"
    }
    dlssSr = @{
        quality = "$($dlssQualityRow.temporal_upscale_active_width)x$($dlssQualityRow.temporal_upscale_active_height)"
        balanced = "$($dlssBalancedRow.temporal_upscale_active_width)x$($dlssBalancedRow.temporal_upscale_active_height)"
        performance = "$($dlssPerformanceRow.temporal_upscale_active_width)x$($dlssPerformanceRow.temporal_upscale_active_height)"
        mipLodBias = @{
        quality = "$($dlssQualityRow.frame_material_texture_mip_lod_bias)"
            runtimeQuality = "$($dlssRuntimeQualityRow.frame_material_texture_mip_lod_bias)"
            debugCwdQuality = "$($dlssDebugCwdQualityRow.frame_material_texture_mip_lod_bias)"
            balanced = "$($dlssBalancedRow.frame_material_texture_mip_lod_bias)"
            performance = "$($dlssPerformanceRow.frame_material_texture_mip_lod_bias)"
        }
        preset = @{
            quality = "$($dlssQualityRow.temporal_upscaler_dlss_recommended_preset)"
            runtimeQuality = "$($dlssRuntimeQualityRow.temporal_upscaler_dlss_recommended_preset)"
            debugCwdQuality = "$($dlssDebugCwdQualityRow.temporal_upscaler_dlss_recommended_preset)"
            balanced = "$($dlssBalancedRow.temporal_upscaler_dlss_recommended_preset)"
            performance = "$($dlssPerformanceRow.temporal_upscaler_dlss_recommended_preset)"
        }
    }
} | ConvertTo-Json -Depth 4
