param(
    [string]$Target = "SelfEngineForward3D",
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$WindowTitle = "SelfEngine Forward 3D",
    [string]$OutputDirectory = "out\reference_captures\dlss_visual_qa",
    [int]$TimeoutSeconds = 15,
    [int]$CaptureDelaySeconds = 8,
    [int]$MinChangedPixels = 512,
    [double]$MaxMeanDelta = 160.0,
    [string]$BaselinePath = "docs\reference_baselines\dlss_visual_qa_baseline.json",
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (-not $SkipBuild) {
    & .\_quick_build.bat | Out-Host
    $buildExitCodeVariable = Get-Variable -Name LASTEXITCODE -ErrorAction SilentlyContinue
    $buildExitCode = if ($null -ne $buildExitCodeVariable) {
        [int]$buildExitCodeVariable.Value
    } else {
        0
    }
    if ($buildExitCode -ne 0) {
        throw "_quick_build.bat failed with exit code $buildExitCode"
    }
}

$exePath = Join-Path $repoRoot $ExecutablePath
if (!(Test-Path -LiteralPath $exePath)) {
    throw "Executable not found: $exePath"
}

$outputRoot = Join-Path $repoRoot $OutputDirectory
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$baselineManifestPath = if ([System.IO.Path]::IsPathRooted($BaselinePath)) {
    [System.IO.Path]::GetFullPath($BaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BaselinePath))
}
if (!(Test-Path -LiteralPath $baselineManifestPath)) {
    throw "DLSS visual QA baseline manifest not found: $baselineManifestPath"
}
$baselineManifest = Get-Content -Raw -LiteralPath $baselineManifestPath | ConvertFrom-Json
if ($baselineManifest.target -ne $Target) {
    throw "DLSS visual QA baseline target mismatch: expected $Target, manifest has $($baselineManifest.target)"
}

Add-Type -AssemblyName System.Drawing
if (-not ("SelfEngineVisualQaWin32" -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class SelfEngineVisualQaWin32 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll")]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(
        IntPtr hWnd,
        IntPtr hWndInsertAfter,
        int X,
        int Y,
        int cx,
        int cy,
        uint uFlags
    );
}
'@
}

$managedEnvironmentKeys = @(
    "SE_AUTO_EXIT_FRAMES",
    "SE_BENCHMARK_SCENE",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_BENCHMARK_CSV",
    "SE_RENDER_SCALE",
    "SE_RENDER_SCALE_APPLY",
    "SE_INTERNAL_RENDER_SCALE_APPLY",
    "SE_TAA",
    "SE_TAA_RESOLVE",
    "SE_TEMPORAL_JITTER",
    "SE_CAMERA_JITTER",
    "SE_RENDER_VIEW",
    "SE_UPSCALER_PLUGIN",
    "SE_TEMPORAL_UPSCALER_PLUGIN",
    "SE_DLSS_PRESENT",
    "SE_TEMPORAL_UPSCALE_PRESENT",
    "SE_TEMPORAL_UPSCALE_OUTPUT_PRESENT",
    "SE_UPSCALER_PRESENT",
    "SE_DLSS_REFERENCE_BASELINE_PATH",
    "SE_DLSS_VISUAL_BASELINE_PATH",
    "SE_NVIDIA_DLSS_SDK_DIR",
    "SE_DLSS_SDK_DIR",
    "SE_BLOOM"
)

function Invoke-WithEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Environment,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Script
    )

    $previous = @{}
    foreach ($key in $managedEnvironmentKeys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, $null, "Process")
    }

    try {
        foreach ($key in $Environment.Keys) {
            [Environment]::SetEnvironmentVariable($key, [string]$Environment[$key], "Process")
        }
        & $Script
    } finally {
        foreach ($key in $managedEnvironmentKeys) {
            [Environment]::SetEnvironmentVariable($key, $previous[$key], "Process")
        }
    }
}

