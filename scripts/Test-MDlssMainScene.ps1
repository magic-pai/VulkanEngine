param(
    [ValidateSet("still", "object-motion", "moving-camera", "combined", "all")]
    [string]$Suite = "object-motion",
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "out\m_dlss_main_scene",
    [int]$Width = 2560,
    [int]$Height = 1440,
    [int]$MonitorIndex = 1,
    [int]$SequenceFrameCount = 2,
    [int]$InitialDelaySeconds = 4,
    [int]$SequenceIntervalSeconds = 1,
    [int]$WindowTimeoutSeconds = 20,
    [int]$BenchmarkFrames = 900,
    [int]$BenchmarkWarmupFrames = 0,
    [int]$AutoExitFrames = 1200,
    [int]$ImageSampleStride = 8,
    [string[]]$AdditionalEnv = @(),
    [switch]$Build,
    [switch]$DryRun,
    [switch]$Force,
    [switch]$ForceMvJitteredInputs,
    [switch]$StrictGate,
    [switch]$UseKnownNgxInternalLayoutIsolation
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if ($SequenceFrameCount -lt 1) {
    throw "-SequenceFrameCount must be at least 1."
}
if ($Width -lt 320 -or $Height -lt 240) {
    throw "-Width/-Height are too small for a Forward 3D visual QA capture."
}

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

if (-not ("SelfEngineMDlssWin32" -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class SelfEngineMDlssWin32 {
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

[void][SelfEngineMDlssWin32]::SetProcessDPIAware()

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Add-EnvironmentOverride {
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.Specialized.OrderedDictionary]$Environment,
        [Parameter(Mandatory = $true)]
        [string]$Entry
    )

    $separator = $Entry.IndexOf("=")
    if ($separator -le 0) {
        throw "Additional environment entries must use NAME=VALUE format: $Entry"
    }

    $name = $Entry.Substring(0, $separator).Trim()
    if ($name.Length -eq 0) {
        throw "Additional environment entry has an empty name: $Entry"
    }

    $Environment[$name] = $Entry.Substring($separator + 1)
}

function Invoke-WithEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$ManagedKeys,
        [Parameter(Mandatory = $true)]
        [System.Collections.Specialized.OrderedDictionary]$Environment,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Script
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

    $resolvedIndex = $RequestedIndex
    if ($resolvedIndex -lt 0 -or $resolvedIndex -ge $screens.Count) {
        Write-Warning "Monitor index $RequestedIndex is unavailable; falling back to monitor 0."
        $resolvedIndex = 0
    }

    $screen = $screens[$resolvedIndex]
    return [pscustomobject]@{
        Index = $resolvedIndex
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

    $script:renderWindowHandle = [IntPtr]::Zero
    $callback = [SelfEngineMDlssWin32+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)

        $windowProcessId = [uint32]0
        [void][SelfEngineMDlssWin32]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($windowProcessId -ne [uint32]$ProcessId -or -not [SelfEngineMDlssWin32]::IsWindowVisible($hWnd)) {
            return $true
        }

        $titleBuilder = New-Object System.Text.StringBuilder 256
        [void][SelfEngineMDlssWin32]::GetWindowText($hWnd, $titleBuilder, $titleBuilder.Capacity)
        if ($titleBuilder.ToString() -eq $Title) {
            $script:renderWindowHandle = $hWnd
            return $false
        }

        return $true
    }

    [void][SelfEngineMDlssWin32]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:renderWindowHandle
}

function Wait-RenderWindowHandle {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
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

    throw "Timed out waiting for the 'SelfEngine Forward 3D' render window."
}

function Set-WindowToMonitorBounds {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$WindowHandle,
        [Parameter(Mandatory = $true)]$Monitor
    )

    $topMost = [IntPtr](-1)
    $showWindow = 0x0040
    [void][SelfEngineMDlssWin32]::SetWindowPos(
        $WindowHandle,
        $topMost,
        [int]$Monitor.Bounds.Left,
        [int]$Monitor.Bounds.Top,
        [int]$Monitor.Bounds.Width,
        [int]$Monitor.Bounds.Height,
        $showWindow
    )
    [void][SelfEngineMDlssWin32]::SetForegroundWindow($WindowHandle)
}

