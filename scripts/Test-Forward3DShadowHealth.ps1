param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "out\shadow_health",
    [string]$ShadowQuality = "high",
    [int]$WarmupFrames = 2,
    [int]$CaptureFrames = 8,
    [int]$AutoExitFrames = 14,
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

function Test-Column {
    param(
        [Parameter(Mandatory = $true)]$Rows,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($Rows.Count -eq 0) {
        return $false
    }

    return $null -ne $Rows[0].PSObject.Properties[$Name]
}

function Get-Number {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value -or "$($property.Value)" -eq "") {
        return $null
    }

    return [double]::Parse("$($property.Value)", [Globalization.CultureInfo]::InvariantCulture)
}

function Measure-Column {
    param(
        [Parameter(Mandatory = $true)]$Rows,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if (-not (Test-Column -Rows $Rows -Name $Name)) {
        return [pscustomobject]@{
            present = $false
            min = $null
            max = $null
            first = $null
            last = $null
        }
    }

    $values = @(
        foreach ($row in $Rows) {
            $value = Get-Number -Row $row -Name $Name
            if ($null -ne $value) {
                $value
            }
        }
    )

    if ($values.Count -eq 0) {
        return [pscustomobject]@{
            present = $true
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

function Add-MinCheck {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)]$Metrics,
        [Parameter(Mandatory = $true)][string]$Column,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$Minimum,
        [string]$MissingStatus = "warn"
    )

    $metric = $Metrics[$Column]
    if (-not $metric.present) {
        Add-Check -Checks $Checks -Area $Area -Name $Name -Status $MissingStatus `
            -Actual "missing:$Column" -Expected ">= $Minimum" `
            -Detail "CSV does not expose this signal yet."
        return
    }

    $status = if ($null -ne $metric.max -and [double]$metric.max -ge $Minimum) { "pass" } else { "fail" }
    Add-Check -Checks $Checks -Area $Area -Name $Name -Status $status `
        -Actual $metric.max -Expected ">= $Minimum"
}

function Add-MaxCheck {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)]$Metrics,
        [Parameter(Mandatory = $true)][string]$Column,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$Maximum,
        [string]$MissingStatus = "warn"
    )

    $metric = $Metrics[$Column]
    if (-not $metric.present) {
        Add-Check -Checks $Checks -Area $Area -Name $Name -Status $MissingStatus `
            -Actual "missing:$Column" -Expected "<= $Maximum" `
            -Detail "CSV does not expose this signal yet."
        return
    }

    $status = if ($null -ne $metric.max -and [double]$metric.max -le $Maximum) { "pass" } else { "fail" }
    Add-Check -Checks $Checks -Area $Area -Name $Name -Status $status `
        -Actual $metric.max -Expected "<= $Maximum"
}

function Add-EqualityCheck {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)]$Metrics,
        [Parameter(Mandatory = $true)][string]$Column,
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double]$Expected,
        [double]$Tolerance = 0.0001,
        [string]$MissingStatus = "warn"
    )

    $metric = $Metrics[$Column]
    if (-not $metric.present) {
        Add-Check -Checks $Checks -Area $Area -Name $Name -Status $MissingStatus `
            -Actual "missing:$Column" -Expected $Expected `
            -Detail "CSV does not expose this signal yet."
        return
    }

    $delta = if ($null -ne $metric.max) { [Math]::Abs([double]$metric.max - $Expected) } else { [double]::PositiveInfinity }
    $status = if ($delta -le $Tolerance) { "pass" } else { "fail" }
    Add-Check -Checks $Checks -Area $Area -Name $Name -Status $status `
        -Actual $metric.max -Expected "$Expected +/- $Tolerance"
}

function New-ShadowHealthReport {
    param(
        [Parameter(Mandatory = $true)][string]$Quality,
        [Parameter(Mandatory = $true)][string]$CsvPath,
        [AllowNull()]$RunError
    )

    $checks = [System.Collections.Generic.List[object]]::new()

    if ($RunError) {
        Add-Check -Checks $checks -Area "run" -Name "shadow regression executable" `
            -Status "fail" -Actual $RunError -Expected "exit code 0"
        return [pscustomobject]@{
            quality = $Quality
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
            quality = $Quality
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
            quality = $Quality
            csv = $CsvPath
            verdict = "fail"
            passCount = 0
            warnCount = 0
            failCount = 1
            metrics = [pscustomobject]@{}
            checks = @($checks)
        }
    }

    $columns = @(
        "framegraph_validation_issues",
        "framegraph_validation_missing_resource_refs",
        "framegraph_validation_read_before_first_write",
        "framegraph_validation_duplicate_pass_ids",
        "framegraph_validation_duplicate_resource_ids",
        "framegraph_validation_active_writes_planned_resources",
        "framegraph_validation_write_only_roadmap_resources",
        "framegraph_validation_unused_physical_resources",
        "runtime_import_animation_playback_changed_bone_palette_entry_count",
        "main_skinned_conservative_bounds",
        "shadow_skinned_conservative_bounds",
        "main_culled",
        "main_visible",
        "shadow_visible",
        "shadow_cascade_active_count",
        "shadow_cascade_atlas_allocated",
        "shadow_cascade_atlas_passes",
        "shadow_cascade_atlas_draws",
        "shadow_cascade_receiver_guard",
        "shadow_pcf_kernel_radius",
        "shadow_pcss_strength",
        "local_shadow_atlas_allocated",
        "local_shadow_rect_light_count",
        "local_shadow_requested_tiles",
        "local_shadow_assigned_tiles",
        "local_shadow_dropped_tiles",
        "local_shadow_recorded_tile_passes",
        "local_shadow_recorded_draws",
        "local_shadow_cache_hit_tiles",
        "local_shadow_cache_skipped_tiles",
        "local_shadow_cache_dynamic_skinned_caster_tiles",
        "local_shadow_resolve_enabled",
        "local_shadow_pcf_radius",
        "local_shadow_pcf_kernel_radius",
        "local_shadow_pcss_strength",
        "local_shadow_face_blend_strength",
        "local_shadow_rect_sample_pattern",
        "shadow_contact_strength",
        "shadow_contact_length",
        "shadow_contact_thickness",
        "shadow_contact_steps",
        "shadow_contact_jitter_strength",
        "shadow_contact_edge_fade_pixels"
    )

    $metrics = @{}
    foreach ($column in $columns) {
        $metrics[$column] = Measure-Column -Rows $rows -Name $column
    }

    $expectedCascades = @{
        low = 1
        medium = 3
        high = 4
        ultra = 4
    }
    $expectedMaxDistance = @{
        low = 45.0
        medium = 55.0
        high = 60.0
        ultra = 75.0
    }
    $expectedContactSteps = @{
        low = 2
        medium = 4
        high = 6
        ultra = 8
    }

    Add-Check -Checks $checks -Area "run" -Name "captured frame rows" `
        -Status ($(if ($rows.Count -gt 0) { "pass" } else { "fail" })) `
        -Actual $rows.Count -Expected "> 0"

    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_missing_resource_refs" `
        -Area "framegraph" -Name "no missing framegraph resource refs" -Maximum 0 -MissingStatus "warn"
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_read_before_first_write" `
        -Area "framegraph" -Name "no framegraph read before first write" -Maximum 0 -MissingStatus "warn"
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_duplicate_pass_ids" `
        -Area "framegraph" -Name "no duplicate framegraph pass ids" -Maximum 0 -MissingStatus "warn"
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_duplicate_resource_ids" `
        -Area "framegraph" -Name "no duplicate framegraph resource ids" -Maximum 0 -MissingStatus "warn"
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_active_writes_planned_resources" `
        -Area "framegraph" -Name "active passes do not write planned resources" -Maximum 0 -MissingStatus "warn"
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "framegraph_validation_write_only_roadmap_resources" `
        -Area "framegraph" -Name "roadmap resources are not write-only active leftovers" -Maximum 0 -MissingStatus "warn"

    $unusedPhysical = $metrics["framegraph_validation_unused_physical_resources"]
    if ($unusedPhysical.present) {
        $unusedStatus = if ($null -ne $unusedPhysical.max -and [double]$unusedPhysical.max -le 0) { "pass" } else { "warn" }
        Add-Check -Checks $checks -Area "framegraph" -Name "unused physical resources are only a hygiene warning" `
            -Status $unusedStatus -Actual $unusedPhysical.max -Expected "<= 0 for a fully tidy graph"
    } else {
        Add-Check -Checks $checks -Area "framegraph" -Name "unused physical resource signal exists" `
            -Status "warn" -Actual "missing:framegraph_validation_unused_physical_resources" `
            -Expected "CSV column present"
    }

    $validationIssues = $metrics["framegraph_validation_issues"]
    if ($validationIssues.present) {
        $validationStatus = if ($null -ne $validationIssues.max -and [double]$validationIssues.max -le 0) { "pass" } else { "warn" }
        Add-Check -Checks $checks -Area "framegraph" -Name "aggregate validation issue count" `
            -Status $validationStatus -Actual $validationIssues.max `
            -Expected "0 preferred; see typed framegraph checks for severity"
    } else {
        Add-Check -Checks $checks -Area "framegraph" -Name "aggregate validation issue count signal exists" `
            -Status "warn" -Actual "missing:framegraph_validation_issues" `
            -Expected "CSV column present"
    }

    Add-MinCheck -Checks $checks -Metrics $metrics -Column "runtime_import_animation_playback_changed_bone_palette_entry_count" `
        -Area "animated casters" -Name "skinned animation changes the shadow input" -Minimum 1
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "main_skinned_conservative_bounds" `
        -Area "animated casters" -Name "main queue keeps conservative skinned bounds" -Minimum 1
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_skinned_conservative_bounds" `
        -Area "animated casters" -Name "shadow queue keeps conservative skinned bounds" -Minimum 1

    Add-MinCheck -Checks $checks -Metrics $metrics -Column "main_culled" `
        -Area "caster visibility" -Name "main camera culls off-camera control casters" -Minimum 2
    $mainVisible = $metrics["main_visible"].max
    $shadowVisible = $metrics["shadow_visible"].max
    $visibleStatus = if ($null -ne $mainVisible -and $null -ne $shadowVisible -and [double]$shadowVisible -gt [double]$mainVisible) {
        "pass"
    } else {
        "fail"
    }
    Add-Check -Checks $checks -Area "caster visibility" -Name "shadow queue preserves off-camera casters" `
        -Status $visibleStatus -Actual "main=$mainVisible shadow=$shadowVisible" `
        -Expected "shadow_visible > main_visible"

    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_active_count" `
        -Area "csm" -Name "expected cascade count is active" -Minimum $expectedCascades[$Quality]
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_allocated" `
        -Area "csm" -Name "directional cascade atlas allocated" -Minimum 1
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_passes" `
        -Area "csm" -Name "cascade atlas records depth passes" -Minimum $expectedCascades[$Quality]
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_draws" `
        -Area "csm" -Name "cascade atlas records caster draws" -Minimum 1
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_receiver_guard" `
        -Area "csm" -Name "receiver UV guard protects split-line coverage" -Minimum 0.25
    if (Test-Column -Rows $rows -Name "shadow_cascade_max_distance") {
        $metrics["shadow_cascade_max_distance"] = Measure-Column -Rows $rows -Name "shadow_cascade_max_distance"
        Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_max_distance" `
            -Area "csm" -Name "quality profile keeps intended CSM range" `
            -Minimum $expectedMaxDistance[$Quality]
    }
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_pcf_kernel_radius" `
        -Area "csm" -Name "directional PCF kernel is reported" -Minimum 1

    Add-MinCheck -Checks $checks -Metrics $metrics -Column "local_shadow_atlas_allocated" `
        -Area "local shadows" -Name "local shadow atlas allocated" -Minimum 1
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "local_shadow_recorded_tile_passes" `
        -Area "local shadows" -Name "local shadow tile passes recorded" -Minimum 8
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "local_shadow_recorded_draws" `
        -Area "local shadows" -Name "local shadow caster draws recorded" -Minimum 1
    Add-MaxCheck -Checks $checks -Metrics $metrics -Column "local_shadow_dropped_tiles" `
        -Area "local shadows" -Name "local shadow atlas does not drop tiles" -Maximum 0
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "local_shadow_cache_dynamic_skinned_caster_tiles" `
        -Area "local shadows" -Name "animated skinned casters mark affected local shadow tiles non-reusable" -Minimum 1
    $unsafeCacheReuseRows = @(
        $rows | Where-Object {
            $assigned = Get-Number -Row $_ -Name "local_shadow_assigned_tiles"
            $dynamicSkinned = Get-Number -Row $_ -Name "local_shadow_cache_dynamic_skinned_caster_tiles"
            $skipped = Get-Number -Row $_ -Name "local_shadow_cache_skipped_tiles"
            $dynamicSkinned -gt 0 -and $skipped -gt [Math]::Max($assigned - $dynamicSkinned, 0)
        }
    )
    Add-Check -Checks $checks -Area "local shadows" -Name "animated skinned tiles never reuse stale local shadows" `
        -Status ($(if ($unsafeCacheReuseRows.Count -eq 0) { "pass" } else { "fail" })) `
        -Actual "unsafeRows=$($unsafeCacheReuseRows.Count)" `
        -Expected "0"
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "local_shadow_resolve_enabled" `
        -Area "local shadows" -Name "local shadow resolve path is active" -Minimum 1
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "local_shadow_pcf_radius" `
        -Area "local shadows" -Name "local PCF radius is reported" -Minimum 0.1
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "local_shadow_face_blend_strength" `
        -Area "local shadows" -Name "point-face seam blend control is reported" -Minimum 0.1
    $rectLightCount = $metrics["local_shadow_rect_light_count"]
    $rectSamplePattern = $metrics["local_shadow_rect_sample_pattern"]
    if ($rectLightCount.present -and [double]$rectLightCount.max -gt 0) {
        $patternStatus = if ($rectSamplePattern.present -and [double]$rectSamplePattern.max -eq 1) {
            "pass"
        } else {
            "fail"
        }
        Add-Check -Checks $checks -Area "local shadows" -Name "rect local shadows use 2x2 surface sample pattern" `
            -Status $patternStatus -Actual $rectSamplePattern.max -Expected "1 when rect local lights exist"
    } else {
        Add-Check -Checks $checks -Area "local shadows" -Name "rect local shadow pattern check" `
            -Status "warn" -Actual "rectLightCount=$($rectLightCount.max)" `
            -Expected "rect local lights present for this check"
    }

    $tilePasses = $metrics["local_shadow_recorded_tile_passes"].max
    $localDraws = $metrics["local_shadow_recorded_draws"].max
    $fullEstimate = if ($null -ne $tilePasses -and $null -ne $shadowVisible) {
        [double]$tilePasses * [double]$shadowVisible
    } else {
        $null
    }
    $perTileCullStatus = if ($null -ne $localDraws -and $null -ne $fullEstimate -and $fullEstimate -gt 0 -and [double]$localDraws -lt $fullEstimate) {
        "pass"
    } else {
        "fail"
    }
    Add-Check -Checks $checks -Area "local shadows" -Name "per-tile culling reduces local shadow draw work" `
        -Status $perTileCullStatus -Actual "draws=$localDraws fullEstimate=$fullEstimate" `
        -Expected "recorded_draws < tile_passes * shadow_visible"

    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_contact_strength" `
        -Area "contact shadows" -Name "contact shadow strength is active" -Minimum 0.01
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_contact_steps" `
        -Area "contact shadows" -Name "quality profile keeps expected contact step floor" `
        -Minimum $expectedContactSteps[$Quality]
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_contact_jitter_strength" `
        -Area "contact shadows" -Name "contact shadow deterministic jitter is reported" -Minimum 0.01
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_contact_edge_fade_pixels" `
        -Area "contact shadows" -Name "contact shadow screen-edge fade is reported" -Minimum 1

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
        quality = $Quality
        csv = $CsvPath
        verdict = $verdict
        passCount = $passCount
        warnCount = $warnCount
        failCount = $failCount
        metrics = [pscustomobject]@{
            rows = $rows.Count
            mainVisibleMax = $mainVisible
            shadowVisibleMax = $shadowVisible
            localShadowFullDrawEstimate = $fullEstimate
            columns = $metrics
        }
        checks = @($checks)
    }
}

