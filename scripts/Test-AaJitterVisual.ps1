param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "out\aa_jitter_visual",
    [int]$Width = 960,
    [int]$Height = 540,
    [int]$MonitorIndex = 0,
    [int]$SequenceFrameCount = 4,
    [int]$InitialDelaySeconds = 3,
    [int]$SequenceIntervalMilliseconds = 250,
    [int]$WindowTimeoutSeconds = 20,
    [int]$ReadyTimeoutSeconds = 20,
    [int]$BenchmarkFrames = 600,
    [int]$AutoExitFrames = 900,
    [int]$ImageSampleStride = 6,
    [double]$MaxMeanAbsRgbDelta = 2.0,
    [double]$MaxChangedPixelRatio = 0.02,
    [string]$BenchmarkScene = "grid",
    [string]$BenchmarkCameraMotion = "none",
    [double]$BenchmarkCameraMotionSpeed = 0.65,
    [string]$BenchmarkObjectMotion = "none",
    [double]$BenchmarkObjectMotionSpeed = 0.9,
    [double]$BenchmarkObjectMotionRadius = 0.42,
    [string]$RenderView = "",
    [switch]$DefaultSceneSkinnedFbxProduction,
    [switch]$Build,
    [switch]$FailOnJitter
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if ($SequenceFrameCount -lt 2) {
    throw "-SequenceFrameCount must be at least 2."
}

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

if (-not ("SelfEngineAaJitterWin32" -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class SelfEngineAaJitterWin32 {
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

    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();
}
'@
}

[void][SelfEngineAaJitterWin32]::SetProcessDPIAware()

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
        [Parameter(Mandatory = $true)][System.Collections.Specialized.OrderedDictionary]$Environment,
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

function Get-MonitorInfo {
    param([int]$RequestedIndex)

    $screens = [System.Windows.Forms.Screen]::AllScreens
    if ($screens.Count -le 0) {
        throw "Windows did not report any display screens."
    }

    $index = $RequestedIndex
    if ($index -lt 0 -or $index -ge $screens.Count) {
        $index = 0
    }

    $screen = $screens[$index]
    return [pscustomobject]@{
        Index = $index
        DeviceName = $screen.DeviceName
        Bounds = $screen.Bounds
        WorkingArea = $screen.WorkingArea
    }
}

function Get-RenderWindowHandle {
    param(
        [Parameter(Mandatory = $true)][int]$ProcessId,
        [string]$Title = "SelfEngine Forward 3D"
    )

    $script:aaJitterWindowHandle = [IntPtr]::Zero
    $callback = [SelfEngineAaJitterWin32+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)

        $windowProcessId = [uint32]0
        [void][SelfEngineAaJitterWin32]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($windowProcessId -ne [uint32]$ProcessId -or -not [SelfEngineAaJitterWin32]::IsWindowVisible($hWnd)) {
            return $true
        }

        $titleBuilder = New-Object System.Text.StringBuilder 256
        [void][SelfEngineAaJitterWin32]::GetWindowText($hWnd, $titleBuilder, $titleBuilder.Capacity)
        if ($titleBuilder.ToString() -eq $Title) {
            $script:aaJitterWindowHandle = $hWnd
            return $false
        }

        return $true
    }

    [void][SelfEngineAaJitterWin32]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:aaJitterWindowHandle
}

function Wait-RenderWindowHandle {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 250
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "SelfEngineForward3D exited before its render window appeared. ExitCode=$($Process.ExitCode)"
        }

        $handle = Get-RenderWindowHandle -ProcessId $Process.Id
        if ($handle -ne [IntPtr]::Zero) {
            return $handle
        }
    } while ((Get-Date) -lt $deadline)

    throw "Timed out waiting for the render window."
}