function Capture-WindowPng {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$WindowHandle,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $rect = New-Object SelfEngineMDlssWin32+RECT
    if (-not [SelfEngineMDlssWin32]::GetWindowRect($WindowHandle, [ref]$rect)) {
        throw "GetWindowRect failed for SelfEngine Forward 3D."
    }

    $captureWidth = $rect.Right - $rect.Left
    $captureHeight = $rect.Bottom - $rect.Top
    if ($captureWidth -le 0 -or $captureHeight -le 0) {
        throw "The render window has an invalid capture size: ${captureWidth}x${captureHeight}."
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
        Path = $Path
        Left = $rect.Left
        Top = $rect.Top
        Width = $captureWidth
        Height = $captureHeight
    }
}

function Get-ImageSampleMetrics {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$Stride = 8
    )

    $bitmap = New-Object System.Drawing.Bitmap $Path
    try {
        $sampleCount = 0
        $blackCount = 0
        $purpleCount = 0
        $edgeCount = 0
        $edgeSum = 0.0
        $strongEdgeCount = 0
        $lumaSum = 0.0

        for ($y = 0; $y -lt $bitmap.Height; $y += $Stride) {
            for ($x = 0; $x -lt $bitmap.Width; $x += $Stride) {
                $color = $bitmap.GetPixel($x, $y)
                $luma = 0.2126 * $color.R + 0.7152 * $color.G + 0.0722 * $color.B
                $lumaSum += $luma
                if (($color.R + $color.G + $color.B) -lt 18) {
                    ++$blackCount
                }
                if ($color.R -gt 90 -and $color.B -gt 110 -and $color.G -lt 85 -and (($color.R + $color.B) -gt (3 * $color.G))) {
                    ++$purpleCount
                }

                if ($x + $Stride -lt $bitmap.Width -and $y + $Stride -lt $bitmap.Height) {
                    $right = $bitmap.GetPixel($x + $Stride, $y)
                    $down = $bitmap.GetPixel($x, $y + $Stride)
                    $rightLuma = 0.2126 * $right.R + 0.7152 * $right.G + 0.0722 * $right.B
                    $downLuma = 0.2126 * $down.R + 0.7152 * $down.G + 0.0722 * $down.B
                    $edge = [Math]::Abs($luma - $rightLuma) + [Math]::Abs($luma - $downLuma)
                    $edgeSum += $edge
                    ++$edgeCount
                    if ($edge -gt 45.0) {
                        ++$strongEdgeCount
                    }
                }

                ++$sampleCount
            }
        }

        return [pscustomobject]@{
            path = $Path
            width = $bitmap.Width
            height = $bitmap.Height
            samples = $sampleCount
            meanLuma = [Math]::Round($lumaSum / [Math]::Max(1, $sampleCount), 4)
            blackRatio = [Math]::Round($blackCount / [double][Math]::Max(1, $sampleCount), 6)
            purpleRatio = [Math]::Round($purpleCount / [double][Math]::Max(1, $sampleCount), 6)
            meanEdge = [Math]::Round($edgeSum / [Math]::Max(1, $edgeCount), 4)
            strongEdgeRatio = [Math]::Round($strongEdgeCount / [double][Math]::Max(1, $edgeCount), 6)
        }
    } finally {
        $bitmap.Dispose()
    }
}

function Compare-ImagePair {
    param(
        [Parameter(Mandatory = $true)][string]$PreviousPath,
        [Parameter(Mandatory = $true)][string]$CurrentPath,
        [int]$Stride = 8
    )

    $previous = New-Object System.Drawing.Bitmap $PreviousPath
    $current = New-Object System.Drawing.Bitmap $CurrentPath
    try {
        $width = [Math]::Min($previous.Width, $current.Width)
        $height = [Math]::Min($previous.Height, $current.Height)
        $sampleCount = 0
        $changedCount = 0
        $deltaSum = 0.0
        $maxDelta = 0
        $edgeDeltaSum = 0.0
        $edgeSampleCount = 0

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
            comparedWidth = $width
            comparedHeight = $height
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

    return [pscustomobject]@{
        row = $rows[$rows.Count - 1]
        sampleCount = $rows.Count
    }
}

function Get-RowValue {
    param(
        [AllowNull()]$Row,
        [Parameter(Mandatory = $true)][string]$Name,
        $Fallback = $null
    )

    if ($null -eq $Row) {
        return $Fallback
    }

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value -or $property.Value -eq "") {
        return $Fallback
    }

    return $property.Value
}

