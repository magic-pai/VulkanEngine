param(
    [Parameter(Mandatory = $true)]
    [string]$CapturePath,
    [string]$NsightHostPath = "C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.2.0\host\windows-desktop-nomad-x64",
    [string]$BenchmarkCsvPath = "",
    [string]$BenchmarkLogPath = "",
    [string]$OutputPath = "",
    [string]$ExpectedResolution = "2560x1440",
    [int]$EvaluateEventWindow = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Resolve-NsightTool {
    param([Parameter(Mandatory = $true)][string]$ToolName)

    $candidate = Join-Path $NsightHostPath $ToolName
    if (Test-Path -LiteralPath $candidate) {
        return (Resolve-Path $candidate).Path
    }

    $command = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "$ToolName was not found. Pass -NsightHostPath or add it to PATH."
}

function Invoke-NgfxReplay {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $output = & $replayPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ngfx-replay failed with exit code $LASTEXITCODE for arguments: $($Arguments -join ' ')"
    }
    return $output
}

function Read-LastCsvRow {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolvedCsv = Resolve-Path $Path
    $rows = @(Import-Csv -LiteralPath $resolvedCsv.Path)
    if ($rows.Count -eq 0) {
        throw "Benchmark CSV has no rows: $($resolvedCsv.Path)"
    }
    return $rows[-1]
}

function Get-PropertyText {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return ""
    }
    return "$($property.Value)"
}

function Parse-TraceKeyValues {
    param([Parameter(Mandatory = $true)][string]$Line)

    $values = [ordered]@{}
    foreach ($match in [regex]::Matches($Line, "([A-Za-z0-9_]+)=([^ ]+)")) {
        $values[$match.Groups[1].Value] = $match.Groups[2].Value
    }
    return $values
}

function Convert-TraceTable {
    param([string[]]$Lines = @())

    @(
        foreach ($line in $Lines) {
            $values = Parse-TraceKeyValues $line
            [pscustomobject]@{
                line = $line
                values = $values
            }
        }
    )
}

function Get-TraceValue {
    param(
        [Parameter(Mandatory = $true)]$Trace,
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$Default = ""
    )

    if ($null -eq $Trace -or $null -eq $Trace.values) {
        return $Default
    }
    if (-not $Trace.values.Contains($Name)) {
        return $Default
    }
    return "$($Trace.values[$Name])"
}

$resolvedCapture = Resolve-Path $CapturePath
$replayPath = Resolve-NsightTool "ngfx-replay.exe"
$uiPath = Resolve-NsightTool "ngfx-ui.exe"

if ($OutputPath.Trim().Length -eq 0) {
    $OutputPath = [System.IO.Path]::ChangeExtension($resolvedCapture.Path, ".inspection.json")
} elseif (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
}

if ($BenchmarkLogPath.Trim().Length -eq 0) {
    $inferredLog = [System.IO.Path]::ChangeExtension($resolvedCapture.Path, ".benchmark.log")
    if (Test-Path -LiteralPath $inferredLog) {
        $BenchmarkLogPath = $inferredLog
    }
} elseif (-not [System.IO.Path]::IsPathRooted($BenchmarkLogPath)) {
    $BenchmarkLogPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BenchmarkLogPath))
}

if ($BenchmarkCsvPath.Trim().Length -gt 0 -and
    -not [System.IO.Path]::IsPathRooted($BenchmarkCsvPath)) {
    $BenchmarkCsvPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BenchmarkCsvPath))
}

$metadata = Invoke-NgfxReplay @("--metadata", $resolvedCapture.Path) | ConvertFrom-Json
$objects = Invoke-NgfxReplay @("--metadata-objects", $resolvedCapture.Path) | ConvertFrom-Json
$functions = Invoke-NgfxReplay @("--metadata-functions", $resolvedCapture.Path) | ConvertFrom-Json
$errorLogText = (Invoke-NgfxReplay @("--metadata-logs-errors", $resolvedCapture.Path)) -join "`n"

