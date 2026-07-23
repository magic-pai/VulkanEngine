[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AssetPath,
    [string]$ReportPath = "tmp\gltf_validation\report.json",
    [string]$ToolDirectory = "tmp\tools\gltf-validator-2.0.0-dev.3.10",
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$validatorVersion = "2.0.0-dev.3.10"
$validatorUrl =
    "https://registry.npmjs.org/gltf-validator/-/gltf-validator-$validatorVersion.tgz"
$validatorIntegrity =
    "odJ4k0tRkGXiDGn78yDBg+fBbAIvBnXxh3RwAta0emSxGtyagFE8B4xELB1oYe3S5RD8Ci3uZAsZaascH2LAEQ=="
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Resolve-ProjectPath([string]$Path) {
    $candidate = if ([IO.Path]::IsPathRooted($Path)) {
        $Path
    } else {
        Join-Path $projectRoot $Path
    }
    return [IO.Path]::GetFullPath($candidate)
}

$asset = Resolve-ProjectPath $AssetPath
if (!(Test-Path -LiteralPath $asset -PathType Leaf)) {
    throw "glTF asset not found: $asset"
}
$report = Resolve-ProjectPath $ReportPath
$toolRoot = Resolve-ProjectPath $ToolDirectory
$archive = Join-Path $toolRoot "gltf-validator.tgz"
$packageRoot = Join-Path $toolRoot "package"
$packageEntry = Join-Path $packageRoot "index.js"
New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($report)) | Out-Null

if (!(Test-Path -LiteralPath $archive -PathType Leaf)) {
    Invoke-WebRequest -UseBasicParsing -Uri $validatorUrl -OutFile $archive
}

$bytes = [IO.File]::ReadAllBytes($archive)
$sha512 = [Security.Cryptography.SHA512]::Create()
try {
    $actualIntegrity = [Convert]::ToBase64String($sha512.ComputeHash($bytes))
} finally {
    $sha512.Dispose()
}
if ($actualIntegrity -ne $validatorIntegrity) {
    throw "Khronos glTF Validator integrity mismatch: $actualIntegrity"
}

if (!(Test-Path -LiteralPath $packageEntry -PathType Leaf)) {
    & tar.exe -xf $archive -C $toolRoot
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $packageEntry -PathType Leaf)) {
        throw "Failed to extract Khronos glTF Validator $validatorVersion"
    }
}

$node = Get-Command node.exe -ErrorAction Stop
$adapter = Join-Path $PSScriptRoot "Validate-GltfAsset.cjs"
& $node.Source $adapter $packageRoot $asset $report
$validatorExit = $LASTEXITCODE
if ($validatorExit -gt 1) {
    throw "Khronos glTF Validator invocation failed with exit code $validatorExit"
}

$validation = Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
Write-Host (
    "glTF validation: {0} error / {1} warning / {2} info / {3} hint" -f
        $validation.issues.numErrors,
        $validation.issues.numWarnings,
        $validation.issues.numInfos,
        $validation.issues.numHints
)
Write-Host "Report: $report"

if ($Strict -and [int]$validation.issues.numErrors -gt 0) {
    exit 1
}