function Set-WindowPlacement {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$WindowHandle,
        [Parameter(Mandatory = $true)]$Monitor
    )

    $topMost = [IntPtr](-1)
    $showWindow = 0x0040
    $x = [int]($Monitor.WorkingArea.Left + 24)
    $y = [int]($Monitor.WorkingArea.Top + 24)
    [void][SelfEngineAaJitterWin32]::SetWindowPos(
        $WindowHandle,
        $topMost,
        $x,
        $y,
        $Width,
        $Height,
        $showWindow
    )
    [void][SelfEngineAaJitterWin32]::SetForegroundWindow($WindowHandle)
}

function Capture-WindowPng {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$WindowHandle,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $rect = New-Object SelfEngineAaJitterWin32+RECT
    if (-not [SelfEngineAaJitterWin32]::GetWindowRect($WindowHandle, [ref]$rect)) {
        throw "GetWindowRect failed."
    }

    $captureWidth = $rect.Right - $rect.Left
    $captureHeight = $rect.Bottom - $rect.Top
    if ($captureWidth -le 0 -or $captureHeight -le 0) {
        throw "Invalid capture size: ${captureWidth}x${captureHeight}."
    }

    $bitmap = New-Object System.Drawing.Bitmap $captureWidth, $captureHeight
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }

    return [pscustomobject]@{
        path = $Path
        width = $captureWidth
        height = $captureHeight
    }
}

function Compare-ImagePair {
    param(
        [Parameter(Mandatory = $true)][string]$PreviousPath,
        [Parameter(Mandatory = $true)][string]$CurrentPath,
        [int]$Stride
    )

    $previous = New-Object System.Drawing.Bitmap $PreviousPath
    $current = New-Object System.Drawing.Bitmap $CurrentPath
    try {
        $width = [Math]::Min($previous.Width, $current.Width)
        $height = [Math]::Min($previous.Height, $current.Height)
        $sampleCount = 0
        $changedCount = 0
        $deltaSum = 0.0
        $edgeDeltaSum = 0.0
        $edgeSampleCount = 0
        $maxDelta = 0

        for ($y = 0; $y -lt $height; $y += $Stride) {
            for ($x = 0; $x -lt $width; $x += $Stride) {
                $a = $previous.GetPixel($x, $y)
                $b = $current.GetPixel($x, $y)
                $delta =
                    [Math]::Abs($a.R - $b.R) +
                    [Math]::Abs($a.G - $b.G) +
                    [Math]::Abs($a.B - $b.B)
                $deltaSum += $delta
                if ($delta -gt $maxDelta) {
                    $maxDelta = $delta
                }
                if ($delta -gt 24) {
                    ++$changedCount
                }

                if ($x + $Stride -lt $width -and $y + $Stride -lt $height) {
                    $aRight = $previous.GetPixel($x + $Stride, $y)
                    $aDown = $previous.GetPixel($x, $y + $Stride)
                    $bRight = $current.GetPixel($x + $Stride, $y)
                    $bDown = $current.GetPixel($x, $y + $Stride)
                    $aLuma = 0.2126 * $a.R + 0.7152 * $a.G + 0.0722 * $a.B
                    $bLuma = 0.2126 * $b.R + 0.7152 * $b.G + 0.0722 * $b.B
                    $aEdge =
                        [Math]::Abs($aLuma - (0.2126 * $aRight.R + 0.7152 * $aRight.G + 0.0722 * $aRight.B)) +
                        [Math]::Abs($aLuma - (0.2126 * $aDown.R + 0.7152 * $aDown.G + 0.0722 * $aDown.B))
                    $bEdge =
                        [Math]::Abs($bLuma - (0.2126 * $bRight.R + 0.7152 * $bRight.G + 0.0722 * $bRight.B)) +
                        [Math]::Abs($bLuma - (0.2126 * $bDown.R + 0.7152 * $bDown.G + 0.0722 * $bDown.B))
                    $edgeDeltaSum += [Math]::Abs($aEdge - $bEdge)
                    ++$edgeSampleCount
                }

                ++$sampleCount
            }
        }

        return [pscustomobject]@{
            previous = $PreviousPath
            current = $CurrentPath
            samples = $sampleCount
            meanAbsRgbDelta = [Math]::Round($deltaSum / [Math]::Max(1, $sampleCount), 4)
            maxAbsRgbDelta = $maxDelta
            changedPixelRatio = [Math]::Round($changedCount / [double][Math]::Max(1, $sampleCount), 6)
            meanEdgeDelta = [Math]::Round($edgeDeltaSum / [Math]::Max(1, $edgeSampleCount), 4)
        }
    } finally {
        $previous.Dispose()
        $current.Dispose()
    }
}

