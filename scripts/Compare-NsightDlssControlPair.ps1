param(
    [Parameter(Mandatory = $true)]
    [string]$KResultPath,
    [Parameter(Mandatory = $true)]
    [string]$MResultPath,
    [string]$KInspectionPath = "",
    [string]$MInspectionPath = "",
    [string]$OutputPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return (Resolve-Path -LiteralPath $Path).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path $repoRoot $Path)).Path
}

function Read-JsonFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    return Get-Content -LiteralPath (Resolve-RepoPath $Path) -Raw | ConvertFrom-Json
}

function Get-PropertyValue {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        $Default = $null
    )

    if ($null -eq $Object) {
        return $Default
    }

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $Default
    }

    return $property.Value
}

function Get-NestedValue {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string[]]$Path,
        $Default = $null
    )

    $current = $Object
    foreach ($name in $Path) {
        $current = Get-PropertyValue $current $name $null
        if ($null -eq $current) {
            return $Default
        }
    }

    return $current
}

function Get-BoolValue {
    param($Value)

    if ($Value -is [bool]) {
        return $Value
    }

    if ($null -eq $Value) {
        return $false
    }

    return "$Value" -eq "true" -or "$Value" -eq "1"
}

function Get-InspectionPath {
    param(
        [Parameter(Mandatory = $true)]$Result,
        [AllowEmptyString()][string]$OverridePath
    )

    if ($OverridePath.Trim().Length -gt 0) {
        return Resolve-RepoPath $OverridePath
    }

    $embeddedPath = Get-PropertyValue $Result "inspection" ""
    if ($embeddedPath.Trim().Length -gt 0 -and (Test-Path -LiteralPath $embeddedPath)) {
        return (Resolve-Path -LiteralPath $embeddedPath).Path
    }

    $capturePath = Get-PropertyValue $Result "capture" ""
    if ($capturePath.Trim().Length -gt 0) {
        $candidate = [System.IO.Path]::ChangeExtension($capturePath, ".inspection.json")
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return ""
}

function New-CaptureSummary {
    param(
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)]$Inspection,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $metadataGate = Get-PropertyValue $Result "metadataGate" $null
    $csvContract = Get-NestedValue $Result @("metadataGate", "csvContract") $null
    $inspectionPackage = Get-PropertyValue $Result "inspectionPackage" $null

    $productionBlockedByResult =
        Get-BoolValue (Get-PropertyValue $Result "productionReadyBlockedByKnownNgxIsolation" $null)
    $productionBlockedByPackage =
        Get-BoolValue (Get-NestedValue $Result @("inspectionPackage", "productionCleanBlockedByKnownNgxIsolation") $null)
    $productionBlockedByInspection =
        Get-BoolValue (Get-NestedValue $Inspection @("benchmarkLog", "productionCleanBlockedByKnownNgxIsolation") $null)
    $productionBlocked =
        $productionBlockedByResult -or
        $productionBlockedByPackage -or
        $productionBlockedByInspection

    $productionStrictCleanFromResult =
        Get-PropertyValue $Result "productionStrictValidationClean" $null
    $productionStrictCleanFromInspection =
        Get-NestedValue $Inspection @("benchmarkLog", "productionStrictValidationClean") $null
    if ($null -ne $productionStrictCleanFromResult) {
        $productionStrictCleanSource = $productionStrictCleanFromResult
    } else {
        $productionStrictCleanSource = $productionStrictCleanFromInspection
    }
    $productionStrictClean = Get-BoolValue $productionStrictCleanSource

    $metadataGateReady =
        (Get-BoolValue (Get-PropertyValue $metadataGate "requiredEnvironmentReady" $false)) -and
        (Get-BoolValue (Get-PropertyValue $metadataGate "requiredObjectNamesReady" $false)) -and
        (Get-BoolValue (Get-PropertyValue $metadataGate "embeddedErrorLogsReady" $false)) -and
        (Get-BoolValue (Get-PropertyValue $metadataGate "csvContractReady" $false))
    $manualInspectionReady =
        (Get-BoolValue (Get-PropertyValue $inspectionPackage "readyForManualNsightInspection" $false)) -or
        (Get-BoolValue (Get-PropertyValue $Inspection "readyForManualNsightInspection" $false))

    return [pscustomobject][ordered]@{
        label = $Label
        capture = Get-PropertyValue $Result "capture" ""
        screenshot = Get-PropertyValue $Result "screenshot" ""
        resolution = Get-PropertyValue $Result "resolution" ""
        monitorIndex = Get-PropertyValue $Result "defaultMonitorIndex" ""
        monitorDevice = Get-PropertyValue $Result "defaultMonitorDevice" ""
        validationClean = Get-BoolValue (Get-PropertyValue $Result "validationClean" $false)
        captureLogValidationClean = Get-BoolValue (Get-PropertyValue $Result "captureLogValidationClean" (Get-PropertyValue $Result "validationClean" $false))
        productionStrictValidationClean = $productionStrictClean
        productionBlockedByKnownNgxIsolation = $productionBlocked
        preset = Get-NestedValue $Result @("captureEnvironment", "SE_DLSS_PRESET") ""
        quality = Get-NestedValue $Result @("captureEnvironment", "SE_DLSS_QUALITY") ""
        renderScale = Get-NestedValue $Result @("captureEnvironment", "SE_RENDER_SCALE") ""
        renderView = Get-NestedValue $Result @("captureEnvironment", "SE_RENDER_VIEW") ""
        objectMotion = Get-NestedValue $Result @("captureEnvironment", "SE_BENCHMARK_OBJECT_MOTION") ""
        objectMotionSpeed = Get-NestedValue $Result @("captureEnvironment", "SE_BENCHMARK_OBJECT_MOTION_SPEED") ""
        objectMotionRadius = Get-NestedValue $Result @("captureEnvironment", "SE_BENCHMARK_OBJECT_MOTION_RADIUS") ""
        borderless = Get-NestedValue $Result @("captureEnvironment", "SE_WINDOW_BORDERLESS") ""
        hiddenImgui = Get-NestedValue $Result @("captureEnvironment", "SE_VISUAL_QA_HIDE_IMGUI") ""
        skinnedFbxProduction = Get-NestedValue $Result @("captureEnvironment", "SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION") ""
        mvJittered = Get-NestedValue $Result @("captureEnvironment", "SE_DLSS_CREATE_FLAG_MV_JITTERED") ""
        velocityJitterPolicy = Get-NestedValue $Result @("captureEnvironment", "SE_TEMPORAL_VELOCITY_JITTER_POLICY") ""
        metadataGateReady = $metadataGateReady
        readyForManualNsightInspection = $manualInspectionReady
        evaluateEventCount = Get-PropertyValue $inspectionPackage "evaluateEventCount" (Get-NestedValue $Inspection @("functionStream", "evaluateEventCount") "")
        evaluateBracketedByDebugLabel = Get-PropertyValue $inspectionPackage "evaluateBracketedByDebugLabel" (Get-NestedValue $Inspection @("functionStream", "evaluateBracketedByDebugLabel") "")
        requiredObjectNamesReady = Get-PropertyValue $inspectionPackage "requiredObjectNamesReady" (Get-NestedValue $Inspection @("objects", "requiredObjectNamesReady") "")
        resourceTraceReady = Get-PropertyValue $inspectionPackage "resourceTraceReady" (Get-NestedValue $Inspection @("benchmarkLog", "resourceTraceReady") "")
        resourceTraceContractReady = Get-PropertyValue $inspectionPackage "resourceTraceContractReady" (Get-NestedValue $Inspection @("benchmarkLog", "resourceTraceContractReady") "")
        lifecycleTraceReady = Get-PropertyValue $inspectionPackage "lifecycleTraceReady" (Get-NestedValue $Inspection @("benchmarkLog", "lifecycleTraceReady") "")
        knownNgxIsolationAttributionReady = Get-PropertyValue $inspectionPackage "knownNgxIsolationAttributionReady" (Get-NestedValue $Inspection @("benchmarkLog", "knownNgxIsolationAttributionReady") "")
        suppressedEvaluateResourceOverlapCount = Get-PropertyValue $inspectionPackage "suppressedEvaluateResourceOverlapCount" (Get-NestedValue $Inspection @("benchmarkLog", "suppressedEvaluateResourceOverlapCount") "")
        suppressedNgxInternalLayoutResources = Get-NestedValue $Inspection @("benchmarkLog", "suppressedNgxInternalLayoutResources") @()
        csvPreset = Get-PropertyValue $csvContract "dlssPreset" ""
        csvDlssEvaluateOutput = Get-PropertyValue $csvContract "dlssEvaluateOutput" ""
        csvQualityMasks = Get-PropertyValue $csvContract "qualityMasks" ""
        csvInputExtents = Get-PropertyValue $csvContract "dlssInputExtents" ""
        csvOutputExtent = Get-PropertyValue $csvContract "dlssOutputExtent" ""
        csvMotionVectorScale = Get-PropertyValue $csvContract "dlssMotionVectorScale" ""
        csvDlssJitter = Get-PropertyValue $csvContract "dlssJitter" ""
        csvTemporalJitter = Get-PropertyValue $csvContract "temporalJitter" ""
        csvJitteredHistory = Get-PropertyValue $csvContract "jitteredHistory" ""
        csvSkinnedProduction = Get-PropertyValue $csvContract "skinnedProduction" ""
    }
}

