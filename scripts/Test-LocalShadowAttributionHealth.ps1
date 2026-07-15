param(
    [string]$ExecutablePath = "build\Debug\SelfEngineLightingShowcase.exe",
    [string]$OutputDirectory = "out\local_shadow_attribution",
    [string]$LightIndices = "0-10",
    [ValidateSet("low", "medium", "high", "ultra")]
    [string]$ShadowQuality = "high",
    [int]$WarmupFrames = 2,
    [int]$CaptureFrames = 3,
    [int]$AutoExitFrames = 8,
    [switch]$SkipBuild,
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Convert-LightIndices {
    param([Parameter(Mandatory = $true)][string]$Spec)

    $indices = [System.Collections.Generic.List[int]]::new()
    foreach ($part in ($Spec -split ",")) {
        $token = $part.Trim()
        if ($token -eq "") {
            continue
        }
        if ($token -match "^(\d+)-(\d+)$") {
            $start = [int]$Matches[1]
            $end = [int]$Matches[2]
            if ($end -lt $start) {
                throw "Invalid LightIndices range '$token'"
            }
            for ($i = $start; $i -le $end; ++$i) {
                if (-not $indices.Contains($i)) {
                    $indices.Add($i) | Out-Null
                }
            }
        } elseif ($token -match "^\d+$") {
            $value = [int]$token
            if (-not $indices.Contains($value)) {
                $indices.Add($value) | Out-Null
            }
        } else {
            throw "Invalid LightIndices token '$token'. Use forms like 0-10 or 0,3,7."
        }
    }

    if ($indices.Count -eq 0) {
        throw "LightIndices did not resolve to any indices."
    }

    return @($indices)
}

function Get-Number {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Name,
        [double]$Default = [double]::NaN
    )

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value -or "$($property.Value)" -eq "") {
        return $Default
    }

    return [double]::Parse("$($property.Value)", [Globalization.CultureInfo]::InvariantCulture)
}

function Get-Text {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return ""
    }

    return "$($property.Value)"
}

function Measure-Number {
    param(
        [Parameter(Mandatory = $true)]$Rows,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $values = @(
        foreach ($row in $Rows) {
            $value = Get-Number -Row $row -Name $Name
            if (-not [double]::IsNaN($value)) {
                $value
            }
        }
    )

    if ($values.Count -eq 0) {
        return [pscustomobject]@{
            present = $false
            min = $null
            max = $null
            first = $null
            last = $null
        }
    }

    return [pscustomobject]@{
        present = $true
        min = ($values | Measure-Object -Minimum).Minimum
        max = ($values | Measure-Object -Maximum).Maximum
        first = $values[0]
        last = $values[$values.Count - 1]
    }
}

function Add-Check {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Status,
        $Actual,
        $Expected,
        [string]$Detail = ""
    )

    $Checks.Add([pscustomobject]@{
        area = $Area
        name = $Name
        status = $Status
        actual = $Actual
        expected = $Expected
        detail = $Detail
    }) | Out-Null
}

function Invoke-WithEnvironment {
    param(
        [Parameter(Mandatory = $true)][string[]]$ManagedKeys,
        [Parameter(Mandatory = $true)][hashtable]$Environment,
        [Parameter(Mandatory = $true)][scriptblock]$Script
    )

    $previous = @{}
    foreach ($key in $ManagedKeys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, $null, "Process")
    }

    try {
        foreach ($key in $Environment.Keys) {
            [Environment]::SetEnvironmentVariable(
                [string]$key,
                [string]$Environment[$key],
                "Process"
            )
        }
        & $Script
    } finally {
        foreach ($key in $ManagedKeys) {
            [Environment]::SetEnvironmentVariable($key, $previous[$key], "Process")
        }
    }
}

function Convert-LightKindName {
    param([double]$Kind)

    switch ([int]$Kind) {
        0 { return "directional" }
        1 { return "point" }
        2 { return "spot" }
        3 { return "rect" }
        default { return "unknown-$([int]$Kind)" }
    }
}

