param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectRoot,

    [string]$OutputPath = "",

    [string]$ExportedRootPath = "",

    [int]$MaxReferenceScreenshots = 8
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

function ConvertTo-RelativePath {
    param(
        [string]$BasePath,
        [string]$Path
    )

    $baseUri = [System.Uri]::new((Resolve-FullPath $BasePath).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar)
    $pathUri = [System.Uri]::new((Resolve-FullPath $Path))
    $relativeUri = $baseUri.MakeRelativeUri($pathUri)
    return [System.Uri]::UnescapeDataString($relativeUri.ToString()).Replace('/', [System.IO.Path]::DirectorySeparatorChar)
}

function ConvertTo-UePath {
    param(
        [string]$ContentPath,
        [System.IO.FileInfo]$File
    )

    $relative = ConvertTo-RelativePath -BasePath $ContentPath -Path $File.FullName
    $extension = [System.IO.Path]::GetExtension($relative)
    $withoutExtension = if ([string]::IsNullOrEmpty($extension)) {
        $relative
    } else {
        $relative.Substring(0, $relative.Length - $extension.Length)
    }
    return "/Game/" + $withoutExtension.Replace('\', '/')
}

function Get-UProjectInfo {
    param([System.IO.FileInfo]$UProject)

    $info = [ordered]@{
        fileVersion = $null
        engineAssociation = ""
        category = ""
        description = ""
        plugins = @()
    }

    if ($null -eq $UProject) {
        return $info
    }

    try {
        $json = Get-Content -Raw -LiteralPath $UProject.FullName | ConvertFrom-Json
        if ($null -ne $json.FileVersion) {
            $info.fileVersion = [int]$json.FileVersion
        }
        if ($null -ne $json.EngineAssociation) {
            $info.engineAssociation = [string]$json.EngineAssociation
        }
        if ($null -ne $json.Category) {
            $info.category = [string]$json.Category
        }
        if ($null -ne $json.Description) {
            $info.description = [string]$json.Description
        }
        if ($null -ne $json.Plugins) {
            $info.plugins = @($json.Plugins | ForEach-Object {
                [ordered]@{
                    name = [string]$_.Name
                    enabled = [bool]$_.Enabled
                }
            })
        }
    } catch {
        $info.description = "Failed to parse .uproject: $($_.Exception.Message)"
    }

    return $info
}

function Get-AssetKind {
    param([System.IO.FileInfo]$File)

    $name = [System.IO.Path]::GetFileNameWithoutExtension($File.Name)
    $lowerName = $name.ToLowerInvariant()
    $lowerPath = $File.FullName.ToLowerInvariant()

    if ($lowerName.Contains("layerinfo")) {
        return "LandscapeLayerInfo"
    }
    if ($lowerName.StartsWith("bp_") -or $lowerPath.Contains("\blueprint")) {
        return "Blueprint"
    }
    if ($lowerPath.Contains("\material") -or
        $lowerPath.Contains("\mat\") -or
        $lowerPath.Contains("\04_mat\") -or
        $lowerName.StartsWith("m_") -or
        $lowerName.StartsWith("mi_") -or
        $lowerName.StartsWith("mm_") -or
        $lowerName.StartsWith("mf_") -or
        $lowerName.StartsWith("mim_") -or
        $lowerName.EndsWith("_inst")) {
        return "Material"
    }
    if ($lowerPath.Contains("\texture") -or
        $lowerPath.Contains("\textures") -or
        $lowerPath.Contains("\tex\") -or
        $lowerPath.Contains("\03_tex\") -or
        $lowerName.StartsWith("t_") -or
        $lowerName.StartsWith("tex_") -or
        $lowerName.StartsWith("txt_") -or
        $lowerName.Contains("basecolor") -or
        $lowerName.Contains("base_color") -or
        $lowerName.Contains("diffuse") -or
        $lowerName.Contains("normal") -or
        $lowerName.Contains("roughness") -or
        $lowerName.Contains("metallic") -or
        $lowerName.Contains("occlusion") -or
        $lowerName.Contains("opacity") -or
        $lowerName.Contains("mask") -or
        $lowerName.Contains("_ao") -or
        $lowerName.Contains("_orm")) {
        return "Texture"
    }
    if ($lowerPath.Contains("\mesh") -or
        $lowerPath.Contains("\meshes") -or
        $lowerPath.Contains("\geometries") -or
        $lowerName.StartsWith("sm_") -or
        $lowerName.StartsWith("sk_") -or
        $lowerName.StartsWith("qs")) {
        return "Mesh"
    }

    return "Other"
}

function New-AssetRecord {
    param(
        [string]$ContentPath,
        [System.IO.FileInfo]$File
    )

    $kind = Get-AssetKind -File $File
    $sourceAssetId = ConvertTo-UePath -ContentPath $ContentPath -File $File
    return [ordered]@{
        id = $sourceAssetId
        sourceAssetId = $sourceAssetId
        name = [System.IO.Path]::GetFileNameWithoutExtension($File.Name)
        kind = $kind
        sourcePath = $File.FullName
        relativePath = ConvertTo-RelativePath -BasePath $ContentPath -Path $File.FullName
        sizeBytes = $File.Length
        exportedPath = ""
        exportStatus = "requires_ue_editor_export"
    }
}

function New-ReferenceCaptureRecords {
    param(
        [string]$ProjectRoot,
        [int]$MaxCount
    )

    $patterns = @(
        [System.IO.Path]::Combine($ProjectRoot, "Saved", "Screenshots"),
        [System.IO.Path]::Combine($ProjectRoot, "Saved")
    )

    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $captures = @()
    $imageExtensions = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]@(".png", ".jpg", ".jpeg", ".bmp", ".exr", ".hdr"),
        [System.StringComparer]::OrdinalIgnoreCase
    )

    foreach ($path in $patterns) {
        if (-not (Test-Path -LiteralPath $path)) {
            continue
        }

        Get-ChildItem -LiteralPath $path -Recurse -File |
            Where-Object { $imageExtensions.Contains($_.Extension) } |
            Sort-Object LastWriteTime -Descending |
            ForEach-Object {
                if ($captures.Count -lt $MaxCount -and $seen.Add($_.FullName)) {
                    $captures += [ordered]@{
                        id = "reference_capture_" + $captures.Count
                        kind = "UEViewportScreenshot"
                        sourceImagePath = $_.FullName
                        cameraName = ""
                        notes = "Camera/exposure metadata still requires UE Editor automation."
                    }
                }
            }
    }

    return $captures
}

function New-SceneRecord {
    param(
        [string]$ProjectRoot,
        [string]$ContentPath,
        [string]$ExportedRootPath,
        [System.IO.FileInfo]$MapFile,
        [array]$ReferenceCaptures
    )

    $sourceId = ConvertTo-UePath -ContentPath $ContentPath -File $MapFile
    $plannedExport = ""
    if (-not [string]::IsNullOrWhiteSpace($ExportedRootPath)) {
        $plannedExport = [System.IO.Path]::Combine(
            $ExportedRootPath,
            ([System.IO.Path]::GetFileNameWithoutExtension($MapFile.Name) + ".glb")
        )
    }

    $emptyArray = [System.Collections.ArrayList]::new()
    $sceneReferenceCaptures = [System.Collections.ArrayList]::new()
    if ($null -ne $ReferenceCaptures) {
        foreach ($capture in $ReferenceCaptures) {
            if ($null -ne $capture) {
                [void]$sceneReferenceCaptures.Add($capture)
            }
        }
    }

    return [ordered]@{
        id = $sourceId
        sourceAssetId = $sourceId
        name = [System.IO.Path]::GetFileNameWithoutExtension($MapFile.Name)
        sourceMapPath = $MapFile.FullName
        exportedScenePath = ""
        plannedExportedScenePath = $plannedExport
        exportStatus = "requires_ue_editor_export"
        actors = $emptyArray
        meshInstances = $emptyArray.Clone()
        lights = $emptyArray.Clone()
        cameras = $emptyArray.Clone()
        referenceCaptures = $sceneReferenceCaptures
        unsupportedFeatures = @(
            [ordered]@{
                feature = "Raw .umap actor/component graph"
                fallback = "UE Editor automation must export scene hierarchy into this manifest."
            },
            [ordered]@{
                feature = "Raw .uasset material and mesh dependency graph"
                fallback = "UE asset registry/export automation must fill exported paths and material metadata."
            }
        )
    }
}

$projectRootFull = Resolve-FullPath $ProjectRoot
if (-not (Test-Path -LiteralPath $projectRootFull -PathType Container)) {
    throw "ProjectRoot is not a directory: $projectRootFull"
}

$uproject = Get-ChildItem -LiteralPath $projectRootFull -Filter *.uproject -File | Select-Object -First 1
if ($null -eq $uproject) {
    throw "No .uproject file found in: $projectRootFull"
}

$contentPath = [System.IO.Path]::Combine($projectRootFull, "Content")
if (-not (Test-Path -LiteralPath $contentPath -PathType Container)) {
    throw "No Content directory found in: $projectRootFull"
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = [System.IO.Path]::Combine($projectRootFull, "SelfEngineBridge.json")
}
$outputPathFull = Resolve-FullPath $OutputPath

if ([string]::IsNullOrWhiteSpace($ExportedRootPath)) {
    $ExportedRootPath = [System.IO.Path]::Combine($projectRootFull, "Saved", "SelfEngineBridge", "Exported")
}
$exportedRootFull = Resolve-FullPath $ExportedRootPath

$uassetFiles = @(Get-ChildItem -LiteralPath $contentPath -Recurse -File -Filter *.uasset | Sort-Object FullName)
$mapFiles = @(Get-ChildItem -LiteralPath $contentPath -Recurse -File -Filter *.umap | Sort-Object FullName)
$assetRecords = @($uassetFiles | ForEach-Object { New-AssetRecord -ContentPath $contentPath -File $_ })

$meshAssets = @($assetRecords | Where-Object { $_.kind -eq "Mesh" })
$materialAssets = @($assetRecords | Where-Object { $_.kind -eq "Material" })
$textureAssets = @($assetRecords | Where-Object { $_.kind -eq "Texture" })
$otherAssets = @($assetRecords | Where-Object {
    $_.kind -ne "Mesh" -and $_.kind -ne "Material" -and $_.kind -ne "Texture"
})

$referenceCaptures = @(New-ReferenceCaptureRecords -ProjectRoot $projectRootFull -MaxCount $MaxReferenceScreenshots)
$sceneRecords = @()
for ($sceneIndex = 0; $sceneIndex -lt $mapFiles.Count; ++$sceneIndex) {
    $capturesForScene = if ($sceneIndex -eq 0) { $referenceCaptures } else { @() }
    $sceneRecords += New-SceneRecord `
        -ProjectRoot $projectRootFull `
        -ContentPath $contentPath `
        -ExportedRootPath $exportedRootFull `
        -MapFile $mapFiles[$sceneIndex] `
        -ReferenceCaptures $capturesForScene
}

$projectInfo = Get-UProjectInfo -UProject $uproject
$manifest = [ordered]@{
    schemaVersion = "selfengine.bridge.v0"
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    generator = "scripts/Generate-SelfEngineBridgeManifest.ps1"
    sourceProjectPath = $uproject.FullName
    projectRootPath = $projectRootFull
    contentPath = $contentPath
    exportedRootPath = $exportedRootFull
    project = [ordered]@{
        displayName = [System.IO.Path]::GetFileNameWithoutExtension($uproject.Name)
        uprojectPath = $uproject.FullName
        uproject = $projectInfo
    }
    scenes = $sceneRecords
    assets = [ordered]@{
        meshes = $meshAssets
        materials = $materialAssets
        textures = $textureAssets
        otherAssets = $otherAssets
    }
    referenceCaptures = $referenceCaptures
    diagnostics = [ordered]@{
        mapCount = $mapFiles.Count
        uassetCount = $uassetFiles.Count
        meshAssetCount = $meshAssets.Count
        materialAssetCount = $materialAssets.Count
        textureAssetCount = $textureAssets.Count
        otherAssetCount = $otherAssets.Count
        generatedFromRawUAssets = $true
        directUAssetParsing = $false
        needsUEEditorExportAutomation = $true
        notes = @(
            "This manifest preserves source asset identity and file inventory only.",
            "Actor transforms, light/camera/post-process state, material node graphs, and exported mesh paths must be filled by UE Editor automation."
        )
    }
}

$json = $manifest | ConvertTo-Json -Depth 20
$outputDirectory = [System.IO.Path]::GetDirectoryName($outputPathFull)
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}
[System.IO.File]::WriteAllText($outputPathFull, $json, [System.Text.UTF8Encoding]::new($false))

Write-Host "Wrote SelfEngine bridge manifest: $outputPathFull"
Write-Host "Project: $($uproject.FullName)"
Write-Host "Maps: $($mapFiles.Count)"
Write-Host "Assets: meshes=$($meshAssets.Count) materials=$($materialAssets.Count) textures=$($textureAssets.Count) other=$($otherAssets.Count)"
Write-Host "Reference captures: $($referenceCaptures.Count)"
