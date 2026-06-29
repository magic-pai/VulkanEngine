param(
    [string]$Target = "SelfEngine",
    [string]$WindowTitle = "MagicPai Engine",
    [int]$TimeoutSeconds = 10
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

. .\scripts\Enter-VulkanDev.ps1 -Quiet
cmake --build out\build --target $Target | Out-Host

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class SelfEngineSmokeWin32 {
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

function Get-RenderWindowHandle {
    param(
        [int]$ProcessId,
        [string]$Title
    )

    $script:renderWindowHandle = [IntPtr]::Zero
    $callback = [SelfEngineSmokeWin32+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)

        $windowProcessId = [uint32]0
        [void][SelfEngineSmokeWin32]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($windowProcessId -ne [uint32]$ProcessId -or -not [SelfEngineSmokeWin32]::IsWindowVisible($hWnd)) {
            return $true
        }

        $titleBuilder = New-Object System.Text.StringBuilder 256
        [void][SelfEngineSmokeWin32]::GetWindowText($hWnd, $titleBuilder, $titleBuilder.Capacity)
        if ($titleBuilder.ToString() -eq $Title) {
            $script:renderWindowHandle = $hWnd
            return $false
        }

        return $true
    }

    [void][SelfEngineSmokeWin32]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:renderWindowHandle
}

$exePath = Join-Path $repoRoot "out\build\$Target.exe"
$stdoutPath = Join-Path $repoRoot "out\build\$Target.render-smoke.out.log"
$stderrPath = Join-Path $repoRoot "out\build\$Target.render-smoke.err.log"
$screenshotPath = Join-Path $repoRoot "out\build\$Target.render-smoke.png"

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
        throw "$Target exited before capture with code $($process.ExitCode)"
    }
    if ($windowHandle -eq [IntPtr]::Zero) {
        throw "Could not find render window '$WindowTitle'"
    }

    $topMost = [IntPtr](-1)
    $showWindow = 0x0040
    [void][SelfEngineSmokeWin32]::SetWindowPos($windowHandle, $topMost, 40, 40, 1038, 614, $showWindow)
    [void][SelfEngineSmokeWin32]::SetForegroundWindow($windowHandle)
    Start-Sleep -Seconds 2

    $rect = New-Object SelfEngineSmokeWin32+RECT
    [void][SelfEngineSmokeWin32]::GetWindowRect($windowHandle, [ref]$rect)
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top

    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($screenshotPath, [System.Drawing.Imaging.ImageFormat]::Png)

    $background = $bitmap.GetPixel([int]($width * 0.90), [int]($height * 0.90))
    $differentPixels = 0
    $sampledPixels = 0
    $maxDelta = 0

    for ($y = [int]($height * 0.30); $y -lt [int]($height * 0.70); $y += 4) {
        for ($x = [int]($width * 0.36); $x -lt [int]($width * 0.64); $x += 4) {
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
            ++$sampledPixels
        }
    }

    $graphics.Dispose()
    $bitmap.Dispose()

    if ($differentPixels -le 0) {
        throw "Render smoke failed: sampled $sampledPixels pixels, none differed from background"
    }

    Write-Host "Render smoke passed: sampled=$sampledPixels different=$differentPixels maxDelta=$maxDelta screenshot=$screenshotPath"
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