$ngxEvents = @(
    $functions |
        Where-Object { $_.function_name -match "NGX|DLSS|NVSDK" } |
        Select-Object event_index,function_name,sequence_id,thread_index
)
$evaluateEvents = @(
    $functions |
        Where-Object { $_.function_name -eq "NVSDK_NGX_VULKAN_EvaluateFeature" } |
        Select-Object event_index,function_name,sequence_id,thread_index
)
$firstEvaluate = $evaluateEvents | Select-Object -First 1
$evaluateWindow = @()
$evaluateBracketedByDebugLabel = $false
$preEvaluateBarrierCount = 0
$postEvaluateBarrierCount = 0
if ($null -ne $firstEvaluate) {
    $evaluateIndex = [int]$firstEvaluate.event_index
    $windowStart = $evaluateIndex - $EvaluateEventWindow
    $windowEnd = $evaluateIndex + $EvaluateEventWindow
    $evaluateWindow = @(
        $functions |
            Where-Object {
                [int]$_.event_index -ge $windowStart -and
                [int]$_.event_index -le $windowEnd
            } |
            Select-Object event_index,function_name,sequence_id,thread_index
    )
    $previousEvent = $functions |
        Where-Object { [int]$_.event_index -eq ($evaluateIndex - 1) } |
        Select-Object -First 1
    $nextEvent = $functions |
        Where-Object { [int]$_.event_index -eq ($evaluateIndex + 1) } |
        Select-Object -First 1
    $evaluateBracketedByDebugLabel =
        $null -ne $previousEvent -and
        $null -ne $nextEvent -and
        $previousEvent.function_name -eq "vkCmdBeginDebugUtilsLabelEXT" -and
        $nextEvent.function_name -eq "vkCmdEndDebugUtilsLabelEXT"
    $preEvaluateBarrierCount = @(
        $evaluateWindow |
            Where-Object {
                [int]$_.event_index -lt $evaluateIndex -and
                $_.function_name -eq "vkCmdPipelineBarrier"
            }
    ).Count
    $postEvaluateBarrierCount = @(
        $evaluateWindow |
            Where-Object {
                [int]$_.event_index -gt $evaluateIndex -and
                $_.function_name -eq "vkCmdPipelineBarrier"
            }
    ).Count
}

$selfEngineObjects = @(
    $objects |
        Where-Object { $_.object_name -like "SelfEngine.*" } |
        Select-Object type_name,object_name,uid
)
$ngxObjects = @(
    $objects |
        Where-Object { $_.object_name -like "nv.ngx*" } |
        Select-Object type_name,object_name,uid
)
$requiredObjectPrefixes = @(
    "SelfEngine.DLSS.InputColor.image",
    "SelfEngine.DLSS.InputDepth.image",
    "SelfEngine.DLSS.InputMotionVectors.image",
    "SelfEngine.DLSS.OutputColor.image",
    "SelfEngine.DLSS.BiasCurrentColorMask.image",
    "SelfEngine.DLSS.TransparencyMask.image",
    "SelfEngine.Temporal.HistoryColor.image"
)
$objectNames = @($objects | ForEach-Object { $_.object_name })
$missingObjectPrefixes = @(
    foreach ($prefix in $requiredObjectPrefixes) {
        if (-not (@($objectNames | Where-Object { $_ -like "$prefix*" }).Count -gt 0)) {
            $prefix
        }
    }
)

