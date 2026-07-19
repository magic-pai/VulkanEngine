[CmdletBinding()]
param(
    [string]$ForwardExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$ShowcaseExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [switch]$SkipBuild,
    [switch]$Strict,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot "tmp\ffx_sssr_integration_health"
}
if (![System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $projectRoot $Path))
}

function New-Check {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Actual,
        [string]$Expected
    )

    return [pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }
}

function Get-UIntMetric {
    param([pscustomobject]$Row, [string]$Name)

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Missing CSV metric: $Name"
    }
    return [uint32]$property.Value
}

function Set-ProcessEnvironment {
    param([hashtable]$Values)

    $previous = @{}
    foreach ($entry in $Values.GetEnumerator()) {
        $previous[$entry.Key] =
            [Environment]::GetEnvironmentVariable($entry.Key, "Process")
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
    return $previous
}

function Restore-ProcessEnvironment {
    param([hashtable]$Values)

    foreach ($entry in $Values.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
}

function Invoke-Build {
    $buildDirectory = Join-Path $projectRoot "build"
    $cmakeCommand = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if ([string]::IsNullOrWhiteSpace($cmakeCommand)) {
        $vsCmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path -LiteralPath $vsCmake) {
            $cmakeCommand = $vsCmake
        }
    }
    if ([string]::IsNullOrWhiteSpace($cmakeCommand)) {
        throw "Unable to locate cmake.exe"
    }
    & $cmakeCommand -S $projectRoot -B $buildDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE"
    }

    $vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    foreach ($project in @(
            "SelfEngineShaders.vcxproj",
            "SelfEngineForward3D.vcxproj",
            "SelfEngineLightingShowcase.vcxproj"
        )) {
        & cmd.exe /c "call `"$vcvars`" >nul 2>&1 && cd /d `"$buildDirectory`" && MSBuild $project /p:Configuration=Debug /v:minimal /nologo"
        if ($LASTEXITCODE -ne 0) {
            throw "$project build failed with exit code $LASTEXITCODE"
        }
    }
}

function Invoke-StaticChecks {
    $vendorRoot = Join-Path $projectRoot "thirdParty\fidelityfx_sssr"
    $cmakePath = Join-Path $projectRoot "CMakeLists.txt"
    $cmakeSource = Get-Content -Raw -LiteralPath $cmakePath
    $sssrHeader = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "include\ffx-sssr\ffx_sssr.h")
    $dnsrCommon = Get-Content -Raw -LiteralPath (
        Join-Path $vendorRoot "include\ffx-dnsr\ffx_denoiser_reflections_common.h")
    $checks = @()

    $requiredFiles = @(
        "LICENSE.txt",
        "NOTICES.txt",
        "SELFENGINE_INTEGRATION.md",
        "include\ffx-sssr\ffx_sssr.h",
        "include\ffx-dnsr\ffx_denoiser_reflections_common.h",
        "include\ffx-dnsr\ffx_denoiser_reflections_reproject.h",
        "include\ffx-dnsr\ffx_denoiser_reflections_resolve_temporal.h",
        "include\ffx-spd\ffx_a.h",
        "include\ffx-spd\ffx_spd.h",
        "shaders\ClassifyTiles.hlsl",
        "shaders\Intersect.hlsl",
        "shaders\Reproject.hlsl",
        "shaders\ResolveTemporal.hlsl",
        "shaders\DepthDownsample.hlsl"
    )
    foreach ($relativePath in $requiredFiles) {
        $fullPath = Join-Path $vendorRoot $relativePath
        $checks += New-Check `
            "FFX SSSR vendor file exists: $relativePath" `
            (Test-Path -LiteralPath $fullPath) `
            $fullPath `
            "exists"
    }

    $shaderOutputDirectory = Join-Path $projectRoot "build\shaders"
    $shaderNames = @(
        "ClassifyTiles",
        "PrepareBlueNoiseTexture",
        "PrepareIndirectArgs",
        "Intersect",
        "Reproject",
        "Prefilter",
        "ResolveTemporal",
        "DepthDownsample"
    )
    foreach ($shaderName in $shaderNames) {
        $spvPath = Join-Path $shaderOutputDirectory (
            "ffx_sssr_$shaderName.hlsl.spv")
        $spv = Get-Item -LiteralPath $spvPath -ErrorAction SilentlyContinue
        $spvActual = "missing"
        if ($null -ne $spv) {
            $spvActual = "$($spv.Length) bytes"
        }
        $checks += New-Check `
            "FFX SSSR SPIR-V exists: $shaderName" `
            ($null -ne $spv -and $spv.Length -gt 0) `
            $spvActual `
            "non-empty SPIR-V"
    }

    $checks += New-Check `
        "FFX SSSR core header present" `
        ($sssrHeader -match "FFX_SSSR_HierarchicalRaymarch") `
        "contains=$($sssrHeader -match 'FFX_SSSR_HierarchicalRaymarch')" `
        "true"
    $sssrSelectPatchPresent = $sssrHeader -match "select\(direction != 0"
    $dnsrSelectPatchPresent =
        $dnsrCommon -match "select\(\(round_down == value\)"
    $checks += New-Check `
        "FFX SSSR DXC vector select patch present" `
        ($sssrSelectPatchPresent -and $dnsrSelectPatchPresent) `
        "sssrSelect=$sssrSelectPatchPresent,dnsrSelect=$dnsrSelectPatchPresent" `
        "true/true"
    $checks += New-Check `
        "CMake compiles FFX SSSR shaders with DXC" `
        ($cmakeSource -match "DXC_EXECUTABLE" -and
            $cmakeSource -match "FFX_SSSR_HLSL_SHADERS" -and
            $cmakeSource -match "ffx_sssr_") `
        "dxc=$($cmakeSource -match 'DXC_EXECUTABLE'),list=$($cmakeSource -match 'FFX_SSSR_HLSL_SHADERS'),prefix=$($cmakeSource -match 'ffx_sssr_')" `
        "true/true/true"

    $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
    $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
    return [pscustomobject]@{
        lane = "ffx-sssr-static-contract"
        verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
        passCount = $passCount
        failCount = $failCount
        checks = $checks
    }
}

