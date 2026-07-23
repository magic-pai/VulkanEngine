[CmdletBinding()]
param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "tmp\hybrid_reflection_full_audit",
    [string]$BenchmarkScene = "lighting-showcase",
    [string]$ReceiverNamePattern = "Center Mirror Sphere",
    [string]$ExpectedHitNamePattern = "Polished Metal Sphere",
    [ValidateSet("default", "additive", "destination-alpha")]
    [string]$ApplyBlendMode = "default",
    [ValidateSet("taa", "dlaa", "sr-quality", "sr-balanced", "sr-performance")]
    [string]$AaMode = "taa",
    [ValidateRange(1, 120)]
    [uint32]$CaptureFrames = 1,
    [ValidateRange(0, 300)]
    [uint32]$AuditStartFrame = 12,
    [switch]$Forward3DFbx,
    [switch]$RawEvidence,
    [switch]$VerbosePixelCsv,
    [switch]$ExtendedReport,
    [switch]$CaptureRenderDocOnFailure,
    [switch]$DisableZeroConfidenceHistoryRejection,
    [switch]$DisableRadianceSanitization,
    [switch]$DisableBackFaceCull,
    [switch]$ForceAllRayQueries,
    [switch]$DisableRayQueryHitIbl,
    [switch]$DisableDirectMirrorRayQuery,
    [switch]$DisableSkinnedBlas,
    [switch]$FreezeFbxAnimation,
    [switch]$DisableHdrAlphaPreservation,
    [switch]$DisablePointSpotDirectSpecular,
    [switch]$BypassReproject,
    [switch]$DisableObjectStableProbeSelection,
    [switch]$EnableHardPixelProbeSwitch,
    [switch]$CoupleMirrorSourceSelectionToSsrStrength,
    [switch]$SkipBuild,
    [switch]$SkipSigning,
    [switch]$SkipAnalysis,
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (![IO.Path]::IsPathRooted($ExecutablePath)) {
    $ExecutablePath = Join-Path $projectRoot $ExecutablePath
}
if (![IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

if (!$SkipBuild) {
    $vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    $buildDirectory = Join-Path $projectRoot "build"
    $targetName = [IO.Path]::GetFileNameWithoutExtension($ExecutablePath)
    if ($targetName -notin @(
        "SelfEngineForward3D",
        "SelfEngineLightingShowcase"
    )) {
        throw "Unsupported full-audit build target: $targetName"
    }
    $projectFile = "$targetName.vcxproj"
    & cmd.exe /d /c "call `"$vcvars`" >nul 2>&1 && cd /d `"$buildDirectory`" && MSBuild $projectFile /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo"
    if ($LASTEXITCODE -ne 0) {
        throw "$targetName Debug build failed with exit code $LASTEXITCODE"
    }
}
$ExecutablePath = (Resolve-Path $ExecutablePath).Path
if (!$SkipSigning) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File `
        (Join-Path $projectRoot "scripts\Sign-SelfEngineDevBinary.ps1") `
        -TargetPath $ExecutablePath
    if ($LASTEXITCODE -ne 0) {
        throw "Full-audit executable signing failed with exit code $LASTEXITCODE"
    }
    Start-Sleep -Milliseconds 1000
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
foreach ($name in @(
    "frames.csv",
    "objects.csv",
    "instances.csv",
    "instance_counters.csv",
    "rays.csv",
    "apply.csv",
    "gbuffer_samples.csv",
    "reflection_composition.csv",
    "reflection_composition_summary.csv",
    "audit_index.csv",
    "runtime_object_summary.csv",
    "runtime_receiver_hit_matrix.csv",
    "runtime_receiver_quality.csv",
    "runtime_apply_discontinuities.csv",
    "runtime_dnsr_confidence_quality.csv",
    "runtime_dnsr_confidence_transitions.csv",
    "benchmark_frame_matches.csv",
    "image_stage_contract.csv",
    "benchmark_long.csv",
    "dnsr_confidence_quality.csv",
    "dnsr_confidence_transitions.csv",
    "image_snapshot_manifest.csv",
    "lights.csv",
    "probes.csv",
    "queue_commands.csv",
    "object_summary.csv",
    "receiver_hit_matrix.csv",
    "selected_receiver_summary.csv",
    "analysis.json",
    "launch_contract.json",
    "launch_failure.json",
    "observability_escalation.json",
    "benchmark.csv",
    "stdout.log",
    "stderr.log"
)) {
    Remove-Item -LiteralPath (Join-Path $OutputDirectory $name) `
        -Force -ErrorAction SilentlyContinue
}
Get-ChildItem -LiteralPath $OutputDirectory -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "image_capture_*.bin" -or
        $_.Name -like "object_ids_capture_*.bin" } |
    ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Force
    }

$managedKeys = @(
    "SE_HYBRID_REFLECTIONS_RT",
    "SE_HYBRID_REFLECTIONS_DIAGNOSTICS",
    "SE_HYBRID_REFLECTIONS_FULL_AUDIT",
    "SE_HYBRID_REFLECTIONS_FULL_AUDIT_DIR",
    "SE_HYBRID_REFLECTIONS_FULL_AUDIT_CAPTURE_FRAMES",
    "SE_HYBRID_REFLECTIONS_FULL_AUDIT_START_FRAME",
    "SE_HYBRID_REFLECTIONS_FORCE_ALL_RAY_QUERIES",
    "SE_HYBRID_REFLECTIONS_HIT_IBL_OFF",
    "SE_HYBRID_REFLECTIONS_DIRECT_MIRROR_OFF",
    "SE_HYBRID_REFLECTIONS_SKINNED_BLAS_OFF",
    "SE_HYBRID_REFLECTIONS_CULL_BACK_FACES_OFF",
    "SE_HYBRID_REFLECTIONS_APPLY_BLEND_MODE",
    "SE_HYBRID_REFLECTIONS_HDR_ALPHA_PRESERVATION_OFF",
    "SE_HYBRID_REFLECTIONS_FULL_AUDIT_VERBOSE_PIXEL_CSV",
    "SE_HYBRID_REFLECTIONS_FULL_AUDIT_RAW_EVIDENCE",
    "SE_SSR",
    "SE_SSR_BACKEND",
    "SE_FORWARD3D_AA_MODE",
    "SE_BENCHMARK_SCENE",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_LIGHTING_SHOWCASE_FORCE_OFF",
    "SE_FORWARD3D_DEBUG_DEFAULT_SCENE",
    "SE_SCENE_UPDATE_FREEZE",
    "SE_FBX_ANIMATION_FREEZE",
    "SE_VISUAL_QA_HIDE_IMGUI",
    "SE_WINDOW_HIDDEN",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_AUTO_EXIT_FRAMES",
    "SE_BENCHMARK_CSV",
    "SE_SSR_FFX_ZERO_CONFIDENCE_HISTORY_REJECTION_OFF",
    "SE_SSR_FFX_RADIANCE_SANITIZE_OFF",
    "SE_SSR_FFX_REPROJECT_BYPASS",
    "SE_POINT_SPOT_DIRECT_SPECULAR_OFF",
    "SE_REFLECTION_PROBE_OBJECT_STABLE_OFF",
    "SE_REFLECTION_PROBE_DOMINANT_MIRROR_HARD_SWITCH",
    "SE_SSR_FFX_MIRROR_SOURCE_SELECTION_STRENGTH_COUPLED"
)
$previous = @{}
foreach ($key in $managedKeys) {
    $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
    [Environment]::SetEnvironmentVariable($key, $null, "Process")
}
$effectiveBenchmarkScene = $BenchmarkScene
$productionFbxValue = ""
$forceShowcaseOffValue = ""
if ($Forward3DFbx) {
    $effectiveBenchmarkScene = ""
    $productionFbxValue = "1"
    $forceShowcaseOffValue = "1"
}
$environment = @{
    SE_HYBRID_REFLECTIONS_RT = "1"
    SE_HYBRID_REFLECTIONS_DIAGNOSTICS = "1"
    SE_HYBRID_REFLECTIONS_FULL_AUDIT = "1"
    SE_HYBRID_REFLECTIONS_FULL_AUDIT_DIR = $OutputDirectory
    SE_HYBRID_REFLECTIONS_FULL_AUDIT_CAPTURE_FRAMES = [string]$CaptureFrames
    SE_HYBRID_REFLECTIONS_FULL_AUDIT_START_FRAME = [string]$AuditStartFrame
    SE_HYBRID_REFLECTIONS_FULL_AUDIT_VERBOSE_PIXEL_CSV = if ($VerbosePixelCsv) { "1" } else { "0" }
    SE_HYBRID_REFLECTIONS_FULL_AUDIT_RAW_EVIDENCE = if ($RawEvidence -or $VerbosePixelCsv) { "1" } else { "0" }
    SE_HYBRID_REFLECTIONS_FORCE_ALL_RAY_QUERIES = if ($ForceAllRayQueries) { "1" } else { "" }
    SE_HYBRID_REFLECTIONS_HIT_IBL_OFF = if ($DisableRayQueryHitIbl) { "1" } else { "" }
    SE_HYBRID_REFLECTIONS_DIRECT_MIRROR_OFF = if ($DisableDirectMirrorRayQuery) { "1" } else { "" }
    SE_HYBRID_REFLECTIONS_SKINNED_BLAS_OFF = if ($DisableSkinnedBlas) { "1" } else { "" }
    SE_HYBRID_REFLECTIONS_CULL_BACK_FACES_OFF = if ($DisableBackFaceCull) { "1" } else { "" }
    SE_HYBRID_REFLECTIONS_APPLY_BLEND_MODE = if ($ApplyBlendMode -eq "default") { "" } else { $ApplyBlendMode }
    SE_HYBRID_REFLECTIONS_HDR_ALPHA_PRESERVATION_OFF = if ($DisableHdrAlphaPreservation) { "1" } else { "" }
    SE_SSR = "1"
    SE_SSR_BACKEND = "ffx-sssr"
    SE_SSR_FFX_ZERO_CONFIDENCE_HISTORY_REJECTION_OFF = if ($DisableZeroConfidenceHistoryRejection) { "1" } else { "" }
    SE_SSR_FFX_RADIANCE_SANITIZE_OFF = if ($DisableRadianceSanitization) { "1" } else { "" }
    SE_SSR_FFX_REPROJECT_BYPASS = if ($BypassReproject) { "1" } else { "" }
    SE_POINT_SPOT_DIRECT_SPECULAR_OFF = if ($DisablePointSpotDirectSpecular) { "1" } else { "" }
    SE_REFLECTION_PROBE_OBJECT_STABLE_OFF = if ($DisableObjectStableProbeSelection) { "1" } else { "" }
    SE_REFLECTION_PROBE_DOMINANT_MIRROR_HARD_SWITCH = if ($EnableHardPixelProbeSwitch) { "1" } else { "" }
    SE_SSR_FFX_MIRROR_SOURCE_SELECTION_STRENGTH_COUPLED = if ($CoupleMirrorSourceSelectionToSsrStrength) { "1" } else { "" }
    SE_FORWARD3D_AA_MODE = $AaMode
    SE_BENCHMARK_SCENE = $effectiveBenchmarkScene
    SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = $productionFbxValue
    SE_LIGHTING_SHOWCASE_FORCE_OFF = $forceShowcaseOffValue
    SE_FORWARD3D_DEBUG_DEFAULT_SCENE = ""
    SE_SCENE_UPDATE_FREEZE = "1"
    SE_FBX_ANIMATION_FREEZE = if ($FreezeFbxAnimation) { "1" } else { "" }
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_WINDOW_HIDDEN = "1"
    SE_BENCHMARK_WARMUP_FRAMES = [string]$AuditStartFrame
    SE_BENCHMARK_FRAMES = "7"
    SE_AUTO_EXIT_FRAMES = [string]($AuditStartFrame + 16)
    SE_BENCHMARK_CSV = Join-Path $OutputDirectory "benchmark.csv"
}
foreach ($entry in $environment.GetEnumerator()) {
    [Environment]::SetEnvironmentVariable(
        $entry.Key,
        [string]$entry.Value,
        "Process"
    )
}

$stdoutPath = Join-Path $OutputDirectory "stdout.log"
$stderrPath = Join-Path $OutputDirectory "stderr.log"
$launchContractPath = Join-Path $OutputDirectory "launch_contract.json"
$launchFailurePath = Join-Path $OutputDirectory "launch_failure.json"
$signature = Get-AuthenticodeSignature -LiteralPath $ExecutablePath
$launchStarted = Get-Date
[ordered]@{
    executable = $ExecutablePath
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $ExecutablePath).Hash
    bytes = [uint64](Get-Item -LiteralPath $ExecutablePath).Length
    signatureStatus = $signature.Status.ToString()
    signatureMessage = $signature.StatusMessage
    signerSubject = if ($null -ne $signature.SignerCertificate) {
        $signature.SignerCertificate.Subject
    } else { "" }
    zeroConfidenceHistoryRejectionExpected =
        -not [bool]$DisableZeroConfidenceHistoryRejection
    forceAllRayQueriesExpected = [bool]$ForceAllRayQueries
    rayQueryHitIblEnabledExpected = -not [bool]$DisableRayQueryHitIbl
    directMirrorRayQueryEnabledExpected = -not [bool]$DisableDirectMirrorRayQuery
    pointSpotDirectSpecularDisabledExpected =
        [bool]$DisablePointSpotDirectSpecular
    reprojectBypassExpected = [bool]$BypassReproject
    objectStableProbeSelectionExpected =
        -not [bool]$DisableObjectStableProbeSelection
    hardPixelProbeSwitchExpected = [bool]$EnableHardPixelProbeSwitch
    mirrorSourceSelectionStrengthCoupledExpected =
        [bool]$CoupleMirrorSourceSelectionToSsrStrength
    startedAt = $launchStarted.ToString("o")
    outputDirectory = $OutputDirectory
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $launchContractPath
try {
    $executableDirectory = Split-Path -Parent $ExecutablePath
    $commandLine =
        "cd /d `"$executableDirectory`" && `"$ExecutablePath`"" +
        " 1> `"$stdoutPath`" 2> `"$stderrPath`""
    & cmd.exe /d /c $commandLine
    $exitCode = $LASTEXITCODE
} finally {
    foreach ($entry in $previous.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable(
            $entry.Key,
            $entry.Value,
            "Process"
        )
    }
}
if ($exitCode -ne 0) {
    $stderr = Get-Content -Raw -LiteralPath $stderrPath -ErrorAction SilentlyContinue
    if ($exitCode -eq 4551 -or $stderr -match "Device Guard" -or
        $stderr -match "was blocked") {
        $events = @(
            Get-WinEvent -LogName `
                "Microsoft-Windows-CodeIntegrity/Operational" `
                -MaxEvents 80 -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.TimeCreated -ge $launchStarted.AddSeconds(-2) -and
                    $_.Message -match [regex]::Escape(
                        [IO.Path]::GetFileName($ExecutablePath)
                    )
                } |
                Select-Object TimeCreated,Id,LevelDisplayName,Message
        )
        [ordered]@{
            executable = $ExecutablePath
            exitCode = $exitCode
            blocked = $true
            signatureStatus = $signature.Status.ToString()
            codeIntegrityEvents = $events
        } | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath $launchFailurePath
        throw "Full-audit executable was blocked by Device Guard (4551). No stale report was accepted."
    }
    throw "Full-audit executable failed with exit code $exitCode. See $stderrPath"
}

if (!$SkipAnalysis) {
    $analyzer = Join-Path $projectRoot `
        "scripts\Analyze-HybridReflectionFullAudit.ps1"
    $arguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $analyzer,
        "-AuditDirectory", $OutputDirectory,
        "-ReceiverNamePattern", $ReceiverNamePattern,
        "-ExpectedHitNamePattern", $ExpectedHitNamePattern
    )
    if ($Strict) { $arguments += "-Strict" }
    if ($ExtendedReport -or $RawEvidence -or $VerbosePixelCsv) {
        $arguments += "-ExtendedReport"
    }
    & powershell @arguments
    $analysisExitCode = $LASTEXITCODE
    if ($analysisExitCode -ne 0) {
        $renderDocExitCode = $null
        $renderDocManifestPath = ""
        if ($CaptureRenderDocOnFailure) {
            $renderDocOutputDirectory = Join-Path $OutputDirectory `
                "renderdoc_failure"
            $renderDocManifestPath = Join-Path $renderDocOutputDirectory `
                "capture_manifest.json"
            $renderDocScript = Join-Path $projectRoot `
                "scripts\Capture-RenderDocFrame.ps1"
            $renderDocArguments = @(
                "-NoProfile",
                "-ExecutionPolicy", "Bypass",
                "-File", $renderDocScript,
                "-ExecutablePath", $ExecutablePath,
                "-OutputDirectory", $renderDocOutputDirectory,
                "-CaptureFrame", [string]$AuditStartFrame,
                "-AaMode", $AaMode,
                "-BenchmarkScene", $BenchmarkScene,
                "-SkipBuild",
                "-SkipSigning"
            )
            if ($Forward3DFbx) {
                $renderDocArguments += "-Forward3DFbx"
            }
            & powershell @renderDocArguments
            $renderDocExitCode = $LASTEXITCODE
        }
        [ordered]@{
            contractVersion = 1
            auditAnalysisExitCode = $analysisExitCode
            auditAnalysisPath = Join-Path $OutputDirectory "analysis.json"
            renderDocRequested = [bool]$CaptureRenderDocOnFailure
            renderDocExitCode = $renderDocExitCode
            renderDocManifestPath = $renderDocManifestPath
            renderDocReady =
                $CaptureRenderDocOnFailure -and
                $renderDocExitCode -eq 0 -and
                (Test-Path -LiteralPath $renderDocManifestPath)
        } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
            (Join-Path $OutputDirectory "observability_escalation.json")
        if ($CaptureRenderDocOnFailure -and $renderDocExitCode -ne 0) {
            throw "Full-audit analysis failed with exit code $analysisExitCode and RenderDoc escalation failed with exit code $renderDocExitCode"
        }
        throw "Full-audit analysis failed with exit code $analysisExitCode"
    }
}
