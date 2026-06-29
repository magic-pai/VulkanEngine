param(
    [string]$ProjectRoot = "",

    [string]$ManifestPath = "",

    [string]$UnrealEditor = "",

    [string]$Scene = "",

    [string]$Camera = "",

    [string]$OutputPath = "",

    [int]$Width = 1920,

    [int]$Height = 1080,

    [int]$WaitSeconds = 8,

    [switch]$NoQuit,

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

function Find-UnrealEditor {
    if (-not [string]::IsNullOrWhiteSpace($UnrealEditor)) {
        if (Test-Path -LiteralPath $UnrealEditor -PathType Leaf) {
            return (Resolve-FullPath $UnrealEditor)
        }
        throw "UnrealEditor.exe was explicitly provided but not found: $UnrealEditor"
    }

    if (-not [string]::IsNullOrWhiteSpace($env:UE_EDITOR_EXE)) {
        if (Test-Path -LiteralPath $env:UE_EDITOR_EXE -PathType Leaf) {
            return (Resolve-FullPath $env:UE_EDITOR_EXE)
        }
        throw "UE_EDITOR_EXE points to a missing file: $env:UE_EDITOR_EXE"
    }

    if (-not [string]::IsNullOrWhiteSpace($env:UE_EDITOR_CMD)) {
        $candidate = $env:UE_EDITOR_CMD -replace "UnrealEditor-Cmd\.exe$", "UnrealEditor.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-FullPath $candidate)
        }
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

        $candidates += Get-ChildItem -LiteralPath $root -Recurse -Filter UnrealEditor.exe -File -ErrorAction SilentlyContinue
    }

    $best = $candidates |
        Sort-Object @{ Expression = { $_.FullName }; Descending = $true } |
        Select-Object -First 1

    if ($null -eq $best) {
        throw "Could not find UnrealEditor.exe. Set -UnrealEditor or UE_EDITOR_EXE."
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

$scriptPath = Resolve-FullPath ([System.IO.Path]::Combine($PSScriptRoot, "..", "tools", "unreal", "selfengine_bridge_capture_reference.py"))
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "UE bridge reference capture script not found: $scriptPath"
}

$editorExe = Find-UnrealEditor
$encodedScript = ConvertTo-UeArgumentPath $scriptPath

$arguments = @(
    "`"$uprojectPath`"",
    "-ExecutePythonScript=`"$encodedScript`"",
    "-nop4",
    "-NoSplash"
)

Write-Host "Running UE reference capture:"
Write-Host "  Editor:   $editorExe"
Write-Host "  Project:  $uprojectPath"
Write-Host "  Script:   $scriptPath"
Write-Host "  Manifest: $manifestFull"
Write-Host "  Scene:    $(if ([string]::IsNullOrWhiteSpace($Scene)) { '<first scene>' } else { $Scene })"
Write-Host "  Camera:   $(if ([string]::IsNullOrWhiteSpace($Camera)) { '<first camera>' } else { $Camera })"
Write-Host "  Output:   $(if ([string]::IsNullOrWhiteSpace($OutputPath)) { '<default bridge reference path>' } else { $OutputPath })"
Write-Host "  Size:     ${Width}x${Height}"

$oldManifest = $env:SELFENGINE_BRIDGE_MANIFEST
$oldScene = $env:SELFENGINE_BRIDGE_SCENE
$oldCamera = $env:SELFENGINE_BRIDGE_CAMERA_ID
$oldOutput = $env:SELFENGINE_REFERENCE_CAPTURE_OUTPUT
$oldWidth = $env:SELFENGINE_REFERENCE_CAPTURE_WIDTH
$oldHeight = $env:SELFENGINE_REFERENCE_CAPTURE_HEIGHT
$oldWait = $env:SELFENGINE_REFERENCE_CAPTURE_WAIT_SECONDS
$oldNoQuit = $env:SELFENGINE_REFERENCE_CAPTURE_NO_QUIT

try {
    $env:SELFENGINE_BRIDGE_MANIFEST = $manifestFull
    $env:SELFENGINE_BRIDGE_SCENE = $Scene
    $env:SELFENGINE_BRIDGE_CAMERA_ID = $Camera
    $env:SELFENGINE_REFERENCE_CAPTURE_OUTPUT = if ([string]::IsNullOrWhiteSpace($OutputPath)) { "" } else { Resolve-FullPath $OutputPath }
    $env:SELFENGINE_REFERENCE_CAPTURE_WIDTH = [string]$Width
    $env:SELFENGINE_REFERENCE_CAPTURE_HEIGHT = [string]$Height
    $env:SELFENGINE_REFERENCE_CAPTURE_WAIT_SECONDS = [string]$WaitSeconds
    $env:SELFENGINE_REFERENCE_CAPTURE_NO_QUIT = if ($NoQuit.IsPresent) { "1" } else { "0" }

    $process = Start-Process `
        -FilePath $editorExe `
        -ArgumentList $arguments `
        -Wait `
        -PassThru `
        -NoNewWindow
} finally {
    $env:SELFENGINE_BRIDGE_MANIFEST = $oldManifest
    $env:SELFENGINE_BRIDGE_SCENE = $oldScene
    $env:SELFENGINE_BRIDGE_CAMERA_ID = $oldCamera
    $env:SELFENGINE_REFERENCE_CAPTURE_OUTPUT = $oldOutput
    $env:SELFENGINE_REFERENCE_CAPTURE_WIDTH = $oldWidth
    $env:SELFENGINE_REFERENCE_CAPTURE_HEIGHT = $oldHeight
    $env:SELFENGINE_REFERENCE_CAPTURE_WAIT_SECONDS = $oldWait
    $env:SELFENGINE_REFERENCE_CAPTURE_NO_QUIT = $oldNoQuit
}

if ($process.ExitCode -ne 0) {
    $manifestUpdated = $false
    if (Test-Path -LiteralPath $manifestFull -PathType Leaf) {
        $manifestUpdated = (Get-Item -LiteralPath $manifestFull).LastWriteTimeUtc -gt $manifestLastWriteUtc
    }

    if ($AllowNonZeroExitWithManifestUpdate.IsPresent -and $manifestUpdated) {
        Write-Warning "UnrealEditor.exe exited with code $($process.ExitCode), but the bridge manifest was updated. Treating this as a usable capture attempt; inspect UE warnings in the captured log."
    } else {
        throw "UnrealEditor.exe exited with code $($process.ExitCode)"
    }
}

Write-Host "UE reference capture finished."