function Add-Mismatch {
    param(
        [System.Collections.ArrayList]$List,
        [Parameter(Mandatory = $true)][string]$Name,
        $KValue,
        $MValue
    )

    if ("$KValue" -ne "$MValue") {
        [void]$List.Add([ordered]@{
            field = $Name
            k = "$KValue"
            m = "$MValue"
        })
    }
}

$kResult = Read-JsonFile $KResultPath
$mResult = Read-JsonFile $MResultPath

$resolvedKInspectionPath = Get-InspectionPath $kResult $KInspectionPath
$resolvedMInspectionPath = Get-InspectionPath $mResult $MInspectionPath
$kInspection = if ($resolvedKInspectionPath.Length -gt 0) { Read-JsonFile $resolvedKInspectionPath } else { [pscustomobject]@{} }
$mInspection = if ($resolvedMInspectionPath.Length -gt 0) { Read-JsonFile $resolvedMInspectionPath } else { [pscustomobject]@{} }

$k = New-CaptureSummary $kResult $kInspection "K control"
$m = New-CaptureSummary $mResult $mInspection "M candidate"
$kPresetValue = Get-PropertyValue $k "csvPreset" ""
$mPresetValue = Get-PropertyValue $m "csvPreset" ""
$kCapturePath = Get-PropertyValue $k "capture" ""
$mCapturePath = Get-PropertyValue $m "capture" ""