function Test-RowEquals {
    param(
        [AllowNull()]$Row,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Expected
    )

    return [string](Get-RowValue -Row $Row -Name $Name -Fallback "") -eq $Expected
}

function New-CsvGateSummary {
    param(
        [Parameter(Mandatory = $true)][string]$CsvPath,
        [int]$ExpectedWidth,
        [int]$ExpectedHeight
    )

    $csv = Get-LastCsvRow -Path $CsvPath
    if ($null -eq $csv) {
        return [pscustomobject]@{
            csvPath = $CsvPath
            available = $false
            productionInputsReady = $false
            reason = "CSV was not written or had no samples."
        }
    }

    $row = $csv.row
    $qualityGateReady =
        (Test-RowEquals -Row $row -Name "temporal_upscaler_dlss_quality_gate_requested" -Expected "1") -and
        (Test-RowEquals -Row $row -Name "temporal_upscaler_dlss_quality_gate_ready" -Expected "1") -and
        (Test-RowEquals -Row $row -Name "temporal_upscaler_dlss_quality_gate_fallback_reason" -Expected "0") -and
        (Test-RowEquals -Row $row -Name "temporal_upscaler_dlss_quality_required_mask" -Expected "255") -and
        (Test-RowEquals -Row $row -Name "temporal_upscaler_dlss_quality_ready_mask" -Expected "255") -and
        (Test-RowEquals -Row $row -Name "temporal_upscaler_dlss_quality_blocker_mask" -Expected "0")
    $dlssOutputReady =
        (Test-RowEquals -Row $row -Name "temporal_upscaler_dlss_output_ready" -Expected "1") -and
        (Test-RowEquals -Row $row -Name "temporal_upscale_post_source_active" -Expected "1")
    $presetMReady =
        (Test-RowEquals -Row $row -Name "temporal_upscaler_dlss_recommended_preset" -Expected "13")
    $skinnedReady =
        (Test-RowEquals -Row $row -Name "runtime_import_skinned_animation_unsupported" -Expected "0") -and
        (Test-RowEquals -Row $row -Name "runtime_import_animation_playback_ready" -Expected "1") -and
        (Test-RowEquals -Row $row -Name "bone_palette_shader_skinning_path_ready" -Expected "1") -and
        (Test-RowEquals -Row $row -Name "bone_palette_shader_velocity_path_ready" -Expected "1")
    $motionReady =
        (Test-RowEquals -Row $row -Name "temporal_velocity_camera_motion_ready" -Expected "1") -and
        (Test-RowEquals -Row $row -Name "temporal_velocity_object_motion_ready" -Expected "1")
    $extentReady =
        ([string](Get-RowValue -Row $row -Name "temporal_upscaler_dlss_output_width" -Fallback "") -eq [string]$ExpectedWidth) -and
        ([string](Get-RowValue -Row $row -Name "temporal_upscaler_dlss_output_height" -Fallback "") -eq [string]$ExpectedHeight)
    $frameGraphClean = Test-RowEquals -Row $row -Name "framegraph_validation_issues" -Expected "0"
    $historyStable = Test-RowEquals -Row $row -Name "temporal_history_reset" -Expected "0"
    $qualityGateText = "{0}/{1}/{2}" -f `
        (Get-RowValue -Row $row -Name "temporal_upscaler_dlss_quality_gate_requested" -Fallback ""),
        (Get-RowValue -Row $row -Name "temporal_upscaler_dlss_quality_gate_ready" -Fallback ""),
        (Get-RowValue -Row $row -Name "temporal_upscaler_dlss_quality_gate_fallback_reason" -Fallback "")
    $qualityMaskText = "{0}/{1}/{2}" -f `
        (Get-RowValue -Row $row -Name "temporal_upscaler_dlss_quality_required_mask" -Fallback ""),
        (Get-RowValue -Row $row -Name "temporal_upscaler_dlss_quality_ready_mask" -Fallback ""),
        (Get-RowValue -Row $row -Name "temporal_upscaler_dlss_quality_blocker_mask" -Fallback "")

    return [pscustomobject]@{
        csvPath = $CsvPath
        available = $true
        sampleCount = $csv.sampleCount
        renderedFrame = Get-RowValue -Row $row -Name "rendered_frame" -Fallback ""
        framegraphValidationIssues = Get-RowValue -Row $row -Name "framegraph_validation_issues" -Fallback ""
        dlssQualityMode = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_quality_mode" -Fallback ""
        dlssRecommendedPreset = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_recommended_preset" -Fallback ""
        dlssCreateFlags = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_create_flags" -Fallback ""
        dlssCreateFlagIsHdr = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_create_flag_is_hdr" -Fallback ""
        dlssCreateFlagMvLowRes = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_create_flag_mv_low_res" -Fallback ""
        dlssCreateFlagMvJittered = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_create_flag_mv_jittered" -Fallback ""
        dlssCreateFlagDepthInverted = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_create_flag_depth_inverted" -Fallback ""
        dlssCreateFlagAutoExposure = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_create_flag_auto_exposure" -Fallback ""
        dlssOutputReady = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_output_ready" -Fallback ""
        dlssOutputWidth = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_output_width" -Fallback ""
        dlssOutputHeight = Get-RowValue -Row $row -Name "temporal_upscaler_dlss_output_height" -Fallback ""
        dlssQualityGate = $qualityGateText
        dlssQualityMasks = $qualityMaskText
        temporalHistoryValid = Get-RowValue -Row $row -Name "temporal_history_valid" -Fallback ""
        temporalHistoryReset = Get-RowValue -Row $row -Name "temporal_history_reset" -Fallback ""
        temporalJitterApplied = Get-RowValue -Row $row -Name "temporal_jitter_applied" -Fallback ""
        temporalVelocityJitteredHistoryPolicy = Get-RowValue -Row $row -Name "temporal_velocity_jittered_history_policy" -Fallback ""
        temporalVelocityPreviousJitterApplied = Get-RowValue -Row $row -Name "temporal_velocity_previous_jitter_applied" -Fallback ""
        temporalVelocityCameraMotionReady = Get-RowValue -Row $row -Name "temporal_velocity_camera_motion_ready" -Fallback ""
        temporalVelocityObjectMotionReady = Get-RowValue -Row $row -Name "temporal_velocity_object_motion_ready" -Fallback ""
        skinnedUnsupported = Get-RowValue -Row $row -Name "runtime_import_skinned_animation_unsupported" -Fallback ""
        skinnedPlaybackReady = Get-RowValue -Row $row -Name "runtime_import_animation_playback_ready" -Fallback ""
        skinnedShaderSkinningReady = Get-RowValue -Row $row -Name "bone_palette_shader_skinning_path_ready" -Fallback ""
        skinnedShaderVelocityReady = Get-RowValue -Row $row -Name "bone_palette_shader_velocity_path_ready" -Fallback ""
        presetMReady = $presetMReady
        dlssOutputAndPostReady = $dlssOutputReady
        qualityGateReady = $qualityGateReady
        skinnedProductionPathReady = $skinnedReady
        temporalMotionReady = $motionReady
        outputExtentMatchesExpected = $extentReady
        frameGraphClean = $frameGraphClean
        historyStableOnLastSample = $historyStable
        productionInputsReady = (
            $presetMReady -and
            $dlssOutputReady -and
            $qualityGateReady -and
            $skinnedReady -and
            $motionReady -and
            $extentReady -and
            $frameGraphClean -and
            $historyStable
        )
    }
}

