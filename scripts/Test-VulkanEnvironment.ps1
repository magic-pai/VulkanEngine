param(
    [switch]$SkipLoad
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$requiredFailures = 0
$warnings = 0

function Write-Result {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("OK", "WARN", "FAIL")][string]$Level,
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$Detail = ""
    )

    $color = switch ($Level) {
        "OK" { "Green" }
        "WARN" { "Yellow" }
        "FAIL" { "Red" }
    }

    $message = "[{0}] {1}" -f $Level, $Name
    if ($Detail) {
        $message = "$message - $Detail"
    }

    Write-Host $message -ForegroundColor $color
}

function Require-Path {
    param([string]$Name, [string]$Path)

    if ($Path -and (Test-Path -LiteralPath $Path)) {
        Write-Result OK $Name $Path
    } else {
        $script:requiredFailures++
        Write-Result FAIL $Name "missing: $Path"
    }
}

function Optional-Path {
    param([string]$Name, [string]$Path, [string]$Reason)

    if ($Path -and (Test-Path -LiteralPath $Path)) {
        Write-Result OK $Name $Path
    } else {
        $script:warnings++
        Write-Result WARN $Name $Reason
    }
}

function Require-Command {
    param([string]$Name, [string]$CommandName)

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($command) {
        Write-Result OK $Name $command.Source
    } else {
        $script:requiredFailures++
        Write-Result FAIL $Name "$CommandName is not available in PATH"
    }
}

function Optional-Command {
    param([string]$Name, [string]$CommandName, [string]$Reason)

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($command) {
        Write-Result OK $Name $command.Source
    } else {
        $script:warnings++
        Write-Result WARN $Name $Reason
    }
}

if (-not $SkipLoad) {
    & (Join-Path $PSScriptRoot "Enter-VulkanDev.ps1") -Quiet
}

Write-Host "Checking Vulkan learning environment in $repoRoot"

Require-Path "Vulkan SDK root" $env:VULKAN_SDK
Require-Path "Vulkan header" (Join-Path $env:VULKAN_SDK "Include\vulkan\vulkan.h")
Require-Path "Vulkan loader library" (Join-Path $env:VULKAN_SDK "Lib\vulkan-1.lib")
Require-Path "Vulkan shader compiler" (Join-Path $env:VULKAN_SDK "Bin\glslc.exe")
Require-Path "Vulkan Configurator" (Join-Path $env:VULKAN_SDK "Bin\vkconfig.exe")

Require-Command "MSVC C++ compiler" "cl.exe"
Require-Command "CMake" "cmake.exe"
Require-Command "Ninja" "ninja.exe"
Require-Command "glslc" "glslc.exe"
Require-Command "vulkaninfo" "vulkaninfo.exe"

Require-Path "GLFW header" (Join-Path $repoRoot "thirdParty\include\GLFW\glfw3.h")
Require-Path "GLFW static library" (Join-Path $repoRoot "thirdParty\lib\glfw3.lib")
Require-Path "GLM header" (Join-Path $repoRoot "thirdParty\include\glm\glm.hpp")

Optional-Path "Assimp headers" (Join-Path $repoRoot "thirdParty\include\assimp\Importer.hpp") "only needed when you start model loading"

$assimpLib = Get-ChildItem -LiteralPath (Join-Path $repoRoot "thirdParty\lib") -Filter "assimp*.lib" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($assimpLib) {
    Write-Result OK "Assimp library" $assimpLib.FullName
} else {
    $warnings++
    Write-Result WARN "Assimp library" "not needed for the first Vulkan lessons; add it before model loading"
}

Optional-Command "RenderDoc" "renderdoccmd.exe" "optional, but very useful once you debug frames"

try {
    $summary = & vulkaninfo.exe --summary 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Result OK "Vulkan runtime" "vulkaninfo --summary succeeded"
        $gpuLine = ($summary | Select-String -Pattern "deviceName\s*=" | Select-Object -First 1).Line
        if ($gpuLine) {
            Write-Host "      $gpuLine"
        }
    } else {
        $requiredFailures++
        Write-Result FAIL "Vulkan runtime" "vulkaninfo returned exit code $LASTEXITCODE"
    }
} catch {
    $requiredFailures++
    Write-Result FAIL "Vulkan runtime" $_.Exception.Message
}

Write-Host ""
if ($requiredFailures -eq 0) {
    Write-Host "Required environment is ready. Warnings are optional items for later lessons." -ForegroundColor Green
    if ($warnings -gt 0) {
        Write-Host "$warnings optional item(s) are missing or deferred." -ForegroundColor Yellow
    }
    exit 0
}

Write-Host "$requiredFailures required item(s) need attention before the first Vulkan exercise." -ForegroundColor Red
exit 1