$contractFields = @(
    "resolution",
    "monitorIndex",
    "monitorDevice",
    "quality",
    "renderScale",
    "renderView",
    "objectMotion",
    "objectMotionSpeed",
    "objectMotionRadius",
    "borderless",
    "hiddenImgui",
    "skinnedFbxProduction",
    "mvJittered",
    "velocityJitterPolicy",
    "csvDlssEvaluateOutput",
    "csvQualityMasks",
    "csvInputExtents",
    "csvOutputExtent",
    "csvMotionVectorScale",
    "csvDlssJitter",
    "csvTemporalJitter",
    "csvJitteredHistory",
    "csvSkinnedProduction"
)

$contractMismatches = [System.Collections.ArrayList]::new()
foreach ($field in $contractFields) {
    Add-Mismatch $contractMismatches $field (Get-PropertyValue $k $field "") (Get-PropertyValue $m $field "")
}

$expectedPresetValuesReady =
    "$kPresetValue" -eq "11" -and
    "$mPresetValue" -eq "13"
$sameSceneContractReady = $contractMismatches.Count -eq 0
$controlsReady =
    $sameSceneContractReady -and
    $expectedPresetValuesReady -and
    [bool](Get-PropertyValue $k "metadataGateReady" $false) -and
    [bool](Get-PropertyValue $m "metadataGateReady" $false) -and
    [bool](Get-PropertyValue $k "readyForManualNsightInspection" $false) -and
    [bool](Get-PropertyValue $m "readyForManualNsightInspection" $false)
