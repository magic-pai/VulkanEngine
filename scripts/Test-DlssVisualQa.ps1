param(
    [string]$Target = "SelfEngineForward3D",
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$WindowTitle = "SelfEngine Forward 3D",
    [string]$OutputDirectory = "out\reference_captures\dlss_visual_qa",
    [int]$TimeoutSeconds = 15,
    [int]$CaptureDelaySeconds = 8,
    [int]$MinChangedPixels = 128,
    [double]$MaxMeanDelta = 160.0,
    [string]$BaselinePath = "docs\reference_baselines\dlss_visual_qa_baseline.json",
    [string]$WboitBaselinePath = "docs\reference_baselines\dlss_wboit_visual_qa_baseline.json",
    [string]$ForwardSpecialBaselinePath = "docs\reference_baselines\dlss_forward_special_visual_qa_baseline.json",
    [string]$MaterialStressBaselinePath = "docs\reference_baselines\dlss_material_stress_visual_qa_baseline.json",
    [string]$DlaaBaselinePath = "docs\reference_baselines\dlss_dlaa_visual_qa_baseline.json",
    [string]$DefaultSceneDlaaBaselinePath = "docs\reference_baselines\dlss_default_scene_dlaa_visual_qa_baseline.json",
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
$wboitBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($WboitBaselinePath)) {
    [System.IO.Path]::GetFullPath($WboitBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $WboitBaselinePath))
}
if (!(Test-Path -LiteralPath $wboitBaselineManifestPath)) {
    throw "DLSS WBOIT visual QA baseline manifest not found: $wboitBaselineManifestPath"
}
$wboitBaselineManifest = Get-Content -Raw -LiteralPath $wboitBaselineManifestPath | ConvertFrom-Json
if ($wboitBaselineManifest.target -ne $Target) {
    throw "DLSS WBOIT visual QA baseline target mismatch: expected $Target, manifest has $($wboitBaselineManifest.target)"
}
$forwardSpecialBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($ForwardSpecialBaselinePath)) {
    [System.IO.Path]::GetFullPath($ForwardSpecialBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $ForwardSpecialBaselinePath))
}
if (!(Test-Path -LiteralPath $forwardSpecialBaselineManifestPath)) {
    throw "DLSS forward-special visual QA baseline manifest not found: $forwardSpecialBaselineManifestPath"
}
$forwardSpecialBaselineManifest = Get-Content -Raw -LiteralPath $forwardSpecialBaselineManifestPath | ConvertFrom-Json
if ($forwardSpecialBaselineManifest.target -ne $Target) {
    throw "DLSS forward-special visual QA baseline target mismatch: expected $Target, manifest has $($forwardSpecialBaselineManifest.target)"
}
$materialStressBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($MaterialStressBaselinePath)) {
    [System.IO.Path]::GetFullPath($MaterialStressBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $MaterialStressBaselinePath))
}
if (!(Test-Path -LiteralPath $materialStressBaselineManifestPath)) {
    throw "DLSS material-stress visual QA baseline manifest not found: $materialStressBaselineManifestPath"
}
$materialStressBaselineManifest = Get-Content -Raw -LiteralPath $materialStressBaselineManifestPath | ConvertFrom-Json
if ($materialStressBaselineManifest.target -ne $Target) {
    throw "DLSS material-stress visual QA baseline target mismatch: expected $Target, manifest has $($materialStressBaselineManifest.target)"
}
$dlaaBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($DlaaBaselinePath)) {
    [System.IO.Path]::GetFullPath($DlaaBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $DlaaBaselinePath))
}
if (!(Test-Path -LiteralPath $dlaaBaselineManifestPath)) {
    throw "DLAA visual QA baseline manifest not found: $dlaaBaselineManifestPath"
}
$dlaaBaselineManifest = Get-Content -Raw -LiteralPath $dlaaBaselineManifestPath | ConvertFrom-Json
if ($dlaaBaselineManifest.target -ne $Target) {
    throw "DLAA visual QA baseline target mismatch: expected $Target, manifest has $($dlaaBaselineManifest.target)"
}
$defaultSceneDlaaBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($DefaultSceneDlaaBaselinePath)) {
    [System.IO.Path]::GetFullPath($DefaultSceneDlaaBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $DefaultSceneDlaaBaselinePath))
}
if (!(Test-Path -LiteralPath $defaultSceneDlaaBaselineManifestPath)) {
    throw "Default-scene DLAA visual QA baseline manifest not found: $defaultSceneDlaaBaselineManifestPath"
}
$defaultSceneDlaaBaselineManifest =
    Get-Content -Raw -LiteralPath $defaultSceneDlaaBaselineManifestPath | ConvertFrom-Json
