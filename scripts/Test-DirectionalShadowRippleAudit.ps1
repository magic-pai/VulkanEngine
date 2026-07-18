param(
    [string]$ExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [string]$ControlExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "tmp\directional_shadow_ripple_audit",
    [int]$Width = 2560,
    [int]$Height = 1440,
    [int]$MonitorIndex = 0,
    [int]$CaptureFrameCount = 5,
    [int]$InitialDelaySeconds = 3,
    [int]$CaptureIntervalMilliseconds = 180,
    [int]$MetricSampleStride = 4,
    [int]$BandHalfWindowSamples = 4,
    [double]$BandResidualThreshold = 0.012,
    [double]$MaxTentBandEnergy = 0.016,
    [double]$MaxPcssBandEnergy = 0.018,
    [double]$MaxPcssNormalizedBandEnergy = 0.35,
    [double]$MaxStaticFrameDelta = 0.010,
    [switch]$SkipControlScene,
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if ($CaptureFrameCount -lt 2) {
    throw "-CaptureFrameCount must be at least 2."
}
if ($MetricSampleStride -lt 1 -or $BandHalfWindowSamples -lt 1) {
    throw "Metric sampling values must be positive."
}

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

if (-not ("SelfEngineShadowRippleWin32" -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class SelfEngineShadowRippleWin32 {
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

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);

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
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();
}
'@
}

[void][SelfEngineShadowRippleWin32]::SetProcessDPIAware()

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
            if ($null -ne $Environment[$key]) {
                [Environment]::SetEnvironmentVariable(
                    [string]$key,
                    [string]$Environment[$key],
                    "Process"
                )
            }
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
    if ($screens.Count -eq 0) {
        throw "Windows did not report a display."
    }
    $index = [Math]::Min([Math]::Max($RequestedIndex, 0), $screens.Count - 1)
    return [pscustomobject]@{
        Index = $index
        Bounds = $screens[$index].Bounds
        DeviceName = $screens[$index].DeviceName
    }
}

function Get-RenderWindowHandle {
    param(
        [Parameter(Mandatory = $true)][int]$ProcessId,
        [Parameter(Mandatory = $true)][string]$Title
    )

    $script:rippleWindowHandle = [IntPtr]::Zero
    $callback = [SelfEngineShadowRippleWin32+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        $windowProcessId = [uint32]0
        [void][SelfEngineShadowRippleWin32]::GetWindowThreadProcessId(
            $hWnd,
            [ref]$windowProcessId
        )
        if ($windowProcessId -ne [uint32]$ProcessId -or
            -not [SelfEngineShadowRippleWin32]::IsWindowVisible($hWnd)) {
            return $true
        }

        $text = New-Object System.Text.StringBuilder 256
        [void][SelfEngineShadowRippleWin32]::GetWindowText($hWnd, $text, $text.Capacity)
        if ($text.ToString() -eq $Title) {
            $script:rippleWindowHandle = $hWnd
            return $false
        }
        return $true
    }
    [void][SelfEngineShadowRippleWin32]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:rippleWindowHandle
}

function Wait-RenderWindowHandle {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$Title,
        [int]$TimeoutSeconds = 20
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 200
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Renderer exited before the audit window appeared. ExitCode=$($Process.ExitCode)"
        }
        $handle = Get-RenderWindowHandle -ProcessId $Process.Id -Title $Title
        if ($handle -ne [IntPtr]::Zero) {
            return $handle
        }
    } while ((Get-Date) -lt $deadline)

    throw "Timed out waiting for the '$Title' audit window."
}

function Set-WindowPlacement {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$WindowHandle,
        [Parameter(Mandatory = $true)]$Monitor
    )

    $showWindow = 0x0040
    $topMost = [IntPtr](-1)
    [void][SelfEngineShadowRippleWin32]::SetWindowPos(
        $WindowHandle,
        $topMost,
        [int]$Monitor.Bounds.Left,
        [int]$Monitor.Bounds.Top,
        $Width,
        $Height,
        $showWindow
    )
    [void][SelfEngineShadowRippleWin32]::SetForegroundWindow($WindowHandle)
}

