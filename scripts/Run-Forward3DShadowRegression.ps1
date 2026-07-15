param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [int]$Width = 2560,
    [int]$Height = 1440,
    [switch]$Windowed,
    [switch]$ShowImGui,
    [switch]$FixedCamera,
    [ValidateSet("dlaa", "taa", "sr-quality", "sr-balanced", "sr-performance", "off")]
    [string]$AaMode = "dlaa"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedExecutable = if ([System.IO.Path]::IsPathRooted($ExecutablePath)) {
    $ExecutablePath
} else {
    Join-Path $repoRoot $ExecutablePath
}
$workingDirectory = Split-Path -Parent $resolvedExecutable

if (-not (Test-Path -LiteralPath $resolvedExecutable)) {
    throw "Executable not found: $resolvedExecutable"
}

$env:SE_WINDOW_WIDTH = [string]$Width
$env:SE_WINDOW_HEIGHT = [string]$Height
$env:SE_BENCHMARK_SCENE = "shadow-regression"
$env:SE_FORWARD3D_SHADOW_PROFILE = "production"
$env:SE_SHADOW_QUALITY = "high"
$env:SE_FORWARD3D_AA_MODE = $AaMode
$env:SE_RENDER_VIEW = "lit"
$env:SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
$env:SE_BENCHMARK_CAMERA_MOTION = "static"
if ($FixedCamera) {
    Remove-Item Env:SE_SHADOW_REGRESSION_CAMERA_CONTROLS -ErrorAction SilentlyContinue
} else {
    $env:SE_SHADOW_REGRESSION_CAMERA_CONTROLS = "1"
}
Remove-Item Env:SE_SHADOW_CASCADE_MAX_DISTANCE -ErrorAction SilentlyContinue
Remove-Item Env:SE_SHADOW_BIAS_MIN -ErrorAction SilentlyContinue
Remove-Item Env:SE_SHADOW_BIAS_SLOPE -ErrorAction SilentlyContinue
Remove-Item Env:SE_CONTACT_SHADOW_STRENGTH -ErrorAction SilentlyContinue
Remove-Item Env:SE_SSAO_STRENGTH -ErrorAction SilentlyContinue

if ($Windowed) {
    Remove-Item Env:SE_WINDOW_BORDERLESS -ErrorAction SilentlyContinue
    Remove-Item Env:SE_BORDERLESS_FULLSCREEN -ErrorAction SilentlyContinue
    Remove-Item Env:SE_MAXIMIZE_BORDERLESS_FULLSCREEN -ErrorAction SilentlyContinue
} else {
    $env:SE_WINDOW_BORDERLESS = "1"
    $env:SE_BORDERLESS_FULLSCREEN = "1"
    $env:SE_MAXIMIZE_BORDERLESS_FULLSCREEN = "1"
}

if ($ShowImGui) {
    Remove-Item Env:SE_VISUAL_QA_HIDE_IMGUI -ErrorAction SilentlyContinue
    Remove-Item Env:SE_HIDE_IMGUI -ErrorAction SilentlyContinue
} else {
    $env:SE_VISUAL_QA_HIDE_IMGUI = "1"
    $env:SE_HIDE_IMGUI = "1"
}

Start-Process -FilePath $resolvedExecutable -WorkingDirectory $workingDirectory
