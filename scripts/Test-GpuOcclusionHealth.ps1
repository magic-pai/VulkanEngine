[CmdletBinding()]
param(
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$ShowcaseExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [string]$ReleaseShowcaseExecutablePath = "build\Release\SelfEngineLightingShowcase.exe",
    [switch]$SkipBuild,
    [switch]$Strict,
    [string]$OutputDirectory = "tmp\gpu_occlusion_health"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (![IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Resolve-ProjectPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $candidate = if ([IO.Path]::IsPathRooted($Path)) {
        $Path
    } else {
        Join-Path $projectRoot $Path
    }
    return (Resolve-Path $candidate).Path
}

function New-Check {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Passed,
        [Parameter(Mandatory = $true)][object]$Actual,
        [Parameter(Mandatory = $true)][object]$Expected
    )

    return [pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }
}

function Get-UInt32 {
    param($Row, [Parameter(Mandatory = $true)][string]$Name)

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV column: $Name"
    }
    return [Convert]::ToUInt32($property.Value)
}

function Get-UInt64 {
    param($Row, [Parameter(Mandatory = $true)][string]$Name)

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV column: $Name"
    }
    return [Convert]::ToUInt64($property.Value)
}

function Get-Double {
    param($Row, [Parameter(Mandatory = $true)][string]$Name)

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV column: $Name"
    }
    return [Convert]::ToDouble(
        $property.Value,
        [Globalization.CultureInfo]::InvariantCulture
    )
}

function Set-LaneEnvironment {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Values,
        [Parameter(Mandatory = $true)][string[]]$ManagedKeys
    )

    $previous = @{}
    foreach ($key in $ManagedKeys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, $null, "Process")
    }
    foreach ($entry in $Values.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable(
            $entry.Key,
            [string]$entry.Value,
            "Process"
        )
    }
    return $previous
}

function Restore-LaneEnvironment {
    param([Parameter(Mandatory = $true)][hashtable]$Previous)

    foreach ($entry in $Previous.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable(
            $entry.Key,
            $entry.Value,
            "Process"
        )
    }
}

function Get-ExpectedMipCount {
    param(
        [Parameter(Mandatory = $true)][uint32]$Width,
        [Parameter(Mandatory = $true)][uint32]$Height
    )

    [uint32]$extent = [Math]::Max($Width, $Height)
    [uint32]$mipCount = 0
    while ($extent -gt 0) {
        ++$mipCount
        $extent = [uint32]([Math]::Floor($extent / 2.0))
    }
    return $mipCount
}