$productionBlockers = [System.Collections.ArrayList]::new()

if (-not $sameSceneContractReady) {
    [void]$productionBlockers.Add("K/M captures are not the same-scene contract.")
}
if (-not $expectedPresetValuesReady) {
    [void]$productionBlockers.Add("Expected preset values are not K=11 and M=13.")
}
if (-not [bool](Get-PropertyValue $k "productionStrictValidationClean" $false)) {
    [void]$productionBlockers.Add("K control is not strict-validation clean.")
}
if (-not [bool](Get-PropertyValue $m "productionStrictValidationClean" $false)) {
    [void]$productionBlockers.Add("M candidate is not strict-validation clean.")
}
if ([bool](Get-PropertyValue $m "productionBlockedByKnownNgxIsolation" $false)) {
    [void]$productionBlockers.Add("M candidate is blocked by known NGX internal layout isolation.")
}
if ([bool](Get-PropertyValue $m "productionBlockedByKnownNgxIsolation" $false) -and
    -not [bool](Get-PropertyValue $m "knownNgxIsolationAttributionReady" $false)) {
    [void]$productionBlockers.Add("M candidate NGX isolation attribution is not ready.")
}
if (-not [bool](Get-PropertyValue $m "readyForManualNsightInspection" $false)) {
    [void]$productionBlockers.Add("M candidate is not ready for manual Nsight inspection.")
}

$nsightUi = Get-PropertyValue $kResult "ngfxUi" (Get-PropertyValue $mResult "ngfxUi" "")
$result = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    nsightUi = $nsightUi
    kInspection = $resolvedKInspectionPath
    mInspection = $resolvedMInspectionPath
    controlsReady = $controlsReady
    sameSceneContractReady = $sameSceneContractReady
    expectedPresetValuesReady = $expectedPresetValuesReady
    productionCandidateReady = $productionBlockers.Count -eq 0
    productionBlockers = $productionBlockers
    contractMismatches = $contractMismatches
    k = $k
    m = $m
    manualNsightCommands = [ordered]@{
        openK = if ($nsightUi.Length -gt 0) { "& `"$nsightUi`" `"$kCapturePath`"" } else { "" }
        openM = if ($nsightUi.Length -gt 0) { "& `"$nsightUi`" `"$mCapturePath`"" } else { "" }
    }
    nextManualChecks = @(
        "Open the K and M captures at NVSDK_NGX_VULKAN_EvaluateFeature.",
        "Compare SelfEngine.DLSS.InputMotionVectors around the skinned FBX silhouette and cube edges.",
        "Compare SelfEngine.DLSS.InputDepth in the same disocclusion regions.",
        "Compare OutputColor versus InputColor for shimmer, ghosting, and over-softening.",
        "Do not promote M while known NGX internal layout isolation is active or strict validation is false."
    )
}

if ($OutputPath.Trim().Length -eq 0) {
    $mCapture = Get-PropertyValue $mResult "capture" ""
    if ($mCapture.Trim().Length -gt 0) {
        $OutputPath = [System.IO.Path]::ChangeExtension($mCapture, ".k-m-comparison.json")
    } else {
        $OutputPath = Join-Path $repoRoot "out\nsight_captures\k-m-comparison.json"
    }
} elseif (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
}

$result | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $OutputPath -Encoding UTF8

$result | ConvertTo-Json -Depth 8
