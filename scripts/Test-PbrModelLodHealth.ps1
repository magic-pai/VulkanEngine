[CmdletBinding()]
param(
    [string]$PbrExecutablePath = "build\Debug\SelfEnginePbrModelShowcase.exe",
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$LightingExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [string]$AssetPath = "assets\models\lvjuren.glb",
    [string]$OutputDirectory = "tmp\pbr_model_lod_health",
    [switch]$SkipBuild,
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
function Resolve-ProjectPath([string]$Path) {
    $candidate = if ([IO.Path]::IsPathRooted($Path)) {
        $Path
    } else {
        Join-Path $projectRoot $Path
    }
    return [IO.Path]::GetFullPath($candidate)
}

$pbrExecutable = Resolve-ProjectPath $PbrExecutablePath
$forwardExecutable = Resolve-ProjectPath $ForwardExecutablePath
$lightingExecutable = Resolve-ProjectPath $LightingExecutablePath
$asset = Resolve-ProjectPath $AssetPath
$output = Resolve-ProjectPath $OutputDirectory
New-Item -ItemType Directory -Force -Path $output | Out-Null

if (!$SkipBuild) {
    $buildCommand = @(
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1',
        "cd /d `"$projectRoot\build`"",
        'MSBuild SelfEnginePbrModelShowcase.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo',
        'MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo',
        'MSBuild SelfEngineLightingShowcase.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo'
    ) -join ' && '
    & cmd.exe /d /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "LOD target build failed with exit code $LASTEXITCODE"
    }
}

foreach ($executable in @($pbrExecutable, $forwardExecutable, $lightingExecutable)) {
    if (!(Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "LOD health executable not found: $executable"
    }
}
if (!(Test-Path -LiteralPath $asset -PathType Leaf)) {
    throw "LOD health asset not found: $asset"
}

$checks = [Collections.Generic.List[object]]::new()
function Add-Check([string]$Name, [bool]$Passed, [object]$Actual, [object]$Expected) {
    $checks.Add([pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }) | Out-Null
}

function Get-U64($Row, [string]$Name) {
    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing benchmark CSV column: $Name"
    }
    return [uint64]$property.Value
}

function Get-F64($Row, [string]$Name) {
    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing benchmark CSV column: $Name"
    }
    return [double]::Parse(
        [string]$property.Value,
        [Globalization.CultureInfo]::InvariantCulture
    )
}

function Get-Median([object[]]$Rows, [string]$Name) {
    $values = @(
        $Rows |
            ForEach-Object { $_.PSObject.Properties[$Name].Value } |
            Where-Object { ![string]::IsNullOrWhiteSpace([string]$_) } |
            ForEach-Object {
                [double]::Parse(
                    [string]$_,
                    [Globalization.CultureInfo]::InvariantCulture
                )
            } |
            Sort-Object
    )
    if ($values.Count -eq 0) {
        return $null
    }
    $middle = [int][Math]::Floor($values.Count / 2)
    if (($values.Count % 2) -eq 1) {
        return $values[$middle]
    }
    return ($values[$middle - 1] + $values[$middle]) * 0.5
}

function Invoke-Lane(
    [string]$Name,
    [string]$Executable,
    [bool]$LodEnabled,
    [double]$CameraDistance
) {
    $lane = Join-Path $output $Name
    New-Item -ItemType Directory -Force -Path $lane | Out-Null
    $csv = Join-Path $lane "benchmark.csv"
    $stdout = Join-Path $lane "stdout.log"
    $stderr = Join-Path $lane "stderr.log"
    $environment = [ordered]@{
        SE_PBR_MODEL_SHOWCASE_PATH = $asset
        SE_FORWARD3D_AA_MODE = "taa"
        SE_BENCHMARK_WARMUP_FRAMES = "6"
        SE_BENCHMARK_FRAMES = "6"
        SE_BENCHMARK_CSV = $csv
        SE_BENCHMARK_CAMERA_MOTION = "1"
        SE_BENCHMARK_CAMERA_MOTION_SPEED = "0"
        SE_BENCHMARK_CAMERA_MOTION_DISTANCE = $CameraDistance.ToString(
            [Globalization.CultureInfo]::InvariantCulture
        )
        SE_CAMERA_FREEZE = "0"
        SE_MESH_LOD = if ($LodEnabled) { "1" } else { "0" }
        SE_MESH_LOD_TARGET_PIXEL_ERROR = "1.0"
        SE_ENABLE_GPU_TIMESTAMPS = "1"
        SE_HIDE_IMGUI = "1"
        SE_WINDOW_WIDTH = "1280"
        SE_WINDOW_HEIGHT = "720"
    }
    $saved = @{}
    foreach ($entry in $environment.GetEnumerator()) {
        $saved[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, "Process")
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
    try {
        $process = Start-Process `
            -FilePath $Executable `
            -WorkingDirectory $projectRoot `
            -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr `
            -Wait `
            -PassThru
    } finally {
        foreach ($entry in $environment.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable($entry.Key, $saved[$entry.Key], "Process")
        }
    }

    Add-Check "$Name exits cleanly" ($process.ExitCode -eq 0) $process.ExitCode 0
    Add-Check "$Name writes benchmark CSV" (Test-Path -LiteralPath $csv) `
        (Test-Path -LiteralPath $csv) $true
    $rows = @(Import-Csv -LiteralPath $csv)
    Add-Check "$Name captures six rows" ($rows.Count -eq 6) $rows.Count 6
    $lines = Get-Content -LiteralPath $csv -TotalCount 2
    Add-Check "$Name CSV columns conserve" `
        ($lines[0].Split(',').Count -eq $lines[1].Split(',').Count) `
        "$($lines[0].Split(',').Count)/$($lines[1].Split(',').Count)" "equal"
    $diagnostics = @(
        Get-Content -LiteralPath $stdout, $stderr |
            Select-String -Pattern "VUID|Vulkan Validation|failed|exception" -CaseSensitive:$false
    )
    Add-Check "$Name has no validation/runtime diagnostics" `
        ($diagnostics.Count -eq 0) $diagnostics.Count 0

    return [pscustomobject]@{
        rows = $rows
        last = $rows[-1]
        gpuMainMedianMs = Get-Median $rows "gpu_main_ms"
        gpuTotalMedianMs = Get-Median $rows "gpu_total_recorded_ms"
    }
}

$near = Invoke-Lane "pbr-near-lod-on" $pbrExecutable $true 4.0
$far = Invoke-Lane "pbr-far-lod-on" $pbrExecutable $true 24.0
$off = Invoke-Lane "pbr-far-lod-off" $pbrExecutable $false 24.0
$forward = Invoke-Lane "forward3d-control" $forwardExecutable $true 15.0
$lighting = Invoke-Lane "lighting-showcase-control" $lightingExecutable $true 15.0

$n = $near.last
$f = $far.last
$o = $off.last
$sourceTriangles = Get-U64 $n "runtime_import_source_triangle_count"

Add-Check "near PBR lane resolves the enabled LOD contract" `
    ((Get-U64 $n "mesh_lod_enabled") -eq 1 -and
        (Get-U64 $n "mesh_lod_eligible_commands") -eq 1 -and
        (Get-U64 $n "mesh_lod_selected_commands") -eq 1) `
    "enabled/eligible/selected=$($n.mesh_lod_enabled)/$($n.mesh_lod_eligible_commands)/$($n.mesh_lod_selected_commands)" `
    "1/1/1"
Add-Check "near full-screen inspection keeps LOD0" `
    ((Get-U64 $n "mesh_lod_level_0_commands") -eq 1 -and
        (Get-U64 $n "mesh_lod_reduced_commands") -eq 0 -and
        (Get-U64 $n "mesh_lod_rendered_triangles") -eq $sourceTriangles -and
        (Get-F64 $n "mesh_lod_max_screen_fraction") -ge 0.65) `
    "lod0/reduced/triangles/fraction=$($n.mesh_lod_level_0_commands)/$($n.mesh_lod_reduced_commands)/$($n.mesh_lod_rendered_triangles)/$($n.mesh_lod_max_screen_fraction)" `
    "1/0/$sourceTriangles/>=0.65"
Add-Check "runtime owns a four-level compact GPU chain" `
    ((Get-U64 $n "mesh_lod_resident_chain_count") -eq 1 -and
        (Get-U64 $n "mesh_lod_resident_level_count") -eq 4 -and
        (Get-U64 $n "mesh_lod_extra_vertex_bytes") -lt
            [uint64]([double](Get-U64 $n "mesh_lod_source_vertex_bytes") * 1.25) -and
        (Get-U64 $n "mesh_lod_extra_index_bytes") -lt
            (Get-U64 $n "mesh_lod_source_index_bytes")) `
    "chains/levels/extra-v/extra-i=$($n.mesh_lod_resident_chain_count)/$($n.mesh_lod_resident_level_count)/$($n.mesh_lod_extra_vertex_bytes)/$($n.mesh_lod_extra_index_bytes)" `
    "1/4/<1.25x source-v/<source-i"

Add-Check "far PBR lane selects a lower GPU mesh" `
    ((Get-U64 $f "mesh_lod_reduced_commands") -eq 1 -and
        (Get-U64 $f "mesh_lod_level_3_commands") -eq 1 -and
        (Get-U64 $f "mesh_lod_rendered_triangles") -lt
            (Get-U64 $f "mesh_lod_source_triangles")) `
    "reduced/lod3/rendered/source=$($f.mesh_lod_reduced_commands)/$($f.mesh_lod_level_3_commands)/$($f.mesh_lod_rendered_triangles)/$($f.mesh_lod_source_triangles)" `
    "1/1/rendered<source"
Add-Check "far PBR lane removes at least eighty percent of eligible triangles" `
    ((Get-U64 $f "mesh_lod_saved_triangles") -ge
        [uint64]([double](Get-U64 $f "mesh_lod_source_triangles") * 0.8)) `
    $f.mesh_lod_saved_triangles ">=80% of $($f.mesh_lod_source_triangles)"
Add-Check "far selected LOD remains inside hysteretic pixel budget" `
    ((Get-F64 $f "mesh_lod_max_selected_error_pixels") -le
        (Get-F64 $f "mesh_lod_target_pixel_error") * 1.15) `
    $f.mesh_lod_max_selected_error_pixels "<=1.15x target"
Add-Check "steady near and far cameras do not chatter between LOD levels" `
    ((@($near.rows + $far.rows | Where-Object {
        (Get-U64 $_ "mesh_lod_transition_count") -ne 0
    })).Count -eq 0) `
    (@($near.rows + $far.rows | Where-Object {
        (Get-U64 $_ "mesh_lod_transition_count") -ne 0
    })).Count 0

Add-Check "LOD-off control restores source geometry" `
    ((Get-U64 $o "mesh_lod_enabled") -eq 0 -and
        (Get-U64 $o "mesh_lod_selected_commands") -eq 0 -and
        (Get-U64 $o "mesh_lod_reduced_commands") -eq 0 -and
        (Get-U64 $o "mesh_lod_level_0_commands") -eq 1 -and
        (Get-U64 $o "mesh_lod_rendered_triangles") -eq
            (Get-U64 $o "mesh_lod_source_triangles")) `
    "enabled/selected/reduced/lod0/rendered=$($o.mesh_lod_enabled)/$($o.mesh_lod_selected_commands)/$($o.mesh_lod_reduced_commands)/$($o.mesh_lod_level_0_commands)/$($o.mesh_lod_rendered_triangles)" `
    "0/0/0/1/source"
Add-Check "main GBuffer consumes the selected lower mesh" `
    ((Get-U64 $f "gbuffer_triangles") -lt (Get-U64 $o "gbuffer_triangles")) `
    "$($f.gbuffer_triangles)/$($o.gbuffer_triangles)" "far-on < far-off"
Add-Check "directional shadows remain on LOD0" `
    ((Get-U64 $f "shadow_triangles") -eq (Get-U64 $o "shadow_triangles") -and
        (Get-U64 $f "shadow_triangles") -ge $sourceTriangles) `
    "$($f.shadow_triangles)/$($o.shadow_triangles)" "equal and >= $sourceTriangles"

foreach ($control in @(
    [pscustomobject]@{ name = "Forward3D"; row = $forward.last },
    [pscustomobject]@{ name = "LightingShowcase"; row = $lighting.last }
)) {
    $row = $control.row
    $eligible = Get-U64 $row "mesh_lod_eligible_commands"
    $selected = Get-U64 $row "mesh_lod_selected_commands"
    $source = Get-U64 $row "mesh_lod_source_triangles"
    $rendered = Get-U64 $row "mesh_lod_rendered_triangles"
    Add-Check "$($control.name) resolves scene-independent LOD state" `
        ((Get-U64 $row "mesh_lod_enabled") -eq 1 -and
            $selected -eq $eligible -and
            $rendered -le $source) `
        "enabled/selected/eligible/rendered/source=$($row.mesh_lod_enabled)/$selected/$eligible/$rendered/$source" `
        "1/selected=eligible/rendered<=source"
}
Add-Check "Forward3D keeps animated skinned geometry on LOD0" `
    ((Get-U64 $forward.last "mesh_lod_skinned_excluded_commands") -gt 0 -and
        (Get-U64 $forward.last "main_skinned_conservative_bounds") -gt 0) `
    "excluded/skinned-bounds=$($forward.last.mesh_lod_skinned_excluded_commands)/$($forward.last.main_skinned_conservative_bounds)" `
    ">0/>0"

$failed = @($checks | Where-Object status -eq "fail")
$health = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    meshoptimizerVersion = "v1.2"
    asset = $asset
    passed = $checks.Count - $failed.Count
    failed = $failed.Count
    metrics = [ordered]@{
        nearTriangles = Get-U64 $n "mesh_lod_rendered_triangles"
        farTriangles = Get-U64 $f "mesh_lod_rendered_triangles"
        farSavedTriangles = Get-U64 $f "mesh_lod_saved_triangles"
        farSelectedErrorPixels = Get-F64 $f "mesh_lod_max_selected_error_pixels"
        residentVertexBytes = Get-U64 $f "mesh_lod_resident_vertex_bytes"
        residentIndexBytes = Get-U64 $f "mesh_lod_resident_index_bytes"
        nearGpuMainMedianMs = $near.gpuMainMedianMs
        farGpuMainMedianMs = $far.gpuMainMedianMs
        offGpuMainMedianMs = $off.gpuMainMedianMs
        nearGpuTotalMedianMs = $near.gpuTotalMedianMs
        farGpuTotalMedianMs = $far.gpuTotalMedianMs
        offGpuTotalMedianMs = $off.gpuTotalMedianMs
    }
    checks = $checks
}
$healthPath = Join-Path $output "pbr_model_lod_health.json"
$health | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $healthPath -Encoding UTF8
Write-Host "PBR model LOD health: $($health.passed) pass / $($health.failed) fail"
Write-Host "Near/far triangles: $($health.metrics.nearTriangles)/$($health.metrics.farTriangles)"
Write-Host "Far selected error: $($health.metrics.farSelectedErrorPixels) px"
Write-Host "Report: $healthPath"
if ($Strict -and $failed.Count -gt 0) {
    $failed | Format-Table -AutoSize | Out-Host
    exit 1
}