function New-LogSummary {
    param(
        [string]$StdoutPath,
        [string]$StderrPath,
        [bool]$KnownNgxIsolation
    )

    $issueLines = New-Object System.Collections.Generic.List[string]
    $unknownIssueLines = New-Object System.Collections.Generic.List[string]
    $knownNgxLayoutLines = New-Object System.Collections.Generic.List[string]
    foreach ($path in @($StdoutPath, $StderrPath)) {
        if (Test-Path -LiteralPath $path) {
            $previousWasKnownNgxLayout = $false
            foreach ($line in (Get-Content -LiteralPath $path -ErrorAction SilentlyContinue)) {
                $isIssue =
                    $line -match "VUID|Vulkan Validation|Validation Error|validation error|VK_ERROR|Unhandled|exception|Exception|failed|Failed|ERROR|Error"
                if (-not $isIssue -or
                    $line -match "0 validation issues" -or
                    $line -match "fallback_reason=0") {
                    $previousWasKnownNgxLayout = $false
                    continue
                }

                $isKnownNgxLayout =
                    $line -match "\[nv\.ngx\.dlss\.resource\]" -and
                    $line -match "VK_IMAGE_LAYOUT_GENERAL" -and
                    $line -match "VK_IMAGE_LAYOUT_UNDEFINED"
                $isKnownNgxVuidContinuation =
                    $previousWasKnownNgxLayout -and
                    $line -match "VUID-vkCmdDraw-None-09600"

                $issueLines.Add($line)
                if ($isKnownNgxLayout -or $isKnownNgxVuidContinuation) {
                    $knownNgxLayoutLines.Add($line)
                } else {
                    $unknownIssueLines.Add($line)
                }
                $previousWasKnownNgxLayout = $isKnownNgxLayout
            }
        }
    }

    $validationClean = $issueLines.Count -eq 0
    return [pscustomobject]@{
        stdout = $StdoutPath
        stderr = $StderrPath
        knownNgxInternalLayoutIsolation = $KnownNgxIsolation
        knownNgxResourceLayoutDiagnostic = $knownNgxLayoutLines.Count -gt 0
        validationClean = $validationClean
        productionStrictValidationClean = ($validationClean -and -not $KnownNgxIsolation)
        issueCount = $issueLines.Count
        knownNgxResourceLayoutIssueCount = $knownNgxLayoutLines.Count
        unknownIssueCount = $unknownIssueLines.Count
        allIssuesKnownNgxResourceLayout = (
            $issueLines.Count -gt 0 -and
            $knownNgxLayoutLines.Count -eq $issueLines.Count
        )
        issues = @($issueLines.ToArray() | Select-Object -First 20)
        unknownIssues = @($unknownIssueLines.ToArray() | Select-Object -First 20)
    }
}

