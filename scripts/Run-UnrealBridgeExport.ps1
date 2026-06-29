param(
    [string]$ProjectRoot = "",

    [string]$ManifestPath = "",

    [string]$UnrealEditorCmd = "",

    [string]$Scene = "",

    [int]$MaxScenes = 0,

    [switch]$NoMeshExport,

    [switch]$ForceMeshExport,

    [switch]$AllowNonZeroExitWithManifestUpdate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-FullPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }

    return [System.IO.Path]::GetFullPath($Path)
}

function ConvertTo-UeArgumentPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }

    return (Resolve-FullPath $Path).Replace('\', '/')
}

function Find-DefaultProjectRoot {
    $searchRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:SE_UE_PROJECT_ROOT)) {
        $searchRoots += $env:SE_UE_PROJECT_ROOT
    }
    $searchRoots += "D:\UEProject"

    $candidates = @()
    foreach ($root in $searchRoots) {
        if ([string]::IsNullOrWhiteSpace($root) -or -not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }

        $directProject = Get-ChildItem -LiteralPath $root -Filter *.uproject -File -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $directProject) {
            $candidates += (Get-Item -LiteralPath $root)
        }

        $candidates += Get-ChildItem -LiteralPath $root -Recurse -Filter *.uproject -File -ErrorAction SilentlyContinue |
            ForEach-Object { $_.Directory }
    }

    $best = $candidates |
        Where-Object { $null -ne $_ } |
        Sort-Object -Property FullName -Unique |
        Sort-Object `
            @{ Expression = { Test-Path -LiteralPath ([System.IO.Path]::Combine($_.FullName, "SelfEngineBridge.json")) -PathType Leaf }; Descending = $true },
            @{ Expression = { $_.FullName }; Descending = $false } |
        Select-Object -First 1

    if ($null -eq $best) {
        throw "ProjectRoot was not provided and no .uproject was found under SE_UE_PROJECT_ROOT or D:\UEProject."
    }

    return $best.FullName
}

function Quote-PythonScriptArgument {
    param([string]$Value)

    $normalized = $Value
    if ($normalized -match "^[A-Za-z]:\\" -or $normalized.Contains("\")) {
        $normalized = $normalized.Replace('\', '/')
    }
    return "'" + ($normalized -replace "'", "\\'") + "'"
}

function Find-UnrealEditorCmd {
    if (-not [string]::IsNullOrWhiteSpace($UnrealEditorCmd)) {
        if (Test-Path -LiteralPath $UnrealEditorCmd -PathType Leaf) {
            return (Resolve-FullPath $UnrealEditorCmd)
        }
        throw "UnrealEditor-Cmd.exe was explicitly provided but not found: $UnrealEditorCmd"
    }

    if (-not [string]::IsNullOrWhiteSpace($env:UE_EDITOR_CMD)) {
        if (Test-Path -LiteralPath $env:UE_EDITOR_CMD -PathType Leaf) {
            return (Resolve-FullPath $env:UE_EDITOR_CMD)
        }
        throw "UE_EDITOR_CMD points to a missing file: $env:UE_EDITOR_CMD"
    }

    $roots = @(
        "C:\Program Files\Epic Games",
        "C:\Program Files\Unreal Engine",
        "C:\Program Files\UE",
        "D:\Epic Games",
        "D:\Unreal Engine",
        "D:\Program Files\UE",
        "D:\UE"
    )

    $candidates = @()
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }

        $candidates += Get-ChildItem -LiteralPath $root -Recurse -Filter UnrealEditor-Cmd.exe -File -ErrorAction SilentlyContinue
    }

    $best = $candidates |
        Sort-Object @{ Expression = { $_.FullName }; Descending = $true } |
        Select-Object -First 1

    if ($null -eq $best) {
        throw "Could not find UnrealEditor-Cmd.exe. Set -UnrealEditorCmd or UE_EDITOR_CMD."
    }

    return $best.FullName
}

function Find-UProject {
    param([string]$Root)

    $uproject = Get-ChildItem -LiteralPath $Root -Filter *.uproject -File | Select-Object -First 1
    if ($null -eq $uproject) {
        throw "No .uproject file found in: $Root"
    }
    return $uproject.FullName
}

$projectRootFull = if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    Find-DefaultProjectRoot
} else {
    Resolve-FullPath $ProjectRoot
}
if (-not (Test-Path -LiteralPath $projectRootFull -PathType Container)) {
    throw "ProjectRoot is not a directory: $projectRootFull"
}

$uprojectPath = Find-UProject -Root $projectRootFull
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = [System.IO.Path]::Combine($projectRootFull, "SelfEngineBridge.json")
}
$manifestFull = Resolve-FullPath $ManifestPath
if (-not (Test-Path -LiteralPath $manifestFull -PathType Leaf)) {
    throw "Manifest does not exist yet: $manifestFull. Run scripts/Generate-SelfEngineBridgeManifest.ps1 first."
}
$manifestLastWriteUtc = (Get-Item -LiteralPath $manifestFull).LastWriteTimeUtc

$scriptPath = Resolve-FullPath ([System.IO.Path]::Combine($PSScriptRoot, "..", "tools", "unreal", "selfengine_bridge_export.py"))
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "UE bridge Python script not found: $scriptPath"
}

$editorCmd = Find-UnrealEditorCmd
$encodedScript = ConvertTo-UeArgumentPath $scriptPath

$arguments = @(
    "`"$uprojectPath`"",
    "-run=pythonscript",
    "-script=`"$encodedScript`"",
    "-unattended",
    "-nop4",
    "-NoSplash"
)

Write-Host "Running UE bridge export:"
Write-Host "  Editor:  $editorCmd"
Write-Host "  Project: $uprojectPath"
Write-Host "  Script:  $scriptPath"
Write-Host "  Manifest:$manifestFull"
Write-Host "  UE -script: $encodedScript"
Write-Host "  Scene:   $(if ([string]::IsNullOrWhiteSpace($Scene)) { '<all selected scenes>' } else { $Scene })"
Write-Host "  MeshExport: $(if ($NoMeshExport.IsPresent) { 'metadata-only' } elseif ($ForceMeshExport.IsPresent) { 'force' } else { 'reuse-existing-or-export-missing' })"

$oldManifest = $env:SELFENGINE_BRIDGE_MANIFEST
$oldScene = $env:SELFENGINE_BRIDGE_SCENE
$oldMaxScenes = $env:SELFENGINE_BRIDGE_MAX_SCENES
$oldNoMeshExport = $env:SELFENGINE_BRIDGE_NO_MESH_EXPORT
$oldForceMeshExport = $env:SELFENGINE_BRIDGE_FORCE_MESH_EXPORT

try {
    $env:SELFENGINE_BRIDGE_MANIFEST = $manifestFull
    $env:SELFENGINE_BRIDGE_SCENE = $Scene
    $env:SELFENGINE_BRIDGE_MAX_SCENES = [string]$MaxScenes
    $env:SELFENGINE_BRIDGE_NO_MESH_EXPORT = if ($NoMeshExport.IsPresent) { "1" } else { "0" }
    $env:SELFENGINE_BRIDGE_FORCE_MESH_EXPORT = if ($ForceMeshExport.IsPresent) { "1" } else { "0" }

    $process = Start-Process `
        -FilePath $editorCmd `
        -ArgumentList $arguments `
        -Wait `
        -PassThru `
        -NoNewWindow
} finally {
    $env:SELFENGINE_BRIDGE_MANIFEST = $oldManifest
    $env:SELFENGINE_BRIDGE_SCENE = $oldScene
    $env:SELFENGINE_BRIDGE_MAX_SCENES = $oldMaxScenes
    $env:SELFENGINE_BRIDGE_NO_MESH_EXPORT = $oldNoMeshExport
    $env:SELFENGINE_BRIDGE_FORCE_MESH_EXPORT = $oldForceMeshExport
}

if ($process.ExitCode -ne 0) {
    $manifestUpdated = $false
    if (Test-Path -LiteralPath $manifestFull -PathType Leaf) {
        $manifestUpdated = (Get-Item -LiteralPath $manifestFull).LastWriteTimeUtc -gt $manifestLastWriteUtc
    }

    if ($AllowNonZeroExitWithManifestUpdate.IsPresent -and $manifestUpdated) {
        Write-Warning "UnrealEditor-Cmd.exe exited with code $($process.ExitCode), but the bridge manifest was updated. Treating this as a usable export; inspect UE warnings in the captured log."
    } else {
        throw "UnrealEditor-Cmd.exe exited with code $($process.ExitCode)"
    }
}

Write-Host "UE bridge export finished."