function Invoke-RuntimeLane {
    param(
        [string]$Name,
        [string]$Executable,
        [hashtable]$Environment,
        [uint32]$ExpectedRequestedProvider,
        [uint32]$ExpectedActiveProvider,
        [uint32]$ExpectedDispatchReady,
        [uint32]$ExpectedRuntimeActive,
        [bool]$ExpectPrepareDispatch,
        [uint32]$ExpectedFallbackReason
    )

    $laneDirectory = Join-Path $OutputDirectory $Name
    New-Item -ItemType Directory -Force -Path $laneDirectory | Out-Null
    $csvPath = Join-Path $laneDirectory "ffx_sssr_backend.csv"
    $stdoutPath = Join-Path $laneDirectory "stdout.log"
    $stderrPath = Join-Path $laneDirectory "stderr.log"
    Remove-Item -LiteralPath $csvPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stdoutPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue

    $Environment["SE_BENCHMARK_CSV"] = $csvPath
    $previous = Set-ProcessEnvironment -Values $Environment
    try {
        $process = Start-Process `
            -FilePath $Executable `
            -PassThru `
            -Wait `
            -WindowStyle Hidden `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath
        if ($process.ExitCode -ne 0) {
            throw "$Name exited with code $($process.ExitCode)"
        }
    } finally {
        Restore-ProcessEnvironment -Values $previous
    }

    $rows = @()
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $csvPath) {
            $rows = @(Import-Csv -LiteralPath $csvPath)
            if ($rows.Count -gt 0) {
                break
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if ($rows.Count -eq 0) {
        throw "$Name did not emit benchmark CSV: $csvPath"
    }

    $last = $rows[-1]
    $requestedProvider = Get-UIntMetric $last "ssr_backend_requested_provider"
    $activeProvider = Get-UIntMetric $last "ssr_backend_active_provider"
    $contractVersion = Get-UIntMetric $last "ssr_ffx_sssr_contract_version"
    $sourceReady = Get-UIntMetric $last "ssr_ffx_sssr_source_ready"
    $shaderBuild = Get-UIntMetric $last "ssr_ffx_sssr_shader_build_integrated"
    $shaderCount = Get-UIntMetric $last "ssr_ffx_sssr_shader_count"
    $denoiserReady = Get-UIntMetric $last "ssr_ffx_sssr_denoiser_dependency_ready"
    $spdReady = Get-UIntMetric $last "ssr_ffx_sssr_spd_dependency_ready"
    $prepareResourcesReady = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_resources_ready"
    $prepareDescriptorSetsReady = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_descriptor_sets_ready"
    $preparePipelineReady = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_pipeline_ready"
    $prepareDispatches = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_dispatches"
    $prepareDescriptorBinds = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_descriptor_binds"
    $prepareBindDispatches = Get-UIntMetric $last "ffx_sssr_prepare_indirect_args_dispatches"
    $prepareBindDescriptorBinds = Get-UIntMetric $last "ffx_sssr_prepare_indirect_args_descriptor_binds"
    $prepareBufferBytes = Get-UIntMetric $last "ssr_ffx_sssr_prepare_indirect_args_buffer_bytes"
    $dispatchReady = Get-UIntMetric $last "ssr_ffx_sssr_runtime_dispatch_ready"
    $runtimeActive = Get-UIntMetric $last "ssr_ffx_sssr_runtime_active"
    $fallbackReason = Get-UIntMetric $last "ssr_ffx_sssr_fallback_reason"
    $prepareDispatchStateMatches = $false
    $prepareExpectedLabel = "0/0 mirrored"
    if ($ExpectPrepareDispatch) {
        $prepareDispatchStateMatches =
            $prepareDispatches -gt 0 -and
            $prepareDescriptorBinds -gt 0 -and
            $prepareBindDispatches -eq $prepareDispatches -and
            $prepareBindDescriptorBinds -eq $prepareDescriptorBinds
        $prepareExpectedLabel = ">0/>0 mirrored"
    } else {
        $prepareDispatchStateMatches =
            $prepareDispatches -eq 0 -and
            $prepareDescriptorBinds -eq 0 -and
            $prepareBindDispatches -eq 0 -and
            $prepareBindDescriptorBinds -eq 0
    }

    $checks = @(
        (New-Check "$Name requested provider" `
            ($requestedProvider -eq $ExpectedRequestedProvider) `
            "$requestedProvider" "$ExpectedRequestedProvider"),
        (New-Check "$Name active provider" `
            ($activeProvider -eq $ExpectedActiveProvider) `
            "$activeProvider" "$ExpectedActiveProvider"),
        (New-Check "$Name FFX source contract ready" `
            ($contractVersion -eq 2 -and $sourceReady -eq 1) `
            "contract=$contractVersion,source=$sourceReady" "2/1"),
        (New-Check "$Name FFX shader build integrated" `
            ($shaderBuild -eq 1 -and $shaderCount -eq 8) `
            "build=$shaderBuild,count=$shaderCount" "1/8"),
        (New-Check "$Name FFX dependencies ready" `
            ($denoiserReady -eq 1 -and $spdReady -eq 1) `
            "dnsr=$denoiserReady,spd=$spdReady" "1/1"),
        (New-Check "$Name prepare-args resources ready" `
            ($prepareResourcesReady -eq 1 -and
                $prepareDescriptorSetsReady -eq 1 -and
                $preparePipelineReady -eq 1 -and
                $prepareBufferBytes -gt 0) `
            "resources=$prepareResourcesReady,sets=$prepareDescriptorSetsReady,pipeline=$preparePipelineReady,bytes=$prepareBufferBytes" `
            "1/1/1/>0"),
        (New-Check "$Name prepare-args dispatch/bind state" `
            $prepareDispatchStateMatches `
            "stats=$prepareDispatches/$prepareDescriptorBinds,binds=$prepareBindDispatches/$prepareBindDescriptorBinds" `
            $prepareExpectedLabel),
        (New-Check "$Name runtime dispatch state" `
            ($dispatchReady -eq $ExpectedDispatchReady -and
                $runtimeActive -eq $ExpectedRuntimeActive) `
            "dispatch=$dispatchReady,active=$runtimeActive" `
            "$ExpectedDispatchReady/$ExpectedRuntimeActive"),
        (New-Check "$Name fallback reason" `
            ($fallbackReason -eq $ExpectedFallbackReason) `
            "$fallbackReason" "$ExpectedFallbackReason")
    )
    $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
    $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
    return [pscustomobject]@{
        lane = $Name
        executable = $Executable
        csv = $csvPath
        verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
        passCount = $passCount
        failCount = $failCount
        metrics = [pscustomobject]@{
            requestedProvider = $requestedProvider
            activeProvider = $activeProvider
            contractVersion = $contractVersion
            sourceReady = $sourceReady
            shaderBuildIntegrated = $shaderBuild
            shaderCount = $shaderCount
            denoiserDependencyReady = $denoiserReady
            spdDependencyReady = $spdReady
            prepareIndirectArgsResourcesReady = $prepareResourcesReady
            prepareIndirectArgsDescriptorSetsReady = $prepareDescriptorSetsReady
            prepareIndirectArgsPipelineReady = $preparePipelineReady
            prepareIndirectArgsDispatches = $prepareDispatches
            prepareIndirectArgsDescriptorBinds = $prepareDescriptorBinds
            prepareIndirectArgsBindDispatches = $prepareBindDispatches
            prepareIndirectArgsBindDescriptorBinds = $prepareBindDescriptorBinds
            prepareIndirectArgsBufferBytes = $prepareBufferBytes
            runtimeDispatchReady = $dispatchReady
            runtimeActive = $runtimeActive
            fallbackReason = $fallbackReason
        }
        checks = $checks
    }
}