function New-BaseEnvironment {
    param(
        [string]$CsvPath,
        [int]$TargetWidth,
        [int]$TargetHeight
    )

    $baselinePath = "docs\reference_baselines\dlss_default_scene_skinned_fbx_m_production_visual_qa_baseline.json"
    $environment = [ordered]@{
        "SE_UPSCALER_PLUGIN" = "dlss"
        "SE_DLSS_QUALITY" = "dlaa"
        "SE_DLSS_PRESET" = "m"
        "SE_DLSS_PRESENT" = "1"
        "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION" = "1"
        "SE_RENDER_SCALE" = "1.0"
        "SE_RENDER_SCALE_APPLY" = "1"
        "SE_TAA" = "1"
        "SE_TEMPORAL_JITTER" = "1"
        "SE_TAA_APPLY_JITTER" = "1"
        "SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS" = "1"
        "SE_RENDER_VIEW" = "deferred-hdr"
        "SE_WINDOW_WIDTH" = [string]$TargetWidth
        "SE_WINDOW_HEIGHT" = [string]$TargetHeight
        "SE_WINDOW_BORDERLESS" = "1"
        "SE_BORDERLESS_FULLSCREEN" = "1"
        "SE_MAXIMIZE_BORDERLESS_FULLSCREEN" = "1"
        "SE_VISUAL_QA_HIDE_IMGUI" = "1"
        "SE_HIDE_IMGUI" = "1"
        "SE_BENCHMARK_CSV" = $CsvPath
        "SE_BENCHMARK_WARMUP_FRAMES" = [string]$BenchmarkWarmupFrames
        "SE_BENCHMARK_FRAMES" = [string]$BenchmarkFrames
        "SE_AUTO_EXIT_FRAMES" = [string]$AutoExitFrames
    }

    if (Test-Path -LiteralPath $baselinePath) {
        $environment["SE_DLSS_REFERENCE_BASELINE_PATH"] = $baselinePath
    }
    if ($UseKnownNgxInternalLayoutIsolation) {
        $environment["SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT"] = "1"
    }
    if ($ForceMvJitteredInputs) {
        $environment["SE_DLSS_CREATE_FLAG_MV_JITTERED"] = "1"
        $environment["SE_TEMPORAL_VELOCITY_JITTER_POLICY"] = "jittered"
    }

    return $environment
}

