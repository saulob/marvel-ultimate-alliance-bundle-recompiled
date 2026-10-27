<#
.SYNOPSIS
  Downloads the parts of the pinned NVIDIA DLSS SDK that build mua_dlss_bridge.dll into tools\nvidia-dlss.

.DESCRIPTION
  Only NVIDIA's official repository at the pinned commit is used, and every file must match its pinned SHA-256: the
  NGX headers the bridge includes, the static NGX libraries it links, NVIDIA's license and the release (not the
  watermarked development) nvngx_dlss.dll. The SDK is licensed by NVIDIA under the NVIDIA RTX SDKs License in
  LICENSE.txt, not under this project's license, and is never committed. Returns $true when the files are ready;
  a failed download only warns, since the programs build and run without DLSS.

.EXAMPLE
  .\tools\setup-dlss-sdk.ps1
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$version = '310.9.16'
$commit = '9981c6350dc58c0ce21f024fd65e31e851c31689'
$files = [ordered]@{
    'LICENSE.txt' = 'D4216E39EBEF5F9B50A6712EBB37BEEB5379862A67733A9999C651F21592AAF0'
    'include/nvsdk_ngx.h' = 'DC38E7467CF415379C9D12AE1B6E4A494C453ED92720FB53E92AECB523E7B848'
    'include/nvsdk_ngx_defs.h' = 'EA23F33497CD274860D1C25A97644FCE807DCB0037C594547203343103FAD03E'
    'include/nvsdk_ngx_params.h' = '943BC8CC5CDAE03B6303016FBAD3183636F2335AE27A2D18776798C3B4EFABBC'
    'include/nvsdk_ngx_helpers.h' = '5BCBADFE7478B802CF6D3ACA4DC5DDD7D0889B99726E69C63F9E9BD555F44471'
    'include/nvsdk_ngx_helpers_d3d.h' = 'EC75224F36ED6580BAAF250FA83620405AD81A1D98C2C436654A0C9CF6A6B8BA'
    'include/nvsdk_ngx_helpers_cuda.h' = 'F2851A76107BDF4FBCB7B261F3C35573C312D8ECB4BC18FB02D9E2CEC7705042'
    'lib/Windows_x86_64/x64/nvsdk_ngx_s.lib' = '7B9A5239C1145AF5F645AB472AA066F9EA79DB36AC819562312AAE69D65F3765'
    'lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib' = 'D4BEB60CFD83468EA1F58AC885C3E826011A1199E358BA159E7FA0604ACE290E'
    'lib/Windows_x86_64/rel/nvngx_dlss.dll' = 'D00B5B0AB29C3CA101B70C3A5ECB3A01B7EF33766A3383A28A18623083E6123E'
}

$sdkDir = Join-Path $root "tools\nvidia-dlss\$version"

function Test-SdkFile([string]$relative) {
    $path = Join-Path $sdkDir $relative
    (Test-Path -LiteralPath $path -PathType Leaf) -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $files[$relative]
}

$missing = @($files.Keys | Where-Object { -not (Test-SdkFile $_) })
if (-not $missing.Count) {
    Write-Host "NVIDIA DLSS SDK $version is ready."
    return $true
}

Write-Host "Downloading NVIDIA DLSS SDK $version ($($missing.Count) files; NVIDIA RTX SDKs License, see $sdkDir\LICENSE.txt)..."
try {
    foreach ($relative in $missing) {
        $path = Join-Path $sdkDir $relative
        New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
        $partial = "$path.part"
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/NVIDIA/DLSS/$commit/$relative" -OutFile $partial -UseBasicParsing
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $files[$relative]) {
            Remove-Item -LiteralPath $partial
            throw "$relative is not the file of NVIDIA DLSS SDK $version"
        }
        Move-Item -LiteralPath $partial $path -Force
    }
} catch {
    Write-Warning "NVIDIA DLSS SDK ${version}: $($_.Exception.Message); building without mua_dlss_bridge.dll"
    return $false
}
Write-Host "NVIDIA DLSS SDK $version installed in $sdkDir"
return $true