$benchmarkCsvSummary = [ordered]@{}
if ($BenchmarkCsvPath.Trim().Length -gt 0) {
    $row = Read-LastCsvRow $BenchmarkCsvPath
    $benchmarkCsvSummary = [ordered]@{
        benchmarkCsv = (Resolve-Path $BenchmarkCsvPath).Path
        sampleFrame = Get-PropertyText $row "sample_frame"
        renderedFrame = Get-PropertyText $row "rendered_frame"
        dlssEvaluateOutput =
            "$(Get-PropertyText $row "temporal_upscaler_dlss_evaluate_result")/$(Get-PropertyText $row "temporal_upscaler_dlss_output_ready")"
        qualityMasks =
            "$(Get-PropertyText $row "temporal_upscaler_dlss_quality_required_mask")/$(Get-PropertyText $row "temporal_upscaler_dlss_quality_ready_mask")/$(Get-PropertyText $row "temporal_upscaler_dlss_quality_blocker_mask")"
        inputExtents =
            "$(Get-PropertyText $row "temporal_upscaler_dlss_input_color_width")x$(Get-PropertyText $row "temporal_upscaler_dlss_input_color_height")/$(Get-PropertyText $row "temporal_upscaler_dlss_input_depth_width")x$(Get-PropertyText $row "temporal_upscaler_dlss_input_depth_height")/$(Get-PropertyText $row "temporal_upscaler_dlss_input_motion_vector_width")x$(Get-PropertyText $row "temporal_upscaler_dlss_input_motion_vector_height")"
        outputExtent =
            "$(Get-PropertyText $row "temporal_upscaler_dlss_output_width")x$(Get-PropertyText $row "temporal_upscaler_dlss_output_height")"
        motionVectorScale =
            "$(Get-PropertyText $row "temporal_upscaler_dlss_motion_vector_scale_x")/$(Get-PropertyText $row "temporal_upscaler_dlss_motion_vector_scale_y")"
        dlssJitter =
            "$(Get-PropertyText $row "temporal_upscaler_dlss_jitter_offset_x")/$(Get-PropertyText $row "temporal_upscaler_dlss_jitter_offset_y")"
        temporalJitter =
            "$(Get-PropertyText $row "temporal_jitter_pixels_x")/$(Get-PropertyText $row "temporal_jitter_pixels_y")"
        jitteredHistory =
            "$(Get-PropertyText $row "temporal_velocity_jittered_history_policy")/$(Get-PropertyText $row "temporal_velocity_previous_jitter_applied")"
        skinnedProduction =
            "$(Get-PropertyText $row "runtime_import_skinned_animation_unsupported")/$(Get-PropertyText $row "temporal_upscaler_dlss_quality_scene_content_motion_supported")/$(Get-PropertyText $row "temporal_velocity_object_motion_ready")"
    }
}

$resourceTraceLines = @()
$lifecycleTraceLines = @()
$suppressedNgxInternalLayoutLines = @()
if ($BenchmarkLogPath.Trim().Length -gt 0 -and
    (Test-Path -LiteralPath $BenchmarkLogPath)) {
    $logLines = Get-Content -LiteralPath $BenchmarkLogPath
    $resourceTraceLines = @(
        $logLines | Where-Object { $_ -like "SelfEngineDLSSResourceTrace*" }
    )
    $lifecycleTraceLines = @(
        $logLines | Where-Object { $_ -like "SelfEngineDLSSLifecycleTrace*" }
    )
    $suppressedNgxInternalLayoutLines = @(
        $logLines | Where-Object { $_ -like "SelfEngineVkSuppressedNgxInternalLayout*" }
    )
}

$resourceTrace = @(Convert-TraceTable $resourceTraceLines)
$lifecycleTrace = @(Convert-TraceTable $lifecycleTraceLines)
$suppressedNgxInternalLayouts = @(Convert-TraceTable $suppressedNgxInternalLayoutLines)
$productionCleanBlockedByKnownNgxIsolation =
    $suppressedNgxInternalLayouts.Count -gt 0