function Add-SuiteEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.Specialized.OrderedDictionary]$Environment,
        [Parameter(Mandatory = $true)]
        [string]$Lane
    )

    if ($Lane -eq "object-motion" -or $Lane -eq "combined") {
        $Environment["SE_BENCHMARK_OBJECT_MOTION"] = "orbit"
        $Environment["SE_BENCHMARK_OBJECT_MOTION_SPEED"] = "1.1"
        $Environment["SE_BENCHMARK_OBJECT_MOTION_RADIUS"] = "0.32"
    }

    if ($Lane -eq "moving-camera" -or $Lane -eq "combined") {
        $Environment["SE_BENCHMARK_CAMERA_MOTION"] = "orbit"
        $Environment["SE_BENCHMARK_CAMERA_MOTION_SPEED"] = "0.65"
        $Environment["SE_BENCHMARK_CAMERA_MOTION_YAW"] = "0.18"
        $Environment["SE_BENCHMARK_CAMERA_MOTION_PITCH"] = "0.035"
        $Environment["SE_BENCHMARK_CAMERA_MOTION_DISTANCE"] = "5.0"
    }
}

function Get-ManagedEnvironmentKeys {
    param([System.Collections.Specialized.OrderedDictionary]$Environment)

    $keys = @(
        @($Environment.Keys) +
        @(
            "SELFENGINE_MODEL_PATH",
            "SE_ENABLE_IMPORTED_SKINNING_PREVIEW",
            "SE_BENCHMARK_SCENE",
            "SE_BENCHMARK_CAMERA_MOTION",
            "SE_BENCHMARK_OBJECT_MOTION",
            "SE_RENDER_VIEW",
            "SE_TAA_DEBUG_VIEW",
            "SE_TEMPORAL_DEBUG_VIEW",
            "SE_VISUAL_QA_CAPTURE_VIEW",
            "SE_DLSS_RENDER_PRESET",
            "SE_DLSS_PRESET_OVERRIDE"
        )
    ) | Select-Object -Unique

    return [string[]]$keys
}

