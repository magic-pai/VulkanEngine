param(
    [switch]$Quiet
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

# Keep localized compiler output byte-stable for tools that parse stdout.
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[Console]::InputEncoding = $utf8NoBom
[Console]::OutputEncoding = $utf8NoBom
chcp.com 65001 | Out-Null

# Keep MSVC diagnostics in English so Ninja can reliably parse /showIncludes
# dependencies even on a Chinese Windows install.
[Environment]::SetEnvironmentVariable("VSLANG", "1033", "Process")

function Add-PathEntry {
    param([Parameter(Mandatory = $true)][string]$PathToAdd)

    if (-not (Test-Path -LiteralPath $PathToAdd)) {
        return
    }

    $resolved = (Resolve-Path -LiteralPath $PathToAdd).Path
    $current = [Environment]::GetEnvironmentVariable("Path", "Process")
    $parts = $current -split ";" | Where-Object { $_ -ne "" }

    if ($parts -notcontains $resolved) {
        [Environment]::SetEnvironmentVariable("Path", "$resolved;$current", "Process")
    }
}

function Get-VsInstallPath {
    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"

    if (Test-Path -LiteralPath $vswhere) {
        $path = & $vswhere -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $path -and (Test-Path -LiteralPath $path)) {
            return $path
        }
    }

    $candidates = @(
        Join-Path $programFilesX86 "Microsoft Visual Studio\2022\BuildTools",
        Join-Path $programFilesX86 "Microsoft Visual Studio\2022\Community",
        Join-Path $programFilesX86 "Microsoft Visual Studio\2022\Professional",
        Join-Path $programFilesX86 "Microsoft Visual Studio\2022\Enterprise"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

function Import-CmdEnvironment {
    param([Parameter(Mandatory = $true)][string]$BatchFile)

    $command = "`"$BatchFile`" >nul && set"
    $lines = & cmd.exe /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to import environment from $BatchFile"
    }

    foreach ($line in $lines) {
        if ($line -match "^(.*?)=(.*)$") {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }
}

if (-not $env:VULKAN_SDK -or -not (Test-Path -LiteralPath $env:VULKAN_SDK)) {
    $sdkRoot = "D:\VulkanSDK"
    if (Test-Path -LiteralPath $sdkRoot) {
        $latestSdk = Get-ChildItem -LiteralPath $sdkRoot -Directory |
            Sort-Object Name -Descending |
            Select-Object -First 1

        if ($latestSdk) {
            [Environment]::SetEnvironmentVariable("VULKAN_SDK", $latestSdk.FullName, "Process")
        }
    }
}

$vsRoot = Get-VsInstallPath
if ($vsRoot) {
    $vcvars64 = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
    if (Test-Path -LiteralPath $vcvars64) {
        Import-CmdEnvironment -BatchFile $vcvars64
    }

    [Environment]::SetEnvironmentVariable("VSLANG", "1033", "Process")

    Add-PathEntry (Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin")
    Add-PathEntry (Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja")
}

if ($env:VULKAN_SDK -and (Test-Path -LiteralPath $env:VULKAN_SDK)) {
    Add-PathEntry (Join-Path $env:VULKAN_SDK "Bin")
}

if (-not $Quiet) {
    Write-Host "Vulkan development environment loaded for this PowerShell session."
    Write-Host "VULKAN_SDK = $env:VULKAN_SDK"

    $tools = @("cl.exe", "cmake.exe", "ninja.exe", "glslc.exe", "vkconfig.exe")
    foreach ($tool in $tools) {
        $command = Get-Command $tool -ErrorAction SilentlyContinue
        if ($command) {
            Write-Host ("  {0,-10} {1}" -f $tool, $command.Source)
        } else {
            Write-Host ("  {0,-10} not found" -f $tool)
        }
    }
}