function Get-LastCsvRow {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    $rows = @(Import-Csv -LiteralPath $Path)
    if ($rows.Count -eq 0) {
        return $null
    }
    return $rows[$rows.Count - 1]
}

function Get-CsvValue {
    param(
        [AllowNull()]$Row,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($null -eq $Row) {
        return ""
    }
    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return ""
    }
    return [string]$property.Value
}

function Wait-AaVisualLaneReady {
    param(
        [Parameter(Mandatory = $true)][string]$Lane,
        [Parameter(Mandatory = $true)][string]$CsvPath,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "$Lane exited while waiting for lane readiness. ExitCode=$($Process.ExitCode)"
        }

        $row = Get-LastCsvRow -Path $CsvPath
        if ($null -ne $row) {
            if ($Lane -eq "dlss-dlaa-l") {
                $dlssReady =
                    (Get-CsvValue -Row $row -Name "temporal_upscaler_dlss_output_ready") -eq "1" -and
                    (Get-CsvValue -Row $row -Name "temporal_upscale_post_source_active") -eq "1"
                if ($dlssReady) {
                    return $row
                }
            } else {
                $historyStable =
                    (Get-CsvValue -Row $row -Name "temporal_history_reset") -eq "0"
                if ($historyStable) {
                    return $row
                }
            }
        }

        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)

    throw "$Lane did not become ready within $TimeoutSeconds seconds. CSV=$CsvPath"
}

function New-BaseEnvironment {
    param(
        [Parameter(Mandatory = $true)][string]$CsvPath,
        [Parameter(Mandatory = $true)][string]$Mode
    )

    $skinnedProduction = "0"
    if ($DefaultSceneSkinnedFbxProduction.IsPresent) {
        $skinnedProduction = "1"
    }

    $env = [ordered]@{
        "SE_FORWARD3D_AA_MODE" = $Mode
        "SE_WINDOW_WIDTH" = [string]$Width
        "SE_WINDOW_HEIGHT" = [string]$Height
        "SE_WINDOW_BORDERLESS" = "0"
        "SE_BORDERLESS_FULLSCREEN" = "0"
        "SE_MAXIMIZE_BORDERLESS_FULLSCREEN" = "0"
        "SE_VISUAL_QA_HIDE_IMGUI" = "1"
        "SE_HIDE_IMGUI" = "1"
        "SE_BENCHMARK_SCENE" = $BenchmarkScene
        "SE_BENCHMARK_CAMERA_MOTION" = $BenchmarkCameraMotion
        "SE_BENCHMARK_CAMERA_MOTION_SPEED" = [string]$BenchmarkCameraMotionSpeed
        "SE_BENCHMARK_OBJECT_MOTION" = $BenchmarkObjectMotion
        "SE_BENCHMARK_OBJECT_MOTION_SPEED" = [string]$BenchmarkObjectMotionSpeed
        "SE_BENCHMARK_OBJECT_MOTION_RADIUS" = [string]$BenchmarkObjectMotionRadius
        "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION" = $skinnedProduction
        "SE_AUTO_EXPOSURE" = "0"
        "SE_BLOOM" = "0"
        "SE_SSAO" = "0"
        "SE_SSR" = "0"
        "SE_SHADOW_QUALITY" = "off"
        "SE_BENCHMARK_CSV" = $CsvPath
        "SE_BENCHMARK_WARMUP_FRAMES" = "0"
        "SE_BENCHMARK_FRAMES" = [string]$BenchmarkFrames
        "SE_AUTO_EXIT_FRAMES" = [string]$AutoExitFrames
    }

    if (-not $DefaultSceneSkinnedFbxProduction.IsPresent) {
        $env["SE_DEFAULT_SCENE_SKINNED_FBX"] = "0"
        $env["SE_DEFAULT_SCENE_BIND_SKINNED_FBX"] = "0"
    }

    if ($RenderView.Trim().Length -gt 0) {
        $env["SE_RENDER_VIEW"] = $RenderView
    }

    return $env
}