function Invoke-MDlssLane {
    param(
        [Parameter(Mandatory = $true)][string]$Lane,
        [Parameter(Mandatory = $true)][string]$ExePath,
        [Parameter(Mandatory = $true)][string]$RunRoot,
        [Parameter(Mandatory = $true)]$Monitor
    )

    $laneRoot = Join-Path $RunRoot $Lane
    New-Item -ItemType Directory -Force -Path $laneRoot | Out-Null

    $csvPath = Join-Path $laneRoot "$Lane.csv"
    $stdoutPath = Join-Path $laneRoot "$Lane.stdout.log"
    $stderrPath = Join-Path $laneRoot "$Lane.stderr.log"
    $resultPath = Join-Path $laneRoot "$Lane.result.json"

    if (-not $Force) {
        foreach ($path in @($csvPath, $stdoutPath, $stderrPath, $resultPath)) {
            if (Test-Path -LiteralPath $path) {
                throw "Output already exists: $path. Pass -Force or use a new output directory."
            }
        }
    } else {
        foreach ($path in @($csvPath, $stdoutPath, $stderrPath, $resultPath)) {
            Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
        }
        Get-ChildItem -LiteralPath $laneRoot -Filter "*.png" -ErrorAction SilentlyContinue |
            Remove-Item -Force
    }

    $environment = New-BaseEnvironment `
        -CsvPath $csvPath `
        -TargetWidth $Width `
        -TargetHeight $Height
    Add-SuiteEnvironment -Environment $environment -Lane $Lane
    foreach ($entry in $AdditionalEnv) {
        Add-EnvironmentOverride -Environment $environment -Entry $entry
    }

    $environmentForJson = [ordered]@{}
    foreach ($key in $environment.Keys) {
        $environmentForJson[$key] = $environment[$key]
    }

    $planned = [ordered]@{
        lane = $Lane
        executable = $ExePath
        outputDirectory = $laneRoot
        monitor = @{
            index = $Monitor.Index
            deviceName = $Monitor.DeviceName
            bounds = @{
                left = $Monitor.Bounds.Left
                top = $Monitor.Bounds.Top
                width = $Monitor.Bounds.Width
                height = $Monitor.Bounds.Height
            }
        }
        screenshots = $SequenceFrameCount
        environment = $environmentForJson
        productionCandidateReady = $false
    }

    if ($DryRun) {
        $planned | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resultPath -Encoding UTF8
        Write-Host "Dry run wrote $resultPath"
        return [pscustomobject]$planned
    }

    Write-Host "Running M/DLAA lane '$Lane' on monitor $($Monitor.Index) $($Monitor.DeviceName)..."

    $process = $null
    $windowHandle = [IntPtr]::Zero
    $captures = New-Object System.Collections.Generic.List[object]
    $imageMetrics = New-Object System.Collections.Generic.List[object]
    $pairMetrics = New-Object System.Collections.Generic.List[object]
    $exitCode = $null
    $closedByScript = $false

    try {
        $managedKeys = Get-ManagedEnvironmentKeys -Environment $environment
        Invoke-WithEnvironment `
            -ManagedKeys $managedKeys `
            -Environment $environment `
            -Script {
                $script:startedProcess = Start-Process `
                    -FilePath $ExePath `
                    -WorkingDirectory $repoRoot `
                    -RedirectStandardOutput $stdoutPath `
                    -RedirectStandardError $stderrPath `
                    -PassThru
            }
        $process = $script:startedProcess
        $windowHandle = Wait-RenderWindowHandle -Process $process -TimeoutSeconds $WindowTimeoutSeconds
        Set-WindowToMonitorBounds -WindowHandle $windowHandle -Monitor $Monitor
        Start-Sleep -Seconds $InitialDelaySeconds

        for ($index = 0; $index -lt $SequenceFrameCount; ++$index) {
            $process.Refresh()
            if ($process.HasExited) {
                throw "SelfEngineForward3D exited before screenshot $index. ExitCode=$($process.ExitCode)"
            }

            if ($index -gt 0 -and $SequenceIntervalSeconds -gt 0) {
                Start-Sleep -Seconds $SequenceIntervalSeconds
            }

            $pngPath = Join-Path $laneRoot ("{0}_{1:D2}.png" -f $Lane, $index)
            $capture = Capture-WindowPng -WindowHandle $windowHandle -Path $pngPath
            $captures.Add($capture)
            $metrics = Get-ImageSampleMetrics -Path $pngPath -Stride $ImageSampleStride
            $imageMetrics.Add($metrics)

            if ($metrics.blackRatio -gt 0.98) {
                throw "Screenshot looks black (blackRatio=$($metrics.blackRatio)): $pngPath"
            }
            if ($metrics.purpleRatio -gt 0.65) {
                throw "Screenshot looks like a debug/depth view, not lit DLAA output (purpleRatio=$($metrics.purpleRatio)): $pngPath"
            }

            if ($index -gt 0) {
                $previousPath = $captures[$index - 1].Path
                $pairMetrics.Add((Compare-ImagePair `
                    -PreviousPath $previousPath `
                    -CurrentPath $pngPath `
                    -Stride $ImageSampleStride))
            }
        }
    } finally {
        if ($process -and -not $process.HasExited) {
            [void]$process.CloseMainWindow()
            Start-Sleep -Milliseconds 750
            $process.Refresh()
            if (-not $process.HasExited) {
                $process.Kill()
                $process.WaitForExit()
            } else {
                $closedByScript = $true
            }
        }
        if ($process) {
            $process.Refresh()
            if ($process.HasExited) {
                $exitCode = $process.ExitCode
            }
        }
    }

    $csvSummary = New-CsvGateSummary `
        -CsvPath $csvPath `
        -ExpectedWidth $Monitor.Bounds.Width `
        -ExpectedHeight $Monitor.Bounds.Height
    $logSummary = New-LogSummary `
        -StdoutPath $stdoutPath `
        -StderrPath $stderrPath `
        -KnownNgxIsolation ([bool]$UseKnownNgxInternalLayoutIsolation)
    $productionCandidateReady =
        [bool]$csvSummary.productionInputsReady -and
        [bool]$logSummary.productionStrictValidationClean

    $result = [ordered]@{
        lane = $Lane
        executable = $ExePath
        outputDirectory = $laneRoot
        monitor = @{
            index = $Monitor.Index
            deviceName = $Monitor.DeviceName
            bounds = @{
                left = $Monitor.Bounds.Left
                top = $Monitor.Bounds.Top
                width = $Monitor.Bounds.Width
                height = $Monitor.Bounds.Height
            }
        }
        capture = @{
            sequenceFrameCount = $SequenceFrameCount
            initialDelaySeconds = $InitialDelaySeconds
            sequenceIntervalSeconds = $SequenceIntervalSeconds
            sampleStride = $ImageSampleStride
            screenshots = @($captures.ToArray())
            imageMetrics = @($imageMetrics.ToArray())
            pairMetrics = @($pairMetrics.ToArray())
        }
        process = @{
            exitCode = $exitCode
            closedByScript = $closedByScript
        }
        csv = $csvSummary
        logs = $logSummary
        productionCandidateReady = $productionCandidateReady
        environment = $environmentForJson
    }

    $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    if ($StrictGate -and -not $productionCandidateReady) {
        throw "M/DLAA strict gate failed for lane '$Lane'. See $resultPath"
    }

    Write-Host "Lane '$Lane' complete: productionCandidateReady=$productionCandidateReady result=$resultPath"
    return [pscustomobject]$result
}