function Get-RenderWindowHandle {
    param(
        [int]$ProcessId,
        [string]$Title
    )

    $script:renderWindowHandle = [IntPtr]::Zero
    $callback = [SelfEngineVisualQaWin32+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)

        $windowProcessId = [uint32]0
        [void][SelfEngineVisualQaWin32]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($windowProcessId -ne [uint32]$ProcessId -or -not [SelfEngineVisualQaWin32]::IsWindowVisible($hWnd)) {
            return $true
        }

        $titleBuilder = New-Object System.Text.StringBuilder 256
        [void][SelfEngineVisualQaWin32]::GetWindowText($hWnd, $titleBuilder, $titleBuilder.Capacity)
        if ($titleBuilder.ToString() -eq $Title) {
            $script:renderWindowHandle = $hWnd
            return $false
        }

        return $true
    }

    [void][SelfEngineVisualQaWin32]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:renderWindowHandle
}

function Assert-CleanLog {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        return
    }

    $matches = Select-String `
        -LiteralPath $Path `
        -Pattern "VUID|validation|error|failed|exception|shader" `
        -CaseSensitive:$false
    if ($matches) {
        throw "Log contains diagnostic matches: $Path"
    }
}

function Assert-CsvShape {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        throw "CSV not found: $Path"
    }
    $lines = Get-Content -LiteralPath $Path
    if ($lines.Count -lt 2) {
        throw "CSV has no captured rows: $Path"
    }
    $headerColumns = $lines[0].Split(',').Count
    $lastColumns = $lines[-1].Split(',').Count
    if ($headerColumns -ne $lastColumns) {
        throw "CSV column mismatch: $Path header=$headerColumns row=$lastColumns"
    }

    return [pscustomobject]@{
        HeaderColumns = $headerColumns
        LastColumns = $lastColumns
        LastRow = (Import-Csv -LiteralPath $Path | Select-Object -Last 1)
    }
}

function Get-NativeExitCode {
    $exitCodeVariable = Get-Variable -Name LASTEXITCODE -ErrorAction SilentlyContinue
    if ($null -eq $exitCodeVariable -or $null -eq $exitCodeVariable.Value) {
        return 0
    }

    return [int]$exitCodeVariable.Value
}

function Invoke-BenchmarkRun {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][hashtable]$Environment
    )

    $csvPath = Join-Path $outputRoot "$Name.csv"
    $logPath = Join-Path $outputRoot "$Name.log"
    $errPath = Join-Path $outputRoot "$Name.err.log"
    Remove-Item -LiteralPath $csvPath -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $logPath -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $errPath -ErrorAction SilentlyContinue
    $runEnvironment = $Environment.Clone()
    $runEnvironment["SE_BENCHMARK_SCENE"] = "grid"
    $runEnvironment["SE_BENCHMARK_WARMUP_FRAMES"] = "3"
    $runEnvironment["SE_BENCHMARK_FRAMES"] = "3"
    $runEnvironment["SE_BENCHMARK_CSV"] = $csvPath

    Invoke-WithEnvironment -Environment $runEnvironment -Script {
        $process = Start-Process `
            -FilePath $exePath `
            -WorkingDirectory $repoRoot `
            -PassThru `
            -Wait `
            -RedirectStandardOutput $logPath `
            -RedirectStandardError $errPath
        if ($process.ExitCode -ne 0) {
            throw "$Name benchmark failed with exit code $($process.ExitCode)"
        }
    }

    Assert-CleanLog -Path $logPath
    Assert-CleanLog -Path $errPath
    $shape = Assert-CsvShape -Path $csvPath
    return [pscustomobject]@{
        Name = $Name
        CsvPath = $csvPath
        LogPath = $logPath
        HeaderColumns = $shape.HeaderColumns
        LastColumns = $shape.LastColumns
        LastRow = $shape.LastRow
    }
}

function Capture-WindowImage {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][hashtable]$Environment
    )

    $imagePath = Join-Path $outputRoot "$Name.png"
    $stdoutPath = Join-Path $outputRoot "$Name.capture.out.log"
    $stderrPath = Join-Path $outputRoot "$Name.capture.err.log"
    Remove-Item -LiteralPath $imagePath -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stderrPath -ErrorAction SilentlyContinue
    $runEnvironment = $Environment.Clone()
    $runEnvironment["SE_AUTO_EXIT_FRAMES"] = "900"

    Invoke-WithEnvironment -Environment $runEnvironment -Script {
        $process = Start-Process `
            -FilePath $exePath `
            -WorkingDirectory $repoRoot `
            -PassThru `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath

        try {
            $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
            $windowHandle = [IntPtr]::Zero
            do {
                Start-Sleep -Milliseconds 250
                $process.Refresh()
                $windowHandle = Get-RenderWindowHandle -ProcessId $process.Id -Title $WindowTitle
            } while (
                $windowHandle -eq [IntPtr]::Zero -and
                (Get-Date) -lt $deadline -and
                -not $process.HasExited
            )

            if ($process.HasExited) {
                throw "$Name exited before capture with code $($process.ExitCode)"
            }
            if ($windowHandle -eq [IntPtr]::Zero) {
                throw "Could not find render window '$WindowTitle' for $Name"
            }

            $topMost = [IntPtr](-1)
            $showWindow = 0x0040
            [void][SelfEngineVisualQaWin32]::SetWindowPos($windowHandle, $topMost, 40, 40, 1038, 614, $showWindow)
            [void][SelfEngineVisualQaWin32]::SetForegroundWindow($windowHandle)
            Start-Sleep -Seconds $CaptureDelaySeconds

            $rect = New-Object SelfEngineVisualQaWin32+RECT
            [void][SelfEngineVisualQaWin32]::GetWindowRect($windowHandle, [ref]$rect)
            $width = $rect.Right - $rect.Left
            $height = $rect.Bottom - $rect.Top
            if ($width -le 0 -or $height -le 0) {
                throw "Invalid capture bounds for $Name`: ${width}x${height}"
            }

            $bitmap = New-Object System.Drawing.Bitmap $width, $height
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
            $bitmap.Save($imagePath, [System.Drawing.Imaging.ImageFormat]::Png)
            $graphics.Dispose()
            $bitmap.Dispose()
        } finally {
            if ($process -and -not $process.HasExited) {
                [void]$process.CloseMainWindow()
                Start-Sleep -Milliseconds 500
                $process.Refresh()
                if (-not $process.HasExited) {
                    $process.Kill()
                }
            }
        }
    }

    Assert-CleanLog -Path $stdoutPath
    Assert-CleanLog -Path $stderrPath
    return $imagePath
}

