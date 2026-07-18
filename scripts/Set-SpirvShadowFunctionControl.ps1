param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$SpirvDisPath,
    [Parameter(Mandatory = $true)][string]$SpirvAsPath,
    [Parameter(Mandatory = $true)][string]$SpirvValPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$targetFunctions = @(
    "SampleLocalShadowTileVisibility",
    "LocalShadowVisibility",
    "DirectPbrContribution",
    "AccumulateLocalLight",
    "AccumulateRectAreaLight",
    "AccumulateFrameLocalLight",
    "SampleDirectionalPcssVisibility",
    "SampleShadowCascadeVisibility",
    "ResolveShadowCascadeVisibility",
    "ShadowVisibility"
)

$resolvedInput = [IO.Path]::GetFullPath($InputPath)
$assemblyPath = "$resolvedInput.function-control.spvasm"
$outputPath = "$resolvedInput.function-control.spv"

try {
    $assembly = ((& $SpirvDisPath $resolvedInput --no-color) -join "`n")
    if ($LASTEXITCODE -ne 0) {
        throw "spirv-dis failed for $resolvedInput"
    }

    foreach ($functionName in $targetFunctions) {
        $nameMatch = [regex]::Match(
            $assembly,
            "OpName\s+(%\S+)\s+`"$([regex]::Escape($functionName))"
        )
        if (-not $nameMatch.Success) {
            continue
        }

        $functionId = [regex]::Escape($nameMatch.Groups[1].Value)
        $functionPattern =
            "(?m)^(\s*$functionId\s*=\s*OpFunction\s+\S+\s+)(None|Inline)(\s+\S+\s*)$"
        $patchedAssembly = [regex]::Replace(
            $assembly,
            $functionPattern,
            '$1DontInline$3',
            1
        )
        if ($patchedAssembly -eq $assembly -and
            $assembly -notmatch
                "(?m)^\s*$functionId\s*=\s*OpFunction\s+\S+\s+DontInline\s+") {
            throw "Could not set DontInline on $functionName in $resolvedInput"
        }
        $assembly = $patchedAssembly
    }

    [IO.File]::WriteAllText(
        $assemblyPath,
        $assembly,
        [Text.UTF8Encoding]::new($false)
    )
    & $SpirvAsPath $assemblyPath `
        --target-env spv1.0 `
        --preserve-numeric-ids `
        -o $outputPath
    if ($LASTEXITCODE -ne 0) {
        throw "spirv-as failed for $resolvedInput"
    }

    & $SpirvValPath --target-env vulkan1.3 $outputPath
    if ($LASTEXITCODE -ne 0) {
        throw "spirv-val failed for $resolvedInput"
    }

    Move-Item -LiteralPath $outputPath -Destination $resolvedInput -Force
} finally {
    Remove-Item -LiteralPath $assemblyPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $outputPath -Force -ErrorAction SilentlyContinue
}
