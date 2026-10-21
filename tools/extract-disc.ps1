<#
.SYNOPSIS
  Extracts the Gold Edition disc image into game\extracted. The disc is read with build\extract_disc.exe into a
  staging folder next to the destination, checked, and only then swapped in; the image is never modified.

.EXAMPLE
  .\tools\extract-disc.ps1
  .\tools\extract-disc.ps1 -Iso D:\mua.iso
#>
param(
    [string]$Iso,
    [string]$Destination
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
if (-not $Iso) { $Iso = Join-Path $Root 'game\Marvel - Ultimate Alliance (USA) (En,It) (Gold Edition).iso' }
if (-not $Destination) { $Destination = Join-Path $Root 'game\extracted' }
$Staging = "$Destination.staging"
$Previous = "$Destination.previous"
$Tool = Join-Path $Root 'build\extract_disc.exe'
$IsoSize = 7835492352
$IsoSha256 = '00066C18F5B35A64CE39332AAEB0CC5921CE67ACBEF70609D133A6306BF46AE3'
$XexSha256 = '06D1D7811E1668A5C16AFEF5102CA59B00684DC1EA6184849418F59F022B86B7'
$RequiredFolders = @('movies', 'sounds', 'z')
$RequiredFiles = @('default.xex', 'build.ini', 'alchemy.ini', 'muadlc_content.zip', 'muadlc_titleupdate.zip')

if (-not (Test-Path $Iso)) { throw "Missing $Iso" }
if ((Get-Item $Iso).Length -ne $IsoSize) { throw "$Iso is not the supported Gold Edition image (size)" }
if ((Get-FileHash $Iso -Algorithm SHA256).Hash -ne $IsoSha256) { throw "$Iso is not the supported Gold Edition image (SHA-256)" }

if (-not (Test-Path $Tool)) { & (Join-Path $PSScriptRoot 'build.ps1') }
if (Test-Path $Staging) { Remove-Item $Staging -Recurse -Force }

& $Tool $Iso $Staging
if ($LASTEXITCODE) { Remove-Item $Staging -Recurse -Force -ErrorAction SilentlyContinue; throw "Extraction failed ($LASTEXITCODE)" }

try {
    foreach ($folder in $RequiredFolders) {
        if (-not (Test-Path (Join-Path $Staging $folder) -PathType Container)) { throw "Extraction is missing the $folder folder" }
    }
    foreach ($file in $RequiredFiles) {
        if (-not (Test-Path (Join-Path $Staging $file) -PathType Leaf)) { throw "Extraction is missing $file" }
    }
    if ((Get-FileHash (Join-Path $Staging 'default.xex') -Algorithm SHA256).Hash -ne $XexSha256) {
        throw 'Extracted default.xex is not the supported Gold Edition build'
    }
} catch {
    Remove-Item $Staging -Recurse -Force -ErrorAction SilentlyContinue
    throw
}

if (Test-Path $Previous) { Remove-Item $Previous -Recurse -Force }
if (Test-Path $Destination) { Move-Item $Destination $Previous }
Move-Item $Staging $Destination
if (Test-Path $Previous) { Remove-Item $Previous -Recurse -Force }
Write-Host "Extracted to $Destination"