function Get-ImageVariationStats {
    param([Parameter(Mandatory = $true)][string]$Path)

    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $width = $bitmap.Width
        $height = $bitmap.Height
        $background = $bitmap.GetPixel([int]($width * 0.90), [int]($height * 0.90))
        $sampledPixels = 0
        $differentPixels = 0
        $maxDelta = 0
        $sumDelta = 0

        for ($y = [int]($height * 0.30); $y -lt [int]($height * 0.70); $y += 4) {
            for ($x = [int]($width * 0.25); $x -lt [int]($width * 0.75); $x += 4) {
                $color = $bitmap.GetPixel($x, $y)
                $delta =
                    [Math]::Abs($color.R - $background.R) +
                    [Math]::Abs($color.G - $background.G) +
                    [Math]::Abs($color.B - $background.B)
                if ($delta -gt 20) {
                    ++$differentPixels
                }
                if ($delta -gt $maxDelta) {
                    $maxDelta = $delta
                }
                $sumDelta += $delta
                ++$sampledPixels
            }
        }

        if ($differentPixels -le 0) {
            throw "Capture appears blank: $Path"
        }

        return [pscustomobject]@{
            Path = $Path
            Width = $width
            Height = $height
            SampledPixels = $sampledPixels
            DifferentPixels = $differentPixels
            MaxDelta = $maxDelta
            MeanDelta = [Math]::Round($sumDelta / [Math]::Max($sampledPixels, 1), 4)
        }
    } finally {
        $bitmap.Dispose()
    }
}