$forwardExecutable = Resolve-FullPath $ForwardExecutablePath
$showcaseExecutable = Resolve-FullPath $ShowcaseExecutablePath
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

if (!$SkipBuild) {
    Invoke-Build
}
foreach ($executable in @($forwardExecutable, $showcaseExecutable)) {
    if (!(Test-Path -LiteralPath $executable)) {
        throw "Missing executable: $executable"
    }
}

$reports = @()
$reports += Invoke-StaticChecks

$common = @{
    SE_WINDOW_HIDDEN = "1"
    SE_HIDE_IMGUI = "1"
    SE_VISUAL_QA_HIDE_IMGUI = "1"
    SE_BENCHMARK_WARMUP_FRAMES = "4"
    SE_BENCHMARK_FRAMES = "4"
    SE_AUTO_EXIT_FRAMES = "10"
    SE_FORWARD3D_AA_MODE = "taa"
    SE_SSR = "1"
}

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-ffx-backend-contract" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "ffx-sssr"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0

$reports += Invoke-RuntimeLane `
    -Name "forward3d-fbx-ffx-backend-contract" `
    -Executable $forwardExecutable `
    -Environment ($common.Clone() + @{
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "1"
        SE_SSR_BACKEND = "ffx-sssr"
    }) `
    -ExpectedRequestedProvider 1 `
    -ExpectedActiveProvider 1 `
    -ExpectedDispatchReady 1 `
    -ExpectedRuntimeActive 1 `
    -ExpectPrepareDispatch $true `
    -ExpectedFallbackReason 0

$reports += Invoke-RuntimeLane `
    -Name "lighting-showcase-internal-backend-control" `
    -Executable $showcaseExecutable `
    -Environment ($common.Clone() + @{
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION = "0"
        SE_SSR_BACKEND = "selfengine"
    }) `
    -ExpectedRequestedProvider 0 `
    -ExpectedActiveProvider 0 `
    -ExpectedDispatchReady 0 `
    -ExpectedRuntimeActive 0 `
    -ExpectPrepareDispatch $false `
    -ExpectedFallbackReason 1

$passCount = ($reports | ForEach-Object { $_.passCount } | Measure-Object -Sum).Sum
$failCount = ($reports | ForEach-Object { $_.failCount } | Measure-Object -Sum).Sum
$summary = [pscustomobject]@{
    generatedAt = (Get-Date).ToString("o")
    outputDirectory = $OutputDirectory
    verdict = if ($failCount -eq 0) { "pass" } else { "fail" }
    passCount = [int]$passCount
    failCount = [int]$failCount
    reports = $reports
}
$summary | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $OutputDirectory "summary.json") -Encoding utf8
$summary

if ($Strict -and $failCount -gt 0) {
    exit 1
}
