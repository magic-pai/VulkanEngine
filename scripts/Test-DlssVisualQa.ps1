param(
    [string]$Target = "SelfEngineForward3D",
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$WindowTitle = "SelfEngine Forward 3D",
    [string]$OutputDirectory = "out\reference_captures\dlss_visual_qa",
    [int]$TimeoutSeconds = 45,
    [int]$CaptureDelaySeconds = 8,
    [int]$MinChangedPixels = 0,
    [double]$MaxMeanDelta = 160.0,
    [string]$BaselinePath = "docs\reference_baselines\dlss_visual_qa_baseline.json",
    [string]$WboitBaselinePath = "docs\reference_baselines\dlss_wboit_visual_qa_baseline.json",
    [string]$ForwardSpecialBaselinePath = "docs\reference_baselines\dlss_forward_special_visual_qa_baseline.json",
    [string]$MaterialStressBaselinePath = "docs\reference_baselines\dlss_material_stress_visual_qa_baseline.json",
    [string]$MaskPolicyBaselinePath = "docs\reference_baselines\dlss_mask_policy_visual_qa_baseline.json",
    [string]$DlaaBaselinePath = "docs\reference_baselines\dlss_dlaa_visual_qa_baseline.json",
    [string]$DefaultSceneDlaaBaselinePath = "docs\reference_baselines\dlss_default_scene_dlaa_visual_qa_baseline.json",
    [string]$DefaultSceneDlaaMotionBaselinePath = "docs\reference_baselines\dlss_default_scene_dlaa_motion_visual_qa_baseline.json",
    [string]$DefaultSceneDlaaObjectMotionBaselinePath = "docs\reference_baselines\dlss_default_scene_dlaa_object_motion_visual_qa_baseline.json",
    [string]$ImportedDynamicDlaaObjectMotionBaselinePath = "docs\reference_baselines\dlss_imported_dynamic_dlaa_object_motion_visual_qa_baseline.json",
    [string]$ImportedArticulatedDlaaObjectMotionBaselinePath = "docs\reference_baselines\dlss_imported_articulated_dlaa_object_motion_visual_qa_baseline.json",
    [string]$ImportedSkinnedDiagnosticBaselinePath = "docs\reference_baselines\dlss_imported_skinned_diagnostic_visual_qa_baseline.json",
    [int]$CaptureMonitorIndex = 1,
    [string[]]$Suite = @("full"),
    [switch]$ListSuites,
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$baseSuites =
    @(
        "full",
        "default",
        "default-motion",
        "default-object-motion",
        "imported-dynamic",
        "imported-articulated",
        "imported-skinned-diagnostic",
        "wboit",
        "forward-special",
        "material-stress",
        "mask-policy"
    )
$suiteGroups =
    [ordered]@{
        "dynamic" = @(
            "default-motion",
            "default-object-motion",
            "imported-dynamic",
            "imported-articulated"
        )
        "mask-material" = @(
            "wboit",
            "forward-special",
            "material-stress",
            "mask-policy"
        )
    }
$validSuites = @($baseSuites + @($suiteGroups.Keys))
$requestedSuites = @(
    @(
        foreach ($suiteValue in $Suite) {
            foreach ($suitePart in $suiteValue.ToString().Split(
                    [char[]]@(','),
                    [System.StringSplitOptions]::RemoveEmptyEntries
                )) {
                $normalizedSuite = $suitePart.Trim().ToLowerInvariant()
                if ($normalizedSuite.Length -gt 0) {
                    $normalizedSuite
                }
            }
        }
    ) | Select-Object -Unique
)
if ($requestedSuites.Count -eq 0) {
    $requestedSuites = @("full")
}
foreach ($requestedSuite in $requestedSuites) {
    if ($validSuites -notcontains $requestedSuite) {
        throw "Unknown -Suite '$requestedSuite'. Valid suites: $($validSuites -join ', ')"
    }
}
if (($requestedSuites -contains "full") -and $requestedSuites.Count -gt 1) {
    throw "-Suite full cannot be combined with focused suites"
}
$selectedSuites = @(
    @(
        foreach ($requestedSuite in $requestedSuites) {
            if ($suiteGroups.Contains($requestedSuite)) {
                foreach ($suiteMember in $suiteGroups[$requestedSuite]) {
                    $suiteMember
                }
            } else {
                $requestedSuite
            }
        }
    ) | Select-Object -Unique
)

if ($ListSuites) {
    [pscustomobject]@{
        validSuites = $validSuites
        suiteGroups = $suiteGroups
        requestedSuites = $requestedSuites
        expandedSuites = $selectedSuites
    } | ConvertTo-Json -Depth 5
    return
}

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
$maskPolicyBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($MaskPolicyBaselinePath)) {
    [System.IO.Path]::GetFullPath($MaskPolicyBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $MaskPolicyBaselinePath))
}
if (!(Test-Path -LiteralPath $maskPolicyBaselineManifestPath)) {
    throw "DLSS mask-policy visual QA baseline manifest not found: $maskPolicyBaselineManifestPath"
}
$maskPolicyBaselineManifest = Get-Content -Raw -LiteralPath $maskPolicyBaselineManifestPath | ConvertFrom-Json
if ($maskPolicyBaselineManifest.target -ne $Target) {
    throw "DLSS mask-policy visual QA baseline target mismatch: expected $Target, manifest has $($maskPolicyBaselineManifest.target)"
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
$defaultSceneDlaaMotionBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($DefaultSceneDlaaMotionBaselinePath)) {
    [System.IO.Path]::GetFullPath($DefaultSceneDlaaMotionBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $DefaultSceneDlaaMotionBaselinePath))
}
if (!(Test-Path -LiteralPath $defaultSceneDlaaMotionBaselineManifestPath)) {
    throw "Default-scene DLAA motion visual QA baseline manifest not found: $defaultSceneDlaaMotionBaselineManifestPath"
}
$defaultSceneDlaaMotionBaselineManifest =
    Get-Content -Raw -LiteralPath $defaultSceneDlaaMotionBaselineManifestPath | ConvertFrom-Json
if ($defaultSceneDlaaMotionBaselineManifest.target -ne $Target) {
    throw "Default-scene DLAA motion visual QA baseline target mismatch: expected $Target, manifest has $($defaultSceneDlaaMotionBaselineManifest.target)"
}
$defaultSceneDlaaObjectMotionBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($DefaultSceneDlaaObjectMotionBaselinePath)) {
    [System.IO.Path]::GetFullPath($DefaultSceneDlaaObjectMotionBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $DefaultSceneDlaaObjectMotionBaselinePath))
}
if (!(Test-Path -LiteralPath $defaultSceneDlaaObjectMotionBaselineManifestPath)) {
    throw "Default-scene DLAA object-motion visual QA baseline manifest not found: $defaultSceneDlaaObjectMotionBaselineManifestPath"
}
$defaultSceneDlaaObjectMotionBaselineManifest =
    Get-Content -Raw -LiteralPath $defaultSceneDlaaObjectMotionBaselineManifestPath | ConvertFrom-Json
if ($defaultSceneDlaaObjectMotionBaselineManifest.target -ne $Target) {
    throw "Default-scene DLAA object-motion visual QA baseline target mismatch: expected $Target, manifest has $($defaultSceneDlaaObjectMotionBaselineManifest.target)"
}
$importedDynamicDlaaObjectMotionBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($ImportedDynamicDlaaObjectMotionBaselinePath)) {
    [System.IO.Path]::GetFullPath($ImportedDynamicDlaaObjectMotionBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $ImportedDynamicDlaaObjectMotionBaselinePath))
}
if (!(Test-Path -LiteralPath $importedDynamicDlaaObjectMotionBaselineManifestPath)) {
    throw "Imported-dynamic DLAA object-motion visual QA baseline manifest not found: $importedDynamicDlaaObjectMotionBaselineManifestPath"
}
$importedDynamicDlaaObjectMotionBaselineManifest =
    Get-Content -Raw -LiteralPath $importedDynamicDlaaObjectMotionBaselineManifestPath | ConvertFrom-Json
if ($importedDynamicDlaaObjectMotionBaselineManifest.target -ne $Target) {
    throw "Imported-dynamic DLAA object-motion visual QA baseline target mismatch: expected $Target, manifest has $($importedDynamicDlaaObjectMotionBaselineManifest.target)"
}
$importedArticulatedDlaaObjectMotionBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($ImportedArticulatedDlaaObjectMotionBaselinePath)) {
    [System.IO.Path]::GetFullPath($ImportedArticulatedDlaaObjectMotionBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $ImportedArticulatedDlaaObjectMotionBaselinePath))
}
if (!(Test-Path -LiteralPath $importedArticulatedDlaaObjectMotionBaselineManifestPath)) {
    throw "Imported-articulated DLAA object-motion visual QA baseline manifest not found: $importedArticulatedDlaaObjectMotionBaselineManifestPath"
}
$importedArticulatedDlaaObjectMotionBaselineManifest =
    Get-Content -Raw -LiteralPath $importedArticulatedDlaaObjectMotionBaselineManifestPath | ConvertFrom-Json
if ($importedArticulatedDlaaObjectMotionBaselineManifest.target -ne $Target) {
    throw "Imported-articulated DLAA object-motion visual QA baseline target mismatch: expected $Target, manifest has $($importedArticulatedDlaaObjectMotionBaselineManifest.target)"
}
$importedSkinnedDiagnosticBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($ImportedSkinnedDiagnosticBaselinePath)) {
    [System.IO.Path]::GetFullPath($ImportedSkinnedDiagnosticBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $ImportedSkinnedDiagnosticBaselinePath))
}
if (!(Test-Path -LiteralPath $importedSkinnedDiagnosticBaselineManifestPath)) {
    throw "Imported-skinned diagnostic visual QA baseline manifest not found: $importedSkinnedDiagnosticBaselineManifestPath"
}
$importedSkinnedDiagnosticBaselineManifest =
    Get-Content -Raw -LiteralPath $importedSkinnedDiagnosticBaselineManifestPath | ConvertFrom-Json
if ($importedSkinnedDiagnosticBaselineManifest.target -ne $Target) {
    throw "Imported-skinned diagnostic visual QA baseline target mismatch: expected $Target, manifest has $($importedSkinnedDiagnosticBaselineManifest.target)"
}

$importedDynamicModelPath =
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot "assets\models\demo_crystal.obj"))
if (!(Test-Path -LiteralPath $importedDynamicModelPath)) {
    throw "Imported-dynamic DLAA object-motion model not found: $importedDynamicModelPath"
}
$importedArticulatedModelPath =
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot "assets\models\articulated_links.obj"))
if (!(Test-Path -LiteralPath $importedArticulatedModelPath)) {
    throw "Imported-articulated DLAA object-motion model not found: $importedArticulatedModelPath"
}
$importedSkinnedDiagnosticModelPath =
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot "assets\models\skinned_probe.dae"))
if (!(Test-Path -LiteralPath $importedSkinnedDiagnosticModelPath)) {
    throw "Imported-skinned diagnostic model not found: $importedSkinnedDiagnosticModelPath"
}

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
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

    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();

    [DllImport("user32.dll")]
    public static extern IntPtr SetProcessDpiAwarenessContext(IntPtr dpiContext);
}
'@
}

try {
    [void][SelfEngineVisualQaWin32]::SetProcessDpiAwarenessContext([IntPtr](-4))
} catch {
    [void][SelfEngineVisualQaWin32]::SetProcessDPIAware()
}

