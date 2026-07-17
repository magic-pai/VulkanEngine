param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "tmp",
    [int]$WarmupFrames = 2,
    [int]$CaptureFrames = 8,
    [int]$AutoExitFrames = 14,
    [ValidateSet("low", "medium", "high", "ultra")]
    [string]$ShadowQuality = "high",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (-not $SkipBuild) {
    $buildCommand =
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cd /d "{0}\build" && MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo' -f $repoRoot
    cmd /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "SelfEngineForward3D build failed with exit code $LASTEXITCODE"
    }
}

$exePath = if ([System.IO.Path]::IsPathRooted($ExecutablePath)) {
    $ExecutablePath
} else {
    Join-Path $repoRoot $ExecutablePath
}
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Executable not found: $exePath"
}

$outputRoot = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
} else {
    Join-Path $repoRoot $OutputDirectory
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$csvPath = Join-Path $outputRoot "forward3d_shadow_regression.csv"
Remove-Item -LiteralPath $csvPath -ErrorAction SilentlyContinue

$managedKeys = @(
    "SE_WINDOW_WIDTH",
    "SE_WINDOW_HEIGHT",
    "SE_WINDOW_BORDERLESS",
    "SE_VISUAL_QA_HIDE_IMGUI",
    "SE_HIDE_IMGUI",
    "SE_BENCHMARK_SCENE",
    "SE_FORWARD3D_SHADOW_PROFILE",
    "SE_SHADOW_QUALITY",
    "SE_FORWARD3D_AA_MODE",
    "SE_RENDER_VIEW",
    "SE_GLOBAL_IBL_QUALITY",
    "SE_GLOBAL_IBL_SOURCE",
    "SE_GLOBAL_IBL_CACHE_POLICY",
    "SE_GLOBAL_IBL_CACHE",
    "SE_GLOBAL_IBL_CACHE_DIR",
    "SE_GLOBAL_IBL_ASSET",
    "SE_GLOBAL_IBL_SOURCE_ASSET",
    "SE_GLOBAL_IBL_SOURCE_PATH",
    "SE_IBL_QUALITY",
    "SE_IBL_SOURCE",
    "SE_IBL_CACHE_POLICY",
    "SE_IBL_CACHE",
    "SE_IBL_CACHE_DIR",
    "SE_IBL_ASSET",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_SHADOW_REGRESSION_CAMERA_CONTROLS",
    "SE_SHADOW_REGRESSION_RECT_LIGHT_ONLY",
    "SE_SHADOW_BIAS_MIN",
    "SE_SHADOW_BIAS_SLOPE",
    "SE_LOCAL_SHADOW_BIAS_MIN",
    "SE_LOCAL_SHADOW_BIAS_SLOPE",
    "SE_LOCAL_SHADOW_PCF_RADIUS",
    "SE_LOCAL_SHADOW_PCF_KERNEL_RADIUS",
    "SE_LOCAL_SHADOW_PCSS_STRENGTH",
    "SE_LOCAL_SHADOW_FACE_BLEND",
    "SE_LOCAL_SHADOW_POINT_BIAS_MIN",
    "SE_LOCAL_SHADOW_POINT_BIAS_SLOPE",
    "SE_LOCAL_SHADOW_POINT_PCF_RADIUS",
    "SE_LOCAL_SHADOW_POINT_PCF_KERNEL_RADIUS",
    "SE_LOCAL_SHADOW_POINT_PCSS_STRENGTH",
    "SE_LOCAL_SHADOW_SPOT_BIAS_MIN",
    "SE_LOCAL_SHADOW_SPOT_BIAS_SLOPE",
    "SE_LOCAL_SHADOW_SPOT_PCF_RADIUS",
    "SE_LOCAL_SHADOW_SPOT_PCF_KERNEL_RADIUS",
    "SE_LOCAL_SHADOW_SPOT_PCSS_STRENGTH",
    "SE_LOCAL_SHADOW_RECT_BIAS_MIN",
    "SE_LOCAL_SHADOW_RECT_BIAS_SLOPE",
    "SE_LOCAL_SHADOW_RECT_PCF_RADIUS",
    "SE_LOCAL_SHADOW_RECT_PCF_KERNEL_RADIUS",
    "SE_LOCAL_SHADOW_RECT_PCSS_STRENGTH",
    "SE_RECT_SHADOW_BIAS_SCALE",
    "SE_LOCAL_SHADOW_RECT_BIAS_SCALE",
    "SE_POINT_LIGHT_SHADOWS_OFF",
    "SE_SPOT_LIGHT_SHADOWS_OFF",
    "SE_RECT_LIGHT_SHADOWS_OFF",
    "SE_LOCAL_SHADOW_POINT_OFF",
    "SE_LOCAL_SHADOW_SPOT_OFF",
    "SE_LOCAL_SHADOW_RECT_OFF",
    "SE_LOCAL_SHADOW_DEBUG_LIGHT_INDEX",
    "SE_LOCAL_SHADOW_ONLY_LIGHT_INDEX",
    "SE_CONTACT_SHADOW_STRENGTH",
    "SE_SSAO_STRENGTH",
    "SE_ENABLE_GPU_TIMESTAMPS",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_AUTO_EXIT_FRAMES",
    "SE_BENCHMARK_CSV"
)
$previous = @{}
foreach ($key in $managedKeys) {
    $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
}

try {
    [Environment]::SetEnvironmentVariable("SE_WINDOW_WIDTH", "1280", "Process")
    [Environment]::SetEnvironmentVariable("SE_WINDOW_HEIGHT", "720", "Process")
    [Environment]::SetEnvironmentVariable("SE_WINDOW_BORDERLESS", "0", "Process")
    [Environment]::SetEnvironmentVariable("SE_VISUAL_QA_HIDE_IMGUI", "1", "Process")
    [Environment]::SetEnvironmentVariable("SE_HIDE_IMGUI", "1", "Process")
    [Environment]::SetEnvironmentVariable("SE_BENCHMARK_SCENE", "shadow-regression", "Process")
    [Environment]::SetEnvironmentVariable("SE_FORWARD3D_SHADOW_PROFILE", "production", "Process")
    [Environment]::SetEnvironmentVariable("SE_SHADOW_QUALITY", $ShadowQuality, "Process")
    [Environment]::SetEnvironmentVariable("SE_FORWARD3D_AA_MODE", "taa", "Process")
    [Environment]::SetEnvironmentVariable("SE_RENDER_VIEW", "lit", "Process")
    [Environment]::SetEnvironmentVariable("SE_GLOBAL_IBL_QUALITY", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_GLOBAL_IBL_SOURCE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_GLOBAL_IBL_CACHE_POLICY", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_GLOBAL_IBL_CACHE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_GLOBAL_IBL_CACHE_DIR", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_GLOBAL_IBL_ASSET", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_GLOBAL_IBL_SOURCE_ASSET", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_GLOBAL_IBL_SOURCE_PATH", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_IBL_QUALITY", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_IBL_SOURCE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_IBL_CACHE_POLICY", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_IBL_CACHE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_IBL_CACHE_DIR", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_IBL_ASSET", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION", "1", "Process")
    [Environment]::SetEnvironmentVariable("SE_SHADOW_REGRESSION_CAMERA_CONTROLS", "0", "Process")
    [Environment]::SetEnvironmentVariable("SE_SHADOW_REGRESSION_RECT_LIGHT_ONLY", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_SHADOW_BIAS_MIN", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_SHADOW_BIAS_SLOPE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_BIAS_MIN", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_BIAS_SLOPE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_PCF_RADIUS", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_PCF_KERNEL_RADIUS", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_PCSS_STRENGTH", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_FACE_BLEND", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_POINT_BIAS_MIN", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_POINT_BIAS_SLOPE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_POINT_PCF_RADIUS", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_POINT_PCF_KERNEL_RADIUS", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_POINT_PCSS_STRENGTH", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_SPOT_BIAS_MIN", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_SPOT_BIAS_SLOPE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_SPOT_PCF_RADIUS", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_SPOT_PCF_KERNEL_RADIUS", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_SPOT_PCSS_STRENGTH", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_RECT_BIAS_MIN", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_RECT_BIAS_SLOPE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_RECT_PCF_RADIUS", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_RECT_PCF_KERNEL_RADIUS", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_RECT_PCSS_STRENGTH", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_RECT_SHADOW_BIAS_SCALE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_RECT_BIAS_SCALE", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_POINT_LIGHT_SHADOWS_OFF", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_SPOT_LIGHT_SHADOWS_OFF", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_RECT_LIGHT_SHADOWS_OFF", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_POINT_OFF", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_SPOT_OFF", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_RECT_OFF", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_DEBUG_LIGHT_INDEX", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_LOCAL_SHADOW_ONLY_LIGHT_INDEX", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_CONTACT_SHADOW_STRENGTH", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_SSAO_STRENGTH", $null, "Process")
    [Environment]::SetEnvironmentVariable("SE_ENABLE_GPU_TIMESTAMPS", "1", "Process")
    [Environment]::SetEnvironmentVariable("SE_BENCHMARK_WARMUP_FRAMES", [string]$WarmupFrames, "Process")
    [Environment]::SetEnvironmentVariable("SE_BENCHMARK_FRAMES", [string]$CaptureFrames, "Process")
    [Environment]::SetEnvironmentVariable("SE_AUTO_EXIT_FRAMES", [string]$AutoExitFrames, "Process")
    [Environment]::SetEnvironmentVariable("SE_BENCHMARK_CSV", $csvPath, "Process")

    & $exePath | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "SelfEngineForward3D exited with code $LASTEXITCODE"
    }
} finally {
    foreach ($key in $managedKeys) {
        [Environment]::SetEnvironmentVariable($key, $previous[$key], "Process")
    }
}

if (-not (Test-Path -LiteralPath $csvPath)) {
    throw "Benchmark CSV was not written: $csvPath"
}

$rows = @(Import-Csv -LiteralPath $csvPath)
if ($rows.Count -eq 0) {
    throw "Benchmark CSV has no captured rows: $csvPath"
}

$animatedRows = @(
    $rows | Where-Object {
        [int]$_.'runtime_import_animation_playback_changed_bone_palette_entry_count' -gt 0
    }
)
if ($animatedRows.Count -eq 0) {
    throw "Shadow regression scene did not exercise animated skinned bone palettes."
}

$missingDynamicSkinnedCacheRows = @(
    $animatedRows | Where-Object {
        [int]$_.'local_shadow_cache_dynamic_skinned_caster_tiles' -le 0
    }
)
if ($missingDynamicSkinnedCacheRows.Count -gt 0) {
    throw "Animated skinned caster did not mark affected local shadow tiles as non-reusable for $($missingDynamicSkinnedCacheRows.Count) frame(s)."
}

$unsafeCacheReuseRows = @(
    $animatedRows | Where-Object {
        $assigned = [int]$_.'local_shadow_assigned_tiles'
        $dynamicSkinned = [int]$_.'local_shadow_cache_dynamic_skinned_caster_tiles'
        $skipped = [int]$_.'local_shadow_cache_skipped_tiles'
        $skipped -gt [Math]::Max($assigned - $dynamicSkinned, 0)
    }
)
if ($unsafeCacheReuseRows.Count -gt 0) {
    throw "Animated skinned casters allowed unsafe local shadow cache reuse on $($unsafeCacheReuseRows.Count) frame(s)."
}

$missingSkinnedBoundsRows = @(
    $animatedRows | Where-Object {
        [int]$_.'main_skinned_conservative_bounds' -le 0 -or
        [int]$_.'shadow_skinned_conservative_bounds' -le 0
    }
)
if ($missingSkinnedBoundsRows.Count -gt 0) {
    throw "Animated skinned caster did not keep conservative bounds in main/shadow queues for $($missingSkinnedBoundsRows.Count) frame(s)."
}

$maxMainCulled = ($rows | Measure-Object -Property main_culled -Maximum).Maximum
if ([int]$maxMainCulled -lt 2) {
    throw "Shadow regression scene did not cull the off-camera shadow casters from the main camera queue."
}

$maxMainVisible = ($rows | Measure-Object -Property main_visible -Maximum).Maximum
$maxShadowVisible = ($rows | Measure-Object -Property shadow_visible -Maximum).Maximum
if ([int]$maxShadowVisible -le [int]$maxMainVisible) {
    throw "Off-camera shadow casters were not preserved in the shadow queue. main_visible=$maxMainVisible shadow_visible=$maxShadowVisible"
}

$maxLocalShadowRecordedTilePasses =
    ($rows | Measure-Object -Property local_shadow_recorded_tile_passes -Maximum).Maximum
if ([int]$maxLocalShadowRecordedTilePasses -lt 8) {
    throw "Shadow regression scene did not record the expected rect-light local shadow tile."
}

$maxLocalShadowRecordedDraws =
    ($rows | Measure-Object -Property local_shadow_recorded_draws -Maximum).Maximum
$fullLocalShadowDrawEstimate = [int]$maxLocalShadowRecordedTilePasses * [int]$maxShadowVisible
if ($fullLocalShadowDrawEstimate -gt 0 -and
    [int]$maxLocalShadowRecordedDraws -ge $fullLocalShadowDrawEstimate) {
    throw "Local shadow per-tile culling did not reduce recorded draws. recorded=$maxLocalShadowRecordedDraws full=$fullLocalShadowDrawEstimate"
}

[pscustomobject]@{
    csv = $csvPath
    capturedRows = $rows.Count
    animatedRows = $animatedRows.Count
    maxLocalShadowSkippedTiles =
        ($rows | Measure-Object -Property local_shadow_cache_skipped_tiles -Maximum).Maximum
    maxLocalShadowDynamicSkinnedCasterTiles =
        ($rows | Measure-Object -Property local_shadow_cache_dynamic_skinned_caster_tiles -Maximum).Maximum
    maxLocalShadowRecordedTilePasses =
        $maxLocalShadowRecordedTilePasses
    minMainSkinnedConservativeBounds =
        ($animatedRows | Measure-Object -Property main_skinned_conservative_bounds -Minimum).Minimum
    minShadowSkinnedConservativeBounds =
        ($animatedRows | Measure-Object -Property shadow_skinned_conservative_bounds -Minimum).Minimum
    maxMainCulled = $maxMainCulled
    maxMainVisible = $maxMainVisible
    maxShadowVisible = $maxShadowVisible
    maxLocalShadowRecordedDraws = $maxLocalShadowRecordedDraws
    fullLocalShadowDrawEstimate = $fullLocalShadowDrawEstimate
}