function Compare-Images {
    param(
        [Parameter(Mandatory = $true)][string]$A,
        [Parameter(Mandatory = $true)][string]$B
    )

    $bitmapA = [System.Drawing.Bitmap]::FromFile($A)
    $bitmapB = [System.Drawing.Bitmap]::FromFile($B)
    try {
        $width = [Math]::Min($bitmapA.Width, $bitmapB.Width)
        $height = [Math]::Min($bitmapA.Height, $bitmapB.Height)
        $sampledPixels = 0
        $changedPixels = 0
        $maxDelta = 0
        $sumDelta = 0

        for ($y = [int]($height * 0.20); $y -lt [int]($height * 0.80); $y += 4) {
            for ($x = [int]($width * 0.20); $x -lt [int]($width * 0.80); $x += 4) {
                $colorA = $bitmapA.GetPixel($x, $y)
                $colorB = $bitmapB.GetPixel($x, $y)
                $delta =
                    [Math]::Abs($colorA.R - $colorB.R) +
                    [Math]::Abs($colorA.G - $colorB.G) +
                    [Math]::Abs($colorA.B - $colorB.B)
                if ($delta -gt 6) {
                    ++$changedPixels
                }
                if ($delta -gt $maxDelta) {
                    $maxDelta = $delta
                }
                $sumDelta += $delta
                ++$sampledPixels
            }
        }

        if ($changedPixels -le 0) {
            throw "Native and DLSS-present captures are identical across sampled pixels"
        }

        return [pscustomobject]@{
            SampledPixels = $sampledPixels
            ChangedPixels = $changedPixels
            MaxDelta = $maxDelta
            MeanDelta = [Math]::Round($sumDelta / [Math]::Max($sampledPixels, 1), 4)
        }
    } finally {
        $bitmapA.Dispose()
        $bitmapB.Dispose()
    }
}

function Assert-BaselineText {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Actual,
        [Parameter(Mandatory = $true)][string]$Expected
    )

    if ($Actual -ne $Expected) {
        throw "Baseline mismatch for $Name`: expected '$Expected', got '$Actual'"
    }
}

function Assert-BaselineRange {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$Actual,
        [Parameter(Mandatory = $true)][double]$Min,
        [Parameter(Mandatory = $true)][double]$Max
    )

    if ($Actual -lt $Min -or $Actual -gt $Max) {
        throw "Baseline range mismatch for $Name`: expected [$Min, $Max], got $Actual"
    }
}

