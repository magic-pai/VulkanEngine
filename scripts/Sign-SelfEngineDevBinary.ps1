param(
    [string]$TargetPath = "build\Debug\SelfEngineForward3D.exe",
    [string]$CertificateSubject = "CN=SelfEngine Local Dev Code Signing",
    [int]$CertificateYears = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$resolvedTarget = Resolve-Path $TargetPath

function Find-SignTool {
    $pathCommand = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($null -ne $pathCommand) {
        return $pathCommand.Source
    }

    $candidateRoots = @(
        "C:\Program Files (x86)\Windows Kits\10\bin",
        "C:\Program Files (x86)\Windows Kits\10\App Certification Kit"
    )
    foreach ($root in $candidateRoots) {
        if (-not (Test-Path -LiteralPath $root)) {
            continue
        }
        $candidate = Get-ChildItem -LiteralPath $root -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\x64\\signtool\.exe$" -or $_.FullName -match "App Certification Kit\\signtool\.exe$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($null -ne $candidate) {
            return $candidate.FullName
        }
    }

    throw "signtool.exe was not found. Install the Windows SDK or add signtool.exe to PATH."
}

$certificate = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
    Where-Object { $_.Subject -eq $CertificateSubject } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if ($null -eq $certificate) {
    $certificate = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $CertificateSubject `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyAlgorithm RSA `
        -KeyLength 3072 `
        -HashAlgorithm SHA256 `
        -NotAfter (Get-Date).AddYears($CertificateYears)
}

$signTool = Find-SignTool
& $signTool sign /fd SHA256 /sha1 $certificate.Thumbprint $resolvedTarget.Path
if ($LASTEXITCODE -ne 0) {
    throw "signtool failed with exit code $LASTEXITCODE"
}

$signature = Get-AuthenticodeSignature -LiteralPath $resolvedTarget.Path
[ordered]@{
    target = $resolvedTarget.Path
    subject = $certificate.Subject
    thumbprint = $certificate.Thumbprint
    notAfter = $certificate.NotAfter
    signatureStatus = $signature.Status.ToString()
    signatureStatusMessage = $signature.StatusMessage
} | ConvertTo-Json -Depth 4