function New-LightAttributionReport {
    param(
        [Parameter(Mandatory = $true)][int]$LightIndex,
        [Parameter(Mandatory = $true)][string]$CsvPath,
        [AllowNull()]$RunError
    )

    $checks = [System.Collections.Generic.List[object]]::new()
    if ($RunError) {
        Add-Check -Checks $checks -Area "run" -Name "lighting showcase attribution run" `
            -Status "fail" -Actual $RunError -Expected "exit code 0"
        return [pscustomobject]@{
            lightIndex = $LightIndex
            csv = $CsvPath
            verdict = "fail"
            passCount = 0
            warnCount = 0
            failCount = 1
            metrics = [pscustomobject]@{}
            checks = @($checks)
        }
    }

    if (-not (Test-Path -LiteralPath $CsvPath)) {
        Add-Check -Checks $checks -Area "run" -Name "benchmark csv" `
            -Status "fail" -Actual "missing" -Expected $CsvPath
        return [pscustomobject]@{
            lightIndex = $LightIndex
            csv = $CsvPath
            verdict = "fail"
            passCount = 0
            warnCount = 0
            failCount = 1
            metrics = [pscustomobject]@{}
            checks = @($checks)
        }
    }

    $rows = @(Import-Csv -LiteralPath $CsvPath)
    if ($rows.Count -eq 0) {
        Add-Check -Checks $checks -Area "run" -Name "captured rows" `
            -Status "fail" -Actual 0 -Expected "> 0"
        return [pscustomobject]@{
            lightIndex = $LightIndex
            csv = $CsvPath
            verdict = "fail"
            passCount = 0
            warnCount = 0
            failCount = 1
            metrics = [pscustomobject]@{}
            checks = @($checks)
        }
    }

    $lastRow = $rows[$rows.Count - 1]
    $valid = Measure-Number -Rows $rows -Name "local_shadow_attribution_light_valid"
    $kind = Measure-Number -Rows $rows -Name "local_shadow_attribution_light_kind"
    $requested = Measure-Number -Rows $rows -Name "local_shadow_attribution_requested_tiles"
    $assigned = Measure-Number -Rows $rows -Name "local_shadow_attribution_assigned_tiles"
    $dropped = Measure-Number -Rows $rows -Name "local_shadow_attribution_dropped_tiles"
    $cacheHit = Measure-Number -Rows $rows -Name "local_shadow_attribution_cache_hit_tiles"
    $cacheMiss = Measure-Number -Rows $rows -Name "local_shadow_attribution_cache_miss_tiles"
    $recordedTilePasses = Measure-Number -Rows $rows -Name "local_shadow_attribution_recorded_tile_passes"
    $recordedDraws = Measure-Number -Rows $rows -Name "local_shadow_attribution_recorded_draws"
    $candidateDraws = Measure-Number -Rows $rows -Name "local_shadow_attribution_candidate_draws"
    $uniqueCasters = Measure-Number -Rows $rows -Name "local_shadow_attribution_unique_casters"
    $shadowEnabled = Measure-Number -Rows $rows -Name "local_shadow_attribution_shadow_enabled"
    $matchesFilter = Measure-Number -Rows $rows -Name "local_shadow_attribution_matches_generation_filter"
    $debugIndex = Measure-Number -Rows $rows -Name "local_shadow_debug_light_index"
    $frameGraphIssues = Measure-Number -Rows $rows -Name "framegraph_validation_issues"
    $rectBudgetLimited = Measure-Number -Rows $rows -Name "local_shadow_rect_budget_limited_sample_tiles"
    $rectExtraSamples = Measure-Number -Rows $rows -Name "local_shadow_rect_extra_sample_tiles"
    $rectMaxSamples = Measure-Number -Rows $rows -Name "local_shadow_rect_max_sample_tiles"
    $rectSamplePattern = Measure-Number -Rows $rows -Name "local_shadow_rect_sample_pattern"

    $signatures = @(
        $rows |
            ForEach-Object { Get-Text -Row $_ -Name "local_shadow_attribution_caster_signature" } |
            Where-Object { $_ -ne "" } |
            Select-Object -Unique
    )
    $tileCandidateDraws = Get-Text -Row $lastRow -Name "local_shadow_attribution_tile_candidate_draws"
    $casterSummary = Get-Text -Row $lastRow -Name "local_shadow_attribution_caster_summary"
    $lightKindName = if ($kind.present) { Convert-LightKindName -Kind $kind.max } else { "missing" }
    $lightIsValid = $valid.present -and [double]$valid.max -ge 1

    Add-Check -Checks $checks -Area "run" -Name "captured frame rows" `
        -Status ($(if ($rows.Count -gt 0) { "pass" } else { "fail" })) `
        -Actual $rows.Count -Expected "> 0"
    Add-Check -Checks $checks -Area "framegraph" -Name "framegraph aggregate validation" `
        -Status ($(if ($frameGraphIssues.present -and [double]$frameGraphIssues.max -le 0) { "pass" } else { "warn" })) `
        -Actual $frameGraphIssues.max -Expected "0 preferred"

    if (-not $lightIsValid) {
        Add-Check -Checks $checks -Area "selection" -Name "selected index exists in current scene" `
            -Status "warn" -Actual "lightIndex=$LightIndex valid=$($valid.max)" `
            -Expected "valid local light index"

        $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
        $warnCount = @($checks | Where-Object { $_.status -eq "warn" }).Count
        $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
        $verdict = if ($failCount -gt 0) {
            "fail"
        } elseif ($warnCount -gt 0) {
            "warn"
        } else {
            "pass"
        }

        return [pscustomobject]@{
            lightIndex = $LightIndex
            lightKind = "invalid"
            csv = $CsvPath
            verdict = $verdict
            passCount = $passCount
            warnCount = $warnCount
            failCount = $failCount
            metrics = [pscustomobject]@{
                rows = $rows.Count
                requestedTiles = $requested.max
                assignedTilesMin = $assigned.min
                droppedTilesMax = $dropped.max
                cacheHitTilesMax = $cacheHit.max
                cacheMissTilesMax = $cacheMiss.max
                recordedTilePassesMax = $recordedTilePasses.max
                recordedDrawsMax = $recordedDraws.max
                candidateDrawsMax = $candidateDraws.max
                uniqueCastersMax = $uniqueCasters.max
                casterSignatureCount = $signatures.Count
                tileCandidateDraws = $tileCandidateDraws
                casterSummary = $casterSummary
                rectMaxSampleTiles = $rectMaxSamples.max
                rectSamplePattern = $rectSamplePattern.max
                rectExtraSampleTiles = $rectExtraSamples.max
                rectBudgetLimitedSampleTiles = $rectBudgetLimited.max
            }
            checks = @($checks)
        }
    }

    Add-Check -Checks $checks -Area "selection" -Name "selected light index is valid" `
        -Status ($(if ([double]$valid.min -ge 1) { "pass" } else { "fail" })) `
        -Actual $valid.min -Expected "1"
    Add-Check -Checks $checks -Area "selection" -Name "debug light index matches requested index" `
        -Status ($(if ($debugIndex.present -and [double]$debugIndex.min -eq $LightIndex -and [double]$debugIndex.max -eq $LightIndex) { "pass" } else { "fail" })) `
        -Actual "min=$($debugIndex.min) max=$($debugIndex.max)" -Expected $LightIndex
    Add-Check -Checks $checks -Area "selection" -Name "viewed light matches tile generation filter" `
        -Status ($(if ($matchesFilter.present -and [double]$matchesFilter.min -ge 1) { "pass" } else { "fail" })) `
        -Actual $matchesFilter.min -Expected "1"
    Add-Check -Checks $checks -Area "tiles" -Name "local shadow is enabled for selected light" `
        -Status ($(if ($shadowEnabled.present -and [double]$shadowEnabled.min -ge 1) { "pass" } else { "fail" })) `
        -Actual $shadowEnabled.min -Expected "1"
    Add-Check -Checks $checks -Area "tiles" -Name "requested tile count is nonzero" `
        -Status ($(if ($requested.present -and [double]$requested.max -gt 0) { "pass" } else { "fail" })) `
        -Actual $requested.max -Expected "> 0"
    Add-Check -Checks $checks -Area "tiles" -Name "assigned tiles satisfy request" `
        -Status ($(if ($requested.present -and $assigned.present -and [double]$assigned.min -ge [double]$requested.max) { "pass" } else { "fail" })) `
        -Actual "requested=$($requested.max) assignedMin=$($assigned.min)" -Expected "assigned >= requested"
    Add-Check -Checks $checks -Area "tiles" -Name "no selected-light tile drops" `
        -Status ($(if ($dropped.present -and [double]$dropped.max -le 0) { "pass" } else { "fail" })) `
        -Actual $dropped.max -Expected "0"
    Add-Check -Checks $checks -Area "casters" -Name "selected light has caster candidates" `
        -Status ($(if ($uniqueCasters.present -and [double]$uniqueCasters.max -gt 0) { "pass" } else { "warn" })) `
        -Actual $uniqueCasters.max -Expected "> 0 for shadow attribution"
    Add-Check -Checks $checks -Area "casters" -Name "caster signature is stable across captured frames" `
        -Status ($(if ($signatures.Count -le 1) { "pass" } else { "fail" })) `
        -Actual ($signatures -join "|") -Expected "one unique signature"
    Add-Check -Checks $checks -Area "recording" -Name "tile pass recording is active or cached" `
        -Status ($(if (($recordedTilePasses.present -and [double]$recordedTilePasses.max -gt 0) -or ($cacheHit.present -and [double]$cacheHit.max -gt 0)) { "pass" } else { "fail" })) `
        -Actual "recorded=$($recordedTilePasses.max) cacheHit=$($cacheHit.max)" `
        -Expected "recorded > 0 or cacheHit > 0"

    $kindValue = if ($kind.present) { [int]$kind.max } else { -1 }
    $expectedCeilingLight = switch ($LightIndex) {
        0 { [pscustomobject]@{ kind = 1; requestedTiles = 6; name = "ceiling point" } }
        1 { [pscustomobject]@{ kind = 2; requestedTiles = 1; name = "ceiling warm spot" } }
        2 { [pscustomobject]@{ kind = 2; requestedTiles = 1; name = "ceiling cool spot" } }
        default { $null }
    }
    if ($null -ne $expectedCeilingLight) {
        Add-Check -Checks $checks -Area "ceiling fixtures" -Name "$($expectedCeilingLight.name) kind is preserved" `
            -Status ($(if ($kindValue -eq $expectedCeilingLight.kind) { "pass" } else { "fail" })) `
            -Actual $lightKindName -Expected $expectedCeilingLight.kind
        Add-Check -Checks $checks -Area "ceiling fixtures" -Name "$($expectedCeilingLight.name) requests its full shadow footprint" `
            -Status ($(if ($requested.present -and [double]$requested.max -eq $expectedCeilingLight.requestedTiles) { "pass" } else { "fail" })) `
            -Actual $requested.max -Expected $expectedCeilingLight.requestedTiles
    }
    if ($kindValue -eq 3) {
        Add-Check -Checks $checks -Area "rect" -Name "rect-light max sample tier is reported" `
            -Status ($(if ($rectMaxSamples.present -and [double]$rectMaxSamples.max -ge 2) { "pass" } else { "warn" })) `
            -Actual $rectMaxSamples.max -Expected ">= 2"
        Add-Check -Checks $checks -Area "rect" -Name "4-sample rect shadows use 2x2 surface pattern" `
            -Status ($(if ($rectSamplePattern.present -and [double]$requested.max -ge 4 -and [double]$rectSamplePattern.max -eq 1) { "pass" } elseif ($rectSamplePattern.present -and [double]$requested.max -lt 4) { "pass" } else { "fail" })) `
            -Actual "requested=$($requested.max) pattern=$($rectSamplePattern.max)" `
            -Expected "pattern 1 when requested >= 4"
        Add-Check -Checks $checks -Area "rect" -Name "rect-light adaptive extra samples are reported" `
            -Status ($(if ($rectExtraSamples.present) { "pass" } else { "warn" })) `
            -Actual $rectExtraSamples.max -Expected "CSV signal present"
        Add-Check -Checks $checks -Area "rect" -Name "rect-light budget-limited count is visible" `
            -Status ($(if ($rectBudgetLimited.present) { "pass" } else { "warn" })) `
            -Actual $rectBudgetLimited.max -Expected "CSV signal present"
    }

    $passCount = @($checks | Where-Object { $_.status -eq "pass" }).Count
    $warnCount = @($checks | Where-Object { $_.status -eq "warn" }).Count
    $failCount = @($checks | Where-Object { $_.status -eq "fail" }).Count
    $verdict = if ($failCount -gt 0) {
        "fail"
    } elseif ($warnCount -gt 0) {
        "warn"
    } else {
        "pass"
    }

    return [pscustomobject]@{
        lightIndex = $LightIndex
        lightKind = $lightKindName
        csv = $CsvPath
        verdict = $verdict
        passCount = $passCount
        warnCount = $warnCount
        failCount = $failCount
        metrics = [pscustomobject]@{
            rows = $rows.Count
            requestedTiles = $requested.max
            assignedTilesMin = $assigned.min
            droppedTilesMax = $dropped.max
            cacheHitTilesMax = $cacheHit.max
            cacheMissTilesMax = $cacheMiss.max
            recordedTilePassesMax = $recordedTilePasses.max
            recordedDrawsMax = $recordedDraws.max
            candidateDrawsMax = $candidateDraws.max
            uniqueCastersMax = $uniqueCasters.max
            casterSignatureCount = $signatures.Count
            tileCandidateDraws = $tileCandidateDraws
            casterSummary = $casterSummary
            rectMaxSampleTiles = $rectMaxSamples.max
            rectSamplePattern = $rectSamplePattern.max
            rectExtraSampleTiles = $rectExtraSamples.max
            rectBudgetLimitedSampleTiles = $rectBudgetLimited.max
        }
        checks = @($checks)
    }
}

if (-not $SkipBuild) {
    $buildCommand =
        'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cd /d "{0}\build" && MSBuild SelfEngineLightingShowcase.vcxproj /p:Configuration=Debug /v:minimal /nologo' -f $repoRoot
    cmd /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "SelfEngineLightingShowcase build failed with exit code $LASTEXITCODE"
    }
}

$resolvedExecutable = Resolve-FullPath -Path $ExecutablePath
if (-not (Test-Path -LiteralPath $resolvedExecutable)) {
    throw "Executable not found: $resolvedExecutable"
}
$resolvedOutput = Resolve-FullPath -Path $OutputDirectory
New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null

$managedKeys = @(
    "SE_WINDOW_WIDTH",
    "SE_WINDOW_HEIGHT",
    "SE_WINDOW_BORDERLESS",
    "SE_VISUAL_QA_HIDE_IMGUI",
    "SE_HIDE_IMGUI",
    "SE_BENCHMARK_SCENE",
    "SE_FORCE_LIGHTING_SHOWCASE",
    "SE_FORWARD3D_SHADOW_PROFILE",
    "SE_SHADOW_QUALITY",
    "SE_FORWARD3D_AA_MODE",
    "SE_RENDER_VIEW",
    "SE_LOCAL_SHADOW_DEBUG_LIGHT_INDEX",
    "SE_LOCAL_SHADOW_ONLY_LIGHT_INDEX",
    "SE_BENCHMARK_WARMUP_FRAMES",
    "SE_BENCHMARK_FRAMES",
    "SE_AUTO_EXIT_FRAMES",
    "SE_BENCHMARK_CSV"
)

$reports = [System.Collections.Generic.List[object]]::new()
$indices = Convert-LightIndices -Spec $LightIndices
foreach ($index in $indices) {
    $lightOutput = Join-Path $resolvedOutput ("light_{0:00}" -f $index)
    New-Item -ItemType Directory -Force -Path $lightOutput | Out-Null
    $csvPath = Join-Path $lightOutput "local_shadow_attribution.csv"
    Remove-Item -LiteralPath $csvPath -ErrorAction SilentlyContinue

    $runError = $null
    $environment = @{
        SE_WINDOW_WIDTH = "1280"
        SE_WINDOW_HEIGHT = "720"
        SE_WINDOW_BORDERLESS = "0"
        SE_VISUAL_QA_HIDE_IMGUI = "1"
        SE_HIDE_IMGUI = "1"
        SE_BENCHMARK_SCENE = "lighting-showcase"
        SE_FORCE_LIGHTING_SHOWCASE = "1"
        SE_FORWARD3D_SHADOW_PROFILE = "production"
        SE_SHADOW_QUALITY = $ShadowQuality
        SE_FORWARD3D_AA_MODE = "taa"
        SE_RENDER_VIEW = "local-shadow-selected"
        SE_LOCAL_SHADOW_DEBUG_LIGHT_INDEX = [string]$index
        SE_LOCAL_SHADOW_ONLY_LIGHT_INDEX = [string]$index
        SE_BENCHMARK_WARMUP_FRAMES = [string]$WarmupFrames
        SE_BENCHMARK_FRAMES = [string]$CaptureFrames
        SE_AUTO_EXIT_FRAMES = [string]$AutoExitFrames
        SE_BENCHMARK_CSV = $csvPath
    }

    try {
        Invoke-WithEnvironment -ManagedKeys $managedKeys -Environment $environment -Script {
            & $resolvedExecutable | Out-Host
            if ($LASTEXITCODE -ne 0) {
                throw "SelfEngineLightingShowcase exited with code $LASTEXITCODE"
            }
        }
    } catch {
        $runError = $_.Exception.Message
    }

    $reports.Add((New-LightAttributionReport -LightIndex $index -CsvPath $csvPath -RunError $runError)) | Out-Null
}

$totalPass = @($reports | ForEach-Object { $_.passCount } | Measure-Object -Sum).Sum
$totalWarn = @($reports | ForEach-Object { $_.warnCount } | Measure-Object -Sum).Sum
$totalFail = @($reports | ForEach-Object { $_.failCount } | Measure-Object -Sum).Sum
$overall = if ([int]$totalFail -gt 0) {
    "fail"
} elseif ([int]$totalWarn -gt 0) {
    "warn"
} else {
    "pass"
}

$summary = [pscustomobject]@{
    generatedAt = (Get-Date).ToString("o")
    executable = $resolvedExecutable
    outputDirectory = $resolvedOutput
    shadowQuality = $ShadowQuality
    lightIndices = @($indices)
    verdict = $overall
    passCount = [int]$totalPass
    warnCount = [int]$totalWarn
    failCount = [int]$totalFail
    reports = @($reports)
}

$jsonPath = Join-Path $resolvedOutput "local_shadow_attribution_health.json"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$summary

if ($Strict -and $overall -ne "pass") {
    throw "Local shadow attribution health verdict is $overall. See $jsonPath"
}