if ($Build) {
    & .\_quick_build.bat | Out-Host
    $buildExitCode = if ($null -ne (Get-Variable -Name LASTEXITCODE -ErrorAction SilentlyContinue)) {
        [int]$LASTEXITCODE
    } else {
        0
    }
    if ($buildExitCode -ne 0) {
        throw "_quick_build.bat failed with exit code $buildExitCode"
    }
}

$resolvedExePath = Resolve-FullPath $ExecutablePath
if (-not (Test-Path -LiteralPath $resolvedExePath)) {
    throw "Executable not found: $resolvedExePath. Build first or pass -Build."
}

$outputRoot = Resolve-FullPath $OutputDirectory
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$runRoot = Join-Path $outputRoot ("{0}_{1}" -f $Suite, $timestamp)
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

$monitor = Get-MonitorInfo -RequestedIndex $MonitorIndex
$lanes = if ($Suite -eq "all") {
    @("still", "object-motion", "moving-camera", "combined")
} else {
    @($Suite)
}

$results = New-Object System.Collections.Generic.List[object]
foreach ($lane in $lanes) {
    $results.Add((Invoke-MDlssLane `
        -Lane $lane `
        -ExePath $resolvedExePath `
        -RunRoot $runRoot `
        -Monitor $monitor))
}

$allProductionCandidatesReady = $true
foreach ($result in $results) {
    if (-not [bool]$result.productionCandidateReady) {
        $allProductionCandidatesReady = $false
        break
    }
}

$summaryPath = Join-Path $runRoot "summary.json"
$summary = [ordered]@{
    suite = $Suite
    runRoot = $runRoot
    monitorIndex = $monitor.Index
    monitorDeviceName = $monitor.DeviceName
    monitorBounds = @{
        left = $monitor.Bounds.Left
        top = $monitor.Bounds.Top
        width = $monitor.Bounds.Width
        height = $monitor.Bounds.Height
    }
    laneCount = $results.Count
    productionCandidateReady = $allProductionCandidatesReady
    results = @($results.ToArray())
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host "M/DLAA main-scene suite complete: $summaryPath"
