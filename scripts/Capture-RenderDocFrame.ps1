[CmdletBinding()]
param(
    [string]$ExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [string]$OutputDirectory = "tmp\renderdoc_capture",
    [string]$RenderDocDirectory = "",
    [ValidateRange(1, 10000)]
    [uint32]$CaptureFrame = 12,
    [ValidateSet("taa", "dlaa", "sr-quality", "sr-balanced", "sr-performance")]
    [string]$AaMode = "taa",
    [string]$BenchmarkScene = "lighting-showcase",
    [switch]$Forward3DFbx,
    [switch]$KeepWindowVisible,
    [switch]$SkipBuild,
    [switch]$SkipSigning
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
function Resolve-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $projectRoot $Path))
}

function Resolve-RenderDocCommand {
    if (![string]::IsNullOrWhiteSpace($RenderDocDirectory)) {
        $candidate = Join-Path $RenderDocDirectory "renderdoccmd.exe"
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }
    $command = Get-Command renderdoccmd.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    foreach ($candidate in @(
        "$env:ProgramFiles\RenderDoc\renderdoccmd.exe",
        "${env:ProgramFiles(x86)}\RenderDoc\renderdoccmd.exe"
    )) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }
    throw "renderdoccmd.exe was not found"
}

$ExecutablePath = Resolve-ProjectPath $ExecutablePath
$OutputDirectory = Resolve-ProjectPath $OutputDirectory
$renderDocCommand = Resolve-RenderDocCommand
$targetName = [IO.Path]::GetFileNameWithoutExtension($ExecutablePath)
if ($targetName -notin @("SelfEngineForward3D", "SelfEngineLightingShowcase")) {
    throw "Unsupported RenderDoc target: $targetName"
}

if (!$SkipBuild) {
    $projectFile = "$targetName.vcxproj"
    $buildDirectory = Join-Path $projectRoot "build"
    $msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    & $msbuild (Join-Path $buildDirectory $projectFile) `
        /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo
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
        throw "RenderDoc target signing failed with exit code $LASTEXITCODE"
    }
    Start-Sleep -Milliseconds 1000
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$statusPath = Join-Path $OutputDirectory "renderdoc_status.json"
$manifestPath = Join-Path $OutputDirectory "capture_manifest.json"
$failurePath = Join-Path $OutputDirectory "capture_failure.json"
$stdoutPath = Join-Path $OutputDirectory "stdout.log"
$stderrPath = Join-Path $OutputDirectory "stderr.log"
$benchmarkPath = Join-Path $OutputDirectory "benchmark.csv"
$thumbnailPath = Join-Path $OutputDirectory "capture_thumbnail.png"
$thumbnailStdoutPath = Join-Path $OutputDirectory "thumbnail.stdout.log"
$thumbnailStderrPath = Join-Path $OutputDirectory "thumbnail.stderr.log"
$captureBaseName = "$targetName-frame-$CaptureFrame"
$captureTemplate = Join-Path $OutputDirectory $captureBaseName
foreach ($path in @(
    $statusPath,
    $manifestPath,
    $failurePath,
    $stdoutPath,
    $stderrPath,
    $benchmarkPath,
    $thumbnailPath,
    $thumbnailStdoutPath,
    $thumbnailStderrPath
)) {
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
}
Get-ChildItem -LiteralPath $OutputDirectory -Filter "$captureBaseName*.rdc" `
    -File -ErrorAction SilentlyContinue |
    Remove-Item -Force

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $ExecutablePath).Hash
$signature = Get-AuthenticodeSignature -LiteralPath $ExecutablePath
$effectiveScene = if ($Forward3DFbx) { "forward3d-fbx" } else { $BenchmarkScene }
$captureComments = [ordered]@{
    contractVersion = 1
    executable = $ExecutablePath
    executableSha256 = $hash
    scene = $effectiveScene
    renderedFrame = $CaptureFrame
    aaMode = $AaMode
    resolutionSource = "runtime"
} | ConvertTo-Json -Compress