if ($defaultSceneDlaaBaselineManifest.target -ne $Target) {
    throw "Default-scene DLAA visual QA baseline target mismatch: expected $Target, manifest has $($defaultSceneDlaaBaselineManifest.target)"
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
    "SE_BENCHMARK_TRANSPARENT_MATERIAL",
    "SE_BENCHMARK_FORWARD_SPECIAL_MATERIAL",
    "SE_BENCHMARK_SPECULAR_TEXTURE_MATERIAL",
    "SE_BENCHMARK_UV_TRANSFORM_MATERIAL",
    "SE_BENCHMARK_DOUBLE_SIDED_MATERIAL",
    "SE_BENCHMARK_CLEARCOAT_TEXTURE_MATERIAL",
    "SE_BENCHMARK_CLEARCOAT_ROUGHNESS_TEXTURE_MATERIAL",
    "SE_BENCHMARK_TRANSMISSION_TEXTURE_MATERIAL",
    "SE_BENCHMARK_VOLUME_MATERIAL",
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
    "SE_DLSS_QUALITY",
    "SE_DLSS_MODE",
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
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [switch]$UseApplicationScene
    )

    $csvPath = Join-Path $outputRoot "$Name.csv"
    $logPath = Join-Path $outputRoot "$Name.log"
    $errPath = Join-Path $outputRoot "$Name.err.log"
    Remove-Item -LiteralPath $csvPath -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $logPath -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $errPath -ErrorAction SilentlyContinue
    $runEnvironment = $Environment.Clone()
    if (-not $UseApplicationScene -and -not $runEnvironment.ContainsKey("SE_BENCHMARK_SCENE")) {
        $runEnvironment["SE_BENCHMARK_SCENE"] = "grid"
    }
    $runEnvironment["SE_BENCHMARK_WARMUP_FRAMES"] = "3"
    $runEnvironment["SE_BENCHMARK_FRAMES"] = "3"
    $runEnvironment["SE_BENCHMARK_CSV"] = $csvPath

    Invoke-WithEnvironment -Environment $runEnvironment -Script {
        Push-Location $repoRoot
        try {
            $output = & $exePath 2>&1
            $exitCode = Get-NativeExitCode
        } finally {
            Pop-Location
        }

        if ($output) {
            $output | Set-Content -Encoding UTF8 -LiteralPath $logPath
        } else {
            New-Item -ItemType File -Force -Path $logPath | Out-Null
        }
        New-Item -ItemType File -Force -Path $errPath | Out-Null
        if ($exitCode -ne 0) {
            throw "$Name benchmark failed with exit code $exitCode"
        }
    }

    Assert-CleanLog -Path $logPath
    Assert-CleanLog -Path $errPath

    $shape = $null
    $shapeError = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        try {
            $shape = Assert-CsvShape -Path $csvPath
            break
        } catch {
            $shapeError = $_
            Start-Sleep -Milliseconds 100
        }
    } while ((Get-Date) -lt $deadline)

    if ($null -eq $shape) {
        if ($null -ne $shapeError) {
            throw $shapeError
        }
        throw "CSV not found: $csvPath"
    }

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
    "SE_BENCHMARK_SCENE" = "grid"
    "SE_RENDER_SCALE" = "0.75"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$dlssPresentEnvironment = @{
    "SE_BENCHMARK_SCENE" = "grid"
    "SE_RENDER_SCALE" = "0.75"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_UPSCALER_PLUGIN" = "dlss"
    "SE_DLSS_PRESENT" = "1"
    "SE_DLSS_REFERENCE_BASELINE_PATH" = $baselineManifestPath
}
$dlaaNativeEnvironment = @{
    "SE_BENCHMARK_SCENE" = "grid"
    "SE_RENDER_SCALE" = "1.0"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$dlaaPresentEnvironment = @{
    "SE_BENCHMARK_SCENE" = "grid"
    "SE_RENDER_SCALE" = "1.0"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_UPSCALER_PLUGIN" = "dlss"
    "SE_DLSS_QUALITY" = "dlaa"
    "SE_DLSS_PRESENT" = "1"
    "SE_DLSS_REFERENCE_BASELINE_PATH" = $dlaaBaselineManifestPath
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$defaultSceneDlaaNativeEnvironment = @{
    "SE_RENDER_SCALE" = "1.0"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$defaultSceneDlaaPresentEnvironment = @{
    "SE_RENDER_SCALE" = "1.0"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_UPSCALER_PLUGIN" = "dlss"
    "SE_DLSS_QUALITY" = "dlaa"
    "SE_DLSS_PRESENT" = "1"
    "SE_DLSS_REFERENCE_BASELINE_PATH" = $defaultSceneDlaaBaselineManifestPath
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$wboitNativeEnvironment = @{
    "SE_BENCHMARK_SCENE" = "grid"
    "SE_BENCHMARK_TRANSPARENT_MATERIAL" = "1"
    "SE_RENDER_SCALE" = "0.75"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$wboitDlssPresentEnvironment = @{
    "SE_BENCHMARK_SCENE" = "grid"
    "SE_BENCHMARK_TRANSPARENT_MATERIAL" = "1"
    "SE_RENDER_SCALE" = "0.75"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_UPSCALER_PLUGIN" = "dlss"
    "SE_DLSS_PRESENT" = "1"
    "SE_DLSS_REFERENCE_BASELINE_PATH" = $wboitBaselineManifestPath
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$forwardSpecialNativeEnvironment = @{
    "SE_BENCHMARK_SCENE" = "grid"
    "SE_BENCHMARK_FORWARD_SPECIAL_MATERIAL" = "1"
    "SE_RENDER_SCALE" = "0.75"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$forwardSpecialDlssPresentEnvironment = @{
    "SE_BENCHMARK_SCENE" = "grid"
    "SE_BENCHMARK_FORWARD_SPECIAL_MATERIAL" = "1"
    "SE_RENDER_SCALE" = "0.75"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_UPSCALER_PLUGIN" = "dlss"
    "SE_DLSS_PRESENT" = "1"
    "SE_DLSS_REFERENCE_BASELINE_PATH" = $forwardSpecialBaselineManifestPath
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$materialStressEnvironmentBase = @{
    "SE_BENCHMARK_SCENE" = "grid"
    "SE_BENCHMARK_SPECULAR_TEXTURE_MATERIAL" = "1"
    "SE_BENCHMARK_UV_TRANSFORM_MATERIAL" = "1"
    "SE_BENCHMARK_DOUBLE_SIDED_MATERIAL" = "1"
    "SE_BENCHMARK_CLEARCOAT_TEXTURE_MATERIAL" = "1"
    "SE_BENCHMARK_CLEARCOAT_ROUGHNESS_TEXTURE_MATERIAL" = "1"
    "SE_BENCHMARK_TRANSMISSION_TEXTURE_MATERIAL" = "1"
    "SE_BENCHMARK_VOLUME_MATERIAL" = "1"
    "SE_RENDER_SCALE" = "0.75"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$materialStressNativeEnvironment = $materialStressEnvironmentBase.Clone()
$materialStressDlssPresentEnvironment = $materialStressEnvironmentBase.Clone()
$materialStressDlssPresentEnvironment["SE_UPSCALER_PLUGIN"] = "dlss"
$materialStressDlssPresentEnvironment["SE_DLSS_PRESENT"] = "1"
$materialStressDlssPresentEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $materialStressBaselineManifestPath

$nativeBenchmark = Invoke-BenchmarkRun -Name "native_deferred_hdr" -Environment $nativeEnvironment
$dlssBenchmark = Invoke-BenchmarkRun -Name "dlss_present" -Environment $dlssPresentEnvironment
$dlaaNativeBenchmark = Invoke-BenchmarkRun -Name "dlaa_native_deferred_hdr" -Environment $dlaaNativeEnvironment
$dlaaBenchmark = Invoke-BenchmarkRun -Name "dlaa_present" -Environment $dlaaPresentEnvironment
$defaultSceneDlaaNativeBenchmark = Invoke-BenchmarkRun `
    -Name "default_scene_dlaa_native_deferred_hdr" `
    -Environment $defaultSceneDlaaNativeEnvironment `
    -UseApplicationScene
$defaultSceneDlaaBenchmark = Invoke-BenchmarkRun `
    -Name "default_scene_dlaa_present" `
    -Environment $defaultSceneDlaaPresentEnvironment `
    -UseApplicationScene
$wboitNativeBenchmark = Invoke-BenchmarkRun -Name "wboit_native_deferred_hdr" -Environment $wboitNativeEnvironment
$wboitDlssBenchmark = Invoke-BenchmarkRun -Name "wboit_dlss_present" -Environment $wboitDlssPresentEnvironment
$forwardSpecialNativeBenchmark = Invoke-BenchmarkRun -Name "forward_special_native_deferred_hdr" -Environment $forwardSpecialNativeEnvironment
$forwardSpecialDlssBenchmark = Invoke-BenchmarkRun -Name "forward_special_dlss_present" -Environment $forwardSpecialDlssPresentEnvironment
$materialStressNativeBenchmark = Invoke-BenchmarkRun -Name "material_stress_native_deferred_hdr" -Environment $materialStressNativeEnvironment
$materialStressDlssBenchmark = Invoke-BenchmarkRun -Name "material_stress_dlss_present" -Environment $materialStressDlssPresentEnvironment

$nativeRow = $nativeBenchmark.LastRow
$dlssRow = $dlssBenchmark.LastRow
$dlaaNativeRow = $dlaaNativeBenchmark.LastRow
$dlaaRow = $dlaaBenchmark.LastRow
$defaultSceneDlaaNativeRow = $defaultSceneDlaaNativeBenchmark.LastRow
$defaultSceneDlaaRow = $defaultSceneDlaaBenchmark.LastRow
$wboitNativeRow = $wboitNativeBenchmark.LastRow
$wboitDlssRow = $wboitDlssBenchmark.LastRow
$forwardSpecialNativeRow = $forwardSpecialNativeBenchmark.LastRow
$forwardSpecialDlssRow = $forwardSpecialDlssBenchmark.LastRow
$materialStressNativeRow = $materialStressNativeBenchmark.LastRow
$materialStressDlssRow = $materialStressDlssBenchmark.LastRow
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

if ($dlaaNativeRow.framegraph_validation_issues -ne "0") {
    throw "DLAA native frame graph validation issues: $($dlaaNativeRow.framegraph_validation_issues)"
}
if ($dlaaNativeRow.temporal_upscale_post_source_active -ne "0") {
    throw "DLAA native run unexpectedly activated temporal-upscale post source"
}
if ($dlaaNativeRow.temporal_render_scale_active -ne $dlaaBaselineManifest.expected.native.renderScaleActive -or
    $dlaaNativeRow.temporal_render_scale_applied -ne $dlaaBaselineManifest.expected.native.renderScaleApplied) {
    throw "DLAA native render-scale state mismatch"
}
if ($dlaaRow.framegraph_validation_issues -ne "0") {
    throw "DLAA frame graph validation issues: $($dlaaRow.framegraph_validation_issues)"
}
if ($dlaaRow.temporal_upscaler_dlss_quality_mode -ne $dlaaBaselineManifest.expected.dlssPresent.qualityMode -or
    $dlaaRow.temporal_upscaler_dlss_recommended_preset -ne $dlaaBaselineManifest.expected.dlssPresent.recommendedPreset) {
    throw "DLAA run did not select the expected DLSS quality mode/preset"
}
if ($dlaaRow.temporal_render_scale_active -ne $dlaaBaselineManifest.expected.dlssPresent.renderScaleActive -or
    $dlaaRow.temporal_render_scale_applied -ne $dlaaBaselineManifest.expected.dlssPresent.renderScaleApplied) {
    throw "DLAA run did not stay on the full-resolution render path"
}
if ($dlaaRow.temporal_upscaler_dlss_render_width -ne $dlaaRow.temporal_upscale_display_width -or
    $dlaaRow.temporal_upscaler_dlss_render_height -ne $dlaaRow.temporal_upscale_display_height -or
    $dlaaRow.temporal_upscaler_dlss_output_width -ne $dlaaRow.temporal_upscale_display_width -or
    $dlaaRow.temporal_upscaler_dlss_output_height -ne $dlaaRow.temporal_upscale_display_height) {
    throw "DLAA run did not evaluate at full display resolution"
}
if ($dlaaRow.temporal_upscaler_dlss_output_ready -ne "1" -or
    $dlaaRow.temporal_upscale_post_source_requested -ne "1" -or
    $dlaaRow.temporal_upscale_post_source_active -ne "1" -or
    $dlaaRow.temporal_upscale_post_source_fallback_reason -ne "0") {
    throw "DLAA-present run did not produce visible DLSS output"
}
if ($dlaaRow.temporal_upscaler_dlss_quality_gate_ready -ne "1" -or
    $dlaaRow.temporal_upscaler_dlss_quality_gate_fallback_reason -ne "0") {
    throw "DLAA quality gate did not pass"
}
if ($dlaaRow.temporal_upscaler_dlss_quality_object_motion_ready -ne "1" -or
    $dlaaRow.temporal_upscaler_dlss_quality_reference_baseline_ready -ne "1") {
    throw "DLAA quality gate did not report object/baseline readiness"
}

if ($defaultSceneDlaaNativeRow.framegraph_validation_issues -ne "0") {
    throw "Default-scene DLAA native frame graph validation issues: $($defaultSceneDlaaNativeRow.framegraph_validation_issues)"
}
if ($defaultSceneDlaaNativeRow.temporal_upscale_post_source_active -ne "0") {
    throw "Default-scene DLAA native run unexpectedly activated temporal-upscale post source"
}
if ($defaultSceneDlaaNativeRow.temporal_render_scale_active -ne $defaultSceneDlaaBaselineManifest.expected.native.renderScaleActive -or
    $defaultSceneDlaaNativeRow.temporal_render_scale_applied -ne $defaultSceneDlaaBaselineManifest.expected.native.renderScaleApplied) {
    throw "Default-scene DLAA native render-scale state mismatch"
}
if ($defaultSceneDlaaNativeRow.main_draws -ne $defaultSceneDlaaBaselineManifest.expected.native.mainDraws -or
    $defaultSceneDlaaNativeRow.gbuffer_draws -ne $defaultSceneDlaaBaselineManifest.expected.native.gbufferDraws -or
    $defaultSceneDlaaNativeRow.forward_residual_draws -ne $defaultSceneDlaaBaselineManifest.expected.native.forwardResidualDraws -or
    $defaultSceneDlaaNativeRow.weighted_translucency_draws -ne $defaultSceneDlaaBaselineManifest.expected.native.weightedTranslucencyDraws) {
    throw "Default-scene DLAA native draw route mismatch"
}
if ($defaultSceneDlaaNativeRow.frame_light_total_count -ne $defaultSceneDlaaBaselineManifest.expected.native.frameLightTotalCount -or
    $defaultSceneDlaaNativeRow.frame_local_light_count -ne $defaultSceneDlaaBaselineManifest.expected.native.frameLocalLightCount -or
    $defaultSceneDlaaNativeRow.frame_rect_light_count -ne $defaultSceneDlaaBaselineManifest.expected.native.frameRectLightCount -or
    $defaultSceneDlaaNativeRow.reflection_probe_scene_probe_count -ne $defaultSceneDlaaBaselineManifest.expected.native.reflectionProbeSceneProbeCount) {
    throw "Default-scene DLAA native scene-light/probe counters mismatch"
}
if ($defaultSceneDlaaRow.framegraph_validation_issues -ne "0") {
    throw "Default-scene DLAA frame graph validation issues: $($defaultSceneDlaaRow.framegraph_validation_issues)"
}
if ($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_mode -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.qualityMode -or
    $defaultSceneDlaaRow.temporal_upscaler_dlss_recommended_preset -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.recommendedPreset) {
    throw "Default-scene DLAA did not select the expected DLSS quality mode/preset"
}
if ($defaultSceneDlaaRow.temporal_render_scale_active -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.renderScaleActive -or
    $defaultSceneDlaaRow.temporal_render_scale_applied -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.renderScaleApplied) {
    throw "Default-scene DLAA did not stay on the full-resolution render path"
}
if ($defaultSceneDlaaRow.temporal_upscaler_dlss_render_width -ne $defaultSceneDlaaRow.temporal_upscale_display_width -or
    $defaultSceneDlaaRow.temporal_upscaler_dlss_render_height -ne $defaultSceneDlaaRow.temporal_upscale_display_height -or
    $defaultSceneDlaaRow.temporal_upscaler_dlss_output_width -ne $defaultSceneDlaaRow.temporal_upscale_display_width -or
    $defaultSceneDlaaRow.temporal_upscaler_dlss_output_height -ne $defaultSceneDlaaRow.temporal_upscale_display_height) {
    throw "Default-scene DLAA did not evaluate at full display resolution"
}
if ($defaultSceneDlaaRow.temporal_upscaler_dlss_output_ready -ne "1" -or
    $defaultSceneDlaaRow.temporal_upscale_post_source_requested -ne "1" -or
    $defaultSceneDlaaRow.temporal_upscale_post_source_active -ne "1" -or
    $defaultSceneDlaaRow.temporal_upscale_post_source_fallback_reason -ne "0") {
    throw "Default-scene DLAA-present run did not produce visible DLSS output"
}
if ($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_gate_ready -ne "1" -or
    $defaultSceneDlaaRow.temporal_upscaler_dlss_quality_gate_fallback_reason -ne "0") {
    throw "Default-scene DLAA quality gate did not pass"
}
if ($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_object_motion_ready -ne "1" -or
    $defaultSceneDlaaRow.temporal_upscaler_dlss_quality_reference_baseline_ready -ne "1") {
    throw "Default-scene DLAA quality gate did not report object/baseline readiness"
}
if ($defaultSceneDlaaRow.main_draws -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.mainDraws -or
    $defaultSceneDlaaRow.gbuffer_draws -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.gbufferDraws -or
    $defaultSceneDlaaRow.forward_residual_draws -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.forwardResidualDraws -or
    $defaultSceneDlaaRow.weighted_translucency_draws -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.weightedTranslucencyDraws) {
    throw "Default-scene DLAA draw route mismatch"
}
if ($defaultSceneDlaaRow.frame_light_total_count -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.frameLightTotalCount -or
    $defaultSceneDlaaRow.frame_local_light_count -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.frameLocalLightCount -or
    $defaultSceneDlaaRow.frame_rect_light_count -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.frameRectLightCount -or
    $defaultSceneDlaaRow.reflection_probe_scene_probe_count -ne $defaultSceneDlaaBaselineManifest.expected.dlssPresent.reflectionProbeSceneProbeCount) {
    throw "Default-scene DLAA scene-light/probe counters mismatch"
}

if ($wboitNativeRow.framegraph_validation_issues -ne "0") {
    throw "WBOIT native frame graph validation issues: $($wboitNativeRow.framegraph_validation_issues)"
}
if ($wboitDlssRow.framegraph_validation_issues -ne "0") {
    throw "WBOIT DLSS frame graph validation issues: $($wboitDlssRow.framegraph_validation_issues)"
}
if ($wboitNativeRow.weighted_translucency_draws -ne $wboitBaselineManifest.expected.native.weightedTranslucencyDraws) {
    throw "WBOIT native draw count mismatch: $($wboitNativeRow.weighted_translucency_draws)"
}
if ($wboitDlssRow.temporal_upscaler_dlss_output_ready -ne "1" -or
    $wboitDlssRow.temporal_upscale_post_source_active -ne "1") {
    throw "WBOIT DLSS-present run did not produce visible DLSS output"
}
if ($wboitDlssRow.weighted_translucency_draws -ne $wboitBaselineManifest.expected.dlssPresent.weightedTranslucencyDraws -or
    $wboitDlssRow.weighted_translucency_resolve_draws -ne $wboitBaselineManifest.expected.dlssPresent.weightedTranslucencyResolveDraws -or
    $wboitDlssRow.weighted_translucency_velocity_draws -ne $wboitBaselineManifest.expected.dlssPresent.weightedTranslucencyVelocityDraws) {
    throw "WBOIT DLSS draw/resolve count mismatch"
}
if ($wboitDlssRow.dlss_mask_weighted_translucency_draws -ne $wboitBaselineManifest.expected.dlssPresent.dlssMaskWeightedTranslucencyDraws -or
    $wboitDlssRow.dlss_mask_forward_residual_draws -ne $wboitBaselineManifest.expected.dlssPresent.dlssMaskForwardResidualDraws) {
    throw "WBOIT DLSS mask draw count mismatch"
}
if ($wboitDlssRow.forward_residual_draws -ne $wboitBaselineManifest.expected.dlssPresent.forwardResidualDraws) {
    throw "WBOIT DLSS unexpectedly used forward residual draws"
}
if ($wboitDlssRow.temporal_upscaler_dlss_quality_gate_ready -ne "1" -or
    $wboitDlssRow.temporal_upscaler_dlss_quality_gate_fallback_reason -ne "0") {
    throw "WBOIT DLSS quality gate did not pass"
}
if ($wboitDlssRow.temporal_upscaler_dlss_quality_object_motion_ready -ne "1" -or
    $wboitDlssRow.temporal_upscaler_dlss_quality_reference_baseline_ready -ne "1") {
    throw "WBOIT DLSS quality gate did not report the expected object/baseline readiness"
}

if ($forwardSpecialNativeRow.framegraph_validation_issues -ne "0") {
    throw "Forward-special native frame graph validation issues: $($forwardSpecialNativeRow.framegraph_validation_issues)"
}
if ($forwardSpecialDlssRow.framegraph_validation_issues -ne "0") {
    throw "Forward-special DLSS frame graph validation issues: $($forwardSpecialDlssRow.framegraph_validation_issues)"
}
if ($forwardSpecialNativeRow.frame_material_forward_special_count -ne $forwardSpecialBaselineManifest.expected.native.forwardSpecialMaterialCount -or
    $forwardSpecialNativeRow.hybrid_forward_special_draws -ne $forwardSpecialBaselineManifest.expected.native.hybridForwardSpecialDraws -or
    $forwardSpecialNativeRow.forward_residual_draws -ne $forwardSpecialBaselineManifest.expected.native.forwardResidualDraws) {
    throw "Forward-special native draw/material count mismatch"
}
if ($forwardSpecialDlssRow.temporal_upscaler_dlss_output_ready -ne "1" -or
    $forwardSpecialDlssRow.temporal_upscale_post_source_active -ne "1") {
    throw "Forward-special DLSS-present run did not produce visible DLSS output"
}
if ($forwardSpecialDlssRow.frame_material_forward_special_count -ne $forwardSpecialBaselineManifest.expected.dlssPresent.forwardSpecialMaterialCount -or
    $forwardSpecialDlssRow.hybrid_forward_special_draws -ne $forwardSpecialBaselineManifest.expected.dlssPresent.hybridForwardSpecialDraws -or
    $forwardSpecialDlssRow.forward_residual_draws -ne $forwardSpecialBaselineManifest.expected.dlssPresent.forwardResidualDraws -or
    $forwardSpecialDlssRow.forward_residual_shared_light_list_draws -ne $forwardSpecialBaselineManifest.expected.dlssPresent.forwardResidualSharedLightListDraws -or
    $forwardSpecialDlssRow.forward_residual_velocity_draws -ne $forwardSpecialBaselineManifest.expected.dlssPresent.forwardResidualVelocityDraws) {
    throw "Forward-special DLSS draw/material count mismatch"
}
if ($forwardSpecialDlssRow.dlss_mask_forward_residual_draws -ne $forwardSpecialBaselineManifest.expected.dlssPresent.dlssMaskForwardResidualDraws -or
    $forwardSpecialDlssRow.dlss_mask_weighted_translucency_draws -ne $forwardSpecialBaselineManifest.expected.dlssPresent.dlssMaskWeightedTranslucencyDraws) {
    throw "Forward-special DLSS mask draw count mismatch"
}
if ($forwardSpecialDlssRow.weighted_translucency_draws -ne $forwardSpecialBaselineManifest.expected.dlssPresent.weightedTranslucencyDraws) {
    throw "Forward-special DLSS unexpectedly used WBOIT draws"
}
if ($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_gate_ready -ne "1" -or
    $forwardSpecialDlssRow.temporal_upscaler_dlss_quality_gate_fallback_reason -ne "0") {
    throw "Forward-special DLSS quality gate did not pass"
}
if ($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_object_motion_ready -ne "1" -or
    $forwardSpecialDlssRow.temporal_upscaler_dlss_quality_reference_baseline_ready -ne "1") {
    throw "Forward-special DLSS quality gate did not report the expected object/baseline readiness"
}

if ($materialStressNativeRow.framegraph_validation_issues -ne "0") {
    throw "Material-stress native frame graph validation issues: $($materialStressNativeRow.framegraph_validation_issues)"
}
if ($materialStressDlssRow.framegraph_validation_issues -ne "0") {
    throw "Material-stress DLSS frame graph validation issues: $($materialStressDlssRow.framegraph_validation_issues)"
}
if ($materialStressNativeRow.gbuffer_draws -ne $materialStressBaselineManifest.expected.native.gbufferDraws -or
    $materialStressNativeRow.forward_residual_draws -ne $materialStressBaselineManifest.expected.native.forwardResidualDraws -or
    $materialStressNativeRow.weighted_translucency_draws -ne $materialStressBaselineManifest.expected.native.weightedTranslucencyDraws) {
    throw "Material-stress native draw route mismatch"
}
if ($materialStressDlssRow.temporal_upscaler_dlss_output_ready -ne "1" -or
    $materialStressDlssRow.temporal_upscale_post_source_active -ne "1") {
    throw "Material-stress DLSS-present run did not produce visible DLSS output"
}
if ($materialStressDlssRow.gbuffer_draws -ne $materialStressBaselineManifest.expected.dlssPresent.gbufferDraws -or
    $materialStressDlssRow.forward_residual_draws -ne $materialStressBaselineManifest.expected.dlssPresent.forwardResidualDraws -or
    $materialStressDlssRow.weighted_translucency_draws -ne $materialStressBaselineManifest.expected.dlssPresent.weightedTranslucencyDraws) {
    throw "Material-stress DLSS draw route mismatch"
}
if ($materialStressDlssRow.dlss_mask_draws -ne $materialStressBaselineManifest.expected.dlssPresent.dlssMaskDraws) {
    throw "Material-stress DLSS unexpectedly rendered DLSS mask geometry"
}
if ($materialStressDlssRow.temporal_upscaler_dlss_quality_gate_ready -ne "1" -or
    $materialStressDlssRow.temporal_upscaler_dlss_quality_gate_fallback_reason -ne "0") {
    throw "Material-stress DLSS quality gate did not pass"
}
if ($materialStressDlssRow.temporal_upscaler_dlss_quality_object_motion_ready -ne "1" -or
    $materialStressDlssRow.temporal_upscaler_dlss_quality_reference_baseline_ready -ne "1") {
    throw "Material-stress DLSS quality gate did not report object/baseline readiness"
}
if ($materialStressDlssRow.frame_material_specular_texture_count -ne $materialStressBaselineManifest.expected.dlssPresent.specularTextureMaterials -or
    $materialStressDlssRow.frame_material_uv_transform_count -ne $materialStressBaselineManifest.expected.dlssPresent.uvTransformMaterials -or
    $materialStressDlssRow.frame_material_double_sided_count -ne $materialStressBaselineManifest.expected.dlssPresent.doubleSidedMaterials -or
    $materialStressDlssRow.frame_material_clearcoat_texture_count -ne $materialStressBaselineManifest.expected.dlssPresent.clearcoatTextureMaterials -or
    $materialStressDlssRow.frame_material_clearcoat_roughness_texture_count -ne $materialStressBaselineManifest.expected.dlssPresent.clearcoatRoughnessTextureMaterials -or
    $materialStressDlssRow.frame_material_transmission_texture_count -ne $materialStressBaselineManifest.expected.dlssPresent.transmissionTextureMaterials -or
    $materialStressDlssRow.frame_material_volume_count -ne $materialStressBaselineManifest.expected.dlssPresent.volumeMaterials) {
    throw "Material-stress DLSS material counter mismatch"
}

$nativeImage = Capture-WindowImage -Name "native_deferred_hdr" -Environment $nativeEnvironment
$dlssImage = Capture-WindowImage -Name "dlss_present" -Environment $dlssPresentEnvironment
$dlaaNativeImage = Capture-WindowImage -Name "dlaa_native_deferred_hdr" -Environment $dlaaNativeEnvironment
$dlaaImage = Capture-WindowImage -Name "dlaa_present" -Environment $dlaaPresentEnvironment
$defaultSceneDlaaNativeImage = Capture-WindowImage `
    -Name "default_scene_dlaa_native_deferred_hdr" `
    -Environment $defaultSceneDlaaNativeEnvironment
$defaultSceneDlaaImage = Capture-WindowImage `
    -Name "default_scene_dlaa_present" `
    -Environment $defaultSceneDlaaPresentEnvironment
$wboitNativeImage = Capture-WindowImage -Name "wboit_native_deferred_hdr" -Environment $wboitNativeEnvironment
$wboitDlssImage = Capture-WindowImage -Name "wboit_dlss_present" -Environment $wboitDlssPresentEnvironment
$forwardSpecialNativeImage = Capture-WindowImage -Name "forward_special_native_deferred_hdr" -Environment $forwardSpecialNativeEnvironment
$forwardSpecialDlssImage = Capture-WindowImage -Name "forward_special_dlss_present" -Environment $forwardSpecialDlssPresentEnvironment
$materialStressNativeImage = Capture-WindowImage -Name "material_stress_native_deferred_hdr" -Environment $materialStressNativeEnvironment
$materialStressDlssImage = Capture-WindowImage -Name "material_stress_dlss_present" -Environment $materialStressDlssPresentEnvironment
$nativeImageStats = Get-ImageVariationStats -Path $nativeImage
$dlssImageStats = Get-ImageVariationStats -Path $dlssImage
$dlaaNativeImageStats = Get-ImageVariationStats -Path $dlaaNativeImage
$dlaaImageStats = Get-ImageVariationStats -Path $dlaaImage
$defaultSceneDlaaNativeImageStats = Get-ImageVariationStats -Path $defaultSceneDlaaNativeImage
$defaultSceneDlaaImageStats = Get-ImageVariationStats -Path $defaultSceneDlaaImage
$wboitNativeImageStats = Get-ImageVariationStats -Path $wboitNativeImage
$wboitDlssImageStats = Get-ImageVariationStats -Path $wboitDlssImage
$forwardSpecialNativeImageStats = Get-ImageVariationStats -Path $forwardSpecialNativeImage
$forwardSpecialDlssImageStats = Get-ImageVariationStats -Path $forwardSpecialDlssImage
$materialStressNativeImageStats = Get-ImageVariationStats -Path $materialStressNativeImage
$materialStressDlssImageStats = Get-ImageVariationStats -Path $materialStressDlssImage
$comparison = Compare-Images -A $nativeImage -B $dlssImage
$dlaaComparison = Compare-Images -A $dlaaNativeImage -B $dlaaImage
$defaultSceneDlaaComparison =
    Compare-Images -A $defaultSceneDlaaNativeImage -B $defaultSceneDlaaImage
$wboitComparison = Compare-Images -A $wboitNativeImage -B $wboitDlssImage
$forwardSpecialComparison = Compare-Images -A $forwardSpecialNativeImage -B $forwardSpecialDlssImage
$materialStressComparison = Compare-Images -A $materialStressNativeImage -B $materialStressDlssImage
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
$dlaaNativePostSource =
    "$($dlaaNativeRow.temporal_upscale_post_source_requested)/$($dlaaNativeRow.temporal_upscale_post_source_active)/$($dlaaNativeRow.temporal_upscale_post_source_fallback_reason)"
$dlaaNativeQualityGate =
    "$($dlaaNativeRow.temporal_upscaler_dlss_quality_gate_requested)/$($dlaaNativeRow.temporal_upscaler_dlss_quality_gate_ready)/$($dlaaNativeRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$dlaaPostSource =
    "$($dlaaRow.temporal_upscale_post_source_requested)/$($dlaaRow.temporal_upscale_post_source_active)/$($dlaaRow.temporal_upscale_post_source_fallback_reason)"
$dlaaQualityGate =
    "$($dlaaRow.temporal_upscaler_dlss_quality_gate_requested)/$($dlaaRow.temporal_upscaler_dlss_quality_gate_ready)/$($dlaaRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$dlaaQualityMasks =
    "$($dlaaRow.temporal_upscaler_dlss_quality_required_mask)/$($dlaaRow.temporal_upscaler_dlss_quality_ready_mask)/$($dlaaRow.temporal_upscaler_dlss_quality_blocker_mask)"
$dlaaQualityInputs =
    "output/camera/object/reactive/transparency/exposure/post/baseline=$($dlaaRow.temporal_upscaler_dlss_quality_evaluate_output_ready)/$($dlaaRow.temporal_upscaler_dlss_quality_camera_motion_ready)/$($dlaaRow.temporal_upscaler_dlss_quality_object_motion_ready)/$($dlaaRow.temporal_upscaler_dlss_quality_reactive_mask_ready)/$($dlaaRow.temporal_upscaler_dlss_quality_transparency_mask_ready)/$($dlaaRow.temporal_upscaler_dlss_quality_exposure_policy_ready)/$($dlaaRow.temporal_upscaler_dlss_quality_post_ordering_ready)/$($dlaaRow.temporal_upscaler_dlss_quality_reference_baseline_ready)"
$dlaaRenderScale =
    "$($dlaaRow.temporal_render_scale_requested)/$($dlaaRow.temporal_render_scale_active)/$($dlaaRow.temporal_render_scale_applied)"
$dlaaDlssExtents =
    "$($dlaaRow.temporal_upscaler_dlss_render_width)x$($dlaaRow.temporal_upscaler_dlss_render_height)->$($dlaaRow.temporal_upscaler_dlss_output_width)x$($dlaaRow.temporal_upscaler_dlss_output_height)"
$defaultSceneDlaaNativePostSource =
    "$($defaultSceneDlaaNativeRow.temporal_upscale_post_source_requested)/$($defaultSceneDlaaNativeRow.temporal_upscale_post_source_active)/$($defaultSceneDlaaNativeRow.temporal_upscale_post_source_fallback_reason)"
$defaultSceneDlaaNativeQualityGate =
    "$($defaultSceneDlaaNativeRow.temporal_upscaler_dlss_quality_gate_requested)/$($defaultSceneDlaaNativeRow.temporal_upscaler_dlss_quality_gate_ready)/$($defaultSceneDlaaNativeRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$defaultSceneDlaaPostSource =
    "$($defaultSceneDlaaRow.temporal_upscale_post_source_requested)/$($defaultSceneDlaaRow.temporal_upscale_post_source_active)/$($defaultSceneDlaaRow.temporal_upscale_post_source_fallback_reason)"
$defaultSceneDlaaQualityGate =
    "$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_gate_requested)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_gate_ready)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$defaultSceneDlaaQualityMasks =
    "$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_required_mask)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_ready_mask)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_blocker_mask)"
$defaultSceneDlaaQualityInputs =
    "output/camera/object/reactive/transparency/exposure/post/baseline=$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_evaluate_output_ready)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_camera_motion_ready)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_object_motion_ready)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_reactive_mask_ready)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_transparency_mask_ready)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_exposure_policy_ready)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_post_ordering_ready)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_quality_reference_baseline_ready)"
$defaultSceneDlaaRenderScale =
    "$($defaultSceneDlaaRow.temporal_render_scale_requested)/$($defaultSceneDlaaRow.temporal_render_scale_active)/$($defaultSceneDlaaRow.temporal_render_scale_applied)"
$defaultSceneDlaaDlssExtents =
    "$($defaultSceneDlaaRow.temporal_upscaler_dlss_render_width)x$($defaultSceneDlaaRow.temporal_upscaler_dlss_render_height)->$($defaultSceneDlaaRow.temporal_upscaler_dlss_output_width)x$($defaultSceneDlaaRow.temporal_upscaler_dlss_output_height)"
$defaultSceneDlaaDrawRoute =
    "$($defaultSceneDlaaRow.main_draws)/$($defaultSceneDlaaRow.gbuffer_draws)/$($defaultSceneDlaaRow.forward_residual_draws)/$($defaultSceneDlaaRow.weighted_translucency_draws)"
$defaultSceneDlaaSceneCounters =
    "materials=$($defaultSceneDlaaRow.frame_material_count),lights=$($defaultSceneDlaaRow.frame_light_total_count),local=$($defaultSceneDlaaRow.frame_local_light_count),rect=$($defaultSceneDlaaRow.frame_rect_light_count),probes=$($defaultSceneDlaaRow.reflection_probe_scene_probe_count)"
$wboitNativePostSource =
    "$($wboitNativeRow.temporal_upscale_post_source_requested)/$($wboitNativeRow.temporal_upscale_post_source_active)/$($wboitNativeRow.temporal_upscale_post_source_fallback_reason)"
$wboitNativeQualityGate =
    "$($wboitNativeRow.temporal_upscaler_dlss_quality_gate_requested)/$($wboitNativeRow.temporal_upscaler_dlss_quality_gate_ready)/$($wboitNativeRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$wboitDlssEvaluateOutput =
    "$($wboitDlssRow.temporal_upscaler_dlss_evaluate_result)/$($wboitDlssRow.temporal_upscaler_dlss_output_ready)"
$wboitDlssPostSource =
    "$($wboitDlssRow.temporal_upscale_post_source_requested)/$($wboitDlssRow.temporal_upscale_post_source_active)/$($wboitDlssRow.temporal_upscale_post_source_fallback_reason)"
$wboitDlssQualityGate =
    "$($wboitDlssRow.temporal_upscaler_dlss_quality_gate_requested)/$($wboitDlssRow.temporal_upscaler_dlss_quality_gate_ready)/$($wboitDlssRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$wboitDlssQualityMasks =
    "$($wboitDlssRow.temporal_upscaler_dlss_quality_required_mask)/$($wboitDlssRow.temporal_upscaler_dlss_quality_ready_mask)/$($wboitDlssRow.temporal_upscaler_dlss_quality_blocker_mask)"
$wboitDlssQualityInputs =
    "output/camera/object/reactive/transparency/exposure/post/baseline=$($wboitDlssRow.temporal_upscaler_dlss_quality_evaluate_output_ready)/$($wboitDlssRow.temporal_upscaler_dlss_quality_camera_motion_ready)/$($wboitDlssRow.temporal_upscaler_dlss_quality_object_motion_ready)/$($wboitDlssRow.temporal_upscaler_dlss_quality_reactive_mask_ready)/$($wboitDlssRow.temporal_upscaler_dlss_quality_transparency_mask_ready)/$($wboitDlssRow.temporal_upscaler_dlss_quality_exposure_policy_ready)/$($wboitDlssRow.temporal_upscaler_dlss_quality_post_ordering_ready)/$($wboitDlssRow.temporal_upscaler_dlss_quality_reference_baseline_ready)"
$forwardSpecialNativePostSource =
    "$($forwardSpecialNativeRow.temporal_upscale_post_source_requested)/$($forwardSpecialNativeRow.temporal_upscale_post_source_active)/$($forwardSpecialNativeRow.temporal_upscale_post_source_fallback_reason)"
$forwardSpecialNativeQualityGate =
    "$($forwardSpecialNativeRow.temporal_upscaler_dlss_quality_gate_requested)/$($forwardSpecialNativeRow.temporal_upscaler_dlss_quality_gate_ready)/$($forwardSpecialNativeRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$forwardSpecialDlssEvaluateOutput =
    "$($forwardSpecialDlssRow.temporal_upscaler_dlss_evaluate_result)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_output_ready)"
$forwardSpecialDlssPostSource =
    "$($forwardSpecialDlssRow.temporal_upscale_post_source_requested)/$($forwardSpecialDlssRow.temporal_upscale_post_source_active)/$($forwardSpecialDlssRow.temporal_upscale_post_source_fallback_reason)"
$forwardSpecialDlssQualityGate =
    "$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_gate_requested)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_gate_ready)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$forwardSpecialDlssQualityMasks =
    "$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_required_mask)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_ready_mask)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_blocker_mask)"
$forwardSpecialDlssQualityInputs =
    "output/camera/object/reactive/transparency/exposure/post/baseline=$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_evaluate_output_ready)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_camera_motion_ready)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_object_motion_ready)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_reactive_mask_ready)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_transparency_mask_ready)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_exposure_policy_ready)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_post_ordering_ready)/$($forwardSpecialDlssRow.temporal_upscaler_dlss_quality_reference_baseline_ready)"
$materialStressNativePostSource =
    "$($materialStressNativeRow.temporal_upscale_post_source_requested)/$($materialStressNativeRow.temporal_upscale_post_source_active)/$($materialStressNativeRow.temporal_upscale_post_source_fallback_reason)"
$materialStressNativeQualityGate =
    "$($materialStressNativeRow.temporal_upscaler_dlss_quality_gate_requested)/$($materialStressNativeRow.temporal_upscaler_dlss_quality_gate_ready)/$($materialStressNativeRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$materialStressDlssEvaluateOutput =
    "$($materialStressDlssRow.temporal_upscaler_dlss_evaluate_result)/$($materialStressDlssRow.temporal_upscaler_dlss_output_ready)"
$materialStressDlssPostSource =
    "$($materialStressDlssRow.temporal_upscale_post_source_requested)/$($materialStressDlssRow.temporal_upscale_post_source_active)/$($materialStressDlssRow.temporal_upscale_post_source_fallback_reason)"
$materialStressDlssQualityGate =
    "$($materialStressDlssRow.temporal_upscaler_dlss_quality_gate_requested)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_gate_ready)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$materialStressDlssQualityMasks =
    "$($materialStressDlssRow.temporal_upscaler_dlss_quality_required_mask)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_ready_mask)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_blocker_mask)"
$materialStressDlssQualityInputs =
    "output/camera/object/reactive/transparency/exposure/post/baseline=$($materialStressDlssRow.temporal_upscaler_dlss_quality_evaluate_output_ready)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_camera_motion_ready)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_object_motion_ready)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_reactive_mask_ready)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_transparency_mask_ready)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_exposure_policy_ready)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_post_ordering_ready)/$($materialStressDlssRow.temporal_upscaler_dlss_quality_reference_baseline_ready)"

Assert-BaselineText -Name "native.postSource" -Actual $nativePostSource -Expected $baselineManifest.expected.native.postSource
Assert-BaselineText -Name "native.qualityGate" -Actual $nativeQualityGate -Expected $baselineManifest.expected.native.qualityGate
Assert-BaselineText -Name "dlssPresent.evaluateOutput" -Actual $dlssEvaluateOutput -Expected $baselineManifest.expected.dlssPresent.evaluateOutput
Assert-BaselineText -Name "dlssPresent.postSource" -Actual $dlssPostSource -Expected $baselineManifest.expected.dlssPresent.postSource
Assert-BaselineText -Name "dlssPresent.qualityGate" -Actual $dlssQualityGate -Expected $baselineManifest.expected.dlssPresent.qualityGate
Assert-BaselineText -Name "dlssPresent.qualityMasks" -Actual $dlssQualityMasks -Expected $baselineManifest.expected.dlssPresent.qualityMasks
Assert-BaselineText -Name "dlssPresent.qualityInputs" -Actual $dlssQualityInputs -Expected $baselineManifest.expected.dlssPresent.qualityInputs
Assert-BaselineText -Name "dlaa.native.postSource" -Actual $dlaaNativePostSource -Expected $dlaaBaselineManifest.expected.native.postSource
Assert-BaselineText -Name "dlaa.native.qualityGate" -Actual $dlaaNativeQualityGate -Expected $dlaaBaselineManifest.expected.native.qualityGate
Assert-BaselineText -Name "dlaa.dlssPresent.postSource" -Actual $dlaaPostSource -Expected $dlaaBaselineManifest.expected.dlssPresent.postSource
Assert-BaselineText -Name "dlaa.dlssPresent.qualityGate" -Actual $dlaaQualityGate -Expected $dlaaBaselineManifest.expected.dlssPresent.qualityGate
Assert-BaselineText -Name "dlaa.dlssPresent.qualityMasks" -Actual $dlaaQualityMasks -Expected $dlaaBaselineManifest.expected.dlssPresent.qualityMasks
Assert-BaselineText -Name "dlaa.dlssPresent.qualityInputs" -Actual $dlaaQualityInputs -Expected $dlaaBaselineManifest.expected.dlssPresent.qualityInputs
Assert-BaselineText -Name "dlaa.dlssPresent.renderScale" -Actual $dlaaRenderScale -Expected $dlaaBaselineManifest.expected.dlssPresent.renderScale
Assert-BaselineText -Name "dlaa.dlssPresent.qualityMode" -Actual $dlaaRow.temporal_upscaler_dlss_quality_mode -Expected $dlaaBaselineManifest.expected.dlssPresent.qualityMode
Assert-BaselineText -Name "dlaa.dlssPresent.recommendedPreset" -Actual $dlaaRow.temporal_upscaler_dlss_recommended_preset -Expected $dlaaBaselineManifest.expected.dlssPresent.recommendedPreset
Assert-BaselineText -Name "defaultSceneDlaa.native.postSource" -Actual $defaultSceneDlaaNativePostSource -Expected $defaultSceneDlaaBaselineManifest.expected.native.postSource
Assert-BaselineText -Name "defaultSceneDlaa.native.qualityGate" -Actual $defaultSceneDlaaNativeQualityGate -Expected $defaultSceneDlaaBaselineManifest.expected.native.qualityGate
Assert-BaselineText -Name "defaultSceneDlaa.dlssPresent.postSource" -Actual $defaultSceneDlaaPostSource -Expected $defaultSceneDlaaBaselineManifest.expected.dlssPresent.postSource
Assert-BaselineText -Name "defaultSceneDlaa.dlssPresent.qualityGate" -Actual $defaultSceneDlaaQualityGate -Expected $defaultSceneDlaaBaselineManifest.expected.dlssPresent.qualityGate
Assert-BaselineText -Name "defaultSceneDlaa.dlssPresent.qualityMasks" -Actual $defaultSceneDlaaQualityMasks -Expected $defaultSceneDlaaBaselineManifest.expected.dlssPresent.qualityMasks
Assert-BaselineText -Name "defaultSceneDlaa.dlssPresent.qualityInputs" -Actual $defaultSceneDlaaQualityInputs -Expected $defaultSceneDlaaBaselineManifest.expected.dlssPresent.qualityInputs
Assert-BaselineText -Name "defaultSceneDlaa.dlssPresent.renderScale" -Actual $defaultSceneDlaaRenderScale -Expected $defaultSceneDlaaBaselineManifest.expected.dlssPresent.renderScale
Assert-BaselineText -Name "defaultSceneDlaa.dlssPresent.qualityMode" -Actual $defaultSceneDlaaRow.temporal_upscaler_dlss_quality_mode -Expected $defaultSceneDlaaBaselineManifest.expected.dlssPresent.qualityMode
Assert-BaselineText -Name "defaultSceneDlaa.dlssPresent.recommendedPreset" -Actual $defaultSceneDlaaRow.temporal_upscaler_dlss_recommended_preset -Expected $defaultSceneDlaaBaselineManifest.expected.dlssPresent.recommendedPreset
Assert-BaselineText -Name "wboit.native.postSource" -Actual $wboitNativePostSource -Expected $wboitBaselineManifest.expected.native.postSource
Assert-BaselineText -Name "wboit.native.qualityGate" -Actual $wboitNativeQualityGate -Expected $wboitBaselineManifest.expected.native.qualityGate
Assert-BaselineText -Name "wboit.dlssPresent.evaluateOutput" -Actual $wboitDlssEvaluateOutput -Expected $wboitBaselineManifest.expected.dlssPresent.evaluateOutput
Assert-BaselineText -Name "wboit.dlssPresent.postSource" -Actual $wboitDlssPostSource -Expected $wboitBaselineManifest.expected.dlssPresent.postSource
Assert-BaselineText -Name "wboit.dlssPresent.qualityGate" -Actual $wboitDlssQualityGate -Expected $wboitBaselineManifest.expected.dlssPresent.qualityGate
Assert-BaselineText -Name "wboit.dlssPresent.qualityMasks" -Actual $wboitDlssQualityMasks -Expected $wboitBaselineManifest.expected.dlssPresent.qualityMasks
Assert-BaselineText -Name "wboit.dlssPresent.qualityInputs" -Actual $wboitDlssQualityInputs -Expected $wboitBaselineManifest.expected.dlssPresent.qualityInputs
Assert-BaselineText -Name "forwardSpecial.native.postSource" -Actual $forwardSpecialNativePostSource -Expected $forwardSpecialBaselineManifest.expected.native.postSource
Assert-BaselineText -Name "forwardSpecial.native.qualityGate" -Actual $forwardSpecialNativeQualityGate -Expected $forwardSpecialBaselineManifest.expected.native.qualityGate
Assert-BaselineText -Name "forwardSpecial.dlssPresent.evaluateOutput" -Actual $forwardSpecialDlssEvaluateOutput -Expected $forwardSpecialBaselineManifest.expected.dlssPresent.evaluateOutput
Assert-BaselineText -Name "forwardSpecial.dlssPresent.postSource" -Actual $forwardSpecialDlssPostSource -Expected $forwardSpecialBaselineManifest.expected.dlssPresent.postSource
Assert-BaselineText -Name "forwardSpecial.dlssPresent.qualityGate" -Actual $forwardSpecialDlssQualityGate -Expected $forwardSpecialBaselineManifest.expected.dlssPresent.qualityGate
Assert-BaselineText -Name "forwardSpecial.dlssPresent.qualityMasks" -Actual $forwardSpecialDlssQualityMasks -Expected $forwardSpecialBaselineManifest.expected.dlssPresent.qualityMasks
Assert-BaselineText -Name "forwardSpecial.dlssPresent.qualityInputs" -Actual $forwardSpecialDlssQualityInputs -Expected $forwardSpecialBaselineManifest.expected.dlssPresent.qualityInputs
Assert-BaselineText -Name "materialStress.native.postSource" -Actual $materialStressNativePostSource -Expected $materialStressBaselineManifest.expected.native.postSource
Assert-BaselineText -Name "materialStress.native.qualityGate" -Actual $materialStressNativeQualityGate -Expected $materialStressBaselineManifest.expected.native.qualityGate
Assert-BaselineText -Name "materialStress.dlssPresent.evaluateOutput" -Actual $materialStressDlssEvaluateOutput -Expected $materialStressBaselineManifest.expected.dlssPresent.evaluateOutput
Assert-BaselineText -Name "materialStress.dlssPresent.postSource" -Actual $materialStressDlssPostSource -Expected $materialStressBaselineManifest.expected.dlssPresent.postSource
Assert-BaselineText -Name "materialStress.dlssPresent.qualityGate" -Actual $materialStressDlssQualityGate -Expected $materialStressBaselineManifest.expected.dlssPresent.qualityGate
Assert-BaselineText -Name "materialStress.dlssPresent.qualityMasks" -Actual $materialStressDlssQualityMasks -Expected $materialStressBaselineManifest.expected.dlssPresent.qualityMasks
Assert-BaselineText -Name "materialStress.dlssPresent.qualityInputs" -Actual $materialStressDlssQualityInputs -Expected $materialStressBaselineManifest.expected.dlssPresent.qualityInputs
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
Assert-BaselineRange `
    -Name "dlaa.native.imageStats.differentPixels" `
    -Actual $dlaaNativeImageStats.DifferentPixels `
    -Min $dlaaBaselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $dlaaNativeImageStats.SampledPixels
Assert-BaselineRange `
    -Name "dlaa.dlssPresent.imageStats.differentPixels" `
    -Actual $dlaaImageStats.DifferentPixels `
    -Min $dlaaBaselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $dlaaImageStats.SampledPixels
Assert-BaselineRange `
    -Name "dlaa.comparison.changedPixels" `
    -Actual $dlaaComparison.ChangedPixels `
    -Min $dlaaBaselineManifest.thresholds.comparisonChangedPixelsMin `
    -Max $dlaaBaselineManifest.thresholds.comparisonChangedPixelsMax
Assert-BaselineRange `
    -Name "dlaa.comparison.meanDelta" `
    -Actual $dlaaComparison.MeanDelta `
    -Min $dlaaBaselineManifest.thresholds.comparisonMeanDeltaMin `
    -Max $dlaaBaselineManifest.thresholds.comparisonMeanDeltaMax
Assert-BaselineRange `
    -Name "dlaa.comparison.maxDelta" `
    -Actual $dlaaComparison.MaxDelta `
    -Min 0 `
    -Max $dlaaBaselineManifest.thresholds.comparisonMaxDeltaMax
Assert-BaselineRange `
    -Name "defaultSceneDlaa.native.imageStats.differentPixels" `
    -Actual $defaultSceneDlaaNativeImageStats.DifferentPixels `
    -Min $defaultSceneDlaaBaselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $defaultSceneDlaaNativeImageStats.SampledPixels
Assert-BaselineRange `
    -Name "defaultSceneDlaa.dlssPresent.imageStats.differentPixels" `
    -Actual $defaultSceneDlaaImageStats.DifferentPixels `
    -Min $defaultSceneDlaaBaselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $defaultSceneDlaaImageStats.SampledPixels
Assert-BaselineRange `
    -Name "defaultSceneDlaa.comparison.changedPixels" `
    -Actual $defaultSceneDlaaComparison.ChangedPixels `
    -Min $defaultSceneDlaaBaselineManifest.thresholds.comparisonChangedPixelsMin `
    -Max $defaultSceneDlaaBaselineManifest.thresholds.comparisonChangedPixelsMax
Assert-BaselineRange `
    -Name "defaultSceneDlaa.comparison.meanDelta" `
    -Actual $defaultSceneDlaaComparison.MeanDelta `
    -Min $defaultSceneDlaaBaselineManifest.thresholds.comparisonMeanDeltaMin `
    -Max $defaultSceneDlaaBaselineManifest.thresholds.comparisonMeanDeltaMax
Assert-BaselineRange `
    -Name "defaultSceneDlaa.comparison.maxDelta" `
    -Actual $defaultSceneDlaaComparison.MaxDelta `
    -Min 0 `
    -Max $defaultSceneDlaaBaselineManifest.thresholds.comparisonMaxDeltaMax
Assert-BaselineRange `
    -Name "wboit.native.imageStats.differentPixels" `
    -Actual $wboitNativeImageStats.DifferentPixels `
    -Min $wboitBaselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $wboitNativeImageStats.SampledPixels
Assert-BaselineRange `
    -Name "wboit.dlssPresent.imageStats.differentPixels" `
    -Actual $wboitDlssImageStats.DifferentPixels `
    -Min $wboitBaselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $wboitDlssImageStats.SampledPixels
Assert-BaselineRange `
    -Name "wboit.comparison.changedPixels" `
    -Actual $wboitComparison.ChangedPixels `
    -Min $wboitBaselineManifest.thresholds.comparisonChangedPixelsMin `
    -Max $wboitBaselineManifest.thresholds.comparisonChangedPixelsMax
Assert-BaselineRange `
    -Name "wboit.comparison.meanDelta" `
    -Actual $wboitComparison.MeanDelta `
    -Min $wboitBaselineManifest.thresholds.comparisonMeanDeltaMin `
    -Max $wboitBaselineManifest.thresholds.comparisonMeanDeltaMax
Assert-BaselineRange `
    -Name "wboit.comparison.maxDelta" `
    -Actual $wboitComparison.MaxDelta `
    -Min 0 `
    -Max $wboitBaselineManifest.thresholds.comparisonMaxDeltaMax
Assert-BaselineRange `
    -Name "forwardSpecial.native.imageStats.differentPixels" `
    -Actual $forwardSpecialNativeImageStats.DifferentPixels `
    -Min $forwardSpecialBaselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $forwardSpecialNativeImageStats.SampledPixels
Assert-BaselineRange `
    -Name "forwardSpecial.dlssPresent.imageStats.differentPixels" `
    -Actual $forwardSpecialDlssImageStats.DifferentPixels `
    -Min $forwardSpecialBaselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $forwardSpecialDlssImageStats.SampledPixels
Assert-BaselineRange `
    -Name "forwardSpecial.comparison.changedPixels" `
    -Actual $forwardSpecialComparison.ChangedPixels `
    -Min $forwardSpecialBaselineManifest.thresholds.comparisonChangedPixelsMin `
    -Max $forwardSpecialBaselineManifest.thresholds.comparisonChangedPixelsMax
Assert-BaselineRange `
    -Name "forwardSpecial.comparison.meanDelta" `
    -Actual $forwardSpecialComparison.MeanDelta `
    -Min $forwardSpecialBaselineManifest.thresholds.comparisonMeanDeltaMin `
    -Max $forwardSpecialBaselineManifest.thresholds.comparisonMeanDeltaMax
Assert-BaselineRange `
    -Name "forwardSpecial.comparison.maxDelta" `
    -Actual $forwardSpecialComparison.MaxDelta `
    -Min 0 `
    -Max $forwardSpecialBaselineManifest.thresholds.comparisonMaxDeltaMax
Assert-BaselineRange `
    -Name "materialStress.native.imageStats.differentPixels" `
    -Actual $materialStressNativeImageStats.DifferentPixels `
    -Min $materialStressBaselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $materialStressNativeImageStats.SampledPixels
Assert-BaselineRange `
    -Name "materialStress.dlssPresent.imageStats.differentPixels" `
    -Actual $materialStressDlssImageStats.DifferentPixels `
    -Min $materialStressBaselineManifest.thresholds.centralDifferentPixelsMin `
    -Max $materialStressDlssImageStats.SampledPixels
Assert-BaselineRange `
    -Name "materialStress.comparison.changedPixels" `
    -Actual $materialStressComparison.ChangedPixels `
    -Min $materialStressBaselineManifest.thresholds.comparisonChangedPixelsMin `
    -Max $materialStressBaselineManifest.thresholds.comparisonChangedPixelsMax
Assert-BaselineRange `
    -Name "materialStress.comparison.meanDelta" `
    -Actual $materialStressComparison.MeanDelta `
    -Min $materialStressBaselineManifest.thresholds.comparisonMeanDeltaMin `
    -Max $materialStressBaselineManifest.thresholds.comparisonMeanDeltaMax
Assert-BaselineRange `
    -Name "materialStress.comparison.maxDelta" `
    -Actual $materialStressComparison.MaxDelta `
    -Min 0 `
    -Max $materialStressBaselineManifest.thresholds.comparisonMaxDeltaMax

$summary = [pscustomobject]@{
    target = $Target
    generatedAt = (Get-Date).ToString("o")
    baseline = [pscustomobject]@{
        manifest = $baselineManifestPath
        name = $baselineManifest.name
        schemaVersion = [int]$baselineManifest.schemaVersion
        wboitManifest = $wboitBaselineManifestPath
        wboitName = $wboitBaselineManifest.name
        forwardSpecialManifest = $forwardSpecialBaselineManifestPath
        forwardSpecialName = $forwardSpecialBaselineManifest.name
        materialStressManifest = $materialStressBaselineManifestPath
        materialStressName = $materialStressBaselineManifest.name
        dlaaManifest = $dlaaBaselineManifestPath
        dlaaName = $dlaaBaselineManifest.name
        defaultSceneDlaaManifest = $defaultSceneDlaaBaselineManifestPath
        defaultSceneDlaaName = $defaultSceneDlaaBaselineManifest.name
    }
    thresholds = [pscustomobject]@{
        minChangedPixels = $MinChangedPixels
        maxMeanDelta = $MaxMeanDelta
        comparisonChangedPixelsMin = [int]$baselineManifest.thresholds.comparisonChangedPixelsMin
        comparisonChangedPixelsMax = [int]$baselineManifest.thresholds.comparisonChangedPixelsMax
        comparisonMeanDeltaMin = [double]$baselineManifest.thresholds.comparisonMeanDeltaMin
        comparisonMeanDeltaMax = [double]$baselineManifest.thresholds.comparisonMeanDeltaMax
        comparisonMaxDeltaMax = [int]$baselineManifest.thresholds.comparisonMaxDeltaMax
        wboitComparisonChangedPixelsMin = [int]$wboitBaselineManifest.thresholds.comparisonChangedPixelsMin
        wboitComparisonChangedPixelsMax = [int]$wboitBaselineManifest.thresholds.comparisonChangedPixelsMax
        wboitComparisonMeanDeltaMin = [double]$wboitBaselineManifest.thresholds.comparisonMeanDeltaMin
        wboitComparisonMeanDeltaMax = [double]$wboitBaselineManifest.thresholds.comparisonMeanDeltaMax
        wboitComparisonMaxDeltaMax = [int]$wboitBaselineManifest.thresholds.comparisonMaxDeltaMax
        forwardSpecialComparisonChangedPixelsMin = [int]$forwardSpecialBaselineManifest.thresholds.comparisonChangedPixelsMin
        forwardSpecialComparisonChangedPixelsMax = [int]$forwardSpecialBaselineManifest.thresholds.comparisonChangedPixelsMax
        forwardSpecialComparisonMeanDeltaMin = [double]$forwardSpecialBaselineManifest.thresholds.comparisonMeanDeltaMin
        forwardSpecialComparisonMeanDeltaMax = [double]$forwardSpecialBaselineManifest.thresholds.comparisonMeanDeltaMax
        forwardSpecialComparisonMaxDeltaMax = [int]$forwardSpecialBaselineManifest.thresholds.comparisonMaxDeltaMax
        materialStressComparisonChangedPixelsMin = [int]$materialStressBaselineManifest.thresholds.comparisonChangedPixelsMin
        materialStressComparisonChangedPixelsMax = [int]$materialStressBaselineManifest.thresholds.comparisonChangedPixelsMax
        materialStressComparisonMeanDeltaMin = [double]$materialStressBaselineManifest.thresholds.comparisonMeanDeltaMin
        materialStressComparisonMeanDeltaMax = [double]$materialStressBaselineManifest.thresholds.comparisonMeanDeltaMax
        materialStressComparisonMaxDeltaMax = [int]$materialStressBaselineManifest.thresholds.comparisonMaxDeltaMax
        dlaaComparisonChangedPixelsMin = [int]$dlaaBaselineManifest.thresholds.comparisonChangedPixelsMin
        dlaaComparisonChangedPixelsMax = [int]$dlaaBaselineManifest.thresholds.comparisonChangedPixelsMax
        dlaaComparisonMeanDeltaMin = [double]$dlaaBaselineManifest.thresholds.comparisonMeanDeltaMin
        dlaaComparisonMeanDeltaMax = [double]$dlaaBaselineManifest.thresholds.comparisonMeanDeltaMax
        dlaaComparisonMaxDeltaMax = [int]$dlaaBaselineManifest.thresholds.comparisonMaxDeltaMax
        defaultSceneDlaaComparisonChangedPixelsMin = [int]$defaultSceneDlaaBaselineManifest.thresholds.comparisonChangedPixelsMin
        defaultSceneDlaaComparisonChangedPixelsMax = [int]$defaultSceneDlaaBaselineManifest.thresholds.comparisonChangedPixelsMax
        defaultSceneDlaaComparisonMeanDeltaMin = [double]$defaultSceneDlaaBaselineManifest.thresholds.comparisonMeanDeltaMin
        defaultSceneDlaaComparisonMeanDeltaMax = [double]$defaultSceneDlaaBaselineManifest.thresholds.comparisonMeanDeltaMax
        defaultSceneDlaaComparisonMaxDeltaMax = [int]$defaultSceneDlaaBaselineManifest.thresholds.comparisonMaxDeltaMax
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
    dlaaNative = [pscustomobject]@{
        csv = $dlaaNativeBenchmark.CsvPath
        image = $dlaaNativeImage
        columns = "$($dlaaNativeBenchmark.HeaderColumns)/$($dlaaNativeBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$dlaaNativeRow.framegraph_validation_issues
        postSource = $dlaaNativePostSource
        qualityGate = $dlaaNativeQualityGate
        renderScale = "$($dlaaNativeRow.temporal_render_scale_requested)/$($dlaaNativeRow.temporal_render_scale_active)/$($dlaaNativeRow.temporal_render_scale_applied)"
        imageStats = $dlaaNativeImageStats
    }
    dlaaPresent = [pscustomobject]@{
        csv = $dlaaBenchmark.CsvPath
        image = $dlaaImage
        columns = "$($dlaaBenchmark.HeaderColumns)/$($dlaaBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$dlaaRow.framegraph_validation_issues
        evaluateOutput = "$($dlaaRow.temporal_upscaler_dlss_evaluate_result)/$($dlaaRow.temporal_upscaler_dlss_output_ready)"
        postSource = $dlaaPostSource
        qualityGate = $dlaaQualityGate
        qualityMasks = $dlaaQualityMasks
        qualityInputs = $dlaaQualityInputs
        renderScale = $dlaaRenderScale
        qualityMode = [int]$dlaaRow.temporal_upscaler_dlss_quality_mode
        recommendedPreset = [int]$dlaaRow.temporal_upscaler_dlss_recommended_preset
        dlssExtents = $dlaaDlssExtents
        imageStats = $dlaaImageStats
    }
    defaultSceneDlaaNative = [pscustomobject]@{
        csv = $defaultSceneDlaaNativeBenchmark.CsvPath
        image = $defaultSceneDlaaNativeImage
        columns = "$($defaultSceneDlaaNativeBenchmark.HeaderColumns)/$($defaultSceneDlaaNativeBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$defaultSceneDlaaNativeRow.framegraph_validation_issues
        postSource = $defaultSceneDlaaNativePostSource
        qualityGate = $defaultSceneDlaaNativeQualityGate
        renderScale = "$($defaultSceneDlaaNativeRow.temporal_render_scale_requested)/$($defaultSceneDlaaNativeRow.temporal_render_scale_active)/$($defaultSceneDlaaNativeRow.temporal_render_scale_applied)"
        drawRoute = "$($defaultSceneDlaaNativeRow.main_draws)/$($defaultSceneDlaaNativeRow.gbuffer_draws)/$($defaultSceneDlaaNativeRow.forward_residual_draws)/$($defaultSceneDlaaNativeRow.weighted_translucency_draws)"
        sceneCounters = "materials=$($defaultSceneDlaaNativeRow.frame_material_count),lights=$($defaultSceneDlaaNativeRow.frame_light_total_count),local=$($defaultSceneDlaaNativeRow.frame_local_light_count),rect=$($defaultSceneDlaaNativeRow.frame_rect_light_count),probes=$($defaultSceneDlaaNativeRow.reflection_probe_scene_probe_count)"
        imageStats = $defaultSceneDlaaNativeImageStats
    }
    defaultSceneDlaaPresent = [pscustomobject]@{
        csv = $defaultSceneDlaaBenchmark.CsvPath
        image = $defaultSceneDlaaImage
        columns = "$($defaultSceneDlaaBenchmark.HeaderColumns)/$($defaultSceneDlaaBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$defaultSceneDlaaRow.framegraph_validation_issues
        evaluateOutput = "$($defaultSceneDlaaRow.temporal_upscaler_dlss_evaluate_result)/$($defaultSceneDlaaRow.temporal_upscaler_dlss_output_ready)"
        postSource = $defaultSceneDlaaPostSource
        qualityGate = $defaultSceneDlaaQualityGate
        qualityMasks = $defaultSceneDlaaQualityMasks
        qualityInputs = $defaultSceneDlaaQualityInputs
        renderScale = $defaultSceneDlaaRenderScale
        qualityMode = [int]$defaultSceneDlaaRow.temporal_upscaler_dlss_quality_mode
        recommendedPreset = [int]$defaultSceneDlaaRow.temporal_upscaler_dlss_recommended_preset
        dlssExtents = $defaultSceneDlaaDlssExtents
        drawRoute = $defaultSceneDlaaDrawRoute
        sceneCounters = $defaultSceneDlaaSceneCounters
        imageStats = $defaultSceneDlaaImageStats
    }
    wboitNative = [pscustomobject]@{
        csv = $wboitNativeBenchmark.CsvPath
        image = $wboitNativeImage
        columns = "$($wboitNativeBenchmark.HeaderColumns)/$($wboitNativeBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$wboitNativeRow.framegraph_validation_issues
        postSource = $wboitNativePostSource
        qualityGate = $wboitNativeQualityGate
        weightedTranslucency = "$($wboitNativeRow.weighted_translucency_draws)/$($wboitNativeRow.weighted_translucency_resolve_draws)"
        imageStats = $wboitNativeImageStats
    }
    wboitDlssPresent = [pscustomobject]@{
        csv = $wboitDlssBenchmark.CsvPath
        image = $wboitDlssImage
        columns = "$($wboitDlssBenchmark.HeaderColumns)/$($wboitDlssBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$wboitDlssRow.framegraph_validation_issues
        evaluateOutput = $wboitDlssEvaluateOutput
        postSource = $wboitDlssPostSource
        qualityGate = $wboitDlssQualityGate
        qualityMasks = $wboitDlssQualityMasks
        qualityInputs = $wboitDlssQualityInputs
        weightedTranslucency = "$($wboitDlssRow.weighted_translucency_draws)/$($wboitDlssRow.weighted_translucency_resolve_draws)/$($wboitDlssRow.weighted_translucency_velocity_draws)/$($wboitDlssRow.forward_residual_draws)"
        dlssMasks = "$($wboitDlssRow.dlss_mask_draws)/$($wboitDlssRow.dlss_mask_weighted_translucency_draws)/$($wboitDlssRow.dlss_mask_forward_residual_draws)"
        imageStats = $wboitDlssImageStats
    }
    forwardSpecialNative = [pscustomobject]@{
        csv = $forwardSpecialNativeBenchmark.CsvPath
        image = $forwardSpecialNativeImage
        columns = "$($forwardSpecialNativeBenchmark.HeaderColumns)/$($forwardSpecialNativeBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$forwardSpecialNativeRow.framegraph_validation_issues
        postSource = $forwardSpecialNativePostSource
        qualityGate = $forwardSpecialNativeQualityGate
        forwardResidual = "$($forwardSpecialNativeRow.hybrid_forward_special_draws)/$($forwardSpecialNativeRow.forward_residual_draws)/$($forwardSpecialNativeRow.frame_material_forward_special_count)"
        imageStats = $forwardSpecialNativeImageStats
    }
    forwardSpecialDlssPresent = [pscustomobject]@{
        csv = $forwardSpecialDlssBenchmark.CsvPath
        image = $forwardSpecialDlssImage
        columns = "$($forwardSpecialDlssBenchmark.HeaderColumns)/$($forwardSpecialDlssBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$forwardSpecialDlssRow.framegraph_validation_issues
        evaluateOutput = $forwardSpecialDlssEvaluateOutput
        postSource = $forwardSpecialDlssPostSource
        qualityGate = $forwardSpecialDlssQualityGate
        qualityMasks = $forwardSpecialDlssQualityMasks
        qualityInputs = $forwardSpecialDlssQualityInputs
        forwardResidual = "$($forwardSpecialDlssRow.hybrid_forward_special_draws)/$($forwardSpecialDlssRow.forward_residual_draws)/$($forwardSpecialDlssRow.forward_residual_shared_light_list_draws)/$($forwardSpecialDlssRow.frame_material_forward_special_count)"
        dlssMasks = "$($forwardSpecialDlssRow.dlss_mask_draws)/$($forwardSpecialDlssRow.dlss_mask_weighted_translucency_draws)/$($forwardSpecialDlssRow.dlss_mask_forward_residual_draws)"
        imageStats = $forwardSpecialDlssImageStats
    }
    materialStressNative = [pscustomobject]@{
        csv = $materialStressNativeBenchmark.CsvPath
        image = $materialStressNativeImage
        columns = "$($materialStressNativeBenchmark.HeaderColumns)/$($materialStressNativeBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$materialStressNativeRow.framegraph_validation_issues
        postSource = $materialStressNativePostSource
        qualityGate = $materialStressNativeQualityGate
        drawRoute = "$($materialStressNativeRow.gbuffer_draws)/$($materialStressNativeRow.forward_residual_draws)/$($materialStressNativeRow.weighted_translucency_draws)"
        materialCounters = "specTex=$($materialStressNativeRow.frame_material_specular_texture_count),uv=$($materialStressNativeRow.frame_material_uv_transform_count),double=$($materialStressNativeRow.frame_material_double_sided_count),clearcoatTex=$($materialStressNativeRow.frame_material_clearcoat_texture_count),clearcoatRoughTex=$($materialStressNativeRow.frame_material_clearcoat_roughness_texture_count),transTex=$($materialStressNativeRow.frame_material_transmission_texture_count),volume=$($materialStressNativeRow.frame_material_volume_count)"
        imageStats = $materialStressNativeImageStats
    }
    materialStressDlssPresent = [pscustomobject]@{
        csv = $materialStressDlssBenchmark.CsvPath
        image = $materialStressDlssImage
        columns = "$($materialStressDlssBenchmark.HeaderColumns)/$($materialStressDlssBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$materialStressDlssRow.framegraph_validation_issues
        evaluateOutput = $materialStressDlssEvaluateOutput
        postSource = $materialStressDlssPostSource
        qualityGate = $materialStressDlssQualityGate
        qualityMasks = $materialStressDlssQualityMasks
        qualityInputs = $materialStressDlssQualityInputs
        drawRoute = "$($materialStressDlssRow.gbuffer_draws)/$($materialStressDlssRow.forward_residual_draws)/$($materialStressDlssRow.weighted_translucency_draws)"
        dlssMasks = "$($materialStressDlssRow.dlss_mask_draws)/$($materialStressDlssRow.dlss_mask_weighted_translucency_draws)/$($materialStressDlssRow.dlss_mask_forward_residual_draws)"
        materialCounters = "specTex=$($materialStressDlssRow.frame_material_specular_texture_count),uv=$($materialStressDlssRow.frame_material_uv_transform_count),double=$($materialStressDlssRow.frame_material_double_sided_count),clearcoatTex=$($materialStressDlssRow.frame_material_clearcoat_texture_count),clearcoatRoughTex=$($materialStressDlssRow.frame_material_clearcoat_roughness_texture_count),transTex=$($materialStressDlssRow.frame_material_transmission_texture_count),volume=$($materialStressDlssRow.frame_material_volume_count)"
        imageStats = $materialStressDlssImageStats
    }
    comparison = $comparison
    dlaaComparison = $dlaaComparison
    defaultSceneDlaaComparison = $defaultSceneDlaaComparison
    wboitComparison = $wboitComparison
    forwardSpecialComparison = $forwardSpecialComparison
    materialStressComparison = $materialStressComparison
}

$summaryPath = Join-Path $outputRoot "summary.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host "DLSS visual QA passed"
Write-Host "  summary: $summaryPath"
Write-Host "  native:  $nativeImage"
Write-Host "  dlss:    $dlssImage"
Write-Host "  diff: sampled=$($comparison.SampledPixels) changed=$($comparison.ChangedPixels) mean=$($comparison.MeanDelta) max=$($comparison.MaxDelta)"
Write-Host "  dlaa:    $dlaaImage"
Write-Host "  adiff: sampled=$($dlaaComparison.SampledPixels) changed=$($dlaaComparison.ChangedPixels) mean=$($dlaaComparison.MeanDelta) max=$($dlaaComparison.MaxDelta)"
Write-Host "  app dlaa: $defaultSceneDlaaImage"
Write-Host "  appdiff: sampled=$($defaultSceneDlaaComparison.SampledPixels) changed=$($defaultSceneDlaaComparison.ChangedPixels) mean=$($defaultSceneDlaaComparison.MeanDelta) max=$($defaultSceneDlaaComparison.MaxDelta)"
Write-Host "  wboit:   $wboitDlssImage"
Write-Host "  wdiff: sampled=$($wboitComparison.SampledPixels) changed=$($wboitComparison.ChangedPixels) mean=$($wboitComparison.MeanDelta) max=$($wboitComparison.MaxDelta)"
Write-Host "  forward: $forwardSpecialDlssImage"
Write-Host "  fdiff: sampled=$($forwardSpecialComparison.SampledPixels) changed=$($forwardSpecialComparison.ChangedPixels) mean=$($forwardSpecialComparison.MeanDelta) max=$($forwardSpecialComparison.MaxDelta)"
Write-Host "  material: $materialStressDlssImage"
Write-Host "  mdiff: sampled=$($materialStressComparison.SampledPixels) changed=$($materialStressComparison.ChangedPixels) mean=$($materialStressComparison.MeanDelta) max=$($materialStressComparison.MaxDelta)"
