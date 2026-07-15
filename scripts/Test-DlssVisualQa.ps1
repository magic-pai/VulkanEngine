param(
    [string]$Target = "SelfEngineForward3D",
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$WindowTitle = "SelfEngine Forward 3D",
    [string]$OutputDirectory = "out\reference_captures\dlss_visual_qa",
    [int]$TimeoutSeconds = 45,
    [int]$CaptureDelaySeconds = 8,
    [int]$SequenceFrameCount = 3,
    [int]$SequenceInitialDelaySeconds = 4,
    [int]$SequenceIntervalSeconds = 2,
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
    [string]$DefaultSceneSkinnedFbxMProductionBaselinePath = "docs\reference_baselines\dlss_default_scene_skinned_fbx_m_production_visual_qa_baseline.json",
    [string]$ImportedDynamicDlaaObjectMotionBaselinePath = "docs\reference_baselines\dlss_imported_dynamic_dlaa_object_motion_visual_qa_baseline.json",
    [string]$ImportedArticulatedDlaaObjectMotionBaselinePath = "docs\reference_baselines\dlss_imported_articulated_dlaa_object_motion_visual_qa_baseline.json",
    [string]$ImportedSkinnedDiagnosticBaselinePath = "docs\reference_baselines\dlss_imported_skinned_diagnostic_visual_qa_baseline.json",
    [string]$ImportedSkinnedPreviewBaselinePath = "docs\reference_baselines\dlss_imported_skinned_preview_visual_qa_baseline.json",
    [int]$CaptureMonitorIndex = 1,
    [string]$DlssRuntimeOverridePath = "",
    [string[]]$Suite = @("full"),
    [switch]$ListSuites,
    [switch]$SkipBuild,
    [switch]$UseKnownNgxInternalLayoutIsolation
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$baseSuites =
    @(
        "full",
        "default",
        "default-tuning",
        "default-preset-l",
        "default-preset-k-fullscreen-control",
        "default-preset-k-dynamic-control",
        "default-preset-m",
        "default-preset-m-fullscreen",
        "default-preset-m-dynamic",
        "m-ngx-mask-diagnostics",
        "m-ngx-layout-audit",
        "m-ngx-lifecycle-audit",
        "m-ngx-runtime-flavor-audit",
        "m-ngx-internal-mv-repro",
        "m-ngx-parameter-matrix-audit",
        "m-ngx-validation-isolation-audit",
        "m-ngx-runtime-override-audit",
        "m-vs-k-subjective-pack",
        "m-vs-k-dynamic-pack",
        "m-vs-k-moving-camera-fullscreen-pack",
        "m-vs-k-combined-fullscreen-pack",
        "m-object-shimmer-diagnostics",
        "m-object-shimmer-fullscreen-diagnostics",
        "m-object-shimmer-mv-jittered-ab",
        "m-object-shimmer-mv-jittered-masked-ab",
        "m-object-shimmer-m-only-masked-diagnostics",
        "default-motion",
        "default-object-motion",
        "default-scene-skinned-fbx-m-production",
        "imported-dynamic",
        "imported-articulated",
        "imported-skinned-diagnostic",
        "imported-skinned-preview",
        "skinned-fbx-m-production",
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
        "m-production" = @(
            "default-preset-m-fullscreen",
            "default-preset-m-dynamic",
            "default-scene-skinned-fbx-m-production",
            "skinned-fbx-m-production"
        )
        "k-control" = @(
            "default-preset-k-fullscreen-control",
            "default-preset-k-dynamic-control"
        )
        "m-highres-dynamic" = @(
            "m-vs-k-moving-camera-fullscreen-pack",
            "m-vs-k-combined-fullscreen-pack",
            "m-object-shimmer-fullscreen-diagnostics"
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
        sequenceCapture = [ordered]@{
            frameCount = $SequenceFrameCount
            initialDelaySeconds = $SequenceInitialDelaySeconds
            intervalSeconds = $SequenceIntervalSeconds
        }
        highResolutionDynamicRepeatLedger =
            Join-Path $OutputDirectory "m_highres_dynamic_repeat_ledger.json"
        focusedHighResolutionDynamicRepeatCommand =
            "powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-highres-dynamic -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2 -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1"
        useKnownNgxInternalLayoutIsolation =
            [bool]$UseKnownNgxInternalLayoutIsolation
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
$externalDlssRuntimeOverridePath = $DlssRuntimeOverridePath.Trim()
if ($externalDlssRuntimeOverridePath.Length -eq 0) {
    $externalDlssRuntimeOverridePath =
        [string][Environment]::GetEnvironmentVariable(
            "SE_DLSS_RUNTIME_PATH",
            "Process"
        )
}
if ($externalDlssRuntimeOverridePath.Length -eq 0) {
    $externalDlssRuntimeOverridePath =
        [string][Environment]::GetEnvironmentVariable(
            "SE_NGX_RUNTIME_PATH",
            "Process"
        )
}
if ($externalDlssRuntimeOverridePath.Length -gt 0) {
    $externalDlssRuntimeOverridePath =
        [System.IO.Path]::GetFullPath($externalDlssRuntimeOverridePath)
}
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
$defaultSceneSkinnedFbxMProductionBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($DefaultSceneSkinnedFbxMProductionBaselinePath)) {
    [System.IO.Path]::GetFullPath($DefaultSceneSkinnedFbxMProductionBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $DefaultSceneSkinnedFbxMProductionBaselinePath))
}
if (!(Test-Path -LiteralPath $defaultSceneSkinnedFbxMProductionBaselineManifestPath)) {
    throw "Default-scene skinned FBX M production visual QA baseline manifest not found: $defaultSceneSkinnedFbxMProductionBaselineManifestPath"
}
$defaultSceneSkinnedFbxMProductionBaselineManifest =
    Get-Content -Raw -LiteralPath $defaultSceneSkinnedFbxMProductionBaselineManifestPath | ConvertFrom-Json
if ($defaultSceneSkinnedFbxMProductionBaselineManifest.target -ne $Target) {
    throw "Default-scene skinned FBX M production visual QA baseline target mismatch: expected $Target, manifest has $($defaultSceneSkinnedFbxMProductionBaselineManifest.target)"
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
$importedSkinnedPreviewBaselineManifestPath = if ([System.IO.Path]::IsPathRooted($ImportedSkinnedPreviewBaselinePath)) {
    [System.IO.Path]::GetFullPath($ImportedSkinnedPreviewBaselinePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $ImportedSkinnedPreviewBaselinePath))
}
if (!(Test-Path -LiteralPath $importedSkinnedPreviewBaselineManifestPath)) {
    throw "Imported-skinned preview visual QA baseline manifest not found: $importedSkinnedPreviewBaselineManifestPath"
}
$importedSkinnedPreviewBaselineManifest =
    Get-Content -Raw -LiteralPath $importedSkinnedPreviewBaselineManifestPath | ConvertFrom-Json
if ($importedSkinnedPreviewBaselineManifest.target -ne $Target) {
    throw "Imported-skinned preview visual QA baseline target mismatch: expected $Target, manifest has $($importedSkinnedPreviewBaselineManifest.target)"
}
$skinnedFbxMProductionBaselineManifest =
    $importedSkinnedPreviewBaselineManifest | ConvertTo-Json -Depth 64 | ConvertFrom-Json
$skinnedFbxMProductionBaselineManifest.name =
    "dlss_visual_qa_skinned_fbx_m_production"
$skinnedFbxMProductionBaselineManifest.scene =
    "imported-real-skinned-fbx-m-production-static-camera-object-motion"
$skinnedFbxMProductionBaselineManifest.description =
    "Focused production audit contract for the real imported FBX skeletal animation lane. Unlike imported-skinned-preview, this lane provides a DLSS reference baseline so quality input readiness can pass; production readiness still remains blocked until preset-M validation is clean and dynamic image quality is accepted."
$skinnedFbxMProductionBaselineManifest.expected.dlssPresent.qualityGate =
    "1/1/0"
$skinnedFbxMProductionBaselineManifest.expected.dlssPresent.qualityMasks =
    "255/255/0"
$skinnedFbxMProductionBaselineManifest.expected.dlssPresent.qualityInputs =
    "output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/1"

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
$importedSkinnedPreviewModelPath =
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot "assets\models\Fist Fight B.fbx"))
if (($selectedSuites -contains "imported-skinned-preview") -and
    !(Test-Path -LiteralPath $importedSkinnedPreviewModelPath)) {
    throw "Imported-skinned preview model not found: $importedSkinnedPreviewModelPath"
}
if (($selectedSuites -contains "skinned-fbx-m-production") -and
    !(Test-Path -LiteralPath $importedSkinnedPreviewModelPath)) {
    throw "Skinned FBX M production model not found: $importedSkinnedPreviewModelPath"
}
if (($selectedSuites -contains "default-scene-skinned-fbx-m-production") -and
    !(Test-Path -LiteralPath $importedSkinnedPreviewModelPath)) {
    throw "Default-scene skinned FBX M production model not found: $importedSkinnedPreviewModelPath"
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

if (-not ("SelfEngineVisualQaImageMetrics" -as [type])) {
    Add-Type -ReferencedAssemblies "System.Drawing" -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Linq;
using System.Runtime.InteropServices;

public sealed class SelfEngineVisualQaMaskedComparisonResult {
    public int SampleStep;
    public int SampledPixels;
    public int MaskPixels;
    public int ChangedPixels;
    public int MaxDelta;
    public double SumDelta;
    public int EdgePixels;
    public int ChangedEdgePixels;
    public double MaxEdgeDelta;
    public double SumEdgeDelta;
}

public sealed class SelfEngineVisualQaHotspotTile {
    public int TileX;
    public int TileY;
    public int XStart;
    public int YStart;
    public int XEnd;
    public int YEnd;
    public int SampledPixels;
    public int MaskPixels;
    public int EdgePixels;
    public int ExcessPixels;
    public double ReferenceEdgeDeltaSum;
    public double CandidateEdgeDeltaSum;
    public double PositiveExcessSum;
    public double MaxPositiveExcess;
}

public static class SelfEngineVisualQaImageMetrics {
    private sealed class LockedBitmap : IDisposable {
        public readonly Bitmap Bitmap;
        public readonly BitmapData Data;
        public readonly byte[] Bytes;
        public readonly int Stride;

        public LockedBitmap(string path) {
            using (Bitmap source = (Bitmap)Image.FromFile(path)) {
                Bitmap = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppArgb);
                using (Graphics graphics = Graphics.FromImage(Bitmap)) {
                    graphics.DrawImage(source, 0, 0, source.Width, source.Height);
                }
            }

            Rectangle rect = new Rectangle(0, 0, Bitmap.Width, Bitmap.Height);
            Data = Bitmap.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            Stride = Math.Abs(Data.Stride);
            Bytes = new byte[Stride * Bitmap.Height];
            Marshal.Copy(Data.Scan0, Bytes, 0, Bytes.Length);
        }

        public void Dispose() {
            Bitmap.UnlockBits(Data);
            Bitmap.Dispose();
        }
    }

    public static SelfEngineVisualQaMaskedComparisonResult CompareDebugMaskPair(
        string a,
        string b,
        string maskA,
        string maskB,
        string maskName,
        double gradientThreshold,
        double deltaThreshold,
        int sampleStep
    ) {
        using (LockedBitmap bitmapA = new LockedBitmap(a))
        using (LockedBitmap bitmapB = new LockedBitmap(b))
        using (LockedBitmap maskBitmapA = new LockedBitmap(maskA))
        using (LockedBitmap maskBitmapB = new LockedBitmap(maskB)) {
            int width = Math.Min(Math.Min(bitmapA.Bitmap.Width, bitmapB.Bitmap.Width),
                Math.Min(maskBitmapA.Bitmap.Width, maskBitmapB.Bitmap.Width));
            int height = Math.Min(Math.Min(bitmapA.Bitmap.Height, bitmapB.Bitmap.Height),
                Math.Min(maskBitmapA.Bitmap.Height, maskBitmapB.Bitmap.Height));
            int step = Math.Max(1, sampleStep);
            double maskLumaThreshold = 18.0;
            double maskEdgeThreshold = 24.0;
            if (String.Equals(maskName, "depth", StringComparison.OrdinalIgnoreCase)) {
                maskLumaThreshold = 3.0;
                maskEdgeThreshold = 6.0;
            }

            int backgroundX = Math.Min(width - 1, Math.Max(0, (int)(width * 0.90)));
            int backgroundY = Math.Min(height - 1, Math.Max(0, (int)(height * 0.90)));
            double backgroundLumaA = Luma(maskBitmapA, backgroundX, backgroundY);
            double backgroundLumaB = Luma(maskBitmapB, backgroundX, backgroundY);

            SelfEngineVisualQaMaskedComparisonResult result =
                new SelfEngineVisualQaMaskedComparisonResult();
            result.SampleStep = step;

            int yStart = Math.Max(2, (int)(height * 0.20));
            int yEnd = Math.Min(height - 2, (int)(height * 0.80));
            int xStart = Math.Max(2, (int)(width * 0.20));
            int xEnd = Math.Min(width - 2, (int)(width * 0.80));
            for (int y = yStart; y < yEnd; y += step) {
                for (int x = xStart; x < xEnd; x += step) {
                    ++result.SampledPixels;
                    bool maskActive =
                        IsMaskSampleActive(maskBitmapA, backgroundLumaA, x, y,
                            maskLumaThreshold, maskEdgeThreshold) ||
                        IsMaskSampleActive(maskBitmapB, backgroundLumaB, x, y,
                            maskLumaThreshold, maskEdgeThreshold);
                    if (!maskActive) {
                        continue;
                    }

                    ++result.MaskPixels;
                    int delta =
                        Math.Abs(R(bitmapA, x, y) - R(bitmapB, x, y)) +
                        Math.Abs(G(bitmapA, x, y) - G(bitmapB, x, y)) +
                        Math.Abs(B(bitmapA, x, y) - B(bitmapB, x, y));
                    if (delta > 6) {
                        ++result.ChangedPixels;
                    }
                    if (delta > result.MaxDelta) {
                        result.MaxDelta = delta;
                    }
                    result.SumDelta += delta;

                    double edgeA = EdgeMagnitude(bitmapA, x, y);
                    double edgeB = EdgeMagnitude(bitmapB, x, y);
                    if (Math.Max(edgeA, edgeB) < gradientThreshold) {
                        continue;
                    }

                    double edgeDelta = Math.Abs(Luma(bitmapA, x, y) - Luma(bitmapB, x, y));
                    if (edgeDelta > deltaThreshold) {
                        ++result.ChangedEdgePixels;
                    }
                    if (edgeDelta > result.MaxEdgeDelta) {
                        result.MaxEdgeDelta = edgeDelta;
                    }
                    result.SumEdgeDelta += edgeDelta;
                    ++result.EdgePixels;
                }
            }

            return result;
        }
    }

    public static SelfEngineVisualQaHotspotTile[] CompareDebugMaskInstabilityHotspots(
        string referenceA,
        string referenceB,
        string candidateA,
        string candidateB,
        string maskA,
        string maskB,
        string maskName,
        double gradientThreshold,
        double excessThreshold,
        int sampleStep,
        int tileColumns,
        int tileRows,
        int topCount
    ) {
        using (LockedBitmap refBitmapA = new LockedBitmap(referenceA))
        using (LockedBitmap refBitmapB = new LockedBitmap(referenceB))
        using (LockedBitmap candidateBitmapA = new LockedBitmap(candidateA))
        using (LockedBitmap candidateBitmapB = new LockedBitmap(candidateB))
        using (LockedBitmap maskBitmapA = new LockedBitmap(maskA))
        using (LockedBitmap maskBitmapB = new LockedBitmap(maskB)) {
            int width = Math.Min(Math.Min(refBitmapA.Bitmap.Width, refBitmapB.Bitmap.Width),
                Math.Min(Math.Min(candidateBitmapA.Bitmap.Width, candidateBitmapB.Bitmap.Width),
                    Math.Min(maskBitmapA.Bitmap.Width, maskBitmapB.Bitmap.Width)));
            int height = Math.Min(Math.Min(refBitmapA.Bitmap.Height, refBitmapB.Bitmap.Height),
                Math.Min(Math.Min(candidateBitmapA.Bitmap.Height, candidateBitmapB.Bitmap.Height),
                    Math.Min(maskBitmapA.Bitmap.Height, maskBitmapB.Bitmap.Height)));
            int step = Math.Max(1, sampleStep);
            int columns = Math.Max(1, tileColumns);
            int rows = Math.Max(1, tileRows);

            double maskLumaThreshold = 18.0;
            double maskEdgeThreshold = 24.0;
            if (String.Equals(maskName, "depth", StringComparison.OrdinalIgnoreCase)) {
                maskLumaThreshold = 3.0;
                maskEdgeThreshold = 6.0;
            }

            int backgroundX = Math.Min(width - 1, Math.Max(0, (int)(width * 0.90)));
            int backgroundY = Math.Min(height - 1, Math.Max(0, (int)(height * 0.90)));
            double backgroundLumaA = Luma(maskBitmapA, backgroundX, backgroundY);
            double backgroundLumaB = Luma(maskBitmapB, backgroundX, backgroundY);

            SelfEngineVisualQaHotspotTile[] tiles =
                new SelfEngineVisualQaHotspotTile[columns * rows];
            for (int tileY = 0; tileY < rows; ++tileY) {
                for (int tileX = 0; tileX < columns; ++tileX) {
                    int index = tileY * columns + tileX;
                    tiles[index] = new SelfEngineVisualQaHotspotTile {
                        TileX = tileX,
                        TileY = tileY,
                        XStart = (int)Math.Round(width * (double)tileX / columns),
                        XEnd = (int)Math.Round(width * (double)(tileX + 1) / columns),
                        YStart = (int)Math.Round(height * (double)tileY / rows),
                        YEnd = (int)Math.Round(height * (double)(tileY + 1) / rows)
                    };
                }
            }

            int yStart = Math.Max(2, (int)(height * 0.20));
            int yEnd = Math.Min(height - 2, (int)(height * 0.80));
            int xStart = Math.Max(2, (int)(width * 0.20));
            int xEnd = Math.Min(width - 2, (int)(width * 0.80));
            for (int y = yStart; y < yEnd; y += step) {
                for (int x = xStart; x < xEnd; x += step) {
                    int tileX = Math.Min(columns - 1, Math.Max(0, (int)((long)x * columns / width)));
                    int tileY = Math.Min(rows - 1, Math.Max(0, (int)((long)y * rows / height)));
                    SelfEngineVisualQaHotspotTile tile = tiles[tileY * columns + tileX];
                    ++tile.SampledPixels;

                    bool maskActive =
                        IsMaskSampleActive(maskBitmapA, backgroundLumaA, x, y,
                            maskLumaThreshold, maskEdgeThreshold) ||
                        IsMaskSampleActive(maskBitmapB, backgroundLumaB, x, y,
                            maskLumaThreshold, maskEdgeThreshold);
                    if (!maskActive) {
                        continue;
                    }

                    ++tile.MaskPixels;
                    double referenceEdge =
                        Math.Abs(Luma(refBitmapA, x, y) - Luma(refBitmapB, x, y));
                    double candidateEdge =
                        Math.Abs(Luma(candidateBitmapA, x, y) -
                            Luma(candidateBitmapB, x, y));
                    double referenceGradient = Math.Max(
                        EdgeMagnitude(refBitmapA, x, y),
                        EdgeMagnitude(refBitmapB, x, y));
                    double candidateGradient = Math.Max(
                        EdgeMagnitude(candidateBitmapA, x, y),
                        EdgeMagnitude(candidateBitmapB, x, y));
                    if (Math.Max(referenceGradient, candidateGradient) < gradientThreshold) {
                        continue;
                    }

                    ++tile.EdgePixels;
                    tile.ReferenceEdgeDeltaSum += referenceEdge;
                    tile.CandidateEdgeDeltaSum += candidateEdge;
                    double positiveExcess = candidateEdge - referenceEdge;
                    if (positiveExcess > excessThreshold) {
                        ++tile.ExcessPixels;
                        tile.PositiveExcessSum += positiveExcess;
                        if (positiveExcess > tile.MaxPositiveExcess) {
                            tile.MaxPositiveExcess = positiveExcess;
                        }
                    }
                }
            }

            return tiles
                .Where(tile => tile.ExcessPixels > 0)
                .OrderByDescending(tile => tile.PositiveExcessSum)
                .ThenByDescending(tile => tile.ExcessPixels)
                .Take(Math.Max(1, topCount))
                .ToArray();
        }
    }

    public static SelfEngineVisualQaHotspotTile[] CompareSequenceInstabilityHotspots(
        string referenceA,
        string referenceB,
        string candidateA,
        string candidateB,
        double gradientThreshold,
        double excessThreshold,
        int sampleStep,
        int tileColumns,
        int tileRows,
        int topCount
    ) {
        using (LockedBitmap refBitmapA = new LockedBitmap(referenceA))
        using (LockedBitmap refBitmapB = new LockedBitmap(referenceB))
        using (LockedBitmap candidateBitmapA = new LockedBitmap(candidateA))
        using (LockedBitmap candidateBitmapB = new LockedBitmap(candidateB)) {
            int width = Math.Min(Math.Min(refBitmapA.Bitmap.Width, refBitmapB.Bitmap.Width),
                Math.Min(candidateBitmapA.Bitmap.Width, candidateBitmapB.Bitmap.Width));
            int height = Math.Min(Math.Min(refBitmapA.Bitmap.Height, refBitmapB.Bitmap.Height),
                Math.Min(candidateBitmapA.Bitmap.Height, candidateBitmapB.Bitmap.Height));
            int step = Math.Max(1, sampleStep);
            int columns = Math.Max(1, tileColumns);
            int rows = Math.Max(1, tileRows);

            SelfEngineVisualQaHotspotTile[] tiles =
                new SelfEngineVisualQaHotspotTile[columns * rows];
            for (int tileY = 0; tileY < rows; ++tileY) {
                for (int tileX = 0; tileX < columns; ++tileX) {
                    int index = tileY * columns + tileX;
                    tiles[index] = new SelfEngineVisualQaHotspotTile {
                        TileX = tileX,
                        TileY = tileY,
                        XStart = (int)Math.Round(width * (double)tileX / columns),
                        XEnd = (int)Math.Round(width * (double)(tileX + 1) / columns),
                        YStart = (int)Math.Round(height * (double)tileY / rows),
                        YEnd = (int)Math.Round(height * (double)(tileY + 1) / rows)
                    };
                }
            }

            int yStart = Math.Max(2, (int)(height * 0.20));
            int yEnd = Math.Min(height - 2, (int)(height * 0.80));
            int xStart = Math.Max(2, (int)(width * 0.20));
            int xEnd = Math.Min(width - 2, (int)(width * 0.80));
            for (int y = yStart; y < yEnd; y += step) {
                for (int x = xStart; x < xEnd; x += step) {
                    int tileX = Math.Min(columns - 1, Math.Max(0, (int)((long)x * columns / width)));
                    int tileY = Math.Min(rows - 1, Math.Max(0, (int)((long)y * rows / height)));
                    SelfEngineVisualQaHotspotTile tile = tiles[tileY * columns + tileX];
                    ++tile.SampledPixels;

                    double referenceGradient = Math.Max(
                        EdgeMagnitude(refBitmapA, x, y),
                        EdgeMagnitude(refBitmapB, x, y));
                    double candidateGradient = Math.Max(
                        EdgeMagnitude(candidateBitmapA, x, y),
                        EdgeMagnitude(candidateBitmapB, x, y));
                    if (Math.Max(referenceGradient, candidateGradient) < gradientThreshold) {
                        continue;
                    }

                    ++tile.EdgePixels;
                    double referenceEdge =
                        Math.Abs(Luma(refBitmapA, x, y) - Luma(refBitmapB, x, y));
                    double candidateEdge =
                        Math.Abs(Luma(candidateBitmapA, x, y) -
                            Luma(candidateBitmapB, x, y));
                    tile.ReferenceEdgeDeltaSum += referenceEdge;
                    tile.CandidateEdgeDeltaSum += candidateEdge;
                    double positiveExcess = candidateEdge - referenceEdge;
                    if (positiveExcess > excessThreshold) {
                        ++tile.ExcessPixels;
                        tile.PositiveExcessSum += positiveExcess;
                        if (positiveExcess > tile.MaxPositiveExcess) {
                            tile.MaxPositiveExcess = positiveExcess;
                        }
                    }
                }
            }

            return tiles
                .Where(tile => tile.ExcessPixels > 0)
                .OrderByDescending(tile => tile.PositiveExcessSum)
                .ThenByDescending(tile => tile.ExcessPixels)
                .Take(Math.Max(1, topCount))
                .ToArray();
        }
    }

    private static bool IsMaskSampleActive(
        LockedBitmap bitmap,
        double backgroundLuma,
        int x,
        int y,
        double lumaDeltaThreshold,
        double edgeThreshold
    ) {
        double lumaDelta = Math.Abs(Luma(bitmap, x, y) - backgroundLuma);
        if (lumaDelta > lumaDeltaThreshold) {
            return true;
        }

        if (x >= 2 && x < bitmap.Bitmap.Width - 2 &&
            y >= 2 && y < bitmap.Bitmap.Height - 2) {
            return EdgeMagnitude(bitmap, x, y) > edgeThreshold;
        }

        return false;
    }

    private static double EdgeMagnitude(LockedBitmap bitmap, int x, int y) {
        double left = Luma(bitmap, x - 2, y);
        double right = Luma(bitmap, x + 2, y);
        double up = Luma(bitmap, x, y - 2);
        double down = Luma(bitmap, x, y + 2);
        return Math.Abs(right - left) + Math.Abs(down - up);
    }

    private static double Luma(LockedBitmap bitmap, int x, int y) {
        return 0.2126 * R(bitmap, x, y) +
            0.7152 * G(bitmap, x, y) +
            0.0722 * B(bitmap, x, y);
    }

    private static int PixelOffset(LockedBitmap bitmap, int x, int y) {
        return y * bitmap.Stride + x * 4;
    }

    private static int B(LockedBitmap bitmap, int x, int y) {
        return bitmap.Bytes[PixelOffset(bitmap, x, y)];
    }

    private static int G(LockedBitmap bitmap, int x, int y) {
        return bitmap.Bytes[PixelOffset(bitmap, x, y) + 1];
    }

    private static int R(LockedBitmap bitmap, int x, int y) {
        return bitmap.Bytes[PixelOffset(bitmap, x, y) + 2];
    }
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
    "SE_WINDOW_WIDTH",
    "SE_WINDOW_HEIGHT",
    "SE_FORWARD3D_WINDOW_WIDTH",
    "SE_FORWARD3D_WINDOW_HEIGHT",
    "SE_WINDOW_BORDERLESS",
    "SE_BORDERLESS_FULLSCREEN",
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
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_DEFAULT_SCENE_SKINNED_FBX",
    "SE_DEFAULT_SCENE_BIND_SKINNED_FBX",
    "SE_ENABLE_IMPORTED_SKINNING_PREVIEW",
    "SE_IMPORTED_SKINNING_PREVIEW",
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
    "SE_TEMPORAL_VELOCITY_JITTER_POLICY",
    "SE_TEMPORAL_VELOCITY_JITTERED_HISTORY",
    "SE_VELOCITY_JITTERED_HISTORY",
    "SE_RENDER_VIEW",
    "SE_UPSCALER_PLUGIN",
    "SE_TEMPORAL_UPSCALER_PLUGIN",
    "SE_DLSS_QUALITY",
    "SE_DLSS_MODE",
    "SE_DLSS_PRESET",
    "SE_DLSS_RENDER_PRESET",
    "SE_DLSS_PRESET_OVERRIDE",
    "SE_DLSS_SHARPNESS",
    "SE_DLSS_RUNTIME_FLAVOR",
    "SE_NGX_RUNTIME_FLAVOR",
    "SE_DLSS_RUNTIME_PATH",
    "SE_NGX_RUNTIME_PATH",
    "SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS",
    "SE_DLSS_DISABLE_OPTIONAL_MASK_BINDINGS",
    "SE_DLSS_DISABLE_OPTIONAL_MASK_BINDING",
    "SE_DLSS_DISABLE_BIAS_CURRENT_COLOR_MASK_BINDING",
    "SE_DLSS_DISABLE_BIAS_CURRENT_COLOR_MASK_BIND",
    "SE_DLSS_DISABLE_TRANSPARENCY_MASK_BINDING",
    "SE_DLSS_DISABLE_TRANSPARENCY_MASK_BIND",
    "SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT",
    "SE_TEMPORAL_UPSCALER_SHARPNESS",
    "SE_TEXTURE_MIP_LOD_BIAS",
    "SE_MATERIAL_TEXTURE_MIP_BIAS",
    "SE_TEXTURE_MIP_BIAS",
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
        if ($UseKnownNgxInternalLayoutIsolation) {
            [Environment]::SetEnvironmentVariable(
                "SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT",
                "1",
                "Process"
            )
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
    $bounds = $screens[$selectedIndex].Bounds
    return [pscustomobject]@{
        RequestedIndex = $CaptureMonitorIndex
        Index = $selectedIndex
        Count = $screens.Count
        DeviceName = $screens[$selectedIndex].DeviceName
        Left = $area.Left
        Top = $area.Top
        Width = $area.Width
        Height = $area.Height
        BoundsLeft = $bounds.Left
        BoundsTop = $bounds.Top
        BoundsWidth = $bounds.Width
        BoundsHeight = $bounds.Height
    }
}

function Assert-CleanLog {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string[]]$AllowedDiagnosticPatterns = @()
    )

    if (!(Test-Path -LiteralPath $Path)) {
        return
    }

    $matches = Select-String `
        -LiteralPath $Path `
        -Pattern "VUID|validation|error|failed|exception|shader" `
        -CaseSensitive:$false
    if ($matches) {
        if ($AllowedDiagnosticPatterns.Count -gt 0) {
            $unallowedMatches = @()
            foreach ($match in $matches) {
                $allowed = $false
                foreach ($pattern in $AllowedDiagnosticPatterns) {
                    if ($match.Line -match $pattern) {
                        $allowed = $true
                        break
                    }
                }
                if (-not $allowed) {
                    $unallowedMatches += $match
                }
            }
            if ($unallowedMatches.Count -eq 0) {
                Write-Host "Allowed known diagnostics in $Path"
                return
            }
        }
        throw "Log contains diagnostic matches: $Path"
    }
}

function Test-LogContainsPattern {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern
    )

    if (!(Test-Path -LiteralPath $Path)) {
        return $false
    }

    $matches = Select-String `
        -LiteralPath $Path `
        -Pattern $Pattern `
        -CaseSensitive:$false
    return $null -ne $matches
}

function Convert-TraceKeyValueLine {
    param([Parameter(Mandatory = $true)][string]$Line)

    $values = [ordered]@{}
    $matches = [regex]::Matches($Line, '(?<key>[A-Za-z0-9_]+)=(?<value>[^ ]+)')
    foreach ($match in $matches) {
        $values[$match.Groups["key"].Value] = $match.Groups["value"].Value
    }
    return [pscustomobject]$values
}

function Get-UniqueSortedText {
    param([string[]]$Values)

    return @(
        $Values |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Sort-Object -Unique
    )
}

function Get-DlssTraceLogSummary {
    param([Parameter(Mandatory = $true)]$Lane)

    $stdoutLines = @()
    $stderrLines = @()
    if ($Lane.Contains("captureOutLog") -and
        (Test-Path -LiteralPath $Lane["captureOutLog"])) {
        $stdoutLines = @(Get-Content -LiteralPath $Lane["captureOutLog"])
    }
    if ($Lane.Contains("captureErrLog") -and
        (Test-Path -LiteralPath $Lane["captureErrLog"])) {
        $stderrLines = @(Get-Content -LiteralPath $Lane["captureErrLog"])
    }

    $lifecycleLines =
        @($stdoutLines | Where-Object {
            $_ -like "*SelfEngineDLSSLifecycleTrace*"
        })
    $resourceLines =
        @($stdoutLines | Where-Object {
            $_ -like "*SelfEngineDLSSResourceTrace*"
        })
    $suppressedNgxInternalLayoutLines =
        @(@($stdoutLines + $stderrLines) | Where-Object {
            $_ -like "*SelfEngineVkSuppressedNgxInternalLayout*"
        })
    $lifecycleEvents = @()
    $phaseCounts = [ordered]@{}
    foreach ($line in $lifecycleLines) {
        $event = Convert-TraceKeyValueLine -Line $line
        $lifecycleEvents += $event
        $phase = [string]$event.phase
        if ($phase.Length -gt 0) {
            if (-not $phaseCounts.Contains($phase)) {
                $phaseCounts[$phase] = 0
            }
            $phaseCounts[$phase] = [int]$phaseCounts[$phase] + 1
        }
    }

    $resourceTraceEntries = @()
    $selfEngineImageHandles = @()
    foreach ($line in $resourceLines) {
        $entry = Convert-TraceKeyValueLine -Line $line
        $resourceTraceEntries += $entry
        if ($null -ne $entry.PSObject.Properties["image"]) {
            $selfEngineImageHandles += [string]$entry.image
        }
    }
    $selfEngineImageHandles =
        @(Get-UniqueSortedText -Values $selfEngineImageHandles)

    $warningHandles = @()
    $warningResourceNames = @()
    foreach ($line in @($stdoutLines + $stderrLines)) {
        foreach ($match in [regex]::Matches(
            $line,
            'VkImage\s+(0x[0-9a-fA-F]+)\[(nv\.ngx\.dlss\.[^\]]+)\]'
        )) {
            $warningHandles += $match.Groups[1].Value.ToLowerInvariant()
            $warningResourceNames += $match.Groups[2].Value
        }
    }
    $warningHandles = @(Get-UniqueSortedText -Values $warningHandles)
    $warningResourceNames =
        @(Get-UniqueSortedText -Values $warningResourceNames)

    $suppressedNgxInternalLayoutEvents = @()
    foreach ($line in $suppressedNgxInternalLayoutLines) {
        $suppressedNgxInternalLayoutEvents += Convert-TraceKeyValueLine -Line $line
    }
    $suppressedNgxInternalLayoutResourceNames =
        @(Get-UniqueSortedText -Values @(
            $suppressedNgxInternalLayoutEvents |
                ForEach-Object { [string]$_.resource }
        ))
    $suppressedNgxInternalLayoutHandles =
        @(Get-UniqueSortedText -Values @(
            $suppressedNgxInternalLayoutEvents |
                ForEach-Object { [string]$_.image }
        ))
    $selfHandleSet = @{}
    foreach ($handle in $selfEngineImageHandles) {
        $selfHandleSet[$handle.ToLowerInvariant()] = $true
    }
    $overlapHandles = @()
    foreach ($handle in $warningHandles) {
        if ($selfHandleSet.ContainsKey($handle.ToLowerInvariant())) {
            $overlapHandles += $handle
        }
    }
    $overlapHandles = @(Get-UniqueSortedText -Values $overlapHandles)

    $evaluateResultEvents =
        @($lifecycleEvents | Where-Object { $_.phase -eq "evaluateResult" })
    $featureCreateResultEvents =
        @($lifecycleEvents | Where-Object { $_.phase -eq "featureCreateResult" })
    $captureOutLog =
        if ($Lane.Contains("captureOutLog")) { $Lane["captureOutLog"] } else { "" }
    $captureErrLog =
        if ($Lane.Contains("captureErrLog")) { $Lane["captureErrLog"] } else { "" }

    return [ordered]@{
        lifecycleLineCount = $lifecycleLines.Count
        lifecyclePhases = $phaseCounts
        lifecycleEvents = $lifecycleEvents
        featureCreateResultCount = $featureCreateResultEvents.Count
        evaluateResultCount = $evaluateResultEvents.Count
        recreationReasons =
            Get-UniqueSortedText -Values @(
                $lifecycleEvents | ForEach-Object { [string]$_.recreationReason }
            )
        featureHandles =
            Get-UniqueSortedText -Values @(
                $lifecycleEvents | ForEach-Object { [string]$_.featureHandle }
            )
        parameterHandles =
            Get-UniqueSortedText -Values @(
                $lifecycleEvents | ForEach-Object { [string]$_.parameters }
            )
        runtimeFlavors =
            Get-UniqueSortedText -Values @(
                $lifecycleEvents | ForEach-Object { [string]$_.runtimeFlavor }
            )
        runtimePathOverriddenValues =
            Get-UniqueSortedText -Values @(
                $lifecycleEvents | ForEach-Object {
                    [string]$_.runtimePathOverridden
                }
            )
        runtimePaths =
            Get-UniqueSortedText -Values @(
                $lifecycleEvents | ForEach-Object { [string]$_.runtimePath }
            )
        runtimePathFoundValues =
            Get-UniqueSortedText -Values @(
                $lifecycleEvents | ForEach-Object { [string]$_.runtimePathFound }
            )
        runtimeDllFoundValues =
            Get-UniqueSortedText -Values @(
                $lifecycleEvents | ForEach-Object { [string]$_.runtimeDllFound }
            )
        runtimeDllSizeBytesValues =
            Get-UniqueSortedText -Values @(
                $lifecycleEvents | ForEach-Object {
                    [string]$_.runtimeDllSizeBytes
                }
            )
        runtimeDllHashValues =
            Get-UniqueSortedText -Values @(
                $lifecycleEvents | ForEach-Object { [string]$_.runtimeDllHash }
            )
        resourceTraceLineCount = $resourceLines.Count
        selfEngineImageHandles = $selfEngineImageHandles
        warningHandles = $warningHandles
        warningResourceNames = $warningResourceNames
        warningHandleCount = $warningHandles.Count
        warningHandleOverlapWithSelfEngineResources = $overlapHandles
        warningHandleOverlapCount = $overlapHandles.Count
        suppressedNgxInternalLayoutLineCount =
            $suppressedNgxInternalLayoutLines.Count
        suppressedNgxInternalLayoutEvents =
            $suppressedNgxInternalLayoutEvents
        suppressedNgxInternalLayoutResourceNames =
            $suppressedNgxInternalLayoutResourceNames
        suppressedNgxInternalLayoutHandles =
            $suppressedNgxInternalLayoutHandles
        captureOutLog = $captureOutLog
        captureErrLog = $captureErrLog
    }
}

function New-DlssNgxInternalMvReproLaneSummary {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)]$Lane,
        [Parameter(Mandatory = $true)]$Trace
    )

    $metrics = $Lane["metrics"]
    $images = @()
    if ($Lane.Contains("image")) {
        $images += $Lane["image"]
    }
    if ($Lane.Contains("images")) {
        $images += @($Lane["images"])
    }

    return [ordered]@{
        label = $Label
        validationClean = [bool]$Lane["validationClean"]
        ngxResourceDiagnostic = [bool]$Lane["ngxResourceLayoutDiagnostic"]
        dlss = [ordered]@{
            qualityMode = $metrics.qualityMode
            recommendedPreset = $metrics.recommendedPreset
            evaluateOutput = $metrics.evaluateOutput
            postSource = $metrics.postSource
            qualityGate = $metrics.qualityGate
            qualityMasks = $metrics.qualityMasks
        }
        runtime = [ordered]@{
            flavor = $metrics.runtimeFlavor
            pathOverridden = $metrics.runtimePathOverridden
            pathFound = $metrics.runtimePathFound
            path = $metrics.runtimePath
            dllFound = $metrics.runtimeDllFound
            dllSizeBytes = $metrics.runtimeDllSizeBytes
            dllHash = $metrics.runtimeDllHash
            tracedFlavors = $Trace["runtimeFlavors"]
            tracedPathOverriddenValues = $Trace["runtimePathOverriddenValues"]
            tracedPaths = $Trace["runtimePaths"]
            tracedPathFoundValues = $Trace["runtimePathFoundValues"]
            tracedDllFoundValues = $Trace["runtimeDllFoundValues"]
            tracedDllSizeBytesValues = $Trace["runtimeDllSizeBytesValues"]
            tracedDllHashValues = $Trace["runtimeDllHashValues"]
        }
        lifecycle = [ordered]@{
            lineCount = $Trace["lifecycleLineCount"]
            phases = $Trace["lifecyclePhases"]
            featureCreateResultCount = $Trace["featureCreateResultCount"]
            evaluateResultCount = $Trace["evaluateResultCount"]
            recreationReasons = $Trace["recreationReasons"]
            featureHandles = $Trace["featureHandles"]
            parameterHandles = $Trace["parameterHandles"]
        }
        warnings = [ordered]@{
            resourceNames = $Trace["warningResourceNames"]
            handles = $Trace["warningHandles"]
            handleCount = $Trace["warningHandleCount"]
            overlapWithSelfEngineResources =
                $Trace["warningHandleOverlapWithSelfEngineResources"]
            overlapCount = $Trace["warningHandleOverlapCount"]
        }
        suppressedNgxInternalLayout = [ordered]@{
            lineCount = $Trace["suppressedNgxInternalLayoutLineCount"]
            resourceNames = $Trace["suppressedNgxInternalLayoutResourceNames"]
            handles = $Trace["suppressedNgxInternalLayoutHandles"]
            events = $Trace["suppressedNgxInternalLayoutEvents"]
        }
        selfEngineEvaluateResources = [ordered]@{
            traceLineCount = $Trace["resourceTraceLineCount"]
            imageHandles = $Trace["selfEngineImageHandles"]
        }
        artifacts = [ordered]@{
            csv = $Lane["csv"]
            captureOutLog = $Trace["captureOutLog"]
            captureErrLog = $Trace["captureErrLog"]
            images = $images
        }
    }
}

function New-DlssNgxParameterMatrixLaneSummary {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][hashtable]$Variant,
        [Parameter(Mandatory = $true)]$Lane,
        [Parameter(Mandatory = $true)]$Trace
    )

    $metrics = $Lane["metrics"]
    $images = @()
    if ($Lane.Contains("image")) {
        $images += $Lane["image"]
    }
    if ($Lane.Contains("images")) {
        $images += @($Lane["images"])
    }

    return [ordered]@{
        label = $Label
        variant = $Variant
        validationClean = [bool]$Lane["validationClean"]
        ngxResourceDiagnostic = [bool]$Lane["ngxResourceLayoutDiagnostic"]
        dlss = [ordered]@{
            qualityMode = $metrics.qualityMode
            recommendedPreset = $metrics.recommendedPreset
            evaluateOutput = $metrics.evaluateOutput
            postSource = $metrics.postSource
            qualityGate = $metrics.qualityGate
            qualityMasks = $metrics.qualityMasks
            qualityInputs = $metrics.qualityInputs
            jitterApplied = $metrics.jitterApplied
            history =
                "$($metrics.historyValid)/$($metrics.historyReset)/$($metrics.historyResetReason)"
            motionVectorScale = $metrics.dlssMotionVectorScale
        }
        runtime = [ordered]@{
            flavor = $metrics.runtimeFlavor
            pathOverridden = $metrics.runtimePathOverridden
            pathFound = $metrics.runtimePathFound
            path = $metrics.runtimePath
            dllFound = $metrics.runtimeDllFound
            dllSizeBytes = $metrics.runtimeDllSizeBytes
            dllHash = $metrics.runtimeDllHash
            tracedFlavors = $Trace["runtimeFlavors"]
            tracedPaths = $Trace["runtimePaths"]
            tracedDllHashes = $Trace["runtimeDllHashValues"]
        }
        lifecycle = [ordered]@{
            lineCount = $Trace["lifecycleLineCount"]
            phases = $Trace["lifecyclePhases"]
            featureCreateResultCount = $Trace["featureCreateResultCount"]
            evaluateResultCount = $Trace["evaluateResultCount"]
            recreationReasons = $Trace["recreationReasons"]
        }
        warnings = [ordered]@{
            resourceNames = $Trace["warningResourceNames"]
            handles = $Trace["warningHandles"]
            handleCount = $Trace["warningHandleCount"]
            overlapWithSelfEngineResources =
                $Trace["warningHandleOverlapWithSelfEngineResources"]
            overlapCount = $Trace["warningHandleOverlapCount"]
        }
        selfEngineEvaluateResources = [ordered]@{
            traceLineCount = $Trace["resourceTraceLineCount"]
            imageHandles = $Trace["selfEngineImageHandles"]
        }
        artifacts = [ordered]@{
            csv = $Lane["csv"]
            captureOutLog = $Trace["captureOutLog"]
            captureErrLog = $Trace["captureErrLog"]
            images = $images
        }
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
$dlssPresetMAllowedDiagnosticPatterns = @(
    "nv\.ngx\.dlss\.resource",
    "VUID-vkCmdDraw-None-09600",
    "descriptor with type equal"
)
$dlssPresetMInternalResourceAllowedDiagnosticPatterns = @(
    "nv\.ngx\.dlss\.",
    "VUID-vkCmdDraw-None-09600",
    "descriptor with type equal"
)

function Set-CaptureWindowPlacement {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$WindowHandle,
        [switch]$UseMonitorResolution
    )

    $topMost = [IntPtr](-1)
    $showWindow = 0x0040
    if ($UseMonitorResolution) {
        $captureLeft = [int]$script:captureMonitorWorkArea.BoundsLeft
        $captureTop = [int]$script:captureMonitorWorkArea.BoundsTop
        $captureWidth = [int]$script:captureMonitorWorkArea.BoundsWidth
        $captureHeight = [int]$script:captureMonitorWorkArea.BoundsHeight
    } else {
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
    }
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
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [string[]]$AllowedDiagnosticPatterns = @(),
        [switch]$UseMonitorResolution
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

            Set-CaptureWindowPlacement `
                -WindowHandle $windowHandle `
                -UseMonitorResolution:$UseMonitorResolution
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

    Assert-CleanLog `
        -Path $stdoutPath `
        -AllowedDiagnosticPatterns $AllowedDiagnosticPatterns
    Assert-CleanLog `
        -Path $stderrPath `
        -AllowedDiagnosticPatterns $AllowedDiagnosticPatterns
    return $imagePath
}

function Capture-WindowImageSequence {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [int]$FrameCount = 3,
        [int]$InitialDelaySeconds = 4,
        [int]$IntervalSeconds = 2,
        [string[]]$AllowedDiagnosticPatterns = @(),
        [switch]$UseMonitorResolution
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

            Set-CaptureWindowPlacement `
                -WindowHandle $windowHandle `
                -UseMonitorResolution:$UseMonitorResolution
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

    Assert-CleanLog `
        -Path $stdoutPath `
        -AllowedDiagnosticPatterns $AllowedDiagnosticPatterns
    Assert-CleanLog `
        -Path $stderrPath `
        -AllowedDiagnosticPatterns $AllowedDiagnosticPatterns
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

function Get-DebugImageVariationStats {
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

        for ($y = [int]($height * 0.20); $y -lt [int]($height * 0.80); $y += 4) {
            for ($x = [int]($width * 0.20); $x -lt [int]($width * 0.80); $x += 4) {
                $color = $bitmap.GetPixel($x, $y)
                $delta =
                    [Math]::Abs($color.R - $background.R) +
                    [Math]::Abs($color.G - $background.G) +
                    [Math]::Abs($color.B - $background.B)
                if ($delta -gt 2) {
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
            throw "Debug capture appears flat: $Path"
        }

        return [pscustomobject]@{
            Path = $Path
            Width = $width
            Height = $height
            SampledPixels = $sampledPixels
            DifferentPixels = $differentPixels
            MaxDelta = $maxDelta
            MeanDelta = [Math]::Round($sumDelta / [Math]::Max($sampledPixels, 1), 4)
            DebugThreshold = 2
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
            ChangedEdgeRatio = [Math]::Round(
                [double]$changedEdgePixels /
                [double][Math]::Max($edgePixels, 1),
                4
            )
            MaxEdgeDelta = [Math]::Round($maxEdgeDelta, 4)
            MeanEdgeDelta = [Math]::Round($sumEdgeDelta / [Math]::Max($edgePixels, 1), 4)
        }
    } finally {
        $bitmapA.Dispose()
        $bitmapB.Dispose()
    }
}

function Compare-ImageSequence {
    param(
        [Parameter(Mandatory = $true)][string[]]$Paths,
        [double]$GradientThreshold = 36.0,
        [double]$DeltaThreshold = 8.0
    )

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
        $edgeComparison =
            Compare-ImageEdges `
                -A $Paths[$index - 1] `
                -B $Paths[$index] `
                -GradientThreshold $GradientThreshold `
                -DeltaThreshold $DeltaThreshold
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

function Test-DebugMaskSampleActive {
    param(
        [Parameter(Mandatory = $true)][System.Drawing.Bitmap]$Bitmap,
        [Parameter(Mandatory = $true)]$Background,
        [Parameter(Mandatory = $true)][int]$X,
        [Parameter(Mandatory = $true)][int]$Y,
        [double]$LumaDeltaThreshold = 18.0,
        [double]$EdgeThreshold = 24.0
    )

    $color = $Bitmap.GetPixel($X, $Y)
    $lumaDelta =
        [Math]::Abs((Get-ColorLuma -Color $color) - (Get-ColorLuma -Color $Background))
    if ($lumaDelta -gt $LumaDeltaThreshold) {
        return $true
    }

    if ($X -ge 2 -and $X -lt ($Bitmap.Width - 2) -and
        $Y -ge 2 -and $Y -lt ($Bitmap.Height - 2)) {
        return (Get-ImageEdgeMagnitude -Bitmap $Bitmap -X $X -Y $Y) -gt $EdgeThreshold
    }

    return $false
}

function Compare-ImagePairInDebugMask {
    param(
        [Parameter(Mandatory = $true)][string]$A,
        [Parameter(Mandatory = $true)][string]$B,
        [Parameter(Mandatory = $true)][string]$MaskA,
        [Parameter(Mandatory = $true)][string]$MaskB,
        [Parameter(Mandatory = $true)][string]$MaskName,
        [double]$GradientThreshold = 36.0,
        [double]$DeltaThreshold = 8.0,
        [int]$SampleStep = 4
    )

    $comparison =
        [SelfEngineVisualQaImageMetrics]::CompareDebugMaskPair(
            $A,
            $B,
            $MaskA,
            $MaskB,
            $MaskName,
            $GradientThreshold,
            $DeltaThreshold,
            $SampleStep
        )
    if ($comparison.MaskPixels -le 0) {
        throw "No active $MaskName debug-mask pixels found for masked comparison: $MaskA $MaskB"
    }
    if ($comparison.EdgePixels -le 0) {
        throw "No color edges found inside active $MaskName debug-mask pixels: $A $B"
    }

    return [pscustomobject]@{
        maskName = $MaskName
        sampleStep = $comparison.SampleStep
        sampledPixels = $comparison.SampledPixels
        maskPixels = $comparison.MaskPixels
        maskCoverageRatio =
            [Math]::Round([double]$comparison.MaskPixels / [double][Math]::Max($comparison.SampledPixels, 1), 4)
        changedPixels = $comparison.ChangedPixels
        changedPixelRatio =
            [Math]::Round([double]$comparison.ChangedPixels / [double][Math]::Max($comparison.MaskPixels, 1), 4)
        maxDelta = $comparison.MaxDelta
        meanDelta =
            [Math]::Round($comparison.SumDelta / [Math]::Max($comparison.MaskPixels, 1), 4)
        edgePixels = $comparison.EdgePixels
        changedEdgePixels = $comparison.ChangedEdgePixels
        changedEdgeRatio =
            [Math]::Round([double]$comparison.ChangedEdgePixels / [double][Math]::Max($comparison.EdgePixels, 1), 4)
        meanEdgeDelta =
            [Math]::Round($comparison.SumEdgeDelta / [Math]::Max($comparison.EdgePixels, 1), 4)
        maxEdgeDelta = [Math]::Round($comparison.MaxEdgeDelta, 4)
    }
}

function Compare-ImageSequenceInDebugMask {
    param(
        [Parameter(Mandatory = $true)][string[]]$Paths,
        [Parameter(Mandatory = $true)][string[]]$MaskPaths,
        [Parameter(Mandatory = $true)][string]$MaskName,
        [int]$SampleStep = 4
    )

    if ($Paths.Count -ne $MaskPaths.Count) {
        throw "$MaskName masked sequence comparison needs matching color and mask image counts"
    }
    if ($Paths.Count -lt 2) {
        throw "$MaskName masked sequence comparison needs at least 2 images"
    }

    $pairs = @()
    $minChangedPixels = [int]::MaxValue
    $maxMeanDelta = 0.0
    $maxDelta = 0
    $minMaskPixels = [int]::MaxValue
    $maxMaskCoverageRatio = 0.0
    $minEdgePixels = [int]::MaxValue
    $maxChangedEdgePixels = 0
    $maxChangedEdgeRatio = 0.0
    $maxMeanEdgeDelta = 0.0
    $maxEdgeDelta = 0.0
    for ($index = 1; $index -lt $Paths.Count; ++$index) {
        $comparison =
            Compare-ImagePairInDebugMask `
                -A $Paths[$index - 1] `
                -B $Paths[$index] `
                -MaskA $MaskPaths[$index - 1] `
                -MaskB $MaskPaths[$index] `
                -MaskName $MaskName `
                -SampleStep $SampleStep
        $pairs += [pscustomobject]@{
            from = $Paths[$index - 1]
            to = $Paths[$index]
            maskFrom = $MaskPaths[$index - 1]
            maskTo = $MaskPaths[$index]
            sampleStep = $comparison.sampleStep
            sampledPixels = $comparison.sampledPixels
            maskPixels = $comparison.maskPixels
            maskCoverageRatio = $comparison.maskCoverageRatio
            changedPixels = $comparison.changedPixels
            changedPixelRatio = $comparison.changedPixelRatio
            meanDelta = $comparison.meanDelta
            maxDelta = $comparison.maxDelta
            edgePixels = $comparison.edgePixels
            changedEdgePixels = $comparison.changedEdgePixels
            changedEdgeRatio = $comparison.changedEdgeRatio
            meanEdgeDelta = $comparison.meanEdgeDelta
            maxEdgeDelta = $comparison.maxEdgeDelta
        }
        $minChangedPixels = [Math]::Min($minChangedPixels, [int]$comparison.changedPixels)
        $maxMeanDelta = [Math]::Max($maxMeanDelta, [double]$comparison.meanDelta)
        $maxDelta = [Math]::Max($maxDelta, [int]$comparison.maxDelta)
        $minMaskPixels = [Math]::Min($minMaskPixels, [int]$comparison.maskPixels)
        $maxMaskCoverageRatio =
            [Math]::Max($maxMaskCoverageRatio, [double]$comparison.maskCoverageRatio)
        $minEdgePixels = [Math]::Min($minEdgePixels, [int]$comparison.edgePixels)
        $maxChangedEdgePixels =
            [Math]::Max($maxChangedEdgePixels, [int]$comparison.changedEdgePixels)
        $maxChangedEdgeRatio =
            [Math]::Max($maxChangedEdgeRatio, [double]$comparison.changedEdgeRatio)
        $maxMeanEdgeDelta =
            [Math]::Max($maxMeanEdgeDelta, [double]$comparison.meanEdgeDelta)
        $maxEdgeDelta = [Math]::Max($maxEdgeDelta, [double]$comparison.maxEdgeDelta)
    }

    return [pscustomobject]@{
        maskName = $MaskName
        pairCount = $pairs.Count
        pairs = $pairs
        minChangedPixels = $minChangedPixels
        maxMeanDelta = [Math]::Round($maxMeanDelta, 4)
        maxDelta = $maxDelta
        minMaskPixels = $minMaskPixels
        maxMaskCoverageRatio = [Math]::Round($maxMaskCoverageRatio, 4)
        minEdgePixels = $minEdgePixels
        maxChangedEdgePixels = $maxChangedEdgePixels
        maxChangedEdgeRatio = [Math]::Round($maxChangedEdgeRatio, 4)
        maxMeanEdgeDelta = [Math]::Round($maxMeanEdgeDelta, 4)
        maxEdgeDelta = [Math]::Round($maxEdgeDelta, 4)
    }
}

function Compare-MaskedSequenceInstabilityHotspots {
    param(
        [Parameter(Mandatory = $true)][string[]]$ReferencePaths,
        [Parameter(Mandatory = $true)][string[]]$CandidatePaths,
        [Parameter(Mandatory = $true)][string[]]$MaskPaths,
        [Parameter(Mandatory = $true)][string]$MaskName,
        [int]$SampleStep = 4,
        [int]$TileColumns = 16,
        [int]$TileRows = 9,
        [int]$TopCount = 8
    )

    if ($ReferencePaths.Count -ne $CandidatePaths.Count -or
        $ReferencePaths.Count -ne $MaskPaths.Count) {
        throw "$MaskName hotspot comparison needs matching reference, candidate, and mask image counts"
    }
    if ($ReferencePaths.Count -lt 2) {
        throw "$MaskName hotspot comparison needs at least 2 images"
    }

    $hotspots = @()
    for ($index = 1; $index -lt $ReferencePaths.Count; ++$index) {
        $tiles =
            [SelfEngineVisualQaImageMetrics]::CompareDebugMaskInstabilityHotspots(
                $ReferencePaths[$index - 1],
                $ReferencePaths[$index],
                $CandidatePaths[$index - 1],
                $CandidatePaths[$index],
                $MaskPaths[$index - 1],
                $MaskPaths[$index],
                $MaskName,
                36.0,
                8.0,
                $SampleStep,
                $TileColumns,
                $TileRows,
                $TopCount
            )
        foreach ($tile in @($tiles)) {
            $hotspots += [pscustomobject]@{
                pairIndex = $index - 1
                from = $ReferencePaths[$index - 1]
                to = $ReferencePaths[$index]
                candidateFrom = $CandidatePaths[$index - 1]
                candidateTo = $CandidatePaths[$index]
                maskFrom = $MaskPaths[$index - 1]
                maskTo = $MaskPaths[$index]
                tileX = [int]$tile.TileX
                tileY = [int]$tile.TileY
                xStart = [int]$tile.XStart
                yStart = [int]$tile.YStart
                xEnd = [int]$tile.XEnd
                yEnd = [int]$tile.YEnd
                sampledPixels = [int]$tile.SampledPixels
                maskPixels = [int]$tile.MaskPixels
                edgePixels = [int]$tile.EdgePixels
                maskCoverageRatio =
                    [Math]::Round([double]$tile.MaskPixels / [double][Math]::Max([int]$tile.SampledPixels, 1), 4)
                excessPixels = [int]$tile.ExcessPixels
                excessPixelRatio =
                    [Math]::Round([double]$tile.ExcessPixels / [double][Math]::Max([int]$tile.MaskPixels, 1), 4)
                excessEdgeRatio =
                    [Math]::Round([double]$tile.ExcessPixels / [double][Math]::Max([int]$tile.EdgePixels, 1), 4)
                referenceEdgeDeltaSum =
                    [Math]::Round([double]$tile.ReferenceEdgeDeltaSum, 4)
                candidateEdgeDeltaSum =
                    [Math]::Round([double]$tile.CandidateEdgeDeltaSum, 4)
                positiveExcessSum =
                    [Math]::Round([double]$tile.PositiveExcessSum, 4)
                maxPositiveExcess =
                    [Math]::Round([double]$tile.MaxPositiveExcess, 4)
            }
        }
    }

    $topHotspots =
        @(
            $hotspots |
                Sort-Object `
                    -Property @{ Expression = "positiveExcessSum"; Descending = $true },
                              @{ Expression = "excessPixels"; Descending = $true } |
                Select-Object -First $TopCount
        )

    return [pscustomobject]@{
        maskName = $MaskName
        pairCount = $ReferencePaths.Count - 1
        sampleStep = $SampleStep
        tileColumns = $TileColumns
        tileRows = $TileRows
        topCount = $TopCount
        topHotspots = $topHotspots
    }
}

function Compare-SequenceInstabilityHotspots {
    param(
        [Parameter(Mandatory = $true)][string[]]$ReferencePaths,
        [Parameter(Mandatory = $true)][string[]]$CandidatePaths,
        [int]$SampleStep = 4,
        [int]$TileColumns = 16,
        [int]$TileRows = 9,
        [int]$TopCount = 8
    )

    if ($ReferencePaths.Count -ne $CandidatePaths.Count) {
        throw "Full-frame hotspot comparison needs matching reference and candidate image counts"
    }
    if ($ReferencePaths.Count -lt 2) {
        throw "Full-frame hotspot comparison needs at least 2 images"
    }

    $hotspots = @()
    for ($index = 1; $index -lt $ReferencePaths.Count; ++$index) {
        $tiles =
            [SelfEngineVisualQaImageMetrics]::CompareSequenceInstabilityHotspots(
                $ReferencePaths[$index - 1],
                $ReferencePaths[$index],
                $CandidatePaths[$index - 1],
                $CandidatePaths[$index],
                36.0,
                8.0,
                $SampleStep,
                $TileColumns,
                $TileRows,
                $TopCount
            )
        foreach ($tile in @($tiles)) {
            $hotspots += [pscustomobject]@{
                pairIndex = $index - 1
                from = $ReferencePaths[$index - 1]
                to = $ReferencePaths[$index]
                candidateFrom = $CandidatePaths[$index - 1]
                candidateTo = $CandidatePaths[$index]
                tileX = [int]$tile.TileX
                tileY = [int]$tile.TileY
                xStart = [int]$tile.XStart
                yStart = [int]$tile.YStart
                xEnd = [int]$tile.XEnd
                yEnd = [int]$tile.YEnd
                sampledPixels = [int]$tile.SampledPixels
                edgePixels = [int]$tile.EdgePixels
                edgeCoverageRatio =
                    [Math]::Round([double]$tile.EdgePixels / [double][Math]::Max([int]$tile.SampledPixels, 1), 4)
                excessPixels = [int]$tile.ExcessPixels
                excessEdgeRatio =
                    [Math]::Round([double]$tile.ExcessPixels / [double][Math]::Max([int]$tile.EdgePixels, 1), 4)
                referenceEdgeDeltaSum =
                    [Math]::Round([double]$tile.ReferenceEdgeDeltaSum, 4)
                candidateEdgeDeltaSum =
                    [Math]::Round([double]$tile.CandidateEdgeDeltaSum, 4)
                positiveExcessSum =
                    [Math]::Round([double]$tile.PositiveExcessSum, 4)
                maxPositiveExcess =
                    [Math]::Round([double]$tile.MaxPositiveExcess, 4)
            }
        }
    }

    $topHotspots =
        @(
            $hotspots |
                Sort-Object `
                    -Property @{ Expression = "positiveExcessSum"; Descending = $true },
                              @{ Expression = "excessPixels"; Descending = $true } |
                Select-Object -First $TopCount
        )

    return [pscustomobject]@{
        pairCount = $ReferencePaths.Count - 1
        sampleStep = $SampleStep
        tileColumns = $TileColumns
        tileRows = $TileRows
        topCount = $TopCount
        topHotspots = $topHotspots
    }
}

function New-HotspotLocalizationSummary {
    param(
        [Parameter(Mandatory = $true)]$FullFrameHotspots,
        $VelocityMaskedHotspots = $null,
        $DepthMaskedHotspots = $null,
        [int]$TopCount = 8
    )

    $fullTop = @($FullFrameHotspots.topHotspots | Select-Object -First $TopCount)
    $velocityTop = @()
    if ($null -ne $VelocityMaskedHotspots -and
        $null -ne $VelocityMaskedHotspots.PSObject.Properties["topHotspots"]) {
        $velocityTop =
            @($VelocityMaskedHotspots.topHotspots | Select-Object -First $TopCount)
    }
    $depthTop = @()
    if ($null -ne $DepthMaskedHotspots -and
        $null -ne $DepthMaskedHotspots.PSObject.Properties["topHotspots"]) {
        $depthTop =
            @($DepthMaskedHotspots.topHotspots | Select-Object -First $TopCount)
    }
    $fullTopCount = @($fullTop).Count
    $velocityTopCount = @($velocityTop).Count
    $depthTopCount = @($depthTop).Count

    $velocityKeys = @{}
    foreach ($tile in @($velocityTop)) {
        $velocityKeys["$($tile.tileX),$($tile.tileY)"] = $true
    }
    $depthKeys = @{}
    foreach ($tile in @($depthTop)) {
        $depthKeys["$($tile.tileX),$($tile.tileY)"] = $true
    }

    $overlaps = @()
    $velocityOverlapCount = 0
    $depthOverlapCount = 0
    $bothOverlapCount = 0
    foreach ($tile in @($fullTop)) {
        $key = "$($tile.tileX),$($tile.tileY)"
        $velocityOverlap = $velocityKeys.ContainsKey($key)
        $depthOverlap = $depthKeys.ContainsKey($key)
        if ($velocityOverlap) {
            ++$velocityOverlapCount
        }
        if ($depthOverlap) {
            ++$depthOverlapCount
        }
        if ($velocityOverlap -and $depthOverlap) {
            ++$bothOverlapCount
        }
        $overlaps += [pscustomobject]@{
            tileX = [int]$tile.tileX
            tileY = [int]$tile.tileY
            xStart = [int]$tile.xStart
            yStart = [int]$tile.yStart
            xEnd = [int]$tile.xEnd
            yEnd = [int]$tile.yEnd
            fullFramePositiveExcessSum =
                [double]$tile.positiveExcessSum
            fullFrameExcessPixels = [int]$tile.excessPixels
            overlapsVelocityMask = $velocityOverlap
            overlapsDepthMask = $depthOverlap
        }
    }

    $classification =
        if ($fullTopCount -le 0) {
            "no-positive-excess-hotspots"
        } elseif ($velocityTopCount -le 0 -and $depthTopCount -le 0) {
            "full-frame-only-no-mask-context"
        } elseif ($bothOverlapCount -gt 0) {
            "velocity-and-depth-overlap"
        } elseif ($depthOverlapCount -gt 0) {
            "depth-disocclusion-overlap"
        } elseif ($velocityOverlapCount -gt 0) {
            "velocity-silhouette-overlap"
        } else {
            "full-frame-only-background-or-unmasked"
        }

    return [ordered]@{
        topCount = $TopCount
        fullFrameTopCount = $fullTopCount
        velocityMaskedTopCount = $velocityTopCount
        depthMaskedTopCount = $depthTopCount
        fullFrameVelocityOverlapCount = $velocityOverlapCount
        fullFrameDepthOverlapCount = $depthOverlapCount
        fullFrameBothOverlapCount = $bothOverlapCount
        fullFrameVelocityOverlapRatio =
            [Math]::Round([double]$velocityOverlapCount / [double][Math]::Max($fullTopCount, 1), 4)
        fullFrameDepthOverlapRatio =
            [Math]::Round([double]$depthOverlapCount / [double][Math]::Max($fullTopCount, 1), 4)
        classification = $classification
        fullFrameTiles = $overlaps
        inference =
            "Overlap classification is diagnostic only: velocity overlap suggests skinned/scene motion-vector or silhouette focus; depth overlap suggests disocclusion/history/depth focus; full-frame-only suggests background/camera edge or unmasked content focus."
    }
}

function New-SequenceInstabilitySummary {
    param(
        [Parameter(Mandatory = $true)][string]$ReferenceName,
        [Parameter(Mandatory = $true)][string]$CandidateName,
        [Parameter(Mandatory = $true)]$Reference,
        [Parameter(Mandatory = $true)]$Candidate,
        [double]$ChangedEdgeRatioTolerance = 0.02,
        [double]$MeanEdgeDeltaTolerance = 2.0
    )

    $referenceChangedEdgeRatio = [double]$Reference.maxChangedEdgeRatio
    $candidateChangedEdgeRatio = [double]$Candidate.maxChangedEdgeRatio
    $referenceMeanEdgeDelta = [double]$Reference.maxMeanEdgeDelta
    $candidateMeanEdgeDelta = [double]$Candidate.maxMeanEdgeDelta
    $referenceMeanDelta = [double]$Reference.maxMeanDelta
    $candidateMeanDelta = [double]$Candidate.maxMeanDelta
    $referenceChangedPixels = [int]$Reference.minChangedPixels
    $candidateChangedPixels = [int]$Candidate.minChangedPixels
    $referenceChangedEdgePixels = [int]$Reference.maxChangedEdgePixels
    $candidateChangedEdgePixels = [int]$Candidate.maxChangedEdgePixels

    $changedEdgeRatioRelative = $null
    if ([Math]::Abs($referenceChangedEdgeRatio) -gt 0.000001) {
        $changedEdgeRatioRelative =
            [Math]::Round($candidateChangedEdgeRatio / $referenceChangedEdgeRatio, 4)
    }
    $meanEdgeDeltaRelative = $null
    if ([Math]::Abs($referenceMeanEdgeDelta) -gt 0.000001) {
        $meanEdgeDeltaRelative =
            [Math]::Round($candidateMeanEdgeDelta / $referenceMeanEdgeDelta, 4)
    }
    $meanDeltaRelative = $null
    if ([Math]::Abs($referenceMeanDelta) -gt 0.000001) {
        $meanDeltaRelative =
            [Math]::Round($candidateMeanDelta / $referenceMeanDelta, 4)
    }

    $changedEdgeRatioDelta =
        [Math]::Round($candidateChangedEdgeRatio - $referenceChangedEdgeRatio, 4)
    $meanEdgeDeltaDelta =
        [Math]::Round($candidateMeanEdgeDelta - $referenceMeanEdgeDelta, 4)

    return [ordered]@{
        referenceName = $ReferenceName
        candidateName = $CandidateName
        reference = [ordered]@{
            pairCount = [int]$Reference.pairCount
            minChangedPixels = $referenceChangedPixels
            maxMeanDelta = [Math]::Round($referenceMeanDelta, 4)
            minEdgePixels = [int]$Reference.minEdgePixels
            maxChangedEdgePixels = $referenceChangedEdgePixels
            maxChangedEdgeRatio = [Math]::Round($referenceChangedEdgeRatio, 4)
            maxMeanEdgeDelta = [Math]::Round($referenceMeanEdgeDelta, 4)
        }
        candidate = [ordered]@{
            pairCount = [int]$Candidate.pairCount
            minChangedPixels = $candidateChangedPixels
            maxMeanDelta = [Math]::Round($candidateMeanDelta, 4)
            minEdgePixels = [int]$Candidate.minEdgePixels
            maxChangedEdgePixels = $candidateChangedEdgePixels
            maxChangedEdgeRatio = [Math]::Round($candidateChangedEdgeRatio, 4)
            maxMeanEdgeDelta = [Math]::Round($candidateMeanEdgeDelta, 4)
        }
        delta = [ordered]@{
            minChangedPixels =
                $candidateChangedPixels - $referenceChangedPixels
            maxMeanDelta =
                [Math]::Round($candidateMeanDelta - $referenceMeanDelta, 4)
            maxChangedEdgePixels =
                $candidateChangedEdgePixels - $referenceChangedEdgePixels
            maxChangedEdgeRatio = $changedEdgeRatioDelta
            maxMeanEdgeDelta = $meanEdgeDeltaDelta
        }
        relative = [ordered]@{
            maxChangedEdgeRatio = $changedEdgeRatioRelative
            maxMeanEdgeDelta = $meanEdgeDeltaRelative
            maxMeanDelta = $meanDeltaRelative
        }
        candidateNotWorseWithinTolerance =
            ($changedEdgeRatioDelta -le $ChangedEdgeRatioTolerance -and
             $meanEdgeDeltaDelta -le $MeanEdgeDeltaTolerance)
        tolerances = [ordered]@{
            changedEdgeRatio = $ChangedEdgeRatioTolerance
            meanEdgeDelta = $MeanEdgeDeltaTolerance
        }
    }
}

function New-AnimationPlaybackPhaseComparison {
    param(
        [Parameter(Mandatory = $true)][string]$ReferenceName,
        [Parameter(Mandatory = $true)]$ReferenceMetrics,
        [Parameter(Mandatory = $true)][string]$CandidateName,
        [Parameter(Mandatory = $true)]$CandidateMetrics
    )

    $readString = {
        param(
            [Parameter(Mandatory = $true)]$Metrics,
            [Parameter(Mandatory = $true)][string]$Name,
            [Parameter(Mandatory = $true)][string]$DefaultValue
        )

        $property = $Metrics.PSObject.Properties[$Name]
        if ($null -eq $property -or $null -eq $property.Value) {
            return $DefaultValue
        }

        $text = "$($property.Value)"
        if ($text.Length -le 0) {
            return $DefaultValue
        }

        return $text
    }
    $readDouble = {
        param(
            [Parameter(Mandatory = $true)]$Metrics,
            [Parameter(Mandatory = $true)][string]$Name,
            [Parameter(Mandatory = $true)][double]$DefaultValue
        )

        $text = & $readString $Metrics $Name "$DefaultValue"
        try {
            return [double]::Parse(
                $text,
                [System.Globalization.CultureInfo]::InvariantCulture
            )
        } catch {
            try {
                return [double]$text
            } catch {
                return $DefaultValue
            }
        }
    }

    $referenceClockMode =
        & $readString $ReferenceMetrics "runtimeImportAnimationPlaybackClockMode" "0"
    $candidateClockMode =
        & $readString $CandidateMetrics "runtimeImportAnimationPlaybackClockMode" "0"
    $referenceCurrentSeconds =
        & $readDouble $ReferenceMetrics "runtimeImportAnimationPlaybackCurrentAbsoluteSeconds" -1.0
    $candidateCurrentSeconds =
        & $readDouble $CandidateMetrics "runtimeImportAnimationPlaybackCurrentAbsoluteSeconds" -1.0
    $referenceCurrentTicks =
        & $readDouble $ReferenceMetrics "runtimeImportAnimationPlaybackCurrentTimeTicks" -1.0
    $candidateCurrentTicks =
        & $readDouble $CandidateMetrics "runtimeImportAnimationPlaybackCurrentTimeTicks" -1.0

    return [ordered]@{
        referenceName = $ReferenceName
        candidateName = $CandidateName
        deterministicClockUsed =
            ($referenceClockMode -eq "1" -and $candidateClockMode -eq "1")
        clockMode = [ordered]@{
            reference = $referenceClockMode
            candidate = $candidateClockMode
            expected = "1"
        }
        currentAbsoluteSeconds = [ordered]@{
            reference = $referenceCurrentSeconds
            candidate = $candidateCurrentSeconds
            absoluteDelta =
                [Math]::Round([Math]::Abs($candidateCurrentSeconds - $referenceCurrentSeconds), 4)
        }
        currentTimeTicks = [ordered]@{
            reference = $referenceCurrentTicks
            candidate = $candidateCurrentTicks
            absoluteDelta =
                [Math]::Round([Math]::Abs($candidateCurrentTicks - $referenceCurrentTicks), 4)
        }
        interpretation =
            "Evidence only: preset K/M should use clockMode=1 so animation phase follows capture elapsed time instead of each run's frame delta."
    }
}

function New-DlssDynamicLaneContractSummary {
    param(
        [Parameter(Mandatory = $true)][string]$LaneName,
        [Parameter(Mandatory = $true)]$Metrics,
        [Parameter(Mandatory = $true)][string]$ExpectedPreset,
        [switch]$RequireSkinnedVelocity
    )

    $presetReady = "$($Metrics.recommendedPreset)" -eq $ExpectedPreset
    $postReady = "$($Metrics.postSource)" -eq "1/1/0"
    $evaluateReady = "$($Metrics.evaluateOutput)" -eq "1/1"
    $jitterReady = "$($Metrics.jitterApplied)" -eq "1"
    $historyReady =
        "$($Metrics.historyValid)" -eq "1" -and
        "$($Metrics.historyReset)" -eq "0" -and
        "$($Metrics.historyResetReason)" -eq "0"
    $dlssResetReady = "$($Metrics.dlssReset)" -eq "0"
    $motionVectorScaleReady =
        "$($Metrics.dlssMotionVectorScalePixelSpace)" -eq "1" -or
        "$($Metrics.dlssMotionVectorScaleMatchesRenderExtent)" -eq "1"
    $taaOrderingReady =
        "$($Metrics.temporalUpscaleInputReady)" -eq "1" -and
        "$($Metrics.nativeTaaResolveEnabled)" -eq "0" -and
        "$($Metrics.nativeTaaResolveSuppressedForUpscaler)" -eq "1"
    $velocityObjectMotionReady = "$($Metrics.velocityObjectMotionReady)" -eq "1"
    $sceneContentMotionSupported =
        "$($Metrics.sceneContentMotionSupported)" -eq "1"
    $objectMotionReady = "$($Metrics.objectMotionReady)" -eq "1"
    $qualityGateReady = "$($Metrics.qualityGate)" -eq "1/1/0"
    $qualityMasksReady = "$($Metrics.qualityMasks)" -eq "255/255/0"
    $qualityInputsReady =
        "$($Metrics.qualityInputs)" -eq
        "output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/1"
    $readIntMetric = {
        param(
            [Parameter(Mandatory = $true)][string]$Name
        )

        try {
            return [int](Get-JsonMemberValue -Object $Metrics -Name $Name -DefaultValue 0)
        } catch {
            return 0
        }
    }
    $shaderSkinningReady =
        (& $readIntMetric "bonePaletteShaderSkinningPathReady") -eq 1
    $shaderVelocityReady =
        (& $readIntMetric "bonePaletteShaderVelocityPathReady") -eq 1
    $bonePaletteChangedReady =
        (& $readIntMetric "bonePaletteDrawChangedEntryCount") -gt 0
    $runtimePlaybackReady =
        (& $readIntMetric "runtimeImportAnimationPlaybackReady") -eq 1
    $runtimePlaybackAdvanced =
        (& $readIntMetric "runtimeImportAnimationPlaybackFrameCount") -gt 0 -and
        (& $readIntMetric "runtimeImportAnimationPlaybackChangedBonePaletteEntryCount") -gt 0
    $previousPoseContinuityReady =
        (& $readIntMetric "runtimeImportAnimationPlaybackPreviousPoseCollapsedCount") -eq 0
    $animationClockDeterministicReady =
        -not $RequireSkinnedVelocity -or
        (& $readIntMetric "runtimeImportAnimationPlaybackClockMode") -eq 1
    $skinnedVelocityInputReady =
        -not $RequireSkinnedVelocity -or
        ($shaderSkinningReady -and
         $shaderVelocityReady -and
         $bonePaletteChangedReady -and
         $runtimePlaybackReady -and
         $runtimePlaybackAdvanced -and
         $animationClockDeterministicReady -and
         $previousPoseContinuityReady)

    $temporalInputContractReady =
        $presetReady -and
        $postReady -and
        $evaluateReady -and
        $jitterReady -and
        $historyReady -and
        $dlssResetReady -and
        $motionVectorScaleReady -and
        $taaOrderingReady
    $qualityInputReady =
        $qualityGateReady -and $qualityMasksReady -and $qualityInputsReady

    $blockedReasons = @()
    if (-not $presetReady) {
        $blockedReasons += "unexpected preset"
    }
    if (-not $postReady) {
        $blockedReasons += "post source not active"
    }
    if (-not $evaluateReady) {
        $blockedReasons += "DLSS evaluate output not ready"
    }
    if (-not $jitterReady) {
        $blockedReasons += "projection jitter not applied"
    }
    if (-not $historyReady) {
        $blockedReasons += "temporal history reset or invalid"
    }
    if (-not $dlssResetReady) {
        $blockedReasons += "DLSS reset active"
    }
    if (-not $motionVectorScaleReady) {
        $blockedReasons += "motion-vector scale mismatch"
    }
    if (-not $taaOrderingReady) {
        $blockedReasons += "TAA/upscale ordering mismatch"
    }
    if (-not $qualityGateReady) {
        $blockedReasons += "quality gate not ready"
    }
    if (-not $objectMotionReady) {
        if (-not $velocityObjectMotionReady) {
            $blockedReasons += "object velocity coverage not ready"
        }
        if (-not $sceneContentMotionSupported) {
            $blockedReasons += "scene content motion support not verified"
        }
        if ($velocityObjectMotionReady -and $sceneContentMotionSupported) {
            $blockedReasons += "object quality motion input not ready"
        }
    }
    if (-not $qualityMasksReady) {
        $blockedReasons += "quality mask readiness incomplete"
    }
    if (-not $qualityInputsReady) {
        $blockedReasons += "quality input readiness incomplete"
    }
    if (-not $skinnedVelocityInputReady) {
        if (-not $shaderSkinningReady) {
            $blockedReasons += "skinned shader skinning path not ready"
        }
        if (-not $shaderVelocityReady) {
            $blockedReasons += "skinned shader velocity path not ready"
        }
        if (-not $bonePaletteChangedReady) {
            $blockedReasons += "skinned bone palette did not change"
        }
        if (-not $runtimePlaybackReady) {
            $blockedReasons += "runtime skinned animation playback not ready"
        }
        if (-not $runtimePlaybackAdvanced) {
            $blockedReasons += "runtime skinned animation did not advance"
        }
        if (-not $animationClockDeterministicReady) {
            $blockedReasons += "runtime skinned animation is not using elapsed-time clock"
        }
        if (-not $previousPoseContinuityReady) {
            $blockedReasons += "runtime skinned previous pose collapsed"
        }
    }

    return [ordered]@{
        lane = $LaneName
        temporalInputContractReady = $temporalInputContractReady
        qualityInputReady = $qualityInputReady
        skinnedVelocityRequired = [bool]$RequireSkinnedVelocity
        skinnedVelocityInputReady = $skinnedVelocityInputReady
        productionInputReady =
            ($temporalInputContractReady -and
             $qualityInputReady -and
             $skinnedVelocityInputReady)
        blockedReasons = $blockedReasons
        observed = [ordered]@{
            preset = "$($Metrics.recommendedPreset)"
            postSource = "$($Metrics.postSource)"
            evaluateOutput = "$($Metrics.evaluateOutput)"
            qualityGate = "$($Metrics.qualityGate)"
            qualityMasks = "$($Metrics.qualityMasks)"
            qualityInputs = "$($Metrics.qualityInputs)"
            velocityObjectMotionReady = "$($Metrics.velocityObjectMotionReady)"
            objectMotionReady = "$($Metrics.objectMotionReady)"
            sceneContentMotionSupported = "$($Metrics.sceneContentMotionSupported)"
            benchmarkMotionTime = "$($Metrics.benchmarkCameraMotionTimeSeconds)/$($Metrics.benchmarkObjectMotionTimeSeconds)"
            jitterApplied = "$($Metrics.jitterApplied)"
            history = "$($Metrics.historyValid)/$($Metrics.historyReset)/$($Metrics.historyResetReason)"
            dlssReset = "$($Metrics.dlssReset)"
            dlssMotionVectorScale = "$($Metrics.dlssMotionVectorScale)"
            dlssMotionVectorScaleSemantics =
                "pixel/unit/matchesRender=$($Metrics.dlssMotionVectorScalePixelSpace)/$($Metrics.dlssMotionVectorScaleUnitSpace)/$($Metrics.dlssMotionVectorScaleMatchesRenderExtent)"
            dlssCreateFlagBits = "$($Metrics.dlssCreateFlagBits)"
            dlssInputExtents = "$($Metrics.dlssInputExtents)"
            dlssInputExtentMatches = "$($Metrics.dlssInputExtentMatches)"
            taaOrdering = "$($Metrics.temporalUpscaleInputReady)/$($Metrics.nativeTaaResolveEnabled)/$($Metrics.nativeTaaResolveSuppressedForUpscaler)"
            skinnedVelocity = [ordered]@{
                required = [bool]$RequireSkinnedVelocity
                shaderSkinningReady = $shaderSkinningReady
                shaderVelocityReady = $shaderVelocityReady
                bonePaletteChangedReady = $bonePaletteChangedReady
                runtimePlaybackReady = $runtimePlaybackReady
                runtimePlaybackAdvanced = $runtimePlaybackAdvanced
                animationClockDeterministicReady = $animationClockDeterministicReady
                previousPoseContinuityReady = $previousPoseContinuityReady
                playbackClockMode =
                    (& $readIntMetric "runtimeImportAnimationPlaybackClockMode")
                playbackPreviousTimeTicks =
                    "$($Metrics.runtimeImportAnimationPlaybackPreviousTimeTicks)"
                playbackCurrentTimeTicks =
                    "$($Metrics.runtimeImportAnimationPlaybackCurrentTimeTicks)"
                playbackPreviousAbsoluteSeconds =
                    "$($Metrics.runtimeImportAnimationPlaybackPreviousAbsoluteSeconds)"
                playbackCurrentAbsoluteSeconds =
                    "$($Metrics.runtimeImportAnimationPlaybackCurrentAbsoluteSeconds)"
                bonePaletteChangedEntryCount =
                    (& $readIntMetric "bonePaletteDrawChangedEntryCount")
                playbackFrameCount =
                    (& $readIntMetric "runtimeImportAnimationPlaybackFrameCount")
                playbackChangedBonePaletteEntryCount =
                    (& $readIntMetric "runtimeImportAnimationPlaybackChangedBonePaletteEntryCount")
                previousPoseCollapsedCount =
                    (& $readIntMetric "runtimeImportAnimationPlaybackPreviousPoseCollapsedCount")
                loopWrapCount =
                    (& $readIntMetric "runtimeImportAnimationPlaybackLoopWrapCount")
            }
        }
        expected = [ordered]@{
            preset = $ExpectedPreset
            postSource = "1/1/0"
            evaluateOutput = "1/1"
            qualityGate = "1/1/0"
            qualityMasks = "255/255/0"
            qualityInputs =
                "output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/1"
            velocityObjectMotionReady = "1"
            objectMotionReady = "1"
            sceneContentMotionSupported = "1"
            jitterApplied = "1"
            history = "1/0/0"
            dlssReset = "0"
            dlssMotionVectorScale = "pixel-space render extent"
            taaOrdering = "1/0/1"
            skinnedVelocity =
                if ($RequireSkinnedVelocity) {
                    "shaderSkinning/shaderVelocity/bonePaletteChanged/playbackAdvanced/elapsedClock/previousPose=1/1/>0/1/1/0"
                } else {
                    "not-required"
                }
        }
    }
}

function Compare-ImageSequencePairSet {
    param(
        [Parameter(Mandatory = $true)][string[]]$APaths,
        [Parameter(Mandatory = $true)][string[]]$BPaths
    )

    if ($APaths.Count -ne $BPaths.Count) {
        throw "Image sequence pair-set comparison needs matching image counts"
    }
    if ($APaths.Count -lt 1) {
        throw "Image sequence pair-set comparison needs at least one image pair"
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
    for ($index = 0; $index -lt $APaths.Count; ++$index) {
        $comparison = Compare-Images -A $APaths[$index] -B $BPaths[$index]
        $edgeComparison = Compare-ImageEdges -A $APaths[$index] -B $BPaths[$index]
        $changedEdgeRatio =
            [double]$edgeComparison.ChangedEdgePixels /
            [double][Math]::Max([int]$edgeComparison.EdgePixels, 1)
        $pairs += [pscustomobject]@{
            index = $index
            a = $APaths[$index]
            b = $BPaths[$index]
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

function Test-DlssDynamicFullImageComparisonEnabled {
    $value =
        [Environment]::GetEnvironmentVariable(
            "SE_DLSS_DYNAMIC_FULL_IMAGE_COMPARISON",
            "Process"
        )
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $false
    }

    $normalized = $value.Trim().ToLowerInvariant()
    return @("1", "true", "yes", "on") -contains $normalized
}

function New-SkippedImageSequencePairSetComparison {
    param(
        [Parameter(Mandatory = $true)][string[]]$APaths,
        [Parameter(Mandatory = $true)][string[]]$BPaths
    )

    if ($APaths.Count -ne $BPaths.Count) {
        throw "Image sequence pair-set comparison needs matching image counts"
    }

    return [pscustomobject]@{
        pairCount = $APaths.Count
        comparisonSkipped = $true
        reason =
            "Set SE_DLSS_DYNAMIC_FULL_IMAGE_COMPARISON=1 to run expensive same-frame native/K/M image comparisons"
    }
}

function Compare-DlssDynamicImageSequencePairSet {
    param(
        [Parameter(Mandatory = $true)][string[]]$APaths,
        [Parameter(Mandatory = $true)][string[]]$BPaths
    )

    if (Test-DlssDynamicFullImageComparisonEnabled) {
        return Compare-ImageSequencePairSet -APaths $APaths -BPaths $BPaths
    }

    return New-SkippedImageSequencePairSetComparison `
        -APaths $APaths `
        -BPaths $BPaths
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

function Test-DlssMotionVectorScalePixelSpace {
    param([Parameter(Mandatory = $true)]$Row)

    $pixelSpaceProperty =
        $Row.PSObject.Properties["temporal_upscaler_dlss_motion_vector_scale_pixel_space"]
    if ($null -ne $pixelSpaceProperty -and
        "$($pixelSpaceProperty.Value)".Length -gt 0) {
        return "$($pixelSpaceProperty.Value)" -eq "1"
    }

    $scaleX =
        Convert-InvariantDouble $Row.temporal_upscaler_dlss_motion_vector_scale_x
    $scaleY =
        Convert-InvariantDouble $Row.temporal_upscaler_dlss_motion_vector_scale_y
    $renderWidth =
        Convert-InvariantDouble $Row.temporal_upscaler_dlss_render_width
    $renderHeight =
        Convert-InvariantDouble $Row.temporal_upscaler_dlss_render_height
    return [Math]::Abs([Math]::Abs($scaleX) - $renderWidth) -le 0.0001 -and
        [Math]::Abs([Math]::Abs($scaleY) - $renderHeight) -le 0.0001
}

function Assert-DlssMotionVectorScalePixelSpace {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Row
    )

    if (Test-DlssMotionVectorScalePixelSpace -Row $Row) {
        return
    }

    throw (
        "{0} DLSS motion-vector scale is not pixel-space: scale={1}/{2} render={3}x{4}" -f
        $Name,
        $Row.temporal_upscaler_dlss_motion_vector_scale_x,
        $Row.temporal_upscaler_dlss_motion_vector_scale_y,
        $Row.temporal_upscaler_dlss_render_width,
        $Row.temporal_upscaler_dlss_render_height
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
    "SE_TAA_APPLY_JITTER" = "1"
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$defaultSceneDlaaPresentEnvironment = @{
    "SE_RENDER_SCALE" = "1.0"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_TAA_APPLY_JITTER" = "1"
    "SE_UPSCALER_PLUGIN" = "dlss"
    "SE_DLSS_QUALITY" = "dlaa"
    "SE_DLSS_PRESENT" = "1"
    "SE_DLSS_REFERENCE_BASELINE_PATH" = $defaultSceneDlaaBaselineManifestPath
    "SE_RENDER_VIEW" = "deferred-hdr"
}
$defaultSceneDlaaPresetKEnvironment =
    $defaultSceneDlaaPresentEnvironment.Clone()
$defaultSceneDlaaPresetKEnvironment["SE_DLSS_PRESET"] = "k"
$defaultSceneDlaaPresetLEnvironment =
    $defaultSceneDlaaPresentEnvironment.Clone()
$defaultSceneDlaaPresetLEnvironment["SE_DLSS_PRESET"] = "l"
$defaultSceneDlaaPresetMEnvironment =
    $defaultSceneDlaaPresentEnvironment.Clone()
$defaultSceneDlaaPresetMEnvironment["SE_DLSS_PRESET"] = "m"
$defaultSceneDlaaPresetMEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneDlaaSharpnessZeroEnvironment =
    $defaultSceneDlaaPresentEnvironment.Clone()
$defaultSceneDlaaSharpnessZeroEnvironment["SE_DLSS_SHARPNESS"] = "0.0"
$defaultSceneDlaaMipBiasPositiveEnvironment =
    $defaultSceneDlaaPresentEnvironment.Clone()
$defaultSceneDlaaMipBiasPositiveEnvironment["SE_TEXTURE_MIP_LOD_BIAS"] = "1.0"
$highResolutionWindowWidth = [Math]::Max(
    1280,
    [int]$script:captureMonitorWorkArea.BoundsWidth
)
$highResolutionWindowHeight = [Math]::Max(
    720,
    [int]$script:captureMonitorWorkArea.BoundsHeight
)
function Set-HighResolutionVisualQaEnvironment {
    param([Parameter(Mandatory = $true)][hashtable]$Environment)

    $Environment["SE_WINDOW_WIDTH"] = [string]$highResolutionWindowWidth
    $Environment["SE_WINDOW_HEIGHT"] = [string]$highResolutionWindowHeight
    $Environment["SE_WINDOW_BORDERLESS"] = "1"
    $Environment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
}
$defaultSceneDlaaHighResolutionNativeEnvironment =
    $defaultSceneDlaaNativeEnvironment.Clone()
$defaultSceneDlaaHighResolutionNativeEnvironment["SE_WINDOW_WIDTH"] =
    [string]$highResolutionWindowWidth
$defaultSceneDlaaHighResolutionNativeEnvironment["SE_WINDOW_HEIGHT"] =
    [string]$highResolutionWindowHeight
$defaultSceneDlaaHighResolutionNativeEnvironment["SE_WINDOW_BORDERLESS"] = "1"
$defaultSceneDlaaHighResolutionNativeVisualQaEnvironment =
    $defaultSceneDlaaHighResolutionNativeEnvironment.Clone()
$defaultSceneDlaaHighResolutionNativeVisualQaEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] =
    "1"
$defaultSceneDlaaHighResolutionPresetKEnvironment =
    $defaultSceneDlaaPresetKEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetKEnvironment["SE_WINDOW_WIDTH"] =
    [string]$highResolutionWindowWidth
$defaultSceneDlaaHighResolutionPresetKEnvironment["SE_WINDOW_HEIGHT"] =
    [string]$highResolutionWindowHeight
$defaultSceneDlaaHighResolutionPresetKEnvironment["SE_WINDOW_BORDERLESS"] = "1"
$defaultSceneDlaaHighResolutionPresetKVisualQaEnvironment =
    $defaultSceneDlaaHighResolutionPresetKEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetKVisualQaEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] =
    "1"
$defaultSceneDlaaHighResolutionPresetKResourceDiagnosticsEnvironment =
    $defaultSceneDlaaHighResolutionPresetKVisualQaEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetKResourceDiagnosticsEnvironment["SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS"] =
    "1"
$defaultSceneDlaaHighResolutionPresetMEnvironment =
    $defaultSceneDlaaPresetMEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetMEnvironment["SE_WINDOW_WIDTH"] =
    [string]$highResolutionWindowWidth
$defaultSceneDlaaHighResolutionPresetMEnvironment["SE_WINDOW_HEIGHT"] =
    [string]$highResolutionWindowHeight
$defaultSceneDlaaHighResolutionPresetMEnvironment["SE_WINDOW_BORDERLESS"] = "1"
$defaultSceneDlaaHighResolutionPresetMEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment =
    $defaultSceneDlaaHighResolutionPresetMEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment["SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS"] =
    "1"
$defaultSceneDlaaHighResolutionPresetMRelRuntimeDiagnosticsEnvironment =
    $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetMRelRuntimeDiagnosticsEnvironment["SE_DLSS_RUNTIME_FLAVOR"] =
    "rel"
$dlssWindowsRelRuntimePath =
    [System.IO.Path]::GetFullPath(
        (Join-Path $repoRoot "thirdParty\nvidia_dlss\lib\Windows_x86_64\rel")
    )
$dlssRuntimeOverrideAuditPath = if (
    $externalDlssRuntimeOverridePath.Length -gt 0
) {
    $externalDlssRuntimeOverridePath
} else {
    $dlssWindowsRelRuntimePath
}
$defaultSceneDlaaHighResolutionPresetMRelRuntimeOverrideDiagnosticsEnvironment =
    $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetMRelRuntimeOverrideDiagnosticsEnvironment["SE_DLSS_RUNTIME_FLAVOR"] =
    "rel"
$defaultSceneDlaaHighResolutionPresetMRelRuntimeOverrideDiagnosticsEnvironment["SE_DLSS_RUNTIME_PATH"] =
    $dlssRuntimeOverrideAuditPath
$defaultSceneDlaaHighResolutionPresetMCustomRuntimeDiagnosticsEnvironment =
    $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetMCustomRuntimeDiagnosticsEnvironment["SE_DLSS_RUNTIME_FLAVOR"] =
    "rel"
if ($externalDlssRuntimeOverridePath.Length -gt 0) {
    $defaultSceneDlaaHighResolutionPresetMCustomRuntimeDiagnosticsEnvironment["SE_DLSS_RUNTIME_PATH"] =
        $externalDlssRuntimeOverridePath
}
$defaultSceneDlaaHighResolutionPresetMDevRuntimeDiagnosticsEnvironment =
    $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetMDevRuntimeDiagnosticsEnvironment["SE_DLSS_RUNTIME_FLAVOR"] =
    "dev"
$defaultSceneDlaaHighResolutionPresetMNoTransparencyMaskEnvironment =
    $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetMNoTransparencyMaskEnvironment["SE_DLSS_DISABLE_TRANSPARENCY_MASK_BINDING"] =
    "1"
$defaultSceneDlaaHighResolutionPresetMNoBiasMaskEnvironment =
    $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetMNoBiasMaskEnvironment["SE_DLSS_DISABLE_BIAS_CURRENT_COLOR_MASK_BINDING"] =
    "1"
$defaultSceneDlaaHighResolutionPresetMNoOptionalMasksEnvironment =
    $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment.Clone()
$defaultSceneDlaaHighResolutionPresetMNoOptionalMasksEnvironment["SE_DLSS_DISABLE_OPTIONAL_MASK_BINDINGS"] =
    "1"
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
$defaultSceneDlaaMotionNativeEnvironment =
    $defaultSceneDlaaNativeEnvironment.Clone()
$defaultSceneDlaaMotionNativeEnvironment["SE_BENCHMARK_CAMERA_MOTION"] =
    "orbit"
$defaultSceneDlaaMotionNativeEnvironment["SE_BENCHMARK_CAMERA_MOTION_SPEED"] =
    "0.65"
$defaultSceneDlaaMotionNativeEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneDlaaPresetKMotionPresentEnvironment =
    $defaultSceneDlaaMotionPresentEnvironment.Clone()
$defaultSceneDlaaPresetKMotionPresentEnvironment["SE_DLSS_PRESET"] = "k"
$defaultSceneDlaaPresetKMotionPresentEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneDlaaPresetKMotionResourceDiagnosticsEnvironment =
    $defaultSceneDlaaPresetKMotionPresentEnvironment.Clone()
$defaultSceneDlaaPresetKMotionResourceDiagnosticsEnvironment["SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS"] =
    "1"
$defaultSceneDlaaPresetMMotionPresentEnvironment =
    $defaultSceneDlaaMotionPresentEnvironment.Clone()
$defaultSceneDlaaPresetMMotionPresentEnvironment["SE_DLSS_PRESET"] = "m"
$defaultSceneDlaaPresetMMotionPresentEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneDlaaPresetMMotionResourceDiagnosticsEnvironment =
    $defaultSceneDlaaPresetMMotionPresentEnvironment.Clone()
$defaultSceneDlaaPresetMMotionResourceDiagnosticsEnvironment["SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS"] =
    "1"
$defaultSceneDlaaObjectOnlyNativeEnvironment =
    $defaultSceneDlaaNativeEnvironment.Clone()
$defaultSceneDlaaObjectOnlyNativeEnvironment["SE_BENCHMARK_OBJECT_MOTION"] =
    "orbit"
$defaultSceneDlaaObjectOnlyNativeEnvironment["SE_BENCHMARK_OBJECT_MOTION_SPEED"] =
    "0.9"
$defaultSceneDlaaObjectOnlyNativeEnvironment["SE_BENCHMARK_OBJECT_MOTION_RADIUS"] =
    "0.42"
$defaultSceneDlaaObjectOnlyNativeEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneDlaaPresetKObjectOnlyPresentEnvironment =
    $defaultSceneDlaaPresentEnvironment.Clone()
$defaultSceneDlaaPresetKObjectOnlyPresentEnvironment["SE_DLSS_PRESET"] = "k"
$defaultSceneDlaaPresetKObjectOnlyPresentEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneDlaaPresetKObjectOnlyPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION"] =
    "orbit"
$defaultSceneDlaaPresetKObjectOnlyPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION_SPEED"] =
    "0.9"
$defaultSceneDlaaPresetKObjectOnlyPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION_RADIUS"] =
    "0.42"
$defaultSceneDlaaPresetMObjectOnlyPresentEnvironment =
    $defaultSceneDlaaPresentEnvironment.Clone()
$defaultSceneDlaaPresetMObjectOnlyPresentEnvironment["SE_DLSS_PRESET"] = "m"
$defaultSceneDlaaPresetMObjectOnlyPresentEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneDlaaPresetMObjectOnlyPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION"] =
    "orbit"
$defaultSceneDlaaPresetMObjectOnlyPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION_SPEED"] =
    "0.9"
$defaultSceneDlaaPresetMObjectOnlyPresentEnvironment["SE_BENCHMARK_OBJECT_MOTION_RADIUS"] =
    "0.42"
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
$defaultSceneDlaaCombinedMotionNativeEnvironment =
    $defaultSceneDlaaMotionNativeEnvironment.Clone()
$defaultSceneDlaaCombinedMotionNativeEnvironment["SE_BENCHMARK_OBJECT_MOTION"] =
    "orbit"
$defaultSceneDlaaCombinedMotionNativeEnvironment["SE_BENCHMARK_OBJECT_MOTION_SPEED"] =
    "0.9"
$defaultSceneDlaaCombinedMotionNativeEnvironment["SE_BENCHMARK_OBJECT_MOTION_RADIUS"] =
    "0.42"
$defaultSceneDlaaPresetKCombinedMotionPresentEnvironment =
    $defaultSceneDlaaObjectMotionPresentEnvironment.Clone()
$defaultSceneDlaaPresetKCombinedMotionPresentEnvironment["SE_DLSS_PRESET"] = "k"
$defaultSceneDlaaPresetKCombinedMotionPresentEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneDlaaPresetMCombinedMotionPresentEnvironment =
    $defaultSceneDlaaObjectMotionPresentEnvironment.Clone()
$defaultSceneDlaaPresetMCombinedMotionPresentEnvironment["SE_DLSS_PRESET"] = "m"
$defaultSceneDlaaPresetMCombinedMotionPresentEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneSkinnedFbxDynamicMovingCameraNativeEnvironment =
    $defaultSceneDlaaMotionNativeEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingCameraNativeEnvironment["SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingCameraPresetKEnvironment =
    $defaultSceneDlaaPresetKMotionPresentEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingCameraPresetKEnvironment["SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingCameraPresetKEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $defaultSceneSkinnedFbxMProductionBaselineManifestPath
$defaultSceneSkinnedFbxDynamicMovingCameraPresetMEnvironment =
    $defaultSceneDlaaPresetMMotionPresentEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingCameraPresetMEnvironment["SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingCameraPresetMEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $defaultSceneSkinnedFbxMProductionBaselineManifestPath
$defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionNativeEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingCameraNativeEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionNativeEnvironment
$defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionPresetKEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingCameraPresetKEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionPresetKEnvironment
$defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionPresetMEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingCameraPresetMEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionPresetMEnvironment
$defaultSceneSkinnedFbxDynamicMovingCameraVelocityDebugEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingCameraNativeEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingCameraVelocityDebugEnvironment["SE_RENDER_VIEW"] =
    "gbuffer-velocity"
$defaultSceneSkinnedFbxDynamicMovingCameraVelocityDebugEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingCameraDepthDebugEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingCameraNativeEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingCameraDepthDebugEnvironment["SE_RENDER_VIEW"] =
    "gbuffer-depth"
$defaultSceneSkinnedFbxDynamicMovingCameraDepthDebugEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionVelocityDebugEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingCameraVelocityDebugEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionVelocityDebugEnvironment
$defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionDepthDebugEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingCameraDepthDebugEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionDepthDebugEnvironment
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyNativeEnvironment =
    $defaultSceneDlaaObjectOnlyNativeEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyNativeEnvironment["SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetKEnvironment =
    $defaultSceneDlaaPresetKObjectOnlyPresentEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetKEnvironment["SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetKEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $defaultSceneSkinnedFbxMProductionBaselineManifestPath
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMEnvironment =
    $defaultSceneDlaaPresetMObjectOnlyPresentEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMEnvironment["SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $defaultSceneSkinnedFbxMProductionBaselineManifestPath
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMSharpnessZeroEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMSharpnessZeroEnvironment["SE_DLSS_SHARPNESS"] =
    "0.0"
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyVelocityDebugEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingObjectOnlyNativeEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyVelocityDebugEnvironment["SE_RENDER_VIEW"] =
    "gbuffer-velocity"
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyVelocityDebugEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyDepthDebugEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingObjectOnlyNativeEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyDepthDebugEnvironment["SE_RENDER_VIEW"] =
    "gbuffer-depth"
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyDepthDebugEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionVelocityDebugEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingObjectOnlyVelocityDebugEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionVelocityDebugEnvironment
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionDepthDebugEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingObjectOnlyDepthDebugEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionDepthDebugEnvironment
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetKEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetKEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetKEnvironment
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMEnvironment
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMMvJitteredEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMMvJitteredEnvironment["SE_DLSS_CREATE_FLAG_MV_JITTERED"] =
    "1"
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMMvJitteredJitteredHistoryEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMMvJitteredEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMMvJitteredJitteredHistoryEnvironment["SE_TEMPORAL_VELOCITY_JITTER_POLICY"] =
    "jittered"
$defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMSharpnessZeroEnvironment =
    $defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMSharpnessZeroEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMSharpnessZeroEnvironment
$defaultSceneSkinnedFbxDynamicCombinedNativeEnvironment =
    $defaultSceneDlaaCombinedMotionNativeEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicCombinedNativeEnvironment["SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION"] =
    "1"
$defaultSceneSkinnedFbxDynamicCombinedPresetKEnvironment =
    $defaultSceneDlaaPresetKCombinedMotionPresentEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicCombinedPresetKEnvironment["SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION"] =
    "1"
$defaultSceneSkinnedFbxDynamicCombinedPresetKEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $defaultSceneSkinnedFbxMProductionBaselineManifestPath
$defaultSceneSkinnedFbxDynamicCombinedPresetMEnvironment =
    $defaultSceneDlaaPresetMCombinedMotionPresentEnvironment.Clone()
$defaultSceneSkinnedFbxDynamicCombinedPresetMEnvironment["SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION"] =
    "1"
$defaultSceneSkinnedFbxDynamicCombinedPresetMEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $defaultSceneSkinnedFbxMProductionBaselineManifestPath
$defaultSceneSkinnedFbxDynamicCombinedHighResolutionNativeEnvironment =
    $defaultSceneSkinnedFbxDynamicCombinedNativeEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicCombinedHighResolutionNativeEnvironment
$defaultSceneSkinnedFbxDynamicCombinedHighResolutionPresetKEnvironment =
    $defaultSceneSkinnedFbxDynamicCombinedPresetKEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicCombinedHighResolutionPresetKEnvironment
$defaultSceneSkinnedFbxDynamicCombinedHighResolutionPresetMEnvironment =
    $defaultSceneSkinnedFbxDynamicCombinedPresetMEnvironment.Clone()
Set-HighResolutionVisualQaEnvironment `
    -Environment $defaultSceneSkinnedFbxDynamicCombinedHighResolutionPresetMEnvironment
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
$importedSkinnedPreviewPresentEnvironment =
    $importedDynamicDlaaObjectMotionPresentEnvironment.Clone()
$importedSkinnedPreviewPresentEnvironment.Remove("SE_DLSS_REFERENCE_BASELINE_PATH")
$importedSkinnedPreviewPresentEnvironment["SELFENGINE_MODEL_PATH"] =
    $importedSkinnedPreviewModelPath
$importedSkinnedPreviewPresentEnvironment["SE_ENABLE_IMPORTED_SKINNING_PREVIEW"] =
    "1"
$importedSkinnedPreviewPresentEnvironment["SE_DLSS_PRESET"] = "m"
if (-not $importedSkinnedPreviewPresentEnvironment.ContainsKey("SE_BENCHMARK_OBJECT_MOTION") -or
    $importedSkinnedPreviewPresentEnvironment.ContainsKey("SE_BENCHMARK_CAMERA_MOTION")) {
    throw "Imported skinned preview DLAA lane must be object-motion driven with a static camera"
}
$skinnedFbxMProductionPresentEnvironment =
    $importedSkinnedPreviewPresentEnvironment.Clone()
$skinnedFbxMProductionPresentEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $importedSkinnedPreviewBaselineManifestPath
if (-not $skinnedFbxMProductionPresentEnvironment.ContainsKey("SE_BENCHMARK_OBJECT_MOTION") -or
    $skinnedFbxMProductionPresentEnvironment.ContainsKey("SE_BENCHMARK_CAMERA_MOTION")) {
    throw "Skinned FBX preset-M production lane must be object-motion driven with a static camera"
}
$defaultSceneSkinnedFbxMProductionPresentEnvironment =
    $defaultSceneDlaaObjectMotionPresentEnvironment.Clone()
$defaultSceneSkinnedFbxMProductionPresentEnvironment["SE_DLSS_PRESET"] = "m"
$defaultSceneSkinnedFbxMProductionPresentEnvironment["SE_VISUAL_QA_HIDE_IMGUI"] = "1"
$defaultSceneSkinnedFbxMProductionPresentEnvironment["SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION"] =
    "1"
$defaultSceneSkinnedFbxMProductionPresentEnvironment["SE_DLSS_REFERENCE_BASELINE_PATH"] =
    $defaultSceneSkinnedFbxMProductionBaselineManifestPath
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
        materialTextureMipLodBias = "$($Row.frame_material_texture_mip_lod_bias)"
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
        velocityJitteredHistoryPolicy =
            "$($Row.temporal_velocity_jittered_history_policy)"
        velocityPreviousJitterApplied =
            "$($Row.temporal_velocity_previous_jitter_applied)"
        previousJitterPixels =
            "$($Row.temporal_previous_jitter_pixels_x)/$($Row.temporal_previous_jitter_pixels_y)"
        previousJitterUv =
            "$($Row.temporal_previous_jitter_uv_x)/$($Row.temporal_previous_jitter_uv_y)"
        dlssMotionVectorScale = "$($Row.temporal_upscaler_dlss_motion_vector_scale_x)/$($Row.temporal_upscaler_dlss_motion_vector_scale_y)"
        dlssMotionVectorScalePixelSpace = "$($Row.temporal_upscaler_dlss_motion_vector_scale_pixel_space)"
        dlssMotionVectorScaleUnitSpace = "$($Row.temporal_upscaler_dlss_motion_vector_scale_unit_space)"
        dlssMotionVectorScaleMatchesRenderExtent = "$($Row.temporal_upscaler_dlss_motion_vector_scale_matches_render_extent)"
        dlssCreateFlags = "$($Row.temporal_upscaler_dlss_create_flags)"
        dlssCreateFlagBits = "hdr/mvLowRes/mvJittered/depthInverted/autoExposure=$($Row.temporal_upscaler_dlss_create_flag_is_hdr)/$($Row.temporal_upscaler_dlss_create_flag_mv_low_res)/$($Row.temporal_upscaler_dlss_create_flag_mv_jittered)/$($Row.temporal_upscaler_dlss_create_flag_depth_inverted)/$($Row.temporal_upscaler_dlss_create_flag_auto_exposure)"
        dlssInputFormats = "color/depth/mv=$($Row.temporal_upscaler_dlss_input_color_format)/$($Row.temporal_upscaler_dlss_input_depth_format)/$($Row.temporal_upscaler_dlss_input_motion_vector_format)"
        dlssInputExtents = "color/depth/mv=$($Row.temporal_upscaler_dlss_input_color_width)x$($Row.temporal_upscaler_dlss_input_color_height)/$($Row.temporal_upscaler_dlss_input_depth_width)x$($Row.temporal_upscaler_dlss_input_depth_height)/$($Row.temporal_upscaler_dlss_input_motion_vector_width)x$($Row.temporal_upscaler_dlss_input_motion_vector_height)"
        dlssInputAspectMasks = "depth/mv=$($Row.temporal_upscaler_dlss_input_depth_aspect_mask)/$($Row.temporal_upscaler_dlss_input_motion_vector_aspect_mask)"
        dlssInputExtentMatches = "depth/mv=$($Row.temporal_upscaler_dlss_input_depth_matches_render_extent)/$($Row.temporal_upscaler_dlss_input_motion_vector_matches_render_extent)"
        qualityMode = "$($Row.temporal_upscaler_dlss_quality_mode)"
        recommendedPreset = "$($Row.temporal_upscaler_dlss_recommended_preset)"
        runtimeFlavor = "$($Row.temporal_upscaler_runtime_flavor)"
        runtimePathOverridden = "$($Row.temporal_upscaler_runtime_path_overridden)"
        runtimePathFound = "$($Row.temporal_upscaler_runtime_path_found)"
        runtimePath = "$($Row.temporal_upscaler_runtime_path)"
        runtimeDllFound = "$($Row.temporal_upscaler_runtime_dll_found)"
        runtimeDllSizeBytes = "$($Row.temporal_upscaler_runtime_dll_size_bytes)"
        runtimeDllHash = "$($Row.temporal_upscaler_runtime_dll_hash)"
        upscalerSharpness = "$($Row.temporal_upscaler_sharpness)"
        evaluateSharpness = "$($Row.temporal_upscaler_dlss_evaluate_sharpness)"
        postSharpening = "$($Row.sharpening_enabled)/$($Row.sharpening_strength)/$($Row.sharpening_radius_pixels)"
        cameraMotionReady = "$($Row.temporal_upscaler_dlss_quality_camera_motion_ready)"
        velocityObjectMotionReady = "$($Row.temporal_velocity_object_motion_ready)"
        objectMotionReady = "$($Row.temporal_upscaler_dlss_quality_object_motion_ready)"
        sceneContentMotionSupported = "$($Row.temporal_upscaler_dlss_quality_scene_content_motion_supported)"
        benchmarkCameraMotionTimeSeconds = "$($Row.benchmark_camera_motion_time_seconds)"
        benchmarkObjectMotionTimeSeconds = "$($Row.benchmark_object_motion_time_seconds)"
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
        materialTextureMipLodBias = "$($Row.frame_material_texture_mip_lod_bias)"
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
        runtimeImportSkinnedAnimationSpaceReady = "$($Row.runtime_import_skinned_animation_space_ready)"
        runtimeImportSkinnedAnimationSpaceBlockerMask = "$($Row.runtime_import_skinned_animation_space_blocker_mask)"
        runtimeImportSkinnedAnimationRenderableBound = "$($Row.runtime_import_skinned_animation_renderable_bound)"
        runtimeImportSkinnedAnimationSupportReady = "$($Row.runtime_import_skinned_animation_support_ready)"
        runtimeImportSkinnedAnimationSupportBlockerMask = "$($Row.runtime_import_skinned_animation_support_blocker_mask)"
        runtimeImportAnimationDiagnosticPoseOnly = "$($Row.runtime_import_animation_diagnostic_pose_only)"
        runtimeImportAnimationPlaybackReady = "$($Row.runtime_import_animation_playback_ready)"
        runtimeImportAnimationPlaybackCandidateModelCount = "$($Row.runtime_import_animation_playback_candidate_model_count)"
        runtimeImportAnimationPlaybackReadyModelCount = "$($Row.runtime_import_animation_playback_ready_model_count)"
        runtimeImportAnimationPlaybackFrameCount = "$($Row.runtime_import_animation_playback_frame_count)"
        runtimeImportAnimationPlaybackLoopWrapCount =
            "$(Get-JsonMemberValue -Object $Row -Name "runtime_import_animation_playback_loop_wrap_count" -DefaultValue 0)"
        runtimeImportAnimationPlaybackPreviousPoseCollapsedCount =
            "$(Get-JsonMemberValue -Object $Row -Name "runtime_import_animation_playback_previous_pose_collapsed_count" -DefaultValue 0)"
        runtimeImportAnimationPlaybackChangedBonePaletteEntryCount = "$($Row.runtime_import_animation_playback_changed_bone_palette_entry_count)"
        runtimeImportAnimationPlaybackRendererPaletteReady = "$($Row.runtime_import_animation_playback_renderer_palette_ready)"
        runtimeImportAnimationPlaybackGpuUploadReady = "$($Row.runtime_import_animation_playback_gpu_upload_ready)"
        runtimeImportAnimationPlaybackBlockerMask = "$($Row.runtime_import_animation_playback_blocker_mask)"
        runtimeImportAnimationPlaybackClockMode =
            "$(Get-JsonMemberValue -Object $Row -Name "runtime_import_animation_playback_clock_mode" -DefaultValue 0)"
        runtimeImportAnimationPlaybackPreviousTimeTicks =
            "$(Get-JsonMemberValue -Object $Row -Name "runtime_import_animation_playback_previous_time_ticks" -DefaultValue 0)"
        runtimeImportAnimationPlaybackCurrentTimeTicks =
            "$(Get-JsonMemberValue -Object $Row -Name "runtime_import_animation_playback_current_time_ticks" -DefaultValue 0)"
        runtimeImportAnimationPlaybackPreviousAbsoluteSeconds =
            "$(Get-JsonMemberValue -Object $Row -Name "runtime_import_animation_playback_previous_absolute_seconds" -DefaultValue -1)"
        runtimeImportAnimationPlaybackCurrentAbsoluteSeconds =
            "$(Get-JsonMemberValue -Object $Row -Name "runtime_import_animation_playback_current_absolute_seconds" -DefaultValue -1)"
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
        bonePaletteShaderSkinningCommandCount = "$($Row.bone_palette_shader_skinning_command_count)"
        bonePaletteShaderSkinningReadyCommandCount = "$($Row.bone_palette_shader_skinning_ready_command_count)"
        bonePaletteShaderSkinningCurrentPaletteOffset = "$($Row.bone_palette_shader_skinning_current_palette_offset)"
        bonePaletteShaderSkinningCurrentEntryCount = "$($Row.bone_palette_shader_skinning_current_entry_count)"
        bonePaletteShaderSkinningPathReady = "$($Row.bone_palette_shader_skinning_path_ready)"
        bonePaletteShaderVelocityCommandCount = "$($Row.bone_palette_shader_velocity_command_count)"
        bonePaletteShaderVelocityReadyCommandCount = "$($Row.bone_palette_shader_velocity_ready_command_count)"
        bonePaletteShaderVelocityPreviousPaletteOffset = "$($Row.bone_palette_shader_velocity_previous_palette_offset)"
        bonePaletteShaderVelocityPreviousEntryCount = "$($Row.bone_palette_shader_velocity_previous_entry_count)"
        bonePaletteShaderVelocityPathReady = "$($Row.bone_palette_shader_velocity_path_ready)"
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
    $expectedForComparison =
        New-ExpectedWithoutProperties `
            -Expected $Expected `
            -ExcludedProperties @("dlssMotionVectorScale")
    Assert-ExpectedMetricText `
        -Name $Name `
        -Metrics $metrics `
        -Expected $expectedForComparison
    Assert-DlssMotionVectorScalePixelSpace -Name $Name -Row $Row
    Assert-DlssJitterConsistency -Name $Name -Row $Row
    return $metrics
}

function New-ExpectedWithoutProperties {
    param(
        [Parameter(Mandatory = $true)]$Expected,
        [Parameter(Mandatory = $true)][string[]]$ExcludedProperties
    )

    $filtered = [ordered]@{}
    foreach ($property in $Expected.PSObject.Properties) {
        if ($ExcludedProperties -contains $property.Name) {
            continue
        }
        $filtered[$property.Name] = $property.Value
    }
    return [pscustomobject]$filtered
}

function Assert-QuickDlssTuningRow {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][string]$ExpectedPreset,
        [double]$ExpectedEvaluateSharpness = [double]::NaN,
        [double]$ExpectedMaterialTextureMipLodBias = [double]::NaN,
        [switch]$SkipDlssExtentsTextGate
    )

    $excludedExpectedProperties = @("recommendedPreset")
    if ($SkipDlssExtentsTextGate) {
        $excludedExpectedProperties += "dlssExtents"
    }
    $expected =
        New-ExpectedWithoutProperties `
            -Expected $Manifest.expected.dlssPresent `
            -ExcludedProperties $excludedExpectedProperties
    $metrics =
        Assert-QuickDlssPresentRow `
            -Name $Name `
            -Row $Row `
            -Expected $expected
    Assert-BaselineText `
        -Name "$Name.recommendedPreset" `
        -Actual ([string]$Row.temporal_upscaler_dlss_recommended_preset) `
        -Expected $ExpectedPreset

    if (-not [double]::IsNaN($ExpectedEvaluateSharpness)) {
        $evaluateSharpness =
            Convert-InvariantDouble $Row.temporal_upscaler_dlss_evaluate_sharpness
        Assert-BaselineRange `
            -Name "$Name.evaluateSharpness" `
            -Actual $evaluateSharpness `
            -Min ($ExpectedEvaluateSharpness - 0.0001) `
            -Max ($ExpectedEvaluateSharpness + 0.0001)
    }
    if (-not [double]::IsNaN($ExpectedMaterialTextureMipLodBias)) {
        $mipLodBias =
            Convert-InvariantDouble $Row.frame_material_texture_mip_lod_bias
        Assert-BaselineRange `
            -Name "$Name.materialTextureMipLodBias" `
            -Actual $mipLodBias `
            -Min ($ExpectedMaterialTextureMipLodBias - 0.0001) `
            -Max ($ExpectedMaterialTextureMipLodBias + 0.0001)
    }

    return $metrics
}

function Assert-DlssHighResolutionExtents {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][int]$ExpectedWidth,
        [Parameter(Mandatory = $true)][int]$ExpectedHeight
    )

    $renderWidth = [int]$Row.temporal_upscaler_dlss_render_width
    $renderHeight = [int]$Row.temporal_upscaler_dlss_render_height
    $outputWidth = [int]$Row.temporal_upscaler_dlss_output_width
    $outputHeight = [int]$Row.temporal_upscaler_dlss_output_height
    if ($renderWidth -lt $ExpectedWidth -or
        $renderHeight -lt $ExpectedHeight -or
        $outputWidth -lt $ExpectedWidth -or
        $outputHeight -lt $ExpectedHeight) {
        throw (
            "{0} did not evaluate at high resolution: render={1}x{2} output={3}x{4} expectedAtLeast={5}x{6}" -f
            $Name,
            $renderWidth,
            $renderHeight,
            $outputWidth,
            $outputHeight,
            $ExpectedWidth,
            $ExpectedHeight
        )
    }
}

function Assert-QuickDlssBlockedPreviewRow {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)]$Expected
    )

    if ($Row.framegraph_validation_issues -ne "0") {
        throw "$Name frame graph validation issues: $($Row.framegraph_validation_issues)"
    }
    if ($Row.temporal_upscaler_dlss_quality_gate_ready -ne "0") {
        throw "$Name unexpectedly passed the DLSS quality gate"
    }
    if ($Row.temporal_upscaler_dlss_quality_blocker_mask -eq "0") {
        throw "$Name did not report DLSS quality blockers"
    }

    $metrics = New-QuickDlssPresentMetrics -Row $Row
    $expectedForComparison =
        New-ExpectedWithoutProperties `
            -Expected $Expected `
            -ExcludedProperties @("dlssMotionVectorScale")
    Assert-ExpectedMetricText `
        -Name $Name `
        -Metrics $metrics `
        -Expected $expectedForComparison
    Assert-DlssMotionVectorScalePixelSpace -Name $Name -Row $Row
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

function Assert-QuickEdgeComparison {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$EdgeComparison,
        [Parameter(Mandatory = $true)]$Manifest
    )

    Assert-BaselineRange `
        -Name "$Name.edgeComparison.edgePixels" `
        -Actual $EdgeComparison.EdgePixels `
        -Min $Manifest.thresholds.comparisonEdgePixelsMin `
        -Max $EdgeComparison.EdgePixels
    Assert-BaselineRange `
        -Name "$Name.edgeComparison.changedEdgePixels" `
        -Actual $EdgeComparison.ChangedEdgePixels `
        -Min 0 `
        -Max $Manifest.thresholds.comparisonChangedEdgePixelsMax
    Assert-BaselineRange `
        -Name "$Name.edgeComparison.changedEdgeRatio" `
        -Actual $EdgeComparison.ChangedEdgeRatio `
        -Min 0 `
        -Max $Manifest.thresholds.comparisonChangedEdgeRatioMax
    Assert-BaselineRange `
        -Name "$Name.edgeComparison.meanEdgeDelta" `
        -Actual $EdgeComparison.MeanEdgeDelta `
        -Min 0 `
        -Max $Manifest.thresholds.comparisonMeanEdgeDeltaMax
    Assert-BaselineRange `
        -Name "$Name.edgeComparison.maxEdgeDelta" `
        -Actual $EdgeComparison.MaxEdgeDelta `
        -Min 0 `
        -Max $Manifest.thresholds.comparisonMaxEdgeDeltaMax
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

    $thresholds = [ordered]@{
        centralDifferentPixelsMin = [int]$Manifest.thresholds.centralDifferentPixelsMin
        comparisonChangedPixelsMin = [int]$Manifest.thresholds.comparisonChangedPixelsMin
        comparisonChangedPixelsMax = [int]$Manifest.thresholds.comparisonChangedPixelsMax
        comparisonMeanDeltaMin = [double]$Manifest.thresholds.comparisonMeanDeltaMin
        comparisonMeanDeltaMax = [double]$Manifest.thresholds.comparisonMeanDeltaMax
        comparisonMaxDeltaMax = [int]$Manifest.thresholds.comparisonMaxDeltaMax
    }
    if ($null -ne $Manifest.thresholds.PSObject.Properties["comparisonEdgePixelsMin"]) {
        $thresholds["comparisonEdgePixelsMin"] =
            [int]$Manifest.thresholds.comparisonEdgePixelsMin
        $thresholds["comparisonChangedEdgePixelsMax"] =
            [int]$Manifest.thresholds.comparisonChangedEdgePixelsMax
        $thresholds["comparisonChangedEdgeRatioMax"] =
            [double]$Manifest.thresholds.comparisonChangedEdgeRatioMax
        $thresholds["comparisonMeanEdgeDeltaMax"] =
            [double]$Manifest.thresholds.comparisonMeanEdgeDeltaMax
        $thresholds["comparisonMaxEdgeDeltaMax"] =
            [double]$Manifest.thresholds.comparisonMaxEdgeDeltaMax
    }

    return $thresholds
}

function New-QuickLaneSummary {
    param(
        [Parameter(Mandatory = $true)]$Benchmark,
        [Parameter(Mandatory = $true)]$Metrics,
        [string[]]$Images = @(),
        [object[]]$ImageStats = @(),
        $Comparison = $null,
        $EdgeComparison = $null,
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
    if ($null -ne $EdgeComparison) {
        $lane["edgeComparison"] = $EdgeComparison
    }
    if ($null -ne $SequenceComparison) {
        $lane["sequenceComparison"] = $SequenceComparison
    }
    if ($Model.Length -gt 0) {
        $lane["model"] = $Model
    }

    return $lane
}

function Invoke-QuickDlaaTuningLane {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][string]$ExpectedPreset,
        [Parameter(Mandatory = $true)][string]$NativeImage,
        [double]$ExpectedEvaluateSharpness = [double]::NaN,
        [double]$ExpectedMaterialTextureMipLodBias = [double]::NaN,
        [string[]]$AllowedCaptureDiagnosticPatterns = @(),
        [switch]$UseMonitorResolutionCapture,
        [switch]$SkipComparisonGate,
        [int]$ExpectedDlssWidth = 0,
        [int]$ExpectedDlssHeight = 0
    )

    $benchmark = Invoke-BenchmarkRun `
        -Name $Name `
        -Environment $Environment `
        -UseApplicationScene
    $metrics =
        Assert-QuickDlssTuningRow `
            -Name $LaneKey `
            -Row $benchmark.LastRow `
            -Manifest $Manifest `
            -ExpectedPreset $ExpectedPreset `
            -ExpectedEvaluateSharpness $ExpectedEvaluateSharpness `
            -ExpectedMaterialTextureMipLodBias $ExpectedMaterialTextureMipLodBias `
            -SkipDlssExtentsTextGate:($ExpectedDlssWidth -gt 0 -and $ExpectedDlssHeight -gt 0)
    if ($ExpectedDlssWidth -gt 0 -and $ExpectedDlssHeight -gt 0) {
        Assert-DlssHighResolutionExtents `
            -Name $LaneKey `
            -Row $benchmark.LastRow `
            -ExpectedWidth $ExpectedDlssWidth `
            -ExpectedHeight $ExpectedDlssHeight
    }
    $image = Capture-WindowImage `
        -Name $Name `
        -Environment $Environment `
        -AllowedDiagnosticPatterns $AllowedCaptureDiagnosticPatterns `
        -UseMonitorResolution:$UseMonitorResolutionCapture
    $imageStats = Get-ImageVariationStats -Path $image
    $comparison = Compare-Images -A $NativeImage -B $image
    $edgeComparison = Compare-ImageEdges -A $NativeImage -B $image

    Assert-QuickImageStats `
        -Name $LaneKey `
        -Stats $imageStats `
        -Manifest $Manifest
    if (-not $SkipComparisonGate) {
        Assert-QuickComparison `
            -Name $LaneKey `
            -Comparison $comparison `
            -Manifest $Manifest
        Assert-QuickEdgeComparison `
            -Name $LaneKey `
            -EdgeComparison $edgeComparison `
            -Manifest $Manifest
    }

    return New-QuickLaneSummary `
        -Benchmark $benchmark `
        -Metrics $metrics `
        -Images @($image) `
        -ImageStats @($imageStats) `
        -Comparison $comparison `
        -EdgeComparison $edgeComparison
}

function Invoke-QuickDlssMaskDiagnosticLane {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)]$Manifest,
        [string[]]$AllowedCaptureDiagnosticPatterns = @(),
        [switch]$UseMonitorResolutionCapture,
        [switch]$UseSequenceCapture,
        [int]$ExpectedDlssWidth = 0,
        [int]$ExpectedDlssHeight = 0
    )

    $benchmark = Invoke-BenchmarkRun `
        -Name $Name `
        -Environment $Environment `
        -UseApplicationScene
    if ($benchmark.LastRow.framegraph_validation_issues -ne "0") {
        throw "$LaneKey frame graph validation issues: $($benchmark.LastRow.framegraph_validation_issues)"
    }
    if ($benchmark.LastRow.temporal_upscaler_dlss_output_ready -ne "1") {
        throw "$LaneKey did not produce DLSS output"
    }
    if ($ExpectedDlssWidth -gt 0 -and $ExpectedDlssHeight -gt 0) {
        Assert-DlssHighResolutionExtents `
            -Name $LaneKey `
            -Row $benchmark.LastRow `
            -ExpectedWidth $ExpectedDlssWidth `
            -ExpectedHeight $ExpectedDlssHeight
    }

    $metrics = New-QuickDlssPresentMetrics -Row $benchmark.LastRow
    $images = @()
    if ($UseSequenceCapture) {
        $images = Capture-WindowImageSequence `
            -Name $Name `
            -Environment $Environment `
            -FrameCount $SequenceFrameCount `
            -InitialDelaySeconds $SequenceInitialDelaySeconds `
            -IntervalSeconds $SequenceIntervalSeconds `
            -AllowedDiagnosticPatterns $AllowedCaptureDiagnosticPatterns `
            -UseMonitorResolution:$UseMonitorResolutionCapture
    } else {
        $images = @(
            Capture-WindowImage `
                -Name $Name `
                -Environment $Environment `
                -AllowedDiagnosticPatterns $AllowedCaptureDiagnosticPatterns `
                -UseMonitorResolution:$UseMonitorResolutionCapture
        )
    }
    $imageStats = @()
    foreach ($image in $images) {
        $imageStats += Get-ImageVariationStats -Path $image
    }
    Assert-QuickImageStats `
        -Name $LaneKey `
        -Stats $imageStats `
        -Manifest $Manifest

    $logStem = if ($UseSequenceCapture) { "sequence" } else { "capture" }
    $captureOutLog = Join-Path $outputRoot "$Name.$logStem.out.log"
    $captureErrLog = Join-Path $outputRoot "$Name.$logStem.err.log"
    $hasNgxResourceDiagnostic =
        (Test-LogContainsPattern `
            -Path $captureOutLog `
            -Pattern "nv\.ngx\.dlss\.") -or
        (Test-LogContainsPattern `
            -Path $captureErrLog `
            -Pattern "nv\.ngx\.dlss\.")
    $hasValidationDiagnostic =
        (Test-LogContainsPattern `
            -Path $captureOutLog `
            -Pattern "VUID|validation|error|failed|exception|shader") -or
        (Test-LogContainsPattern `
            -Path $captureErrLog `
            -Pattern "VUID|validation|error|failed|exception|shader")

    $lane = New-QuickLaneSummary `
        -Benchmark $benchmark `
        -Metrics $metrics `
        -Images $images `
        -ImageStats @($imageStats)
    if ($UseSequenceCapture) {
        $lane["sequenceComparison"] = Compare-ImageSequence -Paths $images
    }
    $lane["captureOutLog"] = $captureOutLog
    $lane["captureErrLog"] = $captureErrLog
    $lane["ngxResourceLayoutDiagnostic"] = $hasNgxResourceDiagnostic
    $lane["validationClean"] = -not $hasValidationDiagnostic
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
        [string[]]$AllowedCaptureDiagnosticPatterns = @(),
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
        -FrameCount $SequenceFrameCount `
        -InitialDelaySeconds $SequenceInitialDelaySeconds `
        -IntervalSeconds $SequenceIntervalSeconds `
        -AllowedDiagnosticPatterns $AllowedCaptureDiagnosticPatterns
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

function Assert-QuickNativeDiagnosticRow {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Row
    )

    if ($Row.framegraph_validation_issues -ne "0") {
        throw "$Name frame graph validation issues: $($Row.framegraph_validation_issues)"
    }
    if ($Row.temporal_upscale_post_source_active -ne "0") {
        throw "$Name unexpectedly activated temporal-upscale post source"
    }

    return New-QuickNativeMetrics -Row $Row
}

function Assert-QuickNativeDebugDiagnosticRow {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Row
    )

    $issues = [int]$Row.framegraph_validation_issues
    $unusedPhysicalResources =
        [int]$Row.framegraph_validation_unused_physical_resources
    if ($issues -ne $unusedPhysicalResources) {
        throw "$Name frame graph validation issues are not limited to unused debug-view resources: issues=$issues unusedPhysical=$unusedPhysicalResources"
    }
    if ($Row.temporal_upscale_post_source_active -ne "0") {
        throw "$Name unexpectedly activated temporal-upscale post source"
    }

    return New-QuickNativeMetrics -Row $Row
}

function Invoke-QuickNativeSequenceSuite {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)]$Manifest,
        [string[]]$AllowedCaptureDiagnosticPatterns = @(),
        [switch]$UseMonitorResolutionCapture
    )

    $benchmark = Invoke-BenchmarkRun `
        -Name $Name `
        -Environment $Environment `
        -UseApplicationScene
    $metrics = Assert-QuickNativeDiagnosticRow `
        -Name $LaneKey `
        -Row $benchmark.LastRow
    $images = Capture-WindowImageSequence `
        -Name $Name `
        -Environment $Environment `
        -FrameCount $SequenceFrameCount `
        -InitialDelaySeconds $SequenceInitialDelaySeconds `
        -IntervalSeconds $SequenceIntervalSeconds `
        -AllowedDiagnosticPatterns $AllowedCaptureDiagnosticPatterns `
        -UseMonitorResolution:$UseMonitorResolutionCapture
    $imageStats = @()
    foreach ($image in $images) {
        $imageStats += Get-ImageVariationStats -Path $image
    }
    $sequenceComparison =
        Compare-ImageSequence `
            -Paths $images `
            -GradientThreshold 4.0 `
            -DeltaThreshold 2.0

    Assert-QuickImageStats -Name $LaneKey -Stats $imageStats -Manifest $Manifest

    return New-QuickLaneSummary `
        -Benchmark $benchmark `
        -Metrics $metrics `
        -Images $images `
        -ImageStats $imageStats `
        -SequenceComparison $sequenceComparison
}

function Invoke-QuickNativeDiagnosticSequenceSuite {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [string[]]$AllowedCaptureDiagnosticPatterns = @(),
        [switch]$UseMonitorResolutionCapture
    )

    $benchmark = Invoke-BenchmarkRun `
        -Name $Name `
        -Environment $Environment `
        -UseApplicationScene
    $metrics = Assert-QuickNativeDebugDiagnosticRow `
        -Name $LaneKey `
        -Row $benchmark.LastRow
    $images = Capture-WindowImageSequence `
        -Name $Name `
        -Environment $Environment `
        -FrameCount $SequenceFrameCount `
        -InitialDelaySeconds $SequenceInitialDelaySeconds `
        -IntervalSeconds $SequenceIntervalSeconds `
        -AllowedDiagnosticPatterns $AllowedCaptureDiagnosticPatterns `
        -UseMonitorResolution:$UseMonitorResolutionCapture
    $imageStats = @()
    foreach ($image in $images) {
        $imageStats += Get-DebugImageVariationStats -Path $image
    }
    $sequenceComparison =
        Compare-ImageSequence `
            -Paths $images `
            -GradientThreshold 4.0 `
            -DeltaThreshold 2.0

    return New-QuickLaneSummary `
        -Benchmark $benchmark `
        -Metrics $metrics `
        -Images $images `
        -ImageStats $imageStats `
        -SequenceComparison $sequenceComparison
}

function Invoke-QuickDlaaTuningSequenceSuite {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][string]$ExpectedPreset,
        [string[]]$AllowedCaptureDiagnosticPatterns = @(),
        [switch]$SkipSequenceGate,
        [double]$ExpectedEvaluateSharpness = [double]::NaN,
        [double]$ExpectedMaterialTextureMipLodBias = [double]::NaN,
        [switch]$UseMonitorResolutionCapture,
        [int]$ExpectedDlssWidth = 0,
        [int]$ExpectedDlssHeight = 0
    )

    $benchmark = Invoke-BenchmarkRun `
        -Name $Name `
        -Environment $Environment `
        -UseApplicationScene
    $metrics =
        Assert-QuickDlssTuningRow `
            -Name $LaneKey `
            -Row $benchmark.LastRow `
            -Manifest $Manifest `
            -ExpectedPreset $ExpectedPreset `
            -ExpectedEvaluateSharpness $ExpectedEvaluateSharpness `
            -ExpectedMaterialTextureMipLodBias $ExpectedMaterialTextureMipLodBias `
            -SkipDlssExtentsTextGate:($ExpectedDlssWidth -gt 0 -and $ExpectedDlssHeight -gt 0)
    if ($ExpectedDlssWidth -gt 0 -and $ExpectedDlssHeight -gt 0) {
        Assert-DlssHighResolutionExtents `
            -Name $LaneKey `
            -Row $benchmark.LastRow `
            -ExpectedWidth $ExpectedDlssWidth `
            -ExpectedHeight $ExpectedDlssHeight
    }
    $images = Capture-WindowImageSequence `
        -Name $Name `
        -Environment $Environment `
        -FrameCount $SequenceFrameCount `
        -InitialDelaySeconds $SequenceInitialDelaySeconds `
        -IntervalSeconds $SequenceIntervalSeconds `
        -AllowedDiagnosticPatterns $AllowedCaptureDiagnosticPatterns `
        -UseMonitorResolution:$UseMonitorResolutionCapture
    $imageStats = @()
    foreach ($image in $images) {
        $imageStats += Get-ImageVariationStats -Path $image
    }
    $sequenceComparison = Compare-ImageSequence -Paths $images

    Assert-QuickImageStats -Name $LaneKey -Stats $imageStats -Manifest $Manifest
    if (-not $SkipSequenceGate) {
        Assert-QuickSequenceComparison `
            -Name $LaneKey `
            -Comparison $sequenceComparison `
            -Manifest $Manifest
    }

    $captureOutLog = Join-Path $outputRoot "$Name.sequence.out.log"
    $captureErrLog = Join-Path $outputRoot "$Name.sequence.err.log"
    $hasNgxResourceDiagnostic =
        (Test-LogContainsPattern `
            -Path $captureOutLog `
            -Pattern "nv\.ngx\.dlss\.") -or
        (Test-LogContainsPattern `
            -Path $captureErrLog `
            -Pattern "nv\.ngx\.dlss\.")
    $hasValidationDiagnostic =
        (Test-LogContainsPattern `
            -Path $captureOutLog `
            -Pattern "VUID|validation|error|failed|exception|shader") -or
        (Test-LogContainsPattern `
            -Path $captureErrLog `
            -Pattern "VUID|validation|error|failed|exception|shader")

    $lane = New-QuickLaneSummary `
        -Benchmark $benchmark `
        -Metrics $metrics `
        -Images $images `
        -ImageStats $imageStats `
        -SequenceComparison $sequenceComparison
    $lane["captureOutLog"] = $captureOutLog
    $lane["captureErrLog"] = $captureErrLog
    $lane["ngxResourceLayoutDiagnostic"] = $hasNgxResourceDiagnostic
    $lane["validationClean"] = -not $hasValidationDiagnostic
    return $lane
}

function Invoke-QuickDlssBlockedPreviewSuite {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)]$Manifest,
        [string[]]$AllowedCaptureDiagnosticPatterns = @(),
        [string]$Model = "",
        [switch]$SkipImageStatsGate
    )

    $benchmark = Invoke-BenchmarkRun `
        -Name $Name `
        -Environment $Environment `
        -UseApplicationScene
    $metrics = Assert-QuickDlssBlockedPreviewRow `
        -Name $LaneKey `
        -Row $benchmark.LastRow `
        -Expected $Manifest.expected.dlssPresent
    $image = Capture-WindowImage `
        -Name $Name `
        -Environment $Environment `
        -AllowedDiagnosticPatterns $AllowedCaptureDiagnosticPatterns
    $imageStats = Get-ImageVariationStats -Path $image

    if (-not $SkipImageStatsGate) {
        Assert-QuickImageStats `
            -Name $LaneKey `
            -Stats @($imageStats) `
            -Manifest $Manifest
    }

    return New-QuickLaneSummary `
        -Benchmark $benchmark `
        -Metrics $metrics `
        -Images @($image) `
        -ImageStats @($imageStats) `
        -Model $Model
}

function Invoke-QuickDlssBlockedPreviewSequenceSuite {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)]$Manifest,
        [string[]]$AllowedCaptureDiagnosticPatterns = @(),
        [string]$Model = "",
        [switch]$SkipImageStatsGate
    )

    $benchmark = Invoke-BenchmarkRun `
        -Name $Name `
        -Environment $Environment `
        -UseApplicationScene
    $metrics = Assert-QuickDlssBlockedPreviewRow `
        -Name $LaneKey `
        -Row $benchmark.LastRow `
        -Expected $Manifest.expected.dlssPresent
    $images = Capture-WindowImageSequence `
        -Name $Name `
        -Environment $Environment `
        -FrameCount $SequenceFrameCount `
        -InitialDelaySeconds $SequenceInitialDelaySeconds `
        -IntervalSeconds $SequenceIntervalSeconds `
        -AllowedDiagnosticPatterns $AllowedCaptureDiagnosticPatterns
    $imageStats = @()
    foreach ($image in $images) {
        $imageStats += Get-ImageVariationStats -Path $image
    }
    $sequenceComparison = Compare-ImageSequence -Paths $images

    if (-not $SkipImageStatsGate) {
        Assert-QuickImageStats `
            -Name $LaneKey `
            -Stats $imageStats `
            -Manifest $Manifest
    }

    return New-QuickLaneSummary `
        -Benchmark $benchmark `
        -Metrics $metrics `
        -Images $images `
        -ImageStats $imageStats `
        -SequenceComparison $sequenceComparison `
        -Model $Model
}

function Invoke-QuickDlssProductionAuditSequenceSuite {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LaneKey,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)]$Manifest,
        [string[]]$AllowedCaptureDiagnosticPatterns = @(),
        [string]$Model = "",
        [switch]$SkipImageStatsGate
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
        -FrameCount $SequenceFrameCount `
        -InitialDelaySeconds $SequenceInitialDelaySeconds `
        -IntervalSeconds $SequenceIntervalSeconds `
        -AllowedDiagnosticPatterns $AllowedCaptureDiagnosticPatterns
    $imageStats = @()
    foreach ($image in $images) {
        $imageStats += Get-ImageVariationStats -Path $image
    }
    $sequenceComparison = Compare-ImageSequence -Paths $images

    if (-not $SkipImageStatsGate) {
        Assert-QuickImageStats `
            -Name $LaneKey `
            -Stats $imageStats `
            -Manifest $Manifest
    }

    return New-QuickLaneSummary `
        -Benchmark $benchmark `
        -Metrics $metrics `
        -Images $images `
        -ImageStats $imageStats `
        -SequenceComparison $sequenceComparison `
        -Model $Model
}

function Get-JsonMemberValue {
    param(
        [object]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        [object]$DefaultValue = $null
    )

    if ($null -eq $Object) {
        return $DefaultValue
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $DefaultValue
    }
    return $property.Value
}

function Update-MHighResDynamicRepeatLedger {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Target,
        [Parameter(Mandatory = $true)][int]$MonitorIndex,
        [Parameter(Mandatory = $true)][string]$MonitorDevice,
        [Parameter(Mandatory = $true)][int]$Width,
        [Parameter(Mandatory = $true)][int]$Height,
        [Parameter(Mandatory = $true)][int]$SequenceFrameCount,
        [Parameter(Mandatory = $true)][int]$SequenceInitialDelaySeconds,
        [Parameter(Mandatory = $true)][int]$SequenceIntervalSeconds,
        [Parameter(Mandatory = $true)][bool]$KnownNgxInternalLayoutIsolation,
        [Parameter(Mandatory = $true)][bool]$ValidationClean,
        [Parameter(Mandatory = $true)][bool]$MVsKDynamicStable,
        [Parameter(Mandatory = $true)][bool]$NativeNormalizedDynamicStable,
        [Parameter(Mandatory = $true)][bool]$TemporalInputContractReady,
        [Parameter(Mandatory = $true)][bool]$QualityInputReady,
        [Parameter(Mandatory = $true)][bool]$SkinnedVelocityInputReady,
        [Parameter(Mandatory = $true)][bool]$ObjectMaskedStabilityReady,
        [string[]]$BlockedReasons = @()
    )

    $requiredConsecutivePasses = 3
    $policyName = if ($KnownNgxInternalLayoutIsolation) {
        "known-ngx-internal-layout-isolation"
    } else {
        "strict-without-ngx-isolation"
    }
    $policyKey =
        "target={0};monitor={1}:{2};resolution={3}x{4};policy={5};frames={6};initialDelay={7};interval={8}" -f
        $Target,
        $MonitorIndex,
        $MonitorDevice,
        $Width,
        $Height,
        $policyName,
        $SequenceFrameCount,
        $SequenceInitialDelaySeconds,
        $SequenceIntervalSeconds

    $previousLedger = $null
    $previousLedgerReadError = ""
    if (Test-Path -LiteralPath $Path) {
        try {
            $previousLedger =
                Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
        } catch {
            $previousLedgerReadError = $_.Exception.Message
        }
    }

    $previousConsecutive =
        Get-JsonMemberValue `
            -Object $previousLedger `
            -Name "consecutive"
    $previousCountRaw =
        Get-JsonMemberValue `
            -Object $previousConsecutive `
            -Name "count" `
            -DefaultValue 0
    try {
        $previousCount = [int]$previousCountRaw
    } catch {
        $previousCount = 0
    }
    $previousPolicyKey =
        [string](Get-JsonMemberValue `
            -Object $previousConsecutive `
            -Name "policyKey" `
            -DefaultValue "")
    $policyCompatible =
        ($previousPolicyKey.Length -eq 0 -or $previousPolicyKey -eq $policyKey)

    $productionInputPass =
        ($ValidationClean -and
         $MVsKDynamicStable -and
         $NativeNormalizedDynamicStable -and
         $TemporalInputContractReady -and
         $QualityInputReady -and
         $SkinnedVelocityInputReady -and
         $ObjectMaskedStabilityReady)
    $strictValidationClean =
        $ValidationClean -and -not $KnownNgxInternalLayoutIsolation
    $strictProductionInputPass =
        $productionInputPass -and $strictValidationClean
    $productionPolicyBlockers = @()
    if ($KnownNgxInternalLayoutIsolation) {
        $productionPolicyBlockers +=
            "Preset M high-resolution dynamic evidence still depends on the explicit NGX internal layout isolation policy"
    }
    $resetReason = ""
    if ($productionInputPass) {
        if ($policyCompatible) {
            $observedConsecutivePasses = $previousCount + 1
        } else {
            $observedConsecutivePasses = 1
            $resetReason = "policy-or-capture-config-changed"
        }
    } else {
        $observedConsecutivePasses = 0
        $resetReason = "latest-run-failed-or-incomplete"
    }
    $ready = ($observedConsecutivePasses -ge $requiredConsecutivePasses)
    $generatedAt = (Get-Date).ToString("o")

    $entry = [ordered]@{
        timestamp = $generatedAt
        suite = "m-highres-dynamic"
        target = $Target
        policyKey = $policyKey
        knownNgxInternalLayoutIsolation =
            $KnownNgxInternalLayoutIsolation
        validationClean = $ValidationClean
        mVsKDynamicStable = $MVsKDynamicStable
        nativeNormalizedDynamicStable = $NativeNormalizedDynamicStable
        temporalInputContractReady = $TemporalInputContractReady
        qualityInputReady = $QualityInputReady
        skinnedVelocityInputReady = $SkinnedVelocityInputReady
        objectMaskedStabilityReady = $ObjectMaskedStabilityReady
        productionInputPass = $productionInputPass
        focusedAuditInputPass = $productionInputPass
        strictValidationClean = $strictValidationClean
        strictProductionInputPass = $strictProductionInputPass
        productionPolicyBlockers = $productionPolicyBlockers
        highResolution = [ordered]@{
            width = $Width
            height = $Height
            monitorIndex = $MonitorIndex
            monitorDevice = $MonitorDevice
            borderless = $true
        }
        sequenceCapture = [ordered]@{
            frameCount = $SequenceFrameCount
            initialDelaySeconds = $SequenceInitialDelaySeconds
            intervalSeconds = $SequenceIntervalSeconds
        }
        blockedReasons = @($BlockedReasons)
        resetReason = $resetReason
    }

    $history = @()
    $previousHistory =
        Get-JsonMemberValue `
            -Object $previousLedger `
            -Name "history" `
            -DefaultValue @()
    foreach ($historyItem in @($previousHistory)) {
        if ($null -ne $historyItem) {
            $history += $historyItem
        }
    }
    $history += [pscustomobject]$entry
    if ($history.Count -gt 24) {
        $history = $history[($history.Count - 24)..($history.Count - 1)]
    }

    $ledger = [ordered]@{
        ledgerVersion = 1
        target = $Target
        updatedAt = $generatedAt
        requiredConsecutivePasses = $requiredConsecutivePasses
        ready = $ready
        consecutive = [ordered]@{
            count = $observedConsecutivePasses
            ready = $ready
            policyName = $policyName
            policyKey = $policyKey
            previousCount = $previousCount
            policyCompatible = $policyCompatible
            resetReason = $resetReason
        }
        lastRun = $entry
        history = $history
    }
    if ($previousLedgerReadError.Length -gt 0) {
        $ledger["previousLedgerReadError"] = $previousLedgerReadError
    }

    $ledger |
        ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath $Path -Encoding UTF8

    return [ordered]@{
        path = $Path
        requiredConsecutivePasses = $requiredConsecutivePasses
        observedConsecutivePasses = $observedConsecutivePasses
        ready = $ready
        productionInputPass = $productionInputPass
        focusedAuditInputPass = $productionInputPass
        strictValidationClean = $strictValidationClean
        strictProductionInputPass = $strictProductionInputPass
        productionPolicyBlockers = $productionPolicyBlockers
        skinnedVelocityInputReady = $SkinnedVelocityInputReady
        policyName = $policyName
        policyKey = $policyKey
        policyCompatible = $policyCompatible
        previousConsecutivePasses = $previousCount
        resetReason = $resetReason
        previousLedgerReadError = $previousLedgerReadError
        lastRun = $entry
    }
}

$highResDynamicRepeatLedgerSelected =
    (-not ($selectedSuites -contains "full") -and
     ($selectedSuites -contains "m-vs-k-moving-camera-fullscreen-pack") -and
     ($selectedSuites -contains "m-vs-k-combined-fullscreen-pack") -and
     ($selectedSuites -contains "m-object-shimmer-fullscreen-diagnostics"))

trap {
    if ($highResDynamicRepeatLedgerSelected) {
        try {
            $repeatLedgerPath =
                Join-Path $outputRoot "m_highres_dynamic_repeat_ledger.json"
            [void](Update-MHighResDynamicRepeatLedger `
                -Path $repeatLedgerPath `
                -Target $Target `
                -MonitorIndex $script:captureMonitorWorkArea.Index `
                -MonitorDevice $script:captureMonitorWorkArea.DeviceName `
                -Width $highResolutionWindowWidth `
                -Height $highResolutionWindowHeight `
                -SequenceFrameCount $SequenceFrameCount `
                -SequenceInitialDelaySeconds $SequenceInitialDelaySeconds `
                -SequenceIntervalSeconds $SequenceIntervalSeconds `
                -KnownNgxInternalLayoutIsolation:$UseKnownNgxInternalLayoutIsolation `
                -ValidationClean $false `
                -MVsKDynamicStable $false `
                -NativeNormalizedDynamicStable $false `
                -TemporalInputContractReady $false `
                -QualityInputReady $false `
                -SkinnedVelocityInputReady $false `
                -ObjectMaskedStabilityReady $false `
                -BlockedReasons @(
                    "High-resolution dynamic focused run aborted before aggregate readiness: $($_.Exception.Message)"
                ))
        } catch {
            Write-Warning "Failed to update high-resolution dynamic repeat ledger after abort: $($_.Exception.Message)"
        }
    }
    break
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
        sequenceCapture = [ordered]@{
            frameCount = $SequenceFrameCount
            initialDelaySeconds = $SequenceInitialDelaySeconds
            intervalSeconds = $SequenceIntervalSeconds
        }
        baselines = [ordered]@{}
        thresholds = [ordered]@{}
        lanes = [ordered]@{}
        diagnostics = [ordered]@{}
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
            -Environment $defaultSceneDlaaPresentEnvironment `
            -AllowedDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
        $defaultSceneDlaaNativeImageStats =
            Get-ImageVariationStats -Path $defaultSceneDlaaNativeImage
        $defaultSceneDlaaImageStats =
            Get-ImageVariationStats -Path $defaultSceneDlaaImage
        $defaultSceneDlaaComparison =
            Compare-Images -A $defaultSceneDlaaNativeImage -B $defaultSceneDlaaImage
        $defaultSceneDlaaEdgeComparison =
            Compare-ImageEdges -A $defaultSceneDlaaNativeImage -B $defaultSceneDlaaImage

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
        Assert-QuickEdgeComparison `
            -Name "defaultSceneDlaa" `
            -EdgeComparison $defaultSceneDlaaEdgeComparison `
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
            -Comparison $defaultSceneDlaaComparison `
            -EdgeComparison $defaultSceneDlaaEdgeComparison
        $quickSummary["diagnostics"]["defaultSceneDlaa"] = [ordered]@{
            validationClean = $false
            preset = "M"
            allowedKnownCaptureDiagnostic =
                "NGX internal nv.ngx.dlss.resource layout warning for preset M"
            captureErrLog =
                (Join-Path $outputRoot "default_scene_dlaa_present.capture.err.log")
        }
    }

    if ($selectedSuites -contains "default-tuning") {
        $quickSummary["baselines"]["defaultTuning"] = [ordered]@{
            manifest = $defaultSceneDlaaBaselineManifestPath
            name = $defaultSceneDlaaBaselineManifest.name
        }
        $quickSummary["thresholds"]["defaultTuning"] =
            New-QuickComparisonThresholdSummary `
                -Manifest $defaultSceneDlaaBaselineManifest

        $defaultSceneDlaaTuningNativeBenchmark = Invoke-BenchmarkRun `
            -Name "default_scene_dlaa_tuning_native_deferred_hdr" `
            -Environment $defaultSceneDlaaNativeEnvironment `
            -UseApplicationScene
        $defaultSceneDlaaTuningNativeMetrics = Assert-QuickNativeRow `
            -Name "defaultSceneDlaaTuning.native" `
            -Row $defaultSceneDlaaTuningNativeBenchmark.LastRow `
            -Expected $defaultSceneDlaaBaselineManifest.expected.native
        $defaultSceneDlaaTuningNativeImage = Capture-WindowImage `
            -Name "default_scene_dlaa_tuning_native_deferred_hdr" `
            -Environment $defaultSceneDlaaNativeEnvironment
        $defaultSceneDlaaTuningNativeImageStats =
            Get-ImageVariationStats -Path $defaultSceneDlaaTuningNativeImage
        Assert-QuickImageStats `
            -Name "defaultSceneDlaaTuning.native" `
            -Stats $defaultSceneDlaaTuningNativeImageStats `
            -Manifest $defaultSceneDlaaBaselineManifest

        $quickSummary["lanes"]["defaultSceneDlaaTuningNative"] =
            New-QuickLaneSummary `
                -Benchmark $defaultSceneDlaaTuningNativeBenchmark `
                -Metrics $defaultSceneDlaaTuningNativeMetrics `
                -Images @($defaultSceneDlaaTuningNativeImage) `
                -ImageStats @($defaultSceneDlaaTuningNativeImageStats)
        $quickSummary["lanes"]["defaultSceneDlaaTuningPresetK"] =
            Invoke-QuickDlaaTuningLane `
                -Name "default_scene_dlaa_tuning_preset_k_present" `
                -LaneKey "defaultSceneDlaaTuning.presetK" `
                -Environment $defaultSceneDlaaPresetKEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "11" `
                -NativeImage $defaultSceneDlaaTuningNativeImage
        $defaultSceneDlaaTuningPresetLBenchmark = Invoke-BenchmarkRun `
            -Name "default_scene_dlaa_tuning_preset_l_benchmark" `
            -Environment $defaultSceneDlaaPresetLEnvironment `
            -UseApplicationScene
        $defaultSceneDlaaTuningPresetLMetrics =
            Assert-QuickDlssTuningRow `
                -Name "defaultSceneDlaaTuning.presetLBenchmark" `
                -Row $defaultSceneDlaaTuningPresetLBenchmark.LastRow `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "12"
        $quickSummary["lanes"]["defaultSceneDlaaTuningPresetLBenchmark"] =
            New-QuickLaneSummary `
                -Benchmark $defaultSceneDlaaTuningPresetLBenchmark `
                -Metrics $defaultSceneDlaaTuningPresetLMetrics
        $quickSummary["lanes"]["defaultSceneDlaaTuningSharpnessZero"] =
            Invoke-QuickDlaaTuningLane `
                -Name "default_scene_dlaa_tuning_sharpness_zero_present" `
                -LaneKey "defaultSceneDlaaTuning.sharpnessZero" `
                -Environment $defaultSceneDlaaSharpnessZeroEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "13" `
                -ExpectedEvaluateSharpness 0.0 `
                -NativeImage $defaultSceneDlaaTuningNativeImage `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
        $quickSummary["lanes"]["defaultSceneDlaaTuningMipBiasPositive"] =
            Invoke-QuickDlaaTuningLane `
                -Name "default_scene_dlaa_tuning_mip_bias_positive_present" `
                -LaneKey "defaultSceneDlaaTuning.mipBiasPositive" `
                -Environment $defaultSceneDlaaMipBiasPositiveEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "13" `
                -ExpectedMaterialTextureMipLodBias 1.0 `
                -NativeImage $defaultSceneDlaaTuningNativeImage `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
    }

    if ($selectedSuites -contains "default-preset-l") {
        $quickSummary["baselines"]["defaultPresetL"] = [ordered]@{
            manifest = $defaultSceneDlaaBaselineManifestPath
            name = $defaultSceneDlaaBaselineManifest.name
        }
        $quickSummary["thresholds"]["defaultPresetL"] =
            New-QuickComparisonThresholdSummary `
                -Manifest $defaultSceneDlaaBaselineManifest
        $defaultSceneDlaaPresetLNativeBenchmark = Invoke-BenchmarkRun `
            -Name "default_scene_dlaa_preset_l_native_deferred_hdr" `
            -Environment $defaultSceneDlaaNativeEnvironment `
            -UseApplicationScene
        $defaultSceneDlaaPresetLNativeMetrics = Assert-QuickNativeRow `
            -Name "defaultSceneDlaaPresetL.native" `
            -Row $defaultSceneDlaaPresetLNativeBenchmark.LastRow `
            -Expected $defaultSceneDlaaBaselineManifest.expected.native
        $defaultSceneDlaaPresetLNativeImage = Capture-WindowImage `
            -Name "default_scene_dlaa_preset_l_native_deferred_hdr" `
            -Environment $defaultSceneDlaaNativeEnvironment
        $defaultSceneDlaaPresetLNativeImageStats =
            Get-ImageVariationStats -Path $defaultSceneDlaaPresetLNativeImage
        Assert-QuickImageStats `
            -Name "defaultSceneDlaaPresetL.native" `
            -Stats $defaultSceneDlaaPresetLNativeImageStats `
            -Manifest $defaultSceneDlaaBaselineManifest

        $quickSummary["lanes"]["defaultSceneDlaaPresetLNative"] =
            New-QuickLaneSummary `
                -Benchmark $defaultSceneDlaaPresetLNativeBenchmark `
                -Metrics $defaultSceneDlaaPresetLNativeMetrics `
                -Images @($defaultSceneDlaaPresetLNativeImage) `
                -ImageStats @($defaultSceneDlaaPresetLNativeImageStats)
        $quickSummary["lanes"]["defaultSceneDlaaPresetLPresent"] =
            Invoke-QuickDlaaTuningLane `
                -Name "default_scene_dlaa_preset_l_present" `
                -LaneKey "defaultSceneDlaaPresetL.present" `
                -Environment $defaultSceneDlaaPresetLEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "12" `
                -NativeImage $defaultSceneDlaaPresetLNativeImage `
                -AllowedCaptureDiagnosticPatterns @(
                    "nv\.ngx\.dlss\.resource",
                    "VUID-vkCmdDraw-None-09600",
                    "descriptor with type equal"
                )
        $quickSummary["diagnostics"]["defaultPresetL"] = [ordered]@{
            validationClean = $false
            allowedKnownCaptureDiagnostic =
                "NGX internal nv.ngx.dlss.resource layout warning for preset L"
            captureErrLog =
                (Join-Path $outputRoot "default_scene_dlaa_preset_l_present.capture.err.log")
        }
    }

    if ($selectedSuites -contains "default-preset-k-fullscreen-control") {
        $quickSummary["baselines"]["defaultPresetKFullscreenControl"] = [ordered]@{
            manifest = $defaultSceneDlaaBaselineManifestPath
            name = $defaultSceneDlaaBaselineManifest.name
        }
        $quickSummary["thresholds"]["defaultPresetKFullscreenControl"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            comparisonGate = "record-only"
            validationRequirement = "clean-capture-logs"
            reason =
                "Strict preset-K fullscreen control for isolating whether the NGX resource diagnostic is M-specific"
        }
        $defaultSceneDlaaPresetKFullscreenNativeBenchmark = Invoke-BenchmarkRun `
            -Name "default_scene_dlaa_preset_k_fullscreen_control_native_deferred_hdr" `
            -Environment $defaultSceneDlaaHighResolutionNativeVisualQaEnvironment `
            -UseApplicationScene
        $defaultSceneDlaaPresetKFullscreenNativeMetrics = Assert-QuickNativeRow `
            -Name "defaultSceneDlaaPresetKFullscreenControl.native" `
            -Row $defaultSceneDlaaPresetKFullscreenNativeBenchmark.LastRow `
            -Expected $defaultSceneDlaaBaselineManifest.expected.native
        $defaultSceneDlaaPresetKFullscreenNativeImage = Capture-WindowImage `
            -Name "default_scene_dlaa_preset_k_fullscreen_control_native_deferred_hdr" `
            -Environment $defaultSceneDlaaHighResolutionNativeVisualQaEnvironment `
            -UseMonitorResolution
        $defaultSceneDlaaPresetKFullscreenNativeImageStats =
            Get-ImageVariationStats -Path $defaultSceneDlaaPresetKFullscreenNativeImage
        Assert-QuickImageStats `
            -Name "defaultSceneDlaaPresetKFullscreenControl.native" `
            -Stats $defaultSceneDlaaPresetKFullscreenNativeImageStats `
            -Manifest $defaultSceneDlaaBaselineManifest

        $quickSummary["lanes"]["defaultSceneDlaaPresetKFullscreenControlNative"] =
            New-QuickLaneSummary `
                -Benchmark $defaultSceneDlaaPresetKFullscreenNativeBenchmark `
                -Metrics $defaultSceneDlaaPresetKFullscreenNativeMetrics `
                -Images @($defaultSceneDlaaPresetKFullscreenNativeImage) `
                -ImageStats @($defaultSceneDlaaPresetKFullscreenNativeImageStats)
        $defaultSceneDlaaPresetKFullscreenLane =
            Invoke-QuickDlaaTuningLane `
                -Name "default_scene_dlaa_preset_k_fullscreen_control_present" `
                -LaneKey "defaultSceneDlaaPresetKFullscreenControl.presetK" `
                -Environment $defaultSceneDlaaHighResolutionPresetKResourceDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "11" `
                -NativeImage $defaultSceneDlaaPresetKFullscreenNativeImage `
                -UseMonitorResolutionCapture `
                -SkipComparisonGate `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["defaultSceneDlaaPresetKFullscreenControlPresetK"] =
            $defaultSceneDlaaPresetKFullscreenLane
        $defaultSceneDlaaPresetKFullscreenValidationImages =
            Capture-WindowImageSequence `
                -Name "default_scene_dlaa_preset_k_fullscreen_control_validation_present" `
                -Environment $defaultSceneDlaaHighResolutionPresetKResourceDiagnosticsEnvironment `
                -FrameCount $SequenceFrameCount `
                -InitialDelaySeconds $SequenceInitialDelaySeconds `
                -IntervalSeconds $SequenceIntervalSeconds `
                -UseMonitorResolution
        $defaultSceneDlaaPresetKFullscreenValidationStats = @()
        foreach ($image in $defaultSceneDlaaPresetKFullscreenValidationImages) {
            $defaultSceneDlaaPresetKFullscreenValidationStats +=
                Get-ImageVariationStats -Path $image
        }
        $defaultSceneDlaaPresetKFullscreenValidationSequence =
            Compare-ImageSequence `
                -Paths $defaultSceneDlaaPresetKFullscreenValidationImages
        Assert-QuickImageStats `
            -Name "defaultSceneDlaaPresetKFullscreenControl.validationSequence" `
            -Stats $defaultSceneDlaaPresetKFullscreenValidationStats `
            -Manifest $defaultSceneDlaaBaselineManifest
        $quickSummary["lanes"]["defaultSceneDlaaPresetKFullscreenControlValidationSequence"] =
            [ordered]@{
                csv = $defaultSceneDlaaPresetKFullscreenLane["csv"]
                columns = $defaultSceneDlaaPresetKFullscreenLane["columns"]
                metrics = $defaultSceneDlaaPresetKFullscreenLane["metrics"]
                images = $defaultSceneDlaaPresetKFullscreenValidationImages
                imageStats = $defaultSceneDlaaPresetKFullscreenValidationStats
                sequenceComparison =
                    $defaultSceneDlaaPresetKFullscreenValidationSequence
            }
        $quickSummary["diagnostics"]["defaultPresetKFullscreenControl"] = [ordered]@{
            validationClean = $true
            diagnosticOnly = $true
            productionCandidate = $false
            controlCandidate = $true
            validationRequirement =
                "Preset K control must pass without any stdout/stderr validation/error/VUID/shader diagnostics across still and monitor-resolution sequence captures"
        }
    }

    if ($selectedSuites -contains "default-preset-k-dynamic-control") {
        $quickSummary["baselines"]["defaultPresetKDynamicControl"] = [ordered]@{
            motionManifest = $defaultSceneDlaaMotionBaselineManifestPath
            motionName = $defaultSceneDlaaMotionBaselineManifest.name
            objectMotionManifest = $defaultSceneDlaaObjectMotionBaselineManifestPath
            objectMotionName = $defaultSceneDlaaObjectMotionBaselineManifest.name
        }
        $quickSummary["thresholds"]["defaultPresetKDynamicControl"] = [ordered]@{
            sequenceGate = "record-only"
            movingCamera =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneDlaaMotionBaselineManifest
            movingObjectOnly =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneDlaaObjectMotionBaselineManifest
            combinedCameraObject =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneDlaaObjectMotionBaselineManifest
            validationRequirement = "clean-capture-logs"
            reason =
                "Strict preset-K dynamic control uses the same motion lanes as preset M without the M diagnostic whitelist"
        }
        $quickSummary["lanes"]["defaultSceneDlaaPresetKMovingCameraControl"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "default_scene_dlaa_preset_k_control_moving_camera_present" `
                -LaneKey "defaultSceneDlaaPresetKControl.movingCamera" `
                -Environment $defaultSceneDlaaPresetKMotionPresentEnvironment `
                -Manifest $defaultSceneDlaaMotionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate
        $quickSummary["lanes"]["defaultSceneDlaaPresetKMovingObjectOnlyControl"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "default_scene_dlaa_preset_k_control_moving_object_only_present" `
                -LaneKey "defaultSceneDlaaPresetKControl.movingObjectOnly" `
                -Environment $defaultSceneDlaaPresetKObjectOnlyPresentEnvironment `
                -Manifest $defaultSceneDlaaObjectMotionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate
        $quickSummary["lanes"]["defaultSceneDlaaPresetKCombinedCameraObjectControl"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "default_scene_dlaa_preset_k_control_combined_camera_object_present" `
                -LaneKey "defaultSceneDlaaPresetKControl.combinedCameraObject" `
                -Environment $defaultSceneDlaaPresetKCombinedMotionPresentEnvironment `
                -Manifest $defaultSceneDlaaObjectMotionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate
        $quickSummary["diagnostics"]["defaultPresetKDynamicControl"] = [ordered]@{
            validationClean = $true
            diagnosticOnly = $true
            productionCandidate = $false
            controlCandidate = $true
            lanes = @(
                "movingCamera",
                "movingObjectOnly",
                "combinedCameraObject"
            )
            validationRequirement =
                "Preset K dynamic control must pass without any stdout/stderr validation/error/VUID/shader diagnostics"
        }
    }

    if ($selectedSuites -contains "default-preset-m") {
        $quickSummary["baselines"]["defaultPresetM"] = [ordered]@{
            manifest = $defaultSceneDlaaBaselineManifestPath
            name = $defaultSceneDlaaBaselineManifest.name
        }
        $quickSummary["thresholds"]["defaultPresetM"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            comparisonGate = "record-only"
            reason = "720p absolute comparison thresholds are not reused for monitor-resolution captures"
        }
        $defaultSceneDlaaPresetMNativeBenchmark = Invoke-BenchmarkRun `
            -Name "default_scene_dlaa_preset_m_highres_native_deferred_hdr" `
            -Environment $defaultSceneDlaaHighResolutionNativeEnvironment `
            -UseApplicationScene
        $defaultSceneDlaaPresetMNativeMetrics = Assert-QuickNativeRow `
            -Name "defaultSceneDlaaPresetM.highresNative" `
            -Row $defaultSceneDlaaPresetMNativeBenchmark.LastRow `
            -Expected $defaultSceneDlaaBaselineManifest.expected.native
        $defaultSceneDlaaPresetMNativeImage = Capture-WindowImage `
            -Name "default_scene_dlaa_preset_m_highres_native_deferred_hdr" `
            -Environment $defaultSceneDlaaHighResolutionNativeEnvironment `
            -UseMonitorResolution
        $defaultSceneDlaaPresetMNativeImageStats =
            Get-ImageVariationStats -Path $defaultSceneDlaaPresetMNativeImage
        Assert-QuickImageStats `
            -Name "defaultSceneDlaaPresetM.highresNative" `
            -Stats $defaultSceneDlaaPresetMNativeImageStats `
            -Manifest $defaultSceneDlaaBaselineManifest

        $quickSummary["lanes"]["defaultSceneDlaaPresetMHighResolutionNative"] =
            New-QuickLaneSummary `
                -Benchmark $defaultSceneDlaaPresetMNativeBenchmark `
                -Metrics $defaultSceneDlaaPresetMNativeMetrics `
                -Images @($defaultSceneDlaaPresetMNativeImage) `
                -ImageStats @($defaultSceneDlaaPresetMNativeImageStats)
        $quickSummary["lanes"]["defaultSceneDlaaPresetMHighResolutionPresetK"] =
            Invoke-QuickDlaaTuningLane `
                -Name "default_scene_dlaa_preset_m_highres_preset_k_present" `
                -LaneKey "defaultSceneDlaaPresetM.highresPresetK" `
                -Environment $defaultSceneDlaaHighResolutionPresetKEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "11" `
                -NativeImage $defaultSceneDlaaPresetMNativeImage `
                -UseMonitorResolutionCapture `
                -SkipComparisonGate `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["defaultSceneDlaaPresetMHighResolutionPresetM"] =
            Invoke-QuickDlaaTuningLane `
                -Name "default_scene_dlaa_preset_m_highres_preset_m_present" `
                -LaneKey "defaultSceneDlaaPresetM.highresPresetM" `
                -Environment $defaultSceneDlaaHighResolutionPresetMEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "13" `
                -NativeImage $defaultSceneDlaaPresetMNativeImage `
                -UseMonitorResolutionCapture `
                -SkipComparisonGate `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
        $quickSummary["diagnostics"]["defaultPresetM"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $true
            productionCandidate = $false
            highResolutionPresetKValidationClean = $true
            highResolutionPresetMValidationClean = $false
            allowedKnownCaptureDiagnostic =
                "NGX internal nv.ngx.dlss.resource layout warning for preset M at monitor resolution"
            captureErrLog =
                (Join-Path $outputRoot "default_scene_dlaa_preset_m_highres_preset_m_present.capture.err.log")
        }
    }

    if ($selectedSuites -contains "default-preset-m-fullscreen") {
        $quickSummary["baselines"]["defaultPresetMFullscreen"] = [ordered]@{
            manifest = $defaultSceneDlaaBaselineManifestPath
            name = $defaultSceneDlaaBaselineManifest.name
        }
        $quickSummary["thresholds"]["defaultPresetMFullscreen"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            comparisonGate = "record-only"
            validationRequirement = "clean-capture-logs"
            reason = "Production-candidate M fullscreen captures must not use the NGX diagnostic whitelist"
        }
        $defaultSceneDlaaPresetMFullscreenNativeBenchmark = Invoke-BenchmarkRun `
            -Name "default_scene_dlaa_preset_m_fullscreen_native_deferred_hdr" `
            -Environment $defaultSceneDlaaHighResolutionNativeEnvironment `
            -UseApplicationScene
        $defaultSceneDlaaPresetMFullscreenNativeMetrics = Assert-QuickNativeRow `
            -Name "defaultSceneDlaaPresetMFullscreen.native" `
            -Row $defaultSceneDlaaPresetMFullscreenNativeBenchmark.LastRow `
            -Expected $defaultSceneDlaaBaselineManifest.expected.native
        $defaultSceneDlaaPresetMFullscreenNativeImage = Capture-WindowImage `
            -Name "default_scene_dlaa_preset_m_fullscreen_native_deferred_hdr" `
            -Environment $defaultSceneDlaaHighResolutionNativeEnvironment `
            -UseMonitorResolution
        $defaultSceneDlaaPresetMFullscreenNativeImageStats =
            Get-ImageVariationStats -Path $defaultSceneDlaaPresetMFullscreenNativeImage
        Assert-QuickImageStats `
            -Name "defaultSceneDlaaPresetMFullscreen.native" `
            -Stats $defaultSceneDlaaPresetMFullscreenNativeImageStats `
            -Manifest $defaultSceneDlaaBaselineManifest

        $quickSummary["lanes"]["defaultSceneDlaaPresetMFullscreenNative"] =
            New-QuickLaneSummary `
                -Benchmark $defaultSceneDlaaPresetMFullscreenNativeBenchmark `
                -Metrics $defaultSceneDlaaPresetMFullscreenNativeMetrics `
                -Images @($defaultSceneDlaaPresetMFullscreenNativeImage) `
                -ImageStats @($defaultSceneDlaaPresetMFullscreenNativeImageStats)
        $defaultSceneDlaaPresetMFullscreenLane =
            Invoke-QuickDlaaTuningLane `
                -Name "default_scene_dlaa_preset_m_fullscreen_present" `
                -LaneKey "defaultSceneDlaaPresetMFullscreen.presetM" `
                -Environment $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "13" `
                -NativeImage $defaultSceneDlaaPresetMFullscreenNativeImage `
                -UseMonitorResolutionCapture `
                -SkipComparisonGate `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["defaultSceneDlaaPresetMFullscreenPresetM"] =
            $defaultSceneDlaaPresetMFullscreenLane
        $defaultSceneDlaaPresetMFullscreenValidationImages =
            Capture-WindowImageSequence `
                -Name "default_scene_dlaa_preset_m_fullscreen_validation_present" `
                -Environment $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment `
                -FrameCount $SequenceFrameCount `
                -InitialDelaySeconds $SequenceInitialDelaySeconds `
                -IntervalSeconds $SequenceIntervalSeconds `
                -UseMonitorResolution
        $defaultSceneDlaaPresetMFullscreenValidationStats = @()
        foreach ($image in $defaultSceneDlaaPresetMFullscreenValidationImages) {
            $defaultSceneDlaaPresetMFullscreenValidationStats +=
                Get-ImageVariationStats -Path $image
        }
        $defaultSceneDlaaPresetMFullscreenValidationSequence =
            Compare-ImageSequence `
                -Paths $defaultSceneDlaaPresetMFullscreenValidationImages
        Assert-QuickImageStats `
            -Name "defaultSceneDlaaPresetMFullscreen.validationSequence" `
            -Stats $defaultSceneDlaaPresetMFullscreenValidationStats `
            -Manifest $defaultSceneDlaaBaselineManifest
        $quickSummary["lanes"]["defaultSceneDlaaPresetMFullscreenValidationSequence"] =
            [ordered]@{
                csv = $defaultSceneDlaaPresetMFullscreenLane["csv"]
                columns = $defaultSceneDlaaPresetMFullscreenLane["columns"]
                metrics = $defaultSceneDlaaPresetMFullscreenLane["metrics"]
                images = $defaultSceneDlaaPresetMFullscreenValidationImages
                imageStats = $defaultSceneDlaaPresetMFullscreenValidationStats
                sequenceComparison =
                    $defaultSceneDlaaPresetMFullscreenValidationSequence
            }
        $quickSummary["diagnostics"]["defaultPresetMFullscreen"] = [ordered]@{
            validationClean = $true
            diagnosticOnly = $false
            productionCandidate = $true
            validationRequirement =
                "No stdout/stderr diagnostics may match validation/error/VUID/shader patterns across the still capture and monitor-resolution sequence"
        }
    }

    if ($selectedSuites -contains "m-ngx-mask-diagnostics") {
        $quickSummary["baselines"]["mNgxMaskDiagnostics"] = [ordered]@{
            manifest = $defaultSceneDlaaBaselineManifestPath
            name = $defaultSceneDlaaBaselineManifest.name
        }
        $quickSummary["thresholds"]["mNgxMaskDiagnostics"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            centralDifferentPixelsMin =
                $defaultSceneDlaaBaselineManifest.thresholds.centralDifferentPixelsMin
            validationGate = "record-only"
            reason =
                "Diagnostic-only isolation of preset-M NGX-owned layout warning against optional DLSS mask bindings"
        }
        $quickSummary["lanes"]["mNgxMaskDiagnosticsAllOptionalMasks"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "default_scene_dlaa_preset_m_ngx_masks_all_present" `
                -LaneKey "mNgxMaskDiagnostics.allOptionalMasks" `
                -Environment $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxMaskDiagnosticsNoTransparencyMask"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "default_scene_dlaa_preset_m_ngx_masks_no_transparency_present" `
                -LaneKey "mNgxMaskDiagnostics.noTransparencyMask" `
                -Environment $defaultSceneDlaaHighResolutionPresetMNoTransparencyMaskEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxMaskDiagnosticsNoBiasMask"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "default_scene_dlaa_preset_m_ngx_masks_no_bias_present" `
                -LaneKey "mNgxMaskDiagnostics.noBiasMask" `
                -Environment $defaultSceneDlaaHighResolutionPresetMNoBiasMaskEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxMaskDiagnosticsNoOptionalMasks"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "default_scene_dlaa_preset_m_ngx_masks_no_optional_present" `
                -LaneKey "mNgxMaskDiagnostics.noOptionalMasks" `
                -Environment $defaultSceneDlaaHighResolutionPresetMNoOptionalMasksEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["diagnostics"]["mNgxMaskDiagnostics"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $true
            productionCandidate = $false
            variants = [ordered]@{
                allOptionalMasks =
                    $quickSummary["lanes"]["mNgxMaskDiagnosticsAllOptionalMasks"]["validationClean"]
                noTransparencyMask =
                    $quickSummary["lanes"]["mNgxMaskDiagnosticsNoTransparencyMask"]["validationClean"]
                noBiasMask =
                    $quickSummary["lanes"]["mNgxMaskDiagnosticsNoBiasMask"]["validationClean"]
                noOptionalMasks =
                    $quickSummary["lanes"]["mNgxMaskDiagnosticsNoOptionalMasks"]["validationClean"]
            }
            ngxResourceDiagnosticPresent = [ordered]@{
                allOptionalMasks =
                    $quickSummary["lanes"]["mNgxMaskDiagnosticsAllOptionalMasks"]["ngxResourceLayoutDiagnostic"]
                noTransparencyMask =
                    $quickSummary["lanes"]["mNgxMaskDiagnosticsNoTransparencyMask"]["ngxResourceLayoutDiagnostic"]
                noBiasMask =
                    $quickSummary["lanes"]["mNgxMaskDiagnosticsNoBiasMask"]["ngxResourceLayoutDiagnostic"]
                noOptionalMasks =
                    $quickSummary["lanes"]["mNgxMaskDiagnosticsNoOptionalMasks"]["ngxResourceLayoutDiagnostic"]
            }
        }
    }

    if ($selectedSuites -contains "m-ngx-layout-audit") {
        $quickSummary["baselines"]["mNgxLayoutAudit"] = [ordered]@{
            fullscreenManifest = $defaultSceneDlaaBaselineManifestPath
            fullscreenName = $defaultSceneDlaaBaselineManifest.name
            movingCameraManifest = $defaultSceneDlaaMotionBaselineManifestPath
            movingCameraName = $defaultSceneDlaaMotionBaselineManifest.name
        }
        $quickSummary["thresholds"]["mNgxLayoutAudit"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            sequenceGate = "record-only"
            validationGate = "diagnostic-attribution"
            reason =
                "Run preset K and M under matching fullscreen and moving-camera sequence conditions to attribute the NGX-owned resource layout warning"
        }
        $quickSummary["lanes"]["mNgxLayoutAuditFullscreenPresetK"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_layout_audit_fullscreen_preset_k_present" `
                -LaneKey "mNgxLayoutAudit.fullscreenPresetK" `
                -Environment $defaultSceneDlaaHighResolutionPresetKResourceDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxLayoutAuditFullscreenPresetM"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_layout_audit_fullscreen_preset_m_present" `
                -LaneKey "mNgxLayoutAudit.fullscreenPresetM" `
                -Environment $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxLayoutAuditMovingCameraPresetK"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_layout_audit_moving_camera_preset_k_present" `
                -LaneKey "mNgxLayoutAudit.movingCameraPresetK" `
                -Environment $defaultSceneDlaaPresetKMotionResourceDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaMotionBaselineManifest `
                -UseSequenceCapture `
                -ExpectedDlssWidth 1280 `
                -ExpectedDlssHeight 720
        $quickSummary["lanes"]["mNgxLayoutAuditMovingCameraPresetM"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_layout_audit_moving_camera_preset_m_present" `
                -LaneKey "mNgxLayoutAudit.movingCameraPresetM" `
                -Environment $defaultSceneDlaaPresetMMotionResourceDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaMotionBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -UseSequenceCapture `
                -ExpectedDlssWidth 1280 `
                -ExpectedDlssHeight 720

        $quickSummary["diagnostics"]["mNgxLayoutAudit"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $true
            productionCandidate = $false
            attribution = "preset-M-specific-under-current-K-control"
            presetK = [ordered]@{
                fullscreenValidationClean =
                    $quickSummary["lanes"]["mNgxLayoutAuditFullscreenPresetK"]["validationClean"]
                movingCameraValidationClean =
                    $quickSummary["lanes"]["mNgxLayoutAuditMovingCameraPresetK"]["validationClean"]
                fullscreenNgxResourceDiagnostic =
                    $quickSummary["lanes"]["mNgxLayoutAuditFullscreenPresetK"]["ngxResourceLayoutDiagnostic"]
                movingCameraNgxResourceDiagnostic =
                    $quickSummary["lanes"]["mNgxLayoutAuditMovingCameraPresetK"]["ngxResourceLayoutDiagnostic"]
            }
            presetM = [ordered]@{
                fullscreenValidationClean =
                    $quickSummary["lanes"]["mNgxLayoutAuditFullscreenPresetM"]["validationClean"]
                movingCameraValidationClean =
                    $quickSummary["lanes"]["mNgxLayoutAuditMovingCameraPresetM"]["validationClean"]
                fullscreenNgxResourceDiagnostic =
                    $quickSummary["lanes"]["mNgxLayoutAuditFullscreenPresetM"]["ngxResourceLayoutDiagnostic"]
                movingCameraNgxResourceDiagnostic =
                    $quickSummary["lanes"]["mNgxLayoutAuditMovingCameraPresetM"]["ngxResourceLayoutDiagnostic"]
            }
            requiredProductionState = [ordered]@{
                presetMFullscreenValidationClean = $true
                presetMMovingCameraValidationClean = $true
                presetMFullscreenNgxResourceDiagnostic = $false
                presetMMovingCameraNgxResourceDiagnostic = $false
            }
        }
    }

    if ($selectedSuites -contains "m-ngx-lifecycle-audit") {
        $quickSummary["baselines"]["mNgxLifecycleAudit"] = [ordered]@{
            fullscreenManifest = $defaultSceneDlaaBaselineManifestPath
            fullscreenName = $defaultSceneDlaaBaselineManifest.name
        }
        $quickSummary["thresholds"]["mNgxLifecycleAudit"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            sequenceGate = "record-only"
            validationGate = "diagnostic-attribution"
            reason =
                "Trace NGX DLSS feature create, warmup, reuse, evaluate, and warning handles for fullscreen preset K/M under matching conditions."
        }
        $quickSummary["lanes"]["mNgxLifecycleAuditFullscreenPresetK"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_lifecycle_audit_fullscreen_preset_k_present" `
                -LaneKey "mNgxLifecycleAudit.fullscreenPresetK" `
                -Environment $defaultSceneDlaaHighResolutionPresetKResourceDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxLifecycleAuditFullscreenPresetM"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_lifecycle_audit_fullscreen_preset_m_present" `
                -LaneKey "mNgxLifecycleAudit.fullscreenPresetM" `
                -Environment $defaultSceneDlaaHighResolutionPresetMResourceDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $presetKLifecycle =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxLifecycleAuditFullscreenPresetK"]
        $presetMLifecycle =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxLifecycleAuditFullscreenPresetM"]

        $quickSummary["diagnostics"]["mNgxLifecycleAudit"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $true
            productionCandidate = $false
            attribution =
                "preset-M-specific-after-matching-feature-lifecycle-under-current-K-control"
            presetK = [ordered]@{
                validationClean =
                    $quickSummary["lanes"]["mNgxLifecycleAuditFullscreenPresetK"]["validationClean"]
                ngxResourceDiagnostic =
                    $quickSummary["lanes"]["mNgxLifecycleAuditFullscreenPresetK"]["ngxResourceLayoutDiagnostic"]
                lifecycle = $presetKLifecycle
            }
            presetM = [ordered]@{
                validationClean =
                    $quickSummary["lanes"]["mNgxLifecycleAuditFullscreenPresetM"]["validationClean"]
                ngxResourceDiagnostic =
                    $quickSummary["lanes"]["mNgxLifecycleAuditFullscreenPresetM"]["ngxResourceLayoutDiagnostic"]
                lifecycle = $presetMLifecycle
            }
            handleOverlap = [ordered]@{
                presetKWarningOverlapCount =
                    $presetKLifecycle["warningHandleOverlapCount"]
                presetMWarningOverlapCount =
                    $presetMLifecycle["warningHandleOverlapCount"]
                presetMWarningHandles =
                    $presetMLifecycle["warningHandles"]
                presetMSelfEngineImageHandles =
                    $presetMLifecycle["selfEngineImageHandles"]
                inference =
                    "A zero overlap count means validation warned on NGX-owned images rather than the SelfEngine evaluate resources traced in the same process."
            }
            requiredProductionState = [ordered]@{
                presetMValidationClean = $true
                presetMNgxResourceDiagnostic = $false
                presetMWarningHandleOverlapCount = 0
                presetMLifecycleTracePresent = $true
            }
        }
    }

    if ($selectedSuites -contains "m-ngx-runtime-flavor-audit") {
        $quickSummary["baselines"]["mNgxRuntimeFlavorAudit"] = [ordered]@{
            fullscreenManifest = $defaultSceneDlaaBaselineManifestPath
            fullscreenName = $defaultSceneDlaaBaselineManifest.name
        }
        $quickSummary["thresholds"]["mNgxRuntimeFlavorAudit"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            sequenceGate = "record-only"
            validationGate = "diagnostic-attribution"
            reason =
                "Compare preset-M fullscreen NGX layout diagnostics between explicit rel and dev DLSS runtime feature paths."
        }
        $quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMRel"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_runtime_flavor_audit_preset_m_rel_present" `
                -LaneKey "mNgxRuntimeFlavorAudit.presetMRel" `
                -Environment $defaultSceneDlaaHighResolutionPresetMRelRuntimeDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMInternalResourceAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMDev"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_runtime_flavor_audit_preset_m_dev_present" `
                -LaneKey "mNgxRuntimeFlavorAudit.presetMDev" `
                -Environment $defaultSceneDlaaHighResolutionPresetMDevRuntimeDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMInternalResourceAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $presetMRelRuntime =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMRel"]
        $presetMDevRuntime =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMDev"]
        $relMetrics =
            $quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMRel"]["metrics"]
        $devMetrics =
            $quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMDev"]["metrics"]

        $quickSummary["diagnostics"]["mNgxRuntimeFlavorAudit"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $true
            productionCandidate = $false
            presetMRel = [ordered]@{
                validationClean =
                    $quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMRel"]["validationClean"]
                ngxResourceDiagnostic =
                    $quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMRel"]["ngxResourceLayoutDiagnostic"]
                runtimeFlavor = $relMetrics.runtimeFlavor
                runtimePathFound = $relMetrics.runtimePathFound
                runtimePath = $relMetrics.runtimePath
                lifecycle = $presetMRelRuntime
            }
            presetMDev = [ordered]@{
                validationClean =
                    $quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMDev"]["validationClean"]
                ngxResourceDiagnostic =
                    $quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMDev"]["ngxResourceLayoutDiagnostic"]
                runtimeFlavor = $devMetrics.runtimeFlavor
                runtimePathFound = $devMetrics.runtimePathFound
                runtimePath = $devMetrics.runtimePath
                lifecycle = $presetMDevRuntime
            }
            inference = [ordered]@{
                devRuntimeChangedValidation =
                    ([bool]$quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMRel"]["validationClean"] -ne
                     [bool]$quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMDev"]["validationClean"])
                devRuntimeChangedNgxDiagnostic =
                    ([bool]$quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMRel"]["ngxResourceLayoutDiagnostic"] -ne
                     [bool]$quickSummary["lanes"]["mNgxRuntimeFlavorAuditPresetMDev"]["ngxResourceLayoutDiagnostic"])
                relWarningOverlapCount =
                    $presetMRelRuntime["warningHandleOverlapCount"]
                devWarningOverlapCount =
                    $presetMDevRuntime["warningHandleOverlapCount"]
                relWarningResourceNames =
                    $presetMRelRuntime["warningResourceNames"]
                devWarningResourceNames =
                    $presetMDevRuntime["warningResourceNames"]
                meaning =
                    "If rel/dev both report the same M-only NGX warning with zero SelfEngine handle overlap, runtime flavor is not a production workaround."
            }
            requiredProductionState = [ordered]@{
                presetMRelValidationClean = $true
                presetMDevValidationClean = $true
                presetMRelNgxResourceDiagnostic = $false
                presetMDevNgxResourceDiagnostic = $false
            }
        }
    }

    if ($selectedSuites -contains "m-ngx-parameter-matrix-audit") {
        $quickSummary["baselines"]["mNgxParameterMatrixAudit"] = [ordered]@{
            fullscreenManifest = $defaultSceneDlaaBaselineManifestPath
            fullscreenName = $defaultSceneDlaaBaselineManifest.name
            movingCameraManifest = $defaultSceneDlaaMotionBaselineManifestPath
            movingCameraName = $defaultSceneDlaaMotionBaselineManifest.name
        }
        $quickSummary["thresholds"]["mNgxParameterMatrixAudit"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            validationGate = "diagnostic-parameter-attribution"
            captureMode = "single-frame-focused"
            reason =
                "Focused matrix for isolating whether preset-M NGX-owned layout diagnostics follow optional mask bindings, camera motion, or runtime flavor."
        }

        $parameterMatrixPresetKRelEnvironment =
            $defaultSceneDlaaHighResolutionPresetKResourceDiagnosticsEnvironment.Clone()
        $parameterMatrixPresetKRelEnvironment["SE_DLSS_RUNTIME_FLAVOR"] = "rel"
        $parameterMatrixPresetMRelNoOptionalMasksEnvironment =
            $defaultSceneDlaaHighResolutionPresetMRelRuntimeDiagnosticsEnvironment.Clone()
        $parameterMatrixPresetMRelNoOptionalMasksEnvironment["SE_DLSS_DISABLE_OPTIONAL_MASK_BINDINGS"] =
            "1"
        $parameterMatrixPresetMMovingCameraRelEnvironment =
            $defaultSceneDlaaPresetMMotionResourceDiagnosticsEnvironment.Clone()
        Set-HighResolutionVisualQaEnvironment `
            -Environment $parameterMatrixPresetMMovingCameraRelEnvironment
        $parameterMatrixPresetMMovingCameraRelEnvironment["SE_DLSS_RUNTIME_FLAVOR"] =
            "rel"

        $quickSummary["lanes"]["mNgxParameterMatrixPresetKRelStatic"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_parameter_matrix_preset_k_rel_static_present" `
                -LaneKey "mNgxParameterMatrix.presetKRelStatic" `
                -Environment $parameterMatrixPresetKRelEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticAllMasks"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_parameter_matrix_preset_m_rel_static_all_masks_present" `
                -LaneKey "mNgxParameterMatrix.presetMRelStaticAllMasks" `
                -Environment $defaultSceneDlaaHighResolutionPresetMRelRuntimeDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMInternalResourceAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticNoOptionalMasks"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_parameter_matrix_preset_m_rel_static_no_optional_masks_present" `
                -LaneKey "mNgxParameterMatrix.presetMRelStaticNoOptionalMasks" `
                -Environment $parameterMatrixPresetMRelNoOptionalMasksEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMInternalResourceAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelMovingCameraAllMasks"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_parameter_matrix_preset_m_rel_moving_camera_all_masks_present" `
                -LaneKey "mNgxParameterMatrix.presetMRelMovingCameraAllMasks" `
                -Environment $parameterMatrixPresetMMovingCameraRelEnvironment `
                -Manifest $defaultSceneDlaaMotionBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMInternalResourceAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxParameterMatrixPresetMDevStaticAllMasks"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_parameter_matrix_preset_m_dev_static_all_masks_present" `
                -LaneKey "mNgxParameterMatrix.presetMDevStaticAllMasks" `
                -Environment $defaultSceneDlaaHighResolutionPresetMDevRuntimeDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMInternalResourceAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $parameterMatrixKRelTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxParameterMatrixPresetKRelStatic"]
        $parameterMatrixMRelAllTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticAllMasks"]
        $parameterMatrixMRelNoOptionalTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticNoOptionalMasks"]
        $parameterMatrixMRelMovingCameraTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelMovingCameraAllMasks"]
        $parameterMatrixMDevAllTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxParameterMatrixPresetMDevStaticAllMasks"]

        $parameterMatrixKRelMetrics =
            $quickSummary["lanes"]["mNgxParameterMatrixPresetKRelStatic"]["metrics"]
        $parameterMatrixMRelAllMetrics =
            $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticAllMasks"]["metrics"]
        $parameterMatrixMRelNoOptionalMetrics =
            $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticNoOptionalMasks"]["metrics"]
        $parameterMatrixMRelMovingCameraMetrics =
            $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelMovingCameraAllMasks"]["metrics"]
        $parameterMatrixMDevAllMetrics =
            $quickSummary["lanes"]["mNgxParameterMatrixPresetMDevStaticAllMasks"]["metrics"]

        if ($parameterMatrixKRelMetrics.recommendedPreset -ne "11") {
            throw "mNgxParameterMatrix preset K rel expected preset 11, got $($parameterMatrixKRelMetrics.recommendedPreset)"
        }
        foreach ($mMetrics in @(
                $parameterMatrixMRelAllMetrics,
                $parameterMatrixMRelNoOptionalMetrics,
                $parameterMatrixMRelMovingCameraMetrics,
                $parameterMatrixMDevAllMetrics
            )) {
            if ($mMetrics.recommendedPreset -ne "13") {
                throw "mNgxParameterMatrix preset M expected preset 13, got $($mMetrics.recommendedPreset)"
            }
        }

        $parameterMatrixLanes = [ordered]@{
            presetKRelStatic =
                New-DlssNgxParameterMatrixLaneSummary `
                    -Label "K preset, rel runtime, static fullscreen clean control" `
                    -Variant @{
                        preset = "k"
                        runtimeFlavor = "rel"
                        optionalMasks = "all"
                        motion = "static"
                        resolution = "monitor"
                    } `
                    -Lane $quickSummary["lanes"]["mNgxParameterMatrixPresetKRelStatic"] `
                    -Trace $parameterMatrixKRelTrace
            presetMRelStaticAllMasks =
                New-DlssNgxParameterMatrixLaneSummary `
                    -Label "M preset, rel runtime, static fullscreen, all optional masks" `
                    -Variant @{
                        preset = "m"
                        runtimeFlavor = "rel"
                        optionalMasks = "all"
                        motion = "static"
                        resolution = "monitor"
                    } `
                    -Lane $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticAllMasks"] `
                    -Trace $parameterMatrixMRelAllTrace
            presetMRelStaticNoOptionalMasks =
                New-DlssNgxParameterMatrixLaneSummary `
                    -Label "M preset, rel runtime, static fullscreen, no optional masks" `
                    -Variant @{
                        preset = "m"
                        runtimeFlavor = "rel"
                        optionalMasks = "none"
                        motion = "static"
                        resolution = "monitor"
                    } `
                    -Lane $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticNoOptionalMasks"] `
                    -Trace $parameterMatrixMRelNoOptionalTrace
            presetMRelMovingCameraAllMasks =
                New-DlssNgxParameterMatrixLaneSummary `
                    -Label "M preset, rel runtime, moving camera fullscreen, all optional masks" `
                    -Variant @{
                        preset = "m"
                        runtimeFlavor = "rel"
                        optionalMasks = "all"
                        motion = "moving-camera"
                        resolution = "monitor"
                    } `
                    -Lane $quickSummary["lanes"]["mNgxParameterMatrixPresetMRelMovingCameraAllMasks"] `
                    -Trace $parameterMatrixMRelMovingCameraTrace
            presetMDevStaticAllMasks =
                New-DlssNgxParameterMatrixLaneSummary `
                    -Label "M preset, dev runtime, static fullscreen, all optional masks" `
                    -Variant @{
                        preset = "m"
                        runtimeFlavor = "dev"
                        optionalMasks = "all"
                        motion = "static"
                        resolution = "monitor"
                    } `
                    -Lane $quickSummary["lanes"]["mNgxParameterMatrixPresetMDevStaticAllMasks"] `
                    -Trace $parameterMatrixMDevAllTrace
        }

        $kRelClean =
            [bool]$quickSummary["lanes"]["mNgxParameterMatrixPresetKRelStatic"]["validationClean"]
        $mRelAllClean =
            [bool]$quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticAllMasks"]["validationClean"]
        $mRelNoOptionalClean =
            [bool]$quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticNoOptionalMasks"]["validationClean"]
        $mRelMovingCameraClean =
            [bool]$quickSummary["lanes"]["mNgxParameterMatrixPresetMRelMovingCameraAllMasks"]["validationClean"]
        $mDevAllClean =
            [bool]$quickSummary["lanes"]["mNgxParameterMatrixPresetMDevStaticAllMasks"]["validationClean"]
        $mRelAllDiagnostic =
            [bool]$quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticAllMasks"]["ngxResourceLayoutDiagnostic"]
        $mRelNoOptionalDiagnostic =
            [bool]$quickSummary["lanes"]["mNgxParameterMatrixPresetMRelStaticNoOptionalMasks"]["ngxResourceLayoutDiagnostic"]
        $mRelMovingCameraDiagnostic =
            [bool]$quickSummary["lanes"]["mNgxParameterMatrixPresetMRelMovingCameraAllMasks"]["ngxResourceLayoutDiagnostic"]
        $mDevAllDiagnostic =
            [bool]$quickSummary["lanes"]["mNgxParameterMatrixPresetMDevStaticAllMasks"]["ngxResourceLayoutDiagnostic"]
        $optionalMaskChangedValidation =
            ($mRelNoOptionalClean -ne $mRelAllClean)
        $optionalMaskChangedDiagnostic =
            ($mRelNoOptionalDiagnostic -ne $mRelAllDiagnostic)
        $motionChangedValidation =
            ($mRelMovingCameraClean -ne $mRelAllClean)
        $motionChangedDiagnostic =
            ($mRelMovingCameraDiagnostic -ne $mRelAllDiagnostic)
        $runtimeChangedValidation =
            ($mDevAllClean -ne $mRelAllClean)
        $runtimeChangedDiagnostic =
            ($mDevAllDiagnostic -ne $mRelAllDiagnostic)
        $mWarningResourceNames =
            Get-UniqueSortedText -Values @(
                @($parameterMatrixMRelAllTrace["warningResourceNames"]) +
                @($parameterMatrixMRelNoOptionalTrace["warningResourceNames"]) +
                @($parameterMatrixMRelMovingCameraTrace["warningResourceNames"]) +
                @($parameterMatrixMDevAllTrace["warningResourceNames"])
            )
        $mWarningOverlapCounts = [ordered]@{
            presetMRelStaticAllMasks =
                $parameterMatrixMRelAllTrace["warningHandleOverlapCount"]
            presetMRelStaticNoOptionalMasks =
                $parameterMatrixMRelNoOptionalTrace["warningHandleOverlapCount"]
            presetMRelMovingCameraAllMasks =
                $parameterMatrixMRelMovingCameraTrace["warningHandleOverlapCount"]
            presetMDevStaticAllMasks =
                $parameterMatrixMDevAllTrace["warningHandleOverlapCount"]
        }
        $mWarningOverlapsSelfEngineResource =
            ([int]$parameterMatrixMRelAllTrace["warningHandleOverlapCount"] -gt 0 -or
             [int]$parameterMatrixMRelNoOptionalTrace["warningHandleOverlapCount"] -gt 0 -or
             [int]$parameterMatrixMRelMovingCameraTrace["warningHandleOverlapCount"] -gt 0 -or
             [int]$parameterMatrixMDevAllTrace["warningHandleOverlapCount"] -gt 0)
        $parameterAttribution =
            if ($optionalMaskChangedDiagnostic -or $optionalMaskChangedValidation) {
                "optional-mask-binding-sensitive"
            } elseif ($motionChangedDiagnostic -or $motionChangedValidation) {
                "motion-state-sensitive"
            } elseif ($runtimeChangedDiagnostic -or $runtimeChangedValidation) {
                "runtime-flavor-sensitive"
            } elseif ($mRelAllDiagnostic -and $mRelNoOptionalDiagnostic -and
                $mRelMovingCameraDiagnostic -and $mDevAllDiagnostic -and
                -not $mWarningOverlapsSelfEngineResource) {
                "preset-M-internal-resource-path"
            } else {
                "mixed-or-inconclusive"
            }

        $compactMatrix = [ordered]@{
            target = $Target
            generatedAt = (Get-Date).ToString("o")
            scenario =
                "Default Forward 3D application scene, DLAA, borderless monitor-resolution fullscreen on the configured capture monitor, hidden ImGui, resource diagnostics enabled."
            captureMonitor = $script:captureMonitorWorkArea
            lanes = $parameterMatrixLanes
            conclusion = [ordered]@{
                productionCandidate = $false
                productionBlocked = $true
                validationClean =
                    ($kRelClean -and $mRelAllClean -and
                     $mRelNoOptionalClean -and $mRelMovingCameraClean -and
                     $mDevAllClean)
                attribution = $parameterAttribution
                kControlClean = $kRelClean
                presetMRelClean = $mRelAllClean
                presetMNoOptionalMasksClean = $mRelNoOptionalClean
                presetMMovingCameraClean = $mRelMovingCameraClean
                presetMDevClean = $mDevAllClean
                optionalMaskChangedValidation = $optionalMaskChangedValidation
                optionalMaskChangedDiagnostic = $optionalMaskChangedDiagnostic
                motionChangedValidation = $motionChangedValidation
                motionChangedDiagnostic = $motionChangedDiagnostic
                runtimeChangedValidation = $runtimeChangedValidation
                runtimeChangedDiagnostic = $runtimeChangedDiagnostic
                warningResourceNames = $mWarningResourceNames
                warningHandleOverlapCounts = $mWarningOverlapCounts
                warningOverlapsSelfEngineResource =
                    $mWarningOverlapsSelfEngineResource
                requiredProductionState = [ordered]@{
                    presetKRelValidationClean = $true
                    presetMRelValidationClean = $true
                    presetMNoOptionalMasksValidationClean = $true
                    presetMMovingCameraValidationClean = $true
                    presetMDevValidationClean = $true
                    presetMNgxResourceDiagnostic = $false
                    presetMWarningHandleOverlapCount = 0
                }
            }
            recommendedNextActions = @(
                "Keep SE_DLSS_PRESET=k as the validation-clean fallback/control.",
                "If attribution is preset-M-internal-resource-path, do not tune sharpness, mip bias, or masks as a production workaround for this blocker.",
                "If attribution changes under an official runtime override, rerun this exact suite and compare runtime DLL provenance before changing defaults.",
                "Do not remove the preset-M production block until all M lanes are validation-clean without allowed diagnostic patterns."
            )
        }

        $compactMatrixPath =
            Join-Path $outputRoot "m_ngx_parameter_matrix_audit.json"
        $compactMatrix |
            ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $compactMatrixPath -Encoding UTF8

        $quickSummary["diagnostics"]["mNgxParameterMatrixAudit"] = [ordered]@{
            validationClean =
                $compactMatrix["conclusion"]["validationClean"]
            diagnosticOnly = $true
            productionCandidate = $false
            compactMatrix = $compactMatrixPath
            attribution =
                $compactMatrix["conclusion"]["attribution"]
            kControlClean =
                $compactMatrix["conclusion"]["kControlClean"]
            presetMRelClean =
                $compactMatrix["conclusion"]["presetMRelClean"]
            presetMNoOptionalMasksClean =
                $compactMatrix["conclusion"]["presetMNoOptionalMasksClean"]
            presetMMovingCameraClean =
                $compactMatrix["conclusion"]["presetMMovingCameraClean"]
            presetMDevClean =
                $compactMatrix["conclusion"]["presetMDevClean"]
            optionalMaskChangedDiagnostic =
                $compactMatrix["conclusion"]["optionalMaskChangedDiagnostic"]
            motionChangedDiagnostic =
                $compactMatrix["conclusion"]["motionChangedDiagnostic"]
            runtimeChangedDiagnostic =
                $compactMatrix["conclusion"]["runtimeChangedDiagnostic"]
            warningResourceNames =
                $compactMatrix["conclusion"]["warningResourceNames"]
            warningHandleOverlapCounts =
                $compactMatrix["conclusion"]["warningHandleOverlapCounts"]
            warningOverlapsSelfEngineResource =
                $compactMatrix["conclusion"]["warningOverlapsSelfEngineResource"]
            requiredProductionState =
                $compactMatrix["conclusion"]["requiredProductionState"]
        }
    }

    if ($selectedSuites -contains "m-ngx-validation-isolation-audit") {
        $quickSummary["baselines"]["mNgxValidationIsolationAudit"] = [ordered]@{
            fullscreenManifest = $defaultSceneDlaaBaselineManifestPath
            fullscreenName = $defaultSceneDlaaBaselineManifest.name
        }
        $quickSummary["thresholds"]["mNgxValidationIsolationAudit"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            validationGate = "known-ngx-internal-layout-isolation"
            captureMode = "single-frame-focused"
            reason =
                "Verify that the engine can isolate the previously attributed preset-M NGX internal layout diagnostic without screenshot-log allowlists and without hiding unknown Vulkan diagnostics."
        }

        $validationIsolationPresetKRelEnvironment =
            $defaultSceneDlaaHighResolutionPresetKResourceDiagnosticsEnvironment.Clone()
        $validationIsolationPresetKRelEnvironment["SE_DLSS_RUNTIME_FLAVOR"] =
            "rel"
        $validationIsolationPresetKRelEnvironment["SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT"] =
            "1"
        $validationIsolationPresetMRelEnvironment =
            $defaultSceneDlaaHighResolutionPresetMRelRuntimeDiagnosticsEnvironment.Clone()
        $validationIsolationPresetMRelEnvironment["SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT"] =
            "1"
        $validationIsolationPresetMDevEnvironment =
            $defaultSceneDlaaHighResolutionPresetMDevRuntimeDiagnosticsEnvironment.Clone()
        $validationIsolationPresetMDevEnvironment["SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT"] =
            "1"

        $quickSummary["lanes"]["mNgxValidationIsolationPresetKRel"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_validation_isolation_preset_k_rel_present" `
                -LaneKey "mNgxValidationIsolation.presetKRel" `
                -Environment $validationIsolationPresetKRelEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxValidationIsolationPresetMRel"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_validation_isolation_preset_m_rel_present" `
                -LaneKey "mNgxValidationIsolation.presetMRel" `
                -Environment $validationIsolationPresetMRelEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxValidationIsolationPresetMDev"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_validation_isolation_preset_m_dev_present" `
                -LaneKey "mNgxValidationIsolation.presetMDev" `
                -Environment $validationIsolationPresetMDevEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $validationIsolationKRelTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxValidationIsolationPresetKRel"]
        $validationIsolationMRelTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxValidationIsolationPresetMRel"]
        $validationIsolationMDevTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxValidationIsolationPresetMDev"]

        $validationIsolationKRelMetrics =
            $quickSummary["lanes"]["mNgxValidationIsolationPresetKRel"]["metrics"]
        $validationIsolationMRelMetrics =
            $quickSummary["lanes"]["mNgxValidationIsolationPresetMRel"]["metrics"]
        $validationIsolationMDevMetrics =
            $quickSummary["lanes"]["mNgxValidationIsolationPresetMDev"]["metrics"]
        if ($validationIsolationKRelMetrics.recommendedPreset -ne "11") {
            throw "mNgxValidationIsolation preset K rel expected preset 11, got $($validationIsolationKRelMetrics.recommendedPreset)"
        }
        if ($validationIsolationMRelMetrics.recommendedPreset -ne "13") {
            throw "mNgxValidationIsolation preset M rel expected preset 13, got $($validationIsolationMRelMetrics.recommendedPreset)"
        }
        if ($validationIsolationMDevMetrics.recommendedPreset -ne "13") {
            throw "mNgxValidationIsolation preset M dev expected preset 13, got $($validationIsolationMDevMetrics.recommendedPreset)"
        }
        if (-not [bool]$quickSummary["lanes"]["mNgxValidationIsolationPresetKRel"]["validationClean"]) {
            throw "mNgxValidationIsolation preset K rel control must stay validation-clean"
        }
        if ([int]$validationIsolationKRelTrace["suppressedNgxInternalLayoutLineCount"] -ne 0) {
            throw "mNgxValidationIsolation preset K rel should not suppress NGX internal layout diagnostics"
        }
        if (-not [bool]$quickSummary["lanes"]["mNgxValidationIsolationPresetMRel"]["validationClean"]) {
            throw "mNgxValidationIsolation preset M rel should be clean after known NGX internal layout isolation"
        }
        if (-not [bool]$quickSummary["lanes"]["mNgxValidationIsolationPresetMDev"]["validationClean"]) {
            throw "mNgxValidationIsolation preset M dev should be clean after known NGX internal layout isolation"
        }
        if ([int]$validationIsolationMRelTrace["suppressedNgxInternalLayoutLineCount"] -le 0) {
            throw "mNgxValidationIsolation preset M rel expected isolated NGX internal layout evidence"
        }
        if ([int]$validationIsolationMDevTrace["suppressedNgxInternalLayoutLineCount"] -le 0) {
            throw "mNgxValidationIsolation preset M dev expected isolated NGX internal layout evidence"
        }

        $validationIsolationLanes = [ordered]@{
            presetKRel =
                New-DlssNgxParameterMatrixLaneSummary `
                    -Label "K preset, rel runtime, isolation policy enabled clean control" `
                    -Variant @{
                        preset = "k"
                        runtimeFlavor = "rel"
                        isolationPolicy = "enabled"
                    } `
                    -Lane $quickSummary["lanes"]["mNgxValidationIsolationPresetKRel"] `
                    -Trace $validationIsolationKRelTrace
            presetMRel =
                New-DlssNgxParameterMatrixLaneSummary `
                    -Label "M preset, rel runtime, known NGX internal layout isolated" `
                    -Variant @{
                        preset = "m"
                        runtimeFlavor = "rel"
                        isolationPolicy = "enabled"
                    } `
                    -Lane $quickSummary["lanes"]["mNgxValidationIsolationPresetMRel"] `
                    -Trace $validationIsolationMRelTrace
            presetMDev =
                New-DlssNgxParameterMatrixLaneSummary `
                    -Label "M preset, dev runtime, known NGX internal layout isolated" `
                    -Variant @{
                        preset = "m"
                        runtimeFlavor = "dev"
                        isolationPolicy = "enabled"
                    } `
                    -Lane $quickSummary["lanes"]["mNgxValidationIsolationPresetMDev"] `
                    -Trace $validationIsolationMDevTrace
        }

        $validationIsolationWarningNames =
            Get-UniqueSortedText -Values @(
                @($validationIsolationMRelTrace["suppressedNgxInternalLayoutResourceNames"]) +
                @($validationIsolationMDevTrace["suppressedNgxInternalLayoutResourceNames"])
            )
        $compactIsolation = [ordered]@{
            target = $Target
            generatedAt = (Get-Date).ToString("o")
            scenario =
                "Default Forward 3D application scene, DLAA, borderless monitor-resolution fullscreen, hidden ImGui, SelfEngine resource diagnostics enabled, known NGX internal layout isolation policy enabled."
            captureMonitor = $script:captureMonitorWorkArea
            policy = [ordered]@{
                environment = "SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT=1"
                scope =
                    "Only vkQueueSubmit descriptor-layout VUID-vkCmdDraw-None-09600 messages on nv.ngx.dlss.* resources expecting GENERAL while tracked as UNDEFINED."
                defaultEnabled = $false
                productionInterpretation =
                    "Isolation evidence is cleaner than screenshot allowlists, but M production still requires accepting this as an explained NGX-owned diagnostic policy plus repeat dynamic stability."
            }
            lanes = $validationIsolationLanes
            conclusion = [ordered]@{
                productionCandidate = $false
                validationClean =
                    ([bool]$quickSummary["lanes"]["mNgxValidationIsolationPresetKRel"]["validationClean"] -and
                     [bool]$quickSummary["lanes"]["mNgxValidationIsolationPresetMRel"]["validationClean"] -and
                     [bool]$quickSummary["lanes"]["mNgxValidationIsolationPresetMDev"]["validationClean"])
                isolationPolicyReady =
                    ([int]$validationIsolationKRelTrace["suppressedNgxInternalLayoutLineCount"] -eq 0 -and
                     [int]$validationIsolationMRelTrace["suppressedNgxInternalLayoutLineCount"] -gt 0 -and
                     [int]$validationIsolationMDevTrace["suppressedNgxInternalLayoutLineCount"] -gt 0)
                kControlClean =
                    [bool]$quickSummary["lanes"]["mNgxValidationIsolationPresetKRel"]["validationClean"]
                presetMRelClean =
                    [bool]$quickSummary["lanes"]["mNgxValidationIsolationPresetMRel"]["validationClean"]
                presetMDevClean =
                    [bool]$quickSummary["lanes"]["mNgxValidationIsolationPresetMDev"]["validationClean"]
                suppressedResourceNames = $validationIsolationWarningNames
                suppressedLineCounts = [ordered]@{
                    presetKRel =
                        $validationIsolationKRelTrace["suppressedNgxInternalLayoutLineCount"]
                    presetMRel =
                        $validationIsolationMRelTrace["suppressedNgxInternalLayoutLineCount"]
                    presetMDev =
                        $validationIsolationMDevTrace["suppressedNgxInternalLayoutLineCount"]
                }
                remainingProductionBlockers = @(
                    "Need repeat high-resolution dynamic acceptance under the chosen validation policy.",
                    "Need explicit decision that the isolated NGX-owned internal-resource diagnostic is acceptable, or an official runtime/SDK that removes it without isolation."
                )
                requiredProductionState = [ordered]@{
                    strictWithoutIsolationValidationClean = "preferred"
                    isolatedValidationClean = $true
                    kControlSuppressedCount = 0
                    presetMRelSuppressedCount = ">0 documented"
                    presetMDevSuppressedCount = ">0 documented"
                    unknownVulkanDiagnostics = 0
                    repeatHighResolutionDynamicReady = $true
                }
            }
            recommendedNextActions = @(
                "Keep the isolation policy disabled by default until production policy is decided.",
                "Run m-highres-dynamic with the isolation policy only after this suite stays clean.",
                "Prefer an official runtime/SDK update if one removes the diagnostic without isolation.",
                "Do not treat isolated validation cleanliness alone as production image-quality acceptance."
            )
        }

        $compactIsolationPath =
            Join-Path $outputRoot "m_ngx_validation_isolation_audit.json"
        $compactIsolation |
            ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $compactIsolationPath -Encoding UTF8

        $quickSummary["diagnostics"]["mNgxValidationIsolationAudit"] = [ordered]@{
            validationClean =
                $compactIsolation["conclusion"]["validationClean"]
            diagnosticOnly = $true
            productionCandidate = $false
            compactIsolation = $compactIsolationPath
            isolationPolicyReady =
                $compactIsolation["conclusion"]["isolationPolicyReady"]
            kControlClean =
                $compactIsolation["conclusion"]["kControlClean"]
            presetMRelClean =
                $compactIsolation["conclusion"]["presetMRelClean"]
            presetMDevClean =
                $compactIsolation["conclusion"]["presetMDevClean"]
            suppressedResourceNames =
                $compactIsolation["conclusion"]["suppressedResourceNames"]
            suppressedLineCounts =
                $compactIsolation["conclusion"]["suppressedLineCounts"]
            remainingProductionBlockers =
                $compactIsolation["conclusion"]["remainingProductionBlockers"]
            requiredProductionState =
                $compactIsolation["conclusion"]["requiredProductionState"]
        }
    }

    if ($selectedSuites -contains "m-ngx-internal-mv-repro") {
        $hasCustomDlssRuntimeOverride =
            $externalDlssRuntimeOverridePath.Length -gt 0
        if ($hasCustomDlssRuntimeOverride) {
            $customRuntimeDll =
                Join-Path $externalDlssRuntimeOverridePath "nvngx_dlss.dll"
            if (!(Test-Path -LiteralPath $customRuntimeDll)) {
                throw "Custom DLSS runtime override DLL not found: $customRuntimeDll"
            }
        }

        $quickSummary["baselines"]["mNgxInternalMvRepro"] = [ordered]@{
            fullscreenManifest = $defaultSceneDlaaBaselineManifestPath
            fullscreenName = $defaultSceneDlaaBaselineManifest.name
            customRuntimePath =
                if ($hasCustomDlssRuntimeOverride) {
                    $externalDlssRuntimeOverridePath
                } else {
                    ""
                }
        }
        $quickSummary["thresholds"]["mNgxInternalMvRepro"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            sequenceGate = "record-only"
            validationGate = "compact-repro-attribution"
            reason =
                "Package K-rel, M-rel, and M-dev fullscreen evidence for the preset-M NGX-owned previous-motion-vector layout diagnostic."
        }

        $mNgxInternalMvReproPresetKRelEnvironment =
            $defaultSceneDlaaHighResolutionPresetKResourceDiagnosticsEnvironment.Clone()
        $mNgxInternalMvReproPresetKRelEnvironment["SE_DLSS_RUNTIME_FLAVOR"] =
            "rel"

        $quickSummary["lanes"]["mNgxInternalMvReproPresetKRel"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_internal_mv_repro_preset_k_rel_present" `
                -LaneKey "mNgxInternalMvRepro.presetKRel" `
                -Environment $mNgxInternalMvReproPresetKRelEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxInternalMvReproPresetMRel"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_internal_mv_repro_preset_m_rel_present" `
                -LaneKey "mNgxInternalMvRepro.presetMRel" `
                -Environment $defaultSceneDlaaHighResolutionPresetMRelRuntimeDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMInternalResourceAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mNgxInternalMvReproPresetMDev"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_internal_mv_repro_preset_m_dev_present" `
                -LaneKey "mNgxInternalMvRepro.presetMDev" `
                -Environment $defaultSceneDlaaHighResolutionPresetMDevRuntimeDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMInternalResourceAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        if ($hasCustomDlssRuntimeOverride) {
            $quickSummary["lanes"]["mNgxInternalMvReproPresetMCustomRuntime"] =
                Invoke-QuickDlssMaskDiagnosticLane `
                    -Name "m_ngx_internal_mv_repro_preset_m_custom_runtime_present" `
                    -LaneKey "mNgxInternalMvRepro.presetMCustomRuntime" `
                    -Environment $defaultSceneDlaaHighResolutionPresetMCustomRuntimeDiagnosticsEnvironment `
                    -Manifest $defaultSceneDlaaBaselineManifest `
                    -AllowedCaptureDiagnosticPatterns $dlssPresetMInternalResourceAllowedDiagnosticPatterns `
                    -UseMonitorResolutionCapture `
                    -UseSequenceCapture `
                    -ExpectedDlssWidth $highResolutionWindowWidth `
                    -ExpectedDlssHeight $highResolutionWindowHeight
        }

        $presetKRelTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxInternalMvReproPresetKRel"]
        $presetMRelTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxInternalMvReproPresetMRel"]
        $presetMDevTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxInternalMvReproPresetMDev"]
        $presetMCustomRuntimeTrace = $null
        if ($hasCustomDlssRuntimeOverride) {
            $presetMCustomRuntimeTrace =
                Get-DlssTraceLogSummary `
                    -Lane $quickSummary["lanes"]["mNgxInternalMvReproPresetMCustomRuntime"]
        }

        $presetKRelMetrics =
            $quickSummary["lanes"]["mNgxInternalMvReproPresetKRel"]["metrics"]
        $presetMRelMetrics =
            $quickSummary["lanes"]["mNgxInternalMvReproPresetMRel"]["metrics"]
        $presetMDevMetrics =
            $quickSummary["lanes"]["mNgxInternalMvReproPresetMDev"]["metrics"]
        $presetMCustomRuntimeMetrics = $null
        if ($hasCustomDlssRuntimeOverride) {
            $presetMCustomRuntimeMetrics =
                $quickSummary["lanes"]["mNgxInternalMvReproPresetMCustomRuntime"]["metrics"]
        }
        if ($presetKRelMetrics.recommendedPreset -ne "11") {
            throw "mNgxInternalMvRepro preset K rel expected preset 11, got $($presetKRelMetrics.recommendedPreset)"
        }
        if ($presetMRelMetrics.recommendedPreset -ne "13") {
            throw "mNgxInternalMvRepro preset M rel expected preset 13, got $($presetMRelMetrics.recommendedPreset)"
        }
        if ($presetMDevMetrics.recommendedPreset -ne "13") {
            throw "mNgxInternalMvRepro preset M dev expected preset 13, got $($presetMDevMetrics.recommendedPreset)"
        }
        if ($hasCustomDlssRuntimeOverride) {
            if ($presetMCustomRuntimeMetrics.recommendedPreset -ne "13") {
                throw "mNgxInternalMvRepro preset M custom runtime expected preset 13, got $($presetMCustomRuntimeMetrics.recommendedPreset)"
            }
            if ($presetMCustomRuntimeMetrics.runtimePathOverridden -ne "1") {
                throw "mNgxInternalMvRepro preset M custom runtime expected runtimePathOverridden=1, got $($presetMCustomRuntimeMetrics.runtimePathOverridden)"
            }
            if ($presetMCustomRuntimeMetrics.runtimeDllFound -ne "1") {
                throw "mNgxInternalMvRepro preset M custom runtime expected runtimeDllFound=1, got $($presetMCustomRuntimeMetrics.runtimeDllFound)"
            }
        }

        $mWarningResourceNames =
            Get-UniqueSortedText -Values @(
                @($presetMRelTrace["warningResourceNames"]) +
                @($presetMDevTrace["warningResourceNames"]) +
                @(
                    if ($hasCustomDlssRuntimeOverride) {
                        $presetMCustomRuntimeTrace["warningResourceNames"]
                    }
                )
            )
        $reproLanes = [ordered]@{
            presetKRel =
                New-DlssNgxInternalMvReproLaneSummary `
                    -Label "K preset, rel runtime clean control" `
                    -Lane $quickSummary["lanes"]["mNgxInternalMvReproPresetKRel"] `
                    -Trace $presetKRelTrace
            presetMRel =
                New-DlssNgxInternalMvReproLaneSummary `
                    -Label "M preset, rel runtime default diagnostic" `
                    -Lane $quickSummary["lanes"]["mNgxInternalMvReproPresetMRel"] `
                    -Trace $presetMRelTrace
            presetMDev =
                New-DlssNgxInternalMvReproLaneSummary `
                    -Label "M preset, dev runtime named-resource diagnostic" `
                    -Lane $quickSummary["lanes"]["mNgxInternalMvReproPresetMDev"] `
                    -Trace $presetMDevTrace
        }
        if ($hasCustomDlssRuntimeOverride) {
            $reproLanes["presetMCustomRuntime"] =
                New-DlssNgxInternalMvReproLaneSummary `
                    -Label "M preset, custom runtime override diagnostic" `
                    -Lane $quickSummary["lanes"]["mNgxInternalMvReproPresetMCustomRuntime"] `
                    -Trace $presetMCustomRuntimeTrace
        }
        $compactRepro = [ordered]@{
            target = $Target
            generatedAt = (Get-Date).ToString("o")
            scenario =
                "Default Forward 3D application scene, DLAA, borderless monitor-resolution fullscreen, hidden ImGui, resource diagnostics enabled."
            captureMonitor = $script:captureMonitorWorkArea
            expectedCleanControl =
                "Preset K rel runtime should stay validation-clean under the same fullscreen sequence and SelfEngine resource tracing."
            customRuntimePath =
                if ($hasCustomDlssRuntimeOverride) {
                    $externalDlssRuntimeOverridePath
                } else {
                    ""
                }
            lanes = $reproLanes
            conclusion = [ordered]@{
                productionCandidate = $false
                productionBlocked = $true
                validationClean =
                    ([bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetKRel"]["validationClean"] -and
                     [bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetMRel"]["validationClean"] -and
                     [bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetMDev"]["validationClean"] -and
                     (
                        -not $hasCustomDlssRuntimeOverride -or
                        [bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetMCustomRuntime"]["validationClean"]
                     ))
                attribution =
                    "Preset-M-specific NGX-owned internal previous-motion-vector resource layout diagnostic under a validation-clean K control."
                kControlClean =
                    [bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetKRel"]["validationClean"]
                presetMRelClean =
                    [bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetMRel"]["validationClean"]
                presetMDevClean =
                    [bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetMDev"]["validationClean"]
                presetMCustomRuntimeClean =
                    if ($hasCustomDlssRuntimeOverride) {
                        [bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetMCustomRuntime"]["validationClean"]
                    } else {
                        $null
                    }
                relDevRuntimeIsProductionWorkaround =
                    ([bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetMDev"]["validationClean"] -and
                     -not [bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetMRel"]["validationClean"])
                customRuntimeChangedValidation =
                    if ($hasCustomDlssRuntimeOverride) {
                        ([bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetMCustomRuntime"]["validationClean"] -ne
                         [bool]$quickSummary["lanes"]["mNgxInternalMvReproPresetMRel"]["validationClean"])
                    } else {
                        $null
                    }
                warningResourceNames = $mWarningResourceNames
                relWarningOverlapCount =
                    $presetMRelTrace["warningHandleOverlapCount"]
                devWarningOverlapCount =
                    $presetMDevTrace["warningHandleOverlapCount"]
                customRuntimeWarningOverlapCount =
                    if ($hasCustomDlssRuntimeOverride) {
                        $presetMCustomRuntimeTrace["warningHandleOverlapCount"]
                    } else {
                        $null
                    }
                requiredProductionState = [ordered]@{
                    presetMRelValidationClean = $true
                    presetMDevValidationClean = $true
                    presetMCustomRuntimeValidationClean =
                        if ($hasCustomDlssRuntimeOverride) { $true } else { $null }
                    presetMRelNgxResourceDiagnostic = $false
                    presetMDevNgxResourceDiagnostic = $false
                    presetMCustomRuntimeNgxResourceDiagnostic =
                        if ($hasCustomDlssRuntimeOverride) { $false } else { $null }
                    presetMWarningHandleOverlapCount = 0
                }
            }
            recommendedNextActions = @(
                "Keep SE_DLSS_PRESET=k as the validation-clean fallback/control.",
                "Use SE_DLSS_RUNTIME_FLAVOR=dev only for diagnostic attribution, not as a production workaround.",
                "Set -DlssRuntimeOverridePath or SE_DLSS_RUNTIME_PATH to an official runtime drop and rerun this exact suite before changing production defaults.",
                "Do not remove the preset-M production block until rel/dev captures are validation-clean without a whitelist."
            )
        }
        $compactReproPath =
            Join-Path $outputRoot "m_ngx_internal_mv_repro.json"
        $compactRepro |
            ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $compactReproPath -Encoding UTF8

        $quickSummary["diagnostics"]["mNgxInternalMvRepro"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $true
            productionCandidate = $false
            compactRepro = $compactReproPath
            attribution =
                $compactRepro["conclusion"]["attribution"]
            kControlClean =
                $compactRepro["conclusion"]["kControlClean"]
            presetMRelClean =
                $compactRepro["conclusion"]["presetMRelClean"]
            presetMDevClean =
                $compactRepro["conclusion"]["presetMDevClean"]
            presetMCustomRuntimeClean =
                $compactRepro["conclusion"]["presetMCustomRuntimeClean"]
            customRuntimePath =
                $compactRepro["customRuntimePath"]
            customRuntimeChangedValidation =
                $compactRepro["conclusion"]["customRuntimeChangedValidation"]
            warningResourceNames =
                $compactRepro["conclusion"]["warningResourceNames"]
            relWarningOverlapCount =
                $compactRepro["conclusion"]["relWarningOverlapCount"]
            devWarningOverlapCount =
                $compactRepro["conclusion"]["devWarningOverlapCount"]
            customRuntimeWarningOverlapCount =
                $compactRepro["conclusion"]["customRuntimeWarningOverlapCount"]
            requiredProductionState =
                $compactRepro["conclusion"]["requiredProductionState"]
        }
    }

    if ($selectedSuites -contains "m-ngx-runtime-override-audit") {
        $overrideRuntimeDll =
            Join-Path $dlssRuntimeOverrideAuditPath "nvngx_dlss.dll"
        if (!(Test-Path -LiteralPath $overrideRuntimeDll)) {
            throw "DLSS runtime override audit DLL not found: $overrideRuntimeDll"
        }

        $quickSummary["baselines"]["mNgxRuntimeOverrideAudit"] = [ordered]@{
            fullscreenManifest = $defaultSceneDlaaBaselineManifestPath
            fullscreenName = $defaultSceneDlaaBaselineManifest.name
            overrideRuntimePath = $dlssRuntimeOverrideAuditPath
            overrideRuntimeDll = $overrideRuntimeDll
        }
        $quickSummary["thresholds"]["mNgxRuntimeOverrideAudit"] = [ordered]@{
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            validationGate = "diagnostic-provenance"
            reason =
                "Verify SE_DLSS_RUNTIME_PATH can select an explicit runtime directory and records DLL provenance before or during official runtime update tests."
        }

        $quickSummary["lanes"]["mNgxRuntimeOverrideAuditPresetMRel"] =
            Invoke-QuickDlssMaskDiagnosticLane `
                -Name "m_ngx_runtime_override_audit_preset_m_rel_present" `
                -LaneKey "mNgxRuntimeOverrideAudit.presetMRel" `
                -Environment $defaultSceneDlaaHighResolutionPresetMRelRuntimeOverrideDiagnosticsEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMInternalResourceAllowedDiagnosticPatterns `
                -UseMonitorResolutionCapture `
                -UseSequenceCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $overrideTrace =
            Get-DlssTraceLogSummary `
                -Lane $quickSummary["lanes"]["mNgxRuntimeOverrideAuditPresetMRel"]
        $overrideMetrics =
            $quickSummary["lanes"]["mNgxRuntimeOverrideAuditPresetMRel"]["metrics"]

        if ($overrideMetrics.runtimePathOverridden -ne "1") {
            throw "mNgxRuntimeOverrideAudit expected runtimePathOverridden=1, got $($overrideMetrics.runtimePathOverridden)"
        }
        if ($overrideMetrics.runtimePathFound -ne "1") {
            throw "mNgxRuntimeOverrideAudit expected runtimePathFound=1, got $($overrideMetrics.runtimePathFound)"
        }
        if ($overrideMetrics.runtimeDllFound -ne "1") {
            throw "mNgxRuntimeOverrideAudit expected runtimeDllFound=1, got $($overrideMetrics.runtimeDllFound)"
        }
        if ([uint64]$overrideMetrics.runtimeDllSizeBytes -eq 0) {
            throw "mNgxRuntimeOverrideAudit expected nonzero runtime DLL size"
        }
        if ([uint64]$overrideMetrics.runtimeDllHash -eq 0) {
            throw "mNgxRuntimeOverrideAudit expected nonzero runtime DLL hash"
        }
        if ($overrideMetrics.recommendedPreset -ne "13") {
            throw "mNgxRuntimeOverrideAudit expected preset 13, got $($overrideMetrics.recommendedPreset)"
        }

        $quickSummary["diagnostics"]["mNgxRuntimeOverrideAudit"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $true
            productionCandidate = $false
            runtimeOverrideReady = $true
            runtimePathOverridden = $overrideMetrics.runtimePathOverridden
            runtimeFlavor = $overrideMetrics.runtimeFlavor
            runtimePathFound = $overrideMetrics.runtimePathFound
            runtimePath = $overrideMetrics.runtimePath
            runtimeDllFound = $overrideMetrics.runtimeDllFound
            runtimeDllSizeBytes = $overrideMetrics.runtimeDllSizeBytes
            runtimeDllHash = $overrideMetrics.runtimeDllHash
            tracedPathOverriddenValues =
                $overrideTrace["runtimePathOverriddenValues"]
            tracedRuntimePaths = $overrideTrace["runtimePaths"]
            tracedDllHashes = $overrideTrace["runtimeDllHashValues"]
            warningResourceNames = $overrideTrace["warningResourceNames"]
            warningHandleOverlapCount =
                $overrideTrace["warningHandleOverlapCount"]
            usage =
                "Set SE_DLSS_RUNTIME_PATH to an unpacked official runtime directory containing nvngx_dlss.dll, then rerun m-ngx-internal-mv-repro or this audit."
            requiredProductionState = [ordered]@{
                presetMValidationClean = $true
                presetMNgxResourceDiagnostic = $false
                runtimePathOverridden = "0-or-documented-official-runtime"
            }
        }
    }

    if ($selectedSuites -contains "m-vs-k-subjective-pack") {
        $quickSummary["baselines"]["mVsKSubjectivePack"] = [ordered]@{
            manifest = $defaultSceneDlaaBaselineManifestPath
            name = $defaultSceneDlaaBaselineManifest.name
        }
        $quickSummary["thresholds"]["mVsKSubjectivePack"] = [ordered]@{
            comparisonGate = "record-only"
            reason =
                "Subjective K/M/native pack records still-edge metrics without reusing production absolute thresholds"
        }

        $mVsKNativeBenchmark = Invoke-BenchmarkRun `
            -Name "m_vs_k_subjective_native_deferred_hdr" `
            -Environment $defaultSceneDlaaNativeEnvironment `
            -UseApplicationScene
        $mVsKNativeMetrics = Assert-QuickNativeRow `
            -Name "mVsKSubjectivePack.native" `
            -Row $mVsKNativeBenchmark.LastRow `
            -Expected $defaultSceneDlaaBaselineManifest.expected.native
        $mVsKNativeImage = Capture-WindowImage `
            -Name "m_vs_k_subjective_native_deferred_hdr" `
            -Environment $defaultSceneDlaaNativeEnvironment
        $mVsKNativeImageStats = Get-ImageVariationStats -Path $mVsKNativeImage
        Assert-QuickImageStats `
            -Name "mVsKSubjectivePack.native" `
            -Stats $mVsKNativeImageStats `
            -Manifest $defaultSceneDlaaBaselineManifest

        $quickSummary["lanes"]["mVsKSubjectivePackNative"] =
            New-QuickLaneSummary `
                -Benchmark $mVsKNativeBenchmark `
                -Metrics $mVsKNativeMetrics `
                -Images @($mVsKNativeImage) `
                -ImageStats @($mVsKNativeImageStats)
        $quickSummary["lanes"]["mVsKSubjectivePackPresetK"] =
            Invoke-QuickDlaaTuningLane `
                -Name "m_vs_k_subjective_preset_k_present" `
                -LaneKey "mVsKSubjectivePack.presetK" `
                -Environment $defaultSceneDlaaPresetKEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "11" `
                -NativeImage $mVsKNativeImage `
                -SkipComparisonGate
        $quickSummary["lanes"]["mVsKSubjectivePackPresetM"] =
            Invoke-QuickDlaaTuningLane `
                -Name "m_vs_k_subjective_preset_m_present" `
                -LaneKey "mVsKSubjectivePack.presetM" `
                -Environment $defaultSceneDlaaPresetMEnvironment `
                -Manifest $defaultSceneDlaaBaselineManifest `
                -ExpectedPreset "13" `
                -NativeImage $mVsKNativeImage `
                -SkipComparisonGate `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
        $mVsKPresetKImage =
            $quickSummary["lanes"]["mVsKSubjectivePackPresetK"]["image"]
        $mVsKPresetMImage =
            $quickSummary["lanes"]["mVsKSubjectivePackPresetM"]["image"]
        $quickSummary["diagnostics"]["mVsKSubjectivePack"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $true
            productionCandidate = $false
            presetKValidationClean = $true
            presetMValidationClean = $false
            presetKVsPresetMComparison =
                Compare-Images -A $mVsKPresetKImage -B $mVsKPresetMImage
            presetKVsPresetMEdgeComparison =
                Compare-ImageEdges -A $mVsKPresetKImage -B $mVsKPresetMImage
            allowedKnownCaptureDiagnostic =
                "NGX internal nv.ngx.dlss.resource layout warning for preset M"
        }
    }

    if ($selectedSuites -contains "m-vs-k-dynamic-pack") {
        $quickSummary["baselines"]["mVsKDynamicPack"] = [ordered]@{
            skinnedProductionManifest =
                $defaultSceneSkinnedFbxMProductionBaselineManifestPath
            skinnedProductionName =
                $defaultSceneSkinnedFbxMProductionBaselineManifest.name
            scene =
                "default Forward 3D application scene with Fist Fight B.fbx production skinning"
            model = $importedSkinnedPreviewModelPath
        }
        $quickSummary["thresholds"]["mVsKDynamicPack"] = [ordered]@{
            sequenceGate = "record-only"
            reason =
                "Dynamic native/K/M pack records skinned default-scene adjacent-frame shimmer and same-frame preset deltas without promoting M to production-clean"
            movingCamera =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest
            movingObjectOnly =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest
            combinedCameraObject =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest
        }

        $quickSummary["lanes"]["mVsKDynamicMovingCameraNative"] =
            Invoke-QuickNativeSequenceSuite `
                -Name "m_vs_k_dynamic_moving_camera_native_deferred_hdr" `
                -LaneKey "mVsKDynamic.movingCamera.native" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingCameraNativeEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest
        $quickSummary["lanes"]["mVsKDynamicMovingCameraPresetK"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_vs_k_dynamic_moving_camera_preset_k_present" `
                -LaneKey "mVsKDynamic.movingCamera.presetK" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingCameraPresetKEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate
        $quickSummary["lanes"]["mVsKDynamicMovingCameraPresetM"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_vs_k_dynamic_moving_camera_preset_m_present" `
                -LaneKey "mVsKDynamic.movingCamera.presetM" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingCameraPresetMEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate

        $quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyNative"] =
            Invoke-QuickNativeSequenceSuite `
                -Name "m_vs_k_dynamic_moving_object_only_native_deferred_hdr" `
                -LaneKey "mVsKDynamic.movingObjectOnly.native" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyNativeEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest
        $quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyPresetK"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_vs_k_dynamic_moving_object_only_preset_k_present" `
                -LaneKey "mVsKDynamic.movingObjectOnly.presetK" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetKEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate
        $quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyPresetM"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_vs_k_dynamic_moving_object_only_preset_m_present" `
                -LaneKey "mVsKDynamic.movingObjectOnly.presetM" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate

        $quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectNative"] =
            Invoke-QuickNativeSequenceSuite `
                -Name "m_vs_k_dynamic_combined_camera_object_native_deferred_hdr" `
                -LaneKey "mVsKDynamic.combinedCameraObject.native" `
                -Environment $defaultSceneSkinnedFbxDynamicCombinedNativeEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest
        $quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectPresetK"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_vs_k_dynamic_combined_camera_object_preset_k_present" `
                -LaneKey "mVsKDynamic.combinedCameraObject.presetK" `
                -Environment $defaultSceneSkinnedFbxDynamicCombinedPresetKEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate
        $quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectPresetM"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_vs_k_dynamic_combined_camera_object_preset_m_present" `
                -LaneKey "mVsKDynamic.combinedCameraObject.presetM" `
                -Environment $defaultSceneSkinnedFbxDynamicCombinedPresetMEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate

        $movingCameraNativeImages =
            @($quickSummary["lanes"]["mVsKDynamicMovingCameraNative"]["images"])
        $movingCameraPresetKImages =
            @($quickSummary["lanes"]["mVsKDynamicMovingCameraPresetK"]["images"])
        $movingCameraPresetMImages =
            @($quickSummary["lanes"]["mVsKDynamicMovingCameraPresetM"]["images"])
        $movingObjectOnlyNativeImages =
            @($quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyNative"]["images"])
        $movingObjectOnlyPresetKImages =
            @($quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyPresetK"]["images"])
        $movingObjectOnlyPresetMImages =
            @($quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyPresetM"]["images"])
        $combinedCameraObjectNativeImages =
            @($quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectNative"]["images"])
        $combinedCameraObjectPresetKImages =
            @($quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectPresetK"]["images"])
        $combinedCameraObjectPresetMImages =
            @($quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectPresetM"]["images"])
        $movingCameraNativeSequence =
            $quickSummary["lanes"]["mVsKDynamicMovingCameraNative"]["sequenceComparison"]
        $movingCameraPresetKSequence =
            $quickSummary["lanes"]["mVsKDynamicMovingCameraPresetK"]["sequenceComparison"]
        $movingCameraPresetMSequence =
            $quickSummary["lanes"]["mVsKDynamicMovingCameraPresetM"]["sequenceComparison"]
        $movingObjectOnlyNativeSequence =
            $quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyNative"]["sequenceComparison"]
        $movingObjectOnlyPresetKSequence =
            $quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyPresetK"]["sequenceComparison"]
        $movingObjectOnlyPresetMSequence =
            $quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyPresetM"]["sequenceComparison"]
        $combinedCameraObjectNativeSequence =
            $quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectNative"]["sequenceComparison"]
        $combinedCameraObjectPresetKSequence =
            $quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectPresetK"]["sequenceComparison"]
        $combinedCameraObjectPresetMSequence =
            $quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectPresetM"]["sequenceComparison"]
        $movingCameraPresetMMetrics =
            $quickSummary["lanes"]["mVsKDynamicMovingCameraPresetM"]["metrics"]
        $movingObjectOnlyPresetMMetrics =
            $quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyPresetM"]["metrics"]
        $combinedCameraObjectPresetMMetrics =
            $quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectPresetM"]["metrics"]
        $dynamicPresetMValidationClean =
            [bool]$quickSummary["lanes"]["mVsKDynamicMovingCameraPresetM"]["validationClean"] -and
            [bool]$quickSummary["lanes"]["mVsKDynamicMovingObjectOnlyPresetM"]["validationClean"] -and
            [bool]$quickSummary["lanes"]["mVsKDynamicCombinedCameraObjectPresetM"]["validationClean"]

        $quickSummary["diagnostics"]["mVsKDynamicPack"] = [ordered]@{
            validationClean = $dynamicPresetMValidationClean
            diagnosticOnly = $true
            productionCandidate = $false
            presetKValidationClean = $true
            presetMValidationClean = $dynamicPresetMValidationClean
            knownNgxInternalLayoutIsolation =
                [bool]$UseKnownNgxInternalLayoutIsolation
            allowedKnownCaptureDiagnostic =
                if ($UseKnownNgxInternalLayoutIsolation) {
                    ""
                } else {
                    "NGX internal nv.ngx.dlss.resource layout warning for preset M"
                }
            movingCamera = [ordered]@{
                nativeVsPresetK =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $movingCameraNativeImages `
                        -BPaths $movingCameraPresetKImages
                nativeVsPresetM =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $movingCameraNativeImages `
                        -BPaths $movingCameraPresetMImages
                presetKVsPresetM =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $movingCameraPresetKImages `
                        -BPaths $movingCameraPresetMImages
            }
            movingCameraTemporalInstability = [ordered]@{
                presetKVsNative =
                    New-SequenceInstabilitySummary `
                        -ReferenceName "native" `
                        -CandidateName "presetK" `
                        -Reference $movingCameraNativeSequence `
                        -Candidate $movingCameraPresetKSequence
                presetMVsNative =
                    New-SequenceInstabilitySummary `
                        -ReferenceName "native" `
                        -CandidateName "presetM" `
                        -Reference $movingCameraNativeSequence `
                        -Candidate $movingCameraPresetMSequence
                presetMVsPresetK =
                    New-SequenceInstabilitySummary `
                        -ReferenceName "presetK" `
                        -CandidateName "presetM" `
                        -Reference $movingCameraPresetKSequence `
                        -Candidate $movingCameraPresetMSequence
            }
            movingObjectOnly = [ordered]@{
                nativeVsPresetK =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $movingObjectOnlyNativeImages `
                        -BPaths $movingObjectOnlyPresetKImages
                nativeVsPresetM =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $movingObjectOnlyNativeImages `
                        -BPaths $movingObjectOnlyPresetMImages
                presetKVsPresetM =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $movingObjectOnlyPresetKImages `
                        -BPaths $movingObjectOnlyPresetMImages
            }
            movingObjectOnlyTemporalInstability = [ordered]@{
                presetKVsNative =
                    New-SequenceInstabilitySummary `
                        -ReferenceName "native" `
                        -CandidateName "presetK" `
                        -Reference $movingObjectOnlyNativeSequence `
                        -Candidate $movingObjectOnlyPresetKSequence
                presetMVsNative =
                    New-SequenceInstabilitySummary `
                        -ReferenceName "native" `
                        -CandidateName "presetM" `
                        -Reference $movingObjectOnlyNativeSequence `
                        -Candidate $movingObjectOnlyPresetMSequence
                presetMVsPresetK =
                    New-SequenceInstabilitySummary `
                        -ReferenceName "presetK" `
                        -CandidateName "presetM" `
                        -Reference $movingObjectOnlyPresetKSequence `
                        -Candidate $movingObjectOnlyPresetMSequence
            }
            combinedCameraObject = [ordered]@{
                nativeVsPresetK =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $combinedCameraObjectNativeImages `
                        -BPaths $combinedCameraObjectPresetKImages
                nativeVsPresetM =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $combinedCameraObjectNativeImages `
                        -BPaths $combinedCameraObjectPresetMImages
                presetKVsPresetM =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $combinedCameraObjectPresetKImages `
                        -BPaths $combinedCameraObjectPresetMImages
            }
            combinedCameraObjectTemporalInstability = [ordered]@{
                presetKVsNative =
                    New-SequenceInstabilitySummary `
                        -ReferenceName "native" `
                        -CandidateName "presetK" `
                        -Reference $combinedCameraObjectNativeSequence `
                        -Candidate $combinedCameraObjectPresetKSequence
                presetMVsNative =
                    New-SequenceInstabilitySummary `
                        -ReferenceName "native" `
                        -CandidateName "presetM" `
                        -Reference $combinedCameraObjectNativeSequence `
                        -Candidate $combinedCameraObjectPresetMSequence
                presetMVsPresetK =
                    New-SequenceInstabilitySummary `
                        -ReferenceName "presetK" `
                        -CandidateName "presetM" `
                        -Reference $combinedCameraObjectPresetKSequence `
                        -Candidate $combinedCameraObjectPresetMSequence
            }
        }

        $dynamicDiagnostics = $quickSummary["diagnostics"]["mVsKDynamicPack"]
        $movingCameraInstability =
            $dynamicDiagnostics["movingCameraTemporalInstability"]
        $movingObjectOnlyInstability =
            $dynamicDiagnostics["movingObjectOnlyTemporalInstability"]
        $combinedCameraObjectInstability =
            $dynamicDiagnostics["combinedCameraObjectTemporalInstability"]
        $movingCameraContract =
            New-DlssDynamicLaneContractSummary `
                -LaneName "movingCamera" `
                -Metrics $movingCameraPresetMMetrics `
                -ExpectedPreset "13" `
                -RequireSkinnedVelocity
        $movingObjectOnlyContract =
            New-DlssDynamicLaneContractSummary `
                -LaneName "movingObjectOnly" `
                -Metrics $movingObjectOnlyPresetMMetrics `
                -ExpectedPreset "13" `
                -RequireSkinnedVelocity
        $combinedCameraObjectContract =
            New-DlssDynamicLaneContractSummary `
                -LaneName "combinedCameraObject" `
                -Metrics $combinedCameraObjectPresetMMetrics `
                -ExpectedPreset "13" `
                -RequireSkinnedVelocity
        $mVsKReady =
            [bool]$movingCameraInstability["presetMVsPresetK"]["candidateNotWorseWithinTolerance"] -and
            [bool]$movingObjectOnlyInstability["presetMVsPresetK"]["candidateNotWorseWithinTolerance"] -and
            [bool]$combinedCameraObjectInstability["presetMVsPresetK"]["candidateNotWorseWithinTolerance"]
        $nativeNormalizedReady =
            [bool]$movingCameraInstability["presetMVsNative"]["candidateNotWorseWithinTolerance"] -and
            [bool]$movingObjectOnlyInstability["presetMVsNative"]["candidateNotWorseWithinTolerance"] -and
            [bool]$combinedCameraObjectInstability["presetMVsNative"]["candidateNotWorseWithinTolerance"]
        $temporalInputContractReady =
            [bool]$movingCameraContract["temporalInputContractReady"] -and
            [bool]$movingObjectOnlyContract["temporalInputContractReady"] -and
            [bool]$combinedCameraObjectContract["temporalInputContractReady"]
        $qualityInputReady =
            [bool]$movingCameraContract["qualityInputReady"] -and
            [bool]$movingObjectOnlyContract["qualityInputReady"] -and
            [bool]$combinedCameraObjectContract["qualityInputReady"]
        $skinnedVelocityInputReady =
            [bool]$movingCameraContract["skinnedVelocityInputReady"] -and
            [bool]$movingObjectOnlyContract["skinnedVelocityInputReady"] -and
            [bool]$combinedCameraObjectContract["skinnedVelocityInputReady"]
        $mValidationClean =
            [bool]$dynamicDiagnostics["presetMValidationClean"]
        $dynamicBlockedReasons = @()
        if (-not $mValidationClean) {
            $dynamicBlockedReasons +=
                if ($UseKnownNgxInternalLayoutIsolation) {
                    "Preset M dynamic captures still have an unknown validation diagnostic under the NGX isolation policy"
                } else {
                    "Preset M dynamic captures still require the NGX diagnostic whitelist"
                }
        }
        if (-not $mVsKReady) {
            $dynamicBlockedReasons +=
                "Preset M dynamic stability is worse than preset K beyond the diagnostic tolerance"
        }
        if (-not $nativeNormalizedReady) {
            $dynamicBlockedReasons +=
                "Preset M native-normalized dynamic stability still exceeds the diagnostic tolerance in at least one lane"
        }
        if (-not $temporalInputContractReady) {
            $dynamicBlockedReasons +=
                "Preset M dynamic temporal input contract is incomplete in at least one lane"
        }
        if (-not $qualityInputReady) {
            $dynamicBlockedReasons +=
                "Preset M dynamic DLSS quality inputs are incomplete in at least one lane"
        }
        if (-not $skinnedVelocityInputReady) {
            $dynamicBlockedReasons +=
                "Preset M dynamic skinned velocity input contract is incomplete in at least one lane"
        }

        $dynamicDiagnostics["dynamicProductionReadiness"] = [ordered]@{
            productionReady =
                ($mValidationClean -and
                 $mVsKReady -and
                 $nativeNormalizedReady -and
                 $temporalInputContractReady -and
                 $qualityInputReady -and
                 $skinnedVelocityInputReady)
            validationClean = $mValidationClean
            mVsKDynamicStable = $mVsKReady
            nativeNormalizedDynamicStable = $nativeNormalizedReady
            temporalInputContractReady = $temporalInputContractReady
            qualityInputReady = $qualityInputReady
            skinnedVelocityInputReady = $skinnedVelocityInputReady
            blockedReasons = $dynamicBlockedReasons
            laneReadiness = [ordered]@{
                movingCamera = [ordered]@{
                    presetMVsK =
                        $movingCameraInstability["presetMVsPresetK"]["candidateNotWorseWithinTolerance"]
                    presetMVsNative =
                        $movingCameraInstability["presetMVsNative"]["candidateNotWorseWithinTolerance"]
                    temporalInputContract =
                        $movingCameraContract["temporalInputContractReady"]
                    qualityInput =
                        $movingCameraContract["qualityInputReady"]
                    skinnedVelocityInput =
                        $movingCameraContract["skinnedVelocityInputReady"]
                }
                movingObjectOnly = [ordered]@{
                    presetMVsK =
                        $movingObjectOnlyInstability["presetMVsPresetK"]["candidateNotWorseWithinTolerance"]
                    presetMVsNative =
                        $movingObjectOnlyInstability["presetMVsNative"]["candidateNotWorseWithinTolerance"]
                    temporalInputContract =
                        $movingObjectOnlyContract["temporalInputContractReady"]
                    qualityInput =
                        $movingObjectOnlyContract["qualityInputReady"]
                    skinnedVelocityInput =
                        $movingObjectOnlyContract["skinnedVelocityInputReady"]
                }
                combinedCameraObject = [ordered]@{
                    presetMVsK =
                        $combinedCameraObjectInstability["presetMVsPresetK"]["candidateNotWorseWithinTolerance"]
                    presetMVsNative =
                        $combinedCameraObjectInstability["presetMVsNative"]["candidateNotWorseWithinTolerance"]
                    temporalInputContract =
                        $combinedCameraObjectContract["temporalInputContractReady"]
                    qualityInput =
                        $combinedCameraObjectContract["qualityInputReady"]
                    skinnedVelocityInput =
                        $combinedCameraObjectContract["skinnedVelocityInputReady"]
                }
            }
            laneContracts = [ordered]@{
                movingCamera = $movingCameraContract
                movingObjectOnly = $movingObjectOnlyContract
                combinedCameraObject = $combinedCameraObjectContract
            }
            requiredProductionState = [ordered]@{
                validationClean = $true
                mVsKDynamicStable = $true
                nativeNormalizedDynamicStable = $true
                temporalInputContractReady = $true
                qualityInputReady = $true
                skinnedVelocityInputReady = $true
            }
        }
    }

    if ($selectedSuites -contains "m-vs-k-moving-camera-fullscreen-pack") {
        $quickSummary["baselines"]["mVsKMovingCameraFullscreenPack"] = [ordered]@{
            skinnedProductionManifest =
                $defaultSceneSkinnedFbxMProductionBaselineManifestPath
            skinnedProductionName =
                $defaultSceneSkinnedFbxMProductionBaselineManifest.name
            scene =
                "default Forward 3D application scene with Fist Fight B.fbx production skinning"
            lane = "moving camera"
            fullscreen = $true
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            monitorIndex = $script:captureMonitorWorkArea.Index
            monitorDevice = $script:captureMonitorWorkArea.DeviceName
        }
        $quickSummary["thresholds"]["mVsKMovingCameraFullscreenPack"] = [ordered]@{
            sequenceGate = "record-only"
            reason =
                "Fullscreen monitor-resolution native/K/M moving-camera pack for the real skinned default scene; complements the 720p m-vs-k-dynamic-pack and object-only fullscreen diagnostics."
            expectedDlssExtent = "{0}x{1}" -f
                $highResolutionWindowWidth,
                $highResolutionWindowHeight
            presetMVsKTolerance = [ordered]@{
                changedEdgeRatio = 0.02
                meanEdgeDelta = 2.0
            }
            maskedSequenceGate =
                "record-only; localizes moving-camera color shimmer inside velocity/depth debug masks"
        }

        $quickSummary["lanes"]["mVsKMovingCameraFullscreenVelocityDebug"] =
            Invoke-QuickNativeDiagnosticSequenceSuite `
                -Name "m_vs_k_moving_camera_fullscreen_velocity_debug" `
                -LaneKey "mVsKMovingCameraFullscreen.velocityDebug" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionVelocityDebugEnvironment `
                -UseMonitorResolutionCapture
        $quickSummary["lanes"]["mVsKMovingCameraFullscreenDepthDebug"] =
            Invoke-QuickNativeDiagnosticSequenceSuite `
                -Name "m_vs_k_moving_camera_fullscreen_depth_debug" `
                -LaneKey "mVsKMovingCameraFullscreen.depthDebug" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionDepthDebugEnvironment `
                -UseMonitorResolutionCapture
        $quickSummary["lanes"]["mVsKMovingCameraFullscreenNative"] =
            Invoke-QuickNativeSequenceSuite `
                -Name "m_vs_k_moving_camera_fullscreen_native_deferred_hdr" `
                -LaneKey "mVsKMovingCameraFullscreen.native" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionNativeEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -UseMonitorResolutionCapture
        $quickSummary["lanes"]["mVsKMovingCameraFullscreenPresetK"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_vs_k_moving_camera_fullscreen_preset_k_present" `
                -LaneKey "mVsKMovingCameraFullscreen.presetK" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionPresetKEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mVsKMovingCameraFullscreenPresetM"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_vs_k_moving_camera_fullscreen_preset_m_present" `
                -LaneKey "mVsKMovingCameraFullscreen.presetM" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingCameraHighResolutionPresetMEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $movingCameraFullscreenNativeImages =
            @($quickSummary["lanes"]["mVsKMovingCameraFullscreenNative"]["images"])
        $movingCameraFullscreenPresetKImages =
            @($quickSummary["lanes"]["mVsKMovingCameraFullscreenPresetK"]["images"])
        $movingCameraFullscreenPresetMImages =
            @($quickSummary["lanes"]["mVsKMovingCameraFullscreenPresetM"]["images"])
        $movingCameraFullscreenVelocityImages =
            @($quickSummary["lanes"]["mVsKMovingCameraFullscreenVelocityDebug"]["images"])
        $movingCameraFullscreenDepthImages =
            @($quickSummary["lanes"]["mVsKMovingCameraFullscreenDepthDebug"]["images"])
        $movingCameraFullscreenNativeSequence =
            $quickSummary["lanes"]["mVsKMovingCameraFullscreenNative"]["sequenceComparison"]
        $movingCameraFullscreenPresetKSequence =
            $quickSummary["lanes"]["mVsKMovingCameraFullscreenPresetK"]["sequenceComparison"]
        $movingCameraFullscreenPresetMSequence =
            $quickSummary["lanes"]["mVsKMovingCameraFullscreenPresetM"]["sequenceComparison"]
        $movingCameraFullscreenVelocitySequence =
            $quickSummary["lanes"]["mVsKMovingCameraFullscreenVelocityDebug"]["sequenceComparison"]
        $movingCameraFullscreenDepthSequence =
            $quickSummary["lanes"]["mVsKMovingCameraFullscreenDepthDebug"]["sequenceComparison"]
        $movingCameraFullscreenPresetMMetrics =
            $quickSummary["lanes"]["mVsKMovingCameraFullscreenPresetM"]["metrics"]
        $movingCameraFullscreenPresetKMetrics =
            $quickSummary["lanes"]["mVsKMovingCameraFullscreenPresetK"]["metrics"]
        $movingCameraFullscreenMaskedSampleStep = 4
        $movingCameraFullscreenVelocityMaskedPresetKSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $movingCameraFullscreenPresetKImages `
                -MaskPaths $movingCameraFullscreenVelocityImages `
                -MaskName "velocity" `
                -SampleStep $movingCameraFullscreenMaskedSampleStep
        $movingCameraFullscreenVelocityMaskedPresetMSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $movingCameraFullscreenPresetMImages `
                -MaskPaths $movingCameraFullscreenVelocityImages `
                -MaskName "velocity" `
                -SampleStep $movingCameraFullscreenMaskedSampleStep
        $movingCameraFullscreenDepthMaskedPresetKSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $movingCameraFullscreenPresetKImages `
                -MaskPaths $movingCameraFullscreenDepthImages `
                -MaskName "depth" `
                -SampleStep $movingCameraFullscreenMaskedSampleStep
        $movingCameraFullscreenDepthMaskedPresetMSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $movingCameraFullscreenPresetMImages `
                -MaskPaths $movingCameraFullscreenDepthImages `
                -MaskName "depth" `
                -SampleStep $movingCameraFullscreenMaskedSampleStep
        $movingCameraFullscreenContract =
            New-DlssDynamicLaneContractSummary `
                -LaneName "movingCameraFullscreen" `
                -Metrics $movingCameraFullscreenPresetMMetrics `
                -ExpectedPreset "13" `
                -RequireSkinnedVelocity
        $movingCameraFullscreenMVsK =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $movingCameraFullscreenPresetKSequence `
                -Candidate $movingCameraFullscreenPresetMSequence
        $movingCameraFullscreenMVsNative =
            New-SequenceInstabilitySummary `
                -ReferenceName "native" `
                -CandidateName "presetM" `
                -Reference $movingCameraFullscreenNativeSequence `
                -Candidate $movingCameraFullscreenPresetMSequence
        $movingCameraFullscreenKVsNative =
            New-SequenceInstabilitySummary `
                -ReferenceName "native" `
                -CandidateName "presetK" `
                -Reference $movingCameraFullscreenNativeSequence `
                -Candidate $movingCameraFullscreenPresetKSequence
        $movingCameraFullscreenMVsKHotspots =
            Compare-SequenceInstabilityHotspots `
                -ReferencePaths $movingCameraFullscreenPresetKImages `
                -CandidatePaths $movingCameraFullscreenPresetMImages
        $movingCameraFullscreenVelocityMaskedMVsK =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $movingCameraFullscreenVelocityMaskedPresetKSequence `
                -Candidate $movingCameraFullscreenVelocityMaskedPresetMSequence
        $movingCameraFullscreenDepthMaskedMVsK =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $movingCameraFullscreenDepthMaskedPresetKSequence `
                -Candidate $movingCameraFullscreenDepthMaskedPresetMSequence
        $movingCameraFullscreenVelocityMaskedMVsKHotspots =
            Compare-MaskedSequenceInstabilityHotspots `
                -ReferencePaths $movingCameraFullscreenPresetKImages `
                -CandidatePaths $movingCameraFullscreenPresetMImages `
                -MaskPaths $movingCameraFullscreenVelocityImages `
                -MaskName "velocity" `
                -SampleStep $movingCameraFullscreenMaskedSampleStep
        $movingCameraFullscreenDepthMaskedMVsKHotspots =
            Compare-MaskedSequenceInstabilityHotspots `
                -ReferencePaths $movingCameraFullscreenPresetKImages `
                -CandidatePaths $movingCameraFullscreenPresetMImages `
                -MaskPaths $movingCameraFullscreenDepthImages `
                -MaskName "depth" `
                -SampleStep $movingCameraFullscreenMaskedSampleStep
        $movingCameraFullscreenHotspotLocalization =
            New-HotspotLocalizationSummary `
                -FullFrameHotspots $movingCameraFullscreenMVsKHotspots `
                -VelocityMaskedHotspots $movingCameraFullscreenVelocityMaskedMVsKHotspots `
                -DepthMaskedHotspots $movingCameraFullscreenDepthMaskedMVsKHotspots
        $movingCameraFullscreenAnimationPhase =
            New-AnimationPlaybackPhaseComparison `
                -ReferenceName "presetK" `
                -ReferenceMetrics $movingCameraFullscreenPresetKMetrics `
                -CandidateName "presetM" `
                -CandidateMetrics $movingCameraFullscreenPresetMMetrics

        $movingCameraFullscreenMValidationClean =
            [bool]$quickSummary["lanes"]["mVsKMovingCameraFullscreenPresetM"]["validationClean"]
        $movingCameraFullscreenMVsKReady =
            [bool]$movingCameraFullscreenMVsK["candidateNotWorseWithinTolerance"]
        $movingCameraFullscreenNativeNormalizedReady =
            [bool]$movingCameraFullscreenMVsNative["candidateNotWorseWithinTolerance"]
        $movingCameraFullscreenTemporalInputReady =
            [bool]$movingCameraFullscreenContract["temporalInputContractReady"]
        $movingCameraFullscreenQualityInputReady =
            [bool]$movingCameraFullscreenContract["qualityInputReady"]
        $movingCameraFullscreenSkinnedVelocityInputReady =
            [bool]$movingCameraFullscreenContract["skinnedVelocityInputReady"]
        $movingCameraFullscreenBlockedReasons = @()
        if (-not $movingCameraFullscreenMValidationClean) {
            $movingCameraFullscreenBlockedReasons +=
                if ($UseKnownNgxInternalLayoutIsolation) {
                    "Preset M fullscreen moving-camera capture still has an unknown validation diagnostic under the NGX isolation policy"
                } else {
                    "Preset M fullscreen moving-camera capture still requires the NGX diagnostic whitelist"
                }
        }
        if (-not $movingCameraFullscreenMVsKReady) {
            $movingCameraFullscreenBlockedReasons +=
                "Preset M fullscreen moving-camera stability is worse than preset K beyond tolerance"
        }
        if (-not $movingCameraFullscreenNativeNormalizedReady) {
            $movingCameraFullscreenBlockedReasons +=
                "Preset M fullscreen moving-camera native-normalized stability exceeds tolerance"
        }
        if (-not $movingCameraFullscreenTemporalInputReady) {
            $movingCameraFullscreenBlockedReasons +=
                "Preset M fullscreen moving-camera temporal input contract is incomplete"
        }
        if (-not $movingCameraFullscreenQualityInputReady) {
            $movingCameraFullscreenBlockedReasons +=
                "Preset M fullscreen moving-camera DLSS quality inputs are incomplete"
        }
        if (-not $movingCameraFullscreenSkinnedVelocityInputReady) {
            $movingCameraFullscreenBlockedReasons +=
                "Preset M fullscreen moving-camera skinned velocity input contract is incomplete"
        }

        $quickSummary["diagnostics"]["mVsKMovingCameraFullscreenPack"] = [ordered]@{
            validationClean = $movingCameraFullscreenMValidationClean
            diagnosticOnly = $true
            productionCandidate = $false
            presetKValidationClean = $true
            presetMValidationClean = $movingCameraFullscreenMValidationClean
            knownNgxInternalLayoutIsolation =
                [bool]$UseKnownNgxInternalLayoutIsolation
            allowedKnownCaptureDiagnostic =
                if ($UseKnownNgxInternalLayoutIsolation) {
                    ""
                } else {
                    "NGX internal nv.ngx.dlss.resource layout warning for preset M"
                }
            highResolution = [ordered]@{
                width = $highResolutionWindowWidth
                height = $highResolutionWindowHeight
                monitorIndex = $script:captureMonitorWorkArea.Index
                monitorDevice = $script:captureMonitorWorkArea.DeviceName
                borderless = $true
            }
            sameFrame = [ordered]@{
                nativeVsPresetK =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $movingCameraFullscreenNativeImages `
                        -BPaths $movingCameraFullscreenPresetKImages
                nativeVsPresetM =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $movingCameraFullscreenNativeImages `
                        -BPaths $movingCameraFullscreenPresetMImages
                presetKVsPresetM =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $movingCameraFullscreenPresetKImages `
                        -BPaths $movingCameraFullscreenPresetMImages
            }
            temporalInstability = [ordered]@{
                presetKVsNative = $movingCameraFullscreenKVsNative
                presetMVsNative = $movingCameraFullscreenMVsNative
                presetMVsPresetK = $movingCameraFullscreenMVsK
            }
            velocityDebugSequence = $movingCameraFullscreenVelocitySequence
            depthDebugSequence = $movingCameraFullscreenDepthSequence
            velocityMasked = [ordered]@{
                presetK = $movingCameraFullscreenVelocityMaskedPresetKSequence
                presetM = $movingCameraFullscreenVelocityMaskedPresetMSequence
                presetMVsK = $movingCameraFullscreenVelocityMaskedMVsK
            }
            depthMasked = [ordered]@{
                presetK = $movingCameraFullscreenDepthMaskedPresetKSequence
                presetM = $movingCameraFullscreenDepthMaskedPresetMSequence
                presetMVsK = $movingCameraFullscreenDepthMaskedMVsK
            }
            instabilityHotspots = [ordered]@{
                presetMVsK = $movingCameraFullscreenMVsKHotspots
                velocityMaskedMVsK =
                    $movingCameraFullscreenVelocityMaskedMVsKHotspots
                depthMaskedMVsK =
                    $movingCameraFullscreenDepthMaskedMVsKHotspots
                interpretation =
                    "Full-frame and velocity/depth masked tiles rank where preset M adds more adjacent-frame edge delta than preset K; compare these regions before changing sharpness, mip bias, anisotropy, CAS, or RCAS defaults."
            }
            hotspotLocalization = $movingCameraFullscreenHotspotLocalization
            animationPhase = $movingCameraFullscreenAnimationPhase
            maskedReadiness = [ordered]@{
                velocityMaskedMVsK =
                    [bool]$movingCameraFullscreenVelocityMaskedMVsK["candidateNotWorseWithinTolerance"]
                depthMaskedMVsK =
                    [bool]$movingCameraFullscreenDepthMaskedMVsK["candidateNotWorseWithinTolerance"]
                inference =
                    "Moving-camera masked readiness is localization evidence; high-resolution production still requires the aggregate repeat and strict validation gates."
            }
            dynamicProductionReadiness = [ordered]@{
                productionReady =
                    ($movingCameraFullscreenMValidationClean -and
                     $movingCameraFullscreenMVsKReady -and
                     $movingCameraFullscreenNativeNormalizedReady -and
                     $movingCameraFullscreenTemporalInputReady -and
                     $movingCameraFullscreenQualityInputReady -and
                     $movingCameraFullscreenSkinnedVelocityInputReady)
                validationClean = $movingCameraFullscreenMValidationClean
                mVsKDynamicStable = $movingCameraFullscreenMVsKReady
                nativeNormalizedDynamicStable =
                    $movingCameraFullscreenNativeNormalizedReady
                temporalInputContractReady =
                    $movingCameraFullscreenTemporalInputReady
                qualityInputReady =
                    $movingCameraFullscreenQualityInputReady
                skinnedVelocityInputReady =
                    $movingCameraFullscreenSkinnedVelocityInputReady
                blockedReasons = $movingCameraFullscreenBlockedReasons
                laneContract = $movingCameraFullscreenContract
                requiredProductionState = [ordered]@{
                    validationClean = $true
                    mVsKDynamicStable = $true
                    nativeNormalizedDynamicStable = $true
                    temporalInputContractReady = $true
                    qualityInputReady = $true
                    skinnedVelocityInputReady = $true
                    animationPlaybackClockMode = "1/1"
                }
            }
        }
    }

    if ($selectedSuites -contains "m-vs-k-combined-fullscreen-pack") {
        $quickSummary["baselines"]["mVsKCombinedFullscreenPack"] = [ordered]@{
            skinnedProductionManifest =
                $defaultSceneSkinnedFbxMProductionBaselineManifestPath
            skinnedProductionName =
                $defaultSceneSkinnedFbxMProductionBaselineManifest.name
            scene =
                "default Forward 3D application scene with Fist Fight B.fbx production skinning"
            lane = "combined camera + object motion"
            fullscreen = $true
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            monitorIndex = $script:captureMonitorWorkArea.Index
            monitorDevice = $script:captureMonitorWorkArea.DeviceName
        }
        $quickSummary["thresholds"]["mVsKCombinedFullscreenPack"] = [ordered]@{
            sequenceGate = "record-only"
            reason =
                "Fullscreen monitor-resolution native/K/M combined camera+object pack for the real skinned default scene; complements the moving-camera fullscreen pack and object-only fullscreen diagnostics."
            expectedDlssExtent = "{0}x{1}" -f
                $highResolutionWindowWidth,
                $highResolutionWindowHeight
            presetMVsKTolerance = [ordered]@{
                changedEdgeRatio = 0.02
                meanEdgeDelta = 2.0
            }
        }

        $quickSummary["lanes"]["mVsKCombinedFullscreenNative"] =
            Invoke-QuickNativeSequenceSuite `
                -Name "m_vs_k_combined_fullscreen_native_deferred_hdr" `
                -LaneKey "mVsKCombinedFullscreen.native" `
                -Environment $defaultSceneSkinnedFbxDynamicCombinedHighResolutionNativeEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -UseMonitorResolutionCapture
        $quickSummary["lanes"]["mVsKCombinedFullscreenPresetK"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_vs_k_combined_fullscreen_preset_k_present" `
                -LaneKey "mVsKCombinedFullscreen.presetK" `
                -Environment $defaultSceneSkinnedFbxDynamicCombinedHighResolutionPresetKEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mVsKCombinedFullscreenPresetM"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_vs_k_combined_fullscreen_preset_m_present" `
                -LaneKey "mVsKCombinedFullscreen.presetM" `
                -Environment $defaultSceneSkinnedFbxDynamicCombinedHighResolutionPresetMEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $combinedFullscreenNativeImages =
            @($quickSummary["lanes"]["mVsKCombinedFullscreenNative"]["images"])
        $combinedFullscreenPresetKImages =
            @($quickSummary["lanes"]["mVsKCombinedFullscreenPresetK"]["images"])
        $combinedFullscreenPresetMImages =
            @($quickSummary["lanes"]["mVsKCombinedFullscreenPresetM"]["images"])
        $combinedFullscreenNativeSequence =
            $quickSummary["lanes"]["mVsKCombinedFullscreenNative"]["sequenceComparison"]
        $combinedFullscreenPresetKSequence =
            $quickSummary["lanes"]["mVsKCombinedFullscreenPresetK"]["sequenceComparison"]
        $combinedFullscreenPresetMSequence =
            $quickSummary["lanes"]["mVsKCombinedFullscreenPresetM"]["sequenceComparison"]
        $combinedFullscreenPresetMMetrics =
            $quickSummary["lanes"]["mVsKCombinedFullscreenPresetM"]["metrics"]
        $combinedFullscreenPresetKMetrics =
            $quickSummary["lanes"]["mVsKCombinedFullscreenPresetK"]["metrics"]
        $combinedFullscreenContract =
            New-DlssDynamicLaneContractSummary `
                -LaneName "combinedCameraObjectFullscreen" `
                -Metrics $combinedFullscreenPresetMMetrics `
                -ExpectedPreset "13" `
                -RequireSkinnedVelocity
        $combinedFullscreenMVsK =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $combinedFullscreenPresetKSequence `
                -Candidate $combinedFullscreenPresetMSequence
        $combinedFullscreenMVsNative =
            New-SequenceInstabilitySummary `
                -ReferenceName "native" `
                -CandidateName "presetM" `
                -Reference $combinedFullscreenNativeSequence `
                -Candidate $combinedFullscreenPresetMSequence
        $combinedFullscreenKVsNative =
            New-SequenceInstabilitySummary `
                -ReferenceName "native" `
                -CandidateName "presetK" `
                -Reference $combinedFullscreenNativeSequence `
                -Candidate $combinedFullscreenPresetKSequence
        $combinedFullscreenMVsKHotspots =
            Compare-SequenceInstabilityHotspots `
                -ReferencePaths $combinedFullscreenPresetKImages `
                -CandidatePaths $combinedFullscreenPresetMImages
        $combinedFullscreenHotspotLocalization =
            New-HotspotLocalizationSummary `
                -FullFrameHotspots $combinedFullscreenMVsKHotspots
        $combinedFullscreenAnimationPhase =
            New-AnimationPlaybackPhaseComparison `
                -ReferenceName "presetK" `
                -ReferenceMetrics $combinedFullscreenPresetKMetrics `
                -CandidateName "presetM" `
                -CandidateMetrics $combinedFullscreenPresetMMetrics

        $combinedFullscreenMValidationClean =
            [bool]$quickSummary["lanes"]["mVsKCombinedFullscreenPresetM"]["validationClean"]
        $combinedFullscreenMVsKReady =
            [bool]$combinedFullscreenMVsK["candidateNotWorseWithinTolerance"]
        $combinedFullscreenNativeNormalizedReady =
            [bool]$combinedFullscreenMVsNative["candidateNotWorseWithinTolerance"]
        $combinedFullscreenTemporalInputReady =
            [bool]$combinedFullscreenContract["temporalInputContractReady"]
        $combinedFullscreenQualityInputReady =
            [bool]$combinedFullscreenContract["qualityInputReady"]
        $combinedFullscreenSkinnedVelocityInputReady =
            [bool]$combinedFullscreenContract["skinnedVelocityInputReady"]
        $combinedFullscreenBlockedReasons = @()
        if (-not $combinedFullscreenMValidationClean) {
            $combinedFullscreenBlockedReasons +=
                if ($UseKnownNgxInternalLayoutIsolation) {
                    "Preset M fullscreen combined camera+object capture still has an unknown validation diagnostic under the NGX isolation policy"
                } else {
                    "Preset M fullscreen combined camera+object capture still requires the NGX diagnostic whitelist"
                }
        }
        if (-not $combinedFullscreenMVsKReady) {
            $combinedFullscreenBlockedReasons +=
                "Preset M fullscreen combined camera+object stability is worse than preset K beyond tolerance"
        }
        if (-not $combinedFullscreenNativeNormalizedReady) {
            $combinedFullscreenBlockedReasons +=
                "Preset M fullscreen combined camera+object native-normalized stability exceeds tolerance"
        }
        if (-not $combinedFullscreenTemporalInputReady) {
            $combinedFullscreenBlockedReasons +=
                "Preset M fullscreen combined camera+object temporal input contract is incomplete"
        }
        if (-not $combinedFullscreenQualityInputReady) {
            $combinedFullscreenBlockedReasons +=
                "Preset M fullscreen combined camera+object DLSS quality inputs are incomplete"
        }
        if (-not $combinedFullscreenSkinnedVelocityInputReady) {
            $combinedFullscreenBlockedReasons +=
                "Preset M fullscreen combined camera+object skinned velocity input contract is incomplete"
        }

        $quickSummary["diagnostics"]["mVsKCombinedFullscreenPack"] = [ordered]@{
            validationClean = $combinedFullscreenMValidationClean
            diagnosticOnly = $true
            productionCandidate = $false
            presetKValidationClean = $true
            presetMValidationClean = $combinedFullscreenMValidationClean
            knownNgxInternalLayoutIsolation =
                [bool]$UseKnownNgxInternalLayoutIsolation
            allowedKnownCaptureDiagnostic =
                if ($UseKnownNgxInternalLayoutIsolation) {
                    ""
                } else {
                    "NGX internal nv.ngx.dlss.resource layout warning for preset M"
                }
            highResolution = [ordered]@{
                width = $highResolutionWindowWidth
                height = $highResolutionWindowHeight
                monitorIndex = $script:captureMonitorWorkArea.Index
                monitorDevice = $script:captureMonitorWorkArea.DeviceName
                borderless = $true
            }
            sameFrame = [ordered]@{
                nativeVsPresetK =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $combinedFullscreenNativeImages `
                        -BPaths $combinedFullscreenPresetKImages
                nativeVsPresetM =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $combinedFullscreenNativeImages `
                        -BPaths $combinedFullscreenPresetMImages
                presetKVsPresetM =
                    Compare-DlssDynamicImageSequencePairSet `
                        -APaths $combinedFullscreenPresetKImages `
                        -BPaths $combinedFullscreenPresetMImages
            }
            temporalInstability = [ordered]@{
                presetKVsNative = $combinedFullscreenKVsNative
                presetMVsNative = $combinedFullscreenMVsNative
                presetMVsPresetK = $combinedFullscreenMVsK
            }
            instabilityHotspots = [ordered]@{
                presetMVsK = $combinedFullscreenMVsKHotspots
                interpretation =
                    "Full-frame tiles rank where preset M adds more adjacent-frame edge delta than preset K; use this with object-only masked hotspots to separate camera-motion instability from skinned silhouette instability."
            }
            hotspotLocalization = $combinedFullscreenHotspotLocalization
            animationPhase = $combinedFullscreenAnimationPhase
            dynamicProductionReadiness = [ordered]@{
                productionReady =
                    ($combinedFullscreenMValidationClean -and
                     $combinedFullscreenMVsKReady -and
                     $combinedFullscreenNativeNormalizedReady -and
                     $combinedFullscreenTemporalInputReady -and
                     $combinedFullscreenQualityInputReady -and
                     $combinedFullscreenSkinnedVelocityInputReady)
                validationClean = $combinedFullscreenMValidationClean
                mVsKDynamicStable = $combinedFullscreenMVsKReady
                nativeNormalizedDynamicStable =
                    $combinedFullscreenNativeNormalizedReady
                temporalInputContractReady =
                    $combinedFullscreenTemporalInputReady
                qualityInputReady =
                    $combinedFullscreenQualityInputReady
                skinnedVelocityInputReady =
                    $combinedFullscreenSkinnedVelocityInputReady
                blockedReasons = $combinedFullscreenBlockedReasons
                laneContract = $combinedFullscreenContract
                requiredProductionState = [ordered]@{
                    validationClean = $true
                    mVsKDynamicStable = $true
                    nativeNormalizedDynamicStable = $true
                    temporalInputContractReady = $true
                    qualityInputReady = $true
                    skinnedVelocityInputReady = $true
                    animationPlaybackClockMode = "1/1"
                }
            }
        }
    }

    if ($selectedSuites -contains "m-object-shimmer-diagnostics") {
        $quickSummary["baselines"]["mObjectShimmerDiagnostics"] = [ordered]@{
            skinnedProductionManifest =
                $defaultSceneSkinnedFbxMProductionBaselineManifestPath
            skinnedProductionName =
                $defaultSceneSkinnedFbxMProductionBaselineManifest.name
            scene =
                "default Forward 3D application scene with Fist Fight B.fbx production skinning"
            lane = "static camera + skinned object motion only"
            model = $importedSkinnedPreviewModelPath
        }
        $quickSummary["thresholds"]["mObjectShimmerDiagnostics"] = [ordered]@{
            sequenceGate = "record-only"
            reason =
                "Focused object-only shimmer diagnostics compare preset K, preset M, and preset M sharpness=0 before any tuning can be promoted"
            presetMVsKTolerance = [ordered]@{
                changedEdgeRatio = 0.02
                meanEdgeDelta = 2.0
            }
            velocityDebug =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest
            depthDebug =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest
            maskedSequenceGate =
                "record-only; evaluates color shimmer inside velocity/depth debug masks"
        }

        $quickSummary["lanes"]["mObjectShimmerVelocityDebug"] =
            Invoke-QuickNativeDiagnosticSequenceSuite `
                -Name "m_object_shimmer_object_only_velocity_debug" `
                -LaneKey "mObjectShimmer.velocityDebug" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyVelocityDebugEnvironment
        $quickSummary["lanes"]["mObjectShimmerDepthDebug"] =
            Invoke-QuickNativeDiagnosticSequenceSuite `
                -Name "m_object_shimmer_object_only_depth_debug" `
                -LaneKey "mObjectShimmer.depthDebug" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyDepthDebugEnvironment
        $quickSummary["lanes"]["mObjectShimmerPresetK"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_object_only_preset_k_present" `
                -LaneKey "mObjectShimmer.presetK" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetKEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate
        $quickSummary["lanes"]["mObjectShimmerPresetM"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_object_only_preset_m_present" `
                -LaneKey "mObjectShimmer.presetM" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate
        $quickSummary["lanes"]["mObjectShimmerPresetMSharpnessZero"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_object_only_preset_m_sharpness_zero_present" `
                -LaneKey "mObjectShimmer.presetMSharpnessZero" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyPresetMSharpnessZeroEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -ExpectedEvaluateSharpness 0.0

        $objectShimmerPresetKSequence =
            $quickSummary["lanes"]["mObjectShimmerPresetK"]["sequenceComparison"]
        $objectShimmerPresetMSequence =
            $quickSummary["lanes"]["mObjectShimmerPresetM"]["sequenceComparison"]
        $objectShimmerPresetMSharpnessZeroSequence =
            $quickSummary["lanes"]["mObjectShimmerPresetMSharpnessZero"]["sequenceComparison"]
        $objectShimmerVelocitySequence =
            $quickSummary["lanes"]["mObjectShimmerVelocityDebug"]["sequenceComparison"]
        $objectShimmerDepthSequence =
            $quickSummary["lanes"]["mObjectShimmerDepthDebug"]["sequenceComparison"]
        $objectShimmerVelocityImages =
            @($quickSummary["lanes"]["mObjectShimmerVelocityDebug"]["images"])
        $objectShimmerDepthImages =
            @($quickSummary["lanes"]["mObjectShimmerDepthDebug"]["images"])
        $objectShimmerPresetKImages =
            @($quickSummary["lanes"]["mObjectShimmerPresetK"]["images"])
        $objectShimmerPresetMImages =
            @($quickSummary["lanes"]["mObjectShimmerPresetM"]["images"])
        $objectShimmerPresetMSharpnessZeroImages =
            @($quickSummary["lanes"]["mObjectShimmerPresetMSharpnessZero"]["images"])
        $objectShimmerPresetMMetrics =
            $quickSummary["lanes"]["mObjectShimmerPresetM"]["metrics"]
        $objectShimmerPresetMSharpnessZeroMetrics =
            $quickSummary["lanes"]["mObjectShimmerPresetMSharpnessZero"]["metrics"]
        $objectShimmerVelocityMaskedPresetKSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerPresetKImages `
                -MaskPaths $objectShimmerVelocityImages `
                -MaskName "velocity"
        $objectShimmerVelocityMaskedPresetMSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerPresetMImages `
                -MaskPaths $objectShimmerVelocityImages `
                -MaskName "velocity"
        $objectShimmerVelocityMaskedPresetMSharpnessZeroSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerPresetMSharpnessZeroImages `
                -MaskPaths $objectShimmerVelocityImages `
                -MaskName "velocity"
        $objectShimmerDepthMaskedPresetKSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerPresetKImages `
                -MaskPaths $objectShimmerDepthImages `
                -MaskName "depth"
        $objectShimmerDepthMaskedPresetMSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerPresetMImages `
                -MaskPaths $objectShimmerDepthImages `
                -MaskName "depth"
        $objectShimmerDepthMaskedPresetMSharpnessZeroSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerPresetMSharpnessZeroImages `
                -MaskPaths $objectShimmerDepthImages `
                -MaskName "depth"
        $mDefaultVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $objectShimmerPresetKSequence `
                -Candidate $objectShimmerPresetMSequence
        $mSharpnessZeroVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMSharpnessZero" `
                -Reference $objectShimmerPresetKSequence `
                -Candidate $objectShimmerPresetMSharpnessZeroSequence
        $mSharpnessZeroVsDefaultInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMSharpnessZero" `
                -Reference $objectShimmerPresetMSequence `
                -Candidate $objectShimmerPresetMSharpnessZeroSequence
        $mVelocityMaskedDefaultVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $objectShimmerVelocityMaskedPresetKSequence `
                -Candidate $objectShimmerVelocityMaskedPresetMSequence
        $mVelocityMaskedSharpnessZeroVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMSharpnessZero" `
                -Reference $objectShimmerVelocityMaskedPresetKSequence `
                -Candidate $objectShimmerVelocityMaskedPresetMSharpnessZeroSequence
        $mDepthMaskedDefaultVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $objectShimmerDepthMaskedPresetKSequence `
                -Candidate $objectShimmerDepthMaskedPresetMSequence
        $mDepthMaskedSharpnessZeroVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMSharpnessZero" `
                -Reference $objectShimmerDepthMaskedPresetKSequence `
                -Candidate $objectShimmerDepthMaskedPresetMSharpnessZeroSequence
        $sharpnessZeroImprovesEdgeRatio =
            [double]$objectShimmerPresetMSharpnessZeroSequence.maxChangedEdgeRatio -lt
            [double]$objectShimmerPresetMSequence.maxChangedEdgeRatio
        $sharpnessZeroImprovesMeanEdge =
            [double]$objectShimmerPresetMSharpnessZeroSequence.maxMeanEdgeDelta -le
            [double]$objectShimmerPresetMSequence.maxMeanEdgeDelta
        $sharpnessZeroImproves =
            $sharpnessZeroImprovesEdgeRatio -and $sharpnessZeroImprovesMeanEdge
        $sharpnessZeroPassesKGate =
            [bool]$mSharpnessZeroVsKInstability["candidateNotWorseWithinTolerance"]
        $velocityMaskedMDefaultPassesKGate =
            [bool]$mVelocityMaskedDefaultVsKInstability["candidateNotWorseWithinTolerance"]
        $velocityMaskedMSharpnessZeroPassesKGate =
            [bool]$mVelocityMaskedSharpnessZeroVsKInstability["candidateNotWorseWithinTolerance"]
        $depthMaskedMDefaultPassesKGate =
            [bool]$mDepthMaskedDefaultVsKInstability["candidateNotWorseWithinTolerance"]
        $depthMaskedMSharpnessZeroPassesKGate =
            [bool]$mDepthMaskedSharpnessZeroVsKInstability["candidateNotWorseWithinTolerance"]

        $quickSummary["diagnostics"]["mObjectShimmerDiagnostics"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $true
            productionCandidate = $false
            allowedKnownCaptureDiagnostic =
                "NGX internal nv.ngx.dlss.resource layout warning for preset M"
            objectOnlyInputContract =
                New-DlssDynamicLaneContractSummary `
                    -LaneName "objectOnly" `
                    -Metrics $objectShimmerPresetMMetrics `
                    -ExpectedPreset "13" `
                    -RequireSkinnedVelocity
            presetMVsK = $mDefaultVsKInstability
            presetMSharpnessZeroVsK = $mSharpnessZeroVsKInstability
            presetMSharpnessZeroVsPresetM = $mSharpnessZeroVsDefaultInstability
            velocityDebugSequence = $objectShimmerVelocitySequence
            depthDebugSequence = $objectShimmerDepthSequence
            velocityMasked = [ordered]@{
                presetK = $objectShimmerVelocityMaskedPresetKSequence
                presetM = $objectShimmerVelocityMaskedPresetMSequence
                presetMSharpnessZero =
                    $objectShimmerVelocityMaskedPresetMSharpnessZeroSequence
                presetMVsK = $mVelocityMaskedDefaultVsKInstability
                presetMSharpnessZeroVsK =
                    $mVelocityMaskedSharpnessZeroVsKInstability
            }
            depthMasked = [ordered]@{
                presetK = $objectShimmerDepthMaskedPresetKSequence
                presetM = $objectShimmerDepthMaskedPresetMSequence
                presetMSharpnessZero =
                    $objectShimmerDepthMaskedPresetMSharpnessZeroSequence
                presetMVsK = $mDepthMaskedDefaultVsKInstability
                presetMSharpnessZeroVsK =
                    $mDepthMaskedSharpnessZeroVsKInstability
            }
            maskedReadiness = [ordered]@{
                velocityMaskedMVsK =
                    $velocityMaskedMDefaultPassesKGate
                velocityMaskedMSharpnessZeroVsK =
                    $velocityMaskedMSharpnessZeroPassesKGate
                depthMaskedMVsK =
                    $depthMaskedMDefaultPassesKGate
                depthMaskedMSharpnessZeroVsK =
                    $depthMaskedMSharpnessZeroPassesKGate
                depthDisocclusionFocus =
                    (-not $depthMaskedMSharpnessZeroPassesKGate)
                inference =
                    "Treat masked readiness as localization evidence, not production acceptance; require repeat stability and validationClean=true before promoting sharpness or mip tuning."
            }
            sharpnessControl = [ordered]@{
                defaultEvaluateSharpness =
                    "$($objectShimmerPresetMMetrics.evaluateSharpness)"
                sharpnessZeroEvaluateSharpness =
                    "$($objectShimmerPresetMSharpnessZeroMetrics.evaluateSharpness)"
                improvesChangedEdgeRatio = $sharpnessZeroImprovesEdgeRatio
                improvesMeanEdgeDelta = $sharpnessZeroImprovesMeanEdge
                improvesBothEdgeMetrics = $sharpnessZeroImproves
                passesPresetKGate = $sharpnessZeroPassesKGate
                defaultChangedEdgeRatio =
                    [double]$objectShimmerPresetMSequence.maxChangedEdgeRatio
                sharpnessZeroChangedEdgeRatio =
                    [double]$objectShimmerPresetMSharpnessZeroSequence.maxChangedEdgeRatio
                defaultMeanEdgeDelta =
                    [double]$objectShimmerPresetMSequence.maxMeanEdgeDelta
                sharpnessZeroMeanEdgeDelta =
                    [double]$objectShimmerPresetMSharpnessZeroSequence.maxMeanEdgeDelta
                velocityMaskedDefaultChangedEdgeRatio =
                    [double]$objectShimmerVelocityMaskedPresetMSequence.maxChangedEdgeRatio
                velocityMaskedSharpnessZeroChangedEdgeRatio =
                    [double]$objectShimmerVelocityMaskedPresetMSharpnessZeroSequence.maxChangedEdgeRatio
                depthMaskedDefaultChangedEdgeRatio =
                    [double]$objectShimmerDepthMaskedPresetMSequence.maxChangedEdgeRatio
                depthMaskedSharpnessZeroChangedEdgeRatio =
                    [double]$objectShimmerDepthMaskedPresetMSharpnessZeroSequence.maxChangedEdgeRatio
                promoteToDefault =
                    ($sharpnessZeroImproves -and
                     $sharpnessZeroPassesKGate -and
                     $false)
                promotionBlocker =
                    "Preset M still has the NGX-owned layout diagnostic; tuning controls cannot become defaults until validationClean=true"
            }
            nextProbe =
                "Repeat masked object-only diagnostics under high-resolution/fullscreen; if depth-masked readiness regresses, inspect depth discontinuities, disocclusion handling, and skinned silhouette coverage before changing default tuning."
        }
    }

    if ($selectedSuites -contains "m-object-shimmer-fullscreen-diagnostics") {
        $quickSummary["baselines"]["mObjectShimmerFullscreenDiagnostics"] = [ordered]@{
            skinnedProductionManifest =
                $defaultSceneSkinnedFbxMProductionBaselineManifestPath
            skinnedProductionName =
                $defaultSceneSkinnedFbxMProductionBaselineManifest.name
            scene =
                "default Forward 3D application scene with Fist Fight B.fbx production skinning"
            lane = "static camera + skinned object motion only"
            model = $importedSkinnedPreviewModelPath
            fullscreen = $true
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            monitorIndex = $script:captureMonitorWorkArea.Index
            monitorDevice = $script:captureMonitorWorkArea.DeviceName
        }
        $quickSummary["thresholds"]["mObjectShimmerFullscreenDiagnostics"] = [ordered]@{
            sequenceGate = "record-only"
            reason =
                "Repeat the focused object-only shimmer diagnostics at monitor resolution and borderless fullscreen before promoting any preset-M tuning."
            presetMVsKTolerance = [ordered]@{
                changedEdgeRatio = 0.02
                meanEdgeDelta = 2.0
            }
            expectedDlssExtent = "{0}x{1}" -f
                $highResolutionWindowWidth,
                $highResolutionWindowHeight
            maskedSequenceGate =
                "record-only; evaluates fullscreen color shimmer inside velocity/depth debug masks"
        }

        $quickSummary["lanes"]["mObjectShimmerFullscreenVelocityDebug"] =
            Invoke-QuickNativeDiagnosticSequenceSuite `
                -Name "m_object_shimmer_fullscreen_object_only_velocity_debug" `
                -LaneKey "mObjectShimmerFullscreen.velocityDebug" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionVelocityDebugEnvironment `
                -UseMonitorResolutionCapture
        $quickSummary["lanes"]["mObjectShimmerFullscreenDepthDebug"] =
            Invoke-QuickNativeDiagnosticSequenceSuite `
                -Name "m_object_shimmer_fullscreen_object_only_depth_debug" `
                -LaneKey "mObjectShimmerFullscreen.depthDebug" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionDepthDebugEnvironment `
                -UseMonitorResolutionCapture
        $quickSummary["lanes"]["mObjectShimmerFullscreenPresetK"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_fullscreen_object_only_preset_k_present" `
                -LaneKey "mObjectShimmerFullscreen.presetK" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetKEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mObjectShimmerFullscreenPresetM"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_fullscreen_object_only_preset_m_present" `
                -LaneKey "mObjectShimmerFullscreen.presetM" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mObjectShimmerFullscreenPresetMSharpnessZero"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_fullscreen_object_only_preset_m_sharpness_zero_present" `
                -LaneKey "mObjectShimmerFullscreen.presetMSharpnessZero" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMSharpnessZeroEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -ExpectedEvaluateSharpness 0.0 `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $objectShimmerFullscreenPresetKSequence =
            $quickSummary["lanes"]["mObjectShimmerFullscreenPresetK"]["sequenceComparison"]
        $objectShimmerFullscreenPresetMSequence =
            $quickSummary["lanes"]["mObjectShimmerFullscreenPresetM"]["sequenceComparison"]
        $objectShimmerFullscreenPresetMSharpnessZeroSequence =
            $quickSummary["lanes"]["mObjectShimmerFullscreenPresetMSharpnessZero"]["sequenceComparison"]
        $objectShimmerFullscreenVelocitySequence =
            $quickSummary["lanes"]["mObjectShimmerFullscreenVelocityDebug"]["sequenceComparison"]
        $objectShimmerFullscreenDepthSequence =
            $quickSummary["lanes"]["mObjectShimmerFullscreenDepthDebug"]["sequenceComparison"]
        $objectShimmerFullscreenVelocityImages =
            @($quickSummary["lanes"]["mObjectShimmerFullscreenVelocityDebug"]["images"])
        $objectShimmerFullscreenDepthImages =
            @($quickSummary["lanes"]["mObjectShimmerFullscreenDepthDebug"]["images"])
        $objectShimmerFullscreenPresetKImages =
            @($quickSummary["lanes"]["mObjectShimmerFullscreenPresetK"]["images"])
        $objectShimmerFullscreenPresetMImages =
            @($quickSummary["lanes"]["mObjectShimmerFullscreenPresetM"]["images"])
        $objectShimmerFullscreenPresetMSharpnessZeroImages =
            @($quickSummary["lanes"]["mObjectShimmerFullscreenPresetMSharpnessZero"]["images"])
        $objectShimmerFullscreenPresetMMetrics =
            $quickSummary["lanes"]["mObjectShimmerFullscreenPresetM"]["metrics"]
        $objectShimmerFullscreenPresetKMetrics =
            $quickSummary["lanes"]["mObjectShimmerFullscreenPresetK"]["metrics"]
        $objectShimmerFullscreenPresetMSharpnessZeroMetrics =
            $quickSummary["lanes"]["mObjectShimmerFullscreenPresetMSharpnessZero"]["metrics"]
        $objectShimmerFullscreenMaskedSampleStep = 4

        $objectShimmerFullscreenVelocityMaskedPresetKSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerFullscreenPresetKImages `
                -MaskPaths $objectShimmerFullscreenVelocityImages `
                -MaskName "velocity" `
                -SampleStep $objectShimmerFullscreenMaskedSampleStep
        $objectShimmerFullscreenVelocityMaskedPresetMSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerFullscreenPresetMImages `
                -MaskPaths $objectShimmerFullscreenVelocityImages `
                -MaskName "velocity" `
                -SampleStep $objectShimmerFullscreenMaskedSampleStep
        $objectShimmerFullscreenVelocityMaskedPresetMSharpnessZeroSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerFullscreenPresetMSharpnessZeroImages `
                -MaskPaths $objectShimmerFullscreenVelocityImages `
                -MaskName "velocity" `
                -SampleStep $objectShimmerFullscreenMaskedSampleStep
        $objectShimmerFullscreenDepthMaskedPresetKSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerFullscreenPresetKImages `
                -MaskPaths $objectShimmerFullscreenDepthImages `
                -MaskName "depth" `
                -SampleStep $objectShimmerFullscreenMaskedSampleStep
        $objectShimmerFullscreenDepthMaskedPresetMSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerFullscreenPresetMImages `
                -MaskPaths $objectShimmerFullscreenDepthImages `
                -MaskName "depth" `
                -SampleStep $objectShimmerFullscreenMaskedSampleStep
        $objectShimmerFullscreenDepthMaskedPresetMSharpnessZeroSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $objectShimmerFullscreenPresetMSharpnessZeroImages `
                -MaskPaths $objectShimmerFullscreenDepthImages `
                -MaskName "depth" `
                -SampleStep $objectShimmerFullscreenMaskedSampleStep

        $mFullscreenDefaultVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $objectShimmerFullscreenPresetKSequence `
                -Candidate $objectShimmerFullscreenPresetMSequence
        $mFullscreenSharpnessZeroVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMSharpnessZero" `
                -Reference $objectShimmerFullscreenPresetKSequence `
                -Candidate $objectShimmerFullscreenPresetMSharpnessZeroSequence
        $mFullscreenSharpnessZeroVsDefaultInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMSharpnessZero" `
                -Reference $objectShimmerFullscreenPresetMSequence `
                -Candidate $objectShimmerFullscreenPresetMSharpnessZeroSequence
        $mFullscreenVelocityMaskedDefaultVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $objectShimmerFullscreenVelocityMaskedPresetKSequence `
                -Candidate $objectShimmerFullscreenVelocityMaskedPresetMSequence
        $mFullscreenVelocityMaskedSharpnessZeroVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMSharpnessZero" `
                -Reference $objectShimmerFullscreenVelocityMaskedPresetKSequence `
                -Candidate $objectShimmerFullscreenVelocityMaskedPresetMSharpnessZeroSequence
        $mFullscreenDepthMaskedDefaultVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $objectShimmerFullscreenDepthMaskedPresetKSequence `
                -Candidate $objectShimmerFullscreenDepthMaskedPresetMSequence
        $mFullscreenDepthMaskedSharpnessZeroVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMSharpnessZero" `
                -Reference $objectShimmerFullscreenDepthMaskedPresetKSequence `
                -Candidate $objectShimmerFullscreenDepthMaskedPresetMSharpnessZeroSequence
        $mFullscreenDefaultVsKHotspots =
            Compare-SequenceInstabilityHotspots `
                -ReferencePaths $objectShimmerFullscreenPresetKImages `
                -CandidatePaths $objectShimmerFullscreenPresetMImages `
                -SampleStep $objectShimmerFullscreenMaskedSampleStep
        $mFullscreenVelocityMaskedDefaultVsKHotspots =
            Compare-MaskedSequenceInstabilityHotspots `
                -ReferencePaths $objectShimmerFullscreenPresetKImages `
                -CandidatePaths $objectShimmerFullscreenPresetMImages `
                -MaskPaths $objectShimmerFullscreenVelocityImages `
                -MaskName "velocity" `
                -SampleStep $objectShimmerFullscreenMaskedSampleStep
        $mFullscreenDepthMaskedDefaultVsKHotspots =
            Compare-MaskedSequenceInstabilityHotspots `
                -ReferencePaths $objectShimmerFullscreenPresetKImages `
                -CandidatePaths $objectShimmerFullscreenPresetMImages `
                -MaskPaths $objectShimmerFullscreenDepthImages `
                -MaskName "depth" `
                -SampleStep $objectShimmerFullscreenMaskedSampleStep
        $mFullscreenHotspotLocalization =
            New-HotspotLocalizationSummary `
                -FullFrameHotspots $mFullscreenDefaultVsKHotspots `
                -VelocityMaskedHotspots $mFullscreenVelocityMaskedDefaultVsKHotspots `
                -DepthMaskedHotspots $mFullscreenDepthMaskedDefaultVsKHotspots
        $objectShimmerFullscreenAnimationPhase =
            New-AnimationPlaybackPhaseComparison `
                -ReferenceName "presetK" `
                -ReferenceMetrics $objectShimmerFullscreenPresetKMetrics `
                -CandidateName "presetM" `
                -CandidateMetrics $objectShimmerFullscreenPresetMMetrics

        $fullscreenSharpnessZeroImprovesEdgeRatio =
            [double]$objectShimmerFullscreenPresetMSharpnessZeroSequence.maxChangedEdgeRatio -lt
            [double]$objectShimmerFullscreenPresetMSequence.maxChangedEdgeRatio
        $fullscreenSharpnessZeroImprovesMeanEdge =
            [double]$objectShimmerFullscreenPresetMSharpnessZeroSequence.maxMeanEdgeDelta -le
            [double]$objectShimmerFullscreenPresetMSequence.maxMeanEdgeDelta
        $fullscreenSharpnessZeroImproves =
            $fullscreenSharpnessZeroImprovesEdgeRatio -and
            $fullscreenSharpnessZeroImprovesMeanEdge
        $fullscreenSharpnessZeroPassesKGate =
            [bool]$mFullscreenSharpnessZeroVsKInstability["candidateNotWorseWithinTolerance"]
        $fullscreenVelocityMaskedMDefaultPassesKGate =
            [bool]$mFullscreenVelocityMaskedDefaultVsKInstability["candidateNotWorseWithinTolerance"]
        $fullscreenVelocityMaskedMSharpnessZeroPassesKGate =
            [bool]$mFullscreenVelocityMaskedSharpnessZeroVsKInstability["candidateNotWorseWithinTolerance"]
        $fullscreenDepthMaskedMDefaultPassesKGate =
            [bool]$mFullscreenDepthMaskedDefaultVsKInstability["candidateNotWorseWithinTolerance"]
        $fullscreenDepthMaskedMSharpnessZeroPassesKGate =
            [bool]$mFullscreenDepthMaskedSharpnessZeroVsKInstability["candidateNotWorseWithinTolerance"]
        $objectShimmerFullscreenMValidationClean =
            [bool]$quickSummary["lanes"]["mObjectShimmerFullscreenPresetM"]["validationClean"]

        $quickSummary["diagnostics"]["mObjectShimmerFullscreenDiagnostics"] = [ordered]@{
            validationClean = $objectShimmerFullscreenMValidationClean
            diagnosticOnly = $true
            productionCandidate = $false
            fullscreen = $true
            borderless = $true
            useMonitorResolutionCapture = $true
            maskedSampleStep = $objectShimmerFullscreenMaskedSampleStep
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            monitorIndex = $script:captureMonitorWorkArea.Index
            monitorDevice = $script:captureMonitorWorkArea.DeviceName
            knownNgxInternalLayoutIsolation =
                [bool]$UseKnownNgxInternalLayoutIsolation
            allowedKnownCaptureDiagnostic =
                if ($UseKnownNgxInternalLayoutIsolation) {
                    ""
                } else {
                    "NGX internal nv.ngx.dlss.resource layout warning for preset M"
                }
            objectOnlyInputContract =
                New-DlssDynamicLaneContractSummary `
                    -LaneName "objectOnlyFullscreen" `
                    -Metrics $objectShimmerFullscreenPresetMMetrics `
                    -ExpectedPreset "13" `
                    -RequireSkinnedVelocity
            presetMVsK = $mFullscreenDefaultVsKInstability
            presetMSharpnessZeroVsK = $mFullscreenSharpnessZeroVsKInstability
            presetMSharpnessZeroVsPresetM =
                $mFullscreenSharpnessZeroVsDefaultInstability
            velocityDebugSequence = $objectShimmerFullscreenVelocitySequence
            depthDebugSequence = $objectShimmerFullscreenDepthSequence
            velocityMasked = [ordered]@{
                presetK = $objectShimmerFullscreenVelocityMaskedPresetKSequence
                presetM = $objectShimmerFullscreenVelocityMaskedPresetMSequence
                presetMSharpnessZero =
                    $objectShimmerFullscreenVelocityMaskedPresetMSharpnessZeroSequence
                presetMVsK = $mFullscreenVelocityMaskedDefaultVsKInstability
                presetMSharpnessZeroVsK =
                    $mFullscreenVelocityMaskedSharpnessZeroVsKInstability
            }
            depthMasked = [ordered]@{
                presetK = $objectShimmerFullscreenDepthMaskedPresetKSequence
                presetM = $objectShimmerFullscreenDepthMaskedPresetMSequence
                presetMSharpnessZero =
                    $objectShimmerFullscreenDepthMaskedPresetMSharpnessZeroSequence
                presetMVsK = $mFullscreenDepthMaskedDefaultVsKInstability
                presetMSharpnessZeroVsK =
                    $mFullscreenDepthMaskedSharpnessZeroVsKInstability
            }
            instabilityHotspots = [ordered]@{
                presetMVsK =
                    $mFullscreenDefaultVsKHotspots
                velocityMaskedMVsK =
                    $mFullscreenVelocityMaskedDefaultVsKHotspots
                depthMaskedMVsK =
                    $mFullscreenDepthMaskedDefaultVsKHotspots
                interpretation =
                    "Tiles rank where preset M adds more adjacent-frame edge delta than preset K inside the same debug mask; use this to inspect skinned silhouette/disocclusion concentration before tuning sharpness or mip bias."
            }
            hotspotLocalization = $mFullscreenHotspotLocalization
            animationPhase = $objectShimmerFullscreenAnimationPhase
            maskedReadiness = [ordered]@{
                velocityMaskedMVsK =
                    $fullscreenVelocityMaskedMDefaultPassesKGate
                velocityMaskedMSharpnessZeroVsK =
                    $fullscreenVelocityMaskedMSharpnessZeroPassesKGate
                depthMaskedMVsK =
                    $fullscreenDepthMaskedMDefaultPassesKGate
                depthMaskedMSharpnessZeroVsK =
                    $fullscreenDepthMaskedMSharpnessZeroPassesKGate
                inference =
                    "Fullscreen masked readiness is repeat evidence only; require validationClean=true and repeat stability before promoting preset-M tuning."
            }
            sharpnessControl = [ordered]@{
                defaultEvaluateSharpness =
                    "$($objectShimmerFullscreenPresetMMetrics.evaluateSharpness)"
                sharpnessZeroEvaluateSharpness =
                    "$($objectShimmerFullscreenPresetMSharpnessZeroMetrics.evaluateSharpness)"
                improvesChangedEdgeRatio =
                    $fullscreenSharpnessZeroImprovesEdgeRatio
                improvesMeanEdgeDelta =
                    $fullscreenSharpnessZeroImprovesMeanEdge
                improvesBothEdgeMetrics =
                    $fullscreenSharpnessZeroImproves
                passesPresetKGate =
                    $fullscreenSharpnessZeroPassesKGate
                defaultChangedEdgeRatio =
                    [double]$objectShimmerFullscreenPresetMSequence.maxChangedEdgeRatio
                sharpnessZeroChangedEdgeRatio =
                    [double]$objectShimmerFullscreenPresetMSharpnessZeroSequence.maxChangedEdgeRatio
                defaultMeanEdgeDelta =
                    [double]$objectShimmerFullscreenPresetMSequence.maxMeanEdgeDelta
                sharpnessZeroMeanEdgeDelta =
                    [double]$objectShimmerFullscreenPresetMSharpnessZeroSequence.maxMeanEdgeDelta
                velocityMaskedDefaultChangedEdgeRatio =
                    [double]$objectShimmerFullscreenVelocityMaskedPresetMSequence.maxChangedEdgeRatio
                velocityMaskedSharpnessZeroChangedEdgeRatio =
                    [double]$objectShimmerFullscreenVelocityMaskedPresetMSharpnessZeroSequence.maxChangedEdgeRatio
                depthMaskedDefaultChangedEdgeRatio =
                    [double]$objectShimmerFullscreenDepthMaskedPresetMSequence.maxChangedEdgeRatio
                depthMaskedSharpnessZeroChangedEdgeRatio =
                    [double]$objectShimmerFullscreenDepthMaskedPresetMSharpnessZeroSequence.maxChangedEdgeRatio
                promoteToDefault =
                    ($fullscreenSharpnessZeroImproves -and
                     $fullscreenSharpnessZeroPassesKGate -and
                     $false)
                promotionBlocker =
                    "Preset M still has the NGX-owned layout diagnostic; fullscreen tuning evidence cannot become a default until validationClean=true"
            }
            nextProbe =
                "If fullscreen object-only masked evidence regresses versus 720p, inspect motion-vector/depth/disocclusion coverage around the skinned silhouette before trying mip bias, anisotropy, or sharpening defaults."
        }
    }

    if ($selectedSuites -contains "m-object-shimmer-mv-jittered-ab") {
        $quickSummary["baselines"]["mObjectShimmerMvJitteredAb"] = [ordered]@{
            skinnedProductionManifest =
                $defaultSceneSkinnedFbxMProductionBaselineManifestPath
            skinnedProductionName =
                $defaultSceneSkinnedFbxMProductionBaselineManifest.name
            scene =
                "default Forward 3D application scene with Fist Fight B.fbx production skinning"
            lane = "static camera + skinned object motion only"
            model = $importedSkinnedPreviewModelPath
            fullscreen = $true
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            monitorIndex = $script:captureMonitorWorkArea.Index
            monitorDevice = $script:captureMonitorWorkArea.DeviceName
        }
        $quickSummary["thresholds"]["mObjectShimmerMvJitteredAb"] = [ordered]@{
            sequenceGate = "record-only"
            reason =
                "Controlled MVJittered create-flag A/B for the remaining fullscreen object-only skinned silhouette shimmer."
            presetMVsKTolerance = [ordered]@{
                changedEdgeRatio = 0.02
                meanEdgeDelta = 2.0
            }
            expectedDlssExtent = "{0}x{1}" -f
                $highResolutionWindowWidth,
                $highResolutionWindowHeight
        }

        $quickSummary["lanes"]["mObjectShimmerMvJitteredAbPresetK"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_mv_jittered_ab_object_only_preset_k_present" `
                -LaneKey "mObjectShimmerMvJitteredAb.presetK" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetKEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mObjectShimmerMvJitteredAbPresetM"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_mv_jittered_ab_object_only_preset_m_present" `
                -LaneKey "mObjectShimmerMvJitteredAb.presetM" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mObjectShimmerMvJitteredAbPresetMMvJittered"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_mv_jittered_ab_object_only_preset_m_mv_jittered_present" `
                -LaneKey "mObjectShimmerMvJitteredAb.presetMMvJittered" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMMvJitteredEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $mvJitteredPresetKSequence =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredAbPresetK"]["sequenceComparison"]
        $mvJitteredPresetMSequence =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredAbPresetM"]["sequenceComparison"]
        $mvJitteredPresetMMvJitteredSequence =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredAbPresetMMvJittered"]["sequenceComparison"]
        $mvJitteredPresetKMetrics =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredAbPresetK"]["metrics"]
        $mvJitteredPresetMMetrics =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredAbPresetM"]["metrics"]
        $mvJitteredPresetMMvJitteredMetrics =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredAbPresetMMvJittered"]["metrics"]
        $mvJitteredDefaultVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $mvJitteredPresetKSequence `
                -Candidate $mvJitteredPresetMSequence
        $mvJitteredVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMMvJittered" `
                -Reference $mvJitteredPresetKSequence `
                -Candidate $mvJitteredPresetMMvJitteredSequence
        $mvJitteredVsDefaultInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJittered" `
                -Reference $mvJitteredPresetMSequence `
                -Candidate $mvJitteredPresetMMvJitteredSequence
        $mvJitteredCreateFlagReady =
            "$($mvJitteredPresetMMvJitteredMetrics.dlssCreateFlagBits)" -eq
            "hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/1/0/0"
        if (-not $mvJitteredCreateFlagReady) {
            throw (
                "mObjectShimmerMvJitteredAb did not enable the DLSS MVJittered create flag: {0}" -f
                $mvJitteredPresetMMvJitteredMetrics.dlssCreateFlagBits
            )
        }
        $mvJitteredImprovesChangedEdgeRatio =
            [double]$mvJitteredPresetMMvJitteredSequence.maxChangedEdgeRatio -lt
            [double]$mvJitteredPresetMSequence.maxChangedEdgeRatio
        $mvJitteredImprovesMeanEdge =
            [double]$mvJitteredPresetMMvJitteredSequence.maxMeanEdgeDelta -lt
            [double]$mvJitteredPresetMSequence.maxMeanEdgeDelta
        $mvJitteredImprovesBoth =
            $mvJitteredImprovesChangedEdgeRatio -and
            $mvJitteredImprovesMeanEdge
        $mvJitteredPassesKGate =
            [bool]$mvJitteredVsKInstability["candidateNotWorseWithinTolerance"]

        $quickSummary["diagnostics"]["mObjectShimmerMvJitteredAb"] = [ordered]@{
            validationClean =
                [bool]$quickSummary["lanes"]["mObjectShimmerMvJitteredAbPresetMMvJittered"]["validationClean"]
            diagnosticOnly = $true
            productionCandidate = $false
            fullscreen = $true
            borderless = $true
            useMonitorResolutionCapture = $true
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            monitorIndex = $script:captureMonitorWorkArea.Index
            monitorDevice = $script:captureMonitorWorkArea.DeviceName
            knownNgxInternalLayoutIsolation =
                [bool]$UseKnownNgxInternalLayoutIsolation
            defaultInputContract =
                New-DlssDynamicLaneContractSummary `
                    -LaneName "objectOnlyFullscreenPresetM" `
                    -Metrics $mvJitteredPresetMMetrics `
                    -ExpectedPreset "13" `
                    -RequireSkinnedVelocity
            mvJitteredInputContract =
                New-DlssDynamicLaneContractSummary `
                    -LaneName "objectOnlyFullscreenPresetMMvJittered" `
                    -Metrics $mvJitteredPresetMMvJitteredMetrics `
                    -ExpectedPreset "13" `
                    -RequireSkinnedVelocity
            presetMVsK = $mvJitteredDefaultVsKInstability
            presetMMvJitteredVsK = $mvJitteredVsKInstability
            presetMMvJitteredVsPresetM = $mvJitteredVsDefaultInstability
            mvJitteredControl = [ordered]@{
                defaultCreateFlagBits =
                    "$($mvJitteredPresetMMetrics.dlssCreateFlagBits)"
                mvJitteredCreateFlagBits =
                    "$($mvJitteredPresetMMvJitteredMetrics.dlssCreateFlagBits)"
                createFlagReady = $mvJitteredCreateFlagReady
                defaultMotionVectorScale =
                    "$($mvJitteredPresetMMetrics.dlssMotionVectorScale)"
                mvJitteredMotionVectorScale =
                    "$($mvJitteredPresetMMvJitteredMetrics.dlssMotionVectorScale)"
                defaultMotionVectorScaleSemantics =
                    "pixel/unit/matchesRender=$($mvJitteredPresetMMetrics.dlssMotionVectorScalePixelSpace)/$($mvJitteredPresetMMetrics.dlssMotionVectorScaleUnitSpace)/$($mvJitteredPresetMMetrics.dlssMotionVectorScaleMatchesRenderExtent)"
                mvJitteredMotionVectorScaleSemantics =
                    "pixel/unit/matchesRender=$($mvJitteredPresetMMvJitteredMetrics.dlssMotionVectorScalePixelSpace)/$($mvJitteredPresetMMvJitteredMetrics.dlssMotionVectorScaleUnitSpace)/$($mvJitteredPresetMMvJitteredMetrics.dlssMotionVectorScaleMatchesRenderExtent)"
                improvesChangedEdgeRatio = $mvJitteredImprovesChangedEdgeRatio
                improvesMeanEdgeDelta = $mvJitteredImprovesMeanEdge
                improvesBothEdgeMetrics = $mvJitteredImprovesBoth
                passesPresetKGate = $mvJitteredPassesKGate
                promoteToDefault =
                    ($mvJitteredImprovesBoth -and
                     $mvJitteredPassesKGate -and
                     $false)
                promotionBlocker =
                    "MVJittered is a controlled create-flag A/B only; require masked repeat evidence, validation-policy acceptance, and subjective clarity before defaulting it."
            }
            nextProbe =
                "If MVJittered improves full-frame M-vs-K, repeat with velocity/depth masks; otherwise focus on skinned silhouette disocclusion/history rather than create-flag tuning."
        }
    }

    if ($selectedSuites -contains "m-object-shimmer-mv-jittered-masked-ab") {
        $quickSummary["baselines"]["mObjectShimmerMvJitteredMaskedAb"] = [ordered]@{
            skinnedProductionManifest =
                $defaultSceneSkinnedFbxMProductionBaselineManifestPath
            skinnedProductionName =
                $defaultSceneSkinnedFbxMProductionBaselineManifest.name
            scene =
                "default Forward 3D application scene with Fist Fight B.fbx production skinning"
            lane = "static camera + skinned object motion only"
            model = $importedSkinnedPreviewModelPath
            fullscreen = $true
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            monitorIndex = $script:captureMonitorWorkArea.Index
            monitorDevice = $script:captureMonitorWorkArea.DeviceName
        }
        $quickSummary["thresholds"]["mObjectShimmerMvJitteredMaskedAb"] = [ordered]@{
            sequenceGate = "record-only"
            reason =
                "Masked MVJittered create-flag A/B for fullscreen object-only skinned silhouette shimmer."
            presetMVsKTolerance = [ordered]@{
                changedEdgeRatio = 0.02
                meanEdgeDelta = 2.0
            }
            expectedDlssExtent = "{0}x{1}" -f
                $highResolutionWindowWidth,
                $highResolutionWindowHeight
            maskedSequenceGate =
                "record-only; compares K, default M, and MVJittered M inside velocity/depth debug masks"
        }

        $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbVelocityDebug"] =
            Invoke-QuickNativeDiagnosticSequenceSuite `
                -Name "m_object_shimmer_mv_jittered_masked_ab_object_only_velocity_debug" `
                -LaneKey "mObjectShimmerMvJitteredMaskedAb.velocityDebug" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionVelocityDebugEnvironment `
                -UseMonitorResolutionCapture
        $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbDepthDebug"] =
            Invoke-QuickNativeDiagnosticSequenceSuite `
                -Name "m_object_shimmer_mv_jittered_masked_ab_object_only_depth_debug" `
                -LaneKey "mObjectShimmerMvJitteredMaskedAb.depthDebug" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionDepthDebugEnvironment `
                -UseMonitorResolutionCapture
        $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetK"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_mv_jittered_masked_ab_object_only_preset_k_present" `
                -LaneKey "mObjectShimmerMvJitteredMaskedAb.presetK" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetKEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "11" `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetM"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_mv_jittered_masked_ab_object_only_preset_m_present" `
                -LaneKey "mObjectShimmerMvJitteredMaskedAb.presetM" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetMMvJittered"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_mv_jittered_masked_ab_object_only_preset_m_mv_jittered_present" `
                -LaneKey "mObjectShimmerMvJitteredMaskedAb.presetMMvJittered" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMMvJitteredEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetMMvJitteredJitteredHistory"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_mv_jittered_masked_ab_object_only_preset_m_mv_jittered_jittered_history_present" `
                -LaneKey "mObjectShimmerMvJitteredMaskedAb.presetMMvJitteredJitteredHistory" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMMvJitteredJitteredHistoryEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $mvJitteredMaskedVelocityImages =
            @($quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbVelocityDebug"]["images"])
        $mvJitteredMaskedDepthImages =
            @($quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbDepthDebug"]["images"])
        $mvJitteredMaskedPresetKImages =
            @($quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetK"]["images"])
        $mvJitteredMaskedPresetMImages =
            @($quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetM"]["images"])
        $mvJitteredMaskedPresetMMvJitteredImages =
            @($quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetMMvJittered"]["images"])
        $mvJitteredMaskedPresetMMvJitteredJitteredHistoryImages =
            @($quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetMMvJitteredJitteredHistory"]["images"])
        $mvJitteredMaskedPresetKSequence =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetK"]["sequenceComparison"]
        $mvJitteredMaskedPresetMSequence =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetM"]["sequenceComparison"]
        $mvJitteredMaskedPresetMMvJitteredSequence =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetMMvJittered"]["sequenceComparison"]
        $mvJitteredMaskedPresetMMvJitteredJitteredHistorySequence =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetMMvJitteredJitteredHistory"]["sequenceComparison"]
        $mvJitteredMaskedPresetMMetrics =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetM"]["metrics"]
        $mvJitteredMaskedPresetMMvJitteredMetrics =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetMMvJittered"]["metrics"]
        $mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics =
            $quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetMMvJitteredJitteredHistory"]["metrics"]
        $mvJitteredMaskedSampleStep = 4

        $mvJitteredMaskedVelocityPresetKSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mvJitteredMaskedPresetKImages `
                -MaskPaths $mvJitteredMaskedVelocityImages `
                -MaskName "velocity" `
                -SampleStep $mvJitteredMaskedSampleStep
        $mvJitteredMaskedVelocityPresetMSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mvJitteredMaskedPresetMImages `
                -MaskPaths $mvJitteredMaskedVelocityImages `
                -MaskName "velocity" `
                -SampleStep $mvJitteredMaskedSampleStep
        $mvJitteredMaskedVelocityPresetMMvJitteredSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mvJitteredMaskedPresetMMvJitteredImages `
                -MaskPaths $mvJitteredMaskedVelocityImages `
                -MaskName "velocity" `
                -SampleStep $mvJitteredMaskedSampleStep
        $mvJitteredMaskedVelocityPresetMMvJitteredJitteredHistorySequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mvJitteredMaskedPresetMMvJitteredJitteredHistoryImages `
                -MaskPaths $mvJitteredMaskedVelocityImages `
                -MaskName "velocity" `
                -SampleStep $mvJitteredMaskedSampleStep
        $mvJitteredMaskedDepthPresetKSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mvJitteredMaskedPresetKImages `
                -MaskPaths $mvJitteredMaskedDepthImages `
                -MaskName "depth" `
                -SampleStep $mvJitteredMaskedSampleStep
        $mvJitteredMaskedDepthPresetMSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mvJitteredMaskedPresetMImages `
                -MaskPaths $mvJitteredMaskedDepthImages `
                -MaskName "depth" `
                -SampleStep $mvJitteredMaskedSampleStep
        $mvJitteredMaskedDepthPresetMMvJitteredSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mvJitteredMaskedPresetMMvJitteredImages `
                -MaskPaths $mvJitteredMaskedDepthImages `
                -MaskName "depth" `
                -SampleStep $mvJitteredMaskedSampleStep
        $mvJitteredMaskedDepthPresetMMvJitteredJitteredHistorySequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mvJitteredMaskedPresetMMvJitteredJitteredHistoryImages `
                -MaskPaths $mvJitteredMaskedDepthImages `
                -MaskName "depth" `
                -SampleStep $mvJitteredMaskedSampleStep

        $mvJitteredMaskedDefaultVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $mvJitteredMaskedPresetKSequence `
                -Candidate $mvJitteredMaskedPresetMSequence
        $mvJitteredMaskedVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMMvJittered" `
                -Reference $mvJitteredMaskedPresetKSequence `
                -Candidate $mvJitteredMaskedPresetMMvJitteredSequence
        $mvJitteredMaskedVsDefaultInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJittered" `
                -Reference $mvJitteredMaskedPresetMSequence `
                -Candidate $mvJitteredMaskedPresetMMvJitteredSequence
        $mvJitteredMaskedJitteredHistoryVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mvJitteredMaskedPresetKSequence `
                -Candidate $mvJitteredMaskedPresetMMvJitteredJitteredHistorySequence
        $mvJitteredMaskedJitteredHistoryVsDefaultInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mvJitteredMaskedPresetMSequence `
                -Candidate $mvJitteredMaskedPresetMMvJitteredJitteredHistorySequence
        $mvJitteredMaskedJitteredHistoryVsMvJitteredInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetMMvJittered" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mvJitteredMaskedPresetMMvJitteredSequence `
                -Candidate $mvJitteredMaskedPresetMMvJitteredJitteredHistorySequence
        $mvJitteredMaskedVelocityDefaultVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $mvJitteredMaskedVelocityPresetKSequence `
                -Candidate $mvJitteredMaskedVelocityPresetMSequence
        $mvJitteredMaskedVelocityVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMMvJittered" `
                -Reference $mvJitteredMaskedVelocityPresetKSequence `
                -Candidate $mvJitteredMaskedVelocityPresetMMvJitteredSequence
        $mvJitteredMaskedVelocityVsDefaultInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJittered" `
                -Reference $mvJitteredMaskedVelocityPresetMSequence `
                -Candidate $mvJitteredMaskedVelocityPresetMMvJitteredSequence
        $mvJitteredMaskedVelocityJitteredHistoryVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mvJitteredMaskedVelocityPresetKSequence `
                -Candidate $mvJitteredMaskedVelocityPresetMMvJitteredJitteredHistorySequence
        $mvJitteredMaskedVelocityJitteredHistoryVsDefaultInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mvJitteredMaskedVelocityPresetMSequence `
                -Candidate $mvJitteredMaskedVelocityPresetMMvJitteredJitteredHistorySequence
        $mvJitteredMaskedVelocityJitteredHistoryVsMvJitteredInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetMMvJittered" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mvJitteredMaskedVelocityPresetMMvJitteredSequence `
                -Candidate $mvJitteredMaskedVelocityPresetMMvJitteredJitteredHistorySequence
        $mvJitteredMaskedDepthDefaultVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetM" `
                -Reference $mvJitteredMaskedDepthPresetKSequence `
                -Candidate $mvJitteredMaskedDepthPresetMSequence
        $mvJitteredMaskedDepthVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMMvJittered" `
                -Reference $mvJitteredMaskedDepthPresetKSequence `
                -Candidate $mvJitteredMaskedDepthPresetMMvJitteredSequence
        $mvJitteredMaskedDepthVsDefaultInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJittered" `
                -Reference $mvJitteredMaskedDepthPresetMSequence `
                -Candidate $mvJitteredMaskedDepthPresetMMvJitteredSequence
        $mvJitteredMaskedDepthJitteredHistoryVsKInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetK" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mvJitteredMaskedDepthPresetKSequence `
                -Candidate $mvJitteredMaskedDepthPresetMMvJitteredJitteredHistorySequence
        $mvJitteredMaskedDepthJitteredHistoryVsDefaultInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mvJitteredMaskedDepthPresetMSequence `
                -Candidate $mvJitteredMaskedDepthPresetMMvJitteredJitteredHistorySequence
        $mvJitteredMaskedDepthJitteredHistoryVsMvJitteredInstability =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetMMvJittered" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mvJitteredMaskedDepthPresetMMvJitteredSequence `
                -Candidate $mvJitteredMaskedDepthPresetMMvJitteredJitteredHistorySequence

        $mvJitteredMaskedCreateFlagReady =
            "$($mvJitteredMaskedPresetMMvJitteredMetrics.dlssCreateFlagBits)" -eq
            "hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/1/0/0"
        $mvJitteredMaskedJitteredHistoryCreateFlagReady =
            "$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.dlssCreateFlagBits)" -eq
            "hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/1/0/0"
        if (-not $mvJitteredMaskedCreateFlagReady) {
            throw (
                "mObjectShimmerMvJitteredMaskedAb did not enable the DLSS MVJittered create flag: {0}" -f
                $mvJitteredMaskedPresetMMvJitteredMetrics.dlssCreateFlagBits
            )
        }
        if (-not $mvJitteredMaskedJitteredHistoryCreateFlagReady) {
            throw (
                "mObjectShimmerMvJitteredMaskedAb jittered-history lane did not enable the DLSS MVJittered create flag: {0}" -f
                $mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.dlssCreateFlagBits
            )
        }
        $mvJitteredMaskedJitteredHistoryPolicyReady =
            "$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.velocityJitteredHistoryPolicy)" -eq "1"
        $mvJitteredMaskedJitteredHistoryPreviousJitterReady =
            "$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.velocityPreviousJitterApplied)" -eq "1"
        if (-not $mvJitteredMaskedJitteredHistoryPolicyReady -or
            -not $mvJitteredMaskedJitteredHistoryPreviousJitterReady) {
            throw (
                "mObjectShimmerMvJitteredMaskedAb jittered-history lane did not prove jittered velocity history: policy={0} previousApplied={1}" -f
                $mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.velocityJitteredHistoryPolicy,
                $mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.velocityPreviousJitterApplied
            )
        }
        $mvJitteredMaskedFullFramePassesKGate =
            [bool]$mvJitteredMaskedVsKInstability["candidateNotWorseWithinTolerance"]
        $mvJitteredMaskedVelocityPassesKGate =
            [bool]$mvJitteredMaskedVelocityVsKInstability["candidateNotWorseWithinTolerance"]
        $mvJitteredMaskedDepthPassesKGate =
            [bool]$mvJitteredMaskedDepthVsKInstability["candidateNotWorseWithinTolerance"]
        $mvJitteredMaskedJitteredHistoryFullFramePassesKGate =
            [bool]$mvJitteredMaskedJitteredHistoryVsKInstability["candidateNotWorseWithinTolerance"]
        $mvJitteredMaskedJitteredHistoryVelocityPassesKGate =
            [bool]$mvJitteredMaskedVelocityJitteredHistoryVsKInstability["candidateNotWorseWithinTolerance"]
        $mvJitteredMaskedJitteredHistoryDepthPassesKGate =
            [bool]$mvJitteredMaskedDepthJitteredHistoryVsKInstability["candidateNotWorseWithinTolerance"]
        $mvJitteredMaskedReadiness =
            $mvJitteredMaskedFullFramePassesKGate -and
            $mvJitteredMaskedVelocityPassesKGate -and
            $mvJitteredMaskedDepthPassesKGate
        $mvJitteredMaskedJitteredHistoryReadiness =
            $mvJitteredMaskedJitteredHistoryFullFramePassesKGate -and
            $mvJitteredMaskedJitteredHistoryVelocityPassesKGate -and
            $mvJitteredMaskedJitteredHistoryDepthPassesKGate

        $quickSummary["diagnostics"]["mObjectShimmerMvJitteredMaskedAb"] = [ordered]@{
            validationClean =
                [bool]$quickSummary["lanes"]["mObjectShimmerMvJitteredMaskedAbPresetMMvJittered"]["validationClean"]
            diagnosticOnly = $true
            productionCandidate = $false
            fullscreen = $true
            borderless = $true
            useMonitorResolutionCapture = $true
            maskedSampleStep = $mvJitteredMaskedSampleStep
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            monitorIndex = $script:captureMonitorWorkArea.Index
            monitorDevice = $script:captureMonitorWorkArea.DeviceName
            knownNgxInternalLayoutIsolation =
                [bool]$UseKnownNgxInternalLayoutIsolation
            defaultInputContract =
                New-DlssDynamicLaneContractSummary `
                    -LaneName "objectOnlyFullscreenPresetM" `
                    -Metrics $mvJitteredMaskedPresetMMetrics `
                    -ExpectedPreset "13" `
                    -RequireSkinnedVelocity
            mvJitteredInputContract =
                New-DlssDynamicLaneContractSummary `
                    -LaneName "objectOnlyFullscreenPresetMMvJittered" `
                    -Metrics $mvJitteredMaskedPresetMMvJitteredMetrics `
                    -ExpectedPreset "13" `
                    -RequireSkinnedVelocity
            mvJitteredJitteredHistoryInputContract =
                New-DlssDynamicLaneContractSummary `
                    -LaneName "objectOnlyFullscreenPresetMMvJitteredJitteredHistory" `
                    -Metrics $mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics `
                    -ExpectedPreset "13" `
                    -RequireSkinnedVelocity
            presetMVsK = $mvJitteredMaskedDefaultVsKInstability
            presetMMvJitteredVsK = $mvJitteredMaskedVsKInstability
            presetMMvJitteredVsPresetM = $mvJitteredMaskedVsDefaultInstability
            presetMMvJitteredJitteredHistoryVsK =
                $mvJitteredMaskedJitteredHistoryVsKInstability
            presetMMvJitteredJitteredHistoryVsPresetM =
                $mvJitteredMaskedJitteredHistoryVsDefaultInstability
            presetMMvJitteredJitteredHistoryVsPresetMMvJittered =
                $mvJitteredMaskedJitteredHistoryVsMvJitteredInstability
            velocityMasked = [ordered]@{
                presetMVsK = $mvJitteredMaskedVelocityDefaultVsKInstability
                presetMMvJitteredVsK = $mvJitteredMaskedVelocityVsKInstability
                presetMMvJitteredVsPresetM =
                    $mvJitteredMaskedVelocityVsDefaultInstability
                presetMMvJitteredJitteredHistoryVsK =
                    $mvJitteredMaskedVelocityJitteredHistoryVsKInstability
                presetMMvJitteredJitteredHistoryVsPresetM =
                    $mvJitteredMaskedVelocityJitteredHistoryVsDefaultInstability
                presetMMvJitteredJitteredHistoryVsPresetMMvJittered =
                    $mvJitteredMaskedVelocityJitteredHistoryVsMvJitteredInstability
            }
            depthMasked = [ordered]@{
                presetMVsK = $mvJitteredMaskedDepthDefaultVsKInstability
                presetMMvJitteredVsK = $mvJitteredMaskedDepthVsKInstability
                presetMMvJitteredVsPresetM =
                    $mvJitteredMaskedDepthVsDefaultInstability
                presetMMvJitteredJitteredHistoryVsK =
                    $mvJitteredMaskedDepthJitteredHistoryVsKInstability
                presetMMvJitteredJitteredHistoryVsPresetM =
                    $mvJitteredMaskedDepthJitteredHistoryVsDefaultInstability
                presetMMvJitteredJitteredHistoryVsPresetMMvJittered =
                    $mvJitteredMaskedDepthJitteredHistoryVsMvJitteredInstability
            }
            maskedReadiness = [ordered]@{
                fullFrameMVsK = $mvJitteredMaskedFullFramePassesKGate
                velocityMaskedMVsK = $mvJitteredMaskedVelocityPassesKGate
                depthMaskedMVsK = $mvJitteredMaskedDepthPassesKGate
                allReady = $mvJitteredMaskedReadiness
                jitteredHistoryFullFrameMVsK =
                    $mvJitteredMaskedJitteredHistoryFullFramePassesKGate
                jitteredHistoryVelocityMaskedMVsK =
                    $mvJitteredMaskedJitteredHistoryVelocityPassesKGate
                jitteredHistoryDepthMaskedMVsK =
                    $mvJitteredMaskedJitteredHistoryDepthPassesKGate
                jitteredHistoryAllReady =
                    $mvJitteredMaskedJitteredHistoryReadiness
                inference =
                    "MVJittered or the jittered-history variant can only become a default candidate after full-frame, velocity-mask, and depth-mask evidence all pass repeatedly."
            }
            mvJitteredControl = [ordered]@{
                defaultCreateFlagBits =
                    "$($mvJitteredMaskedPresetMMetrics.dlssCreateFlagBits)"
                mvJitteredCreateFlagBits =
                    "$($mvJitteredMaskedPresetMMvJitteredMetrics.dlssCreateFlagBits)"
                mvJitteredJitteredHistoryCreateFlagBits =
                    "$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.dlssCreateFlagBits)"
                createFlagReady = $mvJitteredMaskedCreateFlagReady
                jitteredHistoryCreateFlagReady =
                    $mvJitteredMaskedJitteredHistoryCreateFlagReady
                defaultMotionVectorScale =
                    "$($mvJitteredMaskedPresetMMetrics.dlssMotionVectorScale)"
                mvJitteredMotionVectorScale =
                    "$($mvJitteredMaskedPresetMMvJitteredMetrics.dlssMotionVectorScale)"
                mvJitteredJitteredHistoryMotionVectorScale =
                    "$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.dlssMotionVectorScale)"
                defaultMotionVectorScaleSemantics =
                    "pixel/unit/matchesRender=$($mvJitteredMaskedPresetMMetrics.dlssMotionVectorScalePixelSpace)/$($mvJitteredMaskedPresetMMetrics.dlssMotionVectorScaleUnitSpace)/$($mvJitteredMaskedPresetMMetrics.dlssMotionVectorScaleMatchesRenderExtent)"
                mvJitteredMotionVectorScaleSemantics =
                    "pixel/unit/matchesRender=$($mvJitteredMaskedPresetMMvJitteredMetrics.dlssMotionVectorScalePixelSpace)/$($mvJitteredMaskedPresetMMvJitteredMetrics.dlssMotionVectorScaleUnitSpace)/$($mvJitteredMaskedPresetMMvJitteredMetrics.dlssMotionVectorScaleMatchesRenderExtent)"
                mvJitteredJitteredHistoryMotionVectorScaleSemantics =
                    "pixel/unit/matchesRender=$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.dlssMotionVectorScalePixelSpace)/$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.dlssMotionVectorScaleUnitSpace)/$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.dlssMotionVectorScaleMatchesRenderExtent)"
                jitteredHistoryPolicyReady =
                    $mvJitteredMaskedJitteredHistoryPolicyReady
                jitteredHistoryPreviousJitterReady =
                    $mvJitteredMaskedJitteredHistoryPreviousJitterReady
                jitteredHistoryPolicy =
                    "$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.velocityJitteredHistoryPolicy)"
                jitteredHistoryPreviousJitterApplied =
                    "$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.velocityPreviousJitterApplied)"
                jitteredHistoryPreviousJitterPixels =
                    "$($mvJitteredMaskedPresetMMvJitteredJitteredHistoryMetrics.previousJitterPixels)"
                promoteToDefault =
                    (($mvJitteredMaskedReadiness -or
                      $mvJitteredMaskedJitteredHistoryReadiness) -and $false)
                promotionBlocker =
                    "Masked A/B is still diagnostic; require repeat high-resolution dynamic acceptance, validation-policy decision, Nsight/RenderDoc input inspection, and subjective clarity review before defaulting MVJittered or jittered-history velocity."
            }
            nextProbe =
                if ($mvJitteredMaskedJitteredHistoryReadiness) {
                    "Capture the jittered-history lane with ngfx-capture and inspect color/depth/velocity just before DLSS evaluate; then repeat inside m-highres-dynamic before defaulting it."
                } elseif ($mvJitteredMaskedReadiness) {
                    "Repeat MVJittered inside m-highres-dynamic or add an opt-in high-res production audit variant before defaulting it."
                } else {
                    "Inspect whichever mask fails MVJittered or jittered-history with ngfx-capture; the remaining issue is likely skinned silhouette disocclusion/history rather than full-frame MV semantics alone."
                }
        }
    }

    if ($selectedSuites -contains "m-object-shimmer-m-only-masked-diagnostics") {
        $quickSummary["baselines"]["mObjectShimmerMOnlyMaskedDiagnostics"] = [ordered]@{
            skinnedProductionManifest =
                $defaultSceneSkinnedFbxMProductionBaselineManifestPath
            skinnedProductionName =
                $defaultSceneSkinnedFbxMProductionBaselineManifest.name
            scene =
                "default Forward 3D application scene with Fist Fight B.fbx production skinning"
            lane = "static camera + skinned object motion only"
            model = $importedSkinnedPreviewModelPath
            fullscreen = $true
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            monitorIndex = $script:captureMonitorWorkArea.Index
            monitorDevice = $script:captureMonitorWorkArea.DeviceName
        }
        $quickSummary["thresholds"]["mObjectShimmerMOnlyMaskedDiagnostics"] = [ordered]@{
            sequenceGate = "record-only"
            reason =
                "M-only masked diagnostics for fullscreen skinned silhouette shimmer; no preset-K control lane is captured."
            baseline = "preset M default"
            variantTolerance = [ordered]@{
                changedEdgeRatio = 0.02
                meanEdgeDelta = 2.0
            }
            expectedDlssExtent = "{0}x{1}" -f
                $highResolutionWindowWidth,
                $highResolutionWindowHeight
            maskedSequenceGate =
                "record-only; compares M input-semantics variants against default M inside velocity/depth debug masks"
        }

        $quickSummary["lanes"]["mObjectShimmerMOnlyVelocityDebug"] =
            Invoke-QuickNativeDiagnosticSequenceSuite `
                -Name "m_object_shimmer_m_only_object_only_velocity_debug" `
                -LaneKey "mObjectShimmerMOnly.velocityDebug" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionVelocityDebugEnvironment `
                -UseMonitorResolutionCapture
        $quickSummary["lanes"]["mObjectShimmerMOnlyDepthDebug"] =
            Invoke-QuickNativeDiagnosticSequenceSuite `
                -Name "m_object_shimmer_m_only_object_only_depth_debug" `
                -LaneKey "mObjectShimmerMOnly.depthDebug" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionDepthDebugEnvironment `
                -UseMonitorResolutionCapture
        $quickSummary["lanes"]["mObjectShimmerMOnlyPresetM"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_m_only_object_only_preset_m_present" `
                -LaneKey "mObjectShimmerMOnly.presetM" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mObjectShimmerMOnlyPresetMMvJittered"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_m_only_object_only_preset_m_mv_jittered_present" `
                -LaneKey "mObjectShimmerMOnly.presetMMvJittered" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMMvJitteredEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight
        $quickSummary["lanes"]["mObjectShimmerMOnlyPresetMMvJitteredJitteredHistory"] =
            Invoke-QuickDlaaTuningSequenceSuite `
                -Name "m_object_shimmer_m_only_object_only_preset_m_mv_jittered_jittered_history_present" `
                -LaneKey "mObjectShimmerMOnly.presetMMvJitteredJitteredHistory" `
                -Environment $defaultSceneSkinnedFbxDynamicMovingObjectOnlyHighResolutionPresetMMvJitteredJitteredHistoryEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -ExpectedPreset "13" `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -SkipSequenceGate `
                -UseMonitorResolutionCapture `
                -ExpectedDlssWidth $highResolutionWindowWidth `
                -ExpectedDlssHeight $highResolutionWindowHeight

        $mOnlyVelocityImages =
            @($quickSummary["lanes"]["mObjectShimmerMOnlyVelocityDebug"]["images"])
        $mOnlyDepthImages =
            @($quickSummary["lanes"]["mObjectShimmerMOnlyDepthDebug"]["images"])
        $mOnlyPresetMImages =
            @($quickSummary["lanes"]["mObjectShimmerMOnlyPresetM"]["images"])
        $mOnlyPresetMMvJitteredImages =
            @($quickSummary["lanes"]["mObjectShimmerMOnlyPresetMMvJittered"]["images"])
        $mOnlyPresetMMvJitteredJitteredHistoryImages =
            @($quickSummary["lanes"]["mObjectShimmerMOnlyPresetMMvJitteredJitteredHistory"]["images"])
        $mOnlyPresetMSequence =
            $quickSummary["lanes"]["mObjectShimmerMOnlyPresetM"]["sequenceComparison"]
        $mOnlyPresetMMvJitteredSequence =
            $quickSummary["lanes"]["mObjectShimmerMOnlyPresetMMvJittered"]["sequenceComparison"]
        $mOnlyPresetMMvJitteredJitteredHistorySequence =
            $quickSummary["lanes"]["mObjectShimmerMOnlyPresetMMvJitteredJitteredHistory"]["sequenceComparison"]
        $mOnlyPresetMMetrics =
            $quickSummary["lanes"]["mObjectShimmerMOnlyPresetM"]["metrics"]
        $mOnlyPresetMMvJitteredMetrics =
            $quickSummary["lanes"]["mObjectShimmerMOnlyPresetMMvJittered"]["metrics"]
        $mOnlyPresetMMvJitteredJitteredHistoryMetrics =
            $quickSummary["lanes"]["mObjectShimmerMOnlyPresetMMvJitteredJitteredHistory"]["metrics"]
        $mOnlyMaskedSampleStep = 4

        $mOnlyVelocityPresetMSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mOnlyPresetMImages `
                -MaskPaths $mOnlyVelocityImages `
                -MaskName "velocity" `
                -SampleStep $mOnlyMaskedSampleStep
        $mOnlyVelocityPresetMMvJitteredSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mOnlyPresetMMvJitteredImages `
                -MaskPaths $mOnlyVelocityImages `
                -MaskName "velocity" `
                -SampleStep $mOnlyMaskedSampleStep
        $mOnlyVelocityPresetMMvJitteredJitteredHistorySequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mOnlyPresetMMvJitteredJitteredHistoryImages `
                -MaskPaths $mOnlyVelocityImages `
                -MaskName "velocity" `
                -SampleStep $mOnlyMaskedSampleStep
        $mOnlyDepthPresetMSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mOnlyPresetMImages `
                -MaskPaths $mOnlyDepthImages `
                -MaskName "depth" `
                -SampleStep $mOnlyMaskedSampleStep
        $mOnlyDepthPresetMMvJitteredSequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mOnlyPresetMMvJitteredImages `
                -MaskPaths $mOnlyDepthImages `
                -MaskName "depth" `
                -SampleStep $mOnlyMaskedSampleStep
        $mOnlyDepthPresetMMvJitteredJitteredHistorySequence =
            Compare-ImageSequenceInDebugMask `
                -Paths $mOnlyPresetMMvJitteredJitteredHistoryImages `
                -MaskPaths $mOnlyDepthImages `
                -MaskName "depth" `
                -SampleStep $mOnlyMaskedSampleStep

        $mOnlyMvJitteredVsDefault =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJittered" `
                -Reference $mOnlyPresetMSequence `
                -Candidate $mOnlyPresetMMvJitteredSequence
        $mOnlyJitteredHistoryVsDefault =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mOnlyPresetMSequence `
                -Candidate $mOnlyPresetMMvJitteredJitteredHistorySequence
        $mOnlyJitteredHistoryVsMvJittered =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetMMvJittered" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mOnlyPresetMMvJitteredSequence `
                -Candidate $mOnlyPresetMMvJitteredJitteredHistorySequence
        $mOnlyVelocityMvJitteredVsDefault =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJittered" `
                -Reference $mOnlyVelocityPresetMSequence `
                -Candidate $mOnlyVelocityPresetMMvJitteredSequence
        $mOnlyVelocityJitteredHistoryVsDefault =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mOnlyVelocityPresetMSequence `
                -Candidate $mOnlyVelocityPresetMMvJitteredJitteredHistorySequence
        $mOnlyDepthMvJitteredVsDefault =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJittered" `
                -Reference $mOnlyDepthPresetMSequence `
                -Candidate $mOnlyDepthPresetMMvJitteredSequence
        $mOnlyDepthJitteredHistoryVsDefault =
            New-SequenceInstabilitySummary `
                -ReferenceName "presetM" `
                -CandidateName "presetMMvJitteredJitteredHistory" `
                -Reference $mOnlyDepthPresetMSequence `
                -Candidate $mOnlyDepthPresetMMvJitteredJitteredHistorySequence

        $mOnlyMvJitteredCreateFlagReady =
            "$($mOnlyPresetMMvJitteredMetrics.dlssCreateFlagBits)" -eq
            "hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/1/0/0"
        $mOnlyJitteredHistoryCreateFlagReady =
            "$($mOnlyPresetMMvJitteredJitteredHistoryMetrics.dlssCreateFlagBits)" -eq
            "hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/1/0/0"
        if (-not $mOnlyMvJitteredCreateFlagReady) {
            throw (
                "mObjectShimmerMOnly did not enable the DLSS MVJittered create flag: {0}" -f
                $mOnlyPresetMMvJitteredMetrics.dlssCreateFlagBits
            )
        }
        if (-not $mOnlyJitteredHistoryCreateFlagReady) {
            throw (
                "mObjectShimmerMOnly jittered-history lane did not enable the DLSS MVJittered create flag: {0}" -f
                $mOnlyPresetMMvJitteredJitteredHistoryMetrics.dlssCreateFlagBits
            )
        }
        $mOnlyJitteredHistoryPolicyReady =
            "$($mOnlyPresetMMvJitteredJitteredHistoryMetrics.velocityJitteredHistoryPolicy)" -eq "1"
        $mOnlyJitteredHistoryPreviousJitterReady =
            "$($mOnlyPresetMMvJitteredJitteredHistoryMetrics.velocityPreviousJitterApplied)" -eq "1"
        if (-not $mOnlyJitteredHistoryPolicyReady -or
            -not $mOnlyJitteredHistoryPreviousJitterReady) {
            throw (
                "mObjectShimmerMOnly jittered-history lane did not prove jittered velocity history: policy={0} previousApplied={1}" -f
                $mOnlyPresetMMvJitteredJitteredHistoryMetrics.velocityJitteredHistoryPolicy,
                $mOnlyPresetMMvJitteredJitteredHistoryMetrics.velocityPreviousJitterApplied
            )
        }

        $mOnlyMvJitteredImprovesAll =
            ([double]$mOnlyPresetMMvJitteredSequence.maxChangedEdgeRatio -lt
             [double]$mOnlyPresetMSequence.maxChangedEdgeRatio) -and
            ([double]$mOnlyPresetMMvJitteredSequence.maxMeanEdgeDelta -lt
             [double]$mOnlyPresetMSequence.maxMeanEdgeDelta) -and
            ([double]$mOnlyVelocityPresetMMvJitteredSequence.maxChangedEdgeRatio -lt
             [double]$mOnlyVelocityPresetMSequence.maxChangedEdgeRatio) -and
            ([double]$mOnlyVelocityPresetMMvJitteredSequence.maxMeanEdgeDelta -lt
             [double]$mOnlyVelocityPresetMSequence.maxMeanEdgeDelta) -and
            ([double]$mOnlyDepthPresetMMvJitteredSequence.maxChangedEdgeRatio -lt
             [double]$mOnlyDepthPresetMSequence.maxChangedEdgeRatio) -and
            ([double]$mOnlyDepthPresetMMvJitteredSequence.maxMeanEdgeDelta -lt
             [double]$mOnlyDepthPresetMSequence.maxMeanEdgeDelta)
        $mOnlyJitteredHistoryImprovesAll =
            ([double]$mOnlyPresetMMvJitteredJitteredHistorySequence.maxChangedEdgeRatio -lt
             [double]$mOnlyPresetMSequence.maxChangedEdgeRatio) -and
            ([double]$mOnlyPresetMMvJitteredJitteredHistorySequence.maxMeanEdgeDelta -lt
             [double]$mOnlyPresetMSequence.maxMeanEdgeDelta) -and
            ([double]$mOnlyVelocityPresetMMvJitteredJitteredHistorySequence.maxChangedEdgeRatio -lt
             [double]$mOnlyVelocityPresetMSequence.maxChangedEdgeRatio) -and
            ([double]$mOnlyVelocityPresetMMvJitteredJitteredHistorySequence.maxMeanEdgeDelta -lt
             [double]$mOnlyVelocityPresetMSequence.maxMeanEdgeDelta) -and
            ([double]$mOnlyDepthPresetMMvJitteredJitteredHistorySequence.maxChangedEdgeRatio -lt
             [double]$mOnlyDepthPresetMSequence.maxChangedEdgeRatio) -and
            ([double]$mOnlyDepthPresetMMvJitteredJitteredHistorySequence.maxMeanEdgeDelta -lt
             [double]$mOnlyDepthPresetMSequence.maxMeanEdgeDelta)

        $quickSummary["diagnostics"]["mObjectShimmerMOnlyMaskedDiagnostics"] = [ordered]@{
            validationClean =
                [bool]$quickSummary["lanes"]["mObjectShimmerMOnlyPresetM"]["validationClean"] -and
                [bool]$quickSummary["lanes"]["mObjectShimmerMOnlyPresetMMvJittered"]["validationClean"] -and
                [bool]$quickSummary["lanes"]["mObjectShimmerMOnlyPresetMMvJitteredJitteredHistory"]["validationClean"]
            diagnosticOnly = $true
            productionCandidate = $false
            comparisonPolicy = "M-only; no preset-K lane captured"
            fullscreen = $true
            borderless = $true
            useMonitorResolutionCapture = $true
            maskedSampleStep = $mOnlyMaskedSampleStep
            highResolutionWidth = $highResolutionWindowWidth
            highResolutionHeight = $highResolutionWindowHeight
            monitorIndex = $script:captureMonitorWorkArea.Index
            monitorDevice = $script:captureMonitorWorkArea.DeviceName
            knownNgxInternalLayoutIsolation =
                [bool]$UseKnownNgxInternalLayoutIsolation
            defaultInputContract =
                New-DlssDynamicLaneContractSummary `
                    -LaneName "objectOnlyFullscreenPresetM" `
                    -Metrics $mOnlyPresetMMetrics `
                    -ExpectedPreset "13" `
                    -RequireSkinnedVelocity
            mvJitteredInputContract =
                New-DlssDynamicLaneContractSummary `
                    -LaneName "objectOnlyFullscreenPresetMMvJittered" `
                    -Metrics $mOnlyPresetMMvJitteredMetrics `
                    -ExpectedPreset "13" `
                    -RequireSkinnedVelocity
            mvJitteredJitteredHistoryInputContract =
                New-DlssDynamicLaneContractSummary `
                    -LaneName "objectOnlyFullscreenPresetMMvJitteredJitteredHistory" `
                    -Metrics $mOnlyPresetMMvJitteredJitteredHistoryMetrics `
                    -ExpectedPreset "13" `
                    -RequireSkinnedVelocity
            fullFrame = [ordered]@{
                presetM = $mOnlyPresetMSequence
                presetMMvJittered = $mOnlyPresetMMvJitteredSequence
                presetMMvJitteredJitteredHistory =
                    $mOnlyPresetMMvJitteredJitteredHistorySequence
                presetMMvJitteredVsPresetM = $mOnlyMvJitteredVsDefault
                presetMMvJitteredJitteredHistoryVsPresetM =
                    $mOnlyJitteredHistoryVsDefault
                presetMMvJitteredJitteredHistoryVsPresetMMvJittered =
                    $mOnlyJitteredHistoryVsMvJittered
            }
            velocityMasked = [ordered]@{
                presetM = $mOnlyVelocityPresetMSequence
                presetMMvJittered = $mOnlyVelocityPresetMMvJitteredSequence
                presetMMvJitteredJitteredHistory =
                    $mOnlyVelocityPresetMMvJitteredJitteredHistorySequence
                presetMMvJitteredVsPresetM =
                    $mOnlyVelocityMvJitteredVsDefault
                presetMMvJitteredJitteredHistoryVsPresetM =
                    $mOnlyVelocityJitteredHistoryVsDefault
            }
            depthMasked = [ordered]@{
                presetM = $mOnlyDepthPresetMSequence
                presetMMvJittered = $mOnlyDepthPresetMMvJitteredSequence
                presetMMvJitteredJitteredHistory =
                    $mOnlyDepthPresetMMvJitteredJitteredHistorySequence
                presetMMvJitteredVsPresetM =
                    $mOnlyDepthMvJitteredVsDefault
                presetMMvJitteredJitteredHistoryVsPresetM =
                    $mOnlyDepthJitteredHistoryVsDefault
            }
            mOnlyReadiness = [ordered]@{
                mvJitteredCreateFlagReady = $mOnlyMvJitteredCreateFlagReady
                jitteredHistoryCreateFlagReady =
                    $mOnlyJitteredHistoryCreateFlagReady
                jitteredHistoryPolicyReady =
                    $mOnlyJitteredHistoryPolicyReady
                jitteredHistoryPreviousJitterReady =
                    $mOnlyJitteredHistoryPreviousJitterReady
                mvJitteredImprovesAllMEdgeMetrics =
                    $mOnlyMvJitteredImprovesAll
                jitteredHistoryImprovesAllMEdgeMetrics =
                    $mOnlyJitteredHistoryImprovesAll
                anyVariantImprovesAllMEdgeMetrics =
                    ($mOnlyMvJitteredImprovesAll -or
                     $mOnlyJitteredHistoryImprovesAll)
                promoteToDefault = $false
                promotionBlocker =
                    "M-only diagnostics identify candidate input semantics but do not promote without repeat high-resolution dynamic acceptance and strict validation policy."
            }
            nextProbe =
                if ($mOnlyMvJitteredImprovesAll -or $mOnlyJitteredHistoryImprovesAll) {
                    "Repeat the best M-only variant in moving-camera and combined-motion M-only suites before defaulting it."
                } else {
                    "Neither M variant improves full-frame, velocity-masked, and depth-masked edge metrics together; inspect depth/disocclusion and skinned silhouette velocity before more post tuning."
                }
        }
    }

    if ($quickSummary["diagnostics"].Contains("mVsKMovingCameraFullscreenPack") -and
        $quickSummary["diagnostics"].Contains("mVsKCombinedFullscreenPack") -and
        $quickSummary["diagnostics"].Contains("mObjectShimmerFullscreenDiagnostics")) {
        $movingCameraFullscreenReadiness =
            $quickSummary["diagnostics"]["mVsKMovingCameraFullscreenPack"]["dynamicProductionReadiness"]
        $combinedFullscreenReadiness =
            $quickSummary["diagnostics"]["mVsKCombinedFullscreenPack"]["dynamicProductionReadiness"]
        $objectFullscreenDiagnostics =
            $quickSummary["diagnostics"]["mObjectShimmerFullscreenDiagnostics"]
        $objectFullscreenContract =
            $objectFullscreenDiagnostics["objectOnlyInputContract"]
        $objectFullscreenMVsK =
            $objectFullscreenDiagnostics["presetMVsK"]
        $objectFullscreenValidationClean =
            [bool]$objectFullscreenDiagnostics["validationClean"]
        $objectFullscreenMVsKStable =
            [bool]$objectFullscreenMVsK["candidateNotWorseWithinTolerance"]
        $objectFullscreenTemporalInputReady =
            [bool]$objectFullscreenContract["temporalInputContractReady"]
        $objectFullscreenQualityInputReady =
            [bool]$objectFullscreenContract["qualityInputReady"]
        $objectFullscreenSkinnedVelocityInputReady =
            [bool]$objectFullscreenContract["skinnedVelocityInputReady"]
        $objectFullscreenMaskedReady =
            [bool]$objectFullscreenDiagnostics["maskedReadiness"]["velocityMaskedMVsK"] -and
            [bool]$objectFullscreenDiagnostics["maskedReadiness"]["depthMaskedMVsK"]
        $movingCameraFullscreenDiagnostics =
            $quickSummary["diagnostics"]["mVsKMovingCameraFullscreenPack"]
        $combinedFullscreenDiagnostics =
            $quickSummary["diagnostics"]["mVsKCombinedFullscreenPack"]
        $objectFullscreenSharpnessControl =
            $objectFullscreenDiagnostics["sharpnessControl"]
        $highResLaneStabilityDetails = [ordered]@{
            movingCameraFullscreen = [ordered]@{
                mVsKDynamicStable =
                    [bool]$movingCameraFullscreenReadiness["mVsKDynamicStable"]
                delta =
                    $movingCameraFullscreenDiagnostics["temporalInstability"]["presetMVsPresetK"]["delta"]
                animationPhase =
                    $movingCameraFullscreenDiagnostics["animationPhase"]
                hotspotClassification =
                    $movingCameraFullscreenDiagnostics["hotspotLocalization"]["classification"]
            }
            combinedCameraObjectFullscreen = [ordered]@{
                mVsKDynamicStable =
                    [bool]$combinedFullscreenReadiness["mVsKDynamicStable"]
                delta =
                    $combinedFullscreenDiagnostics["temporalInstability"]["presetMVsPresetK"]["delta"]
                animationPhase =
                    $combinedFullscreenDiagnostics["animationPhase"]
                hotspotClassification =
                    $combinedFullscreenDiagnostics["hotspotLocalization"]["classification"]
            }
            objectOnlyFullscreen = [ordered]@{
                mVsKDynamicStable = $objectFullscreenMVsKStable
                delta = $objectFullscreenMVsK["delta"]
                maskedStabilityReady = $objectFullscreenMaskedReady
                velocityMaskedMVsK =
                    $objectFullscreenDiagnostics["maskedReadiness"]["velocityMaskedMVsK"]
                depthMaskedMVsK =
                    $objectFullscreenDiagnostics["maskedReadiness"]["depthMaskedMVsK"]
                animationPhase = $objectFullscreenDiagnostics["animationPhase"]
                hotspotClassification =
                    $objectFullscreenDiagnostics["hotspotLocalization"]["classification"]
                sharpnessControl = $objectFullscreenSharpnessControl
            }
        }

        $highResValidationClean =
            [bool]$movingCameraFullscreenReadiness["validationClean"] -and
            [bool]$combinedFullscreenReadiness["validationClean"] -and
            $objectFullscreenValidationClean
        $highResMVsKStable =
            [bool]$movingCameraFullscreenReadiness["mVsKDynamicStable"] -and
            [bool]$combinedFullscreenReadiness["mVsKDynamicStable"] -and
            $objectFullscreenMVsKStable
        $highResNativeNormalizedStable =
            [bool]$movingCameraFullscreenReadiness["nativeNormalizedDynamicStable"] -and
            [bool]$combinedFullscreenReadiness["nativeNormalizedDynamicStable"]
        $highResTemporalInputReady =
            [bool]$movingCameraFullscreenReadiness["temporalInputContractReady"] -and
            [bool]$combinedFullscreenReadiness["temporalInputContractReady"] -and
            $objectFullscreenTemporalInputReady
        $highResQualityInputReady =
            [bool]$movingCameraFullscreenReadiness["qualityInputReady"] -and
            [bool]$combinedFullscreenReadiness["qualityInputReady"] -and
            $objectFullscreenQualityInputReady
        $highResSkinnedVelocityInputReady =
            [bool]$movingCameraFullscreenReadiness["skinnedVelocityInputReady"] -and
            [bool]$combinedFullscreenReadiness["skinnedVelocityInputReady"] -and
            $objectFullscreenSkinnedVelocityInputReady

        $highResInputBlockedReasons = @()
        if (-not $highResValidationClean) {
            $highResInputBlockedReasons +=
                if ($UseKnownNgxInternalLayoutIsolation) {
                    "Preset M high-resolution dynamic captures still have an unknown validation diagnostic under the NGX isolation policy"
                } else {
                    "Preset M high-resolution dynamic captures still require the NGX diagnostic whitelist"
                }
        }
        if (-not $highResMVsKStable) {
            $highResInputBlockedReasons +=
                "Preset M is not stable against K in at least one high-resolution dynamic lane"
        }
        if (-not $objectFullscreenMVsKStable -and
            [bool]$objectFullscreenSharpnessControl["passesPresetKGate"]) {
            $highResInputBlockedReasons +=
                "Object-only fullscreen default-M misses the K gate while the sharpness-zero control passes; keep this diagnostic-only until validation and subjective clarity are accepted"
        }
        if (-not $highResNativeNormalizedStable) {
            $highResInputBlockedReasons +=
                "Preset M is not native-normalized stable in at least one high-resolution dynamic lane with a native reference"
        }
        if (-not $highResTemporalInputReady) {
            $highResInputBlockedReasons +=
                "Preset M high-resolution dynamic temporal input contract is incomplete"
        }
        if (-not $highResQualityInputReady) {
            $highResInputBlockedReasons +=
                "Preset M high-resolution dynamic quality inputs are incomplete"
        }
        if (-not $highResSkinnedVelocityInputReady) {
            $highResInputBlockedReasons +=
                "Preset M high-resolution dynamic skinned velocity input contract is incomplete"
        }
        if (-not $objectFullscreenMaskedReady) {
            $highResInputBlockedReasons +=
                "Object-only fullscreen masked velocity/depth stability is not ready against K"
        }

        $repeatLedgerPath =
            Join-Path $outputRoot "m_highres_dynamic_repeat_ledger.json"
        $repeatLedger =
            Update-MHighResDynamicRepeatLedger `
                -Path $repeatLedgerPath `
                -Target $Target `
                -MonitorIndex $script:captureMonitorWorkArea.Index `
                -MonitorDevice $script:captureMonitorWorkArea.DeviceName `
                -Width $highResolutionWindowWidth `
                -Height $highResolutionWindowHeight `
                -SequenceFrameCount $SequenceFrameCount `
                -SequenceInitialDelaySeconds $SequenceInitialDelaySeconds `
                -SequenceIntervalSeconds $SequenceIntervalSeconds `
                -KnownNgxInternalLayoutIsolation:$UseKnownNgxInternalLayoutIsolation `
                -ValidationClean $highResValidationClean `
                -MVsKDynamicStable $highResMVsKStable `
                -NativeNormalizedDynamicStable $highResNativeNormalizedStable `
                -TemporalInputContractReady $highResTemporalInputReady `
                -QualityInputReady $highResQualityInputReady `
                -SkinnedVelocityInputReady $highResSkinnedVelocityInputReady `
                -ObjectMaskedStabilityReady $objectFullscreenMaskedReady `
                -BlockedReasons $highResInputBlockedReasons
        $repeatStabilityReady = [bool]$repeatLedger["ready"]
        $highResFocusedAuditReady =
            ($highResValidationClean -and
             $highResMVsKStable -and
             $highResNativeNormalizedStable -and
             $highResTemporalInputReady -and
             $highResQualityInputReady -and
             $highResSkinnedVelocityInputReady -and
             $objectFullscreenMaskedReady -and
             $repeatStabilityReady)
        $highResStrictValidationClean =
            $highResValidationClean -and -not [bool]$UseKnownNgxInternalLayoutIsolation
        $highResStrictProductionReady =
            $highResFocusedAuditReady -and $highResStrictValidationClean

        $highResFocusedBlockedReasons = @($highResInputBlockedReasons)
        if (-not $repeatStabilityReady) {
            $repeatObserved =
                [int]$repeatLedger["observedConsecutivePasses"]
            $repeatRequired =
                [int]$repeatLedger["requiredConsecutivePasses"]
            $highResFocusedBlockedReasons +=
                "High-resolution dynamic repeat acceptance is $repeatObserved/$repeatRequired consecutive passes; production promotion requires $repeatRequired."
        }
        $highResProductionBlockedReasons = @($highResFocusedBlockedReasons)
        if ($UseKnownNgxInternalLayoutIsolation) {
            $highResProductionBlockedReasons +=
                "Preset M high-resolution dynamic focused audit still depends on the explicit NGX internal layout isolation policy"
        }

        $quickSummary["diagnostics"]["mHighResDynamicProductionAudit"] = [ordered]@{
            validationClean = $highResValidationClean
            diagnosticOnly = $true
            productionCandidate = $false
            productionReady = $highResStrictProductionReady
            strictProductionReady = $highResStrictProductionReady
            focusedAuditReady = $highResFocusedAuditReady
            focusedAuditPolicy =
                if ($UseKnownNgxInternalLayoutIsolation) {
                    "known-ngx-internal-layout-isolation"
                } else {
                    "strict-no-ngx-isolation"
                }
            highResolution = [ordered]@{
                width = $highResolutionWindowWidth
                height = $highResolutionWindowHeight
                monitorIndex = $script:captureMonitorWorkArea.Index
                monitorDevice = $script:captureMonitorWorkArea.DeviceName
                borderless = $true
            }
            knownNgxInternalLayoutIsolation =
                [bool]$UseKnownNgxInternalLayoutIsolation
            readiness = [ordered]@{
                validationClean = $highResValidationClean
                strictValidationClean = $highResStrictValidationClean
                mVsKDynamicStable = $highResMVsKStable
                nativeNormalizedDynamicStable = $highResNativeNormalizedStable
                temporalInputContractReady = $highResTemporalInputReady
                qualityInputReady = $highResQualityInputReady
                skinnedVelocityInputReady = $highResSkinnedVelocityInputReady
                objectMaskedStabilityReady = $objectFullscreenMaskedReady
                repeatStabilityReady = $repeatStabilityReady
                focusedAuditReady = $highResFocusedAuditReady
                strictProductionReady = $highResStrictProductionReady
            }
            laneStabilityDetails = $highResLaneStabilityDetails
            laneReadiness = [ordered]@{
                movingCameraFullscreen = $movingCameraFullscreenReadiness
                combinedCameraObjectFullscreen = $combinedFullscreenReadiness
                objectOnlyFullscreen = [ordered]@{
                    validationClean = $objectFullscreenValidationClean
                    mVsKDynamicStable = $objectFullscreenMVsKStable
                    temporalInputContractReady =
                        $objectFullscreenTemporalInputReady
                    qualityInputReady = $objectFullscreenQualityInputReady
                    skinnedVelocityInputReady =
                        $objectFullscreenSkinnedVelocityInputReady
                    maskedStabilityReady = $objectFullscreenMaskedReady
                    objectInputContract = $objectFullscreenContract
                    presetMVsK = $objectFullscreenMVsK
                    velocityMaskedMVsK =
                        $objectFullscreenDiagnostics["maskedReadiness"]["velocityMaskedMVsK"]
                    depthMaskedMVsK =
                        $objectFullscreenDiagnostics["maskedReadiness"]["depthMaskedMVsK"]
                }
            }
            instabilityLocalization = [ordered]@{
                movingCameraFullscreen =
                    $quickSummary["diagnostics"]["mVsKMovingCameraFullscreenPack"]["instabilityHotspots"]
                combinedCameraObjectFullscreen =
                    $quickSummary["diagnostics"]["mVsKCombinedFullscreenPack"]["instabilityHotspots"]
                objectOnlyFullscreen =
                    $objectFullscreenDiagnostics["instabilityHotspots"]
                classifications = [ordered]@{
                    movingCameraFullscreen =
                        $quickSummary["diagnostics"]["mVsKMovingCameraFullscreenPack"]["hotspotLocalization"]
                    combinedCameraObjectFullscreen =
                        $quickSummary["diagnostics"]["mVsKCombinedFullscreenPack"]["hotspotLocalization"]
                    objectOnlyFullscreen =
                        $objectFullscreenDiagnostics["hotspotLocalization"]
                }
                interpretation =
                    "Use full-frame, velocity-masked, and depth-masked hotspot classifications to localize M-vs-K excess edge delta before changing post sharpening or mip policy."
            }
            repeatLedger = $repeatLedger
            repeatPolicy = [ordered]@{
                ledger = $repeatLedgerPath
                requiredConsecutivePasses =
                    $repeatLedger["requiredConsecutivePasses"]
                observedConsecutivePasses =
                    $repeatLedger["observedConsecutivePasses"]
                ready = $repeatStabilityReady
                productionInputPass =
                    $repeatLedger["productionInputPass"]
                focusedAuditInputPass =
                    $repeatLedger["focusedAuditInputPass"]
                strictProductionInputPass =
                    $repeatLedger["strictProductionInputPass"]
                policyName =
                    $repeatLedger["policyName"]
                productionPolicyBlockers =
                    $repeatLedger["productionPolicyBlockers"]
                policyCompatible =
                    $repeatLedger["policyCompatible"]
                resetReason =
                    $repeatLedger["resetReason"]
                reason =
                    "Single focused runs are diagnostic evidence; production promotion requires repeated clean high-resolution dynamic passes."
            }
            inputBlockedReasons = $highResInputBlockedReasons
            focusedBlockedReasons = $highResFocusedBlockedReasons
            blockedReasons = $highResProductionBlockedReasons
            requiredProductionState = [ordered]@{
                validationClean = $true
                strictValidationClean = $true
                knownNgxInternalLayoutIsolation = $false
                mVsKDynamicStable = $true
                nativeNormalizedDynamicStable = $true
                temporalInputContractReady = $true
                qualityInputReady = $true
                skinnedVelocityInputReady = $true
                objectMaskedStabilityReady = $true
                repeatStabilityReady = $true
                focusedAuditReady = $true
                strictProductionReady = $true
            }
        }
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
                -Manifest $defaultSceneDlaaMotionBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
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
                -Manifest $defaultSceneDlaaObjectMotionBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
    }

    if ($selectedSuites -contains "default-preset-m-dynamic") {
        $quickSummary["baselines"]["defaultPresetMDynamic"] = [ordered]@{
            motionManifest = $defaultSceneDlaaMotionBaselineManifestPath
            motionName = $defaultSceneDlaaMotionBaselineManifest.name
            objectMotionManifest = $defaultSceneDlaaObjectMotionBaselineManifestPath
            objectMotionName = $defaultSceneDlaaObjectMotionBaselineManifest.name
        }
        $quickSummary["thresholds"]["defaultPresetMDynamic"] = [ordered]@{
            movingCamera =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneDlaaMotionBaselineManifest
            movingObjectOnly =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneDlaaObjectMotionBaselineManifest
            combinedCameraObject =
                New-QuickSequenceThresholdSummary `
                    -Manifest $defaultSceneDlaaObjectMotionBaselineManifest
            validationRequirement = "clean-capture-logs"
            reason = "Production-candidate M dynamic captures must not use the NGX diagnostic whitelist"
        }
        $quickSummary["lanes"]["defaultSceneDlaaPresetMMovingCamera"] =
            Invoke-QuickDlaaSequenceSuite `
                -Name "default_scene_dlaa_preset_m_moving_camera_present" `
                -LaneKey "defaultSceneDlaaPresetM.movingCamera" `
                -Environment $defaultSceneDlaaPresetMMotionPresentEnvironment `
                -Manifest $defaultSceneDlaaMotionBaselineManifest
        $quickSummary["lanes"]["defaultSceneDlaaPresetMMovingObjectOnly"] =
            Invoke-QuickDlaaSequenceSuite `
                -Name "default_scene_dlaa_preset_m_moving_object_only_present" `
                -LaneKey "defaultSceneDlaaPresetM.movingObjectOnly" `
                -Environment $defaultSceneDlaaPresetMObjectOnlyPresentEnvironment `
                -Manifest $defaultSceneDlaaObjectMotionBaselineManifest
        $quickSummary["lanes"]["defaultSceneDlaaPresetMCombinedCameraObject"] =
            Invoke-QuickDlaaSequenceSuite `
                -Name "default_scene_dlaa_preset_m_combined_camera_object_present" `
                -LaneKey "defaultSceneDlaaPresetM.combinedCameraObject" `
                -Environment $defaultSceneDlaaPresetMCombinedMotionPresentEnvironment `
                -Manifest $defaultSceneDlaaObjectMotionBaselineManifest
        $quickSummary["diagnostics"]["defaultPresetMDynamic"] = [ordered]@{
            validationClean = $true
            diagnosticOnly = $false
            productionCandidate = $true
            lanes = @(
                "movingCamera",
                "movingObjectOnly",
                "combinedCameraObject"
            )
            validationRequirement =
                "No stdout/stderr diagnostics may match validation/error/VUID/shader patterns"
        }
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
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
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
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
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

    if ($selectedSuites -contains "imported-skinned-preview") {
        $quickSummary["baselines"]["importedSkinnedPreview"] = [ordered]@{
            manifest = $importedSkinnedPreviewBaselineManifestPath
            name = $importedSkinnedPreviewBaselineManifest.name
            model = $importedSkinnedPreviewModelPath
        }
        $quickSummary["thresholds"]["importedSkinnedPreview"] = [ordered]@{
            centralDifferentPixelsMin =
                $importedSkinnedPreviewBaselineManifest.thresholds.centralDifferentPixelsMin
        }
        $quickSummary["lanes"]["importedSkinnedPreviewPresent"] =
            Invoke-QuickDlssBlockedPreviewSuite `
                -Name "imported_skinned_preview_present" `
                -LaneKey "importedSkinnedPreview" `
                -Environment $importedSkinnedPreviewPresentEnvironment `
                -Manifest $importedSkinnedPreviewBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -Model $importedSkinnedPreviewModelPath `
                -SkipImageStatsGate
    }

    if ($selectedSuites -contains "default-scene-skinned-fbx-m-production") {
        $quickSummary["baselines"]["defaultSceneSkinnedFbxMProduction"] = [ordered]@{
            manifest = $defaultSceneSkinnedFbxMProductionBaselineManifestPath
            name = $defaultSceneSkinnedFbxMProductionBaselineManifest.name
            model = $importedSkinnedPreviewModelPath
            scene = "default Forward 3D application scene"
        }
        $quickSummary["thresholds"]["defaultSceneSkinnedFbxMProduction"] = [ordered]@{
            centralDifferentPixelsMin =
                $defaultSceneSkinnedFbxMProductionBaselineManifest.thresholds.centralDifferentPixelsMin
            sequenceGate = "record-only"
            productionRequirement =
                "default scene FBX bound to runtime skinned animation path; sceneContentMotionSupported=1; qualityGate=1/1/0; qualityMasks=255/255/0"
            currentExpectedState =
                "quality inputs should be complete; blocked from final production only by preset-M validation and dynamic IQ acceptance"
        }
        $quickSummary["lanes"]["defaultSceneSkinnedFbxMProductionAudit"] =
            Invoke-QuickDlssProductionAuditSequenceSuite `
                -Name "default_scene_skinned_fbx_m_production_audit_present" `
                -LaneKey "defaultSceneSkinnedFbxMProduction.audit" `
                -Environment $defaultSceneSkinnedFbxMProductionPresentEnvironment `
                -Manifest $defaultSceneSkinnedFbxMProductionBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -Model $importedSkinnedPreviewModelPath
        $defaultSceneSkinnedFbxMProductionMetrics =
            $quickSummary["lanes"]["defaultSceneSkinnedFbxMProductionAudit"]["metrics"]
        $quickSummary["diagnostics"]["defaultSceneSkinnedFbxMProduction"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $false
            productionCandidate = $true
            productionReady = $false
            sceneContentMotionSupported =
                "$($defaultSceneSkinnedFbxMProductionMetrics.sceneContentMotionSupported)"
            objectMotionReady =
                "$($defaultSceneSkinnedFbxMProductionMetrics.objectMotionReady)"
            runtimeImportSkinnedAnimationUnsupported =
                "$($defaultSceneSkinnedFbxMProductionMetrics.runtimeImportSkinnedAnimationUnsupported)"
            runtimeImportSkinnedAnimationSupportReady =
                "$($defaultSceneSkinnedFbxMProductionMetrics.runtimeImportSkinnedAnimationSupportReady)"
            blockedReasons = @(
                "Preset M capture still needs clean NGX/Vulkan validation",
                "Default-scene dynamic M image quality still needs acceptance against native/K",
                "This audit lane is not a final production default until strict M validation lanes are clean"
            )
            requiredFutureState = [ordered]@{
                validationClean = $true
                sceneContentMotionSupported = "1"
                objectMotionReady = "1"
                runtimeImportSkinnedAnimationUnsupported = "0"
                runtimeImportSkinnedAnimationRenderableBound = "1"
                runtimeImportSkinnedAnimationSupportReady = "1"
                runtimeImportSkinnedAnimationSupportBlockerMask = "0"
                runtimeImportAnimationPlaybackReady = "1"
                runtimeImportAnimationPlaybackBlockerMask = "0"
                qualityGate = "1/1/0"
                qualityMasks = "255/255/0"
            }
        }
    }

    if ($selectedSuites -contains "skinned-fbx-m-production") {
        $quickSummary["baselines"]["skinnedFbxMProduction"] = [ordered]@{
            manifest = $importedSkinnedPreviewBaselineManifestPath
            name = $skinnedFbxMProductionBaselineManifest.name
            model = $importedSkinnedPreviewModelPath
            referenceBaselineInput = $importedSkinnedPreviewBaselineManifest.name
        }
        $quickSummary["thresholds"]["skinnedFbxMProduction"] = [ordered]@{
            centralDifferentPixelsMin =
                $skinnedFbxMProductionBaselineManifest.thresholds.centralDifferentPixelsMin
            sequenceGate = "record-only"
            productionRequirement =
                "qualityGate=1/1/0, qualityMasks=255/255/0, runtimeImportSkinnedAnimationUnsupported=0, runtimeImportSkinnedAnimationSupportReady=1, runtimeImportSkinnedAnimationSpaceReady=1, runtimeImportAnimationPlaybackReady=1, validationClean=true"
            currentExpectedState =
                "quality inputs should be complete; blocked until clean M validation and dynamic IQ acceptance"
        }
        $quickSummary["lanes"]["skinnedFbxMProductionAudit"] =
            Invoke-QuickDlssProductionAuditSequenceSuite `
                -Name "skinned_fbx_m_production_audit_present" `
                -LaneKey "skinnedFbxMProduction.audit" `
                -Environment $skinnedFbxMProductionPresentEnvironment `
                -Manifest $skinnedFbxMProductionBaselineManifest `
                -AllowedCaptureDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns `
                -Model $importedSkinnedPreviewModelPath
        $quickSummary["diagnostics"]["skinnedFbxMProduction"] = [ordered]@{
            validationClean = $false
            diagnosticOnly = $false
            productionCandidate = $true
            productionReady = $false
            blockedReasons = @(
                "Preset M capture still needs clean NGX/Vulkan validation",
                "Fist Fight B.fbx runtime animation support and reference-baseline readiness are expected ready in this audit lane",
                "Dynamic/subjective M image quality still needs acceptance against K/native before production default"
            )
            requiredFutureState = [ordered]@{
                validationClean = $true
                runtimeImportSkinnedAnimationUnsupported = "0"
                runtimeImportSkinnedAnimationRenderableBound = "1"
                runtimeImportSkinnedAnimationSupportReady = "1"
                runtimeImportSkinnedAnimationSupportBlockerMask = "0"
                runtimeImportSkinnedAnimationSpaceReady = "1"
                runtimeImportSkinnedAnimationSpaceBlockerMask = "0"
                runtimeImportAnimationDiagnosticPoseOnly = "0"
                runtimeImportAnimationPlaybackReady = "1"
                runtimeImportAnimationPlaybackBlockerMask = "0"
                qualityGate = "1/1/0"
                qualityMasks = "255/255/0"
            }
        }
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
        if ($lane.Value.Contains("edgeComparison")) {
            $edgeComparison = $lane.Value["edgeComparison"]
            Write-Host "  $($lane.Key) edges: pixels=$($edgeComparison.EdgePixels) changed=$($edgeComparison.ChangedEdgePixels) ratio=$($edgeComparison.ChangedEdgeRatio) mean=$($edgeComparison.MeanEdgeDelta) max=$($edgeComparison.MaxEdgeDelta)"
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
if ($defaultSceneDlaaMotionRow.temporal_upscaler_dlss_reset -ne $defaultSceneDlaaMotionBaselineManifest.expected.dlssPresent.dlssReset) {
    throw "Default-scene moving DLAA did not preserve the expected DLSS reset state"
}
Assert-DlssMotionVectorScalePixelSpace `
    -Name "Default-scene moving DLAA-present" `
    -Row $defaultSceneDlaaMotionRow
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
if ($defaultSceneDlaaObjectMotionRow.temporal_upscaler_dlss_reset -ne $defaultSceneDlaaObjectMotionBaselineManifest.expected.dlssPresent.dlssReset) {
    throw "Default-scene object-motion DLAA did not preserve the expected DLSS reset state"
}
Assert-DlssMotionVectorScalePixelSpace `
    -Name "Default-scene object-motion DLAA-present" `
    -Row $defaultSceneDlaaObjectMotionRow
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
if ($importedDynamicDlaaObjectMotionRow.temporal_upscaler_dlss_reset -ne $importedDynamicDlaaObjectMotionBaselineManifest.expected.dlssPresent.dlssReset) {
    throw "Imported-dynamic object-motion DLAA did not preserve the expected DLSS reset state"
}
Assert-DlssMotionVectorScalePixelSpace `
    -Name "Imported-dynamic object-motion DLAA-present" `
    -Row $importedDynamicDlaaObjectMotionRow
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
$dlaaImage = Capture-WindowImage `
    -Name "dlaa_present" `
    -Environment $dlaaPresentEnvironment `
    -AllowedDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
$defaultSceneDlaaNativeImage = Capture-WindowImage `
    -Name "default_scene_dlaa_native_deferred_hdr" `
    -Environment $defaultSceneDlaaNativeEnvironment
$defaultSceneDlaaImage = Capture-WindowImage `
    -Name "default_scene_dlaa_present" `
    -Environment $defaultSceneDlaaPresentEnvironment `
    -AllowedDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
$defaultSceneDlaaMotionImages = Capture-WindowImageSequence `
    -Name "default_scene_dlaa_motion_present" `
    -Environment $defaultSceneDlaaMotionPresentEnvironment `
    -FrameCount $SequenceFrameCount `
    -InitialDelaySeconds $SequenceInitialDelaySeconds `
    -IntervalSeconds $SequenceIntervalSeconds `
    -AllowedDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
$defaultSceneDlaaObjectMotionImages = Capture-WindowImageSequence `
    -Name "default_scene_dlaa_object_motion_present" `
    -Environment $defaultSceneDlaaObjectMotionPresentEnvironment `
    -FrameCount $SequenceFrameCount `
    -InitialDelaySeconds $SequenceInitialDelaySeconds `
    -IntervalSeconds $SequenceIntervalSeconds `
    -AllowedDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
$importedDynamicDlaaObjectMotionImages = Capture-WindowImageSequence `
    -Name "imported_dynamic_dlaa_object_motion_present" `
    -Environment $importedDynamicDlaaObjectMotionPresentEnvironment `
    -FrameCount $SequenceFrameCount `
    -InitialDelaySeconds $SequenceInitialDelaySeconds `
    -IntervalSeconds $SequenceIntervalSeconds `
    -AllowedDiagnosticPatterns $dlssPresetMAllowedDiagnosticPatterns
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
$defaultSceneDlaaEdgeComparison =
    Compare-ImageEdges -A $defaultSceneDlaaNativeImage -B $defaultSceneDlaaImage
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
Assert-QuickEdgeComparison `
    -Name "defaultSceneDlaa" `
    -EdgeComparison $defaultSceneDlaaEdgeComparison `
    -Manifest $defaultSceneDlaaBaselineManifest
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
        defaultSceneDlaaComparisonEdgePixelsMin = [int]$defaultSceneDlaaBaselineManifest.thresholds.comparisonEdgePixelsMin
        defaultSceneDlaaComparisonChangedEdgePixelsMax = [int]$defaultSceneDlaaBaselineManifest.thresholds.comparisonChangedEdgePixelsMax
        defaultSceneDlaaComparisonChangedEdgeRatioMax = [double]$defaultSceneDlaaBaselineManifest.thresholds.comparisonChangedEdgeRatioMax
        defaultSceneDlaaComparisonMeanEdgeDeltaMax = [double]$defaultSceneDlaaBaselineManifest.thresholds.comparisonMeanEdgeDeltaMax
        defaultSceneDlaaComparisonMaxEdgeDeltaMax = [double]$defaultSceneDlaaBaselineManifest.thresholds.comparisonMaxEdgeDeltaMax
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
    defaultSceneDlaaEdgeComparison = $defaultSceneDlaaEdgeComparison
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
Write-Host "  appedges: pixels=$($defaultSceneDlaaEdgeComparison.EdgePixels) changed=$($defaultSceneDlaaEdgeComparison.ChangedEdgePixels) ratio=$($defaultSceneDlaaEdgeComparison.ChangedEdgeRatio) mean=$($defaultSceneDlaaEdgeComparison.MeanEdgeDelta) max=$($defaultSceneDlaaEdgeComparison.MaxEdgeDelta)"
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