$managedKeys = @(
    "SE_RENDERDOC_CAPTURE_FRAME",
    "SE_RENDERDOC_REQUIRE_API",
    "SE_RENDERDOC_STATUS_JSON",
    "SE_RENDERDOC_CAPTURE_PATH",
    "SE_RENDERDOC_CAPTURE_TITLE",
    "SE_RENDERDOC_CAPTURE_COMMENTS",
    "SE_SSR",
    "SE_SSR_BACKEND",
    "SE_HYBRID_REFLECTIONS_RT",
    "SE_FORWARD3D_AA_MODE",
    "SE_BENCHMARK_SCENE",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_LIGHTING_SHOWCASE_FORCE_OFF",
    "SE_SCENE_UPDATE_FREEZE",
    "SE_VISUAL_QA_HIDE_IMGUI",
    "SE_WINDOW_HIDDEN",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_BENCHMARK_CSV",
    "SE_AUTO_EXIT_FRAMES"
)
$previous = @{}
foreach ($key in $managedKeys) {
    $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
    [Environment]::SetEnvironmentVariable($key, $null, "Process")
}

$environment = @{
    SE_RENDERDOC_CAPTURE_FRAME = [string]$CaptureFrame
    SE_RENDERDOC_REQUIRE_API = "1"
    SE_RENDERDOC_STATUS_JSON = $statusPath
    SE_RENDERDOC_CAPTURE_PATH = $captureTemplate
    SE_RENDERDOC_CAPTURE_TITLE = "SelfEngine $effectiveScene frame $CaptureFrame"
    SE_RENDERDOC_CAPTURE_COMMENTS = $captureComments
    SE_SSR = "1"
    SE_SSR_BACKEND = "ffx-sssr"
    SE_HYBRID_REFLECTIONS_RT = "1"
    SE_FORWARD3D_AA_MODE = $AaMode
    SE_BENCHMARK_SCENE = if ($Forward3DFbx) { "" } else { $BenchmarkScene }
    SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = if ($Forward3DFbx) { "1" } else { "" }
    SE_LIGHTING_SHOWCASE_FORCE_OFF = if ($Forward3DFbx) { "1" } else { "" }
    SE_SCENE_UPDATE_FREEZE = "1"
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_WINDOW_HIDDEN = if ($KeepWindowVisible) { "0" } else { "1" }
    SE_BENCHMARK_WARMUP_FRAMES = [string]([Math]::Max(0, $CaptureFrame - 1))
    SE_BENCHMARK_FRAMES = "1"
    SE_BENCHMARK_CSV = $benchmarkPath
    SE_AUTO_EXIT_FRAMES = [string]($CaptureFrame + 2)
}
foreach ($entry in $environment.GetEnumerator()) {
    [Environment]::SetEnvironmentVariable(
        $entry.Key,
        [string]$entry.Value,
        "Process"
    )
}