$nativeEnvironment = @{
    "SE_RENDER_SCALE" = "0.75"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$dlssPresentEnvironment = @{
    "SE_RENDER_SCALE" = "0.75"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_UPSCALER_PLUGIN" = "dlss"
    "SE_DLSS_PRESENT" = "1"
    "SE_DLSS_REFERENCE_BASELINE_PATH" = $baselineManifestPath
}

$nativeBenchmark = Invoke-BenchmarkRun -Name "native_deferred_hdr" -Environment $nativeEnvironment
$dlssBenchmark = Invoke-BenchmarkRun -Name "dlss_present" -Environment $dlssPresentEnvironment

$nativeRow = $nativeBenchmark.LastRow
$dlssRow = $dlssBenchmark.LastRow
if ($nativeRow.framegraph_validation_issues -ne "0") {
    throw "Native frame graph validation issues: $($nativeRow.framegraph_validation_issues)"
}
if ($nativeRow.temporal_upscale_post_source_active -ne "0") {
    throw "Native run unexpectedly activated temporal-upscale post source"
}
if ($dlssRow.framegraph_validation_issues -ne "0") {
    throw "DLSS frame graph validation issues: $($dlssRow.framegraph_validation_issues)"
}
if ($dlssRow.temporal_upscaler_dlss_output_ready -ne "1") {
    throw "DLSS-present run did not produce DLSS output"
}
if ($dlssRow.temporal_upscale_post_source_requested -ne "1" -or
    $dlssRow.temporal_upscale_post_source_active -ne "1" -or
    $dlssRow.temporal_upscale_post_source_fallback_reason -ne "0") {
    throw "DLSS-present post source did not activate cleanly"
}
if ($nativeRow.temporal_upscaler_dlss_quality_gate_requested -ne "0" -or
    $nativeRow.temporal_upscaler_dlss_quality_gate_ready -ne "0") {
    throw "Native run unexpectedly requested or passed the DLSS quality gate"
}
if ($dlssRow.temporal_upscaler_dlss_quality_gate_requested -ne "1") {
    throw "DLSS-present run did not request the DLSS quality gate"
}
if ($dlssRow.temporal_upscaler_dlss_quality_gate_ready -ne "1" -or
    $dlssRow.temporal_upscaler_dlss_quality_gate_fallback_reason -ne "0") {
    throw "DLSS-present run did not pass production DLSS quality gate"
}
if ($dlssRow.temporal_upscaler_dlss_quality_evaluate_output_ready -ne "1" -or
    $dlssRow.temporal_upscaler_dlss_quality_post_ordering_ready -ne "1") {
    throw "DLSS-present quality gate did not observe output/post-ordering readiness"
}
if ($dlssRow.temporal_upscaler_dlss_quality_reactive_mask_ready -ne "1" -or
    $dlssRow.temporal_upscaler_dlss_quality_transparency_mask_ready -ne "1") {
    throw "DLSS-present quality gate did not observe DLSS mask carriers"
}
if ($dlssRow.temporal_upscaler_dlss_quality_object_motion_ready -ne "1") {
    throw "DLSS-present quality gate did not observe object motion vectors"
}
if ($dlssRow.temporal_upscaler_dlss_quality_reference_baseline_ready -ne "1") {
    throw "DLSS-present quality gate did not observe the reference baseline"
}
$dlssQualityBlockerMask = [int]$dlssRow.temporal_upscaler_dlss_quality_blocker_mask
if ($dlssQualityBlockerMask -ne 0) {
    throw "DLSS-present quality gate still reports blockers: $dlssQualityBlockerMask"
}

$nativeImage = Capture-WindowImage -Name "native_deferred_hdr" -Environment $nativeEnvironment
$dlssImage = Capture-WindowImage -Name "dlss_present" -Environment $dlssPresentEnvironment
$nativeImageStats = Get-ImageVariationStats -Path $nativeImage
$dlssImageStats = Get-ImageVariationStats -Path $dlssImage
$comparison = Compare-Images -A $nativeImage -B $dlssImage
if ($comparison.ChangedPixels -lt $MinChangedPixels) {
    throw "Native/DLSS comparison changed only $($comparison.ChangedPixels) sampled pixels; expected at least $MinChangedPixels"
}
if ($comparison.MeanDelta -gt $MaxMeanDelta) {
    throw "Native/DLSS comparison mean delta $($comparison.MeanDelta) exceeded $MaxMeanDelta"
}

$nativePostSource =
    "$($nativeRow.temporal_upscale_post_source_requested)/$($nativeRow.temporal_upscale_post_source_active)/$($nativeRow.temporal_upscale_post_source_fallback_reason)"
$nativeQualityGate =
    "$($nativeRow.temporal_upscaler_dlss_quality_gate_requested)/$($nativeRow.temporal_upscaler_dlss_quality_gate_ready)/$($nativeRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$dlssEvaluateOutput =
    "$($dlssRow.temporal_upscaler_dlss_evaluate_result)/$($dlssRow.temporal_upscaler_dlss_output_ready)"
$dlssPostSource =
    "$($dlssRow.temporal_upscale_post_source_requested)/$($dlssRow.temporal_upscale_post_source_active)/$($dlssRow.temporal_upscale_post_source_fallback_reason)"
$dlssQualityGate =
    "$($dlssRow.temporal_upscaler_dlss_quality_gate_requested)/$($dlssRow.temporal_upscaler_dlss_quality_gate_ready)/$($dlssRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$dlssQualityMasks =
    "$($dlssRow.temporal_upscaler_dlss_quality_required_mask)/$($dlssRow.temporal_upscaler_dlss_quality_ready_mask)/$($dlssRow.temporal_upscaler_dlss_quality_blocker_mask)"
$dlssQualityInputs =
    "output/camera/object/reactive/transparency/exposure/post/baseline=$($dlssRow.temporal_upscaler_dlss_quality_evaluate_output_ready)/$($dlssRow.temporal_upscaler_dlss_quality_camera_motion_ready)/$($dlssRow.temporal_upscaler_dlss_quality_object_motion_ready)/$($dlssRow.temporal_upscaler_dlss_quality_reactive_mask_ready)/$($dlssRow.temporal_upscaler_dlss_quality_transparency_mask_ready)/$($dlssRow.temporal_upscaler_dlss_quality_exposure_policy_ready)/$($dlssRow.temporal_upscaler_dlss_quality_post_ordering_ready)/$($dlssRow.temporal_upscaler_dlss_quality_reference_baseline_ready)"

Assert-BaselineText -Name "native.postSource" -Actual $nativePostSource -Expected $baselineManifest.expected.native.postSource
Assert-BaselineText -Name "native.qualityGate" -Actual $nativeQualityGate -Expected $baselineManifest.expected.native.qualityGate
Assert-BaselineText -Name "dlssPresent.evaluateOutput" -Actual $dlssEvaluateOutput -Expected $baselineManifest.expected.dlssPresent.evaluateOutput
Assert-BaselineText -Name "dlssPresent.postSource" -Actual $dlssPostSource -Expected $baselineManifest.expected.dlssPresent.postSource
Assert-BaselineText -Name "dlssPresent.qualityGate" -Actual $dlssQualityGate -Expected $baselineManifest.expected.dlssPresent.qualityGate
Assert-BaselineText -Name "dlssPresent.qualityMasks" -Actual $dlssQualityMasks -Expected $baselineManifest.expected.dlssPresent.qualityMasks
Assert-BaselineText -Name "dlssPresent.qualityInputs" -Actual $dlssQualityInputs -Expected $baselineManifest.expected.dlssPresent.qualityInputs
Assert-BaselineRange `
    -Name "native.imageStats.differentPixels" `
    -Actual $nativeImageStats.DifferentPixels `
    -Min $baselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $nativeImageStats.SampledPixels
Assert-BaselineRange `
    -Name "dlssPresent.imageStats.differentPixels" `
    -Actual $dlssImageStats.DifferentPixels `
    -Min $baselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $dlssImageStats.SampledPixels
Assert-BaselineRange `
    -Name "comparison.changedPixels" `
    -Actual $comparison.ChangedPixels `
    -Min $baselineManifest.thresholds.comparisonChangedPixelsMin `
    -Max $baselineManifest.thresholds.comparisonChangedPixelsMax
Assert-BaselineRange `
    -Name "comparison.meanDelta" `
    -Actual $comparison.MeanDelta `
    -Min $baselineManifest.thresholds.comparisonMeanDeltaMin `
    -Max $baselineManifest.thresholds.comparisonMeanDeltaMax
Assert-BaselineRange `
    -Name "comparison.maxDelta" `
    -Actual $comparison.MaxDelta `
    -Min 0 `
    -Max $baselineManifest.thresholds.comparisonMaxDeltaMax

$summary = [pscustomobject]@{
    target = $Target
    generatedAt = (Get-Date).ToString("o")
    baseline = [pscustomobject]@{
        manifest = $baselineManifestPath
        name = $baselineManifest.name
        schemaVersion = [int]$baselineManifest.schemaVersion
    }
    thresholds = [pscustomobject]@{
        minChangedPixels = $MinChangedPixels
        maxMeanDelta = $MaxMeanDelta
        comparisonChangedPixelsMin = [int]$baselineManifest.thresholds.comparisonChangedPixelsMin
        comparisonChangedPixelsMax = [int]$baselineManifest.thresholds.comparisonChangedPixelsMax
        comparisonMeanDeltaMin = [double]$baselineManifest.thresholds.comparisonMeanDeltaMin
        comparisonMeanDeltaMax = [double]$baselineManifest.thresholds.comparisonMeanDeltaMax
        comparisonMaxDeltaMax = [int]$baselineManifest.thresholds.comparisonMaxDeltaMax
    }
    native = [pscustomobject]@{
        csv = $nativeBenchmark.CsvPath
        image = $nativeImage
        columns = "$($nativeBenchmark.HeaderColumns)/$($nativeBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$nativeRow.framegraph_validation_issues
        postSource = $nativePostSource
        qualityGate = $nativeQualityGate
        imageStats = $nativeImageStats
    }
    dlssPresent = [pscustomobject]@{
        csv = $dlssBenchmark.CsvPath
        image = $dlssImage
        columns = "$($dlssBenchmark.HeaderColumns)/$($dlssBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$dlssRow.framegraph_validation_issues
        evaluateOutput = $dlssEvaluateOutput
        postSource = $dlssPostSource
        qualityGate = $dlssQualityGate
        qualityMasks = $dlssQualityMasks
        qualityInputs = $dlssQualityInputs
        imageStats = $dlssImageStats
    }
    comparison = $comparison
}

$summaryPath = Join-Path $outputRoot "summary.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host "DLSS visual QA passed"
Write-Host "  summary: $summaryPath"
Write-Host "  native:  $nativeImage"
Write-Host "  dlss:    $dlssImage"
Write-Host "  diff: sampled=$($comparison.SampledPixels) changed=$($comparison.ChangedPixels) mean=$($comparison.MeanDelta) max=$($comparison.MaxDelta)"
