[CmdletBinding()]
param(
    [string]$AuditDirectory = "tmp\hybrid_reflection_full_audit",
    [string]$ReceiverNamePattern = "Center Mirror Sphere",
    [string]$ExpectedHitNamePattern = "Polished Metal Sphere",
    [ValidateSet(2, 3, 4, 5, 6, 7, 8)]
    [uint32]$ExpectedAuditContractVersion = 8,
    [switch]$ExtendedReport,
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (![IO.Path]::IsPathRooted($AuditDirectory)) {
    $AuditDirectory = Join-Path $projectRoot $AuditDirectory
}
$AuditDirectory = [IO.Path]::GetFullPath($AuditDirectory)

$paths = [ordered]@{
    frames = Join-Path $AuditDirectory "frames.csv"
    objects = Join-Path $AuditDirectory "objects.csv"
    instances = Join-Path $AuditDirectory "instances.csv"
    counters = Join-Path $AuditDirectory "instance_counters.csv"
    rays = Join-Path $AuditDirectory "rays.csv"
    apply = Join-Path $AuditDirectory "apply.csv"
    gbuffer = Join-Path $AuditDirectory "gbuffer_samples.csv"
    composition = Join-Path $AuditDirectory "reflection_composition.csv"
    compositionSummary = Join-Path $AuditDirectory "reflection_composition_summary.csv"
    lights = Join-Path $AuditDirectory "lights.csv"
    probes = Join-Path $AuditDirectory "probes.csv"
    queues = Join-Path $AuditDirectory "queue_commands.csv"
    auditIndex = Join-Path $AuditDirectory "audit_index.csv"
    runtimeObjects = Join-Path $AuditDirectory "runtime_object_summary.csv"
    runtimePairs = Join-Path $AuditDirectory "runtime_receiver_hit_matrix.csv"
    runtimeQuality = Join-Path $AuditDirectory "runtime_receiver_quality.csv"
    runtimeApplyDiscontinuities = Join-Path $AuditDirectory "runtime_apply_discontinuities.csv"
    imageSnapshots = Join-Path $AuditDirectory "image_snapshot_manifest.csv"
    benchmark = Join-Path $AuditDirectory "benchmark.csv"
}
foreach ($path in $paths.Values) {
    if (!(Test-Path -LiteralPath $path)) {
        throw "Missing full-audit file: $path"
    }
}

$frames = @(Import-Csv -LiteralPath $paths.frames)
$objects = @(Import-Csv -LiteralPath $paths.objects)
$instances = @(Import-Csv -LiteralPath $paths.instances)
$counters = @(Import-Csv -LiteralPath $paths.counters)
$compositionSummary = @(Import-Csv -LiteralPath $paths.compositionSummary)
$lights = @(Import-Csv -LiteralPath $paths.lights)
$probes = @(Import-Csv -LiteralPath $paths.probes)
$queueCommands = @(Import-Csv -LiteralPath $paths.queues)
$auditIndex = @(Import-Csv -LiteralPath $paths.auditIndex)
$runtimeObjects = @(Import-Csv -LiteralPath $paths.runtimeObjects)
$runtimePairs = @(Import-Csv -LiteralPath $paths.runtimePairs)
$runtimeQuality = @(Import-Csv -LiteralPath $paths.runtimeQuality)
$rawEvidenceStatesForLoad = @($frames | ForEach-Object {
    $property = $_.PSObject.Properties["raw_evidence"]
    if ($null -eq $property) { 1 } else { [uint32]$property.Value }
} | Sort-Object -Unique)
$rawEvidenceEnabledForLoad = $rawEvidenceStatesForLoad.Count -eq 1 -and
    [uint32]$rawEvidenceStatesForLoad[0] -ne 0
$runtimeApplyDiscontinuities = @(if ($rawEvidenceEnabledForLoad) {
    Import-Csv -LiteralPath $paths.runtimeApplyDiscontinuities
})
$imageSnapshots = @(Import-Csv -LiteralPath $paths.imageSnapshots)
$benchmark = @(Import-Csv -LiteralPath $paths.benchmark)
$runtimeDnsrConfidenceQualityPath = Join-Path $AuditDirectory `
    "runtime_dnsr_confidence_quality.csv"
$runtimeDnsrConfidenceTransitionsPath = Join-Path $AuditDirectory `
    "runtime_dnsr_confidence_transitions.csv"
$nativeDnsrConfidenceAvailable =
    (Test-Path -LiteralPath $runtimeDnsrConfidenceQualityPath) -and
    (Test-Path -LiteralPath $runtimeDnsrConfidenceTransitionsPath)
$nativeDnsrConfidenceQuality = if ($nativeDnsrConfidenceAvailable) {
    @(Import-Csv -LiteralPath $runtimeDnsrConfidenceQualityPath)
} else { @() }
$nativeDnsrConfidenceTransitions = if ($nativeDnsrConfidenceAvailable) {
    @(Import-Csv -LiteralPath $runtimeDnsrConfidenceTransitionsPath)
} else { @() }
if ($frames.Count -eq 0 -or $objects.Count -eq 0 -or
    $instances.Count -eq 0 -or $compositionSummary.Count -eq 0 -or
    $auditIndex.Count -eq 0 -or $imageSnapshots.Count -eq 0 -or
    $benchmark.Count -eq 0) {
    throw "Full-audit files contain no captured data"
}

function UInt($Value) { return [uint32]$Value }
function ULong($Value) { return [uint64]$Value }
function Number($Value) { return [double]$Value }
function Key([uint32]$Capture, [uint32]$Identity) {
    return "$Capture`:$Identity"
}
function New-Check(
    [string]$Name,
    [bool]$Passed,
    [object]$Actual,
    [object]$Expected
) {
    return [pscustomobject]@{
        name = $Name
        status = if ($Passed) { "pass" } else { "fail" }
        actual = $Actual
        expected = $Expected
    }
}
function Benchmark-Value($Row, [string]$Name) {
    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}
function ConvertTo-CsvField($Value) {
    $text = if ($null -eq $Value) { "" } else { [string]$Value }
    return '"' + $text.Replace('"', '""') + '"'
}
function New-AuditProjection(
    [object[]]$Rows,
    [string]$OutputPath,
    [string]$Pattern
) {
    if ($Rows.Count -eq 0) {
        return [pscustomobject]@{ Count = [uint64]0 }
    }
    $columns = @($Rows[0].PSObject.Properties.Name | Where-Object {
        $_ -match $Pattern
    })
    [uint64]$count = 0
    $writer = [IO.StreamWriter]::new(
        $OutputPath,
        $false,
        [Text.UTF8Encoding]::new($false)
    )
    try {
        $writer.WriteLine(
            '"sample_frame","rendered_frame","source_column","value"'
        )
        foreach ($row in $Rows) {
            $sampleFrame = ConvertTo-CsvField `
                (Benchmark-Value $row "sample_frame")
            $renderedFrame = ConvertTo-CsvField `
                (Benchmark-Value $row "rendered_frame")
            foreach ($column in $columns) {
                $writer.WriteLine((@(
                    $sampleFrame,
                    $renderedFrame,
                    (ConvertTo-CsvField $column),
                    (ConvertTo-CsvField $row.$column)
                ) -join ','))
                ++$count
            }
        }
    } finally {
        $writer.Dispose()
    }
    return [pscustomobject]@{ Count = $count }
}
function Group-ByCapture([object[]]$Rows) {
    $groups = @{}
    foreach ($row in $Rows) {
        $key = [string](UInt $row.capture_index)
        if (!$groups.ContainsKey($key)) {
            $groups[$key] = [Collections.Generic.List[object]]::new()
        }
        $groups[$key].Add($row) | Out-Null
    }
    return $groups
}
function Get-CsvDataRowCount([string]$Path) {
    [uint64]$count = 0
    $reader = [IO.StreamReader]::new($Path)
    try {
        $header = $reader.ReadLine()
        if ([string]::IsNullOrWhiteSpace($header)) {
            return [uint64]0
        }
        while ($null -ne $reader.ReadLine()) { ++$count }
    } finally {
        $reader.Dispose()
    }
    return $count
}
function Read-Float32Snapshot([string]$Path, [uint64]$ExpectedCount) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ([uint64]$bytes.Length -ne $ExpectedCount * 4) {
        throw "Float32 snapshot size mismatch: $Path"
    }
    $values = New-Object 'Single[]' ([int]$ExpectedCount)
    [Buffer]::BlockCopy($bytes, 0, $values, 0, $bytes.Length)
    return $values
}
function Read-UInt32Snapshot([string]$Path, [uint64]$ExpectedCount) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ([uint64]$bytes.Length -ne $ExpectedCount * 4) {
        throw "UInt32 snapshot size mismatch: $Path"
    }
    $values = New-Object 'UInt32[]' ([int]$ExpectedCount)
    [Buffer]::BlockCopy($bytes, 0, $values, 0, $bytes.Length)
    return $values
}
function Add-ZeroCheck(
    [Collections.Generic.List[object]]$Checks,
    [string]$Name,
    [uint64]$Value
) {
    $Checks.Add((New-Check $Name ($Value -eq 0) $Value 0)) | Out-Null
}

$checks = [Collections.Generic.List[object]]::new()
$findings = [Collections.Generic.List[object]]::new()
$pipelinePath = Join-Path $AuditDirectory "pipeline.csv"
$resourcesPath = Join-Path $AuditDirectory "resources.csv"
$benchmarkLongPath = Join-Path $AuditDirectory "benchmark_long.csv"
$benchmarkFrameMatchesPath = Join-Path $AuditDirectory `
    "benchmark_frame_matches.csv"
$imageStageContractPath = Join-Path $AuditDirectory "image_stage_contract.csv"
$dnsrConfidenceQualityPath = Join-Path $AuditDirectory `
    "dnsr_confidence_quality.csv"
