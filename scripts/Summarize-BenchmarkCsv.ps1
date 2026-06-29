param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (!(Test-Path -LiteralPath $Path)) {
    throw "Benchmark CSV not found: $Path"
}

$rows = Import-Csv -LiteralPath $Path
if ($rows.Count -eq 0) {
    throw "Benchmark CSV has no samples: $Path"
}

function Get-NumberValues {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Rows,
        [Parameter(Mandatory = $true)]
        [string]$Column
    )

    $values = New-Object System.Collections.Generic.List[double]
    foreach ($row in $Rows) {
        $raw = $row.$Column
        if ($null -eq $raw -or $raw -eq "") {
            continue
        }

        $values.Add([double]$raw)
    }

    return $values.ToArray()
}

function Get-Percentile {
    param(
        [Parameter(Mandatory = $true)]
        [double[]]$Values,
        [Parameter(Mandatory = $true)]
        [double]$Percentile
    )

    if ($Values.Count -eq 0) {
        return $null
    }

    $sorted = $Values | Sort-Object
    $rank = [Math]::Ceiling(($Percentile / 100.0) * $sorted.Count) - 1
    $index = [Math]::Min([Math]::Max([int]$rank, 0), $sorted.Count - 1)
    return [double]$sorted[$index]
}

function New-MetricSummary {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Rows,
        [Parameter(Mandatory = $true)]
        [string]$Column
    )

    $values = Get-NumberValues -Rows $Rows -Column $Column
    if ($values.Count -eq 0) {
        return [pscustomobject]@{
            Metric = $Column
            Samples = 0
            Min = $null
            P50 = $null
            P95 = $null
            Max = $null
        }
    }

    return [pscustomobject]@{
        Metric = $Column
        Samples = $values.Count
        Min = [Math]::Round(($values | Measure-Object -Minimum).Minimum, 4)
        P50 = [Math]::Round((Get-Percentile -Values $values -Percentile 50), 4)
        P95 = [Math]::Round((Get-Percentile -Values $values -Percentile 95), 4)
        Max = [Math]::Round(($values | Measure-Object -Maximum).Maximum, 4)
    }
}

$gpuRows = @($rows | Where-Object { $_.gpu_available -eq "1" })
$metrics = @(
    "cpu_total_ms",
    "cpu_queue_build_ms",
    "cpu_command_record_ms",
    "cpu_submit_present_ms",
    "main_draws",
    "overlay_draws",
    "shadow_draws",
    "main_bounds_cache_hits",
    "main_bounds_cache_misses",
    "main_command_cache_hits",
    "main_command_cache_misses",
    "main_visibility_cache_hits",
    "main_visibility_cache_misses",
    "main_queue_cache_hits",
    "main_queue_cache_misses",
    "overlay_bounds_cache_hits",
    "overlay_bounds_cache_misses",
    "overlay_command_cache_hits",
    "overlay_command_cache_misses",
    "overlay_visibility_cache_hits",
    "overlay_visibility_cache_misses",
    "overlay_queue_cache_hits",
    "overlay_queue_cache_misses",
    "main_instanced_draws",
    "main_instanced_instances",
    "main_instance_batch_cache_hits",
    "main_instance_batch_cache_misses",
    "main_instance_buffer_uploads",
    "main_instance_buffer_upload_skips",
    "push_constant_updates",
    "push_constant_bytes"
)

$summary = New-Object System.Collections.Generic.List[object]
foreach ($metric in $metrics) {
    $summary.Add((New-MetricSummary -Rows $rows -Column $metric))
}

if ($gpuRows.Count -gt 0) {
    foreach ($metric in @(
        "gpu_total_recorded_ms",
        "gpu_shadow_ms",
        "gpu_main_ms",
        "gpu_overlay_ms",
        "gpu_imgui_ms"
    )) {
        $summary.Add((New-MetricSummary -Rows $gpuRows -Column $metric))
    }
}

$summary | Format-Table -AutoSize
