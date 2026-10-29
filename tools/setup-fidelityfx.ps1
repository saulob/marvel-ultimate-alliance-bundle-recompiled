<#
.SYNOPSIS
  Downloads AMD's signed amd_fidelityfx_dx12.dll (FSR 3.1) of the pinned FidelityFX SDK release into tools\amd-fidelityfx.

.DESCRIPTION
  Only AMD's official repository at the pinned commit (FidelityFX SDK v1.1.4) is used, and every file must match its
  pinned SHA-256: the prebuilt, signed DLL that runs FSR 3.1 on any Direct3D 12 graphics card, and the SDK's MIT
  license. The GPU plugin's patch (patches/rexglue-sdk/0165) carries the SDK's ffx_api headers. The DLL is shipped
  beside the programs, unmodified. Returns $true when the files are ready; a failed download only warns, since the
  programs build and run without FSR 3.1.

.EXAMPLE
  .\tools\setup-fidelityfx.ps1
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$version = '1.1.4'
$commit = 'c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'
$files = [ordered]@{
    'LICENSE.txt' = '82CF74FC23885107C7F69249AC3F1DAC55898FF68320EB2C753984CAE6B2D78A'
    'PrebuiltSignedDLL/amd_fidelityfx_dx12.dll' = '12A5081257EC95B0B53AD51B4A87FB3C03F97FE0BBB59F9496968F8D50EF93A6'
}

$sdkDir = Join-Path $root "tools\amd-fidelityfx\$version"

function Test-SdkFile([string]$relative) {
    $path = Join-Path $sdkDir $relative
    (Test-Path -LiteralPath $path -PathType Leaf) -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $files[$relative]
}

$missing = @($files.Keys | Where-Object { -not (Test-SdkFile $_) })
if (-not $missing.Count) {
    Write-Host "AMD FidelityFX SDK $version (FSR 3.1) is ready."
    return $true
}

Write-Host "Downloading AMD FidelityFX SDK $version ($($missing.Count) files; MIT license, see $sdkDir\LICENSE.txt)..."
try {
    foreach ($relative in $missing) {
        $path = Join-Path $sdkDir $relative
        New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
        $partial = "$path.part"
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/$commit/$relative" -OutFile $partial -UseBasicParsing
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $files[$relative]) {
            Remove-Item -LiteralPath $partial
            throw "$relative is not the file of AMD FidelityFX SDK $version"
        }
        Move-Item -LiteralPath $partial $path -Force
    }
} catch {
    Write-Warning "AMD FidelityFX SDK ${version}: $($_.Exception.Message); building without AMD FSR 3.1"
    return $false
}
Write-Host "AMD FidelityFX SDK $version installed in $sdkDir"
return $true