function Capture-WindowPng {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$WindowHandle,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $rect = New-Object SelfEngineShadowRippleWin32+RECT
    if (-not [SelfEngineShadowRippleWin32]::GetWindowRect($WindowHandle, [ref]$rect)) {
        throw "GetWindowRect failed."
    }
    $captureWidth = $rect.Right - $rect.Left
    $captureHeight = $rect.Bottom - $rect.Top
    if ($captureWidth -le 0 -or $captureHeight -le 0) {
        throw "Invalid capture size ${captureWidth}x${captureHeight}."
    }

    $bitmap = New-Object System.Drawing.Bitmap $captureWidth, $captureHeight
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $deviceContext = $graphics.GetHdc()
        try {
            # PW_RENDERFULLCONTENT captures the target HWND rather than the desktop.
            # This excludes NVIDIA/OS overlays and other processes from audit data.
            $captured = [SelfEngineShadowRippleWin32]::PrintWindow(
                $WindowHandle,
                $deviceContext,
                0x00000002
            )
        } finally {
            $graphics.ReleaseHdc($deviceContext)
        }
        if (-not $captured) {
            throw "PrintWindow failed for the SelfEngine audit window."
        }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
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

function Wait-AuditReady {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$CsvPath,
        [int]$TimeoutSeconds = 20
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Renderer exited before the audit benchmark CSV became ready."
        }
        $row = Get-LastCsvRow -Path $CsvPath
        if ($null -ne $row -and
            (Get-CsvValue -Row $row -Name "shadow_directional_receive_enabled") -ne "") {
            return $row
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)

    throw "Directional-shadow audit benchmark CSV was not ready: $CsvPath"
}

