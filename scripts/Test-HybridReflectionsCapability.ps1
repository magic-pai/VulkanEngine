[CmdletBinding()]
param(
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$ShowcaseExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [switch]$SkipBuild,
    [switch]$Strict,
    [string]$OutputDirectory = "tmp\hybrid_reflections_capability"
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
        [string]$Name,
        [bool]$Passed,
        [object]$Actual,
        [object]$Expected
    )

    return [pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }
}

function Get-UIntValue {
    param($Row, [string]$Name)

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV column: $Name"
    }
    return [uint32]$property.Value
}

function Set-LaneEnvironment {
    param([hashtable]$Values, [string[]]$ManagedKeys)

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
    param([hashtable]$Previous)

    foreach ($entry in $Previous.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable(
            $entry.Key,
            $entry.Value,
            "Process"
        )
    }
}

if (-not $SkipBuild) {
    $buildCommand = @(
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1',
        "cd /d `"$projectRoot\build`"",
        'MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo',
        'MSBuild SelfEngineLightingShowcase.vcxproj /p:Configuration=Debug /m:1 /nr:false /v:minimal /nologo'
    ) -join ' && '
    & cmd.exe /d /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Hybrid reflections capability build failed with exit code $LASTEXITCODE"
    }
}

$forwardExecutable = Resolve-ProjectPath $ForwardExecutablePath
$showcaseExecutable = Resolve-ProjectPath $ShowcaseExecutablePath
$managedKeys = @(
    "SE_HYBRID_REFLECTIONS_RT",
    "SE_HYBRID_REFLECTIONS_RT_OFF",
    "SE_FORWARD3D_AA_MODE",
    "SE_BENCHMARK_SCENE",
    "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION",
    "SE_SCENE_UPDATE_FREEZE",
    "SE_VISUAL_QA_HIDE_IMGUI",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_BENCHMARK_CSV"
)
$commonEnvironment = @{
    SE_FORWARD3D_AA_MODE = "taa"
    SE_SCENE_UPDATE_FREEZE = "1"
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_BENCHMARK_WARMUP_FRAMES = "2"
    SE_BENCHMARK_FRAMES = "3"
}
$laneSpecs = @(
    [pscustomobject]@{
        name = "lighting-showcase-requested"
        executable = $showcaseExecutable
        requested = 1
        disabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
        }
    },
    [pscustomobject]@{
        name = "forward3d-fbx-requested"
        executable = $forwardExecutable
        requested = 1
        disabled = 0
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
        }
    },
    [pscustomobject]@{
        name = "lighting-showcase-disabled-control"
        executable = $showcaseExecutable
        requested = 1
        disabled = 1
        environment = @{
            SE_HYBRID_REFLECTIONS_RT = "1"
            SE_HYBRID_REFLECTIONS_RT_OFF = "1"
        }
    },
    [pscustomobject]@{
        name = "forward3d-not-requested-control"
        executable = $forwardExecutable
        requested = 0
        disabled = 0
        environment = @{
            SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
        }
    }
)

$reports = [Collections.Generic.List[object]]::new()
foreach ($lane in $laneSpecs) {
    $laneDirectory = Join-Path $OutputDirectory $lane.name
    New-Item -ItemType Directory -Force -Path $laneDirectory | Out-Null
    $csvPath = Join-Path $laneDirectory "hybrid_reflections_capability.csv"
    $stdoutPath = Join-Path $laneDirectory "process.stdout.log"
    $stderrPath = Join-Path $laneDirectory "process.stderr.log"
    Remove-Item -LiteralPath $csvPath, $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    $environment = $commonEnvironment.Clone()
    foreach ($entry in $lane.environment.GetEnumerator()) {
        $environment[$entry.Key] = $entry.Value
    }
    $environment["SE_BENCHMARK_CSV"] = $csvPath
    $previous = Set-LaneEnvironment -Values $environment -ManagedKeys $managedKeys
    try {
        $executableDirectory = Split-Path -Parent $lane.executable
        $commandLine =
            "cd /d `"$executableDirectory`" && `"$($lane.executable)`" 1> `"$stdoutPath`" 2> `"$stderrPath`""
        & cmd.exe /d /c $commandLine
        $exitCode = $LASTEXITCODE
    } finally {
        Restore-LaneEnvironment -Previous $previous
    }

    $checks = [Collections.Generic.List[object]]::new()
    $checks.Add((New-Check "$($lane.name) process exits" `
        ($exitCode -eq 0) $exitCode 0)) | Out-Null
    $checks.Add((New-Check "$($lane.name) writes CSV" `
        (Test-Path -LiteralPath $csvPath) (Test-Path -LiteralPath $csvPath) $true)) | Out-Null

    $metrics = $null
    if ($exitCode -eq 0 -and (Test-Path -LiteralPath $csvPath)) {
        $rows = @(Import-Csv -LiteralPath $csvPath)
        $checks.Add((New-Check "$($lane.name) captures rows" `
            ($rows.Count -eq 3) $rows.Count 3)) | Out-Null
        if ($rows.Count -gt 0) {
            $last = $rows[-1]
            $contract = Get-UIntValue $last "hybrid_reflections_capability_contract_version"
            $requested = Get-UIntValue $last "hybrid_reflections_requested"
            $disabled = Get-UIntValue $last "hybrid_reflections_control_disabled"
            $bdaExtension = Get-UIntValue $last "hybrid_reflections_buffer_device_address_extension_supported"
            $deferredExtension = Get-UIntValue $last "hybrid_reflections_deferred_host_operations_extension_supported"
            $asExtension = Get-UIntValue $last "hybrid_reflections_acceleration_structure_extension_supported"
            $rayQueryExtension = Get-UIntValue $last "hybrid_reflections_ray_query_extension_supported"
            $bdaFeature = Get-UIntValue $last "hybrid_reflections_buffer_device_address_feature_supported"
            $asFeature = Get-UIntValue $last "hybrid_reflections_acceleration_structure_feature_supported"
            $rayQueryFeature = Get-UIntValue $last "hybrid_reflections_ray_query_feature_supported"
            $hardwareReady = Get-UIntValue $last "hybrid_reflections_ray_query_hardware_ready"
            $deviceEnabled = Get-UIntValue $last "hybrid_reflections_ray_query_device_enabled"
            $accelerationStructureContract = Get-UIntValue $last "hybrid_reflections_acceleration_structure_contract_version"
            $fullSceneCommands = Get-UIntValue $last "hybrid_reflections_full_scene_command_count"
            $opaqueRigidCommands = Get-UIntValue $last "hybrid_reflections_opaque_rigid_command_count"
            $skinnedFallback = Get-UIntValue $last "hybrid_reflections_skinned_fallback_count"
            $alphaFallback = Get-UIntValue $last "hybrid_reflections_alpha_fallback_count"
            $invalidGeometry = Get-UIntValue $last "hybrid_reflections_invalid_geometry_count"
            $instanceOverflow = Get-UIntValue $last "hybrid_reflections_instance_overflow_count"
            $blasCacheCount = Get-UIntValue $last "hybrid_reflections_blas_cache_count"
            $blasReadyCount = Get-UIntValue $last "hybrid_reflections_blas_ready_count"
            $tlasInstanceCount = Get-UIntValue $last "hybrid_reflections_tlas_instance_count"
            $tlasInstanceCapacity = Get-UIntValue $last "hybrid_reflections_tlas_instance_capacity"
            $tlasAddressReady = Get-UIntValue $last "hybrid_reflections_tlas_address_ready"
            $accelerationStructureResourcesReady = Get-UIntValue $last "hybrid_reflections_acceleration_structure_resources_ready"
            $runtimeReady = Get-UIntValue $last "hybrid_reflections_runtime_resources_ready"
            $active = Get-UIntValue $last "hybrid_reflections_active"
            $fallback = Get-UIntValue $last "hybrid_reflections_fallback_reason"
            $maxBlasBuildCount = [uint32](($rows | ForEach-Object {
                    Get-UIntValue $_ "hybrid_reflections_blas_build_count"
                } | Measure-Object -Maximum).Maximum)
            $maxTlasBuildCount = [uint32](($rows | ForEach-Object {
                    Get-UIntValue $_ "hybrid_reflections_tlas_build_count"
                } | Measure-Object -Maximum).Maximum)
            $maxTlasUpdateCount = [uint32](($rows | ForEach-Object {
                    Get-UIntValue $_ "hybrid_reflections_tlas_update_count"
                } | Measure-Object -Maximum).Maximum)
            $maxAsResourceReady = [uint32](($rows | ForEach-Object {
                    Get-UIntValue $_ "hybrid_reflections_acceleration_structure_resources_ready"
                } | Measure-Object -Maximum).Maximum)
            $maxSkinnedFallback = [uint32](($rows | ForEach-Object {
                    Get-UIntValue $_ "hybrid_reflections_skinned_fallback_count"
                } | Measure-Object -Maximum).Maximum)
            $extensionReady =
                $bdaExtension -eq 1 -and $deferredExtension -eq 1 -and
                $asExtension -eq 1 -and $rayQueryExtension -eq 1
            $featureReady =
                $bdaFeature -eq 1 -and $asFeature -eq 1 -and
                $rayQueryFeature -eq 1
            $expectedDeviceEnabled =
                $lane.requested -eq 1 -and $lane.disabled -eq 0 -and
                $hardwareReady -eq 1
            $runtimeResourcesExpected =
                $lane.requested -eq 1 -and
                $lane.disabled -eq 0 -and
                $hardwareReady -eq 1
            $fallbackMatches = if ($lane.requested -eq 0) {
                $fallback -eq 1
            } elseif ($lane.disabled -eq 1) {
                $fallback -eq 2
            } elseif ($runtimeResourcesExpected) {
                $fallback -eq 7
            } else {
                $fallback -in @(3, 4, 5)
            }

            $checks.Add((New-Check "$($lane.name) contract version" `
                ($contract -eq 1) $contract 1)) | Out-Null
            $checks.Add((New-Check "$($lane.name) request state" `
                ($requested -eq $lane.requested -and $disabled -eq $lane.disabled) `
                "$requested/$disabled" "$($lane.requested)/$($lane.disabled)")) | Out-Null
            $checks.Add((New-Check "$($lane.name) hardware readiness is coherent" `
                ($hardwareReady -eq [uint32]($extensionReady -and $featureReady)) `
                "hardware=$hardwareReady,extensions=$extensionReady,features=$featureReady" `
                "hardware=extensions&&features")) | Out-Null
            $checks.Add((New-Check "$($lane.name) logical device state" `
                ($deviceEnabled -eq [uint32]$expectedDeviceEnabled) `
                $deviceEnabled ([uint32]$expectedDeviceEnabled))) | Out-Null
            $checks.Add((New-Check "$($lane.name) acceleration-structure contract" `
                ($accelerationStructureContract -eq [uint32]$runtimeResourcesExpected) `
                $accelerationStructureContract ([uint32]$runtimeResourcesExpected))) | Out-Null
            if ($runtimeResourcesExpected) {
                $resourcesReady =
                    $accelerationStructureResourcesReady -eq 1 -and
                    $runtimeReady -eq 1 -and
                    $active -eq 0 -and
                    $tlasAddressReady -eq 1 -and
                    $tlasInstanceCount -gt 0 -and
                    $tlasInstanceCapacity -ge $tlasInstanceCount -and
                    $blasCacheCount -gt 0 -and
                    $blasReadyCount -gt 0 -and
                    $opaqueRigidCommands -gt 0 -and
                    $fullSceneCommands -ge $opaqueRigidCommands -and
                    $instanceOverflow -eq 0
                $checks.Add((New-Check "$($lane.name) BLAS/TLAS resources ready" `
                    $resourcesReady `
                    "asReady=$accelerationStructureResourcesReady,runtime=$runtimeReady,address=$tlasAddressReady,instances=$tlasInstanceCount,blas=$blasReadyCount" `
                    "resources=1,address=1,instances>0,blas>0")) | Out-Null
                $checks.Add((New-Check "$($lane.name) acceleration builds recorded" `
                    ($maxBlasBuildCount -gt 0 -and ($maxTlasBuildCount -gt 0 -or $maxTlasUpdateCount -gt 0)) `
                    "blasBuild=$maxBlasBuildCount,tlasBuild=$maxTlasBuildCount,tlasUpdate=$maxTlasUpdateCount" `
                    "blasBuild>0 and (tlasBuild>0 or tlasUpdate>0)")) | Out-Null
                $checks.Add((New-Check "$($lane.name) scene geometry accounting" `
                    ($invalidGeometry -eq 0 -and $alphaFallback -ge 0) `
                    "invalid=$invalidGeometry,alphaFallback=$alphaFallback" `
                    "invalid=0")) | Out-Null
                if ($lane.name -eq "forward3d-fbx-requested") {
                    $checks.Add((New-Check "$($lane.name) skinned geometry uses fallback" `
                        ($maxSkinnedFallback -gt 0) `
                        $maxSkinnedFallback ">0")) | Out-Null
                }
            } else {
                $noResources = @(
                    $rows | Where-Object {
                        (Get-UIntValue $_ "hybrid_reflections_acceleration_structure_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_runtime_resources_ready") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_blas_cache_count") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_tlas_instance_count") -ne 0 -or
                        (Get-UIntValue $_ "hybrid_reflections_tlas_address_ready") -ne 0
                    }
                ).Count -eq 0
                $checks.Add((New-Check "$($lane.name) no AS resources when inactive" `
                    $noResources `
                    "asReady=$maxAsResourceReady,blas=$blasCacheCount,instances=$tlasInstanceCount,address=$tlasAddressReady" `
                    "all=0")) | Out-Null
            }
            $checks.Add((New-Check "$($lane.name) fallback is explicit" `
                $fallbackMatches $fallback "lane-specific")) | Out-Null
            $frameGraphIssues = @(
                $rows | Where-Object { [uint32]$_.framegraph_validation_issues -ne 0 }
            ).Count
            $checks.Add((New-Check "$($lane.name) framegraph validation" `
                ($frameGraphIssues -eq 0) $frameGraphIssues 0)) | Out-Null

            $metrics = [ordered]@{
                requested = $requested
                disabled = $disabled
                hardwareReady = $hardwareReady
                deviceEnabled = $deviceEnabled
                accelerationStructureContract = $accelerationStructureContract
                fullSceneCommands = $fullSceneCommands
                opaqueRigidCommands = $opaqueRigidCommands
                skinnedFallback = $skinnedFallback
                alphaFallback = $alphaFallback
                blasCacheCount = $blasCacheCount
                blasReadyCount = $blasReadyCount
                maxBlasBuildCount = $maxBlasBuildCount
                tlasInstanceCount = $tlasInstanceCount
                tlasInstanceCapacity = $tlasInstanceCapacity
                maxTlasBuildCount = $maxTlasBuildCount
                maxTlasUpdateCount = $maxTlasUpdateCount
                tlasAddressReady = $tlasAddressReady
                accelerationStructureResourcesReady = $accelerationStructureResourcesReady
                runtimeReady = $runtimeReady
                active = $active
                fallbackReason = $fallback
            }
        }
    }

    $stdout = Get-Content -Raw -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
    $stderr = Get-Content -Raw -LiteralPath $stderrPath -ErrorAction SilentlyContinue
    $processLog = @($stdout, $stderr) -join [Environment]::NewLine
    $validationMessages = @(
        $processLog -split "`r?`n" |
            Where-Object { $_ -match '\[Vulkan Validation\]|\bVUID-' }
    )
    $checks.Add((New-Check "$($lane.name) Vulkan validation" `
        ($validationMessages.Count -eq 0) $validationMessages.Count 0)) | Out-Null

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
    throw "Hybrid reflections capability gate failed: $failCount check(s)"
}