$resolvedOutput = Resolve-FullPath -Path $OutputDirectory
New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null

$validQualities = @("low", "medium", "high", "ultra")
$requestedQualities = @(
    $ShadowQuality -split "," |
        ForEach-Object { $_.Trim().ToLowerInvariant() } |
        Where-Object { $_ -ne "" }
)
foreach ($quality in $requestedQualities) {
    if ($validQualities -notcontains $quality) {
        throw "Invalid ShadowQuality '$quality'. Expected one or more of: $($validQualities -join ', ')"
    }
}
if ($requestedQualities.Count -eq 0) {
    throw "ShadowQuality must contain at least one quality name."
}

$testScript = Join-Path $PSScriptRoot "Test-Forward3DShadowRegression.ps1"
if (-not (Test-Path -LiteralPath $testScript)) {
    throw "Missing required script: $testScript"
}

$reports = [System.Collections.Generic.List[object]]::new()
$qualityIndex = 0
foreach ($quality in $requestedQualities) {
    $qualityOutput = Join-Path $resolvedOutput $quality
    New-Item -ItemType Directory -Force -Path $qualityOutput | Out-Null

    $runError = $null
    $testArgs = @{
        ExecutablePath = $ExecutablePath
        OutputDirectory = $qualityOutput
        WarmupFrames = $WarmupFrames
        CaptureFrames = $CaptureFrames
        AutoExitFrames = $AutoExitFrames
        ShadowQuality = $quality
    }
    if ($SkipBuild -or $qualityIndex -gt 0) {
        $testArgs.SkipBuild = $true
    }

    try {
        & $testScript @testArgs | Out-Host
    } catch {
        $runError = $_.Exception.Message
    }

    $csvPath = Join-Path $qualityOutput "forward3d_shadow_regression.csv"
    $reports.Add((New-ShadowHealthReport -Quality $quality -CsvPath $csvPath -RunError $runError)) | Out-Null
    $qualityIndex++
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
    executable = (Resolve-FullPath -Path $ExecutablePath)
    outputDirectory = $resolvedOutput
    verdict = $overall
    passCount = [int]$totalPass
    warnCount = [int]$totalWarn
    failCount = [int]$totalFail
    reports = @($reports)
}

$jsonPath = Join-Path $resolvedOutput "forward3d_shadow_health.json"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$summary

if ($Strict -and $overall -ne "pass") {
    throw "Forward3D shadow health verdict is $overall. See $jsonPath"
}
