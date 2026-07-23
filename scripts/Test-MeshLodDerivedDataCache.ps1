[CmdletBinding()]
param(
    [string]$PbrExecutablePath = "build\Debug\SelfEnginePbrModelShowcase.exe",
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$PbrAssetPath = "assets\models\lvjuren.glb",
    [string]$ObjAssetPath = "assets\models\articulated_links.obj",
    [string]$SkinnedAssetPath = "assets\models\Fist Fight B.fbx",
    [string]$OutputDirectory = "tmp\mesh_lod_ddc_health",
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
$pbrAsset = Resolve-ProjectPath $PbrAssetPath
$objAsset = Resolve-ProjectPath $ObjAssetPath
$skinnedAsset = Resolve-ProjectPath $SkinnedAssetPath
$output = Resolve-ProjectPath $OutputDirectory
$runName = "run-{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $PID
$runRoot = Join-Path $output $runName
$cacheRoot = Join-Path $runRoot "shared-cache"
$disabledCacheRoot = Join-Path $runRoot "disabled-cache"
$skinnedCacheRoot = Join-Path $runRoot "skinned-cache"
$reuseManifestPath = Join-Path $runRoot "lod-reuse-bridge.json"
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
New-Item -ItemType Directory -Force -Path $disabledCacheRoot | Out-Null
New-Item -ItemType Directory -Force -Path $skinnedCacheRoot | Out-Null

$reuseManifest = [ordered]@{
    schemaVersion = "selfengine.bridge.v0"
    scenes = @(
        [ordered]@{
            id = "lod-reuse"
            name = "LOD Reuse"
            meshInstances = @(
                [ordered]@{
                    id = "reuse-a"
                    exportedPath = $pbrAsset
                    exportStatus = "ready"
                    transform = [ordered]@{
                        position = @(-2.0, 0.0, 0.0)
                        rotationDegrees = @(0.0, 0.0, 0.0)
                        scale = @(1.0, 1.0, 1.0)
                    }
                },
                [ordered]@{
                    id = "reuse-b"
                    exportedPath = $pbrAsset
                    exportStatus = "ready"
                    transform = [ordered]@{
                        position = @(2.0, 0.0, 0.0)
                        rotationDegrees = @(0.0, 45.0, 0.0)
                        scale = @(1.0, 1.0, 1.0)
                    }
                }
            )
        }
    )
}
$reuseManifest | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $reuseManifestPath -Encoding UTF8

if (!$SkipBuild) {
    $buildCommand = @(
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1',
        "cd /d `"$projectRoot\build`"",
        'MSBuild SelfEnginePbrModelShowcase.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo',
        'MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo'
    ) -join ' && '
    & cmd.exe /d /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Mesh LOD DDC target build failed with exit code $LASTEXITCODE"
    }
}

foreach ($path in @(
    $pbrExecutable,
    $forwardExecutable,
    $pbrAsset,
    $objAsset,
    $skinnedAsset
)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Mesh LOD DDC input not found: $path"
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

function Get-Column($Row, [string]$Name) {
    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing benchmark CSV column: $Name"
    }
    return [string]$property.Value
}

function Get-U64($Row, [string]$Name) {
    return [uint64]::Parse(
        (Get-Column $Row $Name),
        [Globalization.CultureInfo]::InvariantCulture
    )
}

function Invoke-Lane(
    [string]$Name,
    [string]$Executable,
    [string]$ModelVariable,
    [string]$ModelPath,
    [string]$CacheDirectory,
    [bool]$CacheEnabled,
    [bool]$ForceRebuild = $false,
    [Collections.IDictionary]$AdditionalEnvironment = $null
) {
    $lane = Join-Path $runRoot $Name
    New-Item -ItemType Directory -Force -Path $lane | Out-Null
    $csv = Join-Path $lane "benchmark.csv"
    $stdout = Join-Path $lane "stdout.log"
    $stderr = Join-Path $lane "stderr.log"
    $environment = [ordered]@{
        SE_PBR_MODEL_SHOWCASE_PATH = $null
        SELFENGINE_MODEL_PATH = $null
        SE_BENCHMARK_SCENE = $null
        SE_FORWARD3D_DEBUG_DEFAULT_SCENE = "off"
        SE_MESH_LOD_CACHE = if ($CacheEnabled) { "1" } else { "0" }
        SE_MESH_LOD_CACHE_REBUILD = if ($ForceRebuild) { "1" } else { "0" }
        SE_MESH_LOD_CACHE_DIR = $CacheDirectory
        SE_MESH_LOD_CACHE_KEY_SALT = "mesh-lod-ddc-health-v1"
        SE_MESH_LOD = "1"
        SE_MESH_LOD_TARGET_PIXEL_ERROR = "1.0"
        SE_FORWARD3D_AA_MODE = "taa"
        SE_BENCHMARK_WARMUP_FRAMES = "2"
        SE_BENCHMARK_FRAMES = "2"
        SE_BENCHMARK_CSV = $csv
        SE_CAMERA_FREEZE = "1"
        SE_SCENE_UPDATE_FREEZE = "1"
        SE_HIDE_IMGUI = "1"
        SE_WINDOW_HIDDEN = "1"
        SE_WINDOW_WIDTH = "640"
        SE_WINDOW_HEIGHT = "360"
        SE_ENABLE_GPU_TIMESTAMPS = "0"
        SE_FAST_EXIT = "1"
        SE_CLEAN_SHUTDOWN = "0"
    }
    $environment[$ModelVariable] = $ModelPath
    if ($null -ne $AdditionalEnvironment) {
        foreach ($entry in $AdditionalEnvironment.GetEnumerator()) {
            $environment[$entry.Key] = $entry.Value
        }
    }

    $saved = @{}
    foreach ($entry in $environment.GetEnumerator()) {
        $saved[$entry.Key] = [Environment]::GetEnvironmentVariable(
            $entry.Key,
            "Process"
        )
        [Environment]::SetEnvironmentVariable(
            $entry.Key,
            $entry.Value,
            "Process"
        )
    }

    $wallClock = [Diagnostics.Stopwatch]::StartNew()
    try {
        $process = Start-Process `
            -FilePath $Executable `
            -WorkingDirectory $projectRoot `
            -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr `
            -WindowStyle Hidden `
            -Wait `
            -PassThru
    } finally {
        $wallClock.Stop()
        foreach ($entry in $environment.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable(
                $entry.Key,
                $saved[$entry.Key],
                "Process"
            )
        }
    }

    Add-Check "$Name exits cleanly" ($process.ExitCode -eq 0) $process.ExitCode 0
    $csvExists = Test-Path -LiteralPath $csv -PathType Leaf
    Add-Check "$Name writes benchmark CSV" $csvExists $csvExists $true
    if (!$csvExists) {
        throw "$Name did not write benchmark CSV. See $stdout and $stderr"
    }
    $rows = @(Import-Csv -LiteralPath $csv)
    Add-Check "$Name captures two rows" ($rows.Count -eq 2) $rows.Count 2
    if ($rows.Count -eq 0) {
        throw "$Name benchmark CSV has no data rows"
    }
    $lines = @(Get-Content -LiteralPath $csv -TotalCount 2)
    $columnsConserve = $lines.Count -eq 2 -and
        $lines[0].Split(',').Count -eq $lines[1].Split(',').Count
    Add-Check "$Name CSV columns conserve" $columnsConserve `
        $(if ($lines.Count -eq 2) {
            "$($lines[0].Split(',').Count)/$($lines[1].Split(',').Count)"
        } else {
            $lines.Count
        }) "equal"
    $diagnostics = @(
        Get-Content -LiteralPath $stdout, $stderr |
            Select-String `
                -Pattern "VUID|Vulkan Validation Error|unhandled exception|failed to load model" `
                -CaseSensitive:$false
    )
    Add-Check "$Name has no validation/runtime diagnostics" `
        ($diagnostics.Count -eq 0) $diagnostics.Count 0

    $row = $rows[-1]
    Add-Check "$Name loads the requested model" `
        ((Get-U64 $row "runtime_import_model_requested") -eq 1 -and
            (Get-U64 $row "runtime_import_model_loaded") -eq 1 -and
            (Get-U64 $row "runtime_import_source_triangle_count") -gt 0) `
        "requested/loaded/triangles=$(Get-Column $row 'runtime_import_model_requested')/$(Get-Column $row 'runtime_import_model_loaded')/$(Get-Column $row 'runtime_import_source_triangle_count')" `
        "1/1/>0"

    return [pscustomobject]@{
        name = $Name
        row = $row
        rows = $rows
        wallMilliseconds = [Math]::Round($wallClock.Elapsed.TotalMilliseconds, 3)
        csv = $csv
        stdout = $stdout
        stderr = $stderr
    }
}

function Add-ConservationChecks([string]$Name, $Left, $Right) {
    $columns = @(
        "runtime_import_source_vertex_count",
        "runtime_import_source_triangle_count",
        "runtime_import_lod_cache_source_hash",
        "runtime_import_lod_cache_settings_hash",
        "runtime_import_lod_cache_key_hash",
        "runtime_import_lod_cache_raw_bytes",
        "runtime_import_lod_cache_encoded_bytes",
        "runtime_import_lod_cache_file_bytes",
        "runtime_import_lod_cache_decoded_chain_count",
        "runtime_import_lod_cache_decoded_level_count",
        "mesh_lod_resident_chain_count",
        "mesh_lod_resident_level_count",
        "mesh_lod_resident_vertex_bytes",
        "mesh_lod_resident_index_bytes"
    )
    foreach ($column in $columns) {
        $leftValue = Get-Column $Left.row $column
        $rightValue = Get-Column $Right.row $column
        Add-Check "$Name conserves $column" `
            ($leftValue -eq $rightValue) "$leftValue/$rightValue" "equal"
    }
}

$pbrCold = Invoke-Lane `
    "pbr-glb-cold" $pbrExecutable "SE_PBR_MODEL_SHOWCASE_PATH" `
    $pbrAsset $cacheRoot $true
$cold = $pbrCold.row
Add-Check "cold GLB misses and publishes one cache" `
    ((Get-U64 $cold "runtime_import_lod_cache_requested") -eq 1 -and
        (Get-U64 $cold "runtime_import_lod_cache_hit") -eq 0 -and
        (Get-U64 $cold "runtime_import_lod_cache_miss") -eq 1 -and
        (Get-U64 $cold "runtime_import_lod_cache_rejected") -eq 0 -and
        (Get-U64 $cold "runtime_import_lod_cache_written") -eq 1 -and
        (Get-U64 $cold "runtime_import_lod_cache_fallback_reason") -eq 5) `
    "requested/hit/miss/rejected/written/reason=$(Get-Column $cold 'runtime_import_lod_cache_requested')/$(Get-Column $cold 'runtime_import_lod_cache_hit')/$(Get-Column $cold 'runtime_import_lod_cache_miss')/$(Get-Column $cold 'runtime_import_lod_cache_rejected')/$(Get-Column $cold 'runtime_import_lod_cache_written')/$(Get-Column $cold 'runtime_import_lod_cache_fallback_reason')" `
    "1/0/1/0/1/5"
Add-Check "cold GLB records nonzero build and write work" `
    ((Get-U64 $cold "runtime_import_lod_cache_build_microseconds") -gt 0 -and
        (Get-U64 $cold "runtime_import_lod_cache_write_microseconds") -gt 0) `
    "build/write=$(Get-Column $cold 'runtime_import_lod_cache_build_microseconds')/$(Get-Column $cold 'runtime_import_lod_cache_write_microseconds') us" `
    ">0/>0"

$pbrCacheFiles = @(Get-ChildItem -LiteralPath $cacheRoot -Filter *.selod -File -Recurse)
Add-Check "cold GLB publishes exactly one cache file" `
    ($pbrCacheFiles.Count -eq 1) $pbrCacheFiles.Count 1
if ($pbrCacheFiles.Count -ne 1) {
    throw "Expected exactly one GLB cache file before warm validation"
}

$pbrWarm = Invoke-Lane `
    "pbr-glb-warm" $pbrExecutable "SE_PBR_MODEL_SHOWCASE_PATH" `
    $pbrAsset $cacheRoot $true
$warm = $pbrWarm.row
Add-Check "warm GLB uses the cross-process cache" `
    ((Get-U64 $warm "runtime_import_lod_cache_requested") -eq 1 -and
        (Get-U64 $warm "runtime_import_lod_cache_hit") -eq 1 -and
        (Get-U64 $warm "runtime_import_lod_cache_miss") -eq 0 -and
        (Get-U64 $warm "runtime_import_lod_cache_rejected") -eq 0 -and
        (Get-U64 $warm "runtime_import_lod_cache_written") -eq 0 -and
        (Get-U64 $warm "runtime_import_lod_cache_fallback_reason") -eq 0) `
    "requested/hit/miss/rejected/written/reason=$(Get-Column $warm 'runtime_import_lod_cache_requested')/$(Get-Column $warm 'runtime_import_lod_cache_hit')/$(Get-Column $warm 'runtime_import_lod_cache_miss')/$(Get-Column $warm 'runtime_import_lod_cache_rejected')/$(Get-Column $warm 'runtime_import_lod_cache_written')/$(Get-Column $warm 'runtime_import_lod_cache_fallback_reason')" `
    "1/1/0/0/0/0"
Add-Check "warm GLB replaces LOD build with read and decode" `
    ((Get-U64 $warm "runtime_import_lod_cache_build_microseconds") -eq 0 -and
        (Get-U64 $warm "runtime_import_lod_cache_read_microseconds") -gt 0 -and
        (Get-U64 $warm "runtime_import_lod_cache_decode_microseconds") -gt 0) `
    "build/read/decode=$(Get-Column $warm 'runtime_import_lod_cache_build_microseconds')/$(Get-Column $warm 'runtime_import_lod_cache_read_microseconds')/$(Get-Column $warm 'runtime_import_lod_cache_decode_microseconds') us" `
    "0/>0/>0"
Add-Check "warm GLB decode is faster than cold LOD generation" `
    ((Get-U64 $warm "runtime_import_lod_cache_decode_microseconds") -lt
        (Get-U64 $cold "runtime_import_lod_cache_build_microseconds")) `
    "decode/build=$(Get-Column $warm 'runtime_import_lod_cache_decode_microseconds')/$(Get-Column $cold 'runtime_import_lod_cache_build_microseconds') us" `
    "decode<build"
Add-Check "meshoptimizer payload is smaller than raw LOD data" `
    ((Get-U64 $warm "runtime_import_lod_cache_encoded_bytes") -gt 0 -and
        (Get-U64 $warm "runtime_import_lod_cache_encoded_bytes") -lt
            (Get-U64 $warm "runtime_import_lod_cache_raw_bytes")) `
    "encoded/raw=$(Get-Column $warm 'runtime_import_lod_cache_encoded_bytes')/$(Get-Column $warm 'runtime_import_lod_cache_raw_bytes')" `
    "0<encoded<raw"
Add-ConservationChecks "cold/warm GLB" $pbrCold $pbrWarm

$pbrForced = Invoke-Lane `
    "pbr-glb-forced-rebuild" $pbrExecutable "SE_PBR_MODEL_SHOWCASE_PATH" `
    $pbrAsset $cacheRoot $true $true
$forced = $pbrForced.row
Add-Check "forced GLB rebuild bypasses and replaces the valid cache" `
    ((Get-U64 $forced "runtime_import_lod_cache_hit") -eq 0 -and
        (Get-U64 $forced "runtime_import_lod_cache_miss") -eq 1 -and
        (Get-U64 $forced "runtime_import_lod_cache_rejected") -eq 0 -and
        (Get-U64 $forced "runtime_import_lod_cache_written") -eq 1 -and
        (Get-U64 $forced "runtime_import_lod_cache_fallback_reason") -eq 2 -and
        (Get-U64 $forced "runtime_import_lod_cache_build_microseconds") -gt 0) `
    "hit/miss/rejected/written/reason/build=$(Get-Column $forced 'runtime_import_lod_cache_hit')/$(Get-Column $forced 'runtime_import_lod_cache_miss')/$(Get-Column $forced 'runtime_import_lod_cache_rejected')/$(Get-Column $forced 'runtime_import_lod_cache_written')/$(Get-Column $forced 'runtime_import_lod_cache_fallback_reason')/$(Get-Column $forced 'runtime_import_lod_cache_build_microseconds')" `
    "0/1/0/1/2/>0"
Add-ConservationChecks "warm/forced GLB" $pbrWarm $pbrForced

[IO.File]::WriteAllBytes(
    $pbrCacheFiles[0].FullName,
    [byte[]](0x53, 0x45, 0x2d, 0x43, 0x4f, 0x52, 0x52, 0x55, 0x50, 0x54)
)
$pbrCorrupt = Invoke-Lane `
    "pbr-glb-corrupt-recovery" $pbrExecutable "SE_PBR_MODEL_SHOWCASE_PATH" `
    $pbrAsset $cacheRoot $true
$corrupt = $pbrCorrupt.row
Add-Check "corrupt GLB cache is rejected and rebuilt" `
    ((Get-U64 $corrupt "runtime_import_lod_cache_hit") -eq 0 -and
        (Get-U64 $corrupt "runtime_import_lod_cache_miss") -eq 1 -and
        (Get-U64 $corrupt "runtime_import_lod_cache_rejected") -eq 1 -and
        (Get-U64 $corrupt "runtime_import_lod_cache_written") -eq 1 -and
        (Get-U64 $corrupt "runtime_import_lod_cache_fallback_reason") -eq 7 -and
        (Get-U64 $corrupt "runtime_import_lod_cache_build_microseconds") -gt 0) `
    "hit/miss/rejected/written/reason/build=$(Get-Column $corrupt 'runtime_import_lod_cache_hit')/$(Get-Column $corrupt 'runtime_import_lod_cache_miss')/$(Get-Column $corrupt 'runtime_import_lod_cache_rejected')/$(Get-Column $corrupt 'runtime_import_lod_cache_written')/$(Get-Column $corrupt 'runtime_import_lod_cache_fallback_reason')/$(Get-Column $corrupt 'runtime_import_lod_cache_build_microseconds')" `
    "0/1/1/1/7/>0"

$pbrRecovered = Invoke-Lane `
    "pbr-glb-recovered-warm" $pbrExecutable "SE_PBR_MODEL_SHOWCASE_PATH" `
    $pbrAsset $cacheRoot $true
$recovered = $pbrRecovered.row
Add-Check "rebuilt GLB cache is valid on the next process" `
    ((Get-U64 $recovered "runtime_import_lod_cache_hit") -eq 1 -and
        (Get-U64 $recovered "runtime_import_lod_cache_rejected") -eq 0 -and
        (Get-U64 $recovered "runtime_import_lod_cache_build_microseconds") -eq 0) `
    "hit/rejected/build=$(Get-Column $recovered 'runtime_import_lod_cache_hit')/$(Get-Column $recovered 'runtime_import_lod_cache_rejected')/$(Get-Column $recovered 'runtime_import_lod_cache_build_microseconds')" `
    "1/0/0"
Add-ConservationChecks "warm/recovered GLB" $pbrWarm $pbrRecovered

$pbrDisabled = Invoke-Lane `
    "pbr-glb-cache-disabled" $pbrExecutable "SE_PBR_MODEL_SHOWCASE_PATH" `
    $pbrAsset $disabledCacheRoot $false
$disabled = $pbrDisabled.row
$disabledFiles = @(Get-ChildItem -LiteralPath $disabledCacheRoot -Filter *.selod -File -Recurse)
Add-Check "disabled GLB cache bypasses disk but preserves LOD generation" `
    ((Get-U64 $disabled "runtime_import_lod_cache_requested") -eq 0 -and
        (Get-U64 $disabled "runtime_import_lod_cache_hit") -eq 0 -and
        (Get-U64 $disabled "runtime_import_lod_cache_miss") -eq 0 -and
        (Get-U64 $disabled "runtime_import_lod_cache_written") -eq 0 -and
        (Get-U64 $disabled "runtime_import_lod_cache_fallback_reason") -eq 1 -and
        (Get-U64 $disabled "runtime_import_lod_cache_build_microseconds") -gt 0 -and
        $disabledFiles.Count -eq 0) `
    "requested/hit/miss/written/reason/build/files=$(Get-Column $disabled 'runtime_import_lod_cache_requested')/$(Get-Column $disabled 'runtime_import_lod_cache_hit')/$(Get-Column $disabled 'runtime_import_lod_cache_miss')/$(Get-Column $disabled 'runtime_import_lod_cache_written')/$(Get-Column $disabled 'runtime_import_lod_cache_fallback_reason')/$(Get-Column $disabled 'runtime_import_lod_cache_build_microseconds')/$($disabledFiles.Count)" `
    "0/0/0/0/1/>0/0"
Add-Check "disabled GLB path preserves source and resident geometry" `
    ((Get-Column $disabled "runtime_import_source_triangle_count") -eq
        (Get-Column $cold "runtime_import_source_triangle_count") -and
        (Get-Column $disabled "mesh_lod_resident_level_count") -eq
            (Get-Column $cold "mesh_lod_resident_level_count") -and
        (Get-Column $disabled "mesh_lod_resident_index_bytes") -eq
            (Get-Column $cold "mesh_lod_resident_index_bytes")) `
    "triangles/levels/index-bytes conserved" "equal to cold"

$objCold = Invoke-Lane `
    "forward-obj-cold" $forwardExecutable "SELFENGINE_MODEL_PATH" `
    $objAsset $cacheRoot $true
$objWarm = Invoke-Lane `
    "forward-obj-warm" $forwardExecutable "SELFENGINE_MODEL_PATH" `
    $objAsset $cacheRoot $true
Add-Check "OBJ control supports cold write and cross-process hit" `
    ((Get-U64 $objCold.row "runtime_import_lod_cache_miss") -eq 1 -and
        (Get-U64 $objCold.row "runtime_import_lod_cache_written") -eq 1 -and
        (Get-U64 $objWarm.row "runtime_import_lod_cache_hit") -eq 1 -and
        (Get-U64 $objWarm.row "runtime_import_lod_cache_build_microseconds") -eq 0) `
    "cold-miss/write/warm-hit/build=$(Get-Column $objCold.row 'runtime_import_lod_cache_miss')/$(Get-Column $objCold.row 'runtime_import_lod_cache_written')/$(Get-Column $objWarm.row 'runtime_import_lod_cache_hit')/$(Get-Column $objWarm.row 'runtime_import_lod_cache_build_microseconds')" `
    "1/1/1/0"
Add-Check "GLB and OBJ resolve distinct source/cache identities" `
    ((Get-Column $warm "runtime_import_lod_cache_source_hash") -ne
        (Get-Column $objWarm.row "runtime_import_lod_cache_source_hash") -and
        (Get-Column $warm "runtime_import_lod_cache_key_hash") -ne
            (Get-Column $objWarm.row "runtime_import_lod_cache_key_hash")) `
    "source=$(Get-Column $warm 'runtime_import_lod_cache_source_hash')/$(Get-Column $objWarm.row 'runtime_import_lod_cache_source_hash'); key=$(Get-Column $warm 'runtime_import_lod_cache_key_hash')/$(Get-Column $objWarm.row 'runtime_import_lod_cache_key_hash')" `
    "distinct"
Add-ConservationChecks "cold/warm OBJ" $objCold $objWarm

$reuseEnvironment = [ordered]@{
    SE_UE_BRIDGE_MANIFEST = $reuseManifestPath
    SE_UE_BRIDGE_SCENE = "lod-reuse"
    SE_LOAD_UE_BRIDGE_FIRST_SCENE = "1"
}
$objReuse = Invoke-Lane `
    "forward-obj-in-process-reuse" $forwardExecutable "SELFENGINE_MODEL_PATH" `
    $objAsset $cacheRoot $true $false $reuseEnvironment
Add-Check "same-process repeated GLB instances reuse one GPU LOD chain" `
    ((Get-U64 $objReuse.row "ue_bridge_mesh_instance_loaded_count") -eq 2 -and
        (Get-U64 $objReuse.row "mesh_lod_eligible_commands") -eq 2 -and
        (Get-U64 $objReuse.row "mesh_lod_selected_commands") -eq 2 -and
        (Get-U64 $objReuse.row "mesh_lod_resident_chain_count") -eq 1) `
    "bridge/eligible/selected/resident=$(Get-Column $objReuse.row 'ue_bridge_mesh_instance_loaded_count')/$(Get-Column $objReuse.row 'mesh_lod_eligible_commands')/$(Get-Column $objReuse.row 'mesh_lod_selected_commands')/$(Get-Column $objReuse.row 'mesh_lod_resident_chain_count')" `
    "2/2/2/1"

$skinned = Invoke-Lane `
    "forward-skinned-fbx" $forwardExecutable "SELFENGINE_MODEL_PATH" `
    $skinnedAsset $skinnedCacheRoot $true
$skinnedFiles = @(Get-ChildItem -LiteralPath $skinnedCacheRoot -Filter *.selod -File -Recurse)
Add-Check "fully skinned FBX takes the explicit no-cacheable-mesh fallback" `
    ((Get-U64 $skinned.row "runtime_import_lod_cache_requested") -eq 0 -and
        (Get-U64 $skinned.row "runtime_import_lod_cacheable_mesh_count") -eq 0 -and
        (Get-U64 $skinned.row "runtime_import_lod_cache_fallback_reason") -eq 3 -and
        (Get-U64 $skinned.row "runtime_import_lod_cache_hit") -eq 0 -and
        (Get-U64 $skinned.row "runtime_import_lod_cache_written") -eq 0 -and
        $skinnedFiles.Count -eq 0) `
    "requested/cacheable/reason/hit/written/files=$(Get-Column $skinned.row 'runtime_import_lod_cache_requested')/$(Get-Column $skinned.row 'runtime_import_lod_cacheable_mesh_count')/$(Get-Column $skinned.row 'runtime_import_lod_cache_fallback_reason')/$(Get-Column $skinned.row 'runtime_import_lod_cache_hit')/$(Get-Column $skinned.row 'runtime_import_lod_cache_written')/$($skinnedFiles.Count)" `
    "0/0/3/0/0/0"
Add-Check "skinned FBX source remains intact without a static LOD chain" `
    ((Get-U64 $skinned.row "runtime_import_mesh_with_bones_count") -gt 0 -and
        (Get-U64 $skinned.row "runtime_import_skinned_vertex_attribute_ready") -eq 1 -and
        (Get-U64 $skinned.row "runtime_import_source_triangle_count") -gt 0 -and
        (Get-U64 $skinned.row "mesh_lod_resident_chain_count") -eq 0) `
    "bone-meshes/attributes/triangles/lod-chains=$(Get-Column $skinned.row 'runtime_import_mesh_with_bones_count')/$(Get-Column $skinned.row 'runtime_import_skinned_vertex_attribute_ready')/$(Get-Column $skinned.row 'runtime_import_source_triangle_count')/$(Get-Column $skinned.row 'mesh_lod_resident_chain_count')" `
    ">0/1/>0/0"

$failed = @($checks | Where-Object status -eq "fail")
$rawBytes = [double](Get-U64 $warm "runtime_import_lod_cache_raw_bytes")
$encodedBytes = [double](Get-U64 $warm "runtime_import_lod_cache_encoded_bytes")
$health = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    meshoptimizerVersion = "v1.2"
    cacheSchema = 2
    runRoot = $runRoot
    assets = [ordered]@{
        pbrGlb = $pbrAsset
        staticObj = $objAsset
        skinnedFbx = $skinnedAsset
    }
    passed = $checks.Count - $failed.Count
    failed = $failed.Count
    metrics = [ordered]@{
        glbSourceTriangles = Get-U64 $warm "runtime_import_source_triangle_count"
        glbDecodedLevels = Get-U64 $warm "runtime_import_lod_cache_decoded_level_count"
        glbRawBytes = [uint64]$rawBytes
        glbEncodedBytes = [uint64]$encodedBytes
        glbCompressionRatio = if ($rawBytes -gt 0.0) {
            [Math]::Round($encodedBytes / $rawBytes, 4)
        } else {
            0.0
        }
        glbColdBuildMicroseconds = Get-U64 $cold "runtime_import_lod_cache_build_microseconds"
        glbColdWriteMicroseconds = Get-U64 $cold "runtime_import_lod_cache_write_microseconds"
        glbWarmReadMicroseconds = Get-U64 $warm "runtime_import_lod_cache_read_microseconds"
        glbWarmDecodeMicroseconds = Get-U64 $warm "runtime_import_lod_cache_decode_microseconds"
        glbColdTotalLoadMicroseconds = Get-U64 $cold "runtime_import_lod_cache_total_load_microseconds"
        glbWarmTotalLoadMicroseconds = Get-U64 $warm "runtime_import_lod_cache_total_load_microseconds"
        glbColdWallMilliseconds = $pbrCold.wallMilliseconds
        glbWarmWallMilliseconds = $pbrWarm.wallMilliseconds
        cacheFileCount = @(Get-ChildItem -LiteralPath $cacheRoot -Filter *.selod -File -Recurse).Count
    }
    lanes = @(
        $pbrCold,
        $pbrWarm,
        $pbrForced,
        $pbrCorrupt,
        $pbrRecovered,
        $pbrDisabled,
        $objCold,
        $objWarm,
        $objReuse,
        $skinned
    ) | ForEach-Object {
        [ordered]@{
            name = $_.name
            wallMilliseconds = $_.wallMilliseconds
            csv = $_.csv
        }
    }
    checks = $checks
}
$healthPath = Join-Path $runRoot "mesh_lod_ddc_health.json"
$health | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $healthPath -Encoding UTF8
Write-Host "Mesh LOD DDC health: $($health.passed) pass / $($health.failed) fail"
Write-Host "GLB cache bytes: $($health.metrics.glbEncodedBytes) encoded / $($health.metrics.glbRawBytes) raw (ratio $($health.metrics.glbCompressionRatio))"
Write-Host "GLB cold build/write: $($health.metrics.glbColdBuildMicroseconds)/$($health.metrics.glbColdWriteMicroseconds) us"
Write-Host "GLB warm read/decode: $($health.metrics.glbWarmReadMicroseconds)/$($health.metrics.glbWarmDecodeMicroseconds) us"
Write-Host "GLB total load cold/warm: $($health.metrics.glbColdTotalLoadMicroseconds)/$($health.metrics.glbWarmTotalLoadMicroseconds) us"
Write-Host "Report: $healthPath"
if ($Strict -and $failed.Count -gt 0) {
    $failed | Format-Table -AutoSize | Out-Host
    exit 1
}