function Get-LumaGrid {
    param([Parameter(Mandatory = $true)][string]$Path)

    $bitmap = New-Object System.Drawing.Bitmap $Path
    $bitmapData = $null
    try {
        $margin = [Math]::Max(8, $MetricSampleStride * 2)
        $gridWidth = [int][Math]::Floor(($bitmap.Width - 2 * $margin) / $MetricSampleStride)
        $gridHeight = [int][Math]::Floor(($bitmap.Height - 2 * $margin) / $MetricSampleStride)
        if ($gridWidth -le 8 -or $gridHeight -le 8) {
            throw "Audit capture is too small for a spatial metric: $($bitmap.Width)x$($bitmap.Height)."
        }

        $sourceRect = New-Object System.Drawing.Rectangle 0, 0, $bitmap.Width, $bitmap.Height
        $bitmapData = $bitmap.LockBits(
            $sourceRect,
            [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
        )
        $sourceBytes = New-Object 'byte[]' ([Math]::Abs($bitmapData.Stride) * $bitmap.Height)
        [Runtime.InteropServices.Marshal]::Copy(
            $bitmapData.Scan0,
            $sourceBytes,
            0,
            $sourceBytes.Length
        )
        $values = New-Object 'double[]' ($gridWidth * $gridHeight)
        for ($y = 0; $y -lt $gridHeight; ++$y) {
            $sourceY = $margin + $y * $MetricSampleStride
            for ($x = 0; $x -lt $gridWidth; ++$x) {
                $sourceX = $margin + $x * $MetricSampleStride
                $pixelOffset = $sourceY * $bitmapData.Stride + $sourceX * 4
                $blue = $sourceBytes[$pixelOffset]
                $green = $sourceBytes[$pixelOffset + 1]
                $red = $sourceBytes[$pixelOffset + 2]
                $values[$y * $gridWidth + $x] = (
                    0.2126 * $red + 0.7152 * $green + 0.0722 * $blue
                ) / 255.0
            }
        }
        return [pscustomobject]@{
            Width = $gridWidth
            Height = $gridHeight
            Values = $values
        }
    } finally {
        if ($null -ne $bitmapData) {
            $bitmap.UnlockBits($bitmapData)
        }
        $bitmap.Dispose()
    }
}

function Get-IntegralImage {
    param([Parameter(Mandatory = $true)]$Grid)

    $integralWidth = $Grid.Width + 1
    $integral = New-Object 'double[]' ($integralWidth * ($Grid.Height + 1))
    for ($y = 1; $y -le $Grid.Height; ++$y) {
        $rowSum = 0.0
        for ($x = 1; $x -le $Grid.Width; ++$x) {
            $rowSum += $Grid.Values[($y - 1) * $Grid.Width + ($x - 1)]
            $integral[$y * $integralWidth + $x] =
                $integral[($y - 1) * $integralWidth + $x] + $rowSum
        }
    }
    return [pscustomobject]@{ Width = $integralWidth; Values = $integral }
}

function Get-BoxAverage {
    param(
        [Parameter(Mandatory = $true)]$Integral,
        [Parameter(Mandatory = $true)][int]$GridWidth,
        [Parameter(Mandatory = $true)][int]$GridHeight,
        [Parameter(Mandatory = $true)][int]$X,
        [Parameter(Mandatory = $true)][int]$Y,
        [Parameter(Mandatory = $true)][int]$Radius
    )

    $left = [Math]::Max(0, $X - $Radius)
    $right = [Math]::Min($GridWidth - 1, $X + $Radius)
    $top = [Math]::Max(0, $Y - $Radius)
    $bottom = [Math]::Min($GridHeight - 1, $Y + $Radius)
    $stride = $Integral.Width
    $sum =
        $Integral.Values[($bottom + 1) * $stride + ($right + 1)] -
        $Integral.Values[$top * $stride + ($right + 1)] -
        $Integral.Values[($bottom + 1) * $stride + $left] +
        $Integral.Values[$top * $stride + $left]
    return $sum / [double](($right - $left + 1) * ($bottom - $top + 1))
}

function Measure-ShadowBands {
    param(
        [Parameter(Mandatory = $true)]$Grid,
        [Parameter(Mandatory = $true)]$ReferenceGrid
    )

    if ($Grid.Width -ne $ReferenceGrid.Width -or $Grid.Height -ne $ReferenceGrid.Height) {
        throw "Audit capture dimensions changed between frames."
    }

    $integral = Get-IntegralImage -Grid $Grid
    $sampleCount = 0
    $signalSum = 0.0
    $bandEnergySum = 0.0
    $bandPixels = 0
    $largestLuma = 0.0
    $smallestLuma = 1.0
    $temporalDeltaSum = 0.0
    $temporalChanged = 0
    $candidate = New-Object 'bool[]' ($Grid.Width * $Grid.Height)

    for ($y = 1; $y -lt $Grid.Height - 1; ++$y) {
        for ($x = 1; $x -lt $Grid.Width - 1; ++$x) {
            $index = $y * $Grid.Width + $x
            $value = $Grid.Values[$index]
            # This view is abs(PCSS - Tent PCF), amplified for 8-bit capture.
            # Zero is a valid background/control value, not a dark receiver.
            if ($value -le 0.015) {
                continue
            }
            $gradient =
                [Math]::Abs($Grid.Values[$index + 1] - $Grid.Values[$index - 1]) +
                [Math]::Abs($Grid.Values[$index + $Grid.Width] - $Grid.Values[$index - $Grid.Width])
            if ($gradient -gt 0.35) {
                continue
            }

            $localMean = Get-BoxAverage -Integral $integral -GridWidth $Grid.Width `
                -GridHeight $Grid.Height -X $x -Y $y -Radius $BandHalfWindowSamples
            $residual = [Math]::Abs($value - $localMean)
            $sampleCount++
            $signalSum += $value
            $bandEnergySum += $residual
            $temporalDelta = [Math]::Abs($value - $ReferenceGrid.Values[$index])
            $temporalDeltaSum += $temporalDelta
            if ($temporalDelta -ge 0.012) {
                $temporalChanged++
            }
            if ($residual -ge $BandResidualThreshold) {
                $candidate[$index] = $true
                $bandPixels++
            }
            $largestLuma = [Math]::Max($largestLuma, $value)
            $smallestLuma = [Math]::Min($smallestLuma, $value)
        }
    }

    $visited = New-Object 'bool[]' $candidate.Length
    $queue = New-Object 'int[]' $candidate.Length
    $largestComponent = 0
    for ($seed = 0; $seed -lt $candidate.Length; ++$seed) {
        if (-not $candidate[$seed] -or $visited[$seed]) {
            continue
        }
        $head = 0
        $tail = 0
        $queue[$tail++] = $seed
        $visited[$seed] = $true
        while ($head -lt $tail) {
            $current = $queue[$head++]
            $currentX = $current % $Grid.Width
            $currentY = [int][Math]::Floor($current / $Grid.Width)
            for ($offsetY = -1; $offsetY -le 1; ++$offsetY) {
                for ($offsetX = -1; $offsetX -le 1; ++$offsetX) {
                    if ($offsetX -eq 0 -and $offsetY -eq 0) {
                        continue
                    }
                    $nextX = $currentX + $offsetX
                    $nextY = $currentY + $offsetY
                    if ($nextX -lt 0 -or $nextX -ge $Grid.Width -or
                        $nextY -lt 0 -or $nextY -ge $Grid.Height) {
                        continue
                    }
                    $next = $nextY * $Grid.Width + $nextX
                    if ($candidate[$next] -and -not $visited[$next]) {
                        $visited[$next] = $true
                        $queue[$tail++] = $next
                    }
                }
            }
        }
        $largestComponent = [Math]::Max($largestComponent, $tail)
    }

    $denominator = [Math]::Max(1, $sampleCount)
    return [pscustomobject]@{
        smoothSampleCount = $sampleCount
        deltaSignalMin = [Math]::Round($smallestLuma, 6)
        deltaSignalMax = [Math]::Round($largestLuma, 6)
        deltaSignalMean = [Math]::Round($signalSum / $denominator, 6)
        bandEnergy = [Math]::Round($bandEnergySum / $denominator, 6)
        bandCoverage = [Math]::Round($bandPixels / [double]$denominator, 6)
        largestBandComponentCoverage = [Math]::Round($largestComponent / [double]$denominator, 6)
        staticFrameMeanAbsDelta = [Math]::Round($temporalDeltaSum / $denominator, 6)
        staticFrameChangedRatio = [Math]::Round($temporalChanged / [double]$denominator, 6)
    }
}

function Get-LaneMetrics {
    param([Parameter(Mandatory = $true)][string[]]$CapturePaths)

    $grids = @($CapturePaths | ForEach-Object { Get-LumaGrid -Path $_ })
    $measurements = @()
    for ($index = 1; $index -lt $grids.Count; ++$index) {
        $measurements += Measure-ShadowBands -Grid $grids[$index] -ReferenceGrid $grids[$index - 1]
    }
    if ($measurements.Count -eq 0) {
        throw "No frame pairs were available for the ripple metric."
    }

    $average = @{}
    foreach ($property in $measurements[0].PSObject.Properties.Name) {
        $sum = 0.0
        foreach ($measurement in $measurements) {
            $sum += [double]$measurement.$property
        }
        $average[$property] = [Math]::Round($sum / $measurements.Count, 6)
    }
    return [pscustomobject]$average
}

function Get-LaneEnvironment {
    param(
        [Parameter(Mandatory = $true)][string]$Scene,
        [Parameter(Mandatory = $true)][string]$Lane,
        [Parameter(Mandatory = $true)][string]$CsvPath
    )

    $environment = @{
        SE_WINDOW_WIDTH = [string]$Width
        SE_WINDOW_HEIGHT = [string]$Height
        SE_WINDOW_BORDERLESS = "1"
        SE_BORDERLESS_FULLSCREEN = "0"
        SE_MAXIMIZE_BORDERLESS_FULLSCREEN = "0"
        SE_VISUAL_QA_HIDE_IMGUI = "1"
        SE_HIDE_IMGUI = "1"
        SE_BENCHMARK_SCENE = $Scene
        SE_FORWARD3D_DEBUG_DEFAULT_SCENE = $Scene
        SE_FORWARD3D_AA_MODE = "off"
        SE_RENDER_VIEW = "directional-pcss-delta"
        SE_CAMERA_FREEZE = "1"
        SE_SCENE_UPDATE_FREEZE = "1"
        SE_SHOWCASE_LOCAL_LIGHTS_OFF = "1"
        SE_SHADOW_REGRESSION_LOCAL_LIGHTS_OFF = "1"
        SE_LOCAL_SHADOW_POINT_OFF = "1"
        SE_LOCAL_SHADOW_SPOT_OFF = "1"
        SE_LOCAL_SHADOW_RECT_OFF = "1"
        SE_CONTACT_SHADOW_STRENGTH = "0"
        SE_SSAO_STRENGTH = "0"
        # The difference view bypasses compositing, so disable persistent post
        # resources that would otherwise be intentionally unused by this audit.
        SE_BLOOM = "0"
        SE_AUTO_EXPOSURE = "0"
        SE_COLOR_GRADING = "0"
        SE_COLOR_GRADING_LUT_STRENGTH = "0"
        SE_SHARPENING = "0"
        SE_SHADOW_QUALITY = "ultra"
        SE_DIRECTIONAL_SHADOW_FILTER_MODE = "1"
        SE_DIRECTIONAL_NORMAL_OFFSET_BIAS_TEXELS = "2.0"
        SE_DIRECTIONAL_SLOPE_OFFSET_BIAS_TEXELS = "1.0"
        SE_DIRECTIONAL_RECEIVER_PLANE_BIAS_SCALE = "2.0"
        SE_SHADOW_CASTER_DEPTH_BIAS_SLOPE = "6.0"
        SE_BENCHMARK_WARMUP_FRAMES = "8"
        # Keep the process alive long enough to capture a stable frame sequence.
        # BenchmarkRecorder stops the application when its capture count is reached.
        SE_BENCHMARK_FRAMES = "600"
        SE_AUTO_EXIT_FRAMES = "1200"
        SE_BENCHMARK_CSV = $CsvPath
    }

    switch ($Lane) {
        "receive-off" {
            $environment.SE_DIRECTIONAL_SHADOW_RECEIVE = "0"
            $environment.SE_DIRECTIONAL_PCSS_OFF = "1"
        }
        "tent-pcf" {
            $environment.SE_DIRECTIONAL_SHADOW_RECEIVE = "1"
            $environment.SE_DIRECTIONAL_PCSS_OFF = "1"
        }
        "pcss" {
            $environment.SE_DIRECTIONAL_SHADOW_RECEIVE = "1"
            # Deliberately use the scene/quality resolved PCSS defaults. The audit
            # verifies those values from the benchmark CSV rather than guessing them.
        }
        default { throw "Unexpected ripple audit lane '$Lane'." }
    }
    return $environment
}

function Invoke-RippleAuditLane {
    param(
        [Parameter(Mandatory = $true)][string]$Scene,
        [Parameter(Mandatory = $true)][string]$Lane,
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$WindowTitle,
        [Parameter(Mandatory = $true)][string]$RunRoot,
        [Parameter(Mandatory = $true)]$Monitor,
        [Parameter(Mandatory = $true)][string[]]$ManagedKeys
    )

    $laneDirectory = Join-Path $RunRoot ("{0}_{1}" -f $Scene, $Lane)
    New-Item -ItemType Directory -Force -Path $laneDirectory | Out-Null
    $csvPath = Join-Path $laneDirectory "benchmark.csv"
    $stdoutPath = Join-Path $laneDirectory "stdout.log"
    $stderrPath = Join-Path $laneDirectory "stderr.log"
    Remove-Item -LiteralPath $csvPath, $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    $environment = Get-LaneEnvironment -Scene $Scene -Lane $Lane -CsvPath $csvPath
    $process = $null
    $capturePaths = New-Object System.Collections.Generic.List[string]
    try {
        Invoke-WithEnvironment -ManagedKeys $ManagedKeys -Environment $environment -Script {
            $script:rippleAuditProcess = Start-Process -FilePath $Executable `
                -WorkingDirectory $repoRoot -RedirectStandardOutput $stdoutPath `
                -RedirectStandardError $stderrPath -PassThru
        }
        $process = $script:rippleAuditProcess
        $windowHandle = Wait-RenderWindowHandle -Process $process -Title $WindowTitle
        Set-WindowPlacement -WindowHandle $windowHandle -Monitor $Monitor
        Start-Sleep -Seconds $InitialDelaySeconds
        [void](Wait-AuditReady -Process $process -CsvPath $csvPath)

        for ($index = 0; $index -lt $CaptureFrameCount; ++$index) {
            if ($index -gt 0) {
                Start-Sleep -Milliseconds $CaptureIntervalMilliseconds
            }
            $process.Refresh()
            if ($process.HasExited) {
                throw "$Scene/$Lane exited before capture $index."
            }
            $capturePath = Join-Path $laneDirectory ("shadow_{0:D2}.png" -f $index)
            Capture-WindowPng -WindowHandle $windowHandle -Path $capturePath
            $capturePaths.Add($capturePath)
        }
    } finally {
        if ($process -and -not $process.HasExited) {
            [void]$process.CloseMainWindow()
            Start-Sleep -Milliseconds 400
            $process.Refresh()
            if (-not $process.HasExited) {
                $process.Kill()
                $process.WaitForExit()
            }
        }
    }

    $row = Get-LastCsvRow -Path $csvPath
    if ($null -eq $row) {
        throw "$Scene/$Lane did not produce benchmark CSV data."
    }
    return [pscustomobject]@{
        scene = $Scene
        lane = $Lane
        processId = $process.Id
        windowTitle = $WindowTitle
        outputDirectory = $laneDirectory
        captures = @($capturePaths.ToArray())
        metrics = Get-LaneMetrics -CapturePaths @($capturePaths.ToArray())
        csv = [pscustomobject]@{
            directionalReceiveEnabled = Get-CsvValue -Row $row -Name "shadow_directional_receive_enabled"
            pcssEnabled = Get-CsvValue -Row $row -Name "directional_shadow_pcss_enabled"
            pcssFallbackReason = Get-CsvValue -Row $row -Name "directional_shadow_pcss_fallback_reason"
            pcssStrength = Get-CsvValue -Row $row -Name "shadow_pcss_strength"
            pcssBlockerSamples = Get-CsvValue -Row $row -Name "directional_shadow_pcss_blocker_samples"
            pcssFilterSamples = Get-CsvValue -Row $row -Name "directional_shadow_pcss_filter_samples"
            pcssRawDepthReady = Get-CsvValue -Row $row -Name "directional_shadow_pcss_raw_depth_sampler_ready"
            shadowQuality = Get-CsvValue -Row $row -Name "shadow_quality"
            frameGraphIssues = Get-CsvValue -Row $row -Name "framegraph_validation_issues"
        }
        environment = $environment
    }
}

function Add-Check {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)][string]$Scene,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Status,
        $Actual,
        $Expected
    )

    $Checks.Add([pscustomobject]@{
        scene = $Scene
        name = $Name
        status = $Status
        actual = $Actual
        expected = $Expected
    }) | Out-Null
}

function Get-NumberOrNaN {
    param($Value)

    if ($null -eq $Value -or "$Value" -eq "") {
        return [double]::NaN
    }
    return [double]::Parse("$Value", [Globalization.CultureInfo]::InvariantCulture)
}

function Test-SceneAudit {
    param(
        [Parameter(Mandatory = $true)][string]$Scene,
        [Parameter(Mandatory = $true)]$LaneResults
    )

    $checks = [System.Collections.Generic.List[object]]::new()
    $byLane = @{}
    foreach ($result in $LaneResults) {
        $byLane[$result.lane] = $result
    }
    $receiveOff = $byLane["receive-off"]
    $tent = $byLane["tent-pcf"]
    $pcss = $byLane["pcss"]

    foreach ($required in @($receiveOff, $tent, $pcss)) {
        $frameGraphIssues = Get-NumberOrNaN $required.csv.frameGraphIssues
        Add-Check -Checks $checks -Scene $Scene -Name "$($required.lane) frame graph" `
            -Status $(if ($frameGraphIssues -eq 0) { "pass" } else { "fail" }) `
            -Actual $required.csv.frameGraphIssues -Expected "0"
    }
    Add-Check -Checks $checks -Scene $Scene -Name "receive-off isolates directional visibility" `
        -Status $(if ($receiveOff.csv.directionalReceiveEnabled -eq "0") { "pass" } else { "fail" }) `
        -Actual $receiveOff.csv.directionalReceiveEnabled -Expected "0"
    Add-Check -Checks $checks -Scene $Scene -Name "Tent PCF control resolves without PCSS" `
        -Status $(if ($tent.csv.directionalReceiveEnabled -eq "1" -and $tent.csv.pcssEnabled -eq "0") { "pass" } else { "fail" }) `
        -Actual "receive=$($tent.csv.directionalReceiveEnabled),pcss=$($tent.csv.pcssEnabled)" `
        -Expected "receive=1,pcss=0"
    Add-Check -Checks $checks -Scene $Scene -Name "PCSS lane resolves raw-depth blocker path" `
        -Status $(if ($pcss.csv.directionalReceiveEnabled -eq "1" -and
            $pcss.csv.pcssEnabled -eq "1" -and $pcss.csv.pcssRawDepthReady -eq "1") { "pass" } else { "fail" }) `
        -Actual "receive=$($pcss.csv.directionalReceiveEnabled),pcss=$($pcss.csv.pcssEnabled),raw=$($pcss.csv.pcssRawDepthReady)" `
        -Expected "receive=1,pcss=1,raw=1"

    $controlDelta = [Math]::Max(
        $receiveOff.metrics.deltaSignalMean,
        $tent.metrics.deltaSignalMean
    )
    Add-Check -Checks $checks -Scene $Scene -Name "PCSS delta controls remain black" `
        -Status $(if ($controlDelta -le 0.002) { "pass" } else { "fail" }) `
        -Actual $controlDelta -Expected "<= 0.002"
    Add-Check -Checks $checks -Scene $Scene -Name "PCSS static-frame stability" `
        -Status $(if ($pcss.metrics.staticFrameMeanAbsDelta -le $MaxStaticFrameDelta) { "pass" } else { "fail" }) `
        -Actual $pcss.metrics.staticFrameMeanAbsDelta -Expected "<= $MaxStaticFrameDelta"

    $normalizedBandEnergy = $pcss.metrics.bandEnergy /
        [Math]::Max($pcss.metrics.deltaSignalMean, 0.0001)
    $pcssRippleDetected =
        $pcss.metrics.bandEnergy -gt $MaxPcssBandEnergy -and
        $normalizedBandEnergy -gt $MaxPcssNormalizedBandEnergy
    Add-Check -Checks $checks -Scene $Scene -Name "PCSS incoherent ripple regression" `
        -Status $(if ($pcssRippleDetected) { "fail" } else { "pass" }) `
        -Actual ("energy={0}; normalized={1}; signal={2}; coverage={3}; component={4}" -f
            $pcss.metrics.bandEnergy,
            [Math]::Round($normalizedBandEnergy, 4),
            $pcss.metrics.deltaSignalMean,
            $pcss.metrics.bandCoverage,
            $pcss.metrics.largestBandComponentCoverage) `
        -Expected ("energy <= {0} or normalized band energy <= {1}" -f
            $MaxPcssBandEnergy,
            $MaxPcssNormalizedBandEnergy)

    $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
    $warnCount = @($checks | Where-Object { $_.status -eq "warn" }).Count
    $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
    return [pscustomobject]@{
        scene = $Scene
        verdict = if ($failCount -gt 0) { "fail" } elseif ($warnCount -gt 0) { "warn" } else { "pass" }
        passCount = $passCount
        warnCount = $warnCount
        failCount = $failCount
        pcssNormalizedBandEnergy = [Math]::Round($normalizedBandEnergy, 6)
        lanes = @($LaneResults)
        checks = @($checks)
    }
}

$primaryExecutable = Resolve-FullPath $ExecutablePath
$controlExecutable = Resolve-FullPath $ControlExecutablePath
if (-not (Test-Path -LiteralPath $primaryExecutable)) {
    throw "Executable not found: $primaryExecutable"
}
if (-not $SkipControlScene -and -not (Test-Path -LiteralPath $controlExecutable)) {
    throw "Control executable not found: $controlExecutable"
}

$outputRoot = Resolve-FullPath $OutputDirectory
$runRoot = Join-Path $outputRoot (Get-Date -Format "yyyyMMdd_HHmmss")
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$monitor = Get-MonitorInfo -RequestedIndex $MonitorIndex

$managedKeys = @(
    "SE_WINDOW_WIDTH", "SE_WINDOW_HEIGHT", "SE_WINDOW_BORDERLESS",
    "SE_BORDERLESS_FULLSCREEN", "SE_MAXIMIZE_BORDERLESS_FULLSCREEN",
    "SE_VISUAL_QA_HIDE_IMGUI", "SE_HIDE_IMGUI", "SE_BENCHMARK_SCENE",
    "SE_FORWARD3D_DEBUG_DEFAULT_SCENE", "SE_FORWARD3D_AA_MODE", "SE_RENDER_VIEW",
    "SE_CAMERA_FREEZE", "SE_SCENE_UPDATE_FREEZE", "SE_SHOWCASE_LOCAL_LIGHTS_OFF",
    "SE_SHADOW_REGRESSION_LOCAL_LIGHTS_OFF", "SE_LOCAL_SHADOW_POINT_OFF",
    "SE_LOCAL_SHADOW_SPOT_OFF", "SE_LOCAL_SHADOW_RECT_OFF", "SE_CONTACT_SHADOW_STRENGTH",
    "SE_SSAO_STRENGTH", "SE_BLOOM", "SE_AUTO_EXPOSURE", "SE_COLOR_GRADING",
    "SE_COLOR_GRADING_LUT_STRENGTH", "SE_SHARPENING", "SE_SHADOW_QUALITY",
    "SE_DIRECTIONAL_SHADOW_FILTER_MODE",
    "SE_DIRECTIONAL_NORMAL_OFFSET_BIAS_TEXELS", "SE_DIRECTIONAL_SLOPE_OFFSET_BIAS_TEXELS",
    "SE_DIRECTIONAL_RECEIVER_PLANE_BIAS_SCALE", "SE_SHADOW_CASTER_DEPTH_BIAS_SLOPE",
    "SE_DIRECTIONAL_SHADOW_RECEIVE", "SE_DIRECTIONAL_PCSS_OFF", "SE_SHADOW_PCSS_STRENGTH",
    "SE_BENCHMARK_WARMUP_FRAMES", "SE_BENCHMARK_FRAMES", "SE_AUTO_EXIT_FRAMES",
    "SE_BENCHMARK_CSV"
)

$sceneSpecs = @(
    [pscustomobject]@{
        scene = "lighting-showcase"
        executable = $primaryExecutable
        windowTitle = "SelfEngine Lighting Showcase"
    }
)
if (-not $SkipControlScene) {
    $sceneSpecs += [pscustomobject]@{
        scene = "shadow-regression"
        executable = $controlExecutable
        windowTitle = "SelfEngine Forward 3D"
    }
}

$reports = [System.Collections.Generic.List[object]]::new()
foreach ($sceneSpec in $sceneSpecs) {
    $laneResults = [System.Collections.Generic.List[object]]::new()
    foreach ($lane in @("receive-off", "tent-pcf", "pcss")) {
        Write-Host "Running directional ripple audit $($sceneSpec.scene)/$lane..."
        $laneResults.Add((Invoke-RippleAuditLane -Scene $sceneSpec.scene -Lane $lane `
            -Executable $sceneSpec.executable -WindowTitle $sceneSpec.windowTitle `
            -RunRoot $runRoot -Monitor $monitor -ManagedKeys $managedKeys)) | Out-Null
    }
    $reports.Add((Test-SceneAudit -Scene $sceneSpec.scene -LaneResults @($laneResults))) | Out-Null
}

$auditRows = foreach ($report in $reports) {
    foreach ($lane in $report.lanes) {
        [pscustomobject]@{
            scene = $report.scene
            verdict = $report.verdict
            pcss_normalized_band_energy = $report.pcssNormalizedBandEnergy
            lane = $lane.lane
            process_id = $lane.processId
            window_title = $lane.windowTitle
            smooth_sample_count = $lane.metrics.smoothSampleCount
            delta_signal_mean = $lane.metrics.deltaSignalMean
            band_energy = $lane.metrics.bandEnergy
            band_coverage = $lane.metrics.bandCoverage
            largest_band_component_coverage = $lane.metrics.largestBandComponentCoverage
            static_frame_mean_abs_delta = $lane.metrics.staticFrameMeanAbsDelta
            static_frame_changed_ratio = $lane.metrics.staticFrameChangedRatio
            directional_receive_enabled = $lane.csv.directionalReceiveEnabled
            pcss_enabled = $lane.csv.pcssEnabled
            pcss_strength = $lane.csv.pcssStrength
            pcss_blocker_samples = $lane.csv.pcssBlockerSamples
            pcss_filter_samples = $lane.csv.pcssFilterSamples
            pcss_raw_depth_ready = $lane.csv.pcssRawDepthReady
            frame_graph_issues = $lane.csv.frameGraphIssues
        }
    }
}
$csvSummaryPath = Join-Path $runRoot "ripple_audit.csv"
$auditRows | Export-Csv -LiteralPath $csvSummaryPath -NoTypeInformation -Encoding UTF8

$totalPass = @($reports | ForEach-Object { $_.passCount } | Measure-Object -Sum).Sum
$totalWarn = @($reports | ForEach-Object { $_.warnCount } | Measure-Object -Sum).Sum
$totalFail = @($reports | ForEach-Object { $_.failCount } | Measure-Object -Sum).Sum
$summary = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    runRoot = $runRoot
    monitor = @{ index = $monitor.Index; deviceName = $monitor.DeviceName }
    thresholds = @{
        metricSampleStride = $MetricSampleStride
        bandHalfWindowSamples = $BandHalfWindowSamples
        bandResidualThreshold = $BandResidualThreshold
        maxTentBandEnergy = $MaxTentBandEnergy
        maxPcssBandEnergy = $MaxPcssBandEnergy
        maxPcssNormalizedBandEnergy = $MaxPcssNormalizedBandEnergy
        maxStaticFrameDelta = $MaxStaticFrameDelta
    }
    verdict = if ($totalFail -gt 0) { "fail" } elseif ($totalWarn -gt 0) { "warn" } else { "pass" }
    passCount = $totalPass
    warnCount = $totalWarn
    failCount = $totalFail
    reports = @($reports)
}
$summaryPath = Join-Path $runRoot "summary.json"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host "Directional shadow ripple audit JSON: $summaryPath"
Write-Host "Directional shadow ripple audit CSV: $csvSummaryPath"
$summary | ConvertTo-Json -Depth 8

if ($Strict -and $totalFail -gt 0) {
    throw "Directional shadow ripple audit failed with $totalFail failing check(s)."
}