function Add-LaneEnvironment {
    param(
        [Parameter(Mandatory = $true)][System.Collections.Specialized.OrderedDictionary]$Environment,
        [Parameter(Mandatory = $true)][string]$Lane
    )

    if ($Lane -eq "env-taa-no-jitter") {
        $Environment["SE_TAA"] = "1"
        $Environment["SE_TAA_RESOLVE"] = "1"
    }

    if ($Lane -eq "native-taa") {
        $Environment["SE_TAA"] = "1"
        $Environment["SE_TAA_RESOLVE"] = "1"
        $Environment["SE_TEMPORAL_JITTER"] = "1"
        $Environment["SE_TAA_APPLY_JITTER"] = "1"
    }

    if ($Lane -eq "dlss-dlaa-l") {
        $Environment["SE_UPSCALER_PLUGIN"] = "dlss"
        $Environment["SE_DLSS_QUALITY"] = "dlaa"
        $Environment["SE_DLSS_PRESET"] = "l"
        $Environment["SE_DLSS_SHARPNESS"] = "0.0"
        $Environment["SE_DLSS_PRESENT"] = "1"
        $Environment["SE_TAA"] = "1"
        $Environment["SE_TEMPORAL_JITTER"] = "1"
        $Environment["SE_TAA_APPLY_JITTER"] = "1"
        $Environment["SE_RENDER_SCALE"] = "1.0"
        $Environment["SE_RENDER_SCALE_APPLY"] = "1"
        $Environment["SE_TEMPORAL_VELOCITY_JITTER_POLICY"] = "jittered"
    }
}