$latestResourceSample = @(
    $resourceTrace |
        Where-Object { $_.values.Contains("sample") -and $_.values.Contains("name") } |
        Group-Object { $_.values["sample"] } |
        Sort-Object { [int]$_.Name } |
        Select-Object -Last 1 |
        ForEach-Object { $_.Group }
)
$latestResourceByName = @{}
foreach ($entry in $latestResourceSample) {
    $name = Get-TraceValue $entry "name"
    if ($name.Length -gt 0) {
        $latestResourceByName[$name] = $entry
    }
}
$requiredTraceResources = @(
    [pscustomobject]@{ name = "inputColor"; readWrite = "0"; aspect = "0x1" },
    [pscustomobject]@{ name = "inputDepth"; readWrite = "0"; aspect = "0x2" },
    [pscustomobject]@{ name = "inputMotionVectors"; readWrite = "0"; aspect = "0x1" },
    [pscustomobject]@{ name = "inputBiasCurrentColorMask"; readWrite = "0"; aspect = "0x1" },
    [pscustomobject]@{ name = "inputTransparencyMask"; readWrite = "0"; aspect = "0x1" },
    [pscustomobject]@{ name = "outputColor"; readWrite = "1"; aspect = "0x1" }
)
$resourceTraceIssues = [System.Collections.ArrayList]::new()
$latestEvaluateImageHandles = [System.Collections.ArrayList]::new()
foreach ($required in $requiredTraceResources) {
    if (-not $latestResourceByName.ContainsKey($required.name)) {
        [void]$resourceTraceIssues.Add("missing resource trace: $($required.name)")
        continue
    }

    $entry = $latestResourceByName[$required.name]
    $image = Get-TraceValue $entry "image"
    if ($image.Length -gt 0) {
        [void]$latestEvaluateImageHandles.Add($image)
    }
    $extent = Get-TraceValue $entry "extent"
    if ($ExpectedResolution.Length -gt 0 -and $extent -ne $ExpectedResolution) {
        [void]$resourceTraceIssues.Add(
            "$($required.name) extent expected $ExpectedResolution actual $extent"
        )
    }
    $layout = Get-TraceValue $entry "intendedLayout"
    if ($layout -ne "VK_IMAGE_LAYOUT_GENERAL") {
        [void]$resourceTraceIssues.Add(
            "$($required.name) intended layout expected VK_IMAGE_LAYOUT_GENERAL actual $layout"
        )
    }
    $readWrite = Get-TraceValue $entry "readWrite"
    if ($readWrite -ne $required.readWrite) {
        [void]$resourceTraceIssues.Add(
            "$($required.name) readWrite expected $($required.readWrite) actual $readWrite"
        )
    }
    $aspect = Get-TraceValue $entry "aspect"
    if ($aspect -ne $required.aspect) {
        [void]$resourceTraceIssues.Add(
            "$($required.name) aspect expected $($required.aspect) actual $aspect"
        )
    }
}
$latestEvaluateImageHandleSet = @{}
foreach ($handle in $latestEvaluateImageHandles) {
    $latestEvaluateImageHandleSet[$handle] = $true
}
$suppressedNgxInternalLayoutHandles = @(
    $suppressedNgxInternalLayouts |
        ForEach-Object { Get-TraceValue $_ "image" } |
        Where-Object { $_.Length -gt 0 }
)
$suppressedNgxInternalLayoutResources = @(
    $suppressedNgxInternalLayouts |
        ForEach-Object { Get-TraceValue $_ "resource" } |
        Where-Object { $_.Length -gt 0 } |
        Sort-Object -Unique
)
$suppressedEvaluateResourceOverlaps = @(
    foreach ($handle in $suppressedNgxInternalLayoutHandles) {
        if ($latestEvaluateImageHandleSet.ContainsKey($handle)) {
            $handle
        }
    }
)
$suppressedAllNgxInternal =
    $suppressedNgxInternalLayouts.Count -eq 0 -or
    @(
        $suppressedNgxInternalLayoutResources |
            Where-Object { $_ -notlike "nv.ngx.dlss.*" }
    ).Count -eq 0
$resourceTraceContractReady =
    $resourceTrace.Count -gt 0 -and
    $resourceTraceIssues.Count -eq 0
$knownNgxIsolationAttributionReady =
    $suppressedNgxInternalLayouts.Count -gt 0 -and
    $suppressedAllNgxInternal -and
    $suppressedEvaluateResourceOverlaps.Count -eq 0 -and
    $resourceTraceContractReady

