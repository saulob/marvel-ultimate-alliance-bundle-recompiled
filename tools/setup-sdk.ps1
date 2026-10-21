<#
.SYNOPSIS
  Unpacks the pinned ReXGlue SDK into tools\rexglue.

.DESCRIPTION
  Uses tools\rexglue\rexglue-sdk-<version>-win-amd64.zip when it is already there, copies -SdkZip into place, or
  downloads the official release as a last resort. The zip must match the pinned SHA-256.

.EXAMPLE
  .\tools\setup-sdk.ps1
  .\tools\setup-sdk.ps1 -SdkZip D:\downloads\rexglue-sdk-0.10.0-win-amd64.zip
#>
param(
    [string]$SdkZip
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$sdkVersion = '0.10.0'
$sdkZipSha256 = 'AD4485DA853FBE932E9F5DBB643F9B5FE85DC73775EBBAD4DF6879FE6DF0C11B'

$sdkDir = Join-Path $root 'tools\rexglue'
$rexglue = Join-Path $sdkDir 'win-amd64\bin\rexglue.exe'
$zip = Join-Path $sdkDir "rexglue-sdk-$sdkVersion-win-amd64.zip"

if (Test-Path $rexglue) {
    $installed = "$(& $rexglue --version)".Trim()
    if ($installed -eq $sdkVersion) { Write-Host "ReXGlue SDK $sdkVersion is ready."; return }
    throw "tools\rexglue has ReXGlue $installed; expected $sdkVersion (delete tools\rexglue to set it up again)"
}

New-Item -ItemType Directory -Force $sdkDir | Out-Null
if ($SdkZip) {
    Copy-Item -LiteralPath $SdkZip -Destination $zip -Force
} elseif (-not (Test-Path $zip)) {
    $url = "https://github.com/rexglue/rexglue-sdk/releases/download/v$sdkVersion/rexglue-sdk-$sdkVersion-win-amd64.zip"
    Write-Host "Downloading ReXGlue SDK $sdkVersion..."
    Invoke-WebRequest -Uri $url -OutFile $zip
}
if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $sdkZipSha256) { throw "$zip is not the official ReXGlue $sdkVersion release" }
Expand-Archive -Path $zip -DestinationPath $sdkDir -Force
Write-Host "ReXGlue SDK $("$(& $rexglue --version)".Trim()) installed in $sdkDir"