function Invoke-AaVisualLane {
    param(
        [Parameter(Mandatory = $true)][string]$Lane,
        [Parameter(Mandatory = $true)][string]$Mode,
        [Parameter(Mandatory = $true)][string]$ExePath,
        [Parameter(Mandatory = $true)][string]$RunRoot,
        [Parameter(Mandatory = $true)]$Monitor
    )

    $laneRoot = Join-Path $RunRoot $Lane
    New-Item -ItemType Directory -Force -Path $laneRoot | Out-Null
    $csvPath = Join-Path $laneRoot "$Lane.csv"
    $stdoutPath = Join-Path $laneRoot "$Lane.stdout.log"
    $stderrPath = Join-Path $laneRoot "$Lane.stderr.log"

    Remove-Item -LiteralPath $csvPath, $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    Get-ChildItem -LiteralPath $laneRoot -Filter "*.png" -ErrorAction SilentlyContinue |
        Remove-Item -Force

    $environment = New-BaseEnvironment -CsvPath $csvPath -Mode $Mode
    Add-LaneEnvironment -Environment $environment -Lane $Lane
    $managedKeys = [string[]](@($environment.Keys) + @(
        "SE_UPSCALER_PLUGIN",
        "SE_DLSS_QUALITY",
        "SE_DLSS_PRESET",
        "SE_DLSS_SHARPNESS",
        "SE_DLSS_PRESENT",
        "SE_TAA",
        "SE_TAA_RESOLVE",
        "SE_TEMPORAL_JITTER",
        "SE_TAA_APPLY_JITTER",
        "SE_RENDER_SCALE",
        "SE_RENDER_SCALE_APPLY",
        "SE_TEMPORAL_VELOCITY_JITTER_POLICY",
        "SE_BENCHMARK_SCENE",
        "SE_BENCHMARK_CAMERA_MOTION",
        "SE_BENCHMARK_CAMERA_MOTION_SPEED",
        "SE_BENCHMARK_CAMERA_MOTION_YAW",
        "SE_BENCHMARK_CAMERA_MOTION_PITCH",
        "SE_BENCHMARK_CAMERA_MOTION_DISTANCE",
        "SE_BENCHMARK_OBJECT_MOTION",
        "SE_BENCHMARK_OBJECT_MOTION_SPEED",
        "SE_BENCHMARK_OBJECT_MOTION_RADIUS",
        "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
        "SE_DEFAULT_SCENE_SKINNED_FBX",
        "SE_DEFAULT_SCENE_BIND_SKINNED_FBX"
    ) | Select-Object -Unique)

    $process = $null
    $captures = New-Object System.Collections.Generic.List[object]
    $pairs = New-Object System.Collections.Generic.List[object]
    try {
        Invoke-WithEnvironment `
            -ManagedKeys $managedKeys `
            -Environment $environment `
            -Script {
                $script:aaJitterStartedProcess = Start-Process `
                    -FilePath $ExePath `
                    -WorkingDirectory $repoRoot `
                    -RedirectStandardOutput $stdoutPath `
                    -RedirectStandardError $stderrPath `
                    -PassThru
            }
        $process = $script:aaJitterStartedProcess
        $windowHandle = Wait-RenderWindowHandle -Process $process -TimeoutSeconds $WindowTimeoutSeconds
        Set-WindowPlacement -WindowHandle $windowHandle -Monitor $Monitor
        Start-Sleep -Seconds $InitialDelaySeconds
        [void](Wait-AaVisualLaneReady `
            -Lane $Lane `
            -CsvPath $csvPath `
            -Process $process `
            -TimeoutSeconds $ReadyTimeoutSeconds)

        for ($index = 0; $index -lt $SequenceFrameCount; ++$index) {
            $process.Refresh()
            if ($process.HasExited) {
                throw "$Lane exited before screenshot $index. ExitCode=$($process.ExitCode)"
            }
            if ($index -gt 0 -and $SequenceIntervalMilliseconds -gt 0) {
                Start-Sleep -Milliseconds $SequenceIntervalMilliseconds
            }

            $pngPath = Join-Path $laneRoot ("{0}_{1:D2}.png" -f $Lane, $index)
            $capture = Capture-WindowPng -WindowHandle $windowHandle -Path $pngPath
            $captures.Add($capture)
            if ($index -gt 0) {
                $pairs.Add((Compare-ImagePair `
                    -PreviousPath $captures[$index - 1].path `
                    -CurrentPath $capture.path `
                    -Stride $ImageSampleStride))
            }
        }
    } finally {
        if ($process -and -not $process.HasExited) {
            [void]$process.CloseMainWindow()
            Start-Sleep -Milliseconds 500
            $process.Refresh()
            if (-not $process.HasExited) {
                $process.Kill()
                $process.WaitForExit()
            }
        }
    }

    $csvRow = Get-LastCsvRow -Path $csvPath
    $meanDelta = 0.0
    $meanChangedRatio = 0.0
    $meanEdgeDelta = 0.0
    foreach ($pair in $pairs) {
        $meanDelta += [double]$pair.meanAbsRgbDelta
        $meanChangedRatio += [double]$pair.changedPixelRatio
        $meanEdgeDelta += [double]$pair.meanEdgeDelta
    }
    $pairCount = [Math]::Max(1, $pairs.Count)
    $meanDelta = [Math]::Round($meanDelta / $pairCount, 4)
    $meanChangedRatio = [Math]::Round($meanChangedRatio / $pairCount, 6)
    $meanEdgeDelta = [Math]::Round($meanEdgeDelta / $pairCount, 4)
    $unstable = $meanDelta -gt $MaxMeanAbsRgbDelta -or
        $meanChangedRatio -gt $MaxChangedPixelRatio

    return [pscustomobject]@{
        lane = $Lane
        mode = $Mode
        outputDirectory = $laneRoot
        captures = @($captures.ToArray())
        pairMetrics = @($pairs.ToArray())
        summary = [pscustomobject]@{
            meanAbsRgbDelta = $meanDelta
            changedPixelRatio = $meanChangedRatio
            meanEdgeDelta = $meanEdgeDelta
            unstable = $unstable
            maxMeanAbsRgbDelta = $MaxMeanAbsRgbDelta
            maxChangedPixelRatio = $MaxChangedPixelRatio
        }
        csv = [pscustomobject]@{
            antialiasingMode = Get-CsvValue -Row $csvRow -Name "temporal_antialiasing_mode"
            jitterApplied = Get-CsvValue -Row $csvRow -Name "temporal_jitter_applied"
            historyValid = Get-CsvValue -Row $csvRow -Name "temporal_history_valid"
            historyReset = Get-CsvValue -Row $csvRow -Name "temporal_history_reset"
            taaResolveEnabled = Get-CsvValue -Row $csvRow -Name "temporal_taa_resolve_enabled"
            taaResolveSuppressed = Get-CsvValue -Row $csvRow -Name "temporal_taa_resolve_suppressed_for_upscaler"
            dlssEvaluateAttempted = Get-CsvValue -Row $csvRow -Name "temporal_upscaler_dlss_evaluate_attempted"
            dlssOutputReady = Get-CsvValue -Row $csvRow -Name "temporal_upscaler_dlss_output_ready"
            postSourceActive = Get-CsvValue -Row $csvRow -Name "temporal_upscale_post_source_active"
            velocityCameraMotionReady = Get-CsvValue -Row $csvRow -Name "temporal_velocity_camera_motion_ready"
            velocityObjectMotionReady = Get-CsvValue -Row $csvRow -Name "temporal_velocity_object_motion_ready"
            velocityJitteredHistoryPolicy = Get-CsvValue -Row $csvRow -Name "temporal_velocity_jittered_history_policy"
            velocityPreviousJitterApplied = Get-CsvValue -Row $csvRow -Name "temporal_velocity_previous_jitter_applied"
        }
        environment = $environment
    }
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

$outputRoot = Resolve-FullPath $OutputDirectory
$runRoot = Join-Path $outputRoot (Get-Date -Format "yyyyMMdd_HHmmss")
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

$monitor = Get-MonitorInfo -RequestedIndex $MonitorIndex
$lanes = @(
    @{ Lane = "control-no-jitter"; Mode = "env" },
    @{ Lane = "env-taa-no-jitter"; Mode = "env" },
    @{ Lane = "native-taa"; Mode = "taa" },
    @{ Lane = "dlss-dlaa-l"; Mode = "dlss" }
)

$results = New-Object System.Collections.Generic.List[object]
foreach ($lane in $lanes) {
    Write-Host "Running AA visual lane '$($lane.Lane)'..."
    $results.Add((Invoke-AaVisualLane `
        -Lane $lane.Lane `
        -Mode $lane.Mode `
        -ExePath $resolvedExePath `
        -RunRoot $runRoot `
        -Monitor $monitor))
}

$unstableLanes = @($results.ToArray() | Where-Object { [bool]$_.summary.unstable })
$summary = [ordered]@{
    runRoot = $runRoot
    monitor = @{
        index = $monitor.Index
        deviceName = $monitor.DeviceName
    }
    thresholds = @{
        maxMeanAbsRgbDelta = $MaxMeanAbsRgbDelta
        maxChangedPixelRatio = $MaxChangedPixelRatio
    }
    unstableLaneCount = $unstableLanes.Count
    results = @($results.ToArray())
}
$summaryPath = Join-Path $runRoot "summary.json"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host "AA visual jitter summary: $summaryPath"
$summary | ConvertTo-Json -Depth 8

if ($FailOnJitter -and $unstableLanes.Count -gt 0) {
    $names = ($unstableLanes | ForEach-Object { $_.lane }) -join ", "
    throw "Unstable AA lanes exceeded thresholds: $names"
}
