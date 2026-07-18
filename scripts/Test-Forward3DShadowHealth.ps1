param(
    [string]$ExecutablePath = "build\Debug\SelfEngineForward3D.exe",
    [string]$OutputDirectory = "out\shadow_health",
    [string]$ShadowQuality = "high",
    [int]$WarmupFrames = 4,
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
        "gpu_available",
        "gpu_shadow_ms",
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
        "shadow_quality",
        "shadow_budget_contract_version",
        "shadow_budget_resource_contract_valid",
        "shadow_budget_fallback_reason",
        "shadow_budget_swapchain_images",
        "shadow_budget_generation_max_passes",
        "shadow_budget_directional_receiver_samples",
        "shadow_budget_point_projection_samples",
        "shadow_budget_spot_projection_samples",
        "shadow_budget_rect_projection_samples",
        "shadow_budget_rect_projection_count",
        "shadow_budget_contact_samples",
        "shadow_budget_gpu_generation_scope",
        "shadow_budget_legacy_depth_bytes",
        "shadow_budget_directional_depth_bytes",
        "shadow_budget_local_depth_bytes",
        "shadow_budget_main_depth_bytes",
        "shadow_cascade_active_count",
        "shadow_cascade_atlas_allocated",
        "shadow_cascade_atlas_tile_size",
        "shadow_cascade_atlas_width",
        "shadow_cascade_atlas_height",
        "shadow_cascade_atlas_capacity",
        "shadow_cascade_atlas_passes",
        "shadow_cascade_atlas_draws",
        "shadow_cascade_receiver_guard",
        "shadow_pcf_kernel_radius",
        "shadow_pcss_strength",
        "local_shadow_atlas_allocated",
        "local_shadow_atlas_tile_size",
        "local_shadow_atlas_width",
        "local_shadow_atlas_height",
        "local_shadow_atlas_capacity",
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
        "local_shadow_filter_contract_version",
        "local_shadow_production_filter_enabled",
        "local_shadow_production_filter_ready",
        "local_shadow_production_filter_active",
        "local_shadow_production_filter_fallback_reason",
        "local_shadow_comparison_sampler_ready",
        "local_shadow_raw_depth_sampler_ready",
        "local_shadow_tile_range_contract_valid",
        "local_shadow_tile_range_invalid_lights",
        "local_shadow_tile_range_max_tiles_per_light",
        "local_shadow_filter_geometry_valid_tiles",
        "local_shadow_filter_geometry_invalid_tiles",
        "local_shadow_point_pcss_blocker_samples",
        "local_shadow_point_pcss_filter_samples",
        "local_shadow_point_pcss_search_radius_texels",
        "local_shadow_point_pcss_max_penumbra_texels",
        "local_shadow_spot_pcss_blocker_samples",
        "local_shadow_spot_pcss_filter_samples",
        "local_shadow_spot_pcss_search_radius_texels",
        "local_shadow_spot_pcss_max_penumbra_texels",
        "local_shadow_rect_pcss_blocker_samples",
        "local_shadow_rect_pcss_filter_samples",
        "local_shadow_rect_pcss_search_radius_texels",
        "local_shadow_rect_pcss_max_penumbra_texels",
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
    $expectedQualityId = @{
        low = 1
        medium = 2
        high = 3
        ultra = 4
    }
    $expectedCsmTileSize = @{
        low = 1024
        medium = 2048
        high = 4096
        ultra = 4096
    }
    $expectedLocalTileSize = @{
        low = 512
        medium = 1024
        high = 1024
        ultra = 1024
    }
    $expectedLocalAtlasWidth = @{
        low = 2048
        medium = 5120
        high = 6144
        ultra = 6144
    }
    $expectedLocalAtlasHeight = @{
        low = 1536
        medium = 5120
        high = 6144
        ultra = 6144
    }
    $expectedLocalCapacity = @{
        low = 12
        medium = 24
        high = 32
        ultra = 32
    }
    $expectedDirectionalSamples = @{
        low = 9
        medium = 9
        high = 9
        ultra = 28
    }
    $expectedPointSamples = @{
        low = 4
        medium = 16
        high = 20
        ultra = 28
    }
    $expectedSpotSamples = @{
        low = 4
        medium = 16
        high = 20
        ultra = 28
    }
    $expectedRectSamples = @{
        low = 4
        medium = 16
        high = 20
        ultra = 28
    }
    $expectedLocalBlockerSamples = @{
        low = 0
        medium = 8
        high = 8
        ultra = 12
    }
    $expectedLocalFilterSamples = @{
        low = 4
        medium = 8
        high = 12
        ultra = 16
    }
    $expectedLocalSearchRadius = @{
        low = 0.0
        medium = 4.0
        high = 6.0
        ultra = 8.0
    }
    $expectedLocalMaxPenumbra = @{
        low = 1.5
        medium = 4.0
        high = 6.0
        ultra = 8.0
    }
    $expectedRectProjectionCount = @{
        low = 2
        medium = 2
        high = 4
        ultra = 4
    }
    $expectedRectPattern = @{
        low = 0
        medium = 0
        high = 1
        ultra = 1
    }
    $expectedUnusedPhysicalResources = @{
        low = 1
        medium = 0
        high = 0
        ultra = 0
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

    Add-EqualityCheck -Checks $checks -Metrics $metrics `
        -Column "framegraph_validation_unused_physical_resources" `
        -Area "framegraph" -Name "quality profile has the expected physical-resource hygiene state" `
        -Expected $expectedUnusedPhysicalResources[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics `
        -Column "framegraph_validation_issues" `
        -Area "framegraph" -Name "aggregate issues contain no unexpected category" `
        -Expected $expectedUnusedPhysicalResources[$Quality] -MissingStatus "fail"

    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_quality" `
        -Area "budget" -Name "resolved shadow quality id" `
        -Expected $expectedQualityId[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_contract_version" `
        -Area "budget" -Name "shadow budget contract version" -Expected 1 -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_resource_contract_valid" `
        -Area "budget" -Name "shadow resource budget contract is valid" -Expected 1 -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_fallback_reason" `
        -Area "budget" -Name "shadow budget needs no fallback" -Expected 0 -MissingStatus "fail"
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_swapchain_images" `
        -Area "budget" -Name "shadow resources cover swapchain images" -Minimum 1 -MissingStatus "fail"

    $expectedGenerationPasses = 1 + $expectedCascades[$Quality] + $expectedLocalCapacity[$Quality]
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_generation_max_passes" `
        -Area "budget" -Name "maximum depth-generation pass budget" `
        -Expected $expectedGenerationPasses -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_directional_receiver_samples" `
        -Area "budget" -Name "directional receiver sample budget" `
        -Expected $expectedDirectionalSamples[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_point_projection_samples" `
        -Area "budget" -Name "point-light projection sample budget" `
        -Expected $expectedPointSamples[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_spot_projection_samples" `
        -Area "budget" -Name "spot-light projection sample budget" `
        -Expected $expectedSpotSamples[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_rect_projection_samples" `
        -Area "budget" -Name "rect-light projection sample budget" `
        -Expected $expectedRectSamples[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_rect_projection_count" `
        -Area "budget" -Name "rect-light projection count budget" `
        -Expected $expectedRectProjectionCount[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_contact_samples" `
        -Area "budget" -Name "contact-shadow sample budget" `
        -Expected $expectedContactSteps[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_gpu_generation_scope" `
        -Area "budget" -Name "GPU shadow timer uses the complete generation scope" `
        -Expected 1 -MissingStatus "fail"

    $expectedCsmExtent = 2 * $expectedCsmTileSize[$Quality]
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_tile_size" `
        -Area "budget" -Name "CSM tile resolution" `
        -Expected $expectedCsmTileSize[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_width" `
        -Area "budget" -Name "CSM atlas width" -Expected $expectedCsmExtent -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_height" `
        -Area "budget" -Name "CSM atlas height" -Expected $expectedCsmExtent -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "shadow_cascade_atlas_capacity" `
        -Area "budget" -Name "CSM atlas capacity" -Expected 4 -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "local_shadow_atlas_tile_size" `
        -Area "budget" -Name "local shadow tile resolution" `
        -Expected $expectedLocalTileSize[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "local_shadow_atlas_width" `
        -Area "budget" -Name "local shadow atlas width" `
        -Expected $expectedLocalAtlasWidth[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "local_shadow_atlas_height" `
        -Area "budget" -Name "local shadow atlas height" `
        -Expected $expectedLocalAtlasHeight[$Quality] -MissingStatus "fail"
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "local_shadow_atlas_capacity" `
        -Area "budget" -Name "local shadow atlas capacity" `
        -Expected $expectedLocalCapacity[$Quality] -MissingStatus "fail"

    $memoryContractRows = @(
        $rows | Where-Object {
            $images = Get-Number -Row $_ -Name "shadow_budget_swapchain_images"
            $legacyBytes = Get-Number -Row $_ -Name "shadow_budget_legacy_depth_bytes"
            $directionalBytes = Get-Number -Row $_ -Name "shadow_budget_directional_depth_bytes"
            $localBytes = Get-Number -Row $_ -Name "shadow_budget_local_depth_bytes"
            $mainBytes = Get-Number -Row $_ -Name "shadow_budget_main_depth_bytes"
            $csmWidth = Get-Number -Row $_ -Name "shadow_cascade_atlas_width"
            $csmHeight = Get-Number -Row $_ -Name "shadow_cascade_atlas_height"
            $localWidth = Get-Number -Row $_ -Name "local_shadow_atlas_width"
            $localHeight = Get-Number -Row $_ -Name "local_shadow_atlas_height"
            $mapSize = Get-Number -Row $_ -Name "shadow_cascade_atlas_tile_size"
            $validTexelSizes = @(2.0, 4.0, 8.0)
            if ($images -le 0 -or $mapSize -le 0 -or $csmWidth -le 0 -or
                $csmHeight -le 0 -or $localWidth -le 0 -or $localHeight -le 0) {
                return $true
            }
            $legacyTexelBytes = $legacyBytes / ($mapSize * $mapSize * $images)
            $directionalTexelBytes = $directionalBytes / ($csmWidth * $csmHeight * $images)
            $localTexelBytes = $localBytes / ($localWidth * $localHeight * $images)
            return $mainBytes -ne ($legacyBytes + $directionalBytes + $localBytes) -or
                $validTexelSizes -notcontains $legacyTexelBytes -or
                $validTexelSizes -notcontains $directionalTexelBytes -or
                $validTexelSizes -notcontains $localTexelBytes
        }
    )
    Add-Check -Checks $checks -Area "budget" -Name "logical depth memory is dimension-derived and sums exactly" `
        -Status ($(if ($memoryContractRows.Count -eq 0) { "pass" } else { "fail" })) `
        -Actual "invalidRows=$($memoryContractRows.Count)" -Expected "0"
    Add-MinCheck -Checks $checks -Metrics $metrics -Column "shadow_budget_main_depth_bytes" `
        -Area "budget" -Name "logical shadow depth memory budget is nonzero" `
        -Minimum 1 -MissingStatus "fail"

    $invalidGpuTimingRows = @(
        $rows | Where-Object {
            (Get-Number -Row $_ -Name "gpu_available") -ne 1 -or
            (Get-Number -Row $_ -Name "gpu_shadow_ms") -le 0
        }
    )
    Add-Check -Checks $checks -Area "budget" -Name "GPU generation timing is available and nonzero" `
        -Status ($(if ($invalidGpuTimingRows.Count -eq 0) { "pass" } else { "fail" })) `
        -Actual "invalidRows=$($invalidGpuTimingRows.Count)" -Expected "0"

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
    Add-EqualityCheck -Checks $checks -Metrics $metrics -Column "local_shadow_filter_contract_version" `
        -Area "local filter" -Name "local production filter contract version" `
        -Expected 3 -MissingStatus "fail"
    foreach ($column in @(
        "local_shadow_production_filter_enabled",
        "local_shadow_production_filter_ready",
        "local_shadow_production_filter_active",
        "local_shadow_comparison_sampler_ready",
        "local_shadow_raw_depth_sampler_ready",
        "local_shadow_tile_range_contract_valid"
    )) {
        Add-EqualityCheck -Checks $checks -Metrics $metrics -Column $column `
            -Area "local filter" -Name "$column is active" `
            -Expected 1 -MissingStatus "fail"
    }
    foreach ($column in @(
        "local_shadow_production_filter_fallback_reason",
        "local_shadow_tile_range_invalid_lights",
        "local_shadow_filter_geometry_invalid_tiles"
    )) {
        Add-EqualityCheck -Checks $checks -Metrics $metrics -Column $column `
            -Area "local filter" -Name "$column is zero" `
            -Expected 0 -MissingStatus "fail"
    }
    Add-MaxCheck -Checks $checks -Metrics $metrics `
        -Column "local_shadow_tile_range_max_tiles_per_light" `
        -Area "local filter" -Name "per-light tile range stays shader-bounded" `
        -Maximum 6 -MissingStatus "fail"
    $invalidGeometryCoverageRows = @(
        $rows | Where-Object {
            (Get-Number -Row $_ -Name "local_shadow_filter_geometry_valid_tiles") -ne
                (Get-Number -Row $_ -Name "local_shadow_assigned_tiles")
        }
    )
    Add-Check -Checks $checks -Area "local filter" `
        -Name "every assigned tile has valid projection geometry" `
        -Status ($(if ($invalidGeometryCoverageRows.Count -eq 0) { "pass" } else { "fail" })) `
        -Actual "invalidRows=$($invalidGeometryCoverageRows.Count)" -Expected "0"
    foreach ($kind in @("point", "spot", "rect")) {
        Add-EqualityCheck -Checks $checks -Metrics $metrics `
            -Column "local_shadow_${kind}_pcss_blocker_samples" `
            -Area "local filter" -Name "$kind blocker sample budget" `
            -Expected $expectedLocalBlockerSamples[$Quality] -MissingStatus "fail"
        Add-EqualityCheck -Checks $checks -Metrics $metrics `
            -Column "local_shadow_${kind}_pcss_filter_samples" `
            -Area "local filter" -Name "$kind filter sample budget" `
            -Expected $expectedLocalFilterSamples[$Quality] -MissingStatus "fail"
        Add-EqualityCheck -Checks $checks -Metrics $metrics `
            -Column "local_shadow_${kind}_pcss_search_radius_texels" `
            -Area "local filter" -Name "$kind blocker-search radius" `
            -Expected $expectedLocalSearchRadius[$Quality] -MissingStatus "fail"
        Add-EqualityCheck -Checks $checks -Metrics $metrics `
            -Column "local_shadow_${kind}_pcss_max_penumbra_texels" `
            -Area "local filter" -Name "$kind maximum penumbra radius" `
            -Expected $expectedLocalMaxPenumbra[$Quality] -MissingStatus "fail"
    }
    $rectLightCount = $metrics["local_shadow_rect_light_count"]
    $rectSamplePattern = $metrics["local_shadow_rect_sample_pattern"]
    if ($rectLightCount.present -and [double]$rectLightCount.max -gt 0) {
        $patternStatus = if ($rectSamplePattern.present -and
            [double]$rectSamplePattern.max -eq $expectedRectPattern[$Quality]) {
            "pass"
        } else {
            "fail"
        }
        Add-Check -Checks $checks -Area "local shadows" -Name "rect local shadows use the quality-tier sample pattern" `
            -Status $patternStatus -Actual $rectSamplePattern.max `
            -Expected $expectedRectPattern[$Quality]
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

$matrixChecks = [System.Collections.Generic.List[object]]::new()
$qualityRank = @{
    low = 1
    medium = 2
    high = 3
    ultra = 4
}
$orderedReports = @($reports | Sort-Object { $qualityRank[$_.quality] })
$monotonicColumns = @(
    "shadow_cascade_atlas_tile_size",
    "local_shadow_atlas_tile_size",
    "local_shadow_atlas_capacity",
    "shadow_budget_generation_max_passes",
    "shadow_budget_directional_receiver_samples",
    "shadow_budget_point_projection_samples",
    "shadow_budget_spot_projection_samples",
    "shadow_budget_rect_projection_samples",
    "shadow_budget_rect_projection_count",
    "shadow_budget_contact_samples",
    "shadow_budget_main_depth_bytes"
)
foreach ($column in $monotonicColumns) {
    $values = @(
        foreach ($report in $orderedReports) {
            [pscustomobject]@{
                quality = $report.quality
                value = $report.metrics.columns[$column].max
            }
        }
    )
    $decreases = @(
        for ($index = 1; $index -lt $values.Count; ++$index) {
            if ($null -eq $values[$index - 1].value -or
                $null -eq $values[$index].value -or
                [double]$values[$index].value -lt [double]$values[$index - 1].value) {
                "$($values[$index - 1].quality)->$($values[$index].quality)"
            }
        }
    )
    $actual = $values | ForEach-Object { "$($_.quality)=$($_.value)" }
    Add-Check -Checks $matrixChecks -Area "quality matrix" `
        -Name "$column is monotonic" `
        -Status ($(if ($decreases.Count -eq 0) { "pass" } else { "fail" })) `
        -Actual ($actual -join ", ") -Expected "non-decreasing by quality" `
        -Detail ($(if ($decreases.Count -gt 0) { "decreases: $($decreases -join ', ')" } else { "" }))
}

$totalPass = @($reports | ForEach-Object { $_.passCount } | Measure-Object -Sum).Sum +
    @($matrixChecks | Where-Object { $_.status -eq "pass" }).Count
$totalWarn = @($reports | ForEach-Object { $_.warnCount } | Measure-Object -Sum).Sum +
    @($matrixChecks | Where-Object { $_.status -eq "warn" }).Count
$totalFail = @($reports | ForEach-Object { $_.failCount } | Measure-Object -Sum).Sum +
    @($matrixChecks | Where-Object { $_.status -eq "fail" }).Count
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
    qualityMatrixChecks = @($matrixChecks)
    reports = @($reports)
}

$jsonPath = Join-Path $resolvedOutput "forward3d_shadow_health.json"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$summary

if ($Strict -and $overall -ne "pass") {
    throw "Forward3D shadow health verdict is $overall. See $jsonPath"
}