$manualChecklist = @(
    "Open the capture in Nsight Graphics UI.",
    "Jump to event $($firstEvaluate.event_index): NVSDK_NGX_VULKAN_EvaluateFeature.",
    "Inspect named resources: SelfEngine.DLSS.InputColor, InputDepth, InputMotionVectors, OutputColor, BiasCurrentColorMask, TransparencyMask, and SelfEngine.Temporal.HistoryColor.",
    "In InputMotionVectors, zoom around the Fist Fight B.fbx silhouette and verify skinned pixels have coherent direction/magnitude, not zero/NaN/checker artifacts.",
    "In InputDepth, inspect the same silhouette/disocclusion region for holes, reversed-depth mismatch, or stale depth.",
    "Compare InputColor and OutputColor around high-contrast cube/model edges for shimmer, ghosting, or softening.",
    "Check the optional masks are neutral/expected for opaque content and not accidentally painting the whole scene.",
    "Confirm any nv.ngx internal layout diagnostics are either absent or explicitly isolated by policy; do not treat isolated warnings as production-clean."
)

$result = [ordered]@{
    capture = $resolvedCapture.Path
    nsightUi = $uiPath
    metadata = [ordered]@{
        capturedFrame = $metadata.captured_frame
        api = $metadata.primary_api
        gpu = $metadata.primary_gpu
        resolution = $metadata.resolution
        expectedResolution = $ExpectedResolution
        resolutionReady =
            $ExpectedResolution.Length -eq 0 -or
            $metadata.resolution -eq $ExpectedResolution
        embeddedErrorLogsReady =
            $errorLogText.Trim().Length -eq 0 -or
            $errorLogText -match "No log messages found with severity >= 2"
    }
    functionStream = [ordered]@{
        eventCount = @($functions).Count
        ngxEvents = $ngxEvents
        evaluateEventCount = $evaluateEvents.Count
        evaluateEvents = $evaluateEvents
        evaluateBracketedByDebugLabel = $evaluateBracketedByDebugLabel
        preEvaluateBarrierCount = $preEvaluateBarrierCount
        postEvaluateBarrierCount = $postEvaluateBarrierCount
        evaluateWindow = $evaluateWindow
    }
    objects = [ordered]@{
        requiredObjectNamesReady = $missingObjectPrefixes.Count -eq 0
        missingObjectPrefixes = $missingObjectPrefixes
        selfEngineObjects = $selfEngineObjects
        ngxObjects = $ngxObjects
    }
    benchmarkCsv = $benchmarkCsvSummary
    benchmarkLog = [ordered]@{
        path = $BenchmarkLogPath
        resourceTraceReady = $resourceTrace.Count -gt 0
        resourceTraceContractReady = $resourceTraceContractReady
        resourceTraceIssues = $resourceTraceIssues
        lifecycleTraceReady = $lifecycleTrace.Count -gt 0
        suppressedNgxInternalLayoutCount = $suppressedNgxInternalLayouts.Count
        suppressedNgxInternalLayoutResources =
            $suppressedNgxInternalLayoutResources
        suppressedAllNgxInternal = $suppressedAllNgxInternal
        suppressedEvaluateResourceOverlapCount =
            $suppressedEvaluateResourceOverlaps.Count
        suppressedEvaluateResourceOverlaps =
            $suppressedEvaluateResourceOverlaps
        knownNgxIsolationAttributionReady =
            $knownNgxIsolationAttributionReady
        productionCleanBlockedByKnownNgxIsolation =
            $productionCleanBlockedByKnownNgxIsolation
        productionStrictValidationClean =
            -not $productionCleanBlockedByKnownNgxIsolation
        latestResourceSample = $latestResourceSample
        latestEvaluateImageHandles = $latestEvaluateImageHandles
        suppressedNgxInternalLayouts = $suppressedNgxInternalLayouts
    }
    readyForManualNsightInspection =
        $evaluateEvents.Count -gt 0 -and
        $missingObjectPrefixes.Count -eq 0 -and
        ($ExpectedResolution.Length -eq 0 -or $metadata.resolution -eq $ExpectedResolution)
    manualChecklist = $manualChecklist
}

$result | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $OutputPath -Encoding UTF8

$result | ConvertTo-Json -Depth 8