$startedAt = Get-Date
$executableDirectory = Split-Path -Parent $ExecutablePath
try {
    $renderDocArguments = @(
        "capture",
        "--working-dir", "`"$executableDirectory`"",
        "--capture-file", "`"$captureTemplate`"",
        "--wait-for-exit",
        "`"$ExecutablePath`""
    )
    $renderDocProcess = Start-Process `
        -FilePath $renderDocCommand `
        -ArgumentList $renderDocArguments `
        -PassThru `
        -Wait `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath
    $exitCode = $renderDocProcess.ExitCode
} finally {
    foreach ($entry in $previous.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable(
            $entry.Key,
            $entry.Value,
            "Process"
        )
    }
}

if (!(Test-Path -LiteralPath $statusPath)) {
    $events = @(
        Get-WinEvent -LogName "Microsoft-Windows-CodeIntegrity/Operational" `
            -MaxEvents 80 -ErrorAction SilentlyContinue |
        Where-Object {
            $_.TimeCreated -ge $startedAt.AddSeconds(-2) -and
            $_.Message -match [regex]::Escape(
                [IO.Path]::GetFileName($ExecutablePath)
            )
        } |
        Select-Object TimeCreated,Id,LevelDisplayName,Message
    )
    [ordered]@{
        executable = $ExecutablePath
        exitCode = $exitCode
        statusMissing = $true
        codeIntegrityEvents = $events
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $failurePath
    throw "RenderDoc target did not emit its capture status; see $failurePath"
}

$status = Get-Content -Raw -LiteralPath $statusPath | ConvertFrom-Json
$capturePath = [string]$status.capture_path
$captureExists = ![string]::IsNullOrWhiteSpace($capturePath) -and
    (Test-Path -LiteralPath $capturePath)
$captureBytes = if ($captureExists) {
    [uint64](Get-Item -LiteralPath $capturePath).Length
} else { [uint64]0 }
$thumbnailExitCode = -1
$thumbnailWidth = 0
$thumbnailHeight = 0
if ($captureExists) {
    $thumbnailArguments = @(
        "thumb",
        "--out=`"$thumbnailPath`"",
        "--format=png",
        "--max-size=1024",
        "`"$capturePath`""
    )
    $thumbnailProcess = Start-Process `
        -FilePath $renderDocCommand `
        -ArgumentList $thumbnailArguments `
        -PassThru `
        -Wait `
        -WindowStyle Hidden `
        -RedirectStandardOutput $thumbnailStdoutPath `
        -RedirectStandardError $thumbnailStderrPath
    $thumbnailExitCode = $thumbnailProcess.ExitCode
    if ($thumbnailExitCode -eq 0 -and
        (Test-Path -LiteralPath $thumbnailPath)) {
        Add-Type -AssemblyName System.Drawing
        $thumbnail = [Drawing.Image]::FromFile($thumbnailPath)
        try {
            $thumbnailWidth = $thumbnail.Width
            $thumbnailHeight = $thumbnail.Height
        } finally {
            $thumbnail.Dispose()
        }
    }
}
$thumbnailReady = $thumbnailExitCode -eq 0 -and
    (Test-Path -LiteralPath $thumbnailPath) -and
    (Get-Item -LiteralPath $thumbnailPath).Length -gt 0 -and
    $thumbnailWidth -gt 0 -and
    $thumbnailHeight -gt 0
$toolVersion = (& $renderDocCommand version 2>&1 | Out-String).Trim()
$contractReady =
    $exitCode -eq 0 -and
    [bool]$status.requested -and
    [bool]$status.available -and
    [bool]$status.started -and
    [bool]$status.ended -and
    [bool]$status.succeeded -and
    [uint32]$status.target_frame -eq $CaptureFrame -and
    [uint32]$status.started_frame -eq $CaptureFrame -and
    [uint32]$status.ended_frame -eq $CaptureFrame -and
    $captureExists -and
    $captureBytes -gt 0 -and
    $thumbnailReady

[ordered]@{
    contractVersion = 1
    contractReady = $contractReady
    renderDocCommand = $renderDocCommand
    renderDocVersion = $toolVersion
    executable = $ExecutablePath
    executableSha256 = $hash
    signatureStatus = $signature.Status.ToString()
    scene = $effectiveScene
    renderedFrame = $CaptureFrame
    aaMode = $AaMode
    processExitCode = $exitCode
    capturePath = $capturePath
    captureBytes = $captureBytes
    thumbnailPath = $thumbnailPath
    thumbnailReady = $thumbnailReady
    thumbnailExitCode = $thumbnailExitCode
    thumbnailWidth = $thumbnailWidth
    thumbnailHeight = $thumbnailHeight
    statusPath = $statusPath
    benchmarkPath = $benchmarkPath
    status = $status
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath

if (!$contractReady) {
    throw "RenderDoc capture contract failed; see $manifestPath"
}

Write-Host "RenderDoc capture ready: $capturePath"
Write-Host "Thumbnail: $thumbnailPath ($thumbnailWidth x $thumbnailHeight)"
Write-Host "Manifest: $manifestPath"
