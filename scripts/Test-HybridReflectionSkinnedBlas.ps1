[CmdletBinding()]
param(
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$ShowcaseExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [string]$OutputDirectory = "tmp\hybrid_reflection_skinned_blas",
    [switch]$SkipBuild,
    [switch]$SkipSigning,
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
    return (Resolve-Path $candidate).Path
}
if (![IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$forwardExecutable = Resolve-ProjectPath $ForwardExecutablePath
$showcaseExecutable = Resolve-ProjectPath $ShowcaseExecutablePath

if (!$SkipBuild) {
    $buildCommand = @(
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1',
        "cd /d `"$projectRoot\build`"",
        'MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo',
        'MSBuild SelfEngineLightingShowcase.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo'
    ) -join ' && '
    & cmd.exe /d /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Skinned BLAS build failed with exit code $LASTEXITCODE"
    }
}

$checks = [Collections.Generic.List[object]]::new()
function Add-Check(
    [string]$Name,
    [bool]$Passed,
    [object]$Actual,
    [object]$Expected
) {
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
        throw "Missing CSV column: $Name"
    }
    return [uint64]$property.Value
}
function Get-Max([object[]]$Rows, [string]$Name) {
    return [uint64](($Rows | ForEach-Object { Get-U64 $_ $Name } |
        Measure-Object -Maximum).Maximum)
}
function Test-Logs([string]$Directory) {
    $matches = @(Get-ChildItem -LiteralPath $Directory -Filter "*.log" |
        ForEach-Object {
            Select-String -LiteralPath $_.FullName `
                -Pattern "VUID|validation|error|failed|exception|shader" `
                -CaseSensitive:$false
        })
    return $matches.Count -eq 0
}

$shader = Get-Content -Raw (Join-Path $projectRoot `
    "assets\shaders\hybrid_reflection_skinning.hlsl")
$asSource = Get-Content -Raw (Join-Path $projectRoot `
    "src\renderer\vulkan\hybrid_reflection_acceleration_structures.cpp")
$cmake = Get-Content -Raw (Join-Path $projectRoot "CMakeLists.txt")
Add-Check "GPU skinning shader preserves the Vertex3D byte ABI" `
    ($shader -match 'kBoneIndicesOffset = 60u' -and `
        $shader -match 'kBoneWeightsOffset = 76u' -and `
        $shader -match 'CopyVertex' -and `
        $shader -match 'Store3') `
    "indices/weights/copy/store=$($shader -match '60u')/$($shader -match '76u')/$($shader -match 'CopyVertex')/$($shader -match 'Store3')" `
    "true/true/true/true"
Add-Check "dynamic BLAS uses the standard update contract" `
    ($asSource -match 'VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR' -and `
        $asSource -match 'VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR' -and `
        $asSource -match 'VK_ACCESS_SHADER_WRITE_BIT' -and `
        $asSource -match 'VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR' -and `
        $asSource -match 'dynamicBlasCaches') `
    "allow/update/barrier/frameSlots present" "all present"
Add-Check "production and audit skinning shaders are build products" `
    ($cmake -match 'hybrid_reflection_skinning.hlsl.spv' -and `
        $cmake -match 'hybrid_reflection_skinning_audit.hlsl.spv' -and `
        $cmake -match 'SE_HYBRID_REFLECTION_SKINNING_AUDIT') `
    "production/audit/define present" "all present"

$captureScript = Join-Path $projectRoot `
    "scripts\Capture-HybridReflectionFullAudit.ps1"
function Invoke-Lane(
    [string]$Name,
    [string]$Executable,
    [switch]$Forward3DFbx,
    [switch]$FreezeFbxAnimation,
    [switch]$DisableSkinnedBlas
) {
    $directory = Join-Path $OutputDirectory $Name
    $arguments = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $captureScript,
        "-ExecutablePath", $Executable,
        "-OutputDirectory", $directory,
        "-CaptureFrames", "1",
        "-AuditStartFrame", "12",
        "-SkipBuild",
        "-SkipAnalysis"
    )
    if ($SkipSigning) { $arguments += "-SkipSigning" }
    if ($Forward3DFbx) { $arguments += "-Forward3DFbx" }
    if ($FreezeFbxAnimation) { $arguments += "-FreezeFbxAnimation" }
    if ($DisableSkinnedBlas) { $arguments += "-DisableSkinnedBlas" }
    & powershell @arguments
    Add-Check "$Name capture exits" ($LASTEXITCODE -eq 0) $LASTEXITCODE 0
    $csvPath = Join-Path $directory "benchmark.csv"
    Add-Check "$Name writes benchmark CSV" (Test-Path $csvPath) `
        (Test-Path $csvPath) $true
    $rows = @(Import-Csv -LiteralPath $csvPath)
    Add-Check "$Name captures seven steady rows" ($rows.Count -eq 7) `
        $rows.Count 7
    $rawLines = Get-Content -LiteralPath $csvPath -TotalCount 2
    Add-Check "$Name CSV columns conserve" `
        ($rawLines[0].Split(',').Count -eq $rawLines[1].Split(',').Count) `
        "$($rawLines[0].Split(',').Count)/$($rawLines[1].Split(',').Count)" `
        "equal"
    Add-Check "$Name has no validation or shader diagnostics" `
        (Test-Logs $directory) "clean=$((Test-Logs $directory))" "true"
    return [pscustomobject]@{
        name = $Name
        directory = $directory
        rows = $rows
        last = $rows[-1]
    }
}

$dynamic = Invoke-Lane -Name "dynamic-forward3d" `
    -Executable $forwardExecutable -Forward3DFbx
$frozen = Invoke-Lane -Name "frozen-forward3d" `
    -Executable $forwardExecutable -Forward3DFbx -FreezeFbxAnimation
$disabled = Invoke-Lane -Name "disabled-forward3d" `
    -Executable $forwardExecutable -Forward3DFbx -DisableSkinnedBlas
$static = Invoke-Lane -Name "static-lighting-showcase" `
    -Executable $showcaseExecutable

$d = $dynamic.last
$dynamicPass =
    (Get-U64 $d "hybrid_reflections_acceleration_structure_contract_version") -eq 3 -and
    (Get-U64 $d "hybrid_reflections_skinned_candidate_count") -gt 0 -and
    (Get-U64 $d "hybrid_reflections_skinned_candidate_count") -eq
        (Get-U64 $d "hybrid_reflections_skinned_eligible_count") -and
    (Get-U64 $d "hybrid_reflections_skinned_tlas_instance_count") -eq
        (Get-U64 $d "hybrid_reflections_skinned_eligible_count") -and
    (Get-U64 $d "hybrid_reflections_skinned_fallback_count") -eq 0 -and
    (Get-Max $dynamic.rows "hybrid_reflections_skinned_dynamic_blas_update_count") -gt 0 -and
    (Get-Max $dynamic.rows "hybrid_reflections_skinned_skinning_dispatch_count") -gt 0 -and
    (Get-U64 $d "hybrid_reflections_skinned_pose_revision_min") -eq
        (Get-U64 $d "hybrid_reflections_skinned_output_revision_min") -and
    (Get-U64 $d "hybrid_reflections_skinned_output_revision_min") -eq
        (Get-U64 $d "hybrid_reflections_skinned_blas_revision_min") -and
    (Get-U64 $d "hybrid_reflections_skinned_pose_blas_revision_mismatch_count") -eq 0 -and
    (Get-U64 $d "hybrid_reflections_skinned_palette_snapshot_bytes") -gt 0 -and
    (Get-U64 $d "hybrid_reflections_skinned_skinning_readback_valid") -eq 1 -and
    (Get-U64 $d "hybrid_reflections_skinned_skinning_readback_vertex_count") -gt 0 -and
    (Get-U64 $d "hybrid_reflections_skinned_skinning_readback_vertex_count") -eq
        (Get-U64 $d "hybrid_reflections_skinned_skinning_readback_skinned_vertex_count") -and
    (Get-U64 $d "hybrid_reflections_skinned_skinning_readback_invalid_bone_index_count") -eq 0 -and
    (Get-U64 $d "hybrid_reflections_skinned_skinning_readback_non_finite_vertex_count") -eq 0
Add-Check "dynamic FBX pose, output, BLAS and TLAS contracts agree" $dynamicPass `
    "candidate/eligible/tlas=$($d.hybrid_reflections_skinned_candidate_count)/$($d.hybrid_reflections_skinned_eligible_count)/$($d.hybrid_reflections_skinned_tlas_instance_count),revision=$($d.hybrid_reflections_skinned_pose_revision_min)/$($d.hybrid_reflections_skinned_output_revision_min)/$($d.hybrid_reflections_skinned_blas_revision_min),vertices=$($d.hybrid_reflections_skinned_skinning_readback_skinned_vertex_count)/$($d.hybrid_reflections_skinned_skinning_readback_vertex_count)" `
    "candidate=eligible=tlas,pose=output=blas,all vertices skinned,invalid=0"

$f = $frozen.last
$frozenPass =
    (Get-U64 $f "hybrid_reflections_skinned_fallback_count") -eq 0 -and
    (Get-Max $frozen.rows "hybrid_reflections_skinned_dynamic_blas_build_count") -eq 0 -and
    (Get-Max $frozen.rows "hybrid_reflections_skinned_dynamic_blas_update_count") -eq 0 -and
    (Get-Max $frozen.rows "hybrid_reflections_skinned_skinning_dispatch_count") -eq 0 -and
    (Get-U64 $f "hybrid_reflections_skinned_pose_revision_min") -eq
        (Get-U64 $f "hybrid_reflections_skinned_output_revision_min") -and
    (Get-U64 $f "hybrid_reflections_skinned_output_revision_min") -eq
        (Get-U64 $f "hybrid_reflections_skinned_blas_revision_min")
Add-Check "frozen FBX reuses its completed dynamic BLAS" $frozenPass `
    "build/update/dispatch=$(Get-Max $frozen.rows 'hybrid_reflections_skinned_dynamic_blas_build_count')/$(Get-Max $frozen.rows 'hybrid_reflections_skinned_dynamic_blas_update_count')/$(Get-Max $frozen.rows 'hybrid_reflections_skinned_skinning_dispatch_count')" `
    "0/0/0 after warmup"

$o = $disabled.last
$disabledPass =
    (Get-U64 $o "hybrid_reflections_skinned_blas_control_disabled") -eq 1 -and
    (Get-U64 $o "hybrid_reflections_skinned_candidate_count") -gt 0 -and
    (Get-U64 $o "hybrid_reflections_skinned_fallback_count") -gt 0 -and
    (Get-U64 $o "hybrid_reflections_skinned_eligible_count") -eq 0 -and
    (Get-U64 $o "hybrid_reflections_skinned_dynamic_blas_count") -eq 0 -and
    (Get-U64 $o "hybrid_reflections_skinned_skinning_dispatch_count") -eq 0 -and
    (Get-U64 $o "hybrid_reflections_skinned_skinning_buffer_bytes") -eq 0 -and
    (Get-U64 $o "hybrid_reflections_skinned_palette_snapshot_bytes") -eq 0
Add-Check "skinned BLAS reverse control restores explicit fallback" `
    $disabledPass "fallback=$($o.hybrid_reflections_skinned_fallback_count)" `
    "fallback>0,dynamic resources=0"

$s = $static.last
$staticPass =
    (Get-U64 $s "hybrid_reflections_skinned_candidate_count") -eq 0 -and
    (Get-U64 $s "hybrid_reflections_skinned_dynamic_blas_count") -eq 0 -and
    (Get-U64 $s "hybrid_reflections_skinned_skinning_dispatch_count") -eq 0 -and
    (Get-U64 $s "hybrid_reflections_blas_cache_count") -gt 0 -and
    (Get-U64 $s "hybrid_reflections_blas_cache_count") -eq
        (Get-U64 $s "hybrid_reflections_blas_ready_count") -and
    (Get-U64 $s "hybrid_reflections_tlas_instance_count") -gt 0
Add-Check "static LightingShowcase remains on the static BLAS path" `
    $staticPass "static=$($s.hybrid_reflections_blas_ready_count),dynamic=$($s.hybrid_reflections_skinned_dynamic_blas_count)" `
    "static>0,dynamic=0"

$failed = @($checks | Where-Object status -eq "fail")
$report = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    passed = $checks.Count - $failed.Count
    failed = $failed.Count
    checks = $checks
}
$reportPath = Join-Path $OutputDirectory "skinned_blas_health.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "Hybrid reflection skinned BLAS: $($report.passed) pass / $($report.failed) fail"
Write-Host "Report: $reportPath"
if ($Strict -and $failed.Count -gt 0) {
    $failed | Format-Table -AutoSize | Out-Host
    exit 1
}
