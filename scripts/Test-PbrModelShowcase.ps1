[CmdletBinding()]
param(
    [string]$ExecutablePath = "build\Debug\SelfEnginePbrModelShowcase.exe",
    [string]$AssetPath = "assets\models\lvjuren.glb",
    [string]$OutputDirectory = "tmp\pbr_model_showcase_health",
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

$executable = Resolve-ProjectPath $ExecutablePath
$asset = Resolve-ProjectPath $AssetPath
$output = Resolve-ProjectPath $OutputDirectory
New-Item -ItemType Directory -Force -Path $output | Out-Null

if (!$SkipBuild) {
    $buildCommand = @(
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1',
        "cd /d `"$projectRoot\build`"",
        'MSBuild SelfEnginePbrModelShowcase.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo'
    ) -join ' && '
    & cmd.exe /d /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "PBR model showcase build failed with exit code $LASTEXITCODE"
    }
}
if (!(Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "PBR model showcase executable not found: $executable"
}

$validatorReport = Join-Path $output "gltf-validator-report.json"
& (Join-Path $PSScriptRoot "Validate-GltfAsset.ps1") `
    -AssetPath $asset `
    -ReportPath $validatorReport `
    -Strict
if ($LASTEXITCODE -ne 0) {
    throw "glTF validation failed with exit code $LASTEXITCODE"
}
$assetReport = Get-Content -Raw -LiteralPath $validatorReport | ConvertFrom-Json

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

function Invoke-Lane([string]$Name, [bool]$DisableTangents) {
    $lane = Join-Path $output $Name
    New-Item -ItemType Directory -Force -Path $lane | Out-Null
    $csv = Join-Path $lane "benchmark.csv"
    $stdout = Join-Path $lane "stdout.log"
    $stderr = Join-Path $lane "stderr.log"
    $environment = [ordered]@{
        SE_PBR_MODEL_SHOWCASE_PATH = $asset
        SE_FORWARD3D_AA_MODE = "taa"
        SE_BENCHMARK_WARMUP_FRAMES = "3"
        SE_BENCHMARK_FRAMES = "2"
        SE_BENCHMARK_CSV = $csv
        SE_CAMERA_FREEZE = "1"
        SE_HIDE_IMGUI = "1"
        SE_WINDOW_WIDTH = "1280"
        SE_WINDOW_HEIGHT = "720"
        SE_RUNTIME_IMPORT_TANGENTS_OFF = if ($DisableTangents) { "1" } else { "0" }
    }
    $saved = @{}
    foreach ($entry in $environment.GetEnumerator()) {
        $saved[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, "Process")
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
    try {
        $process = Start-Process `
            -FilePath $executable `
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
    Add-Check "$Name captures two rows" ($rows.Count -eq 2) $rows.Count 2
    $lines = Get-Content -LiteralPath $csv -TotalCount 2
    Add-Check "$Name CSV columns conserve" `
        ($lines[0].Split(',').Count -eq $lines[1].Split(',').Count) `
        "$($lines[0].Split(',').Count)/$($lines[1].Split(',').Count)" "equal"
    $diagnostics = @(
        Get-Content -LiteralPath $stdout, $stderr |
            Select-String -Pattern "VUID|Vulkan Validation|error|failed|exception" -CaseSensitive:$false
    )
    Add-Check "$Name has no validation/runtime diagnostics" `
        ($diagnostics.Count -eq 0) $diagnostics.Count 0
    return [pscustomobject]@{ rows = $rows; last = $rows[-1] }
}

Add-Check "Khronos validator accepts the GLB" `
    ([int]$assetReport.issues.numErrors -eq 0) $assetReport.issues.numErrors 0
$normal = Invoke-Lane "tangent-generation-on" $false
$control = Invoke-Lane "tangent-generation-off" $true
$n = $normal.last
$c = $control.last
$assetVertices = [uint64]$assetReport.info.totalVertexCount
$assetTriangles = [uint64]$assetReport.info.totalTriangleCount

Add-Check "runtime imports the requested PBR model" `
    ((Get-U64 $n "runtime_import_model_requested") -eq 1 -and
        (Get-U64 $n "runtime_import_model_loaded") -eq 1 -and
        (Get-U64 $n "runtime_import_mesh_count") -eq [uint64]$assetReport.info.drawCallCount) `
    "requested/loaded/mesh=$($n.runtime_import_model_requested)/$($n.runtime_import_model_loaded)/$($n.runtime_import_mesh_count)" `
    "1/1/$($assetReport.info.drawCallCount)"
Add-Check "Assimp geometry matches glTF validator" `
    ((Get-U64 $n "runtime_import_source_vertex_count") -eq $assetVertices -and
        (Get-U64 $n "runtime_import_source_triangle_count") -eq $assetTriangles) `
    "$($n.runtime_import_source_vertex_count)/$($n.runtime_import_source_triangle_count)" `
    "$assetVertices/$assetTriangles"
Add-Check "normal-mapped vertices have generated tangents" `
    ((Get-U64 $n "runtime_import_source_tangent_generation_enabled") -eq 1 -and
        (Get-U64 $n "runtime_import_source_tangent_vertex_count") -eq $assetVertices) `
    "enabled/tangents=$($n.runtime_import_source_tangent_generation_enabled)/$($n.runtime_import_source_tangent_vertex_count)" `
    "1/$assetVertices"
Add-Check "PBR texture semantics survive import" `
    ((Get-U64 $n "runtime_import_source_textured_material_count") -gt 0 -and
        (Get-U64 $n "runtime_import_source_base_color_texture_material_count") -gt 0 -and
        (Get-U64 $n "runtime_import_source_normal_texture_material_count") -gt 0 -and
        (Get-U64 $n "runtime_import_source_metallic_roughness_texture_material_count") -gt 0 -and
        (Get-U64 $n "frame_material_textured_count") -gt 0) `
    "textured/base/normal/mr/frame=$($n.runtime_import_source_textured_material_count)/$($n.runtime_import_source_base_color_texture_material_count)/$($n.runtime_import_source_normal_texture_material_count)/$($n.runtime_import_source_metallic_roughness_texture_material_count)/$($n.frame_material_textured_count)" `
    "all > 0"
Add-Check "model reaches GBuffer and directional shadow consumers" `
    ((Get-U64 $n "gbuffer_triangles") -ge $assetTriangles -and
        (Get-U64 $n "shadow_triangles") -ge $assetTriangles) `
    "gbuffer/shadow=$($n.gbuffer_triangles)/$($n.shadow_triangles)" `
    ">= $assetTriangles"
Add-Check "studio local-light rig is active" `
    ((Get-U64 $n "frame_rect_light_count") -ge 2 -and
        (Get-U64 $n "frame_spot_light_count") -ge 1) `
    "rect/spot=$($n.frame_rect_light_count)/$($n.frame_spot_light_count)" `
    ">=2/>=1"
Add-Check "Debug tangent control isolates source fallback" `
    ((Get-U64 $c "runtime_import_source_tangent_generation_enabled") -eq 0 -and
        (Get-U64 $c "runtime_import_source_tangent_vertex_count") -eq 0 -and
        (Get-U64 $c "runtime_import_source_normal_texture_material_count") -gt 0) `
    "enabled/tangents/normal=$($c.runtime_import_source_tangent_generation_enabled)/$($c.runtime_import_source_tangent_vertex_count)/$($c.runtime_import_source_normal_texture_material_count)" `
    "0/0/>0"

$failed = @($checks | Where-Object status -eq "fail")
$health = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    asset = $asset
    validatorVersion = $assetReport.validatorVersion
    passed = $checks.Count - $failed.Count
    failed = $failed.Count
    checks = $checks
}
$healthPath = Join-Path $output "pbr_model_showcase_health.json"
$health | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $healthPath -Encoding UTF8
Write-Host "PBR model showcase health: $($health.passed) pass / $($health.failed) fail"
Write-Host "Report: $healthPath"
if ($Strict -and $failed.Count -gt 0) {
    $failed | Format-Table -AutoSize | Out-Host
    exit 1
}