$dnsrConfidenceTransitionsPath = Join-Path $AuditDirectory `
    "dnsr_confidence_transitions.csv"
$pipelinePattern = "^(framegraph_|gpu_|main_|gbuffer_|shadow_|overlay_|forward_|weighted_|ssr_|hybrid_|temporal_|taa_|dlss_|present_|depth_|light_tile_|bind_|cpu_)"
$resourcePattern = "(resource|format|extent|width|height|mip|memory|descriptor|binding|layout|image_count|buffer_bytes|allocated|ready)"
$benchmarkColumnCount = @($benchmark[0].PSObject.Properties.Name).Count
$pipelineRows = [pscustomobject]@{ Count = [uint64]0 }
$resourceRows = [pscustomobject]@{ Count = [uint64]0 }
$benchmarkLongRows = [pscustomobject]@{ Count = [uint64]0 }
if ($ExtendedReport) {
    $pipelineRows = New-AuditProjection $benchmark $pipelinePath $pipelinePattern
    $resourceRows = New-AuditProjection $benchmark $resourcesPath $resourcePattern
    $benchmarkLongRows = New-AuditProjection $benchmark $benchmarkLongPath "."
    $checks.Add((New-Check "pipeline projection is populated" `
        ($pipelineRows.Count -gt 0) $pipelineRows.Count ">0")) | Out-Null
    $checks.Add((New-Check "resource projection is populated" `
        ($resourceRows.Count -gt 0) $resourceRows.Count ">0")) | Out-Null
    $expectedBenchmarkLongRows = $benchmark.Count * $benchmarkColumnCount
    $checks.Add((New-Check "complete benchmark long-form coverage" `
        ($benchmarkLongRows.Count -eq $expectedBenchmarkLongRows) `
        $benchmarkLongRows.Count $expectedBenchmarkLongRows)) | Out-Null
} else {
    Remove-Item -LiteralPath $pipelinePath,$resourcesPath,$benchmarkLongPath `
        -Force -ErrorAction SilentlyContinue
}
$requiredBenchmarkColumns = @(
    "framegraph_validation_issues",
    "ssr_ffx_sssr_hit_confidence_contract_version",
    "ssr_ffx_sssr_hit_confidence_apply_bound",
    "ssr_ffx_sssr_apply_confidence_source",
    "ssr_ffx_sssr_zero_confidence_history_rejection_enabled",
    "ssr_ffx_sssr_radiance_sanitization_enabled",
    "temporal_upscaler_dlss_output_ready",
    "hybrid_reflections_ray_query_full_audit_requested",
    "hybrid_reflections_ray_query_full_audit_resources_ready",
    "hybrid_reflections_ray_query_full_audit_active",
    "hybrid_reflections_ray_query_full_audit_recorded_ray_count",
    "hybrid_reflections_ray_query_full_audit_captured_frame_count"
)
foreach ($column in $requiredBenchmarkColumns) {
    $present = $null -ne $benchmark[0].PSObject.Properties[$column]
    $checks.Add((New-Check "benchmark schema contains $column" `
        $present $present $true)) | Out-Null
}
if (@($requiredBenchmarkColumns | Where-Object {
    $null -eq $benchmark[0].PSObject.Properties[$_]
}).Count -eq 0) {
    $maxAuditRequested = 0
    $maxAuditResources = 0
    $maxAuditActive = 0
    [uint64]$maxAuditRecorded = 0
    $maxAuditCaptured = 0
    $badConfidenceContractRows = 0
    $badApplyConfidenceSourceRows = 0
    $badZeroConfidenceHistoryRejectionRows = 0
    $badRadianceSanitizationRows = 0
    $zeroConfidenceHistoryRejectionStates = [Collections.Generic.HashSet[uint32]]::new()
    $radianceSanitizationStates = [Collections.Generic.HashSet[uint32]]::new()
    foreach ($row in $benchmark) {
        $maxAuditRequested = [Math]::Max($maxAuditRequested,
            (UInt $row.hybrid_reflections_ray_query_full_audit_requested))
        $maxAuditResources = [Math]::Max($maxAuditResources,
            (UInt $row.hybrid_reflections_ray_query_full_audit_resources_ready))
        $maxAuditActive = [Math]::Max($maxAuditActive,
            (UInt $row.hybrid_reflections_ray_query_full_audit_active))
        $maxAuditRecorded = [Math]::Max($maxAuditRecorded,
            (ULong $row.hybrid_reflections_ray_query_full_audit_recorded_ray_count))
        $maxAuditCaptured = [Math]::Max($maxAuditCaptured,
            (UInt $row.hybrid_reflections_ray_query_full_audit_captured_frame_count))
        if ((UInt $row.ssr_ffx_sssr_hit_confidence_contract_version) -ne 2) {
            ++$badConfidenceContractRows
        }
        if ((UInt $row.ssr_ffx_sssr_hit_confidence_apply_bound) -ne 0 -and
            (UInt $row.ssr_ffx_sssr_apply_confidence_source) -ne 2) {
            ++$badApplyConfidenceSourceRows
        }
        $zeroConfidenceHistoryRejectionState =
            UInt $row.ssr_ffx_sssr_zero_confidence_history_rejection_enabled
        if ($zeroConfidenceHistoryRejectionState -gt 1) {
            ++$badZeroConfidenceHistoryRejectionRows
        }
        [void]$zeroConfidenceHistoryRejectionStates.Add(
            $zeroConfidenceHistoryRejectionState
        )
        $radianceSanitizationState =
            UInt $row.ssr_ffx_sssr_radiance_sanitization_enabled
        if ($radianceSanitizationState -gt 1) {
            ++$badRadianceSanitizationRows
        }
        [void]$radianceSanitizationStates.Add($radianceSanitizationState)
    }
    $checks.Add((New-Check "benchmark Full Audit requested/resources/active" `
        ($maxAuditRequested -eq 1 -and $maxAuditResources -eq 1 -and
         $maxAuditActive -eq 1) `
        "$maxAuditRequested/$maxAuditResources/$maxAuditActive" "1/1/1")) |
        Out-Null
    $checks.Add((New-Check "benchmark Full Audit readback completed" `
        ($maxAuditRecorded -gt 0 -and $maxAuditCaptured -ge $frames.Count) `
        "$maxAuditRecorded/$maxAuditCaptured" "records>0/captured>=frames")) |
        Out-Null
    Add-ZeroCheck $checks "benchmark hit-confidence contract v2 rows" `
        ([uint64]$badConfidenceContractRows)
    Add-ZeroCheck $checks "benchmark Apply confidence source rows" `
        ([uint64]$badApplyConfidenceSourceRows)
    Add-ZeroCheck $checks "benchmark zero-confidence history rejection rows" `
        ([uint64]$badZeroConfidenceHistoryRejectionRows)
    $checks.Add((New-Check `
        "benchmark zero-confidence history rejection state is capture-consistent" `
        ($zeroConfidenceHistoryRejectionStates.Count -eq 1) `
        $zeroConfidenceHistoryRejectionStates.Count 1)) | Out-Null
    Add-ZeroCheck $checks "benchmark radiance sanitization rows" `
        ([uint64]$badRadianceSanitizationRows)
    $checks.Add((New-Check `
        "benchmark radiance sanitization state is capture-consistent" `
        ($radianceSanitizationStates.Count -eq 1) `
        $radianceSanitizationStates.Count 1)) | Out-Null
}

$objectByAuditKey = @{}
foreach ($object in $objects) {
    $capture = UInt $object.capture_index
    $auditId = UInt $object.reflection_audit_object_id
    $key = Key $capture $auditId
    $duplicate = $objectByAuditKey.ContainsKey($key)
    $checks.Add((New-Check "nonzero scene audit identity $key" `
        ($auditId -ne 0) $auditId "nonzero")) | Out-Null
    $checks.Add((New-Check "unique scene audit identity $key" `
        (!$duplicate) $duplicate $false)) | Out-Null
    $objectByAuditKey[$key] = $object
}

$instanceByKey = @{}
$submissionByCapture = @{}
foreach ($instance in $instances) {
    $capture = UInt $instance.capture_index
    $tlas = UInt $instance.tlas_index
    $key = Key $capture $tlas
    $duplicate = $instanceByKey.ContainsKey($key)
    $checks.Add((New-Check "unique TLAS identity $key" `
        (!$duplicate) $duplicate $false)) | Out-Null
    $instanceByKey[$key] = $instance
    if (!$submissionByCapture.ContainsKey($capture)) {
        $submissionByCapture[$capture] = @{}
    }
    $submission = UInt $instance.submission_index
    $submissionDuplicate = $submissionByCapture[$capture].ContainsKey($submission)
    $checks.Add((New-Check "unique submission identity $capture`:$submission" `
        (!$submissionDuplicate) $submissionDuplicate $false)) | Out-Null
    $submissionByCapture[$capture][$submission] = $true
    $auditId = UInt $instance.reflection_audit_object_id
    $objectKey = Key $capture $auditId
    $checks.Add((New-Check "nonzero TLAS audit identity $key" `
        ($auditId -ne 0) $auditId "nonzero")) | Out-Null
    $checks.Add((New-Check "TLAS audit identity resolves $key" `
        ($objectByAuditKey.ContainsKey($objectKey)) $objectKey "scene object")) |
        Out-Null
    if ($objectByAuditKey.ContainsKey($objectKey)) {
        $object = $objectByAuditKey[$objectKey]
        $matches = (UInt $object.in_tlas) -ne 0 -and
            (UInt $object.tlas_index) -eq $tlas -and
            (ULong $object.render_identity) -eq (ULong $instance.render_identity)
        $checks.Add((New-Check "CPU object and TLAS identity agree $key" `
            $matches "$($object.tlas_index)/$($object.render_identity)" `
            "$tlas/$($instance.render_identity)")) | Out-Null
    }
    $boundsOrdered =
        (Number $instance.bounds_min_x) -le (Number $instance.bounds_max_x) -and
        (Number $instance.bounds_min_y) -le (Number $instance.bounds_max_y) -and
        (Number $instance.bounds_min_z) -le (Number $instance.bounds_max_z)
    $checks.Add((New-Check "ordered bounds $key" $boundsOrdered `
        "$($instance.bounds_min_x),$($instance.bounds_min_y),$($instance.bounds_min_z)..$($instance.bounds_max_x),$($instance.bounds_max_y),$($instance.bounds_max_z)" `
        "min<=max")) | Out-Null
    $checks.Add((New-Check "non-degenerate transform $key" `
        ([Math]::Abs((Number $instance.model_determinant)) -gt 1.0e-8) `
        $instance.model_determinant "abs>1e-8")) | Out-Null
}

$frameGroups = Group-ByCapture $frames
$objectGroups = Group-ByCapture $objects
$instanceGroups = Group-ByCapture $instances
$lightGroups = Group-ByCapture $lights
$probeGroups = Group-ByCapture $probes
$queueGroups = Group-ByCapture $queueCommands
$indexByCapture = @{}
foreach ($index in $auditIndex) {
    $capture = UInt $index.capture_index
    $indexByCapture[[string]$capture] = $index
    $checks.Add((New-Check "audit index contract frame $capture" `
        ((UInt $index.contract_version) -eq $ExpectedAuditContractVersion) `
        (UInt $index.contract_version) $ExpectedAuditContractVersion)) |
        Out-Null
    $applyConfidenceSource = Benchmark-Value $index "apply_confidence_source"
    $checks.Add((New-Check "Apply confidence matches DNSR resolved history frame $capture" `
        ($applyConfidenceSource -eq "ffx_resolve_temporal_history") `
        $applyConfidenceSource "ffx_resolve_temporal_history")) |
        Out-Null
    Add-ZeroCheck $checks "unknown receiver IDs frame $capture" `
        (ULong $index.unknown_receiver_count)
    Add-ZeroCheck $checks "unknown hit IDs frame $capture" `
        (ULong $index.unknown_hit_count)
    Add-ZeroCheck $checks "ordered ray stage chains frame $capture" `
        (ULong $index.invalid_stage_chain_count)
    Add-ZeroCheck $checks "GBuffer object IDs present frame $capture" `
        (ULong $index.missing_object_id_count)
    Add-ZeroCheck $checks "GBuffer object IDs resolve frame $capture" `
        (ULong $index.unknown_object_id_count)
    Add-ZeroCheck $checks "receiver IDs match CPU truth frame $capture" `
        (ULong $index.receiver_truth_mismatch_count)
    Add-ZeroCheck $checks "receiver origins match CPU bounds frame $capture" `
        (ULong $index.receiver_origin_outside_bounds_count)
    Add-ZeroCheck $checks "reflection self hits frame $capture" `
        (ULong $index.self_hit_count)
    $selfHitRejectedProperty =
        $index.PSObject.Properties["self_hit_rejected_count"]
    $checks.Add((New-Check "self-hit rejection telemetry present frame $capture" `
        ($null -ne $selfHitRejectedProperty) `
        ($null -ne $selfHitRejectedProperty) $true)) | Out-Null
    Add-ZeroCheck $checks "active below-hemisphere rays frame $capture" `
        (ULong $index.negative_hemisphere_count)
    Add-ZeroCheck $checks "single-sided hits resolve the nearest front side frame $capture" `
        (ULong $index.single_sided_back_side_hit_count)
    Add-ZeroCheck $checks "Apply object IDs present frame $capture" `
        (ULong $index.apply_missing_object_id_count)
    Add-ZeroCheck $checks "Apply object IDs resolve frame $capture" `
        (ULong $index.apply_unknown_object_id_count)
    Add-ZeroCheck $checks "Apply values finite frame $capture" `
        (ULong $index.apply_non_finite_count)
    Add-ZeroCheck $checks "mirror pixels use one Probe source frame $capture" `
        (ULong $index.apply_mirror_mixed_source_pixel_count)
    Add-ZeroCheck $checks "mirror pixels match object Probe assignment frame $capture" `
        (ULong $index.apply_mirror_assignment_mismatch_count)
    Add-ZeroCheck $checks "GBuffer Probe assignment matches CPU metadata frame $capture" `
        (ULong $index.apply_mirror_metadata_mismatch_count)
    Add-ZeroCheck $checks "mirror object Probe code is frame-stable frame $capture" `
        (ULong $index.apply_mirror_probe_code_inconsistent_count)
    Add-ZeroCheck $checks "mirror objects use one Probe source frame $capture" `
        (ULong $index.apply_mirror_multi_source_object_count)
    Add-ZeroCheck $checks "high-confidence mirror hits fully replace Probe frame $capture" `
        (ULong $index.apply_mirror_high_confidence_partial_blend_count)
    $zeroVisibilityProperty = $index.PSObject.Properties[
        "apply_mirror_high_confidence_zero_visibility_count"
    ]
    $checks.Add((New-Check "mirror visibility telemetry present frame $capture" `
        ($null -ne $zeroVisibilityProperty) `
        ($null -ne $zeroVisibilityProperty) $true)) | Out-Null
    if ($null -ne $zeroVisibilityProperty) {
        $zeroVisibilityCount = ULong $zeroVisibilityProperty.Value
        $checks.Add((New-Check "zero-visibility mirror pixels are bounded frame $capture" `
            ($zeroVisibilityCount -le
                (ULong $index.apply_mirror_sample_count)) `
            "$zeroVisibilityCount/$($index.apply_mirror_sample_count)" `
            "zero visibility<=mirror samples")) | Out-Null
    }
    $mirrorDnsrPassthroughProperty =
        $index.PSObject.Properties["apply_mirror_dnsr_passthrough_count"]
    $checks.Add((New-Check "mirror DNSR passthrough telemetry present frame $capture" `
        ($null -ne $mirrorDnsrPassthroughProperty) `
        ($null -ne $mirrorDnsrPassthroughProperty) $true)) | Out-Null
    if ($null -ne $mirrorDnsrPassthroughProperty) {
        $mirrorDnsrPassthroughCount =
            ULong $mirrorDnsrPassthroughProperty.Value
        $checks.Add((New-Check "mirror DNSR passthrough is bounded frame $capture" `
            ($mirrorDnsrPassthroughCount -le
                (ULong $index.apply_mirror_sample_count)) `
            "$mirrorDnsrPassthroughCount/$($index.apply_mirror_sample_count)" `
            "passthrough<=mirror samples")) | Out-Null
    }
    Add-ZeroCheck $checks "GBuffer audit values finite frame $capture" `
        (ULong $index.gbuffer_non_finite_count)
    Add-ZeroCheck $checks "GBuffer reflection sources non-negative frame $capture" `
        (ULong $index.gbuffer_source_negative_count)
    Add-ZeroCheck $checks "CPU/GPU receiver counters agree frame $capture" `
        (ULong $index.receiver_counter_mismatch_count)
    Add-ZeroCheck $checks "CPU/GPU incoming counters agree frame $capture" `
        (ULong $index.incoming_counter_mismatch_count)
    Add-ZeroCheck $checks "Apply alpha lookups resolve frame $capture" `
        (ULong $index.apply_alpha_lookup_missing_count)
}
$checks.Add((New-Check "one audit index per frame" `
    ($auditIndex.Count -eq $frames.Count) $auditIndex.Count $frames.Count)) |
    Out-Null

$compositionByKey = @{}
[uint64]$compositionExpectedRows = 0
[uint64]$compositionCsvExpectedRows = 0
[uint64]$compositionNonFiniteCount = 0
[uint64]$compositionNegativeCount = 0
foreach ($row in $compositionSummary) {
    $capture = UInt $row.capture_index
    $key = "$capture`:$($row.stage)"
    $compositionByKey[$key] = $row
    $finite = ULong $row.finite_count
    $nonFinite = ULong $row.non_finite_count
    $negative = ULong $row.negative_count
    $consumerPixelProperty = $row.PSObject.Properties["consumer_pixel_count"]
    $consumerFiniteProperty = $row.PSObject.Properties["consumer_finite_count"]
    $consumerNonFiniteProperty =
        $row.PSObject.Properties["consumer_non_finite_count"]
    $consumerNegativeProperty =
        $row.PSObject.Properties["consumer_negative_count"]
    $hasConsumerScope = $null -ne $consumerPixelProperty -and
        $null -ne $consumerFiniteProperty -and
        $null -ne $consumerNonFiniteProperty -and
        $null -ne $consumerNegativeProperty
    $consumerPixels = if ($hasConsumerScope) {
        ULong $consumerPixelProperty.Value
    } else { [uint64]0 }
    $consumerFinite = if ($hasConsumerScope) {
        ULong $consumerFiniteProperty.Value
    } else { [uint64]0 }
    $consumerNonFinite = if ($hasConsumerScope) {
        ULong $consumerNonFiniteProperty.Value
    } else { [uint64]0 }
    $consumerNegative = if ($hasConsumerScope) {
        ULong $consumerNegativeProperty.Value
    } else { [uint64]0 }
    $consumerScopedRadiance = $row.stage -in @(
        "dnsr_intersect_radiance",
        "dnsr_reproject_radiance",
        "dnsr_prefilter_radiance",
        "dnsr_resolve_radiance"
    ) -and $hasConsumerScope
    $signedHdrCorrectionStage = $row.stage -in @(
        "hdr_after_apply",
        "taa_or_dlss_input"
    )
    $contractNonFinite = if ($consumerScopedRadiance) {
        $consumerNonFinite
    } else { $nonFinite }
    $contractNegative = if ($signedHdrCorrectionStage) {
        [uint64]0
    } elseif ($consumerScopedRadiance) {
        $consumerNegative
    } else { $negative }
    $pixels = ULong $row.pixel_count
    $compositionExpectedRows += $pixels
    $compositionNonFiniteCount += $contractNonFinite
    $compositionNegativeCount += $contractNegative
    $checks.Add((New-Check "composition finite $key" `
        ($contractNonFinite -eq 0) $contractNonFinite 0)) | Out-Null
    $checks.Add((New-Check "composition non-negative $key" `
        ($contractNegative -eq 0) $contractNegative 0)) | Out-Null
    if ($signedHdrCorrectionStage) {
        $checks.Add((New-Check "composition signed correction semantic $key" `
            $true $negative "signed HDR correction; finite and conserved")) |
            Out-Null
    }
    if ($consumerScopedRadiance) {
        $checks.Add((New-Check "composition consumer accounting $key" `
            (($consumerFinite + $consumerNonFinite) -eq $consumerPixels) `
            ($consumerFinite + $consumerNonFinite) $consumerPixels)) |
            Out-Null
    }
    $checks.Add((New-Check "composition pixel accounting $key" `
        (($finite + $nonFinite) -eq $pixels) ($finite + $nonFinite) $pixels)) |
        Out-Null
    $unboundedHitDistanceAlpha =
        $row.stage -eq "dnsr_intersect_radiance"
    $radianceBlueAliasAlpha = $row.stage -in @(
        "dnsr_reproject_radiance",
        "dnsr_prefilter_radiance"
    )
    $alphaRangeValid = (Number $row.alpha_min) -ge -0.000001 -and
        ($unboundedHitDistanceAlpha -or $radianceBlueAliasAlpha -or
         (Number $row.alpha_max) -le 1.000001)
    $checks.Add((New-Check "composition alpha semantic range $key" `
        $alphaRangeValid "$($row.alpha_min)..$($row.alpha_max)" `
        $(if ($unboundedHitDistanceAlpha) {
            "hit-distance>=0"
        } elseif ($radianceBlueAliasAlpha) {
            "radiance-blue-alias>=0"
        } else { "0..1" }))) |
        Out-Null
    if ($row.stage -in @(
        "dnsr_intersect_confidence",
        "dnsr_reproject_confidence",
        "dnsr_resolve_confidence"
    )) {
        $checks.Add((New-Check "DNSR confidence range $key" `
            ((Number $row.luminance_min) -ge -0.000001 -and
             (Number $row.luminance_max) -le 1.000001) `
            "$($row.luminance_min)..$($row.luminance_max)" "0..1")) |
            Out-Null
    }
    if ($row.stage -eq "dnsr_prefilter_sample_count") {
        $checks.Add((New-Check "DNSR sample-count range $key" `
            ((Number $row.luminance_min) -ge -0.000001 -and
             (Number $row.luminance_max) -le 32.000001) `
            "$($row.luminance_min)..$($row.luminance_max)" "0..32")) |
            Out-Null
    }
}
$checks.Add((New-Check "one image manifest row per composition summary" `
    ($imageSnapshots.Count -eq $compositionSummary.Count) `
    $imageSnapshots.Count $compositionSummary.Count)) | Out-Null
foreach ($snapshot in $imageSnapshots) {
    $key = "$($snapshot.capture_index):$($snapshot.stage)"
    $expectedBytes = (ULong $snapshot.width) * (ULong $snapshot.height) *
        (ULong $snapshot.pixel_bytes)
    $rawBinaryProperty = $snapshot.PSObject.Properties["raw_binary"]
    $rawBinary = if ($null -eq $rawBinaryProperty) {
        1
    } else { UInt $rawBinaryProperty.Value }
    if ($rawBinary -ne 0) {
        $snapshotPath = Join-Path $AuditDirectory $snapshot.file_name
        $exists = Test-Path -LiteralPath $snapshotPath
        $actualBytes = if ($exists) {
            [uint64](Get-Item -LiteralPath $snapshotPath).Length
        } else { [uint64]0 }
        $checks.Add((New-Check "lossless image snapshot exists $key" `
            $exists $exists $true)) | Out-Null
        $checks.Add((New-Check "lossless image snapshot size $key" `
            ($actualBytes -eq $expectedBytes -and
             $actualBytes -eq (ULong $snapshot.file_bytes)) `
            "$actualBytes/$($snapshot.file_bytes)" $expectedBytes)) | Out-Null
    } else {
        $checks.Add((New-Check "compact image evidence omits raw binary $key" `
            ([string]::IsNullOrWhiteSpace($snapshot.file_name) -and
             (ULong $snapshot.file_bytes) -eq 0) `
            "$($snapshot.file_name)/$($snapshot.file_bytes)" "empty/0")) |
            Out-Null
    }
    if ((UInt $snapshot.verbose_csv) -ne 0) {
        $compositionCsvExpectedRows +=
            (ULong $snapshot.width) * (ULong $snapshot.height)
    }
    if (![string]::IsNullOrWhiteSpace($snapshot.object_id_file_name)) {
        $objectIdPath = Join-Path $AuditDirectory $snapshot.object_id_file_name
        $objectIdExists = Test-Path -LiteralPath $objectIdPath
        $objectIdBytes = if ($objectIdExists) {
            [uint64](Get-Item -LiteralPath $objectIdPath).Length
        } else { [uint64]0 }
        $checks.Add((New-Check "object-ID snapshot size $key" `
            ($objectIdExists -and
             $objectIdBytes -eq (ULong $snapshot.object_id_file_bytes)) `
            $objectIdBytes (ULong $snapshot.object_id_file_bytes))) | Out-Null
    }
}

foreach ($frame in $frames) {
    $capture = UInt $frame.capture_index
    $captureKey = [string]$capture
    $frameObjects = if ($objectGroups.ContainsKey($captureKey)) {
        @($objectGroups[$captureKey])
    } else { @() }
    $frameInstances = if ($instanceGroups.ContainsKey($captureKey)) {
        @($instanceGroups[$captureKey])
    } else { @() }
    $frameLights = if ($lightGroups.ContainsKey($captureKey)) {
        @($lightGroups[$captureKey])
    } else { @() }
    $frameProbes = if ($probeGroups.ContainsKey($captureKey)) {
        @($probeGroups[$captureKey])
    } else { @() }
    $frameQueues = if ($queueGroups.ContainsKey($captureKey)) {
        @($queueGroups[$captureKey])
    } else { @() }
    $checks.Add((New-Check "frame $capture scene object count" `
        ($frameObjects.Count -eq (UInt $frame.scene_object_count)) `
        $frameObjects.Count (UInt $frame.scene_object_count))) | Out-Null
    $checks.Add((New-Check "frame $capture instance count" `
        ($frameInstances.Count -eq (UInt $frame.instance_count)) `
        $frameInstances.Count (UInt $frame.instance_count))) | Out-Null
    $checks.Add((New-Check "frame $capture metadata light count" `
        ($frameLights.Count -eq (UInt $frame.metadata_light_count)) `
        $frameLights.Count (UInt $frame.metadata_light_count))) | Out-Null
    $checks.Add((New-Check "frame $capture metadata probe count" `
        ($frameProbes.Count -eq (UInt $frame.metadata_probe_count)) `
        $frameProbes.Count (UInt $frame.metadata_probe_count))) | Out-Null
    $checks.Add((New-Check "frame $capture metadata queue count" `
        ($frameQueues.Count -eq (UInt $frame.metadata_queue_command_count)) `
        $frameQueues.Count (UInt $frame.metadata_queue_command_count))) | Out-Null
    $checks.Add((New-Check "frame $capture Apply overflow" `
        ((UInt $frame.apply_record_overflow) -eq 0) `
        (UInt $frame.apply_record_overflow) 0)) | Out-Null
    if (!$indexByCapture.ContainsKey($captureKey)) { continue }
    $index = $indexByCapture[$captureKey]
    $checks.Add((New-Check "frame $capture ray count" `
        ((ULong $index.ray_rows) -eq (ULong $frame.candidate_records)) `
        (ULong $index.ray_rows) (ULong $frame.candidate_records))) | Out-Null
    $checks.Add((New-Check "frame $capture Apply count" `
        ((ULong $index.apply_rows) -eq (ULong $frame.apply_records)) `
        (ULong $index.apply_rows) (ULong $frame.apply_records))) | Out-Null
    $rawExpected = Number $index.apply_contribution_luminance_sum
    $blendExpected = Number $index.apply_blend_expected_luminance_sum
    $actualDelta = Number $index.hdr_actual_luminance_delta
    $tolerance = [Math]::Max(0.01, [Math]::Abs($blendExpected) * 0.005)
    $conserved = [Math]::Abs($actualDelta - $blendExpected) -le $tolerance
    $checks.Add((New-Check "frame $capture HDR Apply blend conservation" `
        $conserved "$actualDelta/$blendExpected" "delta within $tolerance")) |
        Out-Null
    $contributionReachesHdr = $rawExpected -le 0.000001 -or
        [Math]::Abs($blendExpected) -gt 0.000001
    $checks.Add((New-Check "frame $capture Apply contribution reaches HDR blend" `
        $contributionReachesHdr "$rawExpected/$blendExpected" `
        "nonzero shader contribution remains nonzero after blend")) | Out-Null
    if (!$conserved) {
        $findings.Add([pscustomobject]@{
            severity = "error"
            code = "hdr_apply_composition_not_conserved"
            capture_index = $capture
            object = "final HDR composition"
            detail = "Blend mode '$($index.apply_blend_mode)' expected HDR luminance delta $blendExpected, actual delta $actualDelta."
        }) | Out-Null
    }
    if (!$contributionReachesHdr) {
        $findings.Add([pscustomobject]@{
            severity = "error"
            code = "hdr_apply_destination_alpha_suppressed"
            capture_index = $capture
            object = "final HDR composition"
            detail = "Apply produced luminance $rawExpected, but blend mode '$($index.apply_blend_mode)' reduced the expected HDR write to $blendExpected; before-alpha range is $($index.hdr_before_alpha_min)..$($index.hdr_before_alpha_max)."
        }) | Out-Null
    }
}

foreach ($row in $runtimeObjects) {
    $key = Key (UInt $row.capture_index) (UInt $row.tlas_index)
    $receiverObserved = @(
        ULong $row.receiver_samples
        ULong $row.screen_accepted
        ULong $row.production_traces
        ULong $row.production_hits
        ULong $row.production_misses
    ) -join "/"
    $receiverGpu = @(
        ULong $row.gpu_receiver_samples
        ULong $row.gpu_screen_accepted
        ULong $row.gpu_production_traces
        ULong $row.gpu_production_hits
        ULong $row.gpu_production_misses
    ) -join "/"
    $checks.Add((New-Check "receiver counters match $key" `
        ($receiverObserved -eq $receiverGpu) $receiverObserved $receiverGpu)) |
        Out-Null
    $incomingObserved = @(
        ULong $row.incoming_theoretical_hits
        ULong $row.incoming_attribute_hits
        ULong $row.incoming_denoiser_writes
    ) -join "/"
    $incomingGpu = @(
        ULong $row.gpu_incoming_theoretical_hits
        ULong $row.gpu_incoming_attribute_hits
        ULong $row.gpu_incoming_denoiser_writes
    ) -join "/"
    $checks.Add((New-Check "incoming counters match $key" `
        ($incomingObserved -eq $incomingGpu) $incomingObserved $incomingGpu)) |
        Out-Null
}
$checks.Add((New-Check "runtime object index covers every TLAS instance" `
    ($runtimeObjects.Count -eq $instances.Count) `
    $runtimeObjects.Count $instances.Count)) | Out-Null

foreach ($queue in $queueCommands) {
    $capture = UInt $queue.capture_index
    $auditId = UInt $queue.reflection_audit_object_id
    $key = Key $capture $auditId
    $queueKind = UInt $queue.queue_kind
    $resolved = $auditId -ne 0 -and $objectByAuditKey.ContainsKey($key)
    $auxiliaryOverlay = $queueKind -eq 5 -and !$resolved
    if ($auxiliaryOverlay) {
        $auxiliaryIdentityValid = $auditId -ne 0 -and
            (ULong $queue.render_identity) -ne 0
        $checks.Add((New-Check "queue auxiliary identity valid $capture`:$($queue.queue_kind):$($queue.command_index)" `
            $auxiliaryIdentityValid "$auditId/$($queue.render_identity)" `
            "nonzero audit/render identity")) | Out-Null
    } else {
        $checks.Add((New-Check "queue command identity resolves $capture`:$($queue.queue_kind):$($queue.command_index)" `
            $resolved $auditId "scene object")) | Out-Null
    }
    if ($resolved) {
        $checks.Add((New-Check "queue render identity agrees $capture`:$($queue.queue_kind):$($queue.command_index)" `
            ((ULong $queue.render_identity) -eq
             (ULong $objectByAuditKey[$key].render_identity)) `
            (ULong $queue.render_identity) `
            (ULong $objectByAuditKey[$key].render_identity))) | Out-Null
    }
    $ordered = (UInt $queue.world_bounds_valid) -ne 0 -and
        (Number $queue.bounds_min_x) -le (Number $queue.bounds_max_x) -and
        (Number $queue.bounds_min_y) -le (Number $queue.bounds_max_y) -and
        (Number $queue.bounds_min_z) -le (Number $queue.bounds_max_z)
    $checks.Add((New-Check "queue bounds valid $capture`:$($queue.queue_kind):$($queue.command_index)" `
        $ordered $ordered $true)) | Out-Null
}

foreach ($light in $lights) {
    $key = "$($light.capture_index):$($light.light_index)"
    $intensity = Number $light.intensity
    $radius = Number $light.radius
    $finite = ![double]::IsNaN($intensity) -and
        ![double]::IsInfinity($intensity) -and
        ![double]::IsNaN($radius) -and ![double]::IsInfinity($radius)
    $rangeValid = (UInt $light.assigned_shadow_tiles) -le
        (UInt $light.requested_shadow_tiles) -and
        (UInt $light.shadow_tile_range_valid) -ne 0
    $checks.Add((New-Check "finite light parameters $key" `
        $finite "$($light.intensity)/$($light.radius)" "finite")) | Out-Null
    $checks.Add((New-Check "valid light shadow ownership $key" `
        $rangeValid "$($light.assigned_shadow_tiles)/$($light.requested_shadow_tiles)" `
        "assigned<=requested and range valid")) | Out-Null
}
foreach ($probe in $probes) {
    $key = "$($probe.capture_index):$($probe.probe_index)"
    $aggregate = (UInt $probe.probe_index) -eq [uint32]::MaxValue
    $ready = (UInt $probe.capture_resource_ready) -eq 0 -or
        ((UInt $probe.capture_descriptor_bound) -ne 0 -and
         ($aggregate -or (UInt $probe.capture_mip_count) -gt 0))
    $checks.Add((New-Check "probe ready state is consumable $key" `
        $ready "$($probe.capture_resource_ready)/$($probe.capture_descriptor_bound)/$($probe.capture_mip_count)" `
        $(if ($aggregate) {
            "aggregate ready implies bound"
        } else { "ready implies bound and mipped" }))) | Out-Null
}

$benchmarkFrameMatches = [Collections.Generic.List[object]]::new()
$benchmarkRowByCapture = @{}
$benchmarkFrameMissingCount = 0
$benchmarkFrameAmbiguousCount = 0
$benchmarkFrameOffsetMismatchCount = 0
foreach ($frame in $frames) {
    $capture = UInt $frame.capture_index
    $producerFrame = UInt $frame.frame_number
    $readbackFrame = UInt $frame.cpu_frame_number
    $candidateRecords = ULong $frame.candidate_records
    $matchingRows = @($benchmark | Where-Object {
        (ULong $_.hybrid_reflections_ray_query_full_audit_recorded_ray_count) `
            -eq $candidateRecords -and
        (UInt $_.hybrid_reflections_ray_query_full_audit_captured_frame_count) `
            -eq ($capture + 1)
    })
    if ($matchingRows.Count -eq 0) {
        ++$benchmarkFrameMissingCount
        continue
    }
    if ($matchingRows.Count -ne 1) {
        ++$benchmarkFrameAmbiguousCount
        continue
    }
    $match = $matchingRows[0]
    $benchmarkRowByCapture[[string]$capture] = $match
    $benchmarkFrame = UInt $match.rendered_frame
    $reportOffset = [int64]$benchmarkFrame - [int64]$readbackFrame
    if ($reportOffset -ne 1) {
        ++$benchmarkFrameOffsetMismatchCount
    }
    $benchmarkFrameMatches.Add([pscustomobject]@{
        capture_index = $capture
        producer_frame = $producerFrame
        gpu_readback_frame = $readbackFrame
        benchmark_sample_frame = UInt $match.sample_frame
        benchmark_rendered_frame = $benchmarkFrame
        readback_to_benchmark_offset = $reportOffset
        candidate_records = $candidateRecords
        benchmark_recorded_ray_count = ULong `
            $match.hybrid_reflections_ray_query_full_audit_recorded_ray_count
        benchmark_captured_frame_count = UInt `
            $match.hybrid_reflections_ray_query_full_audit_captured_frame_count
    }) | Out-Null
}
$benchmarkFrameMatches | Export-Csv -LiteralPath $benchmarkFrameMatchesPath `
    -NoTypeInformation -Encoding UTF8
$checks.Add((New-Check "all audited readbacks resolve to benchmark rows" `
    ($benchmarkFrameMissingCount -eq 0) $benchmarkFrameMissingCount 0)) |
    Out-Null
$checks.Add((New-Check "audit benchmark frame matches are unambiguous" `
    ($benchmarkFrameAmbiguousCount -eq 0) $benchmarkFrameAmbiguousCount 0)) |
    Out-Null
$checks.Add((New-Check "audit benchmark report frame offset" `
    ($benchmarkFrameOffsetMismatchCount -eq 0) `
    $benchmarkFrameOffsetMismatchCount 0)) | Out-Null

$legacyStageNameByIndex = @{
    0 = "deferred_hdr_before_apply"
    1 = "hdr_after_apply"
    2 = "taa_or_dlss_input"
    3 = "temporal_upscale_output"
    4 = "final_output_before_present"
}
$stageNameByIndexV3 = @{
    0 = "dnsr_intersect_radiance"
    1 = "dnsr_intersect_confidence"
    2 = "dnsr_reproject_radiance"
    3 = "dnsr_reproject_confidence"
    4 = "dnsr_prefilter_radiance"
    5 = "dnsr_prefilter_variance"
    6 = "dnsr_prefilter_sample_count"
    7 = "dnsr_resolve_radiance"
    8 = "dnsr_resolve_confidence"
    9 = "deferred_hdr_before_apply"
    10 = "hdr_after_apply"
    11 = "taa_or_dlss_input"
    12 = "temporal_upscale_output"
    13 = "final_output_before_present"
}
$imageStageContracts = [Collections.Generic.List[object]]::new()
[uint64]$badImageStageMaskCount = 0
[uint64]$badImageStageIdentityCount = 0
[uint64]$badImageStageExtentCount = 0
[uint64]$badImageStageDuplicateCount = 0
[uint64]$badObjectIdSnapshotContractCount = 0
foreach ($frame in $frames) {
    $capture = UInt $frame.capture_index
    $captureKey = [string]$capture
    $auditContractVersion = if ($indexByCapture.ContainsKey($captureKey)) {
        UInt $indexByCapture[$captureKey].contract_version
    } else { 0 }
    $stageNameByIndex = if ($auditContractVersion -ge 3) {
        $stageNameByIndexV3
    } else { $legacyStageNameByIndex }
    $stageCount = if ($auditContractVersion -ge 3) { 14 } else { 5 }
    $internalLastStageIndex = if ($auditContractVersion -ge 3) { 11 } else { 2 }
    $stageRows = @($imageSnapshots | Where-Object {
        (UInt $_.capture_index) -eq $capture
    })
    [uint32]$manifestMask = 0
    $seenStageIndices = @{}
    $objectIdNames = @{}
    foreach ($stageRow in $stageRows) {
        $stageIndex = UInt $stageRow.stage_index
        if ($stageIndex -ge $stageCount) {
            ++$badImageStageIdentityCount
            continue
        }
        if ($seenStageIndices.ContainsKey([string]$stageIndex)) {
            ++$badImageStageDuplicateCount
        }
        $seenStageIndices[[string]$stageIndex] = $true
        $manifestMask = $manifestMask -bor (1 -shl $stageIndex)
        if ($stageRow.stage -ne $stageNameByIndex[[int]$stageIndex] -or
            (UInt $stageRow.frame_number) -ne (UInt $frame.frame_number) -or
            (UInt $stageRow.image_index) -ne (UInt $frame.image_index)) {
            ++$badImageStageIdentityCount
        }
        $expectedWidth = if ($stageIndex -le $internalLastStageIndex) {
            UInt $frame.width
        } elseif ($benchmarkRowByCapture.ContainsKey($captureKey)) {
            UInt $benchmarkRowByCapture[$captureKey].temporal_upscale_display_width
        } else { UInt $stageRow.width }
        $expectedHeight = if ($stageIndex -le $internalLastStageIndex) {
            UInt $frame.height
        } elseif ($benchmarkRowByCapture.ContainsKey($captureKey)) {
            UInt $benchmarkRowByCapture[$captureKey].temporal_upscale_display_height
        } else { UInt $stageRow.height }
        if ((UInt $stageRow.width) -ne $expectedWidth -or
            (UInt $stageRow.height) -ne $expectedHeight) {
            ++$badImageStageExtentCount
        }
        if (![string]::IsNullOrWhiteSpace($stageRow.object_id_file_name)) {
            $objectIdNames[$stageRow.object_id_file_name] =
                ULong $stageRow.object_id_file_bytes
        }
    }
    $dlssOutputReady = if ($benchmarkRowByCapture.ContainsKey($captureKey)) {
        UInt $benchmarkRowByCapture[$captureKey].temporal_upscaler_dlss_output_ready
    } else { 0 }
    [uint32]$expectedMask = if ($auditContractVersion -ge 3) {
        12287
    } else { 23 }
    if ($dlssOutputReady -ne 0) {
        $expectedMask = if ($auditContractVersion -ge 3) {
            16383
        } else { 31 }
    }
    $recordedMask = UInt $frame.image_stage_mask
    if ($recordedMask -ne $expectedMask -or
        $manifestMask -ne $expectedMask -or
        $recordedMask -ne $manifestMask) {
        ++$badImageStageMaskCount
    }
    $expectedObjectIdBytes = (ULong $frame.width) *
        (ULong $frame.height) * 4
    if ((UInt $frame.object_id_readback_valid) -ne 1 -or
        $objectIdNames.Count -ne 1 -or
        @($objectIdNames.Values | Where-Object {
            (ULong $_) -ne $expectedObjectIdBytes
        }).Count -ne 0) {
        ++$badObjectIdSnapshotContractCount
    }
    $imageStageContracts.Add([pscustomobject]@{
        capture_index = $capture
        audit_contract_version = $auditContractVersion
        producer_frame = UInt $frame.frame_number
        image_index = UInt $frame.image_index
        dlss_output_ready = $dlssOutputReady
        expected_stage_mask = $expectedMask
        recorded_stage_mask = $recordedMask
        manifest_stage_mask = $manifestMask
        expected_stage_count = if ($auditContractVersion -ge 3) {
            if ($dlssOutputReady -ne 0) { 14 } else { 13 }
        } else {
            if ($dlssOutputReady -ne 0) { 5 } else { 4 }
        }
        manifest_stage_count = $stageRows.Count
        object_id_snapshot_count = $objectIdNames.Count
        object_id_expected_bytes = $expectedObjectIdBytes
    }) | Out-Null
}
$imageStageContracts | Export-Csv -LiteralPath $imageStageContractPath `
    -NoTypeInformation -Encoding UTF8
Add-ZeroCheck $checks "exact image-stage masks" $badImageStageMaskCount
Add-ZeroCheck $checks "image-stage identity errors" `
    $badImageStageIdentityCount
Add-ZeroCheck $checks "image-stage extent errors" $badImageStageExtentCount
Add-ZeroCheck $checks "duplicate image stages" $badImageStageDuplicateCount
Add-ZeroCheck $checks "object-ID snapshot contracts" `
    $badObjectIdSnapshotContractCount

$dnsrConfidenceQuality = [Collections.Generic.List[object]]::new()
$dnsrConfidenceTransitions = [Collections.Generic.List[object]]::new()
[uint64]$missingDnsrConfidenceStageCount = 0
[uint64]$unknownDnsrConfidenceObjectCount = 0
[uint64]$resolveConfidenceOwnershipMismatchCount = 0
[uint64]$expectedDnsrConfidenceQualityRows = 0
[uint64]$expectedDnsrConfidenceTransitionRows = 0
if ($nativeDnsrConfidenceAvailable) {
    foreach ($row in $nativeDnsrConfidenceQuality) {
        $dnsrConfidenceQuality.Add($row) | Out-Null
        $key = Key (UInt $row.capture_index) (UInt $row.receiver_object_id)
        if (!$objectByAuditKey.ContainsKey($key) -or
            (Number $row.confidence_min) -lt -0.000001 -or
            (Number $row.confidence_max) -gt 1.000001) {
            ++$unknownDnsrConfidenceObjectCount
        }
    }
    foreach ($row in $nativeDnsrConfidenceTransitions) {
        $dnsrConfidenceTransitions.Add($row) | Out-Null
        $key = Key (UInt $row.capture_index) (UInt $row.receiver_object_id)
        if (!$objectByAuditKey.ContainsKey($key)) {
            ++$unknownDnsrConfidenceObjectCount
        }
        if ($row.source_stage -eq "dnsr_reproject_confidence" -and
            (Number $row.max_absolute_delta) -gt 0.000001) {
            ++$resolveConfidenceOwnershipMismatchCount
        }
    }
    $expectedDnsrConfidenceQualityRows = $nativeDnsrConfidenceQuality.Count
    $expectedDnsrConfidenceTransitionRows =
        $nativeDnsrConfidenceTransitions.Count
} else {
foreach ($frame in $frames) {
    $capture = UInt $frame.capture_index
    $captureKey = [string]$capture
    if (!$indexByCapture.ContainsKey($captureKey) -or
        (UInt $indexByCapture[$captureKey].contract_version) -lt 3) {
        continue
    }
    $stageRows = @($imageSnapshots | Where-Object {
        (UInt $_.capture_index) -eq $capture
    })
    $snapshotByStage = @{}
    foreach ($stageRow in $stageRows) {
        $snapshotByStage[$stageRow.stage] = $stageRow
    }
    $confidenceStageNames = @(
        "dnsr_intersect_confidence",
        "dnsr_reproject_confidence",
        "dnsr_resolve_confidence"
    )
    $missingStages = @($confidenceStageNames | Where-Object {
        !$snapshotByStage.ContainsKey($_)
    })
    if ($missingStages.Count -ne 0) {
        $missingDnsrConfidenceStageCount += $missingStages.Count
        continue
    }
    $pixelCount = (ULong $frame.width) * (ULong $frame.height)
    $objectIdFileName = $snapshotByStage[
        "dnsr_intersect_confidence"
    ].object_id_file_name
    if ([string]::IsNullOrWhiteSpace($objectIdFileName)) {
        ++$missingDnsrConfidenceStageCount
        continue
    }
    $objectIds = Read-UInt32Snapshot `
        (Join-Path $AuditDirectory $objectIdFileName) $pixelCount
    $captureObjects = @($objects | Where-Object {
        (UInt $_.capture_index) -eq $capture
    })
    $maxObjectId = 0
    foreach ($object in $captureObjects) {
        $maxObjectId = [Math]::Max(
            $maxObjectId,
            (UInt $object.reflection_audit_object_id)
        )
    }
    $arrayLength = [int]$maxObjectId + 1
    $uniqueObjectIds = @{}
    foreach ($objectId in $objectIds) {
        if ($objectId -ne 0) {
            $uniqueObjectIds[[string]$objectId] = $true
        }
    }
    $expectedDnsrConfidenceQualityRows += $uniqueObjectIds.Count * 3
    $expectedDnsrConfidenceTransitionRows += $uniqueObjectIds.Count * 2
    $confidenceValuesByStage = @{}
    foreach ($stageName in $confidenceStageNames) {
        $stageRow = $snapshotByStage[$stageName]
        $values = Read-Float32Snapshot `
            (Join-Path $AuditDirectory $stageRow.file_name) $pixelCount
        $confidenceValuesByStage[$stageName] = $values
        $sampleCounts = New-Object 'UInt64[]' $arrayLength
        $sums = New-Object 'Double[]' $arrayLength
        $minimums = New-Object 'Double[]' $arrayLength
        $maximums = New-Object 'Double[]' $arrayLength
        $adjacentPairs = New-Object 'UInt64[]' $arrayLength
        $spatialJumps = New-Object 'UInt64[]' $arrayLength
        for ($objectIndex = 0; $objectIndex -lt $arrayLength; ++$objectIndex) {
            $minimums[$objectIndex] = [double]::PositiveInfinity
            $maximums[$objectIndex] = [double]::NegativeInfinity
        }
        $width = [int](UInt $frame.width)
        $height = [int](UInt $frame.height)
        for ($pixelIndex = 0; $pixelIndex -lt $values.Length; ++$pixelIndex) {
            $objectId = [int]$objectIds[$pixelIndex]
            if ($objectId -eq 0) { continue }
            if ($objectId -ge $arrayLength -or
                !$objectByAuditKey.ContainsKey((Key $capture $objectId))) {
                ++$unknownDnsrConfidenceObjectCount
                continue
            }
            $value = [double]$values[$pixelIndex]
            ++$sampleCounts[$objectId]
            $sums[$objectId] += $value
            $minimums[$objectId] = [Math]::Min($minimums[$objectId], $value)
            $maximums[$objectId] = [Math]::Max($maximums[$objectId], $value)
            $x = $pixelIndex % $width
            $y = [Math]::Floor($pixelIndex / $width)
            if ($x + 1 -lt $width -and
                $objectIds[$pixelIndex + 1] -eq $objectId) {
                ++$adjacentPairs[$objectId]
                if ([Math]::Abs($value -
                    [double]$values[$pixelIndex + 1]) -gt 0.35) {
                    ++$spatialJumps[$objectId]
                }
            }
            if ($y + 1 -lt $height -and
                $objectIds[$pixelIndex + $width] -eq $objectId) {
                ++$adjacentPairs[$objectId]
                if ([Math]::Abs($value -
                    [double]$values[$pixelIndex + $width]) -gt 0.35) {
                    ++$spatialJumps[$objectId]
                }
            }
        }
        foreach ($objectIdKey in $uniqueObjectIds.Keys) {
            $objectId = [int]$objectIdKey
            $object = $objectByAuditKey[(Key $capture $objectId)]
            $samples = $sampleCounts[$objectId]
            $dnsrConfidenceQuality.Add([pscustomobject]@{
                capture_index = $capture
                frame_number = UInt $frame.frame_number
                stage = $stageName
                stage_index = UInt $stageRow.stage_index
                receiver_tlas_index = UInt $object.tlas_index
                receiver_object_id = $objectId
                receiver_name = $object.renderable_name
                sample_count = $samples
                confidence_mean = if ($samples -gt 0) {
                    $sums[$objectId] / $samples
                } else { 0.0 }
                confidence_min = if ($samples -gt 0) {
                    $minimums[$objectId]
                } else { 0.0 }
                confidence_max = if ($samples -gt 0) {
                    $maximums[$objectId]
                } else { 0.0 }
                adjacent_pair_count = $adjacentPairs[$objectId]
                spatial_discontinuity_count = $spatialJumps[$objectId]
                spatial_discontinuity_ratio = if (
                    $adjacentPairs[$objectId] -gt 0
                ) {
                    $spatialJumps[$objectId] /
                        [double]$adjacentPairs[$objectId]
                } else { 0.0 }
            }) | Out-Null
        }
    }
    $transitionStages = @(
        @("dnsr_intersect_confidence", "dnsr_reproject_confidence"),
        @("dnsr_reproject_confidence", "dnsr_resolve_confidence")
    )
    foreach ($transition in $transitionStages) {
        $sourceValues = $confidenceValuesByStage[$transition[0]]
        $destinationValues = $confidenceValuesByStage[$transition[1]]
        $sampleCounts = New-Object 'UInt64[]' $arrayLength
        $largeChanges = New-Object 'UInt64[]' $arrayLength
        $increases = New-Object 'UInt64[]' $arrayLength
        $decreases = New-Object 'UInt64[]' $arrayLength
        $deltaSums = New-Object 'Double[]' $arrayLength
        $deltaMaximums = New-Object 'Double[]' $arrayLength
        for ($pixelIndex = 0; $pixelIndex -lt $sourceValues.Length; ++$pixelIndex) {
            $objectId = [int]$objectIds[$pixelIndex]
            if ($objectId -eq 0 -or $objectId -ge $arrayLength) { continue }
            $delta = [double]$destinationValues[$pixelIndex] -
                [double]$sourceValues[$pixelIndex]
            $absoluteDelta = [Math]::Abs($delta)
            ++$sampleCounts[$objectId]
            $deltaSums[$objectId] += $absoluteDelta
            $deltaMaximums[$objectId] = [Math]::Max(
                $deltaMaximums[$objectId],
                $absoluteDelta
            )
            if ($absoluteDelta -gt 0.35) {
                ++$largeChanges[$objectId]
                if ($delta -gt 0.0) {
                    ++$increases[$objectId]
                } else {
                    ++$decreases[$objectId]
                }
            }
            if ($transition[0] -eq "dnsr_reproject_confidence" -and
                $absoluteDelta -gt 0.000001) {
                ++$resolveConfidenceOwnershipMismatchCount
            }
        }
        foreach ($objectIdKey in $uniqueObjectIds.Keys) {
            $objectId = [int]$objectIdKey
            $object = $objectByAuditKey[(Key $capture $objectId)]
            $samples = $sampleCounts[$objectId]
            $dnsrConfidenceTransitions.Add([pscustomobject]@{
                capture_index = $capture
                frame_number = UInt $frame.frame_number
                receiver_tlas_index = UInt $object.tlas_index
                receiver_object_id = $objectId
                receiver_name = $object.renderable_name
                source_stage = $transition[0]
                destination_stage = $transition[1]
                sample_count = $samples
                large_transition_count = $largeChanges[$objectId]
                increase_count = $increases[$objectId]
                decrease_count = $decreases[$objectId]
                mean_absolute_delta = if ($samples -gt 0) {
                    $deltaSums[$objectId] / $samples
                } else { 0.0 }
                max_absolute_delta = $deltaMaximums[$objectId]
            }) | Out-Null
        }
    }
}
}
$badDnsrConfidenceQualityGroupCount = 0
foreach ($group in @($dnsrConfidenceQuality | Group-Object `
    capture_index,receiver_object_id)) {
    $stages = @($group.Group.stage | Sort-Object -Unique)
    if ($group.Count -ne 3 -or
        @($stages | Where-Object { $_ -notin @(
            "dnsr_intersect_confidence",
            "dnsr_reproject_confidence",
            "dnsr_resolve_confidence"
        ) }).Count -ne 0 -or $stages.Count -ne 3) {
        ++$badDnsrConfidenceQualityGroupCount
    }
}
$badDnsrConfidenceTransitionGroupCount = 0
foreach ($group in @($dnsrConfidenceTransitions | Group-Object `
    capture_index,receiver_object_id)) {
    if ($group.Count -ne 2) {
        ++$badDnsrConfidenceTransitionGroupCount
    }
}
$dnsrConfidenceQuality | Sort-Object capture_index,stage_index,receiver_object_id |
    Export-Csv -LiteralPath $dnsrConfidenceQualityPath `
        -NoTypeInformation -Encoding UTF8
$dnsrConfidenceTransitions |
    Sort-Object capture_index,source_stage,receiver_object_id |
    Export-Csv -LiteralPath $dnsrConfidenceTransitionsPath `
        -NoTypeInformation -Encoding UTF8
$checks.Add((New-Check "DNSR confidence quality row coverage" `
    ($dnsrConfidenceQuality.Count -eq $expectedDnsrConfidenceQualityRows) `
    $dnsrConfidenceQuality.Count $expectedDnsrConfidenceQualityRows)) |
    Out-Null
$checks.Add((New-Check "DNSR confidence transition row coverage" `
    ($dnsrConfidenceTransitions.Count -eq
        $expectedDnsrConfidenceTransitionRows) `
    $dnsrConfidenceTransitions.Count $expectedDnsrConfidenceTransitionRows)) |
    Out-Null
Add-ZeroCheck $checks "missing DNSR confidence snapshots" `
    $missingDnsrConfidenceStageCount
Add-ZeroCheck $checks "unknown DNSR confidence object IDs" `
    $unknownDnsrConfidenceObjectCount
Add-ZeroCheck $checks "Resolve confidence ownership mismatches" `
    $resolveConfidenceOwnershipMismatchCount
Add-ZeroCheck $checks "DNSR confidence quality stage groups" `
    $badDnsrConfidenceQualityGroupCount
Add-ZeroCheck $checks "DNSR confidence transition stage groups" `
    $badDnsrConfidenceTransitionGroupCount
$dnsrConfidenceQualityByKey = @{}
foreach ($row in $dnsrConfidenceQuality) {
    $key = "$(UInt $row.capture_index):$(UInt $row.receiver_object_id):$($row.stage)"
    $dnsrConfidenceQualityByKey[$key] = $row
}
$dnsrConfidenceTransitionByKey = @{}
foreach ($row in $dnsrConfidenceTransitions) {
    $key = "$(UInt $row.capture_index):$(UInt $row.receiver_object_id):$($row.source_stage)->$($row.destination_stage)"
    $dnsrConfidenceTransitionByKey[$key] = $row
}
[uint64]$benchmarkValidationIssueCount = 0
foreach ($row in $benchmark) {
    $benchmarkValidationIssueCount += ULong $row.framegraph_validation_issues
}
$checks.Add((New-Check "FrameGraph validation issues" `
    ($benchmarkValidationIssueCount -eq 0) $benchmarkValidationIssueCount 0)) |
    Out-Null

[uint64]$qualityDiscontinuityCount = 0
[uint64]$qualityConfidenceDiscontinuityCount = 0
[uint64]$qualityUnexplainedDiscontinuityCount = 0
[uint64]$qualityBlendContributionJumpCount = 0
$applyDiscontinuityClassNames = @(
    "source_transition",
    "same_source_different_hit",
    "same_source_same_hit",
    "same_source_miss",
    "unclassified"
)
$qualityDiscontinuityClassTotals = @{}
$qualityContributionJumpClassTotals = @{}
foreach ($classification in $applyDiscontinuityClassNames) {
    $qualityDiscontinuityClassTotals[$classification] = [uint64]0
    $qualityContributionJumpClassTotals[$classification] = [uint64]0
}
[uint64]$qualityDiscontinuityClassConservationErrors = 0
[uint64]$qualityContributionJumpClassConservationErrors = 0
$qualityByKey = @{}
foreach ($quality in $runtimeQuality) {
    $key = Key (UInt $quality.capture_index) (UInt $quality.receiver_object_id)
    $qualityByKey[$key] = $quality
    $qualityDiscontinuityCount +=
        ULong $quality.apply_blend_discontinuity_count
    $qualityConfidenceDiscontinuityCount +=
        ULong $quality.apply_confidence_discontinuity_count
    $qualityUnexplainedDiscontinuityCount +=
        ULong $quality.apply_unexplained_blend_discontinuity_count
    $qualityBlendContributionJumpCount +=
        ULong $quality.apply_blend_contribution_jump_count
    [uint64]$classifiedDiscontinuities = 0
    [uint64]$classifiedContributionJumps = 0
    foreach ($classification in $applyDiscontinuityClassNames) {
        $discontinuityProperty =
            "apply_${classification}_discontinuity_count"
        $jumpProperty =
            "apply_${classification}_contribution_jump_count"
        $discontinuityCount = ULong $quality.$discontinuityProperty
        $jumpCount = ULong $quality.$jumpProperty
        $classifiedDiscontinuities += $discontinuityCount
        $classifiedContributionJumps += $jumpCount
        $qualityDiscontinuityClassTotals[$classification] =
            [uint64]$qualityDiscontinuityClassTotals[$classification] +
                $discontinuityCount
        $qualityContributionJumpClassTotals[$classification] =
            [uint64]$qualityContributionJumpClassTotals[$classification] +
                $jumpCount
    }
    if ($classifiedDiscontinuities -ne
        (ULong $quality.apply_blend_discontinuity_count)) {
        ++$qualityDiscontinuityClassConservationErrors
    }
    if ($classifiedContributionJumps -ne
        (ULong $quality.apply_blend_contribution_jump_count)) {
        ++$qualityContributionJumpClassConservationErrors
    }
}
[uint64]$badDiscontinuityIdentityCount = 0
[uint64]$badDiscontinuityAdjacencyCount = 0
[uint64]$badDiscontinuityThresholdCount = 0
[uint64]$badDiscontinuityValueCount = 0
[uint64]$evidenceConfidenceDiscontinuityCount = 0
[uint64]$evidenceBlendContributionJumpCount = 0
$discontinuityEvidenceByKey = @{}
$discontinuityClassEvidenceByKey = @{}
$contributionJumpClassEvidenceByKey = @{}
foreach ($row in $runtimeApplyDiscontinuities) {
    $capture = UInt $row.capture_index
    $objectId = UInt $row.receiver_object_id
    $key = Key $capture $objectId
    if (!$qualityByKey.ContainsKey($key) -or
        !$objectByAuditKey.ContainsKey($key) -or
        (UInt $row.receiver_tlas_index) -ne
            (UInt $qualityByKey[$key].receiver_tlas_index)) {
        ++$badDiscontinuityIdentityCount
    }
    if (!$discontinuityEvidenceByKey.ContainsKey($key)) {
        $discontinuityEvidenceByKey[$key] = [uint64]0
    }
    $discontinuityEvidenceByKey[$key] =
        [uint64]$discontinuityEvidenceByKey[$key] + 1
    $classification = [string]$row.classification
    if ($classification -notin $applyDiscontinuityClassNames) {
        $classification = "unclassified"
        ++$badDiscontinuityValueCount
    }
    $classificationKey = "$key`:$classification"
    if (!$discontinuityClassEvidenceByKey.ContainsKey($classificationKey)) {
        $discontinuityClassEvidenceByKey[$classificationKey] = [uint64]0
        $contributionJumpClassEvidenceByKey[$classificationKey] = [uint64]0
    }
    $discontinuityClassEvidenceByKey[$classificationKey] =
        [uint64]$discontinuityClassEvidenceByKey[$classificationKey] + 1
    $x = UInt $row.pixel_x
    $y = UInt $row.pixel_y
    $neighborX = UInt $row.neighbor_x
    $neighborY = UInt $row.neighbor_y
    $manhattanDistance = [Math]::Abs([int64]$neighborX - [int64]$x) +
        [Math]::Abs([int64]$neighborY - [int64]$y)
    $expectedAxis = if ($neighborX -ne $x) { "x" } else { "y" }
    if ($manhattanDistance -ne 1 -or $row.axis -ne $expectedAxis) {
        ++$badDiscontinuityAdjacencyCount
    }
    $roughness = Number $row.roughness
    $neighborRoughness = Number $row.neighbor_roughness
    $confidence = Number $row.confidence
    $neighborConfidence = Number $row.neighbor_confidence
    $blend = Number $row.blend_weight
    $neighborBlend = Number $row.neighbor_blend_weight
    $roughnessDelta = Number $row.roughness_delta
    $confidenceDelta = Number $row.confidence_delta
    $blendDelta = Number $row.blend_delta
    $contributionDelta = Number $row.contribution_delta
    $numbers = @(
        $roughness, $neighborRoughness, $confidence, $neighborConfidence,
        $blend, $neighborBlend, $roughnessDelta, $confidenceDelta,
        $blendDelta, $contributionDelta,
        (Number $row.resolved_luminance),
        (Number $row.neighbor_resolved_luminance),
        (Number $row.probe_luminance),
        (Number $row.neighbor_probe_luminance),
        (Number $row.contribution_luminance),
        (Number $row.neighbor_contribution_luminance)
    )
    if (@($numbers | Where-Object {
        [double]::IsNaN($_) -or [double]::IsInfinity($_)
    }).Count -ne 0 -or $roughness -lt 0.0 -or $roughness -gt 1.0 -or
        $neighborRoughness -lt 0.0 -or $neighborRoughness -gt 1.0 -or
        $confidence -lt 0.0 -or $confidence -gt 1.0 -or
        $neighborConfidence -lt 0.0 -or $neighborConfidence -gt 1.0 -or
        $blend -lt 0.0 -or $blend -gt 1.0 -or
        $neighborBlend -lt 0.0 -or $neighborBlend -gt 1.0) {
        ++$badDiscontinuityValueCount
    }
    if ([Math]::Abs($roughnessDelta -
            [Math]::Abs($roughness - $neighborRoughness)) -gt 0.00001 -or
        [Math]::Abs($confidenceDelta -
            [Math]::Abs($confidence - $neighborConfidence)) -gt 0.00001 -or
        [Math]::Abs($blendDelta -
            [Math]::Abs($blend - $neighborBlend)) -gt 0.00001 -or
        $roughnessDelta -ge 0.05 -or $blendDelta -le 0.35) {
        ++$badDiscontinuityThresholdCount
    }
    if ($confidenceDelta -gt 0.35) {
        ++$evidenceConfidenceDiscontinuityCount
    }
    if ($contributionDelta -gt 0.25) {
        ++$evidenceBlendContributionJumpCount
        $contributionJumpClassEvidenceByKey[$classificationKey] =
            [uint64]$contributionJumpClassEvidenceByKey[$classificationKey] + 1
    }
}
[uint64]$badDiscontinuityPerReceiverCount = 0
[uint64]$badDiscontinuityClassEvidenceCount = 0
[uint64]$badContributionJumpClassEvidenceCount = 0
if ($rawEvidenceEnabledForLoad) {
    foreach ($quality in $runtimeQuality) {
        $key = Key (UInt $quality.capture_index) (UInt $quality.receiver_object_id)
        $evidenceCount = if ($discontinuityEvidenceByKey.ContainsKey($key)) {
            ULong $discontinuityEvidenceByKey[$key]
        } else { [uint64]0 }
        if ($evidenceCount -ne
            (ULong $quality.apply_blend_discontinuity_count)) {
            ++$badDiscontinuityPerReceiverCount
        }
        foreach ($classification in $applyDiscontinuityClassNames) {
            $classificationKey = "$key`:$classification"
            $expectedDiscontinuityProperty =
                "apply_${classification}_discontinuity_count"
            $expectedJumpProperty =
                "apply_${classification}_contribution_jump_count"
            $evidenceDiscontinuities = if (
                $discontinuityClassEvidenceByKey.ContainsKey($classificationKey)
            ) {
                ULong $discontinuityClassEvidenceByKey[$classificationKey]
            } else { [uint64]0 }
            $evidenceJumps = if (
                $contributionJumpClassEvidenceByKey.ContainsKey($classificationKey)
            ) {
                ULong $contributionJumpClassEvidenceByKey[$classificationKey]
            } else { [uint64]0 }
            if ($evidenceDiscontinuities -ne
                (ULong $quality.$expectedDiscontinuityProperty)) {
                ++$badDiscontinuityClassEvidenceCount
            }
            if ($evidenceJumps -ne (ULong $quality.$expectedJumpProperty)) {
                ++$badContributionJumpClassEvidenceCount
            }
        }
    }
    $checks.Add((New-Check "Apply discontinuity evidence row conservation" `
        ($runtimeApplyDiscontinuities.Count -eq $qualityDiscontinuityCount) `
        $runtimeApplyDiscontinuities.Count $qualityDiscontinuityCount)) | Out-Null
    $checks.Add((New-Check "Apply discontinuity evidence per receiver" `
        ($badDiscontinuityPerReceiverCount -eq 0) `
        $badDiscontinuityPerReceiverCount 0)) | Out-Null
    $checks.Add((New-Check "Apply discontinuity confidence attribution" `
        ($evidenceConfidenceDiscontinuityCount -eq
            $qualityConfidenceDiscontinuityCount) `
        $evidenceConfidenceDiscontinuityCount `
        $qualityConfidenceDiscontinuityCount)) | Out-Null
    $checks.Add((New-Check "Apply discontinuity contribution attribution" `
        ($evidenceBlendContributionJumpCount -eq
            $qualityBlendContributionJumpCount) `
        $evidenceBlendContributionJumpCount `
        $qualityBlendContributionJumpCount)) | Out-Null
    Add-ZeroCheck $checks "Apply discontinuity class evidence errors" `
        $badDiscontinuityClassEvidenceCount
    Add-ZeroCheck $checks "Apply contribution-jump class evidence errors" `
        $badContributionJumpClassEvidenceCount
} else {
    $compactDiscontinuityRows = Get-CsvDataRowCount `
        $paths.runtimeApplyDiscontinuities
    $checks.Add((New-Check "compact Apply discontinuity evidence omitted" `
        ($compactDiscontinuityRows -eq 0) $compactDiscontinuityRows 0)) |
        Out-Null
}
Add-ZeroCheck $checks "Apply unexplained blend discontinuities" `
    $qualityUnexplainedDiscontinuityCount
Add-ZeroCheck $checks "Apply discontinuity identity errors" `
    $badDiscontinuityIdentityCount
Add-ZeroCheck $checks "Apply discontinuity adjacency errors" `
    $badDiscontinuityAdjacencyCount
Add-ZeroCheck $checks "Apply discontinuity threshold errors" `
    $badDiscontinuityThresholdCount
Add-ZeroCheck $checks "Apply discontinuity non-finite/range errors" `
    $badDiscontinuityValueCount
Add-ZeroCheck $checks "Apply discontinuity class conservation errors" `
    $qualityDiscontinuityClassConservationErrors
Add-ZeroCheck $checks "Apply contribution-jump class conservation errors" `
    $qualityContributionJumpClassConservationErrors

$selectedReport = [Collections.Generic.List[object]]::new()
$selectedQuality = @($runtimeQuality | Where-Object {
    $_.receiver_name -like "*$ReceiverNamePattern*"
})
foreach ($quality in $selectedQuality) {
    $capture = UInt $quality.capture_index
    $receiver = UInt $quality.receiver_tlas_index
    $receiverObjectId = UInt $quality.receiver_object_id
    $intersectConfidence = $dnsrConfidenceQualityByKey[
        "$capture`:$receiverObjectId`:dnsr_intersect_confidence"
    ]
    $reprojectConfidence = $dnsrConfidenceQualityByKey[
        "$capture`:$receiverObjectId`:dnsr_reproject_confidence"
    ]
    $resolveConfidence = $dnsrConfidenceQualityByKey[
        "$capture`:$receiverObjectId`:dnsr_resolve_confidence"
    ]
    $intersectToReproject = $dnsrConfidenceTransitionByKey[
        "$capture`:$receiverObjectId`:dnsr_intersect_confidence->dnsr_reproject_confidence"
    ]
    $reprojectToResolve = $dnsrConfidenceTransitionByKey[
        "$capture`:$receiverObjectId`:dnsr_reproject_confidence->dnsr_resolve_confidence"
    ]
    $intersectSpatialJumps = if ($null -ne $intersectConfidence) {
        ULong $intersectConfidence.spatial_discontinuity_count
    } else { [uint64]0 }
    $reprojectSpatialJumps = if ($null -ne $reprojectConfidence) {
        ULong $reprojectConfidence.spatial_discontinuity_count
    } else { [uint64]0 }
    $resolveSpatialJumps = if ($null -ne $resolveConfidence) {
        ULong $resolveConfidence.spatial_discontinuity_count
    } else { [uint64]0 }
    $intersectToReprojectLargeChanges = if ($null -ne $intersectToReproject) {
        ULong $intersectToReproject.large_transition_count
    } else { [uint64]0 }
    $reprojectToResolveLargeChanges = if ($null -ne $reprojectToResolve) {
        ULong $reprojectToResolve.large_transition_count
    } else { [uint64]0 }
    $pairs = @($runtimePairs | Where-Object {
        (UInt $_.capture_index) -eq $capture -and
        (UInt $_.receiver_tlas_index) -eq $receiver
    })
    $expectedPairs = @($pairs | Where-Object {
        $_.hit_name -like "*$ExpectedHitNamePattern*"
    })
    [uint64]$expectedHits = 0
    [uint64]$expectedScreen = 0
    [uint64]$expectedProduction = 0
    [uint64]$expectedWrites = 0
    foreach ($pair in $expectedPairs) {
        $expectedHits += ULong $pair.total_hits
        $expectedScreen += ULong $pair.screen_accepted_hits
        $expectedProduction += ULong $pair.production_rt_hits
        $expectedWrites += ULong $pair.denoiser_writes
    }
    $adjacent = ULong $quality.adjacent_pair_count
    $applyAdjacent = ULong $quality.apply_adjacent_pair_count
    $sourceTransitionRatio = if ($adjacent -gt 0) {
        (ULong $quality.source_transition_count) / [double]$adjacent
    } else { 0.0 }
    $applyBlendRatio = if ($applyAdjacent -gt 0) {
        (ULong $quality.apply_blend_discontinuity_count) /
            [double]$applyAdjacent
    } else { 0.0 }
    $applyContributionJumpRatio = if ($applyAdjacent -gt 0) {
        (ULong $quality.apply_contribution_jump_count) /
            [double]$applyAdjacent
    } else { 0.0 }
    $selected = [pscustomobject]@{
        capture_index = $capture
        receiver_tlas_index = $receiver
        receiver_object_id = UInt $quality.receiver_object_id
        receiver_name = $quality.receiver_name
        sample_count = ULong $quality.sample_count
        screen_accepted = ULong $quality.screen_accepted
        ray_query_hits = ULong $quality.ray_query_hits
        ray_query_misses = ULong $quality.ray_query_misses
        self_hits = ULong $quality.self_hits
        negative_hemisphere_rays = ULong $quality.negative_hemisphere_rays
        dark_ray_query_hits = ULong $quality.dark_ray_query_hits
        expected_hit_name_pattern = $ExpectedHitNamePattern
        expected_total_hits = $expectedHits
        expected_screen_accepted_hits = $expectedScreen
        expected_production_rt_hits = $expectedProduction
        expected_denoiser_writes = $expectedWrites
        adjacent_pair_count = $adjacent
        source_transition_count = ULong $quality.source_transition_count
        source_transition_ratio = $sourceTransitionRatio
        hit_identity_transition_count = ULong $quality.hit_identity_transition_count
        large_luminance_jump_count = ULong $quality.large_luminance_jump_count
        apply_sample_count = ULong $quality.apply_sample_count
        apply_adjacent_pair_count = $applyAdjacent
        apply_blend_discontinuity_count = ULong $quality.apply_blend_discontinuity_count
        apply_blend_discontinuity_ratio = $applyBlendRatio
        apply_confidence_discontinuity_count =
            ULong $quality.apply_confidence_discontinuity_count
        apply_unexplained_blend_discontinuity_count =
            ULong $quality.apply_unexplained_blend_discontinuity_count
        apply_blend_contribution_jump_count =
            ULong $quality.apply_blend_contribution_jump_count
        apply_source_transition_discontinuity_count =
            ULong $quality.apply_source_transition_discontinuity_count
        apply_same_source_different_hit_discontinuity_count =
            ULong $quality.apply_same_source_different_hit_discontinuity_count
        apply_same_source_same_hit_discontinuity_count =
            ULong $quality.apply_same_source_same_hit_discontinuity_count
        apply_same_source_miss_discontinuity_count =
            ULong $quality.apply_same_source_miss_discontinuity_count
        apply_unclassified_discontinuity_count =
            ULong $quality.apply_unclassified_discontinuity_count
        apply_source_transition_contribution_jump_count =
            ULong $quality.apply_source_transition_contribution_jump_count
        apply_same_source_different_hit_contribution_jump_count =
            ULong $quality.apply_same_source_different_hit_contribution_jump_count
        apply_same_source_same_hit_contribution_jump_count =
            ULong $quality.apply_same_source_same_hit_contribution_jump_count
        apply_same_source_miss_contribution_jump_count =
            ULong $quality.apply_same_source_miss_contribution_jump_count
        apply_unclassified_contribution_jump_count =
            ULong $quality.apply_unclassified_contribution_jump_count
        dnsr_intersect_spatial_discontinuity_count = $intersectSpatialJumps
        dnsr_reproject_spatial_discontinuity_count = $reprojectSpatialJumps
        dnsr_resolve_spatial_discontinuity_count = $resolveSpatialJumps
        dnsr_intersect_to_reproject_large_transition_count =
            $intersectToReprojectLargeChanges
        dnsr_reproject_to_resolve_large_transition_count =
            $reprojectToResolveLargeChanges
        apply_contribution_jump_count = ULong $quality.apply_contribution_jump_count
        apply_large_source_disagreement_count = ULong $quality.apply_large_source_disagreement_count
    }
    $selectedReport.Add($selected) | Out-Null
    if ($expectedHits -eq 0) {
        $findings.Add([pscustomobject]@{
            severity = "error"
            code = "expected_reflector_not_hit"
            capture_index = $capture
            object = $quality.receiver_name
            detail = "No theoretical ray hit matched '$ExpectedHitNamePattern'."
        }) | Out-Null
    } elseif ($expectedProduction -gt 0 -and
        $expectedWrites -ne $expectedProduction) {
        $findings.Add([pscustomobject]@{
            severity = "error"
            code = "expected_hit_not_reaching_denoiser"
            capture_index = $capture
            object = $quality.receiver_name
            detail = "Expected-object RT hits=$expectedProduction but DNSR writes=$expectedWrites."
        }) | Out-Null
    }
    if ((ULong $quality.apply_sample_count) -eq 0) {
        $findings.Add([pscustomobject]@{
            severity = "error"
            code = "selected_receiver_missing_apply_records"
            capture_index = $capture
            object = $quality.receiver_name
            detail = "No Apply records matched the selected receiver."
        }) | Out-Null
    }
    $sameSourceMissDiscontinuities =
        ULong $quality.apply_same_source_miss_discontinuity_count
    $sameSourceMissContributionJumps =
        ULong $quality.apply_same_source_miss_contribution_jump_count
    $sameSourceSameHitContributionJumps =
        ULong $quality.apply_same_source_same_hit_contribution_jump_count
    if ($sameSourceMissContributionJumps -gt 0 -or
        $sameSourceSameHitContributionJumps -gt 0) {
        $findings.Add([pscustomobject]@{
            severity = "warning"
            code = "unphysical_reflection_provenance_discontinuity"
            capture_index = $capture
            object = $quality.receiver_name
            detail = "Unstable Apply provenance has same-source miss boundaries/jumps=$sameSourceMissDiscontinuities/$sameSourceMissContributionJumps and same-source/same-hit large contribution jumps=$sameSourceSameHitContributionJumps. Classified source-transition/different-hit/same-hit/miss/unclassified=$($quality.apply_source_transition_discontinuity_count)/$($quality.apply_same_source_different_hit_discontinuity_count)/$($quality.apply_same_source_same_hit_discontinuity_count)/$($quality.apply_same_source_miss_discontinuity_count)/$($quality.apply_unclassified_discontinuity_count); DNSR stage transitions=$intersectToReprojectLargeChanges/$reprojectToResolveLargeChanges."
        }) | Out-Null
    }
    if ($sourceTransitionRatio -gt 0.10 -and
        $applyContributionJumpRatio -gt 0.03) {
        $findings.Add([pscustomobject]@{
            severity = "warning"
            code = "reflection_source_fragmentation"
            capture_index = $capture
            object = $quality.receiver_name
            detail = "$($quality.source_transition_count) of $adjacent neighboring samples switch reflection source and the final Apply contribution-jump ratio is $applyContributionJumpRatio."
        }) | Out-Null
    }
}

[uint64]$rayRows = 0
[uint64]$applyRows = 0
[uint64]$gBufferRows = 0
[uint64]$sceneResolved = 0
[uint64]$tlasEligible = 0
[uint64]$tlasResolved = 0
foreach ($index in $auditIndex) {
    $rayRows += ULong $index.ray_rows
    $applyRows += ULong $index.apply_rows
    $gBufferRows += ULong $index.gbuffer_rows
    $sceneResolved += ULong $index.scene_receiver_identity_resolved_count
    $tlasEligible += ULong $index.tlas_eligible_receiver_ray_count
    $tlasResolved += ULong $index.tlas_eligible_receiver_resolved_count
}
$receiverResolveRatio = if ($rayRows -gt 0) {
    $sceneResolved / [double]$rayRows
} else { 0.0 }
$receiverTlasResolveRatio = if ($tlasEligible -gt 0) {
    $tlasResolved / [double]$tlasEligible
} else { 1.0 }
if ($receiverResolveRatio -lt 0.95) {
    $findings.Add([pscustomobject]@{
        severity = "warning"
        code = "receiver_identity_coverage_low"
        capture_index = -1
        object = "all"
        detail = "Receiver identity coverage is $receiverResolveRatio."
    }) | Out-Null
}
if ($receiverTlasResolveRatio -lt 0.95) {
    $findings.Add([pscustomobject]@{
        severity = "warning"
        code = "tlas_receiver_identity_coverage_low"
        capture_index = -1
        object = "TLAS-eligible receivers"
        detail = "TLAS receiver identity coverage is $receiverTlasResolveRatio."
    }) | Out-Null
}

$manifestRows = [Collections.Generic.List[object]]::new()
$rawEvidenceStates = $rawEvidenceStatesForLoad
$checks.Add((New-Check "raw-evidence mode is capture-consistent" `
    ($rawEvidenceStates.Count -eq 1) $rawEvidenceStates.Count 1)) |
    Out-Null
$rawEvidenceEnabled = $rawEvidenceStates.Count -eq 1 -and
    (UInt $rawEvidenceStates[0]) -ne 0
$rawExpectations = [ordered]@{
    rays = if ($rawEvidenceEnabled) { $rayRows } else { [uint64]0 }
    apply = if ($rawEvidenceEnabled) { $applyRows } else { [uint64]0 }
    gbuffer = if ($rawEvidenceEnabled) { $gBufferRows } else { [uint64]0 }
    composition = $compositionCsvExpectedRows
}
foreach ($name in $rawExpectations.Keys) {
    $path = $paths[$name]
    $actual = Get-CsvDataRowCount $path
    $expected = ULong $rawExpectations[$name]
    $checks.Add((New-Check "raw $name row count" `
        ($actual -eq $expected) $actual $expected)) | Out-Null
    $file = Get-Item -LiteralPath $path
    $manifestRows.Add([pscustomobject]@{
        source = $name
        path = $file.FullName
        bytes = [uint64]$file.Length
        data_rows = $actual
        expected_rows = $expected
        last_write_utc = $file.LastWriteTimeUtc.ToString("o")
    }) | Out-Null
}
foreach ($snapshot in $imageSnapshots) {
    $rawBinaryProperty = $snapshot.PSObject.Properties["raw_binary"]
    $rawBinary = if ($null -eq $rawBinaryProperty) {
        1
    } else { UInt $rawBinaryProperty.Value }
    if ($rawBinary -eq 0) { continue }
    $snapshotPath = Join-Path $AuditDirectory $snapshot.file_name
    $file = Get-Item -LiteralPath $snapshotPath
    $manifestRows.Add([pscustomobject]@{
        source = "image:$($snapshot.stage)"
        path = $file.FullName
        bytes = [uint64]$file.Length
        data_rows = (ULong $snapshot.width) * (ULong $snapshot.height)
        expected_rows = (ULong $snapshot.width) * (ULong $snapshot.height)
        last_write_utc = $file.LastWriteTimeUtc.ToString("o")
    }) | Out-Null
}
$objectIdManifestNames = @{}
foreach ($snapshot in $imageSnapshots) {
    if ([string]::IsNullOrWhiteSpace($snapshot.object_id_file_name) -or
        $objectIdManifestNames.ContainsKey($snapshot.object_id_file_name)) {
        continue
    }
    $objectIdManifestNames[$snapshot.object_id_file_name] = $true
    $objectIdPath = Join-Path $AuditDirectory $snapshot.object_id_file_name
    $objectIdFile = Get-Item -LiteralPath $objectIdPath
    $objectIdRows = [uint64]$objectIdFile.Length / 4
    $manifestRows.Add([pscustomobject]@{
        source = "object_ids:capture_$($snapshot.capture_index)"
        path = $objectIdFile.FullName
        bytes = [uint64]$objectIdFile.Length
        data_rows = $objectIdRows
        expected_rows = (ULong $snapshot.width) * (ULong $snapshot.height)
        last_write_utc = $objectIdFile.LastWriteTimeUtc.ToString("o")
    }) | Out-Null
}
$discontinuityFile = Get-Item -LiteralPath $paths.runtimeApplyDiscontinuities
$manifestRows.Add([pscustomobject]@{
    source = "runtime_apply_discontinuities"
    path = $discontinuityFile.FullName
    bytes = [uint64]$discontinuityFile.Length
    data_rows = [uint64]$runtimeApplyDiscontinuities.Count
    expected_rows = if ($rawEvidenceEnabled) {
        $qualityDiscontinuityCount
    } else { [uint64]0 }
    last_write_utc = $discontinuityFile.LastWriteTimeUtc.ToString("o")
}) | Out-Null
$manifestPath = Join-Path $AuditDirectory "source_manifest.csv"
$manifestRows | Export-Csv -LiteralPath $manifestPath `
    -NoTypeInformation -Encoding UTF8

$objectSummaryPath = Join-Path $AuditDirectory "object_summary.csv"
$pairSummaryPath = Join-Path $AuditDirectory "receiver_hit_matrix.csv"
$selectedSummaryPath = Join-Path $AuditDirectory "selected_receiver_summary.csv"
$runtimeObjects | Sort-Object capture_index,tlas_index |
    Export-Csv -LiteralPath $objectSummaryPath -NoTypeInformation -Encoding UTF8
$runtimePairs | Sort-Object capture_index,receiver_tlas_index,total_hits -Descending |
    Export-Csv -LiteralPath $pairSummaryPath -NoTypeInformation -Encoding UTF8
$selectedReport | Export-Csv -LiteralPath $selectedSummaryPath `
    -NoTypeInformation -Encoding UTF8

$passCount = @($checks | Where-Object status -eq "pass").Count
$failCount = @($checks | Where-Object status -eq "fail").Count
$errorFindingCount = @($findings | Where-Object severity -eq "error").Count
$warningFindingCount = @($findings | Where-Object severity -eq "warning").Count
$summary = [pscustomobject]@{
    verdict = if ($failCount -eq 0 -and $errorFindingCount -eq 0 -and
        $warningFindingCount -eq 0) { "pass" } else { "fail" }
    passCount = $passCount
    failCount = $failCount
    errorFindingCount = $errorFindingCount
    warningFindingCount = $warningFindingCount
    captureCount = $frames.Count
    instanceRows = $instances.Count
    sceneObjectRows = $objects.Count
    rayRows = $rayRows
    applyRows = $applyRows
    gBufferSampleRows = $gBufferRows
    compositionRows = $compositionExpectedRows
    compositionSummaryRows = $compositionSummary.Count
    lightRows = $lights.Count
    probeRows = $probes.Count
    queueCommandRows = $queueCommands.Count
    applyDiscontinuityCount = $qualityDiscontinuityCount
    applyDiscontinuityEvidenceRows = $runtimeApplyDiscontinuities.Count
    applyConfidenceDiscontinuityRows =
        $qualityConfidenceDiscontinuityCount
    applyUnexplainedDiscontinuityRows =
        $qualityUnexplainedDiscontinuityCount
    applyDiscontinuityClasses = [ordered]@{
        sourceTransition = $qualityDiscontinuityClassTotals["source_transition"]
        sameSourceDifferentHit = $qualityDiscontinuityClassTotals["same_source_different_hit"]
        sameSourceSameHit = $qualityDiscontinuityClassTotals["same_source_same_hit"]
        sameSourceMiss = $qualityDiscontinuityClassTotals["same_source_miss"]
        unclassified = $qualityDiscontinuityClassTotals["unclassified"]
    }
    applyContributionJumpClasses = [ordered]@{
        sourceTransition = $qualityContributionJumpClassTotals["source_transition"]
        sameSourceDifferentHit = $qualityContributionJumpClassTotals["same_source_different_hit"]
        sameSourceSameHit = $qualityContributionJumpClassTotals["same_source_same_hit"]
        sameSourceMiss = $qualityContributionJumpClassTotals["same_source_miss"]
        unclassified = $qualityContributionJumpClassTotals["unclassified"]
    }
    benchmarkRows = $benchmark.Count
    benchmarkColumnCount = $benchmarkColumnCount
    extendedReport = [bool]$ExtendedReport
    benchmarkLongRows = $benchmarkLongRows.Count
    benchmarkFrameMatchRows = $benchmarkFrameMatches.Count
    imageStageContractRows = $imageStageContracts.Count
    dnsrConfidenceQualityRows = $dnsrConfidenceQuality.Count
    dnsrConfidenceTransitionRows = $dnsrConfidenceTransitions.Count
    pipelineProjectionRows = $pipelineRows.Count
    resourceProjectionRows = $resourceRows.Count
    compositionNonFiniteCount = $compositionNonFiniteCount
    compositionNegativeCount = $compositionNegativeCount
    frameGraphValidationIssueCount = $benchmarkValidationIssueCount
    receiverIdentityResolveRatio = $receiverResolveRatio
    receiverTlasIdentityResolveRatio = $receiverTlasResolveRatio
    checks = @($checks)
    findings = @($findings)
    selectedReceivers = @($selectedReport)
    auditFrames = @($auditIndex)
    outputs = [pscustomobject]@{
        objectSummary = $objectSummaryPath
        receiverHitMatrix = $pairSummaryPath
        selectedReceiverSummary = $selectedSummaryPath
        runtimeApplyDiscontinuities = $paths.runtimeApplyDiscontinuities
        benchmarkFrameMatches = $benchmarkFrameMatchesPath
        imageStageContract = $imageStageContractPath
        dnsrConfidenceQuality = $dnsrConfidenceQualityPath
        dnsrConfidenceTransitions = $dnsrConfidenceTransitionsPath
        composition = $paths.composition
        compositionSummary = $paths.compositionSummary
        gBufferSamples = $paths.gbuffer
        lights = $paths.lights
        probes = $paths.probes
        queueCommands = $paths.queues
        pipeline = if ($ExtendedReport) { $pipelinePath } else { "" }
        resources = if ($ExtendedReport) { $resourcesPath } else { "" }
        benchmarkLong = if ($ExtendedReport) { $benchmarkLongPath } else { "" }
        sourceManifest = $manifestPath
        benchmark = $paths.benchmark
    }
}
$summaryPath = Join-Path $AuditDirectory "analysis.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath
Write-Host "Hybrid reflection full audit: $passCount pass / $failCount fail; findings error/warn=$errorFindingCount/$warningFindingCount"
Write-Host "Captured: frames=$($frames.Count), instances=$($instances.Count), rays=$rayRows"
Write-Host "Receiver identity coverage: $receiverResolveRatio"
foreach ($index in $auditIndex) {
    Write-Host (
        "Frame $($index.capture_index) Apply: blend=$($index.apply_blend_mode), " +
        "shader/blend/actual luminance=$($index.apply_contribution_luminance_sum)/" +
        "$($index.apply_blend_expected_luminance_sum)/" +
        "$($index.hdr_actual_luminance_delta), alpha=" +
        "$($index.hdr_before_alpha_min)..$($index.hdr_before_alpha_max)"
    )
}
foreach ($finding in $findings) {
    Write-Host "[$($finding.severity)] $($finding.code): $($finding.detail)"
}
Write-Host "Analysis: $summaryPath"
if ($Strict -and ($failCount -gt 0 -or $errorFindingCount -gt 0 -or
    $warningFindingCount -gt 0)) {
    exit 1
}
