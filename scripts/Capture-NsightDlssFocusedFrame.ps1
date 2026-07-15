param(
    [string]$NsightHostPath = "C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.2.0\host\windows-desktop-nomad-x64",
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "out\nsight_captures",
    [string]$CaptureName = "",
    [int]$CaptureFrame = 90,
    [int]$AutoExitFrames = 150,
    [int]$Width = 2560,
    [int]$Height = 1440,
    [string]$DlssPreset = "m",
    [string]$DlssQuality = "dlaa",
    [string]$BenchmarkCsvPath = "",
    [int]$BenchmarkWarmupFrames = 3,
    [int]$BenchmarkFrames = 3,
    [string[]]$AdditionalEnv = @(),
    [switch]$SkipBuild,
    [switch]$SkipSign,
    [switch]$SkipMetadataGate,
    [switch]$SkipInspectionPackage,
    [switch]$SkipBenchmarkProbe,
    [switch]$KeepApplicationRunningAfterCapture,
    [switch]$UseKnownNgxInternalLayoutIsolation,
    [switch]$CaptureScreenshot,
    [switch]$OpenUi,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Resolve-NsightTool {
    param(
        [Parameter(Mandatory = $true)][string]$ToolName
    )

    $candidate = Join-Path $NsightHostPath $ToolName
    if (Test-Path -LiteralPath $candidate) {
        return (Resolve-Path $candidate).Path
    }

    $command = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "$ToolName was not found. Pass -NsightHostPath or add it to PATH."
}

function Add-EnvironmentArgument {
    param(
        [Parameter(Mandatory = $true)][System.Collections.ArrayList]$Arguments,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Value
    )

    [void]$Arguments.Add("--env")
    [void]$Arguments.Add("$Name=$Value")
}

function Add-EnvironmentOverride {
    param(
        [Parameter(Mandatory = $true)][System.Collections.Specialized.OrderedDictionary]$Environment,
        [Parameter(Mandatory = $true)][string]$Entry
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

function Copy-OrderedEnvironment {
    param(
        [Parameter(Mandatory = $true)][System.Collections.Specialized.OrderedDictionary]$Environment
    )

    $copy = [ordered]@{}
    foreach ($key in $Environment.Keys) {
        $copy[$key] = $Environment[$key]
    }
    return $copy
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

$captureExe = Resolve-NsightTool "ngfx-capture.exe"
$replayExe = Resolve-NsightTool "ngfx-replay.exe"
$uiExe = Resolve-NsightTool "ngfx-ui.exe"

if ($CaptureFrame -le 1) {
    throw "-CaptureFrame must be greater than 1 for ngfx-capture."
}

if (-not $SkipBuild) {
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

$resolvedExe = Resolve-Path $ExecutablePath
if ($KeepApplicationRunningAfterCapture -and $AutoExitFrames -le $CaptureFrame) {
    throw "-AutoExitFrames must be greater than -CaptureFrame when using -KeepApplicationRunningAfterCapture."
}
if (-not $SkipSign) {
    $signScript = Join-Path $PSScriptRoot "Sign-SelfEngineDevBinary.ps1"
    if (-not (Test-Path -LiteralPath $signScript)) {
        throw "Signing script not found: $signScript"
    }
    & $signScript -TargetPath $resolvedExe.Path | Out-Host
}

$outputRoot = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

if ($CaptureName.Trim().Length -eq 0) {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $CaptureName = "selfengine_${DlssPreset}_${DlssQuality}_mvjittered_jitterhistory_frame${CaptureFrame}_$timestamp"
}

$capturePath = Join-Path $outputRoot "$CaptureName.ngfx-capture"
$benchmarkCsvIsExternal = $BenchmarkCsvPath.Trim().Length -gt 0
$resolvedBenchmarkCsvPath = if ($BenchmarkCsvPath.Trim().Length -gt 0) {
    if ([System.IO.Path]::IsPathRooted($BenchmarkCsvPath)) {
        [System.IO.Path]::GetFullPath($BenchmarkCsvPath)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BenchmarkCsvPath))
    }
} else {
    Join-Path $outputRoot "$CaptureName.csv"
}
$benchmarkLogPath = Join-Path $outputRoot "$CaptureName.benchmark.log"
$benchmarkErrLogPath = Join-Path $outputRoot "$CaptureName.benchmark.err.log"
$captureLogPath = Join-Path $outputRoot "$CaptureName.ngfx-capture.log"
$resultPath = Join-Path $outputRoot "$CaptureName.result.json"
$inspectionPath = Join-Path $outputRoot "$CaptureName.inspection.json"
$screenshotPath = if ($CaptureScreenshot) {
    Join-Path $outputRoot "$CaptureName.png"
} else {
    ""
}

if (-not $Force) {
    $collisionPaths = @($capturePath, $benchmarkLogPath, $benchmarkErrLogPath, $captureLogPath, $resultPath, $inspectionPath, $screenshotPath)
    if (-not $benchmarkCsvIsExternal) {
        $collisionPaths += $resolvedBenchmarkCsvPath
    }
    foreach ($path in $collisionPaths) {
        if ($path.Length -gt 0 -and (Test-Path -LiteralPath $path)) {
            throw "Output already exists: $path. Pass -Force or choose another -CaptureName."
        }
    }
} else {
    $removePaths = @($capturePath, $benchmarkLogPath, $benchmarkErrLogPath, $captureLogPath, $resultPath, $inspectionPath, $screenshotPath)
    if (-not $benchmarkCsvIsExternal) {
        $removePaths += $resolvedBenchmarkCsvPath
    }
    foreach ($path in $removePaths) {
        if ($path.Length -gt 0) {
            Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
        }
    }
}

$commonEnvironment = [ordered]@{
    "SE_UPSCALER_PLUGIN" = "dlss"
    "SE_DLSS_QUALITY" = $DlssQuality
    "SE_DLSS_PRESET" = $DlssPreset
    "SE_DLSS_PRESENT" = "1"
    "SE_DLSS_REFERENCE_BASELINE_PATH" = "docs\reference_baselines\dlss_default_scene_skinned_fbx_m_production_visual_qa_baseline.json"
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION" = "1"
    "SE_RENDER_SCALE" = "1.0"
    "SE_RENDER_SCALE_APPLY" = "1"
    "SE_TAA" = "1"
    "SE_BENCHMARK_OBJECT_MOTION" = "orbit"
    "SE_BENCHMARK_OBJECT_MOTION_SPEED" = "1.1"
    "SE_BENCHMARK_OBJECT_MOTION_RADIUS" = "0.32"
    "SE_TEMPORAL_JITTER" = "1"
    "SE_TAA_APPLY_JITTER" = "1"
    "SE_DLSS_CREATE_FLAG_MV_JITTERED" = "1"
    "SE_TEMPORAL_VELOCITY_JITTER_POLICY" = "jittered"
    "SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS" = "1"
    "SE_RENDER_VIEW" = "deferred-hdr"
    "SE_WINDOW_WIDTH" = [string]$Width
    "SE_WINDOW_HEIGHT" = [string]$Height
    "SE_WINDOW_BORDERLESS" = "1"
    "SE_VISUAL_QA_HIDE_IMGUI" = "1"
    "SE_AUTO_EXIT_FRAMES" = [string]$AutoExitFrames
}
if ($UseKnownNgxInternalLayoutIsolation) {
    $commonEnvironment["SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT"] = "1"
}
foreach ($entry in $AdditionalEnv) {
    Add-EnvironmentOverride -Environment $commonEnvironment -Entry $entry
}

$managedEnvironmentKeys = @(
    @($commonEnvironment.Keys) +
    @(
        "SE_BENCHMARK_SCENE",
        "SE_BENCHMARK_WARMUP_FRAMES",
        "SE_BENCHMARK_FRAMES",
        "SE_BENCHMARK_CSV",
        "SELFENGINE_MODEL_PATH",
        "SE_RENDER_VIEW"
    )
) | Select-Object -Unique

$benchmarkEnvironment = Copy-OrderedEnvironment $commonEnvironment
$benchmarkEnvironment["SE_BENCHMARK_WARMUP_FRAMES"] = [string]$BenchmarkWarmupFrames
$benchmarkEnvironment["SE_BENCHMARK_FRAMES"] = [string]$BenchmarkFrames
$benchmarkEnvironment["SE_BENCHMARK_CSV"] = $resolvedBenchmarkCsvPath

if (-not $SkipBenchmarkProbe -and $BenchmarkCsvPath.Trim().Length -eq 0) {
    Write-Host "Benchmark probe: $resolvedBenchmarkCsvPath"
    Invoke-WithEnvironment `
        -ManagedKeys $managedEnvironmentKeys `
        -Environment $benchmarkEnvironment `
        -Script {
            $benchmarkProcess = Start-Process `
                -FilePath $resolvedExe.Path `
                -WorkingDirectory $repoRoot `
                -RedirectStandardOutput $benchmarkLogPath `
                -RedirectStandardError $benchmarkErrLogPath `
                -PassThru `
                -Wait
            $benchmarkExitCode = if ($null -ne $benchmarkProcess.ExitCode) {
                [int]$benchmarkProcess.ExitCode
            } else {
                0
            }
            foreach ($path in @($benchmarkLogPath, $benchmarkErrLogPath)) {
                if (-not (Test-Path -LiteralPath $path)) {
                    New-Item -ItemType File -Force -Path $path | Out-Null
                }
            }
            $benchmarkDiagnostics = @(
                Select-String `
                    -LiteralPath @($benchmarkLogPath, $benchmarkErrLogPath) `
                    -Pattern "VUID|validation|error|failed|exception|shader" `
                    -CaseSensitive:$false `
                    -ErrorAction SilentlyContinue |
                    Where-Object { $_.Line -notmatch "Detected enabled Vulkan layer" }
            )
            if ($benchmarkDiagnostics.Count -gt 0) {
                throw "Benchmark probe logs contain diagnostics. See $benchmarkLogPath and $benchmarkErrLogPath"
            }
            if ($benchmarkExitCode -ne 0 -and
                -not (Test-Path -LiteralPath $resolvedBenchmarkCsvPath)) {
                throw "Benchmark probe failed with exit code $benchmarkExitCode. See $benchmarkLogPath"
            }
            if ($benchmarkExitCode -ne 0) {
                Write-Warning "Benchmark probe returned exit code $benchmarkExitCode, but CSV was written."
            }
        }
    Write-Host "Benchmark probe complete"
}
if (-not $SkipMetadataGate -and -not (Test-Path -LiteralPath $resolvedBenchmarkCsvPath)) {
    throw "Benchmark CSV is required for metadata gate: $resolvedBenchmarkCsvPath"
}

Write-Host "Nsight capture: $capturePath"
$captureArguments = [System.Collections.ArrayList]::new()
foreach ($argument in @(
    "--exe", $resolvedExe.Path,
    "--working-dir", $repoRoot
)) {
    [void]$captureArguments.Add($argument)
}
foreach ($key in $commonEnvironment.Keys) {
    Add-EnvironmentArgument `
        -Arguments $captureArguments `
        -Name ([string]$key) `
        -Value ([string]$commonEnvironment[$key])
}
foreach ($argument in @(
    "--capture-frame", [string]$CaptureFrame,
    "--frame-count", "1",
    "--no-bundle-replayer",
    "--no-block-on-interfering-application",
    "--output-file", (Split-Path -Leaf $capturePath),
    "--output-dir", $outputRoot
)) {
    [void]$captureArguments.Add($argument)
}
if (-not $KeepApplicationRunningAfterCapture) {
    [void]$captureArguments.Add("--terminate-after-capture")
}

Invoke-WithEnvironment `
    -ManagedKeys $managedEnvironmentKeys `
    -Environment $commonEnvironment `
    -Script {
        $captureOutput = & $captureExe @captureArguments 2>&1
        $script:captureExitCode = if ($null -ne (Get-Variable -Name LASTEXITCODE -ErrorAction SilentlyContinue)) {
            [int]$LASTEXITCODE
        } else {
            0
        }
        @($captureOutput | ForEach-Object { $_.ToString() }) |
            Set-Content -LiteralPath $captureLogPath -Encoding UTF8
}
if ($captureExitCode -ne 0) {
    throw "ngfx-capture failed with exit code $captureExitCode. See $captureLogPath"
}
if (-not (Test-Path -LiteralPath $capturePath)) {
    throw "Expected Nsight capture was not created: $capturePath"
}
Write-Host "Nsight capture complete"

$metadataGate = $null
if (-not $SkipMetadataGate) {
    if (-not (Test-Path -LiteralPath $resolvedBenchmarkCsvPath)) {
        throw "Benchmark CSV was not created: $resolvedBenchmarkCsvPath"
    }

    $metadataScript = Join-Path $PSScriptRoot "Test-NsightDlssCaptureMetadata.ps1"
    Write-Host "Metadata gate: $capturePath"
    if ($CaptureScreenshot) {
        $metadataJson = & $metadataScript `
            -CapturePath $capturePath `
            -NsightHostPath $NsightHostPath `
            -ExpectedResolution "${Width}x${Height}" `
            -ExpectedDlssQuality $DlssQuality `
            -ExpectedDlssPreset $DlssPreset `
            -BenchmarkCsvPath $resolvedBenchmarkCsvPath `
            -ScreenshotPath $screenshotPath
    } else {
        $metadataJson = & $metadataScript `
            -CapturePath $capturePath `
            -NsightHostPath $NsightHostPath `
            -ExpectedResolution "${Width}x${Height}" `
            -ExpectedDlssQuality $DlssQuality `
            -ExpectedDlssPreset $DlssPreset `
            -BenchmarkCsvPath $resolvedBenchmarkCsvPath
    }
    $metadataGate = ($metadataJson -join "`n") | ConvertFrom-Json
    Write-Host "Metadata gate complete"
}

$inspectionPackage = $null
if (-not $SkipInspectionPackage) {
    $inspectionScript = Join-Path $PSScriptRoot "Export-NsightDlssInspectionPackage.ps1"
    if (-not (Test-Path -LiteralPath $inspectionScript)) {
        throw "Inspection package script not found: $inspectionScript"
    }

    Write-Host "Inspection package: $inspectionPath"
    $inspectionJson = & $inspectionScript `
        -CapturePath $capturePath `
        -NsightHostPath $NsightHostPath `
        -BenchmarkCsvPath $resolvedBenchmarkCsvPath `
        -BenchmarkLogPath $benchmarkLogPath `
        -OutputPath $inspectionPath `
        -ExpectedResolution "${Width}x${Height}"
    $inspectionPackage = ($inspectionJson -join "`n") | ConvertFrom-Json
    Write-Host "Inspection package complete"
}

$validationMatches = @()
if (Test-Path -LiteralPath $captureLogPath) {
    $validationMatches = @(
        Select-String `
            -LiteralPath $captureLogPath `
            -Pattern "VUID|validation|error|failed|exception|shader" `
            -CaseSensitive:$false `
            -ErrorAction SilentlyContinue |
            Where-Object { $_.Line -notmatch "Detected enabled Vulkan layer" } |
            ForEach-Object { $_.Line.Trim() }
    )
}
$captureLogValidationClean = $validationMatches.Count -eq 0
$productionReadyBlockedByKnownNgxIsolation =
    [bool]$UseKnownNgxInternalLayoutIsolation
if ($null -ne $inspectionPackage) {
    $productionReadyBlockedByKnownNgxIsolation =
        $productionReadyBlockedByKnownNgxIsolation -or
        [bool]$inspectionPackage.benchmarkLog.productionCleanBlockedByKnownNgxIsolation
}
$productionStrictValidationClean =
    $captureLogValidationClean -and
    -not $productionReadyBlockedByKnownNgxIsolation

$result = [ordered]@{
    capture = $capturePath
    benchmarkCsv = $resolvedBenchmarkCsvPath
    benchmarkLog = $benchmarkLogPath
    benchmarkErrLog = $benchmarkErrLogPath
    captureLog = $captureLogPath
    inspection = $inspectionPath
    result = $resultPath
    screenshot = $screenshotPath
    nsightHostPath = $NsightHostPath
    ngfxCapture = $captureExe
    ngfxReplay = $replayExe
    ngfxUi = $uiExe
    exe = $resolvedExe.Path
    workingDirectory = $repoRoot
    captureFrame = $CaptureFrame
    resolution = "${Width}x${Height}"
    defaultMonitorIndex = 1
    defaultMonitorDevice = "\\.\DISPLAY2"
    knownNgxInternalLayoutIsolation = [bool]$UseKnownNgxInternalLayoutIsolation
    terminateAfterCapture = -not [bool]$KeepApplicationRunningAfterCapture
    benchmarkProbeSkipped = [bool]$SkipBenchmarkProbe
    inspectionPackageSkipped = [bool]$SkipInspectionPackage
    captureLogValidationClean = $captureLogValidationClean
    validationClean = $captureLogValidationClean
    productionReadyBlockedByKnownNgxIsolation =
        $productionReadyBlockedByKnownNgxIsolation
    productionStrictValidationClean =
        $productionStrictValidationClean
    productionStrictValidationReason =
        if ($productionStrictValidationClean) {
            "strict-clean"
        } elseif ($productionReadyBlockedByKnownNgxIsolation) {
            "known-ngx-internal-layout-isolation-active"
        } else {
            "capture-log-diagnostics"
        }
    validationMatchCount = $validationMatches.Count
    validationMatches = $validationMatches
    captureEnvironment = $commonEnvironment
    benchmarkEnvironment = $benchmarkEnvironment
    metadataGate = $metadataGate
    inspectionPackage = if ($null -ne $inspectionPackage) {
        [ordered]@{
            readyForManualNsightInspection =
                [bool]$inspectionPackage.readyForManualNsightInspection
            evaluateEventCount =
                [int]$inspectionPackage.functionStream.evaluateEventCount
            evaluateBracketedByDebugLabel =
                [bool]$inspectionPackage.functionStream.evaluateBracketedByDebugLabel
            preEvaluateBarrierCount =
                [int]$inspectionPackage.functionStream.preEvaluateBarrierCount
            postEvaluateBarrierCount =
                [int]$inspectionPackage.functionStream.postEvaluateBarrierCount
            requiredObjectNamesReady =
                [bool]$inspectionPackage.objects.requiredObjectNamesReady
            resourceTraceReady =
                [bool]$inspectionPackage.benchmarkLog.resourceTraceReady
            resourceTraceContractReady =
                [bool]$inspectionPackage.benchmarkLog.resourceTraceContractReady
            lifecycleTraceReady =
                [bool]$inspectionPackage.benchmarkLog.lifecycleTraceReady
            knownNgxIsolationAttributionReady =
                [bool]$inspectionPackage.benchmarkLog.knownNgxIsolationAttributionReady
            suppressedEvaluateResourceOverlapCount =
                [int]$inspectionPackage.benchmarkLog.suppressedEvaluateResourceOverlapCount
            productionCleanBlockedByKnownNgxIsolation =
                [bool]$inspectionPackage.benchmarkLog.productionCleanBlockedByKnownNgxIsolation
            productionStrictValidationClean =
                [bool]$inspectionPackage.benchmarkLog.productionStrictValidationClean
        }
    } else {
        $null
    }
}
$result | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8

if ($OpenUi) {
    Start-Process -FilePath $uiExe -ArgumentList @($capturePath)
}

$result | ConvertTo-Json -Depth 6