$managedEnvironmentKeys = @(
    "SE_AUTO_EXIT_FRAMES",
    "SE_BENCHMARK_SCENE",
    "SE_BENCHMARK_TRANSPARENT_MATERIAL",
    "SE_BENCHMARK_FORWARD_SPECIAL_MATERIAL",
    "SE_BENCHMARK_CONSTANT_EMISSIVE_MATERIAL",
    "SE_BENCHMARK_SPECULAR_MATERIAL",
    "SE_BENCHMARK_SPECULAR_TEXTURE_MATERIAL",
    "SE_BENCHMARK_ALPHA_MASK_MATERIAL",
    "SE_BENCHMARK_ALPHA_BLEND_MATERIAL",
    "SE_BENCHMARK_UV_TRANSFORM_MATERIAL",
    "SE_BENCHMARK_OPACITY_TEXTURE_MATERIAL",
    "SE_BENCHMARK_OPACITY_BLEND_TEXTURE_MATERIAL",
    "SE_BENCHMARK_DOUBLE_SIDED_MATERIAL",
    "SE_BENCHMARK_CLEARCOAT_MATERIAL",
    "SE_BENCHMARK_CLEARCOAT_TEXTURE_MATERIAL",
    "SE_BENCHMARK_CLEARCOAT_ROUGHNESS_TEXTURE_MATERIAL",
    "SE_BENCHMARK_TRANSMISSION_MATERIAL",
    "SE_BENCHMARK_TRANSMISSION_TEXTURE_MATERIAL",
    "SE_BENCHMARK_VOLUME_MATERIAL",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_BENCHMARK_CSV",
    "SE_BENCHMARK_CAMERA_MOTION",
    "SE_BENCHMARK_CAMERA_MOTION_SPEED",
    "SE_BENCHMARK_CAMERA_MOTION_YAW",
    "SE_BENCHMARK_CAMERA_MOTION_PITCH",
    "SE_BENCHMARK_CAMERA_MOTION_DISTANCE",
    "SE_BENCHMARK_OBJECT_MOTION",
    "SE_BENCHMARK_OBJECT_MOTION_SPEED",
    "SE_BENCHMARK_OBJECT_MOTION_RADIUS",
    "SELFENGINE_MODEL_PATH",
    "SE_DEBUG_LOCAL_LIGHTS",
    "SE_VISUAL_QA_HIDE_IMGUI",
    "SE_HIDE_IMGUI",
    "SE_TAA_APPLY_JITTER",
    "SE_TEMPORAL_APPLY_JITTER",
    "SE_CAMERA_JITTER_APPLY",
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

function Get-CaptureMonitorWorkArea {
    $screens = [System.Windows.Forms.Screen]::AllScreens
    if ($screens.Count -le 0) {
        return [pscustomobject]@{
            RequestedIndex = $CaptureMonitorIndex
            Index = 0
            Count = 0
            DeviceName = ""
            Left = 0
            Top = 0
            Width = 1280
            Height = 720
        }
    }

    $selectedIndex = $CaptureMonitorIndex
    if ($selectedIndex -lt 0 -or $selectedIndex -ge $screens.Count) {
        $selectedIndex = 0
    }

    $area = $screens[$selectedIndex].WorkingArea
    return [pscustomobject]@{
        RequestedIndex = $CaptureMonitorIndex
        Index = $selectedIndex
        Count = $screens.Count
        DeviceName = $screens[$selectedIndex].DeviceName
        Left = $area.Left
        Top = $area.Top
        Width = $area.Width
        Height = $area.Height
    }
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

    Write-Host "Benchmark [$Name]"
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

$script:captureMonitorWorkArea = Get-CaptureMonitorWorkArea
Write-Host (
    "Capture monitor requested={0} actual={1}/{2} device={3} area={4},{5} {6}x{7}" -f
    $script:captureMonitorWorkArea.RequestedIndex,
    $script:captureMonitorWorkArea.Index,
    $script:captureMonitorWorkArea.Count,
    $script:captureMonitorWorkArea.DeviceName,
    $script:captureMonitorWorkArea.Left,
    $script:captureMonitorWorkArea.Top,
    $script:captureMonitorWorkArea.Width,
    $script:captureMonitorWorkArea.Height
)

function Set-CaptureWindowPlacement {
    param([Parameter(Mandatory = $true)][IntPtr]$WindowHandle)

    $topMost = [IntPtr](-1)
    $showWindow = 0x0040
    $monitorMargin = 40
    $captureLeft =
        [int]($script:captureMonitorWorkArea.Left + $monitorMargin)
    $captureTop =
        [int]($script:captureMonitorWorkArea.Top + $monitorMargin)
    $captureWidth = [Math]::Min(
        1038,
        [Math]::Max(320, $script:captureMonitorWorkArea.Width - $monitorMargin * 2)
    )
    $captureHeight = [Math]::Min(
        614,
        [Math]::Max(240, $script:captureMonitorWorkArea.Height - $monitorMargin * 2)
    )
    [void][SelfEngineVisualQaWin32]::SetWindowPos(
        $WindowHandle,
        $topMost,
        $captureLeft,
        $captureTop,
        $captureWidth,
        $captureHeight,
        $showWindow
    )
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

    Write-Host "Capture [$Name]"
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

            Set-CaptureWindowPlacement -WindowHandle $windowHandle
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

function Capture-WindowImageSequence {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [int]$FrameCount = 3,
        [int]$InitialDelaySeconds = 4,
        [int]$IntervalSeconds = 2
    )

    if ($FrameCount -lt 2) {
        throw "Image sequence needs at least 2 frames"
    }

    $stdoutPath = Join-Path $outputRoot "$Name.sequence.out.log"
    $stderrPath = Join-Path $outputRoot "$Name.sequence.err.log"
    Remove-Item -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stderrPath -ErrorAction SilentlyContinue
    $imagePaths = @()
    for ($index = 0; $index -lt $FrameCount; ++$index) {
        $imagePath = Join-Path $outputRoot ("{0}_{1:D2}.png" -f $Name, $index)
        Remove-Item -LiteralPath $imagePath -ErrorAction SilentlyContinue
        $imagePaths += $imagePath
    }

    $runEnvironment = $Environment.Clone()
    $runEnvironment["SE_AUTO_EXIT_FRAMES"] = "1200"

    Write-Host "Capture sequence [$Name]"
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
                throw "$Name exited before sequence capture with code $($process.ExitCode)"
            }
            if ($windowHandle -eq [IntPtr]::Zero) {
                throw "Could not find render window '$WindowTitle' for $Name"
            }

            Set-CaptureWindowPlacement -WindowHandle $windowHandle
            [void][SelfEngineVisualQaWin32]::SetForegroundWindow($windowHandle)
            Start-Sleep -Seconds $InitialDelaySeconds

            for ($index = 0; $index -lt $FrameCount; ++$index) {
                $rect = New-Object SelfEngineVisualQaWin32+RECT
                [void][SelfEngineVisualQaWin32]::GetWindowRect($windowHandle, [ref]$rect)
                $width = $rect.Right - $rect.Left
                $height = $rect.Bottom - $rect.Top
                if ($width -le 0 -or $height -le 0) {
                    throw "Invalid capture bounds for $Name sequence: ${width}x${height}"
                }

                $bitmap = New-Object System.Drawing.Bitmap $width, $height
                $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
                $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
                $bitmap.Save($imagePaths[$index], [System.Drawing.Imaging.ImageFormat]::Png)
                $graphics.Dispose()
                $bitmap.Dispose()

                if ($index -lt ($FrameCount - 1)) {
                    Start-Sleep -Seconds $IntervalSeconds
                }
            }
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
    return $imagePaths
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

function Get-ColorLuma {
    param([Parameter(Mandatory = $true)]$Color)

    return 0.2126 * [double]$Color.R +
        0.7152 * [double]$Color.G +
        0.0722 * [double]$Color.B
}

function Get-ImageEdgeMagnitude {
    param(
        [Parameter(Mandatory = $true)][System.Drawing.Bitmap]$Bitmap,
        [Parameter(Mandatory = $true)][int]$X,
        [Parameter(Mandatory = $true)][int]$Y
    )

    $left = Get-ColorLuma -Color ($Bitmap.GetPixel($X - 2, $Y))
    $right = Get-ColorLuma -Color ($Bitmap.GetPixel($X + 2, $Y))
    $up = Get-ColorLuma -Color ($Bitmap.GetPixel($X, $Y - 2))
    $down = Get-ColorLuma -Color ($Bitmap.GetPixel($X, $Y + 2))
    return [Math]::Abs($right - $left) + [Math]::Abs($down - $up)
}

function Compare-ImageEdges {
    param(
        [Parameter(Mandatory = $true)][string]$A,
        [Parameter(Mandatory = $true)][string]$B,
        [double]$GradientThreshold = 36.0,
        [double]$DeltaThreshold = 8.0
    )

    $bitmapA = [System.Drawing.Bitmap]::FromFile($A)
    $bitmapB = [System.Drawing.Bitmap]::FromFile($B)
    try {
        $width = [Math]::Min($bitmapA.Width, $bitmapB.Width)
        $height = [Math]::Min($bitmapA.Height, $bitmapB.Height)
        $edgePixels = 0
        $changedEdgePixels = 0
        $maxEdgeDelta = 0.0
        $sumEdgeDelta = 0.0

        for ($y = [Math]::Max(2, [int]($height * 0.20)); $y -lt [Math]::Min($height - 2, [int]($height * 0.80)); $y += 4) {
            for ($x = [Math]::Max(2, [int]($width * 0.20)); $x -lt [Math]::Min($width - 2, [int]($width * 0.80)); $x += 4) {
                $edgeA = Get-ImageEdgeMagnitude -Bitmap $bitmapA -X $x -Y $y
                $edgeB = Get-ImageEdgeMagnitude -Bitmap $bitmapB -X $x -Y $y
                if ([Math]::Max($edgeA, $edgeB) -lt $GradientThreshold) {
                    continue
                }

                $colorA = $bitmapA.GetPixel($x, $y)
                $colorB = $bitmapB.GetPixel($x, $y)
                $edgeDelta = [Math]::Abs(
                    (Get-ColorLuma -Color $colorA) -
                    (Get-ColorLuma -Color $colorB)
                )
                if ($edgeDelta -gt $DeltaThreshold) {
                    ++$changedEdgePixels
                }
                if ($edgeDelta -gt $maxEdgeDelta) {
                    $maxEdgeDelta = $edgeDelta
                }
                $sumEdgeDelta += $edgeDelta
                ++$edgePixels
            }
        }

        if ($edgePixels -le 0) {
            throw "No high-contrast sampled edges found between captures: $A $B"
        }

        return [pscustomobject]@{
            EdgePixels = $edgePixels
            ChangedEdgePixels = $changedEdgePixels
            MaxEdgeDelta = [Math]::Round($maxEdgeDelta, 4)
            MeanEdgeDelta = [Math]::Round($sumEdgeDelta / [Math]::Max($edgePixels, 1), 4)
        }
    } finally {
        $bitmapA.Dispose()
        $bitmapB.Dispose()
    }
}

function Compare-ImageSequence {
    param([Parameter(Mandatory = $true)][string[]]$Paths)

    if ($Paths.Count -lt 2) {
        throw "Image sequence comparison needs at least 2 images"
    }

    $pairs = @()
    $minChangedPixels = [int]::MaxValue
    $maxMeanDelta = 0.0
    $maxDelta = 0
    $minEdgePixels = [int]::MaxValue
    $maxChangedEdgePixels = 0
    $maxChangedEdgeRatio = 0.0
    $maxMeanEdgeDelta = 0.0
    $maxEdgeDelta = 0.0
    for ($index = 1; $index -lt $Paths.Count; ++$index) {
        $comparison = Compare-Images -A $Paths[$index - 1] -B $Paths[$index]
        $edgeComparison = Compare-ImageEdges -A $Paths[$index - 1] -B $Paths[$index]
        $changedEdgeRatio =
            [double]$edgeComparison.ChangedEdgePixels /
            [double][Math]::Max([int]$edgeComparison.EdgePixels, 1)
        $pairs += [pscustomobject]@{
            from = $Paths[$index - 1]
            to = $Paths[$index]
            sampledPixels = $comparison.SampledPixels
            changedPixels = $comparison.ChangedPixels
            meanDelta = $comparison.MeanDelta
            maxDelta = $comparison.MaxDelta
            edgePixels = $edgeComparison.EdgePixels
            changedEdgePixels = $edgeComparison.ChangedEdgePixels
            changedEdgeRatio = [Math]::Round($changedEdgeRatio, 4)
            meanEdgeDelta = $edgeComparison.MeanEdgeDelta
            maxEdgeDelta = $edgeComparison.MaxEdgeDelta
        }
        $minChangedPixels = [Math]::Min(
            $minChangedPixels,
            [int]$comparison.ChangedPixels
        )
        $maxMeanDelta = [Math]::Max(
            $maxMeanDelta,
            [double]$comparison.MeanDelta
        )
        $maxDelta = [Math]::Max($maxDelta, [int]$comparison.MaxDelta)
        $minEdgePixels = [Math]::Min(
            $minEdgePixels,
            [int]$edgeComparison.EdgePixels
        )
        $maxChangedEdgePixels = [Math]::Max(
            $maxChangedEdgePixels,
            [int]$edgeComparison.ChangedEdgePixels
        )
        $maxChangedEdgeRatio = [Math]::Max(
            $maxChangedEdgeRatio,
            $changedEdgeRatio
        )
        $maxMeanEdgeDelta = [Math]::Max(
            $maxMeanEdgeDelta,
            [double]$edgeComparison.MeanEdgeDelta
        )
        $maxEdgeDelta = [Math]::Max(
            $maxEdgeDelta,
            [double]$edgeComparison.MaxEdgeDelta
        )
    }

    return [pscustomobject]@{
        pairCount = $pairs.Count
        pairs = $pairs
        minChangedPixels = $minChangedPixels
        maxMeanDelta = [Math]::Round($maxMeanDelta, 4)
        maxDelta = $maxDelta
        minEdgePixels = $minEdgePixels
        maxChangedEdgePixels = $maxChangedEdgePixels
        maxChangedEdgeRatio = [Math]::Round($maxChangedEdgeRatio, 4)
        maxMeanEdgeDelta = [Math]::Round($maxMeanEdgeDelta, 4)
        maxEdgeDelta = [Math]::Round($maxEdgeDelta, 4)
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

function Convert-InvariantDouble {
    param([Parameter(Mandatory = $true)][string]$Value)

    return [double]::Parse(
        $Value,
        [System.Globalization.CultureInfo]::InvariantCulture
    )
}

function Assert-DlssJitterConsistency {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Row
    )

    $epsilon = 0.00001
    $jitterApplied = $Row.temporal_jitter_applied -eq "1"
    $temporalJitterX = Convert-InvariantDouble $Row.temporal_jitter_pixels_x
    $temporalJitterY = Convert-InvariantDouble $Row.temporal_jitter_pixels_y
    $dlssJitterX =
        Convert-InvariantDouble $Row.temporal_upscaler_dlss_jitter_offset_x
    $dlssJitterY =
        Convert-InvariantDouble $Row.temporal_upscaler_dlss_jitter_offset_y

    if (-not $jitterApplied) {
        if ([Math]::Abs($dlssJitterX) -gt $epsilon -or
            [Math]::Abs($dlssJitterY) -gt $epsilon) {
            throw "$Name DLSS jitter is non-zero while projection jitter was not applied: dlss=$dlssJitterX/$dlssJitterY"
        }
        return
    }

    if ([Math]::Abs($dlssJitterX - $temporalJitterX) -gt $epsilon -or
        [Math]::Abs($dlssJitterY - $temporalJitterY) -gt $epsilon) {
        throw "$Name DLSS jitter does not match applied projection jitter: temporal=$temporalJitterX/$temporalJitterY dlss=$dlssJitterX/$dlssJitterY"
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
$defaultSceneDlaaMotionPresentEnvironment = @{
    "SE_RENDER_SCALE" = "1.0"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_TAA_APPLY_JITTER" = "1"
    "SE_UPSCALER_PLUGIN" = "dlss"
    "SE_DLSS_QUALITY" = "dlaa"
    "SE_DLSS_PRESENT" = "1"
    "SE_DLSS_REFERENCE_BASELINE_PATH" = $defaultSceneDlaaMotionBaselineManifestPath
    "SE_BENCHMARK_CAMERA_MOTION" = "orbit"
    "SE_BENCHMARK_CAMERA_MOTION_SPEED" = "0.65"
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$defaultSceneDlaaObjectMotionPresentEnvironment =
    $defaultSceneDlaaMotionPresentEnvironment.Clone()
$defaultSceneDlaaObjectMotionPresentEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $defaultSceneDlaaObjectMotionBaselineManifestPath
$defaultSceneDlaaObjectMotionPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION"] =
    "orbit"
$defaultSceneDlaaObjectMotionPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION_SPEED"] =
    "0.9"
$defaultSceneDlaaObjectMotionPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION_RADIUS"] =
    "0.42"
$importedDynamicDlaaObjectMotionPresentEnvironment =
    $defaultSceneDlaaPresentEnvironment.Clone()
$importedDynamicDlaaObjectMotionPresentEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $importedDynamicDlaaObjectMotionBaselineManifestPath
$importedDynamicDlaaObjectMotionPresentEnvironment["SE_TAA_APPLY_JITTER"] =
    "1"
$importedDynamicDlaaObjectMotionPresentEnvironment["SELFENGINE_MODEL_PATH"] =
    $importedDynamicModelPath
$importedDynamicDlaaObjectMotionPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION"] =
    "orbit"
$importedDynamicDlaaObjectMotionPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION_SPEED"] =
    "0.9"
$importedDynamicDlaaObjectMotionPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION_RADIUS"] =
    "0.42"
$importedDynamicDlaaObjectMotionPresentEnvironment["SE_DEBUG_LOCAL_LIGHTS"] =
    "1"
$importedDynamicDlaaObjectMotionPresentEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] =
    "1"
if (-not $importedDynamicDlaaObjectMotionPresentEnvironment.ContainsKey("SE_BENCHMARK_OBJECT_MOTION") -or
    $importedDynamicDlaaObjectMotionPresentEnvironment.ContainsKey("SE_BENCHMARK_CAMERA_MOTION")) {
    throw "Imported dynamic DLAA lane must be object-motion driven with a static camera"
}
$importedArticulatedDlaaObjectMotionPresentEnvironment =
    $importedDynamicDlaaObjectMotionPresentEnvironment.Clone()
$importedArticulatedDlaaObjectMotionPresentEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $importedArticulatedDlaaObjectMotionBaselineManifestPath
$importedArticulatedDlaaObjectMotionPresentEnvironment["SELFENGINE_MODEL_PATH"] =
    $importedArticulatedModelPath
$importedArticulatedDlaaObjectMotionPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION"] =
    "articulated"
$importedArticulatedDlaaObjectMotionPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION_SPEED"] =
    "1.1"
$importedArticulatedDlaaObjectMotionPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION_RADIUS"] =
    "0.32"
if (-not $importedArticulatedDlaaObjectMotionPresentEnvironment.ContainsKey("SE_BENCHMARK_OBJECT_MOTION") -or
    $importedArticulatedDlaaObjectMotionPresentEnvironment.ContainsKey("SE_BENCHMARK_CAMERA_MOTION")) {
    throw "Imported articulated DLAA lane must be object-motion driven with a static camera"
}
$importedSkinnedDiagnosticPresentEnvironment =
    $importedDynamicDlaaObjectMotionPresentEnvironment.Clone()
$importedSkinnedDiagnosticPresentEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $importedSkinnedDiagnosticBaselineManifestPath
$importedSkinnedDiagnosticPresentEnvironment["SELFENGINE_MODEL_PATH"] =
    $importedSkinnedDiagnosticModelPath
if (-not $importedSkinnedDiagnosticPresentEnvironment.ContainsKey("SE_BENCHMARK_OBJECT_MOTION") -or
    $importedSkinnedDiagnosticPresentEnvironment.ContainsKey("SE_BENCHMARK_CAMERA_MOTION")) {
    throw "Imported skinned diagnostic DLAA lane must be object-motion driven with a static camera"
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

$maskPolicyEnvironmentBase = @{
    "SE_BENCHMARK_SCENE" = "grid"
    "SE_BENCHMARK_ALPHA_BLEND_MATERIAL" = "1"
    "SE_BENCHMARK_OPACITY_BLEND_TEXTURE_MATERIAL" = "1"
    "SE_BENCHMARK_CONSTANT_EMISSIVE_MATERIAL" = "1"
    "SE_RENDER_SCALE" = "0.75"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$maskPolicyNativeEnvironment = $maskPolicyEnvironmentBase.Clone()
$maskPolicyDlssPresentEnvironment = $maskPolicyEnvironmentBase.Clone()
$maskPolicyDlssPresentEnvironment["SE_UPSCALER_PLUGIN"] = "dlss"
$maskPolicyDlssPresentEnvironment["SE_DLSS_PRESENT"] = "1"
$maskPolicyDlssPresentEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $maskPolicyBaselineManifestPath

function New-QuickNativeMetrics {
    param([Parameter(Mandatory = $true)]$Row)

    return [pscustomobject]@{
        postSource = "$($Row.temporal_upscale_post_source_requested)/$($Row.temporal_upscale_post_source_active)/$($Row.temporal_upscale_post_source_fallback_reason)"
        qualityGate = "$($Row.temporal_upscaler_dlss_quality_gate_requested)/$($Row.temporal_upscaler_dlss_quality_gate_ready)/$($Row.temporal_upscaler_dlss_quality_gate_fallback_reason)"
        renderScaleActive = "$($Row.temporal_render_scale_active)"
        renderScaleApplied = "$($Row.temporal_render_scale_applied)"
        mainDraws = "$($Row.main_draws)"
        gbufferDraws = "$($Row.gbuffer_draws)"
        forwardResidualDraws = "$($Row.forward_residual_draws)"
        hybridForwardSpecialDraws = "$($Row.hybrid_forward_special_draws)"
        forwardSpecialMaterialCount = "$($Row.frame_material_forward_special_count)"
        weightedTranslucencyDraws = "$($Row.weighted_translucency_draws)"
        weightedTranslucencyResolveDraws = "$($Row.weighted_translucency_resolve_draws)"
        frameMaterialCount = "$($Row.frame_material_count)"
        frameMaterialTexturedCount = "$($Row.frame_material_textured_count)"
        texturedMaterials = "$($Row.frame_material_textured_count)"
        emissiveHintMaterials = "$($Row.frame_material_emissive_hint_count)"
        alphaMaskMaterials = "$($Row.frame_material_alpha_mask_count)"
        alphaBlendMaterials = "$($Row.frame_material_alpha_blend_count)"
        opacityTextureMaterials = "$($Row.frame_material_opacity_texture_count)"
        frameLightTotalCount = "$($Row.frame_light_total_count)"
        frameLocalLightCount = "$($Row.frame_local_light_count)"
        frameRectLightCount = "$($Row.frame_rect_light_count)"
        reflectionProbeSceneProbeCount = "$($Row.reflection_probe_scene_probe_count)"
    }
}

function New-QuickDlssPresentMetrics {
    param([Parameter(Mandatory = $true)]$Row)

    return [pscustomobject]@{
        postSource = "$($Row.temporal_upscale_post_source_requested)/$($Row.temporal_upscale_post_source_active)/$($Row.temporal_upscale_post_source_fallback_reason)"
        evaluateOutput = "$($Row.temporal_upscaler_dlss_evaluate_result)/$($Row.temporal_upscaler_dlss_output_ready)"
        qualityGate = "$($Row.temporal_upscaler_dlss_quality_gate_requested)/$($Row.temporal_upscaler_dlss_quality_gate_ready)/$($Row.temporal_upscaler_dlss_quality_gate_fallback_reason)"
        qualityMasks = "$($Row.temporal_upscaler_dlss_quality_required_mask)/$($Row.temporal_upscaler_dlss_quality_ready_mask)/$($Row.temporal_upscaler_dlss_quality_blocker_mask)"
        qualityInputs = "output/camera/object/reactive/transparency/exposure/post/baseline=$($Row.temporal_upscaler_dlss_quality_evaluate_output_ready)/$($Row.temporal_upscaler_dlss_quality_camera_motion_ready)/$($Row.temporal_upscaler_dlss_quality_object_motion_ready)/$($Row.temporal_upscaler_dlss_quality_reactive_mask_ready)/$($Row.temporal_upscaler_dlss_quality_transparency_mask_ready)/$($Row.temporal_upscaler_dlss_quality_exposure_policy_ready)/$($Row.temporal_upscaler_dlss_quality_post_ordering_ready)/$($Row.temporal_upscaler_dlss_quality_reference_baseline_ready)"
        renderScale = "$($Row.temporal_render_scale_requested)/$($Row.temporal_render_scale_active)/$($Row.temporal_render_scale_applied)"
        renderScaleActive = "$($Row.temporal_render_scale_active)"
        renderScaleApplied = "$($Row.temporal_render_scale_applied)"
        jitterApplied = "$($Row.temporal_jitter_applied)"
        historyValid = "$($Row.temporal_history_valid)"
        historyReset = "$($Row.temporal_history_reset)"
        historyResetReason = "$($Row.temporal_history_reset_reason)"
        temporalUpscaleInputReady = "$($Row.temporal_upscale_input_ready)"
        nativeTaaResolveEnabled = "$($Row.temporal_taa_resolve_enabled)"
        nativeTaaResolveSuppressedForUpscaler = "$($Row.temporal_taa_resolve_suppressed_for_upscaler)"
        dlssReset = "$($Row.temporal_upscaler_dlss_reset)"
        dlssMotionVectorScale = "$($Row.temporal_upscaler_dlss_motion_vector_scale_x)/$($Row.temporal_upscaler_dlss_motion_vector_scale_y)"
        qualityMode = "$($Row.temporal_upscaler_dlss_quality_mode)"
        recommendedPreset = "$($Row.temporal_upscaler_dlss_recommended_preset)"
        cameraMotionReady = "$($Row.temporal_upscaler_dlss_quality_camera_motion_ready)"
        objectMotionReady = "$($Row.temporal_upscaler_dlss_quality_object_motion_ready)"
        mainDraws = "$($Row.main_draws)"
        gbufferDraws = "$($Row.gbuffer_draws)"
        forwardResidualDraws = "$($Row.forward_residual_draws)"
        hybridForwardSpecialDraws = "$($Row.hybrid_forward_special_draws)"
        forwardResidualSharedLightListDraws = "$($Row.forward_residual_shared_light_list_draws)"
        forwardResidualVelocityDraws = "$($Row.forward_residual_velocity_draws)"
        forwardSpecialMaterialCount = "$($Row.frame_material_forward_special_count)"
        weightedTranslucencyDraws = "$($Row.weighted_translucency_draws)"
        weightedTranslucencyResolveDraws = "$($Row.weighted_translucency_resolve_draws)"
        weightedTranslucencyVelocityDraws = "$($Row.weighted_translucency_velocity_draws)"
        dlssMaskDraws = "$($Row.dlss_mask_draws)"
        dlssMaskWeightedTranslucencyDraws = "$($Row.dlss_mask_weighted_translucency_draws)"
        dlssMaskForwardResidualDraws = "$($Row.dlss_mask_forward_residual_draws)"
        frameMaterialCount = "$($Row.frame_material_count)"
        frameMaterialTexturedCount = "$($Row.frame_material_textured_count)"
        texturedMaterials = "$($Row.frame_material_textured_count)"
        emissiveHintMaterials = "$($Row.frame_material_emissive_hint_count)"
        alphaMaskMaterials = "$($Row.frame_material_alpha_mask_count)"
        alphaBlendMaterials = "$($Row.frame_material_alpha_blend_count)"
        opacityTextureMaterials = "$($Row.frame_material_opacity_texture_count)"
        specularTextureMaterials = "$($Row.frame_material_specular_texture_count)"
        uvTransformMaterials = "$($Row.frame_material_uv_transform_count)"
        doubleSidedMaterials = "$($Row.frame_material_double_sided_count)"
        clearcoatTextureMaterials = "$($Row.frame_material_clearcoat_texture_count)"
        clearcoatRoughnessTextureMaterials = "$($Row.frame_material_clearcoat_roughness_texture_count)"
        transmissionTextureMaterials = "$($Row.frame_material_transmission_texture_count)"
        volumeMaterials = "$($Row.frame_material_volume_count)"
        frameLightTotalCount = "$($Row.frame_light_total_count)"
        frameLocalLightCount = "$($Row.frame_local_light_count)"
        frameRectLightCount = "$($Row.frame_rect_light_count)"
        reflectionProbeSceneProbeCount = "$($Row.reflection_probe_scene_probe_count)"
        runtimeImportModelRequested = "$($Row.runtime_import_model_requested)"
        runtimeImportModelLoaded = "$($Row.runtime_import_model_loaded)"
        runtimeImportCacheHit = "$($Row.runtime_import_cache_hit)"
        runtimeImportMeshCount = "$($Row.runtime_import_mesh_count)"
        runtimeImportMaterialCount = "$($Row.runtime_import_material_count)"
        runtimeImportNodeCount = "$($Row.runtime_import_node_count)"
        runtimeImportBoneNodeCount = "$($Row.runtime_import_bone_node_count)"
        runtimeImportAnimationChannelBoundCount = "$($Row.runtime_import_animation_channel_bound_count)"
        runtimeImportAnimationChannelUnboundCount = "$($Row.runtime_import_animation_channel_unbound_count)"
        runtimeImportBoneNameMatchedNodeCount = "$($Row.runtime_import_bone_name_matched_node_count)"
        runtimeImportBoneNameUnmatchedCount = "$($Row.runtime_import_bone_name_unmatched_count)"
        runtimeImportAnimationCount = "$($Row.runtime_import_animation_count)"
        runtimeImportAnimationChannelCount = "$($Row.runtime_import_animation_channel_count)"
        runtimeImportAnimationPositionKeyCount = "$($Row.runtime_import_animation_position_key_count)"
        runtimeImportAnimationRotationKeyCount = "$($Row.runtime_import_animation_rotation_key_count)"
        runtimeImportAnimationScaleKeyCount = "$($Row.runtime_import_animation_scale_key_count)"
        runtimeImportAnimationKeyCount = "$($Row.runtime_import_animation_key_count)"
        runtimeImportMaxAnimationKeysPerChannel = "$($Row.runtime_import_max_animation_keys_per_channel)"
        runtimeImportPoseSampledClipCount = "$($Row.runtime_import_pose_sampled_clip_count)"
        runtimeImportPoseSampledChannelCount = "$($Row.runtime_import_pose_sampled_channel_count)"
        runtimeImportPoseSampledNodeCount = "$($Row.runtime_import_pose_sampled_node_count)"
        runtimeImportPoseAnimatedNodeCount = "$($Row.runtime_import_pose_animated_node_count)"
        runtimeImportPoseBonePaletteEntryCount = "$($Row.runtime_import_pose_bone_palette_entry_count)"
        runtimeImportPosePreviousBonePaletteEntryCount = "$($Row.runtime_import_pose_previous_bone_palette_entry_count)"
        runtimeImportPoseChangedBonePaletteEntryCount = "$($Row.runtime_import_pose_changed_bone_palette_entry_count)"
        runtimeImportPoseBonePaletteReady = "$($Row.runtime_import_pose_bone_palette_ready)"
        runtimeImportPoseCarrierBonePaletteEntryCount = "$($Row.runtime_import_pose_carrier_bone_palette_entry_count)"
        runtimeImportPoseCarrierPreviousBonePaletteEntryCount = "$($Row.runtime_import_pose_carrier_previous_bone_palette_entry_count)"
        runtimeImportPoseCarrierChangedBonePaletteEntryCount = "$($Row.runtime_import_pose_carrier_changed_bone_palette_entry_count)"
        runtimeImportPoseCarrierReady = "$($Row.runtime_import_pose_carrier_ready)"
        runtimeImportRendererPosePaletteRegistered = "$($Row.runtime_import_renderer_pose_palette_registered)"
        runtimeImportRendererPosePaletteBonePaletteEntryCount = "$($Row.runtime_import_renderer_pose_palette_bone_palette_entry_count)"
        runtimeImportRendererPosePalettePreviousBonePaletteEntryCount = "$($Row.runtime_import_renderer_pose_palette_previous_bone_palette_entry_count)"
        runtimeImportRendererPosePaletteChangedBonePaletteEntryCount = "$($Row.runtime_import_renderer_pose_palette_changed_bone_palette_entry_count)"
        runtimeImportRendererPosePaletteReady = "$($Row.runtime_import_renderer_pose_palette_ready)"
        runtimeImportGpuPosePaletteBufferAllocated = "$($Row.runtime_import_gpu_pose_palette_buffer_allocated)"
        runtimeImportGpuPosePaletteBufferUploaded = "$($Row.runtime_import_gpu_pose_palette_buffer_uploaded)"
        runtimeImportGpuPosePaletteDescriptorInfoReady = "$($Row.runtime_import_gpu_pose_palette_descriptor_info_ready)"
        runtimeImportGpuPosePaletteDescriptorSetAllocated = "$($Row.runtime_import_gpu_pose_palette_descriptor_set_allocated)"
        runtimeImportGpuPosePaletteDescriptorSetWritten = "$($Row.runtime_import_gpu_pose_palette_descriptor_set_written)"
        runtimeImportGpuPosePaletteDescriptorSetReady = "$($Row.runtime_import_gpu_pose_palette_descriptor_set_ready)"
        runtimeImportGpuPosePaletteDescriptorBinding = "$($Row.runtime_import_gpu_pose_palette_descriptor_binding)"
        runtimeImportGpuPosePaletteDescriptorRangeBytes = "$($Row.runtime_import_gpu_pose_palette_descriptor_range_bytes)"
        runtimeImportGpuPosePaletteBufferBytes = "$($Row.runtime_import_gpu_pose_palette_buffer_bytes)"
        runtimeImportGpuPosePaletteCurrentEntryCount = "$($Row.runtime_import_gpu_pose_palette_current_entry_count)"
        runtimeImportGpuPosePalettePreviousEntryCount = "$($Row.runtime_import_gpu_pose_palette_previous_entry_count)"
        runtimeImportMeshWithBonesCount = "$($Row.runtime_import_mesh_with_bones_count)"
        runtimeImportBoneCount = "$($Row.runtime_import_bone_count)"
        runtimeImportSkinnedVertexCount = "$($Row.runtime_import_skinned_vertex_count)"
        runtimeImportBoneInfluenceCount = "$($Row.runtime_import_bone_influence_count)"
        runtimeImportMaxBoneInfluencesPerVertex = "$($Row.runtime_import_max_bone_influences_per_vertex)"
        runtimeImportSkinnedVertexAttributeCount = "$($Row.runtime_import_skinned_vertex_attribute_count)"
        runtimeImportBoneAttributeInfluenceCount = "$($Row.runtime_import_bone_attribute_influence_count)"
        runtimeImportMaxBoneAttributeInfluencesPerVertex = "$($Row.runtime_import_max_bone_attribute_influences_per_vertex)"
        runtimeImportBoneInfluenceOverflowCount = "$($Row.runtime_import_bone_influence_overflow_count)"
        runtimeImportSkinnedVertexAttributeReady = "$($Row.runtime_import_skinned_vertex_attribute_ready)"
        rendererSkinnedVertexAttributeStrideBytes = "$($Row.renderer_skinned_vertex_attribute_stride_bytes)"
        rendererSkinnedVertexAttributeBoneIndicesLocation = "$($Row.renderer_skinned_vertex_attribute_bone_indices_location)"
        rendererSkinnedVertexAttributeBoneWeightsLocation = "$($Row.renderer_skinned_vertex_attribute_bone_weights_location)"
        rendererSkinnedVertexAttributeBoneIndicesOffset = "$($Row.renderer_skinned_vertex_attribute_bone_indices_offset)"
        rendererSkinnedVertexAttributeBoneWeightsOffset = "$($Row.renderer_skinned_vertex_attribute_bone_weights_offset)"
        rendererSkinnedVertexAttributePathReady = "$($Row.renderer_skinned_vertex_attribute_path_ready)"
        runtimeImportSkinnedAnimationUnsupported = "$($Row.runtime_import_skinned_animation_unsupported)"
        bonePaletteDrawCommandCount = "$($Row.bone_palette_draw_command_count)"
        bonePaletteDrawReadyCommandCount = "$($Row.bone_palette_draw_ready_command_count)"
        bonePaletteDrawResourceCount = "$($Row.bone_palette_draw_resource_count)"
        bonePaletteDrawReadyResourceCount = "$($Row.bone_palette_draw_ready_resource_count)"
        bonePaletteDrawCurrentEntryCount = "$($Row.bone_palette_draw_current_entry_count)"
        bonePaletteDrawPreviousEntryCount = "$($Row.bone_palette_draw_previous_entry_count)"
        bonePaletteDrawChangedEntryCount = "$($Row.bone_palette_draw_changed_entry_count)"
        bonePaletteDrawPathReady = "$($Row.bone_palette_draw_path_ready)"
        bonePaletteDrawDescriptorCommandCount = "$($Row.bone_palette_draw_descriptor_command_count)"
        bonePaletteDrawDescriptorReadyCommandCount = "$($Row.bone_palette_draw_descriptor_ready_command_count)"
        bonePaletteDrawDescriptorResourceCount = "$($Row.bone_palette_draw_descriptor_resource_count)"
        bonePaletteDrawDescriptorReadyResourceCount = "$($Row.bone_palette_draw_descriptor_ready_resource_count)"
        bonePaletteDrawDescriptorSetIndex = "$($Row.bone_palette_draw_descriptor_set_index)"
        bonePaletteDrawDescriptorBinding = "$($Row.bone_palette_draw_descriptor_binding)"
        bonePaletteDrawDescriptorRangeBytes = "$($Row.bone_palette_draw_descriptor_range_bytes)"
        bonePaletteDrawDescriptorPathReady = "$($Row.bone_palette_draw_descriptor_path_ready)"
        bonePaletteShaderConsumerCommandCount = "$($Row.bone_palette_shader_consumer_command_count)"
        bonePaletteShaderConsumerReadyCommandCount = "$($Row.bone_palette_shader_consumer_ready_command_count)"
        bonePaletteShaderConsumerFallbackDescriptorReady = "$($Row.bone_palette_shader_consumer_fallback_descriptor_ready)"
        bonePaletteShaderConsumerPathReady = "$($Row.bone_palette_shader_consumer_path_ready)"
        mainBonePaletteDescriptorBinds = "$($Row.main_bone_palette_descriptor_binds)"
        gbufferBonePaletteDescriptorBinds = "$($Row.gbuffer_bone_palette_descriptor_binds)"
        bonePaletteDescriptorBinds = "$($Row.bone_palette_descriptor_binds)"
        gbufferBonePaletteFallbackDescriptorBinds = "$($Row.gbuffer_bone_palette_fallback_descriptor_binds)"
        bonePaletteFallbackDescriptorBinds = "$($Row.bone_palette_fallback_descriptor_binds)"
        dlssExtents = "$($Row.temporal_upscaler_dlss_render_width)x$($Row.temporal_upscaler_dlss_render_height)->$($Row.temporal_upscaler_dlss_output_width)x$($Row.temporal_upscaler_dlss_output_height)"
    }
}

function Assert-ExpectedMetricText {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Metrics,
        [Parameter(Mandatory = $true)]$Expected
    )

    foreach ($property in $Expected.PSObject.Properties) {
        $actualProperty = $Metrics.PSObject.Properties[$property.Name]
        if ($null -eq $actualProperty) {
            throw "Metric '$($property.Name)' is not available for $Name"
        }
        Assert-BaselineText `
            -Name "$Name.$($property.Name)" `
            -Actual ([string]$actualProperty.Value) `
            -Expected ([string]$property.Value)
    }
}

function Assert-QuickNativeRow {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)]$Expected
    )

    if ($Row.framegraph_validation_issues -ne "0") {
        throw "$Name frame graph validation issues: $($Row.framegraph_validation_issues)"
    }
    if ($Row.temporal_upscale_post_source_active -ne "0") {
        throw "$Name unexpectedly activated temporal-upscale post source"
    }

    $metrics = New-QuickNativeMetrics -Row $Row
    Assert-ExpectedMetricText -Name $Name -Metrics $metrics -Expected $Expected
    return $metrics
}

function Assert-QuickDlssPresentRow {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)]$Expected
    )

    if ($Row.framegraph_validation_issues -ne "0") {
        throw "$Name frame graph validation issues: $($Row.framegraph_validation_issues)"
    }
    if ($Row.temporal_upscaler_dlss_output_ready -ne "1") {
        throw "$Name did not produce DLSS output"
    }
    $expectedQualityBlockerMask = "0"
    $expectedQualityMasksProperty = $Expected.PSObject.Properties["qualityMasks"]
    if ($null -ne $expectedQualityMasksProperty) {
        $expectedQualityMaskParts =
            ([string]$expectedQualityMasksProperty.Value).Split('/')
        if ($expectedQualityMaskParts.Count -ge 3) {
            $expectedQualityBlockerMask =
                $expectedQualityMaskParts[$expectedQualityMaskParts.Count - 1]
        }
    }
    if ($Row.temporal_upscaler_dlss_quality_blocker_mask -ne "0" -and
        $expectedQualityBlockerMask -eq "0") {
        throw "$Name quality gate still reports blockers: $($Row.temporal_upscaler_dlss_quality_blocker_mask)"
    }

    $metrics = New-QuickDlssPresentMetrics -Row $Row
    Assert-ExpectedMetricText -Name $Name -Metrics $metrics -Expected $Expected
    Assert-DlssJitterConsistency -Name $Name -Row $Row
    return $metrics
}

function Assert-QuickImageStats {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Stats,
        [Parameter(Mandatory = $true)]$Manifest
    )

    $index = 0
    foreach ($stat in @($Stats)) {
        Assert-BaselineRange `
            -Name "$Name.imageStats[$index].differentPixels" `
            -Actual $stat.DifferentPixels `
            -Min $Manifest.thresholds.centralDifferentPixelsMin `
            -Max $stat.SampledPixels
        ++$index
    }
}

function Assert-QuickComparison {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Comparison,
        [Parameter(Mandatory = $true)]$Manifest
    )

    Assert-BaselineRange `
        -Name "$Name.comparison.changedPixels" `
        -Actual $Comparison.ChangedPixels `
        -Min $Manifest.thresholds.comparisonChangedPixelsMin `
        -Max $Manifest.thresholds.comparisonChangedPixelsMax
    Assert-BaselineRange `
        -Name "$Name.comparison.meanDelta" `
        -Actual $Comparison.MeanDelta `
        -Min $Manifest.thresholds.comparisonMeanDeltaMin `
        -Max $Manifest.thresholds.comparisonMeanDeltaMax
    Assert-BaselineRange `
        -Name "$Name.comparison.maxDelta" `
        -Actual $Comparison.MaxDelta `
        -Min 0 `
        -Max $Manifest.thresholds.comparisonMaxDeltaMax
}

function Assert-QuickSequenceComparison {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Comparison,
        [Parameter(Mandatory = $true)]$Manifest
    )

    Assert-BaselineRange `
        -Name "$Name.sequence.minChangedPixels" `
        -Actual $Comparison.minChangedPixels `
        -Min $Manifest.thresholds.sequencePairChangedPixelsMin `
        -Max $Comparison.pairs[0].SampledPixels
    Assert-BaselineRange `
        -Name "$Name.sequence.maxMeanDelta" `
        -Actual $Comparison.maxMeanDelta `
        -Min 0 `
        -Max $Manifest.thresholds.sequencePairMeanDeltaMax
    Assert-BaselineRange `
        -Name "$Name.sequence.minEdgePixels" `
        -Actual $Comparison.minEdgePixels `
        -Min $Manifest.thresholds.sequencePairEdgePixelsMin `
        -Max $Comparison.pairs[0].SampledPixels
    Assert-BaselineRange `
        -Name "$Name.sequence.maxChangedEdgePixels" `
        -Actual $Comparison.maxChangedEdgePixels `
        -Min 0 `
        -Max $Manifest.thresholds.sequencePairChangedEdgePixelsMax
    Assert-BaselineRange `
        -Name "$Name.sequence.maxChangedEdgeRatio" `
        -Actual $Comparison.maxChangedEdgeRatio `
        -Min 0 `
        -Max $Manifest.thresholds.sequencePairChangedEdgeRatioMax
    Assert-BaselineRange `
        -Name "$Name.sequence.maxMeanEdgeDelta" `
        -Actual $Comparison.maxMeanEdgeDelta `
        -Min 0 `
        -Max $Manifest.thresholds.sequencePairMeanEdgeDeltaMax
    Assert-BaselineRange `
        -Name "$Name.sequence.maxEdgeDelta" `
        -Actual $Comparison.maxEdgeDelta `
        -Min 0 `
        -Max $Manifest.thresholds.sequencePairMaxEdgeDeltaMax
}

function New-QuickSequenceThresholdSummary {
    param([Parameter(Mandatory = $true)]$Manifest)

    return [ordered]@{
        centralDifferentPixelsMin = [int]$Manifest.thresholds.centralDifferentPixelsMin
        sequencePairChangedPixelsMin = [int]$Manifest.thresholds.sequencePairChangedPixelsMin
        sequencePairMeanDeltaMax = [double]$Manifest.thresholds.sequencePairMeanDeltaMax
        sequencePairEdgePixelsMin = [int]$Manifest.thresholds.sequencePairEdgePixelsMin
        sequencePairChangedEdgePixelsMax = [int]$Manifest.thresholds.sequencePairChangedEdgePixelsMax
        sequencePairChangedEdgeRatioMax = [double]$Manifest.thresholds.sequencePairChangedEdgeRatioMax
        sequencePairMeanEdgeDeltaMax = [double]$Manifest.thresholds.sequencePairMeanEdgeDeltaMax
        sequencePairMaxEdgeDeltaMax = [double]$Manifest.thresholds.sequencePairMaxEdgeDeltaMax
    }
}

function New-QuickComparisonThresholdSummary {
    param([Parameter(Mandatory = $true)]$Manifest)

    return [ordered]@{
        centralDifferentPixelsMin = [int]$Manifest.thresholds.centralDifferentPixelsMin
        comparisonChangedPixelsMin = [int]$Manifest.thresholds.comparisonChangedPixelsMin
        comparisonChangedPixelsMax = [int]$Manifest.thresholds.comparisonChangedPixelsMax
        comparisonMeanDeltaMin = [double]$Manifest.thresholds.comparisonMeanDeltaMin
        comparisonMeanDeltaMax = [double]$Manifest.thresholds.comparisonMeanDeltaMax
        comparisonMaxDeltaMax = [int]$Manifest.thresholds.comparisonMaxDeltaMax
    }
}

function New-QuickLaneSummary {
    param(
        [Parameter(Mandatory = $true)]$Benchmark,
        [Parameter(Mandatory = $true)]$Metrics,
        [string[]]$Images = @(),
        [object[]]$ImageStats = @(),
        $Comparison = $null,
        $SequenceComparison = $null,
        [string]$Model = ""
    )

    $lane = [ordered]@{
        csv = $Benchmark.CsvPath
        columns = "$($Benchmark.HeaderColumns)/$($Benchmark.LastColumns)"
        metrics = $Metrics
    }

    if ($Images.Count -eq 1) {
        $lane["image"] = $Images[0]
    } elseif ($Images.Count -gt 1) {
        $lane["images"] = $Images
    }
    if ($ImageStats.Count -eq 1) {
        $lane["imageStats"] = $ImageStats[0]
    } elseif ($ImageStats.Count -gt 1) {
        $lane["imageStats"] = $ImageStats
    }
    if ($null -ne $Comparison) {
        $lane["comparison"] = $Comparison
    }
    if ($null -ne $SequenceComparison) {
        $lane["sequenceComparison"] = $SequenceComparison
    }
    if ($Model.Length -gt 0) {
        $lane["model"] = $Model
    }

    return $lane
}

function Invoke-QuickDlssBenchmarkSuite {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)]$Manifest,
        [string]$Model = ""
    )

    $benchmark = Invoke-BenchmarkRun `
        -Name $Name `
        -Environment $Environment `
        -UseApplicationScene
    $metrics = Assert-QuickDlssPresentRow `
        -Name $LaneKey `
        -Row $benchmark.LastRow `
        -Expected $Manifest.expected.dlssPresent

    return New-QuickLaneSummary `
        -Benchmark $benchmark `
        -Metrics $metrics `
        -Model $Model
}

function Invoke-QuickDlssPairSuite {
    param(
        [Parameter(Mandatory = $true)][string]$NativeName,
        [Parameter(Mandatory = $true)][string]$DlssName,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$NativeEnvironment,
        [Parameter(Mandatory = $true)][hashtable]$DlssEnvironment,
        [Parameter(Mandatory = $true)]$Manifest
    )

    $nativeBenchmark = Invoke-BenchmarkRun `
        -Name $NativeName `
        -Environment $NativeEnvironment
    $nativeMetrics = Assert-QuickNativeRow `
        -Name "$LaneKey.native" `
        -Row $nativeBenchmark.LastRow `
        -Expected $Manifest.expected.native
    $dlssBenchmark = Invoke-BenchmarkRun `
        -Name $DlssName `
        -Environment $DlssEnvironment
    $dlssMetrics = Assert-QuickDlssPresentRow `
        -Name "$LaneKey.dlssPresent" `
        -Row $dlssBenchmark.LastRow `
        -Expected $Manifest.expected.dlssPresent

    $nativeImage = Capture-WindowImage `
        -Name $NativeName `
        -Environment $NativeEnvironment
    $dlssImage = Capture-WindowImage `
        -Name $DlssName `
        -Environment $DlssEnvironment
    $nativeImageStats = Get-ImageVariationStats -Path $nativeImage
    $dlssImageStats = Get-ImageVariationStats -Path $dlssImage
    $comparison = Compare-Images -A $nativeImage -B $dlssImage

    Assert-QuickImageStats `
        -Name "$LaneKey.native" `
        -Stats $nativeImageStats `
        -Manifest $Manifest
    Assert-QuickImageStats `
        -Name "$LaneKey.dlssPresent" `
        -Stats $dlssImageStats `
        -Manifest $Manifest
    Assert-QuickComparison `
        -Name $LaneKey `
        -Comparison $comparison `
        -Manifest $Manifest

    return [pscustomobject]@{
        NativeLane = New-QuickLaneSummary `
            -Benchmark $nativeBenchmark `
            -Metrics $nativeMetrics `
            -Images @($nativeImage) `
            -ImageStats @($nativeImageStats)
        DlssLane = New-QuickLaneSummary `
            -Benchmark $dlssBenchmark `
            -Metrics $dlssMetrics `
            -Images @($dlssImage) `
            -ImageStats @($dlssImageStats) `
            -Comparison $comparison
    }
}

function Invoke-QuickDlaaSequenceSuite {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)]$Manifest,
        [string]$Model = ""
    )

    $benchmark = Invoke-BenchmarkRun `
        -Name $Name `
        -Environment $Environment `
        -UseApplicationScene
    $metrics = Assert-QuickDlssPresentRow `
        -Name $LaneKey `
        -Row $benchmark.LastRow `
        -Expected $Manifest.expected.dlssPresent
    $images = Capture-WindowImageSequence `
        -Name $Name `
        -Environment $Environment `
        -FrameCount 3 `
        -InitialDelaySeconds 4 `
        -IntervalSeconds 2
    $imageStats = @()
    foreach ($image in $images) {
        $imageStats += Get-ImageVariationStats -Path $image
    }
    $sequenceComparison = Compare-ImageSequence -Paths $images

    Assert-QuickImageStats -Name $LaneKey -Stats $imageStats -Manifest $Manifest
    Assert-QuickSequenceComparison `
        -Name $LaneKey `
        -Comparison $sequenceComparison `
        -Manifest $Manifest

    return New-QuickLaneSummary `
        -Benchmark $benchmark `
        -Metrics $metrics `
        -Images $images `
        -ImageStats $imageStats `
        -SequenceComparison $sequenceComparison `
        -Model $Model
}

if (-not ($selectedSuites -contains "full")) {
    $requestedSuiteText = $requestedSuites -join ', '
    $selectedSuiteText = $selectedSuites -join ', '
    Write-Host "Focused DLSS visual QA suites: $selectedSuiteText"
    if ($requestedSuiteText -ne $selectedSuiteText) {
        Write-Host "Requested suite groups: $requestedSuiteText"
    }
    $quickSummary = [ordered]@{
        target = $Target
        generatedAt = (Get-Date).ToString("o")
        requestedSuites = $requestedSuites
        selectedSuites = $selectedSuites
        suiteGroups = $suiteGroups
        captureMonitor = $script:captureMonitorWorkArea
        baselines = [ordered]@{}
        thresholds = [ordered]@{}
        lanes = [ordered]@{}
    }

    if ($selectedSuites -contains "default") {
        $quickSummary["baselines"]["default"] = [ordered]@{
            manifest = $defaultSceneDlaaBaselineManifestPath
            name = $defaultSceneDlaaBaselineManifest.name
        }

        $defaultSceneDlaaNativeBenchmark = Invoke-BenchmarkRun `
            -Name "default_scene_dlaa_native_deferred_hdr" `
            -Environment $defaultSceneDlaaNativeEnvironment `
            -UseApplicationScene
        $defaultSceneDlaaNativeMetrics = Assert-QuickNativeRow `
            -Name "defaultSceneDlaa.native" `
            -Row $defaultSceneDlaaNativeBenchmark.LastRow `
            -Expected $defaultSceneDlaaBaselineManifest.expected.native
        $defaultSceneDlaaBenchmark = Invoke-BenchmarkRun `
            -Name "default_scene_dlaa_present" `
            -Environment $defaultSceneDlaaPresentEnvironment `
            -UseApplicationScene
        $defaultSceneDlaaMetrics = Assert-QuickDlssPresentRow `
            -Name "defaultSceneDlaa.dlssPresent" `
            -Row $defaultSceneDlaaBenchmark.LastRow `
            -Expected $defaultSceneDlaaBaselineManifest.expected.dlssPresent

        $defaultSceneDlaaNativeImage = Capture-WindowImage `
            -Name "default_scene_dlaa_native_deferred_hdr" `
            -Environment $defaultSceneDlaaNativeEnvironment
        $defaultSceneDlaaImage = Capture-WindowImage `
            -Name "default_scene_dlaa_present" `
            -Environment $defaultSceneDlaaPresentEnvironment
        $defaultSceneDlaaNativeImageStats =
            Get-ImageVariationStats -Path $defaultSceneDlaaNativeImage
        $defaultSceneDlaaImageStats =
            Get-ImageVariationStats -Path $defaultSceneDlaaImage
        $defaultSceneDlaaComparison =
            Compare-Images -A $defaultSceneDlaaNativeImage -B $defaultSceneDlaaImage

        Assert-QuickImageStats `
            -Name "defaultSceneDlaa.native" `
            -Stats $defaultSceneDlaaNativeImageStats `
            -Manifest $defaultSceneDlaaBaselineManifest
        Assert-QuickImageStats `
            -Name "defaultSceneDlaa.dlssPresent" `
            -Stats $defaultSceneDlaaImageStats `
            -Manifest $defaultSceneDlaaBaselineManifest
        Assert-QuickComparison `
            -Name "defaultSceneDlaa" `
            -Comparison $defaultSceneDlaaComparison `
            -Manifest $defaultSceneDlaaBaselineManifest

        $quickSummary["lanes"]["defaultSceneDlaaNative"] = New-QuickLaneSummary `
            -Benchmark $defaultSceneDlaaNativeBenchmark `
            -Metrics $defaultSceneDlaaNativeMetrics `
            -Images @($defaultSceneDlaaNativeImage) `
            -ImageStats @($defaultSceneDlaaNativeImageStats)
        $quickSummary["lanes"]["defaultSceneDlaaPresent"] = New-QuickLaneSummary `
            -Benchmark $defaultSceneDlaaBenchmark `
            -Metrics $defaultSceneDlaaMetrics `
            -Images @($defaultSceneDlaaImage) `
            -ImageStats @($defaultSceneDlaaImageStats) `
            -Comparison $defaultSceneDlaaComparison
    }

    if ($selectedSuites -contains "default-motion") {
        $quickSummary["baselines"]["defaultMotion"] = [ordered]@{
            manifest = $defaultSceneDlaaMotionBaselineManifestPath
            name = $defaultSceneDlaaMotionBaselineManifest.name
        }
        $quickSummary["thresholds"]["defaultMotion"] =
            New-QuickSequenceThresholdSummary `
                -Manifest $defaultSceneDlaaMotionBaselineManifest
        $quickSummary["lanes"]["defaultSceneDlaaMotionPresent"] =
            Invoke-QuickDlaaSequenceSuite `
                -Name "default_scene_dlaa_motion_present" `
                -LaneKey "defaultSceneDlaaMotion" `
                -Environment $defaultSceneDlaaMotionPresentEnvironment `
                -Manifest $defaultSceneDlaaMotionBaselineManifest
    }

    if ($selectedSuites -contains "default-object-motion") {
        $quickSummary["baselines"]["defaultObjectMotion"] = [ordered]@{
            manifest = $defaultSceneDlaaObjectMotionBaselineManifestPath
            name = $defaultSceneDlaaObjectMotionBaselineManifest.name
        }
        $quickSummary["thresholds"]["defaultObjectMotion"] =
            New-QuickSequenceThresholdSummary `
                -Manifest $defaultSceneDlaaObjectMotionBaselineManifest
        $quickSummary["lanes"]["defaultSceneDlaaObjectMotionPresent"] =
            Invoke-QuickDlaaSequenceSuite `
                -Name "default_scene_dlaa_object_motion_present" `
                -LaneKey "defaultSceneDlaaObjectMotion" `
                -Environment $defaultSceneDlaaObjectMotionPresentEnvironment `
                -Manifest $defaultSceneDlaaObjectMotionBaselineManifest
    }

    if ($selectedSuites -contains "imported-dynamic") {
        $quickSummary["baselines"]["importedDynamic"] = [ordered]@{
            manifest = $importedDynamicDlaaObjectMotionBaselineManifestPath
            name = $importedDynamicDlaaObjectMotionBaselineManifest.name
            model = $importedDynamicModelPath
        }
        $quickSummary["thresholds"]["importedDynamic"] =
            New-QuickSequenceThresholdSummary `
                -Manifest $importedDynamicDlaaObjectMotionBaselineManifest
        $quickSummary["lanes"]["importedDynamicDlaaObjectMotionPresent"] =
            Invoke-QuickDlaaSequenceSuite `
                -Name "imported_dynamic_dlaa_object_motion_present" `
                -LaneKey "importedDynamicDlaaObjectMotion" `
                -Environment $importedDynamicDlaaObjectMotionPresentEnvironment `
                -Manifest $importedDynamicDlaaObjectMotionBaselineManifest `
                -Model $importedDynamicModelPath
    }

    if ($selectedSuites -contains "imported-articulated") {
        $quickSummary["baselines"]["importedArticulated"] = [ordered]@{
            manifest = $importedArticulatedDlaaObjectMotionBaselineManifestPath
            name = $importedArticulatedDlaaObjectMotionBaselineManifest.name
            model = $importedArticulatedModelPath
        }
        $quickSummary["thresholds"]["importedArticulated"] =
            New-QuickSequenceThresholdSummary `
                -Manifest $importedArticulatedDlaaObjectMotionBaselineManifest
        $quickSummary["lanes"]["importedArticulatedDlaaObjectMotionPresent"] =
            Invoke-QuickDlaaSequenceSuite `
                -Name "imported_articulated_dlaa_object_motion_present" `
                -LaneKey "importedArticulatedDlaaObjectMotion" `
                -Environment $importedArticulatedDlaaObjectMotionPresentEnvironment `
                -Manifest $importedArticulatedDlaaObjectMotionBaselineManifest `
                -Model $importedArticulatedModelPath
    }

    if ($selectedSuites -contains "imported-skinned-diagnostic") {
        $quickSummary["baselines"]["importedSkinnedDiagnostic"] = [ordered]@{
            manifest = $importedSkinnedDiagnosticBaselineManifestPath
            name = $importedSkinnedDiagnosticBaselineManifest.name
            model = $importedSkinnedDiagnosticModelPath
        }
        $quickSummary["lanes"]["importedSkinnedDiagnosticPresent"] =
            Invoke-QuickDlssBenchmarkSuite `
                -Name "imported_skinned_diagnostic_present" `
                -LaneKey "importedSkinnedDiagnostic" `
                -Environment $importedSkinnedDiagnosticPresentEnvironment `
                -Manifest $importedSkinnedDiagnosticBaselineManifest `
                -Model $importedSkinnedDiagnosticModelPath
    }

    if ($selectedSuites -contains "wboit") {
        $quickSummary["baselines"]["wboit"] = [ordered]@{
            manifest = $wboitBaselineManifestPath
            name = $wboitBaselineManifest.name
        }
        $quickSummary["thresholds"]["wboit"] =
            New-QuickComparisonThresholdSummary -Manifest $wboitBaselineManifest
        $wboitLanes = Invoke-QuickDlssPairSuite `
            -NativeName "wboit_native_deferred_hdr" `
            -DlssName "wboit_dlss_present" `
            -LaneKey "wboit" `
            -NativeEnvironment $wboitNativeEnvironment `
            -DlssEnvironment $wboitDlssPresentEnvironment `
            -Manifest $wboitBaselineManifest
        $quickSummary["lanes"]["wboitNative"] = $wboitLanes.NativeLane
        $quickSummary["lanes"]["wboitDlssPresent"] = $wboitLanes.DlssLane
    }

    if ($selectedSuites -contains "forward-special") {
        $quickSummary["baselines"]["forwardSpecial"] = [ordered]@{
            manifest = $forwardSpecialBaselineManifestPath
            name = $forwardSpecialBaselineManifest.name
        }
        $quickSummary["thresholds"]["forwardSpecial"] =
            New-QuickComparisonThresholdSummary -Manifest $forwardSpecialBaselineManifest
        $forwardSpecialLanes = Invoke-QuickDlssPairSuite `
            -NativeName "forward_special_native_deferred_hdr" `
            -DlssName "forward_special_dlss_present" `
            -LaneKey "forwardSpecial" `
            -NativeEnvironment $forwardSpecialNativeEnvironment `
            -DlssEnvironment $forwardSpecialDlssPresentEnvironment `
            -Manifest $forwardSpecialBaselineManifest
        $quickSummary["lanes"]["forwardSpecialNative"] =
            $forwardSpecialLanes.NativeLane
        $quickSummary["lanes"]["forwardSpecialDlssPresent"] =
            $forwardSpecialLanes.DlssLane
    }

    if ($selectedSuites -contains "material-stress") {
        $quickSummary["baselines"]["materialStress"] = [ordered]@{
            manifest = $materialStressBaselineManifestPath
            name = $materialStressBaselineManifest.name
        }
        $quickSummary["thresholds"]["materialStress"] =
            New-QuickComparisonThresholdSummary -Manifest $materialStressBaselineManifest
        $materialStressLanes = Invoke-QuickDlssPairSuite `
            -NativeName "material_stress_native_deferred_hdr" `
            -DlssName "material_stress_dlss_present" `
            -LaneKey "materialStress" `
            -NativeEnvironment $materialStressNativeEnvironment `
            -DlssEnvironment $materialStressDlssPresentEnvironment `
            -Manifest $materialStressBaselineManifest
        $quickSummary["lanes"]["materialStressNative"] =
            $materialStressLanes.NativeLane
        $quickSummary["lanes"]["materialStressDlssPresent"] =
            $materialStressLanes.DlssLane
    }

    if ($selectedSuites -contains "mask-policy") {
        $quickSummary["baselines"]["maskPolicy"] = [ordered]@{
            manifest = $maskPolicyBaselineManifestPath
            name = $maskPolicyBaselineManifest.name
        }
        $quickSummary["thresholds"]["maskPolicy"] =
            New-QuickComparisonThresholdSummary -Manifest $maskPolicyBaselineManifest
        $maskPolicyLanes = Invoke-QuickDlssPairSuite `
            -NativeName "mask_policy_native_deferred_hdr" `
            -DlssName "mask_policy_dlss_present" `
            -LaneKey "maskPolicy" `
            -NativeEnvironment $maskPolicyNativeEnvironment `
            -DlssEnvironment $maskPolicyDlssPresentEnvironment `
            -Manifest $maskPolicyBaselineManifest
        $quickSummary["lanes"]["maskPolicyNative"] =
            $maskPolicyLanes.NativeLane
        $quickSummary["lanes"]["maskPolicyDlssPresent"] =
            $maskPolicyLanes.DlssLane
    }

    $summaryPath = Join-Path $outputRoot "summary.json"
    $quickSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

    Write-Host "Focused DLSS visual QA passed"
    Write-Host "  summary: $summaryPath"
    foreach ($lane in $quickSummary["lanes"].GetEnumerator()) {
        if ($lane.Value.Contains("image")) {
            Write-Host "  $($lane.Key): $($lane.Value["image"])"
        } elseif ($lane.Value.Contains("images")) {
            Write-Host "  $($lane.Key): $($lane.Value["images"] -join ', ')"
        }
        if ($lane.Value.Contains("comparison")) {
            $comparison = $lane.Value["comparison"]
            Write-Host "  $($lane.Key) diff: sampled=$($comparison.SampledPixels) changed=$($comparison.ChangedPixels) mean=$($comparison.MeanDelta) max=$($comparison.MaxDelta)"
        }
        if ($lane.Value.Contains("sequenceComparison")) {
            $sequenceComparison = $lane.Value["sequenceComparison"]
            Write-Host "  $($lane.Key) sequence: pairs=$($sequenceComparison.pairCount) minChanged=$($sequenceComparison.minChangedPixels) maxMean=$($sequenceComparison.maxMeanDelta) edgeMin=$($sequenceComparison.minEdgePixels) edgeChangedMax=$($sequenceComparison.maxChangedEdgePixels) edgeChangedRatioMax=$($sequenceComparison.maxChangedEdgeRatio) edgeMeanMax=$($sequenceComparison.maxMeanEdgeDelta)"
        }
    }
    return
}

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
$defaultSceneDlaaMotionBenchmark = Invoke-BenchmarkRun `
    -Name "default_scene_dlaa_motion_present" `
    -Environment $defaultSceneDlaaMotionPresentEnvironment `
    -UseApplicationScene
$defaultSceneDlaaObjectMotionBenchmark = Invoke-BenchmarkRun `
    -Name "default_scene_dlaa_object_motion_present" `
    -Environment $defaultSceneDlaaObjectMotionPresentEnvironment `
    -UseApplicationScene
$importedDynamicDlaaObjectMotionBenchmark = Invoke-BenchmarkRun `
    -Name "imported_dynamic_dlaa_object_motion_present" `
    -Environment $importedDynamicDlaaObjectMotionPresentEnvironment `
    -UseApplicationScene
$wboitNativeBenchmark = Invoke-BenchmarkRun -Name "wboit_native_deferred_hdr" -Environment $wboitNativeEnvironment
$wboitDlssBenchmark = Invoke-BenchmarkRun -Name "wboit_dlss_present" -Environment $wboitDlssPresentEnvironment
$forwardSpecialNativeBenchmark = Invoke-BenchmarkRun -Name "forward_special_native_deferred_hdr" -Environment $forwardSpecialNativeEnvironment
$forwardSpecialDlssBenchmark = Invoke-BenchmarkRun -Name "forward_special_dlss_present" -Environment $forwardSpecialDlssPresentEnvironment
$materialStressNativeBenchmark = Invoke-BenchmarkRun -Name "material_stress_native_deferred_hdr" -Environment $materialStressNativeEnvironment
$materialStressDlssBenchmark = Invoke-BenchmarkRun -Name "material_stress_dlss_present" -Environment $materialStressDlssPresentEnvironment
$maskPolicyNativeBenchmark = Invoke-BenchmarkRun -Name "mask_policy_native_deferred_hdr" -Environment $maskPolicyNativeEnvironment
$maskPolicyDlssBenchmark = Invoke-BenchmarkRun -Name "mask_policy_dlss_present" -Environment $maskPolicyDlssPresentEnvironment

$nativeRow = $nativeBenchmark.LastRow
$dlssRow = $dlssBenchmark.LastRow
$dlaaNativeRow = $dlaaNativeBenchmark.LastRow
$dlaaRow = $dlaaBenchmark.LastRow
$defaultSceneDlaaNativeRow = $defaultSceneDlaaNativeBenchmark.LastRow
$defaultSceneDlaaRow = $defaultSceneDlaaBenchmark.LastRow
$defaultSceneDlaaMotionRow = $defaultSceneDlaaMotionBenchmark.LastRow
$defaultSceneDlaaObjectMotionRow = $defaultSceneDlaaObjectMotionBenchmark.LastRow
$importedDynamicDlaaObjectMotionRow = $importedDynamicDlaaObjectMotionBenchmark.LastRow
$wboitNativeRow = $wboitNativeBenchmark.LastRow
$wboitDlssRow = $wboitDlssBenchmark.LastRow
$forwardSpecialNativeRow = $forwardSpecialNativeBenchmark.LastRow
$forwardSpecialDlssRow = $forwardSpecialDlssBenchmark.LastRow
$materialStressNativeRow = $materialStressNativeBenchmark.LastRow
$materialStressDlssRow = $materialStressDlssBenchmark.LastRow
$maskPolicyNativeRow = $maskPolicyNativeBenchmark.LastRow
$maskPolicyDlssRow = $maskPolicyDlssBenchmark.LastRow
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
Assert-DlssJitterConsistency -Name "DLSS-present" -Row $dlssRow

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
Assert-DlssJitterConsistency -Name "DLAA-present" -Row $dlaaRow

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
Assert-DlssJitterConsistency -Name "Default-scene DLAA-present" -Row $defaultSceneDlaaRow
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

if ($defaultSceneDlaaMotionRow.framegraph_validation_issues -ne "0") {
    throw "Default-scene moving DLAA frame graph validation issues: $($defaultSceneDlaaMotionRow.framegraph_validation_issues)"
}
if ($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_mode -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.qualityMode -or
    $defaultSceneDlaaMotionRow.temporal_upscaler_dlss_recommended_preset -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.recommendedPreset) {
    throw "Default-scene moving DLAA did not select the expected DLSS quality mode/preset"
}
if ($defaultSceneDlaaMotionRow.temporal_jitter_applied -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.jitterApplied) {
    throw "Default-scene moving DLAA did not apply the expected projection jitter policy"
}
if ($defaultSceneDlaaMotionRow.temporal_history_valid -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.historyValid -or
    $defaultSceneDlaaMotionRow.temporal_history_reset -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.historyReset -or
    $defaultSceneDlaaMotionRow.temporal_history_reset_reason -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.historyResetReason) {
    throw "Default-scene moving DLAA did not preserve the expected temporal history state"
}
if ($defaultSceneDlaaMotionRow.temporal_upscale_input_ready -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.temporalUpscaleInputReady -or
    $defaultSceneDlaaMotionRow.temporal_taa_resolve_enabled -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveEnabled -or
    $defaultSceneDlaaMotionRow.temporal_taa_resolve_suppressed_for_upscaler -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveSuppressedForUpscaler) {
    throw "Default-scene moving DLAA did not keep DLSS input readiness separate from native TAA resolve"
}
if ($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_reset -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.dlssReset -or
    "$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_motion_vector_scale_x)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_motion_vector_scale_y)" -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.dlssMotionVectorScale) {
    throw "Default-scene moving DLAA did not preserve the expected DLSS reset/motion-vector scale"
}
if ($defaultSceneDlaaMotionRow.temporal_render_scale_active -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.renderScaleActive -or
    $defaultSceneDlaaMotionRow.temporal_render_scale_applied -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.renderScaleApplied) {
    throw "Default-scene moving DLAA did not stay on the full-resolution render path"
}
if ($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_render_width -ne $defaultSceneDlaaMotionRow.temporal_upscale_display_width -or
    $defaultSceneDlaaMotionRow.temporal_upscaler_dlss_render_height -ne $defaultSceneDlaaMotionRow.temporal_upscale_display_height -or
    $defaultSceneDlaaMotionRow.temporal_upscaler_dlss_output_width -ne $defaultSceneDlaaMotionRow.temporal_upscale_display_width -or
    $defaultSceneDlaaMotionRow.temporal_upscaler_dlss_output_height -ne $defaultSceneDlaaMotionRow.temporal_upscale_display_height) {
    throw "Default-scene moving DLAA did not evaluate at full display resolution"
}
if ($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_output_ready -ne "1" -or
    $defaultSceneDlaaMotionRow.temporal_upscale_post_source_requested -ne "1" -or
    $defaultSceneDlaaMotionRow.temporal_upscale_post_source_active -ne "1" -or
    $defaultSceneDlaaMotionRow.temporal_upscale_post_source_fallback_reason -ne "0") {
    throw "Default-scene moving DLAA-present run did not produce visible DLSS output"
}
if ($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_gate_ready -ne "1" -or
    $defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_gate_fallback_reason -ne "0") {
    throw "Default-scene moving DLAA quality gate did not pass"
}
if ($defaultSceneDlaaMotionRow.temporal_velocity_camera_motion_ready -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.cameraMotionReady -or
    $defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_camera_motion_ready -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.cameraMotionReady) {
    throw "Default-scene moving DLAA did not report camera-motion readiness"
}
if ($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_object_motion_ready -ne "1" -or
    $defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_reference_baseline_ready -ne "1") {
    throw "Default-scene moving DLAA quality gate did not report object/baseline readiness"
}
Assert-DlssJitterConsistency -Name "Default-scene moving DLAA-present" -Row $defaultSceneDlaaMotionRow
if ($defaultSceneDlaaMotionRow.main_draws -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.mainDraws -or
    $defaultSceneDlaaMotionRow.gbuffer_draws -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.gbufferDraws -or
    $defaultSceneDlaaMotionRow.forward_residual_draws -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.forwardResidualDraws -or
    $defaultSceneDlaaMotionRow.weighted_translucency_draws -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.weightedTranslucencyDraws) {
    throw "Default-scene moving DLAA draw route mismatch"
}

if ($defaultSceneDlaaObjectMotionRow.framegraph_validation_issues -ne "0") {
    throw "Default-scene object-motion DLAA frame graph validation issues: $($defaultSceneDlaaObjectMotionRow.framegraph_validation_issues)"
}
if ($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_mode -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.qualityMode -or
    $defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_recommended_preset -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.recommendedPreset) {
    throw "Default-scene object-motion DLAA did not select the expected DLSS quality mode/preset"
}
if ($defaultSceneDlaaObjectMotionRow.temporal_jitter_applied -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.jitterApplied) {
    throw "Default-scene object-motion DLAA did not apply the expected projection jitter policy"
}
if ($defaultSceneDlaaObjectMotionRow.temporal_history_valid -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.historyValid -or
    $defaultSceneDlaaObjectMotionRow.temporal_history_reset -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.historyReset -or
    $defaultSceneDlaaObjectMotionRow.temporal_history_reset_reason -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.historyResetReason) {
    throw "Default-scene object-motion DLAA did not preserve the expected temporal history state"
}
if ($defaultSceneDlaaObjectMotionRow.temporal_upscale_input_ready -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.temporalUpscaleInputReady -or
    $defaultSceneDlaaObjectMotionRow.temporal_taa_resolve_enabled -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveEnabled -or
    $defaultSceneDlaaObjectMotionRow.temporal_taa_resolve_suppressed_for_upscaler -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveSuppressedForUpscaler) {
    throw "Default-scene object-motion DLAA did not keep DLSS input readiness separate from native TAA resolve"
}
if ($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_reset -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.dlssReset -or
    "$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_motion_vector_scale_x)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_motion_vector_scale_y)" -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.dlssMotionVectorScale) {
    throw "Default-scene object-motion DLAA did not preserve the expected DLSS reset/motion-vector scale"
}
if ($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_output_ready -ne "1" -or
    $defaultSceneDlaaObjectMotionRow.temporal_upscale_post_source_active -ne "1" -or
    $defaultSceneDlaaObjectMotionRow.temporal_upscale_post_source_fallback_reason -ne "0") {
    throw "Default-scene object-motion DLAA-present run did not produce visible DLSS output"
}
if ($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_gate_ready -ne "1" -or
    $defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_gate_fallback_reason -ne "0") {
    throw "Default-scene object-motion DLAA quality gate did not pass"
}
if ($defaultSceneDlaaObjectMotionRow.temporal_velocity_camera_motion_ready -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.cameraMotionReady -or
    $defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_camera_motion_ready -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.cameraMotionReady -or
    $defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_object_motion_ready -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.objectMotionReady) {
    throw "Default-scene object-motion DLAA did not report camera/object-motion readiness"
}
if ($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_reference_baseline_ready -ne "1") {
    throw "Default-scene object-motion DLAA quality gate did not report baseline readiness"
}
Assert-DlssJitterConsistency -Name "Default-scene object-motion DLAA-present" -Row $defaultSceneDlaaObjectMotionRow
if ($defaultSceneDlaaObjectMotionRow.main_draws -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.mainDraws -or
    $defaultSceneDlaaObjectMotionRow.gbuffer_draws -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.gbufferDraws -or
    $defaultSceneDlaaObjectMotionRow.forward_residual_draws -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.forwardResidualDraws -or
    $defaultSceneDlaaObjectMotionRow.weighted_translucency_draws -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.weightedTranslucencyDraws) {
    throw "Default-scene object-motion DLAA draw route mismatch"
}

if ($importedDynamicDlaaObjectMotionRow.framegraph_validation_issues -ne "0") {
    throw "Imported-dynamic object-motion DLAA frame graph validation issues: $($importedDynamicDlaaObjectMotionRow.framegraph_validation_issues)"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_mode -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.qualityMode -or
    $importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_recommended_preset -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.recommendedPreset) {
    throw "Imported-dynamic object-motion DLAA did not select the expected DLSS quality mode/preset"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_jitter_applied -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.jitterApplied) {
    throw "Imported-dynamic object-motion DLAA did not apply the expected projection jitter policy"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_history_valid -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.historyValid -or
    $importedDynamicDlaaObjectMotionRow.temporal_history_reset -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.historyReset -or
    $importedDynamicDlaaObjectMotionRow.temporal_history_reset_reason -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.historyResetReason) {
    throw "Imported-dynamic object-motion DLAA did not preserve the expected temporal history state"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_upscale_input_ready -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.temporalUpscaleInputReady -or
    $importedDynamicDlaaObjectMotionRow.temporal_taa_resolve_enabled -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveEnabled -or
    $importedDynamicDlaaObjectMotionRow.temporal_taa_resolve_suppressed_for_upscaler -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveSuppressedForUpscaler) {
    throw "Imported-dynamic object-motion DLAA did not keep DLSS input readiness separate from native TAA resolve"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_reset -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.dlssReset -or
    "$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_motion_vector_scale_x)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_motion_vector_scale_y)" -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.dlssMotionVectorScale) {
    throw "Imported-dynamic object-motion DLAA did not preserve the expected DLSS reset/motion-vector scale"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_render_scale_active -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.renderScaleActive -or
    $importedDynamicDlaaObjectMotionRow.temporal_render_scale_applied -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.renderScaleApplied) {
    throw "Imported-dynamic object-motion DLAA did not stay on the full-resolution render path"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_render_width -ne $importedDynamicDlaaObjectMotionRow.temporal_upscale_display_width -or
    $importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_render_height -ne $importedDynamicDlaaObjectMotionRow.temporal_upscale_display_height -or
    $importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_output_width -ne $importedDynamicDlaaObjectMotionRow.temporal_upscale_display_width -or
    $importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_output_height -ne $importedDynamicDlaaObjectMotionRow.temporal_upscale_display_height) {
    throw "Imported-dynamic object-motion DLAA did not evaluate at full display resolution"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_output_ready -ne "1" -or
    $importedDynamicDlaaObjectMotionRow.temporal_upscale_post_source_active -ne "1" -or
    $importedDynamicDlaaObjectMotionRow.temporal_upscale_post_source_fallback_reason -ne "0") {
    throw "Imported-dynamic object-motion DLAA-present run did not produce visible DLSS output"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_gate_ready -ne "1" -or
    $importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_gate_fallback_reason -ne "0") {
    throw "Imported-dynamic object-motion DLAA quality gate did not pass"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_velocity_camera_motion_ready -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.cameraMotionReady -or
    $importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_camera_motion_ready -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.cameraMotionReady -or
    $importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_object_motion_ready -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.objectMotionReady) {
    throw "Imported-dynamic object-motion DLAA did not report camera/object-motion readiness"
}
if ($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_reference_baseline_ready -ne "1") {
    throw "Imported-dynamic object-motion DLAA quality gate did not report baseline readiness"
}
Assert-DlssJitterConsistency -Name "Imported-dynamic object-motion DLAA-present" -Row $importedDynamicDlaaObjectMotionRow
if ($importedDynamicDlaaObjectMotionRow.main_draws -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.mainDraws -or
    $importedDynamicDlaaObjectMotionRow.gbuffer_draws -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.gbufferDraws -or
    $importedDynamicDlaaObjectMotionRow.forward_residual_draws -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.forwardResidualDraws -or
    $importedDynamicDlaaObjectMotionRow.weighted_translucency_draws -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.weightedTranslucencyDraws) {
    throw "Imported-dynamic object-motion DLAA draw route mismatch"
}
if ($importedDynamicDlaaObjectMotionRow.frame_material_count -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.frameMaterialCount -or
    $importedDynamicDlaaObjectMotionRow.frame_material_textured_count -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.frameMaterialTexturedCount -or
    $importedDynamicDlaaObjectMotionRow.frame_light_total_count -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.frameLightTotalCount -or
    $importedDynamicDlaaObjectMotionRow.frame_local_light_count -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.frameLocalLightCount) {
    throw "Imported-dynamic object-motion DLAA scene/material counters mismatch"
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
Assert-DlssJitterConsistency -Name "WBOIT DLSS-present" -Row $wboitDlssRow

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
Assert-DlssJitterConsistency -Name "Forward-special DLSS-present" -Row $forwardSpecialDlssRow

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
Assert-DlssJitterConsistency -Name "Material-stress DLSS-present" -Row $materialStressDlssRow
if ($materialStressDlssRow.frame_material_specular_texture_count -ne $materialStressBaselineManifest.expected.dlssPresent.specularTextureMaterials -or
    $materialStressDlssRow.frame_material_uv_transform_count -ne $materialStressBaselineManifest.expected.dlssPresent.uvTransformMaterials -or
    $materialStressDlssRow.frame_material_double_sided_count -ne $materialStressBaselineManifest.expected.dlssPresent.doubleSidedMaterials -or
    $materialStressDlssRow.frame_material_clearcoat_texture_count -ne $materialStressBaselineManifest.expected.dlssPresent.clearcoatTextureMaterials -or
    $materialStressDlssRow.frame_material_clearcoat_roughness_texture_count -ne $materialStressBaselineManifest.expected.dlssPresent.clearcoatRoughnessTextureMaterials -or
    $materialStressDlssRow.frame_material_transmission_texture_count -ne $materialStressBaselineManifest.expected.dlssPresent.transmissionTextureMaterials -or
    $materialStressDlssRow.frame_material_volume_count -ne $materialStressBaselineManifest.expected.dlssPresent.volumeMaterials -or
    $materialStressDlssRow.frame_material_textured_count -ne $materialStressBaselineManifest.expected.dlssPresent.texturedMaterials) {
    throw "Material-stress DLSS material counter mismatch"
}
$maskPolicyNativeMetrics = Assert-QuickNativeRow `
    -Name "maskPolicy.native" `
    -Row $maskPolicyNativeRow `
    -Expected $maskPolicyBaselineManifest.expected.native
$maskPolicyDlssMetrics = Assert-QuickDlssPresentRow `
    -Name "maskPolicy.dlssPresent" `
    -Row $maskPolicyDlssRow `
    -Expected $maskPolicyBaselineManifest.expected.dlssPresent

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
$defaultSceneDlaaMotionImages = Capture-WindowImageSequence `
    -Name "default_scene_dlaa_motion_present" `
    -Environment $defaultSceneDlaaMotionPresentEnvironment `
    -FrameCount 3 `
    -InitialDelaySeconds 4 `
    -IntervalSeconds 2
$defaultSceneDlaaObjectMotionImages = Capture-WindowImageSequence `
    -Name "default_scene_dlaa_object_motion_present" `
    -Environment $defaultSceneDlaaObjectMotionPresentEnvironment `
    -FrameCount 3 `
    -InitialDelaySeconds 4 `
    -IntervalSeconds 2
$importedDynamicDlaaObjectMotionImages = Capture-WindowImageSequence `
    -Name "imported_dynamic_dlaa_object_motion_present" `
    -Environment $importedDynamicDlaaObjectMotionPresentEnvironment `
    -FrameCount 3 `
    -InitialDelaySeconds 4 `
    -IntervalSeconds 2
$wboitNativeImage = Capture-WindowImage -Name "wboit_native_deferred_hdr" -Environment $wboitNativeEnvironment
$wboitDlssImage = Capture-WindowImage -Name "wboit_dlss_present" -Environment $wboitDlssPresentEnvironment
$forwardSpecialNativeImage = Capture-WindowImage -Name "forward_special_native_deferred_hdr" -Environment $forwardSpecialNativeEnvironment
$forwardSpecialDlssImage = Capture-WindowImage -Name "forward_special_dlss_present" -Environment $forwardSpecialDlssPresentEnvironment
$materialStressNativeImage = Capture-WindowImage -Name "material_stress_native_deferred_hdr" -Environment $materialStressNativeEnvironment
$materialStressDlssImage = Capture-WindowImage -Name "material_stress_dlss_present" -Environment $materialStressDlssPresentEnvironment
$maskPolicyNativeImage = Capture-WindowImage -Name "mask_policy_native_deferred_hdr" -Environment $maskPolicyNativeEnvironment
$maskPolicyDlssImage = Capture-WindowImage -Name "mask_policy_dlss_present" -Environment $maskPolicyDlssPresentEnvironment
$nativeImageStats = Get-ImageVariationStats -Path $nativeImage
$dlssImageStats = Get-ImageVariationStats -Path $dlssImage
$dlaaNativeImageStats = Get-ImageVariationStats -Path $dlaaNativeImage
$dlaaImageStats = Get-ImageVariationStats -Path $dlaaImage
$defaultSceneDlaaNativeImageStats = Get-ImageVariationStats -Path $defaultSceneDlaaNativeImage
$defaultSceneDlaaImageStats = Get-ImageVariationStats -Path $defaultSceneDlaaImage
$defaultSceneDlaaMotionImageStats = @()
foreach ($motionImage in $defaultSceneDlaaMotionImages) {
    $defaultSceneDlaaMotionImageStats += Get-ImageVariationStats -Path $motionImage
}
$defaultSceneDlaaObjectMotionImageStats = @()
foreach ($objectMotionImage in $defaultSceneDlaaObjectMotionImages) {
    $defaultSceneDlaaObjectMotionImageStats +=
        Get-ImageVariationStats -Path $objectMotionImage
}
$importedDynamicDlaaObjectMotionImageStats = @()
foreach ($importedDynamicImage in $importedDynamicDlaaObjectMotionImages) {
    $importedDynamicDlaaObjectMotionImageStats +=
        Get-ImageVariationStats -Path $importedDynamicImage
}
$wboitNativeImageStats = Get-ImageVariationStats -Path $wboitNativeImage
$wboitDlssImageStats = Get-ImageVariationStats -Path $wboitDlssImage
$forwardSpecialNativeImageStats = Get-ImageVariationStats -Path $forwardSpecialNativeImage
$forwardSpecialDlssImageStats = Get-ImageVariationStats -Path $forwardSpecialDlssImage
$materialStressNativeImageStats = Get-ImageVariationStats -Path $materialStressNativeImage
$materialStressDlssImageStats = Get-ImageVariationStats -Path $materialStressDlssImage
$maskPolicyNativeImageStats = Get-ImageVariationStats -Path $maskPolicyNativeImage
$maskPolicyDlssImageStats = Get-ImageVariationStats -Path $maskPolicyDlssImage
$comparison = Compare-Images -A $nativeImage -B $dlssImage
$dlaaComparison = Compare-Images -A $dlaaNativeImage -B $dlaaImage
$defaultSceneDlaaComparison =
    Compare-Images -A $defaultSceneDlaaNativeImage -B $defaultSceneDlaaImage
$defaultSceneDlaaMotionSequenceComparison =
    Compare-ImageSequence -Paths $defaultSceneDlaaMotionImages
$defaultSceneDlaaObjectMotionSequenceComparison =
    Compare-ImageSequence -Paths $defaultSceneDlaaObjectMotionImages
$importedDynamicDlaaObjectMotionSequenceComparison =
    Compare-ImageSequence -Paths $importedDynamicDlaaObjectMotionImages
$wboitComparison = Compare-Images -A $wboitNativeImage -B $wboitDlssImage
$forwardSpecialComparison = Compare-Images -A $forwardSpecialNativeImage -B $forwardSpecialDlssImage
$materialStressComparison = Compare-Images -A $materialStressNativeImage -B $materialStressDlssImage
$maskPolicyComparison = Compare-Images -A $maskPolicyNativeImage -B $maskPolicyDlssImage

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
$defaultSceneDlaaMotionPostSource =
    "$($defaultSceneDlaaMotionRow.temporal_upscale_post_source_requested)/$($defaultSceneDlaaMotionRow.temporal_upscale_post_source_active)/$($defaultSceneDlaaMotionRow.temporal_upscale_post_source_fallback_reason)"
$defaultSceneDlaaMotionQualityGate =
    "$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_gate_requested)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_gate_ready)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$defaultSceneDlaaMotionQualityMasks =
    "$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_required_mask)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_ready_mask)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_blocker_mask)"
$defaultSceneDlaaMotionQualityInputs =
    "output/camera/object/reactive/transparency/exposure/post/baseline=$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_evaluate_output_ready)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_camera_motion_ready)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_object_motion_ready)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_reactive_mask_ready)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_transparency_mask_ready)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_exposure_policy_ready)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_post_ordering_ready)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_reference_baseline_ready)"
$defaultSceneDlaaMotionRenderScale =
    "$($defaultSceneDlaaMotionRow.temporal_render_scale_requested)/$($defaultSceneDlaaMotionRow.temporal_render_scale_active)/$($defaultSceneDlaaMotionRow.temporal_render_scale_applied)"
$defaultSceneDlaaMotionDlssExtents =
    "$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_render_width)x$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_render_height)->$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_output_width)x$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_output_height)"
$defaultSceneDlaaMotionDrawRoute =
    "$($defaultSceneDlaaMotionRow.main_draws)/$($defaultSceneDlaaMotionRow.gbuffer_draws)/$($defaultSceneDlaaMotionRow.forward_residual_draws)/$($defaultSceneDlaaMotionRow.weighted_translucency_draws)"
$defaultSceneDlaaMotionCameraMotion =
    "$($defaultSceneDlaaMotionRow.temporal_velocity_camera_motion_ready)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_camera_motion_ready)"
$defaultSceneDlaaMotionTaaResolve =
    "input/enabled/suppressed=$($defaultSceneDlaaMotionRow.temporal_upscale_input_ready)/$($defaultSceneDlaaMotionRow.temporal_taa_resolve_enabled)/$($defaultSceneDlaaMotionRow.temporal_taa_resolve_suppressed_for_upscaler)"
$defaultSceneDlaaMotionHistory =
    "valid/reset/reason=$($defaultSceneDlaaMotionRow.temporal_history_valid)/$($defaultSceneDlaaMotionRow.temporal_history_reset)/$($defaultSceneDlaaMotionRow.temporal_history_reset_reason)"
$defaultSceneDlaaMotionDlssReset =
    "$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_reset)"
$defaultSceneDlaaMotionDlssMotionVectorScale =
    "$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_motion_vector_scale_x)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_motion_vector_scale_y)"
$defaultSceneDlaaObjectMotionPostSource =
    "$($defaultSceneDlaaObjectMotionRow.temporal_upscale_post_source_requested)/$($defaultSceneDlaaObjectMotionRow.temporal_upscale_post_source_active)/$($defaultSceneDlaaObjectMotionRow.temporal_upscale_post_source_fallback_reason)"
$defaultSceneDlaaObjectMotionQualityGate =
    "$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_gate_requested)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_gate_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$defaultSceneDlaaObjectMotionQualityMasks =
    "$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_required_mask)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_ready_mask)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_blocker_mask)"
$defaultSceneDlaaObjectMotionQualityInputs =
    "output/camera/object/reactive/transparency/exposure/post/baseline=$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_evaluate_output_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_camera_motion_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_object_motion_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_reactive_mask_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_transparency_mask_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_exposure_policy_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_post_ordering_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_reference_baseline_ready)"
$defaultSceneDlaaObjectMotionRenderScale =
    "$($defaultSceneDlaaObjectMotionRow.temporal_render_scale_requested)/$($defaultSceneDlaaObjectMotionRow.temporal_render_scale_active)/$($defaultSceneDlaaObjectMotionRow.temporal_render_scale_applied)"
$defaultSceneDlaaObjectMotionDlssExtents =
    "$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_render_width)x$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_render_height)->$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_output_width)x$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_output_height)"
$defaultSceneDlaaObjectMotionDrawRoute =
    "$($defaultSceneDlaaObjectMotionRow.main_draws)/$($defaultSceneDlaaObjectMotionRow.gbuffer_draws)/$($defaultSceneDlaaObjectMotionRow.forward_residual_draws)/$($defaultSceneDlaaObjectMotionRow.weighted_translucency_draws)"
$defaultSceneDlaaObjectMotionCameraMotion =
    "$($defaultSceneDlaaObjectMotionRow.temporal_velocity_camera_motion_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_camera_motion_ready)"
$defaultSceneDlaaObjectMotionObjectMotion =
    "$($defaultSceneDlaaObjectMotionRow.temporal_velocity_object_motion_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_object_motion_ready)"
$defaultSceneDlaaObjectMotionTaaResolve =
    "input/enabled/suppressed=$($defaultSceneDlaaObjectMotionRow.temporal_upscale_input_ready)/$($defaultSceneDlaaObjectMotionRow.temporal_taa_resolve_enabled)/$($defaultSceneDlaaObjectMotionRow.temporal_taa_resolve_suppressed_for_upscaler)"
$defaultSceneDlaaObjectMotionHistory =
    "valid/reset/reason=$($defaultSceneDlaaObjectMotionRow.temporal_history_valid)/$($defaultSceneDlaaObjectMotionRow.temporal_history_reset)/$($defaultSceneDlaaObjectMotionRow.temporal_history_reset_reason)"
$defaultSceneDlaaObjectMotionDlssReset =
    "$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_reset)"
$defaultSceneDlaaObjectMotionDlssMotionVectorScale =
    "$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_motion_vector_scale_x)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_motion_vector_scale_y)"
$importedDynamicDlaaObjectMotionPostSource =
    "$($importedDynamicDlaaObjectMotionRow.temporal_upscale_post_source_requested)/$($importedDynamicDlaaObjectMotionRow.temporal_upscale_post_source_active)/$($importedDynamicDlaaObjectMotionRow.temporal_upscale_post_source_fallback_reason)"
$importedDynamicDlaaObjectMotionQualityGate =
    "$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_gate_requested)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_gate_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_gate_fallback_reason)"
$importedDynamicDlaaObjectMotionQualityMasks =
    "$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_required_mask)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_ready_mask)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_blocker_mask)"
$importedDynamicDlaaObjectMotionQualityInputs =
    "output/camera/object/reactive/transparency/exposure/post/baseline=$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_evaluate_output_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_camera_motion_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_object_motion_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_reactive_mask_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_transparency_mask_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_exposure_policy_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_post_ordering_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_reference_baseline_ready)"
$importedDynamicDlaaObjectMotionRenderScale =
    "$($importedDynamicDlaaObjectMotionRow.temporal_render_scale_requested)/$($importedDynamicDlaaObjectMotionRow.temporal_render_scale_active)/$($importedDynamicDlaaObjectMotionRow.temporal_render_scale_applied)"
$importedDynamicDlaaObjectMotionDlssExtents =
    "$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_render_width)x$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_render_height)->$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_output_width)x$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_output_height)"
$importedDynamicDlaaObjectMotionDrawRoute =
    "$($importedDynamicDlaaObjectMotionRow.main_draws)/$($importedDynamicDlaaObjectMotionRow.gbuffer_draws)/$($importedDynamicDlaaObjectMotionRow.forward_residual_draws)/$($importedDynamicDlaaObjectMotionRow.weighted_translucency_draws)"
$importedDynamicDlaaObjectMotionCameraMotion =
    "$($importedDynamicDlaaObjectMotionRow.temporal_velocity_camera_motion_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_camera_motion_ready)"
$importedDynamicDlaaObjectMotionObjectMotion =
    "$($importedDynamicDlaaObjectMotionRow.temporal_velocity_object_motion_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_object_motion_ready)"
$importedDynamicDlaaObjectMotionTaaResolve =
    "input/enabled/suppressed=$($importedDynamicDlaaObjectMotionRow.temporal_upscale_input_ready)/$($importedDynamicDlaaObjectMotionRow.temporal_taa_resolve_enabled)/$($importedDynamicDlaaObjectMotionRow.temporal_taa_resolve_suppressed_for_upscaler)"
$importedDynamicDlaaObjectMotionHistory =
    "valid/reset/reason=$($importedDynamicDlaaObjectMotionRow.temporal_history_valid)/$($importedDynamicDlaaObjectMotionRow.temporal_history_reset)/$($importedDynamicDlaaObjectMotionRow.temporal_history_reset_reason)"
$importedDynamicDlaaObjectMotionDlssReset =
    "$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_reset)"
$importedDynamicDlaaObjectMotionDlssMotionVectorScale =
    "$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_motion_vector_scale_x)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_motion_vector_scale_y)"
$importedDynamicDlaaObjectMotionMaterialCounters =
    "materials=$($importedDynamicDlaaObjectMotionRow.frame_material_count),textured=$($importedDynamicDlaaObjectMotionRow.frame_material_textured_count),lights=$($importedDynamicDlaaObjectMotionRow.frame_light_total_count),local=$($importedDynamicDlaaObjectMotionRow.frame_local_light_count),rect=$($importedDynamicDlaaObjectMotionRow.frame_rect_light_count)"
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
Assert-BaselineText -Name "defaultSceneDlaaMotion.dlssPresent.postSource" -Actual $defaultSceneDlaaMotionPostSource -Expected $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.postSource
Assert-BaselineText -Name "defaultSceneDlaaMotion.dlssPresent.qualityGate" -Actual $defaultSceneDlaaMotionQualityGate -Expected $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.qualityGate
Assert-BaselineText -Name "defaultSceneDlaaMotion.dlssPresent.qualityMasks" -Actual $defaultSceneDlaaMotionQualityMasks -Expected $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.qualityMasks
Assert-BaselineText -Name "defaultSceneDlaaMotion.dlssPresent.qualityInputs" -Actual $defaultSceneDlaaMotionQualityInputs -Expected $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.qualityInputs
Assert-BaselineText -Name "defaultSceneDlaaMotion.dlssPresent.renderScale" -Actual $defaultSceneDlaaMotionRenderScale -Expected $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.renderScale
Assert-BaselineText -Name "defaultSceneDlaaMotion.dlssPresent.qualityMode" -Actual $defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_mode -Expected $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.qualityMode
Assert-BaselineText -Name "defaultSceneDlaaMotion.dlssPresent.recommendedPreset" -Actual $defaultSceneDlaaMotionRow.temporal_upscaler_dlss_recommended_preset -Expected $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.recommendedPreset
Assert-BaselineText -Name "defaultSceneDlaaMotion.dlssPresent.cameraMotion" -Actual $defaultSceneDlaaMotionCameraMotion -Expected "$($defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.cameraMotionReady)/$($defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.cameraMotionReady)"
Assert-BaselineText -Name "defaultSceneDlaaMotion.dlssPresent.taaResolve" -Actual $defaultSceneDlaaMotionTaaResolve -Expected "input/enabled/suppressed=$($defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.temporalUpscaleInputReady)/$($defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveEnabled)/$($defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveSuppressedForUpscaler)"
Assert-BaselineText -Name "defaultSceneDlaaObjectMotion.dlssPresent.postSource" -Actual $defaultSceneDlaaObjectMotionPostSource -Expected $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.postSource
Assert-BaselineText -Name "defaultSceneDlaaObjectMotion.dlssPresent.qualityGate" -Actual $defaultSceneDlaaObjectMotionQualityGate -Expected $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.qualityGate
Assert-BaselineText -Name "defaultSceneDlaaObjectMotion.dlssPresent.qualityMasks" -Actual $defaultSceneDlaaObjectMotionQualityMasks -Expected $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.qualityMasks
Assert-BaselineText -Name "defaultSceneDlaaObjectMotion.dlssPresent.qualityInputs" -Actual $defaultSceneDlaaObjectMotionQualityInputs -Expected $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.qualityInputs
Assert-BaselineText -Name "defaultSceneDlaaObjectMotion.dlssPresent.renderScale" -Actual $defaultSceneDlaaObjectMotionRenderScale -Expected $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.renderScale
Assert-BaselineText -Name "defaultSceneDlaaObjectMotion.dlssPresent.qualityMode" -Actual $defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_mode -Expected $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.qualityMode
Assert-BaselineText -Name "defaultSceneDlaaObjectMotion.dlssPresent.recommendedPreset" -Actual $defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_recommended_preset -Expected $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.recommendedPreset
Assert-BaselineText -Name "defaultSceneDlaaObjectMotion.dlssPresent.cameraMotion" -Actual $defaultSceneDlaaObjectMotionCameraMotion -Expected "$($defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.cameraMotionReady)/$($defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.cameraMotionReady)"
Assert-BaselineText -Name "defaultSceneDlaaObjectMotion.dlssPresent.objectMotion" -Actual $defaultSceneDlaaObjectMotionObjectMotion -Expected "$($defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.objectMotionReady)/$($defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.objectMotionReady)"
Assert-BaselineText -Name "defaultSceneDlaaObjectMotion.dlssPresent.taaResolve" -Actual $defaultSceneDlaaObjectMotionTaaResolve -Expected "input/enabled/suppressed=$($defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.temporalUpscaleInputReady)/$($defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveEnabled)/$($defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveSuppressedForUpscaler)"
Assert-BaselineText -Name "importedDynamicDlaaObjectMotion.dlssPresent.postSource" -Actual $importedDynamicDlaaObjectMotionPostSource -Expected $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.postSource
Assert-BaselineText -Name "importedDynamicDlaaObjectMotion.dlssPresent.qualityGate" -Actual $importedDynamicDlaaObjectMotionQualityGate -Expected $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.qualityGate
Assert-BaselineText -Name "importedDynamicDlaaObjectMotion.dlssPresent.qualityMasks" -Actual $importedDynamicDlaaObjectMotionQualityMasks -Expected $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.qualityMasks
Assert-BaselineText -Name "importedDynamicDlaaObjectMotion.dlssPresent.qualityInputs" -Actual $importedDynamicDlaaObjectMotionQualityInputs -Expected $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.qualityInputs
Assert-BaselineText -Name "importedDynamicDlaaObjectMotion.dlssPresent.renderScale" -Actual $importedDynamicDlaaObjectMotionRenderScale -Expected $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.renderScale
Assert-BaselineText -Name "importedDynamicDlaaObjectMotion.dlssPresent.qualityMode" -Actual $importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_mode -Expected $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.qualityMode
Assert-BaselineText -Name "importedDynamicDlaaObjectMotion.dlssPresent.recommendedPreset" -Actual $importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_recommended_preset -Expected $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.recommendedPreset
Assert-BaselineText -Name "importedDynamicDlaaObjectMotion.dlssPresent.cameraMotion" -Actual $importedDynamicDlaaObjectMotionCameraMotion -Expected "$($importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.cameraMotionReady)/$($importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.cameraMotionReady)"
Assert-BaselineText -Name "importedDynamicDlaaObjectMotion.dlssPresent.objectMotion" -Actual $importedDynamicDlaaObjectMotionObjectMotion -Expected "$($importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.objectMotionReady)/$($importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.objectMotionReady)"
Assert-BaselineText -Name "importedDynamicDlaaObjectMotion.dlssPresent.taaResolve" -Actual $importedDynamicDlaaObjectMotionTaaResolve -Expected "input/enabled/suppressed=$($importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.temporalUpscaleInputReady)/$($importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveEnabled)/$($importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.nativeTaaResolveSuppressedForUpscaler)"
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
for ($index = 0; $index -lt $defaultSceneDlaaMotionImageStats.Count; ++$index) {
    Assert-BaselineRange `
        -Name "defaultSceneDlaaMotion.imageStats[$index].differentPixels" `
        -Actual $defaultSceneDlaaMotionImageStats[$index].DifferentPixels `
        -Min $defaultSceneDlaaMotionBaselineManifest.thresholds.centralDifferentPixelsMin `
        -Max $defaultSceneDlaaMotionImageStats[$index].SampledPixels
}
Assert-BaselineRange `
    -Name "defaultSceneDlaaMotion.sequence.minChangedPixels" `
    -Actual $defaultSceneDlaaMotionSequenceComparison.minChangedPixels `
    -Min $defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairChangedPixelsMin `
    -Max $defaultSceneDlaaMotionSequenceComparison.pairs[0].SampledPixels
Assert-BaselineRange `
    -Name "defaultSceneDlaaMotion.sequence.maxMeanDelta" `
    -Actual $defaultSceneDlaaMotionSequenceComparison.maxMeanDelta `
    -Min 0 `
    -Max $defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairMeanDeltaMax
Assert-BaselineRange `
    -Name "defaultSceneDlaaMotion.sequence.minEdgePixels" `
    -Actual $defaultSceneDlaaMotionSequenceComparison.minEdgePixels `
    -Min $defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairEdgePixelsMin `
    -Max $defaultSceneDlaaMotionSequenceComparison.pairs[0].SampledPixels
Assert-BaselineRange `
    -Name "defaultSceneDlaaMotion.sequence.maxChangedEdgePixels" `
    -Actual $defaultSceneDlaaMotionSequenceComparison.maxChangedEdgePixels `
    -Min 0 `
    -Max $defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairChangedEdgePixelsMax
Assert-BaselineRange `
    -Name "defaultSceneDlaaMotion.sequence.maxChangedEdgeRatio" `
    -Actual $defaultSceneDlaaMotionSequenceComparison.maxChangedEdgeRatio `
    -Min 0 `
    -Max $defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairChangedEdgeRatioMax
Assert-BaselineRange `
    -Name "defaultSceneDlaaMotion.sequence.maxMeanEdgeDelta" `
    -Actual $defaultSceneDlaaMotionSequenceComparison.maxMeanEdgeDelta `
    -Min 0 `
    -Max $defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairMeanEdgeDeltaMax
Assert-BaselineRange `
    -Name "defaultSceneDlaaMotion.sequence.maxEdgeDelta" `
    -Actual $defaultSceneDlaaMotionSequenceComparison.maxEdgeDelta `
    -Min 0 `
    -Max $defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairMaxEdgeDeltaMax
for ($index = 0; $index -lt $defaultSceneDlaaObjectMotionImageStats.Count; ++$index) {
    Assert-BaselineRange `
        -Name "defaultSceneDlaaObjectMotion.imageStats[$index].differentPixels" `
        -Actual $defaultSceneDlaaObjectMotionImageStats[$index].DifferentPixels `
        -Min $defaultSceneDlaaObjectMotionBaselineManifest.thresholds.centralDifferentPixelsMin `
        -Max $defaultSceneDlaaObjectMotionImageStats[$index].SampledPixels
}
Assert-BaselineRange `
    -Name "defaultSceneDlaaObjectMotion.sequence.minChangedPixels" `
    -Actual $defaultSceneDlaaObjectMotionSequenceComparison.minChangedPixels `
    -Min $defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedPixelsMin `
    -Max $defaultSceneDlaaObjectMotionSequenceComparison.pairs[0].SampledPixels
Assert-BaselineRange `
    -Name "defaultSceneDlaaObjectMotion.sequence.maxMeanDelta" `
    -Actual $defaultSceneDlaaObjectMotionSequenceComparison.maxMeanDelta `
    -Min 0 `
    -Max $defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairMeanDeltaMax
Assert-BaselineRange `
    -Name "defaultSceneDlaaObjectMotion.sequence.minEdgePixels" `
    -Actual $defaultSceneDlaaObjectMotionSequenceComparison.minEdgePixels `
    -Min $defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairEdgePixelsMin `
    -Max $defaultSceneDlaaObjectMotionSequenceComparison.pairs[0].SampledPixels
Assert-BaselineRange `
    -Name "defaultSceneDlaaObjectMotion.sequence.maxChangedEdgePixels" `
    -Actual $defaultSceneDlaaObjectMotionSequenceComparison.maxChangedEdgePixels `
    -Min 0 `
    -Max $defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedEdgePixelsMax
Assert-BaselineRange `
    -Name "defaultSceneDlaaObjectMotion.sequence.maxChangedEdgeRatio" `
    -Actual $defaultSceneDlaaObjectMotionSequenceComparison.maxChangedEdgeRatio `
    -Min 0 `
    -Max $defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedEdgeRatioMax
Assert-BaselineRange `
    -Name "defaultSceneDlaaObjectMotion.sequence.maxMeanEdgeDelta" `
    -Actual $defaultSceneDlaaObjectMotionSequenceComparison.maxMeanEdgeDelta `
    -Min 0 `
    -Max $defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairMeanEdgeDeltaMax
Assert-BaselineRange `
    -Name "defaultSceneDlaaObjectMotion.sequence.maxEdgeDelta" `
    -Actual $defaultSceneDlaaObjectMotionSequenceComparison.maxEdgeDelta `
    -Min 0 `
    -Max $defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairMaxEdgeDeltaMax
for ($index = 0; $index -lt $importedDynamicDlaaObjectMotionImageStats.Count; ++$index) {
    Assert-BaselineRange `
        -Name "importedDynamicDlaaObjectMotion.imageStats[$index].differentPixels" `
        -Actual $importedDynamicDlaaObjectMotionImageStats[$index].DifferentPixels `
        -Min $importedDynamicDlaaObjectMotionBaselineManifest.thresholds.centralDifferentPixelsMin `
        -Max $importedDynamicDlaaObjectMotionImageStats[$index].SampledPixels
}
Assert-BaselineRange `
    -Name "importedDynamicDlaaObjectMotion.sequence.minChangedPixels" `
    -Actual $importedDynamicDlaaObjectMotionSequenceComparison.minChangedPixels `
    -Min $importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedPixelsMin `
    -Max $importedDynamicDlaaObjectMotionSequenceComparison.pairs[0].SampledPixels
Assert-BaselineRange `
    -Name "importedDynamicDlaaObjectMotion.sequence.maxMeanDelta" `
    -Actual $importedDynamicDlaaObjectMotionSequenceComparison.maxMeanDelta `
    -Min 0 `
    -Max $importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairMeanDeltaMax
Assert-BaselineRange `
    -Name "importedDynamicDlaaObjectMotion.sequence.minEdgePixels" `
    -Actual $importedDynamicDlaaObjectMotionSequenceComparison.minEdgePixels `
    -Min $importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairEdgePixelsMin `
    -Max $importedDynamicDlaaObjectMotionSequenceComparison.pairs[0].SampledPixels
Assert-BaselineRange `
    -Name "importedDynamicDlaaObjectMotion.sequence.maxChangedEdgePixels" `
    -Actual $importedDynamicDlaaObjectMotionSequenceComparison.maxChangedEdgePixels `
    -Min 0 `
    -Max $importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedEdgePixelsMax
Assert-BaselineRange `
    -Name "importedDynamicDlaaObjectMotion.sequence.maxChangedEdgeRatio" `
    -Actual $importedDynamicDlaaObjectMotionSequenceComparison.maxChangedEdgeRatio `
    -Min 0 `
    -Max $importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedEdgeRatioMax
Assert-BaselineRange `
    -Name "importedDynamicDlaaObjectMotion.sequence.maxMeanEdgeDelta" `
    -Actual $importedDynamicDlaaObjectMotionSequenceComparison.maxMeanEdgeDelta `
    -Min 0 `
    -Max $importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairMeanEdgeDeltaMax
Assert-BaselineRange `
    -Name "importedDynamicDlaaObjectMotion.sequence.maxEdgeDelta" `
    -Actual $importedDynamicDlaaObjectMotionSequenceComparison.maxEdgeDelta `
    -Min 0 `
    -Max $importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairMaxEdgeDeltaMax
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
Assert-QuickImageStats `
    -Name "maskPolicy.native" `
    -Stats $maskPolicyNativeImageStats `
    -Manifest $maskPolicyBaselineManifest
Assert-QuickImageStats `
    -Name "maskPolicy.dlssPresent" `
    -Stats $maskPolicyDlssImageStats `
    -Manifest $maskPolicyBaselineManifest
Assert-QuickComparison `
    -Name "maskPolicy" `
    -Comparison $maskPolicyComparison `
    -Manifest $maskPolicyBaselineManifest

$summary = [pscustomobject]@{
    target = $Target
    generatedAt = (Get-Date).ToString("o")
    captureMonitor = $script:captureMonitorWorkArea
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
        maskPolicyManifest = $maskPolicyBaselineManifestPath
        maskPolicyName = $maskPolicyBaselineManifest.name
        dlaaManifest = $dlaaBaselineManifestPath
        dlaaName = $dlaaBaselineManifest.name
        defaultSceneDlaaManifest = $defaultSceneDlaaBaselineManifestPath
        defaultSceneDlaaName = $defaultSceneDlaaBaselineManifest.name
        defaultSceneDlaaMotionManifest = $defaultSceneDlaaMotionBaselineManifestPath
        defaultSceneDlaaMotionName = $defaultSceneDlaaMotionBaselineManifest.name
        defaultSceneDlaaObjectMotionManifest = $defaultSceneDlaaObjectMotionBaselineManifestPath
        defaultSceneDlaaObjectMotionName = $defaultSceneDlaaObjectMotionBaselineManifest.name
        importedDynamicDlaaObjectMotionManifest = $importedDynamicDlaaObjectMotionBaselineManifestPath
        importedDynamicDlaaObjectMotionName = $importedDynamicDlaaObjectMotionBaselineManifest.name
        importedDynamicModel = $importedDynamicModelPath
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
        maskPolicyComparisonChangedPixelsMin = [int]$maskPolicyBaselineManifest.thresholds.comparisonChangedPixelsMin
        maskPolicyComparisonChangedPixelsMax = [int]$maskPolicyBaselineManifest.thresholds.comparisonChangedPixelsMax
        maskPolicyComparisonMeanDeltaMin = [double]$maskPolicyBaselineManifest.thresholds.comparisonMeanDeltaMin
        maskPolicyComparisonMeanDeltaMax = [double]$maskPolicyBaselineManifest.thresholds.comparisonMeanDeltaMax
        maskPolicyComparisonMaxDeltaMax = [int]$maskPolicyBaselineManifest.thresholds.comparisonMaxDeltaMax
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
        defaultSceneDlaaMotionSequencePairChangedPixelsMin = [int]$defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairChangedPixelsMin
        defaultSceneDlaaMotionSequencePairMeanDeltaMax = [double]$defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairMeanDeltaMax
        defaultSceneDlaaMotionSequencePairEdgePixelsMin = [int]$defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairEdgePixelsMin
        defaultSceneDlaaMotionSequencePairChangedEdgePixelsMax = [int]$defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairChangedEdgePixelsMax
        defaultSceneDlaaMotionSequencePairChangedEdgeRatioMax = [double]$defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairChangedEdgeRatioMax
        defaultSceneDlaaMotionSequencePairMeanEdgeDeltaMax = [double]$defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairMeanEdgeDeltaMax
        defaultSceneDlaaMotionSequencePairMaxEdgeDeltaMax = [double]$defaultSceneDlaaMotionBaselineManifest.thresholds.sequencePairMaxEdgeDeltaMax
        defaultSceneDlaaObjectMotionSequencePairChangedPixelsMin = [int]$defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedPixelsMin
        defaultSceneDlaaObjectMotionSequencePairMeanDeltaMax = [double]$defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairMeanDeltaMax
        defaultSceneDlaaObjectMotionSequencePairEdgePixelsMin = [int]$defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairEdgePixelsMin
        defaultSceneDlaaObjectMotionSequencePairChangedEdgePixelsMax = [int]$defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedEdgePixelsMax
        defaultSceneDlaaObjectMotionSequencePairChangedEdgeRatioMax = [double]$defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedEdgeRatioMax
        defaultSceneDlaaObjectMotionSequencePairMeanEdgeDeltaMax = [double]$defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairMeanEdgeDeltaMax
        defaultSceneDlaaObjectMotionSequencePairMaxEdgeDeltaMax = [double]$defaultSceneDlaaObjectMotionBaselineManifest.thresholds.sequencePairMaxEdgeDeltaMax
        importedDynamicDlaaObjectMotionSequencePairChangedPixelsMin = [int]$importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedPixelsMin
        importedDynamicDlaaObjectMotionSequencePairMeanDeltaMax = [double]$importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairMeanDeltaMax
        importedDynamicDlaaObjectMotionSequencePairEdgePixelsMin = [int]$importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairEdgePixelsMin
        importedDynamicDlaaObjectMotionSequencePairChangedEdgePixelsMax = [int]$importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedEdgePixelsMax
        importedDynamicDlaaObjectMotionSequencePairChangedEdgeRatioMax = [double]$importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairChangedEdgeRatioMax
        importedDynamicDlaaObjectMotionSequencePairMeanEdgeDeltaMax = [double]$importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairMeanEdgeDeltaMax
        importedDynamicDlaaObjectMotionSequencePairMaxEdgeDeltaMax = [double]$importedDynamicDlaaObjectMotionBaselineManifest.thresholds.sequencePairMaxEdgeDeltaMax
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
    defaultSceneDlaaMotionPresent = [pscustomobject]@{
        csv = $defaultSceneDlaaMotionBenchmark.CsvPath
        images = $defaultSceneDlaaMotionImages
        columns = "$($defaultSceneDlaaMotionBenchmark.HeaderColumns)/$($defaultSceneDlaaMotionBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$defaultSceneDlaaMotionRow.framegraph_validation_issues
        evaluateOutput = "$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_evaluate_result)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_output_ready)"
        postSource = $defaultSceneDlaaMotionPostSource
        qualityGate = $defaultSceneDlaaMotionQualityGate
        qualityMasks = $defaultSceneDlaaMotionQualityMasks
        qualityInputs = $defaultSceneDlaaMotionQualityInputs
        renderScale = $defaultSceneDlaaMotionRenderScale
        qualityMode = [int]$defaultSceneDlaaMotionRow.temporal_upscaler_dlss_quality_mode
        recommendedPreset = [int]$defaultSceneDlaaMotionRow.temporal_upscaler_dlss_recommended_preset
        dlssExtents = $defaultSceneDlaaMotionDlssExtents
        drawRoute = $defaultSceneDlaaMotionDrawRoute
        cameraMotion = $defaultSceneDlaaMotionCameraMotion
        taaResolve = $defaultSceneDlaaMotionTaaResolve
        temporalHistory = $defaultSceneDlaaMotionHistory
        dlssReset = $defaultSceneDlaaMotionDlssReset
        dlssMotionVectorScale = $defaultSceneDlaaMotionDlssMotionVectorScale
        jitter = "$($defaultSceneDlaaMotionRow.temporal_jitter_enabled)/$($defaultSceneDlaaMotionRow.temporal_jitter_applied)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_jitter_offset_x)/$($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_jitter_offset_y)"
        imageStats = $defaultSceneDlaaMotionImageStats
        sequenceComparison = $defaultSceneDlaaMotionSequenceComparison
    }
    defaultSceneDlaaObjectMotionPresent = [pscustomobject]@{
        csv = $defaultSceneDlaaObjectMotionBenchmark.CsvPath
        images = $defaultSceneDlaaObjectMotionImages
        columns = "$($defaultSceneDlaaObjectMotionBenchmark.HeaderColumns)/$($defaultSceneDlaaObjectMotionBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$defaultSceneDlaaObjectMotionRow.framegraph_validation_issues
        evaluateOutput = "$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_evaluate_result)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_output_ready)"
        postSource = $defaultSceneDlaaObjectMotionPostSource
        qualityGate = $defaultSceneDlaaObjectMotionQualityGate
        qualityMasks = $defaultSceneDlaaObjectMotionQualityMasks
        qualityInputs = $defaultSceneDlaaObjectMotionQualityInputs
        renderScale = $defaultSceneDlaaObjectMotionRenderScale
        qualityMode = [int]$defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_quality_mode
        recommendedPreset = [int]$defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_recommended_preset
        dlssExtents = $defaultSceneDlaaObjectMotionDlssExtents
        drawRoute = $defaultSceneDlaaObjectMotionDrawRoute
        cameraMotion = $defaultSceneDlaaObjectMotionCameraMotion
        objectMotion = $defaultSceneDlaaObjectMotionObjectMotion
        taaResolve = $defaultSceneDlaaObjectMotionTaaResolve
        temporalHistory = $defaultSceneDlaaObjectMotionHistory
        dlssReset = $defaultSceneDlaaObjectMotionDlssReset
        dlssMotionVectorScale = $defaultSceneDlaaObjectMotionDlssMotionVectorScale
        jitter = "$($defaultSceneDlaaObjectMotionRow.temporal_jitter_enabled)/$($defaultSceneDlaaObjectMotionRow.temporal_jitter_applied)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_jitter_offset_x)/$($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_jitter_offset_y)"
        imageStats = $defaultSceneDlaaObjectMotionImageStats
        sequenceComparison = $defaultSceneDlaaObjectMotionSequenceComparison
    }
    importedDynamicDlaaObjectMotionPresent = [pscustomobject]@{
        csv = $importedDynamicDlaaObjectMotionBenchmark.CsvPath
        images = $importedDynamicDlaaObjectMotionImages
        columns = "$($importedDynamicDlaaObjectMotionBenchmark.HeaderColumns)/$($importedDynamicDlaaObjectMotionBenchmark.LastColumns)"
        model = $importedDynamicModelPath
        framegraphValidationIssues = [int]$importedDynamicDlaaObjectMotionRow.framegraph_validation_issues
        evaluateOutput = "$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_evaluate_result)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_output_ready)"
        postSource = $importedDynamicDlaaObjectMotionPostSource
        qualityGate = $importedDynamicDlaaObjectMotionQualityGate
        qualityMasks = $importedDynamicDlaaObjectMotionQualityMasks
        qualityInputs = $importedDynamicDlaaObjectMotionQualityInputs
        renderScale = $importedDynamicDlaaObjectMotionRenderScale
        qualityMode = [int]$importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_quality_mode
        recommendedPreset = [int]$importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_recommended_preset
        dlssExtents = $importedDynamicDlaaObjectMotionDlssExtents
        drawRoute = $importedDynamicDlaaObjectMotionDrawRoute
        cameraMotion = $importedDynamicDlaaObjectMotionCameraMotion
        objectMotion = $importedDynamicDlaaObjectMotionObjectMotion
        taaResolve = $importedDynamicDlaaObjectMotionTaaResolve
        temporalHistory = $importedDynamicDlaaObjectMotionHistory
        dlssReset = $importedDynamicDlaaObjectMotionDlssReset
        dlssMotionVectorScale = $importedDynamicDlaaObjectMotionDlssMotionVectorScale
        materialCounters = $importedDynamicDlaaObjectMotionMaterialCounters
        jitter = "$($importedDynamicDlaaObjectMotionRow.temporal_jitter_enabled)/$($importedDynamicDlaaObjectMotionRow.temporal_jitter_applied)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_jitter_offset_x)/$($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_jitter_offset_y)"
        imageStats = $importedDynamicDlaaObjectMotionImageStats
        sequenceComparison = $importedDynamicDlaaObjectMotionSequenceComparison
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
    maskPolicyNative = [pscustomobject]@{
        csv = $maskPolicyNativeBenchmark.CsvPath
        image = $maskPolicyNativeImage
        columns = "$($maskPolicyNativeBenchmark.HeaderColumns)/$($maskPolicyNativeBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$maskPolicyNativeRow.framegraph_validation_issues
        metrics = $maskPolicyNativeMetrics
        imageStats = $maskPolicyNativeImageStats
    }
    maskPolicyDlssPresent = [pscustomobject]@{
        csv = $maskPolicyDlssBenchmark.CsvPath
        image = $maskPolicyDlssImage
        columns = "$($maskPolicyDlssBenchmark.HeaderColumns)/$($maskPolicyDlssBenchmark.LastColumns)"
        framegraphValidationIssues = [int]$maskPolicyDlssRow.framegraph_validation_issues
        metrics = $maskPolicyDlssMetrics
        imageStats = $maskPolicyDlssImageStats
    }
    comparison = $comparison
    dlaaComparison = $dlaaComparison
    defaultSceneDlaaComparison = $defaultSceneDlaaComparison
    wboitComparison = $wboitComparison
    forwardSpecialComparison = $forwardSpecialComparison
    materialStressComparison = $materialStressComparison
    maskPolicyComparison = $maskPolicyComparison
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
Write-Host "  app motion: $($defaultSceneDlaaMotionImages -join ', ')"
Write-Host "  appmotiondiff: pairs=$($defaultSceneDlaaMotionSequenceComparison.pairCount) minChanged=$($defaultSceneDlaaMotionSequenceComparison.minChangedPixels) maxMean=$($defaultSceneDlaaMotionSequenceComparison.maxMeanDelta) max=$($defaultSceneDlaaMotionSequenceComparison.maxDelta) edgeMin=$($defaultSceneDlaaMotionSequenceComparison.minEdgePixels) edgeChangedMax=$($defaultSceneDlaaMotionSequenceComparison.maxChangedEdgePixels) edgeChangedRatioMax=$($defaultSceneDlaaMotionSequenceComparison.maxChangedEdgeRatio) edgeMeanMax=$($defaultSceneDlaaMotionSequenceComparison.maxMeanEdgeDelta) edgeMax=$($defaultSceneDlaaMotionSequenceComparison.maxEdgeDelta)"
Write-Host "  app object motion: $($defaultSceneDlaaObjectMotionImages -join ', ')"
Write-Host "  appobjectmotiondiff: pairs=$($defaultSceneDlaaObjectMotionSequenceComparison.pairCount) minChanged=$($defaultSceneDlaaObjectMotionSequenceComparison.minChangedPixels) maxMean=$($defaultSceneDlaaObjectMotionSequenceComparison.maxMeanDelta) max=$($defaultSceneDlaaObjectMotionSequenceComparison.maxDelta) edgeMin=$($defaultSceneDlaaObjectMotionSequenceComparison.minEdgePixels) edgeChangedMax=$($defaultSceneDlaaObjectMotionSequenceComparison.maxChangedEdgePixels) edgeChangedRatioMax=$($defaultSceneDlaaObjectMotionSequenceComparison.maxChangedEdgeRatio) edgeMeanMax=$($defaultSceneDlaaObjectMotionSequenceComparison.maxMeanEdgeDelta) edgeMax=$($defaultSceneDlaaObjectMotionSequenceComparison.maxEdgeDelta)"
Write-Host "  imported motion: $($importedDynamicDlaaObjectMotionImages -join ', ')"
Write-Host "  importeddiff: pairs=$($importedDynamicDlaaObjectMotionSequenceComparison.pairCount) minChanged=$($importedDynamicDlaaObjectMotionSequenceComparison.minChangedPixels) maxMean=$($importedDynamicDlaaObjectMotionSequenceComparison.maxMeanDelta) max=$($importedDynamicDlaaObjectMotionSequenceComparison.maxDelta) edgeMin=$($importedDynamicDlaaObjectMotionSequenceComparison.minEdgePixels) edgeChangedMax=$($importedDynamicDlaaObjectMotionSequenceComparison.maxChangedEdgePixels) edgeChangedRatioMax=$($importedDynamicDlaaObjectMotionSequenceComparison.maxChangedEdgeRatio) edgeMeanMax=$($importedDynamicDlaaObjectMotionSequenceComparison.maxMeanEdgeDelta) edgeMax=$($importedDynamicDlaaObjectMotionSequenceComparison.maxEdgeDelta)"
Write-Host "  wboit:   $wboitDlssImage"
Write-Host "  wdiff: sampled=$($wboitComparison.SampledPixels) changed=$($wboitComparison.ChangedPixels) mean=$($wboitComparison.MeanDelta) max=$($wboitComparison.MaxDelta)"
Write-Host "  forward: $forwardSpecialDlssImage"
Write-Host "  fdiff: sampled=$($forwardSpecialComparison.SampledPixels) changed=$($forwardSpecialComparison.ChangedPixels) mean=$($forwardSpecialComparison.MeanDelta) max=$($forwardSpecialComparison.MaxDelta)"
Write-Host "  material: $materialStressDlssImage"
Write-Host "  mdiff: sampled=$($materialStressComparison.SampledPixels) changed=$($materialStressComparison.ChangedPixels) mean=$($materialStressComparison.MeanDelta) max=$($materialStressComparison.MaxDelta)"
Write-Host "  mask-policy: $maskPolicyDlssImage"
Write-Host "  maskdiff: sampled=$($maskPolicyComparison.SampledPixels) changed=$($maskPolicyComparison.ChangedPixels) mean=$($maskPolicyComparison.MeanDelta) max=$($maskPolicyComparison.MaxDelta)"