if (!$SkipBuild) {
    $cmake =
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    & $cmake --build (Join-Path $projectRoot "build") --config Debug `
        --target SelfEngineShaders SelfEngineForward3D SelfEngineLightingShowcase `
        -- /m:1 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) {
        throw "GPU occlusion Debug build failed with exit code $LASTEXITCODE"
    }
    & $cmake --build (Join-Path $projectRoot "build") --config Release `
        --target SelfEngineLightingShowcase -- /m:1 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) {
        throw "GPU occlusion Release build failed with exit code $LASTEXITCODE"
    }
}

$forwardExecutable = Resolve-ProjectPath $ForwardExecutablePath
$showcaseExecutable = Resolve-ProjectPath $ShowcaseExecutablePath
$releaseShowcaseExecutable = Resolve-ProjectPath $ReleaseShowcaseExecutablePath

$managedKeys = @(
    "SE_GPU_OCCLUSION",
    "SE_GPU_OCCLUSION_DIAGNOSTICS_OFF",
    "SE_GPU_OCCLUSION_DEPTH_EPSILON",
    "SE_FORWARD3D_AA_MODE",
    "SE_NATIVE_TAA_JITTER_SCALE",
    "SE_SSR",
    "SE_SSR_BACKEND",
    "SE_HYBRID_REFLECTIONS_RT",
    "SE_HYBRID_REFLECTIONS_RT_OFF",
    "SE_HYBRID_REFLECTIONS_DIAGNOSTICS",
    "SE_BENCHMARK_SCENE",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_LIGHTING_SHOWCASE_FORCE_OFF",
    "SE_BENCHMARK_CAMERA_MOTION",
    "SE_BENCHMARK_CAMERA_MOTION_SPEED",
    "SE_BENCHMARK_CAMERA_MOTION_YAW",
    "SE_BENCHMARK_CAMERA_MOTION_PITCH",
    "SE_BENCHMARK_CAMERA_MOTION_DISTANCE",
    "SE_SCENE_UPDATE_FREEZE",
    "SE_FBX_ANIMATION_FREEZE",
    "SE_VISUAL_QA_HIDE_IMGUI",
    "SE_WINDOW_HIDDEN",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_BENCHMARK_CSV",
    "SE_AUTO_EXIT_FRAMES"
)

$commonEnvironment = @{
    SE_FORWARD3D_AA_MODE = "taa"
    SE_SSR = "0"
    SE_SSR_BACKEND = "selfengine"
    SE_HYBRID_REFLECTIONS_RT = "0"
    SE_BENCHMARK_CAMERA_MOTION = "static"
    SE_SCENE_UPDATE_FREEZE = "1"
    SE_FBX_ANIMATION_FREEZE = "1"
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_WINDOW_HIDDEN = "1"
    SE_BENCHMARK_WARMUP_FRAMES = "8"
    SE_BENCHMARK_FRAMES = "6"
    SE_AUTO_EXIT_FRAMES = "16"
}

$laneSpecs = @(
    [pscustomobject]@{
        name = "lighting-showcase-native-taa-jitter"
        executable = $showcaseExecutable
        mode = "consume-jitter"
        expectOccluded = $false
        expectedAaMode = 1
        expectJitter = $true
        expectedJitterGuard = 0.5
        environment = @{
            SE_GPU_OCCLUSION = "1"
            SE_BENCHMARK_SCENE = "lighting-showcase"
        }
    },
    [pscustomobject]@{
        name = "forward3d-fbx-native-taa-jitter"
        executable = $forwardExecutable
        mode = "consume-jitter"
        expectOccluded = $false
        expectedAaMode = 1
        expectJitter = $true
        expectedJitterGuard = 0.5
        environment = @{
            SE_GPU_OCCLUSION = "1"
            SE_FORWARD3D_AA_MODE = "taa"
            SE_BENCHMARK_SCENE = "shadow-regression"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
            SE_LIGHTING_SHOWCASE_FORCE_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-consume-static"
        executable = $showcaseExecutable
        mode = "consume-static"
        expectOccluded = $true
        expectedAaMode = 1
        expectJitter = $false
        expectedJitterGuard = 0.0
        environment = @{
            SE_GPU_OCCLUSION = "1"
            SE_FORWARD3D_AA_MODE = "taa"
            SE_NATIVE_TAA_JITTER_SCALE = "0"
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_BENCHMARK_CAMERA_MOTION = "orbit"
            SE_BENCHMARK_CAMERA_MOTION_SPEED = "0"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-dlss-performance-jitter"
        executable = $showcaseExecutable
        mode = "consume-jitter"
        expectOccluded = $true
        expectedAaMode = 5
        expectJitter = $true
        expectedJitterGuard = 1.0
        environment = @{
            SE_GPU_OCCLUSION = "1"
            SE_FORWARD3D_AA_MODE = "sr-performance"
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_BENCHMARK_CAMERA_MOTION = "orbit"
            SE_BENCHMARK_CAMERA_MOTION_SPEED = "0"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-camera-motion"
        executable = $showcaseExecutable
        mode = "audit-motion"
        expectOccluded = $true
        environment = @{
            SE_GPU_OCCLUSION = "1"
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_BENCHMARK_CAMERA_MOTION = "orbit"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-disabled"
        executable = $showcaseExecutable
        mode = "disabled"
        expectOccluded = $false
        environment = @{
            SE_GPU_OCCLUSION = "0"
            SE_BENCHMARK_SCENE = "lighting-showcase"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-release-requested-control"
        executable = $releaseShowcaseExecutable
        mode = "release-consume"
        expectOccluded = $false
        expectedAaMode = 1
        expectJitter = $true
        expectedJitterGuard = 0.5
        environment = @{
            SE_GPU_OCCLUSION = "1"
            SE_FORWARD3D_AA_MODE = "taa"
            SE_BENCHMARK_SCENE = "lighting-showcase"
            SE_BENCHMARK_CAMERA_MOTION = "orbit"
            SE_BENCHMARK_CAMERA_MOTION_SPEED = "0"
        }
    }
)

$reports = [Collections.Generic.List[object]]::new()
foreach ($lane in $laneSpecs) {
    $laneDirectory = Join-Path $OutputDirectory $lane.name
    New-Item -ItemType Directory -Force -Path $laneDirectory | Out-Null
    $csvPath = Join-Path $laneDirectory "benchmark.csv"
    $stdoutPath = Join-Path $laneDirectory "process.stdout.log"
    $stderrPath = Join-Path $laneDirectory "process.stderr.log"
    Remove-Item -LiteralPath $csvPath, $stdoutPath, $stderrPath `
        -Force -ErrorAction SilentlyContinue

    $environment = $commonEnvironment.Clone()
    foreach ($entry in $lane.environment.GetEnumerator()) {
        $environment[$entry.Key] = $entry.Value
    }
    $environment["SE_BENCHMARK_CSV"] = $csvPath

    $previous = Set-LaneEnvironment `
        -Values $environment `
        -ManagedKeys $managedKeys
    try {
        $executableDirectory = Split-Path -Parent $lane.executable
        $process = Start-Process `
            -FilePath $lane.executable `
            -WorkingDirectory $executableDirectory `
            -PassThru `
            -Wait `
            -WindowStyle Hidden `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath
        $exitCode = $process.ExitCode
    } finally {
        Restore-LaneEnvironment -Previous $previous
    }

    $checks = [Collections.Generic.List[object]]::new()
    $checks.Add((New-Check "$($lane.name) process exits" `
        ($exitCode -eq 0) $exitCode 0)) | Out-Null
    $csvExists = Test-Path -LiteralPath $csvPath
    $checks.Add((New-Check "$($lane.name) writes benchmark CSV" `
        $csvExists $csvExists $true)) | Out-Null

    $metrics = $null
    if ($exitCode -eq 0 -and $csvExists) {
        $rows = @(Import-Csv -LiteralPath $csvPath)
        $checks.Add((New-Check "$($lane.name) captures steady rows" `
            ($rows.Count -eq 6) $rows.Count 6)) | Out-Null

        if ($rows.Count -gt 0) {
            $last = $rows[-1]
            $contract = Get-UInt32 $last "gpu_occlusion_contract_version"
            $requested = Get-UInt32 $last "gpu_occlusion_requested"
            $diagnosticsRequested = Get-UInt32 $last `
                "gpu_occlusion_diagnostics_requested"
            $active = Get-UInt32 $last "gpu_occlusion_active"
            $fallback = Get-UInt32 $last "gpu_occlusion_fallback_reason"
            $drawsUnchanged = Get-UInt32 $last `
                "gpu_occlusion_actual_draws_unchanged"
            $actualDrawCount = Get-UInt32 $last `
                "gpu_occlusion_actual_draw_count"
            $mainDrawCount = Get-UInt32 $last "main_draws"
            $frameGraphIssues = Get-UInt32 $last `
                "framegraph_validation_issues"

            $checks.Add((New-Check "$($lane.name) contract version" `
                ($contract -eq 3) $contract 3)) | Out-Null
            $checks.Add((New-Check "$($lane.name) frame graph is valid" `
                ($frameGraphIssues -eq 0) $frameGraphIssues 0)) | Out-Null
            $checks.Add((New-Check "$($lane.name) draw accounting matches main queue" `
                ($actualDrawCount -eq $mainDrawCount -and $actualDrawCount -gt 0) `
                "$actualDrawCount/$mainDrawCount" "actual=main>0")) | Out-Null

            if ($lane.mode -in @("consume-jitter", "audit-motion", "consume-static")) {
                $commandCount = Get-UInt32 $last "gpu_occlusion_command_count"
                $validBounds = Get-UInt32 $last `
                    "gpu_occlusion_valid_bounds_count"
                $invalidBounds = Get-UInt32 $last `
                    "gpu_occlusion_invalid_bounds_count"
                $zeroIdentity = Get-UInt32 $last `
                    "gpu_occlusion_zero_identity_count"
                $capacity = Get-UInt32 $last "gpu_occlusion_capacity"
                $capacityDropped = Get-UInt32 $last `
                    "gpu_occlusion_capacity_dropped_count"
                $uploaded = Get-UInt32 $last `
                    "gpu_occlusion_uploaded_candidate_count"
                $uploadedBytes = Get-UInt64 $last `
                    "gpu_occlusion_uploaded_candidate_bytes"
                $identityHash = Get-UInt64 $last `
                    "gpu_occlusion_candidate_identity_hash"
                $contentHash = Get-UInt64 $last `
                    "gpu_occlusion_candidate_content_hash"
                $classificationJitterX = Get-Double $last `
                    "gpu_occlusion_classification_jitter_pixels_x"
                $classificationJitterY = Get-Double $last `
                    "gpu_occlusion_classification_jitter_pixels_y"
                $classificationJitterGuard = Get-Double $last `
                    "gpu_occlusion_classification_jitter_guard_pixels"
                $pyramidAllocated = Get-UInt32 $last `
                    "gpu_occlusion_depth_pyramid_allocated"
                $pyramidWidth = Get-UInt32 $last `
                    "gpu_occlusion_depth_pyramid_width"
                $pyramidHeight = Get-UInt32 $last `
                    "gpu_occlusion_depth_pyramid_height"
                $pyramidMipCount = Get-UInt32 $last `
                    "gpu_occlusion_depth_pyramid_mip_count"
                $pyramidImageCount = Get-UInt32 $last `
                    "gpu_occlusion_depth_pyramid_image_count"
                $pyramidFormat = Get-UInt32 $last `
                    "gpu_occlusion_depth_pyramid_format"
                $pyramidMemory = Get-UInt64 $last `
                    "gpu_occlusion_depth_pyramid_memory_bytes"
                $buildDispatches = Get-UInt32 $last `
                    "gpu_occlusion_depth_pyramid_build_dispatch_count"
                $classificationDispatches = Get-UInt32 $last `
                    "gpu_occlusion_classification_dispatch_count"
                $classificationGroups = Get-UInt32 $last `
                    "gpu_occlusion_classification_group_count"
                $readbackReady = Get-UInt32 $last `
                    "gpu_occlusion_readback_ready"
                $readbackValid = Get-UInt32 $last `
                    "gpu_occlusion_readback_valid"
                $readbackStale = Get-UInt32 $last `
                    "gpu_occlusion_readback_stale"
                $readbackInvalid = Get-UInt32 $last `
                    "gpu_occlusion_readback_invalid_count"
                $readbackCandidates = Get-UInt32 $last `
                    "gpu_occlusion_readback_candidate_count"
                $visible = Get-UInt32 $last `
                    "gpu_occlusion_classified_visible_count"
                $occluded = Get-UInt32 $last `
                    "gpu_occlusion_classified_occluded_count"
                $uncertain = Get-UInt32 $last `
                    "gpu_occlusion_classified_uncertain_count"
                $maxMip = Get-UInt32 $last `
                    "gpu_occlusion_max_selected_mip"
                $sampledTexels = Get-UInt64 $last `
                    "gpu_occlusion_sampled_texel_count"
                $classificationConserved = Get-UInt32 $last `
                    "gpu_occlusion_classification_conserved"
                $readbackExpectedHash = Get-UInt64 $last `
                    "gpu_occlusion_readback_expected_identity_hash"
                $readbackResultHash = Get-UInt64 $last `
                    "gpu_occlusion_readback_result_identity_hash"
                $historyValid = Get-UInt32 $last `
                    "gpu_occlusion_history_valid"
                $historyReset = Get-UInt32 $last `
                    "gpu_occlusion_history_reset"
                $historyResetReason = Get-UInt32 $last `
                    "gpu_occlusion_history_reset_reason"
                $wouldCullDraws = Get-UInt32 $last `
                    "gpu_occlusion_would_cull_draw_count"
                $wouldCullTriangles = Get-UInt64 $last `
                    "gpu_occlusion_would_cull_triangle_count"
                $actualTriangles = Get-UInt64 $last `
                    "gpu_occlusion_actual_triangle_count"
                $auditMemory = Get-UInt64 $last `
                    "gpu_occlusion_audit_buffer_memory_bytes"
                $indirectReady = Get-UInt32 $last `
                    "gpu_occlusion_indirect_consumer_ready"
                $indirectActive = Get-UInt32 $last `
                    "gpu_occlusion_indirect_consumer_active"
                $indirectFallback = Get-UInt32 $last `
                    "gpu_occlusion_indirect_fallback_reason"
                $indirectCountMatches = Get-UInt32 $last `
                    "gpu_occlusion_indirect_candidate_count_matches"
                $indirectContentMatches = Get-UInt32 $last `
                    "gpu_occlusion_indirect_candidate_content_matches"
                $indirectCanonicalViewMatches = Get-UInt32 $last `
                    "gpu_occlusion_indirect_canonical_view_projection_matches"
                $indirectExtentMatches = Get-UInt32 $last `
                    "gpu_occlusion_indirect_projection_extent_matches"
                $indirectJitterWithinGuard = Get-UInt32 $last `
                    "gpu_occlusion_indirect_jitter_delta_within_guard"
                $indirectJitterDeltaX = Get-Double $last `
                    "gpu_occlusion_indirect_jitter_delta_pixels_x"
                $indirectJitterDeltaY = Get-Double $last `
                    "gpu_occlusion_indirect_jitter_delta_pixels_y"
                $indirectJitterGuard = Get-Double $last `
                    "gpu_occlusion_indirect_jitter_guard_pixels"
                $indirectSubmitted = Get-UInt32 $last `
                    "gpu_occlusion_indirect_submitted_draw_count"
                $indirectDirectFallback = Get-UInt32 $last `
                    "gpu_occlusion_indirect_direct_fallback_draw_count"
                $indirectInstanceFallback = Get-UInt32 $last `
                    "gpu_occlusion_indirect_instance_batch_fallback_count"
                $indirectSkinnedFallback = Get-UInt32 $last `
                    "gpu_occlusion_indirect_skinned_fallback_count"

                $expectedMipCount = Get-ExpectedMipCount `
                    -Width $pyramidWidth `
                    -Height $pyramidHeight
                $expectedGroups = [uint32][Math]::Ceiling($uploaded / 64.0)
                $allRowsActive = @($rows | Where-Object {
                    (Get-UInt32 $_ "gpu_occlusion_requested") -ne 1 -or
                    (Get-UInt32 $_ "gpu_occlusion_diagnostics_requested") -ne 1 -or
                    (Get-UInt32 $_ "gpu_occlusion_active") -ne 1 -or
                    (Get-UInt32 $_ "gpu_occlusion_fallback_reason") -ne 0
                }).Count -eq 0
                $allRowsDispatch = @($rows | Where-Object {
                    (Get-UInt32 $_ "gpu_occlusion_depth_pyramid_build_dispatch_count") -ne
                        (Get-UInt32 $_ "gpu_occlusion_depth_pyramid_mip_count") -or
                    (Get-UInt32 $_ "gpu_occlusion_classification_dispatch_count") -ne 1
                }).Count -eq 0
                $allRowsReadbackValid = @($rows | Where-Object {
                    (Get-UInt32 $_ "gpu_occlusion_readback_ready") -ne 1 -or
                    (Get-UInt32 $_ "gpu_occlusion_readback_valid") -ne 1 -or
                    (Get-UInt32 $_ "gpu_occlusion_readback_invalid_count") -ne 0
                }).Count -eq 0
                $allRowsReadbackCurrent = @($rows | Where-Object {
                    (Get-UInt32 $_ "gpu_occlusion_readback_stale") -ne 0
                }).Count -eq 0

                $checks.Add((New-Check "$($lane.name) audit path stays active" `
                    ($requested -eq 1 -and $diagnosticsRequested -eq 1 -and `
                        $active -eq 1 -and $fallback -eq 0 -and $allRowsActive) `
                    "$requested/$diagnosticsRequested/$active/$fallback" `
                    "1/1/1/0 on every row")) | Out-Null
                $checks.Add((New-Check "$($lane.name) candidate bounds and capacity" `
                    ($commandCount -gt 0 -and $commandCount -eq $validBounds -and `
                        $invalidBounds -eq 0 -and $zeroIdentity -eq 0 -and `
                        $capacity -eq 4096 -and $capacityDropped -eq 0 -and `
                        $uploaded -eq $commandCount -and $uploadedBytes -gt 0 -and `
                        $identityHash -ne 0 -and $contentHash -ne 0) `
                    "commands=$commandCount,valid=$validBounds,invalid=$invalidBounds,identity=$zeroIdentity,capacity=$uploaded/$capacity,drop=$capacityDropped,bytes=$uploadedBytes,hash=$identityHash/$contentHash" `
                    "commands=valid=uploaded>0,invalid/zero/drop=0,capacity=4096,bytes/hashes>0")) | Out-Null
                $checks.Add((New-Check "$($lane.name) conservative depth pyramid" `
                    ($pyramidAllocated -eq 1 -and $pyramidWidth -gt 0 -and `
                        $pyramidHeight -gt 0 -and $pyramidMipCount -eq `
                            $expectedMipCount -and $pyramidImageCount -gt 0 -and `
                        $pyramidFormat -eq 100 -and $pyramidMemory -gt 0 -and `
                        $buildDispatches -eq $pyramidMipCount -and `
                        $allRowsDispatch) `
                    "allocated=$pyramidAllocated,size=$($pyramidWidth)x$pyramidHeight,mips=$pyramidMipCount/$expectedMipCount,images=$pyramidImageCount,format=$pyramidFormat,memory=$pyramidMemory,dispatch=$buildDispatches" `
                    "allocated,size>0,full mip chain,images>0,R32_SFLOAT,memory>0,one dispatch/mip")) | Out-Null
                $checks.Add((New-Check "$($lane.name) classification dispatch" `
                    ($classificationDispatches -eq 1 -and `
                        $classificationGroups -eq $expectedGroups) `
                    "$classificationDispatches/$classificationGroups" `
                    "1/$expectedGroups")) | Out-Null
                $checks.Add((New-Check "$($lane.name) readback identity is current" `
                    ($readbackReady -eq 1 -and $readbackValid -eq 1 -and `
                        $readbackInvalid -eq 0 -and `
                        ($lane.mode -eq "audit-motion" -or `
                            $readbackCandidates -eq $uploaded) -and `
                        $allRowsReadbackValid -and `
                        $readbackExpectedHash -eq $readbackResultHash -and `
                        ($lane.mode -eq "audit-motion" -or `
                            ($readbackStale -eq 0 -and `
                                $allRowsReadbackCurrent -and `
                                $identityHash -eq $readbackExpectedHash))) `
                    "ready/valid/stale/invalid=$readbackReady/$readbackValid/$readbackStale/$readbackInvalid,candidates=$readbackCandidates/$uploaded,hash=$identityHash/$readbackExpectedHash/$readbackResultHash" `
                        "ready/valid/no-invalid,expected=result; static lanes also require candidates=uploaded and current=expected")) | Out-Null
                $checks.Add((New-Check "$($lane.name) classification is conserved" `
                    ($classificationConserved -eq 1 -and `
                        $visible + $occluded + $uncertain -eq `
                            $readbackCandidates -and `
                        $maxMip -lt $pyramidMipCount -and $sampledTexels -gt 0) `
                    "$readbackCandidates=$visible+$occluded+$uncertain,mip=$maxMip/$pyramidMipCount,samples=$sampledTexels" `
                    "candidate=visible+occluded+uncertain,maxMip<mips,samples>0")) | Out-Null
                $checks.Add((New-Check "$($lane.name) audit does not change real draws" `
                    ($drawsUnchanged -eq 1 -and $wouldCullDraws -eq $occluded -and `
                        $wouldCullTriangles -le $actualTriangles) `
                    "unchanged=$drawsUnchanged,wouldCull=$wouldCullDraws/$wouldCullTriangles,actual=$actualDrawCount/$actualTriangles" `
                    "unchanged=1,wouldCullDraws=occluded,wouldCullTriangles<=actual")) | Out-Null
                $checks.Add((New-Check "$($lane.name) audit memory is explicit" `
                    ($auditMemory -gt 0) $auditMemory "bytes>0")) | Out-Null

                if ($lane.expectOccluded) {
                    $checks.Add((New-Check "$($lane.name) exercises occluded candidates" `
                        ($occluded -gt 0 -and $wouldCullTriangles -gt 0) `
                        "$occluded/$wouldCullTriangles" "draws/triangles>0")) | Out-Null
                }

                if ($lane.mode -in @("consume-static", "consume-jitter")) {
                    $maxTemporalJitterMagnitude = 0.0
                    $maxClassificationJitterMismatch = 0.0
                    foreach ($row in $rows) {
                        $temporalX = Get-Double $row "temporal_jitter_pixels_x"
                        $temporalY = Get-Double $row "temporal_jitter_pixels_y"
                        $classificationX = Get-Double $row `
                            "gpu_occlusion_classification_jitter_pixels_x"
                        $classificationY = Get-Double $row `
                            "gpu_occlusion_classification_jitter_pixels_y"
                        $maxTemporalJitterMagnitude = [Math]::Max(
                            $maxTemporalJitterMagnitude,
                            [Math]::Max([Math]::Abs($temporalX), [Math]::Abs($temporalY))
                        )
                        $maxClassificationJitterMismatch = [Math]::Max(
                            $maxClassificationJitterMismatch,
                            [Math]::Max(
                                [Math]::Abs($classificationX - $temporalX),
                                [Math]::Abs($classificationY - $temporalY)
                            )
                        )
                    }
                    $allRowsTemporalContract = @($rows | Where-Object {
                        (Get-UInt32 $_ "temporal_antialiasing_mode") -ne
                            [uint32]$lane.expectedAaMode -or
                        [Math]::Abs(
                            (Get-Double $_ "gpu_occlusion_classification_jitter_guard_pixels") -
                            [double]$lane.expectedJitterGuard
                        ) -gt 0.0001
                    }).Count -eq 0
                    $jitterMagnitudeMatches = if ($lane.expectJitter) {
                        $maxTemporalJitterMagnitude -gt 0.0001
                    } else {
                        $maxTemporalJitterMagnitude -le 0.0001
                    }
                    $dlssContractMatches = $true
                    if ([uint32]$lane.expectedAaMode -eq 5) {
                        $dlssContractMatches = @($rows | Where-Object {
                            (Get-UInt32 $_ "temporal_upscaler_dlss_evaluate_attempted") -ne 1 -or
                            (Get-UInt32 $_ "temporal_upscaler_dlss_evaluate_result") -ne 1 -or
                            (Get-UInt32 $_ "temporal_upscaler_dlss_quality_evaluate_output_ready") -ne 1
                        }).Count -eq 0
                    }
                    $staticHistoryValid = @($rows | Where-Object {
                        (Get-UInt32 $_ "gpu_occlusion_history_valid") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_history_reset") -ne 0 -or
                        (Get-UInt32 $_ "gpu_occlusion_history_reset_reason") -ne 0
                    }).Count -eq 0
                    $allRowsIndirect = @($rows | Where-Object {
                        (Get-UInt32 $_ "gpu_occlusion_indirect_consumer_active") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_fallback_reason") -ne 0 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_candidate_count_matches") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_candidate_content_matches") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_canonical_view_projection_matches") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_projection_extent_matches") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_jitter_delta_within_guard") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_submitted_draw_count") -eq 0
                    }).Count -eq 0
                    $checks.Add((New-Check "$($lane.name) temporal mode is real" `
                        ($allRowsTemporalContract -and $jitterMagnitudeMatches -and `
                            $maxClassificationJitterMismatch -le 0.0001 -and `
                            $dlssContractMatches) `
                        "aa=$(Get-UInt32 $last 'temporal_antialiasing_mode'),maxJitter=$maxTemporalJitterMagnitude,classificationMismatch=$maxClassificationJitterMismatch,guard=$classificationJitterGuard,dlss=$dlssContractMatches" `
                        "aa=$($lane.expectedAaMode),realJitter=$($lane.expectJitter),classification=current,guard=$($lane.expectedJitterGuard),DLSS ready when requested")) | Out-Null
                    $checks.Add((New-Check "$($lane.name) static camera indirect history is reusable" `
                        ($historyValid -eq 1 -and $historyReset -eq 0 -and `
                            $historyResetReason -eq 0 -and $staticHistoryValid -and `
                            $indirectReady -eq 1 -and $indirectActive -eq 1 -and `
                            $indirectFallback -eq 0 -and `
                            $indirectCountMatches -eq 1 -and `
                            $indirectContentMatches -eq 1 -and `
                            $indirectCanonicalViewMatches -eq 1 -and `
                            $indirectExtentMatches -eq 1 -and `
                            $indirectJitterWithinGuard -eq 1 -and `
                            [Math]::Max($indirectJitterDeltaX, $indirectJitterDeltaY) -le `
                                $indirectJitterGuard + 0.0001 -and `
                            $indirectSubmitted -gt 0 -and $allRowsIndirect) `
                        "history=$historyValid/$historyReset/$historyResetReason,indirect=$indirectReady/$indirectActive/$indirectFallback/$indirectCountMatches/$indirectContentMatches/$indirectCanonicalViewMatches/$indirectExtentMatches/$indirectJitterWithinGuard/$indirectJitterDeltaX/$indirectJitterDeltaY/$indirectJitterGuard/$indirectSubmitted/$indirectDirectFallback/$indirectInstanceFallback/$indirectSkinnedFallback" `
                        "history=1/0/0,indirect=1/1/0/count/content/canonical/extent/jitter=1,delta<=guard,submitted>0 on every row")) | Out-Null
                } elseif ($lane.mode -eq "audit-motion") {
                    $motionRowsConservative = @($rows | Where-Object {
                        (Get-UInt32 $_ "gpu_occlusion_indirect_consumer_active") -ne 0 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_submitted_draw_count") -ne 0 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_fallback_reason") -notin @(2, 3, 4, 5, 6)
                    }).Count -eq 0
                    $cameraMotionActive = [double]$last.benchmark_camera_motion_time_seconds
                    $checks.Add((New-Check "$($lane.name) camera motion invalidates indirect history" `
                        ($cameraMotionActive -gt 0.0 -and $indirectActive -eq 0 -and `
                            $indirectSubmitted -eq 0 -and `
                            $indirectFallback -in @(2, 3, 4, 5, 6) -and `
                            $motionRowsConservative) `
                        "camera=$cameraMotionActive,indirect=$indirectActive/$indirectFallback/$indirectSubmitted" `
                        "camera>0,indirect=0/(count, content, canonical view, extent, or jitter guard change)/0 on every row")) | Out-Null
                } else {
                    throw "Unsupported GPU occlusion audit mode: $($lane.mode)"
                }

                $metrics = [ordered]@{
                    mode = $lane.mode
                    active = "$requested/$diagnosticsRequested/$active/$fallback"
                    candidates = "$commandCount/$uploaded/$readbackCandidates"
                    classes = "$visible/$occluded/$uncertain"
                    pyramid = "$($pyramidWidth)x$pyramidHeight/$pyramidMipCount/$pyramidImageCount"
                    dispatch = "$buildDispatches/$classificationDispatches/$classificationGroups"
                    readback = "$readbackReady/$readbackValid/$readbackStale/$readbackInvalid"
                    identity = "$identityHash/$readbackExpectedHash/$readbackResultHash"
                    contentHash = $contentHash
                    history = "$historyValid/$historyReset/$historyResetReason"
                    jitter = "$classificationJitterX/$classificationJitterY/$classificationJitterGuard->$indirectJitterDeltaX/$indirectJitterDeltaY/$indirectJitterGuard"
                    indirect = "$indirectReady/$indirectActive/$indirectFallback/$indirectCountMatches/$indirectContentMatches/$indirectCanonicalViewMatches/$indirectExtentMatches/$indirectJitterWithinGuard/$indirectSubmitted/$indirectDirectFallback/$indirectInstanceFallback/$indirectSkinnedFallback"
                    wouldCull = "$wouldCullDraws/$wouldCullTriangles"
                    actual = "$actualDrawCount/$actualTriangles/$drawsUnchanged"
                    memory = "$pyramidMemory/$auditMemory"
                    frameGraphIssues = $frameGraphIssues
                }
            } else {
                $expectedRequested = 0
                $expectedFallback = 1
                $resourceAndWorkColumns = @(
                    "gpu_occlusion_depth_pyramid_allocated",
                    "gpu_occlusion_depth_pyramid_memory_bytes",
                    "gpu_occlusion_depth_pyramid_build_dispatch_count",
                    "gpu_occlusion_classification_dispatch_count",
                    "gpu_occlusion_readback_ready",
                    "gpu_occlusion_readback_valid",
                    "gpu_occlusion_uploaded_candidate_count",
                    "gpu_occlusion_would_cull_draw_count",
                    "gpu_occlusion_audit_buffer_memory_bytes"
                )
                $resourceAndWorkSum = [uint64]0
                foreach ($column in $resourceAndWorkColumns) {
                    $resourceAndWorkSum += Get-UInt64 $last $column
                }
                if ($lane.mode -eq "release-consume") {
                    $indirectActive = Get-UInt32 $last `
                        "gpu_occlusion_indirect_consumer_active"
                    $indirectFallback = Get-UInt32 $last `
                        "gpu_occlusion_indirect_fallback_reason"
                    $indirectSubmitted = Get-UInt32 $last `
                        "gpu_occlusion_indirect_submitted_draw_count"
                    $indirectDirectFallback = Get-UInt32 $last `
                        "gpu_occlusion_indirect_direct_fallback_draw_count"
                    $readbackReady = Get-UInt32 $last `
                        "gpu_occlusion_readback_ready"
                    $readbackValid = Get-UInt32 $last `
                        "gpu_occlusion_readback_valid"
                    $releaseCanonicalMatches = Get-UInt32 $last `
                        "gpu_occlusion_indirect_canonical_view_projection_matches"
                    $releaseExtentMatches = Get-UInt32 $last `
                        "gpu_occlusion_indirect_projection_extent_matches"
                    $releaseJitterWithinGuard = Get-UInt32 $last `
                        "gpu_occlusion_indirect_jitter_delta_within_guard"
                    $releaseJitterDeltaX = Get-Double $last `
                        "gpu_occlusion_indirect_jitter_delta_pixels_x"
                    $releaseJitterDeltaY = Get-Double $last `
                        "gpu_occlusion_indirect_jitter_delta_pixels_y"
                    $releaseJitterGuard = Get-Double $last `
                        "gpu_occlusion_indirect_jitter_guard_pixels"
                    $allRowsReleaseConsume = @($rows | Where-Object {
                        (Get-UInt32 $_ "gpu_occlusion_requested") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_diagnostics_requested") -ne 0 -or
                        (Get-UInt32 $_ "gpu_occlusion_active") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_fallback_reason") -ne 0 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_consumer_active") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_fallback_reason") -ne 0 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_canonical_view_projection_matches") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_projection_extent_matches") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_jitter_delta_within_guard") -ne 1 -or
                        (Get-UInt32 $_ "gpu_occlusion_indirect_submitted_draw_count") -eq 0 -or
                        (Get-UInt32 $_ "gpu_occlusion_readback_ready") -ne 0 -or
                        (Get-UInt32 $_ "gpu_occlusion_readback_valid") -ne 0 -or
                        (Get-UInt32 $_ "temporal_antialiasing_mode") -ne
                            [uint32]$lane.expectedAaMode -or
                        [Math]::Abs(
                            (Get-Double $_ "gpu_occlusion_classification_jitter_guard_pixels") -
                            [double]$lane.expectedJitterGuard
                        ) -gt 0.0001 -or
                        [Math]::Max(
                            [Math]::Abs((Get-Double $_ "temporal_jitter_pixels_x")),
                            [Math]::Abs((Get-Double $_ "temporal_jitter_pixels_y"))
                        ) -le 0.0001
                    }).Count -eq 0
                    $checks.Add((New-Check "$($lane.name) Release consumes indirect without readback" `
                        ($requested -eq 1 -and $diagnosticsRequested -eq 0 -and `
                            $active -eq 1 -and $fallback -eq 0 -and `
                            $drawsUnchanged -eq 1 -and `
                            $indirectActive -eq 1 -and $indirectFallback -eq 0 -and `
                            $releaseCanonicalMatches -eq 1 -and `
                            $releaseExtentMatches -eq 1 -and `
                            $releaseJitterWithinGuard -eq 1 -and `
                            [Math]::Max($releaseJitterDeltaX, $releaseJitterDeltaY) -le `
                                $releaseJitterGuard + 0.0001 -and `
                            $indirectSubmitted -gt 0 -and `
                            $readbackReady -eq 0 -and $readbackValid -eq 0 -and `
                            $resourceAndWorkSum -gt 0 -and $allRowsReleaseConsume) `
                        "feature=$requested/$diagnosticsRequested/$active/$fallback/$drawsUnchanged,indirect=$indirectActive/$indirectFallback/$releaseCanonicalMatches/$releaseExtentMatches/$releaseJitterWithinGuard/$releaseJitterDeltaX/$releaseJitterDeltaY/$releaseJitterGuard/$indirectSubmitted/$indirectDirectFallback,readback=$readbackReady/$readbackValid,resources=$resourceAndWorkSum" `
                        "1/0/1/0/1,indirect active/canonical/extent/jitter=1,delta<=guard,submitted>0,readback=0/0,resources>0 on every row")) | Out-Null
                } else {
                    $checks.Add((New-Check "$($lane.name) explicit inactive fallback" `
                        ($requested -eq $expectedRequested -and `
                            $diagnosticsRequested -eq 0 -and $active -eq 0 -and `
                            $fallback -eq $expectedFallback -and `
                            $drawsUnchanged -eq 0) `
                        "$requested/$diagnosticsRequested/$active/$fallback/$drawsUnchanged" `
                        "$expectedRequested/0/0/$expectedFallback/0")) | Out-Null
                    $checks.Add((New-Check "$($lane.name) allocates no audit resources or work" `
                        ($resourceAndWorkSum -eq 0) $resourceAndWorkSum 0)) | Out-Null
                }

                $metrics = [ordered]@{
                    mode = $lane.mode
                    active = "$requested/$diagnosticsRequested/$active/$fallback"
                    resourceAndWorkSum = $resourceAndWorkSum
                    actualDrawCount = $actualDrawCount
                    frameGraphIssues = $frameGraphIssues
                }
            }
        }
    }

    $stdout = Get-Content -Raw -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
    $stderr = Get-Content -Raw -LiteralPath $stderrPath -ErrorAction SilentlyContinue
    $processLog = @($stdout, $stderr) -join [Environment]::NewLine
    $validationBlocks = @(
        [regex]::Matches(
            $processLog,
            '(?ms)^\[Vulkan Validation\].*?(?=^\[Vulkan Validation\]|\z)'
        ) | ForEach-Object Value
    )
    $dlssLane = $lane.PSObject.Properties.Name -contains "expectedAaMode" -and
        [uint32]$lane.expectedAaMode -eq 5
    $knownNgxDlssValidation = @()
    if ($dlssLane) {
        $knownNgxDlssValidation = @($validationBlocks | Where-Object {
            $_ -match 'nv\.ngx\.dlss\.resource' -and
            $_ -match 'VUID-vkCmdDraw-None-09600'
        })
    }
    $unexpectedValidation = @($validationBlocks | Where-Object {
        $_ -notin $knownNgxDlssValidation
    })
    $checks.Add((New-Check "$($lane.name) Vulkan validation" `
        ($unexpectedValidation.Count -eq 0) `
        "unexpected=$($unexpectedValidation.Count),knownNgxDlss=$($knownNgxDlssValidation.Count)" `
        "unexpected=0; only exact nv.ngx.dlss.resource/VUID-09600 blocks may be isolated in DLSS")) | Out-Null

    $laneFailCount = @($checks | Where-Object status -eq "fail").Count
    $reports.Add([pscustomobject]@{
        lane = $lane.name
        executable = $lane.executable
        csv = $csvPath
        verdict = if ($laneFailCount -eq 0) { "pass" } else { "fail" }
        passCount = @($checks | Where-Object status -eq "pass").Count
        failCount = $laneFailCount
        metrics = $metrics
        checks = $checks
    }) | Out-Null
}

$passCount = [int](($reports | Measure-Object passCount -Sum).Sum)
$failCount = [int](($reports | Measure-Object failCount -Sum).Sum)
$summary = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    outputDirectory = $OutputDirectory
    verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
    passCount = $passCount
    failCount = $failCount
    reports = $reports
}
$summaryPath = Join-Path $OutputDirectory "summary.json"
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding utf8
[pscustomobject]$summary

if ($Strict -and $failCount -ne 0) {
    throw "GPU occlusion health gate failed: $failCount check(s)"
}
