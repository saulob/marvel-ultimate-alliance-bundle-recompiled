<#
.SYNOPSIS
  Checks game\extracted\default.xex against the supported Gold Edition build and generates the recompiled C++
  sources into generated\default.
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$xexSha256 = '06D1D7811E1668A5C16AFEF5102CA59B00684DC1EA6184849418F59F022B86B7'

$rexglue = Join-Path $root 'tools\rexglue\win-amd64\bin\rexglue.exe'
$xex = Join-Path $root 'game\extracted\default.xex'
$log = Join-Path $root 'generated\codegen.log'
$manifest = 'marvel_ultimate_alliance_manifest.toml'

if (-not (Test-Path $rexglue)) { throw 'Missing tools\rexglue (run tools\setup-sdk.ps1 first)' }
if (-not (Test-Path $xex)) { throw "Missing $xex (extract the disc into game\extracted first)" }
$hash = (Get-FileHash $xex -Algorithm SHA256).Hash
if ($hash -ne $xexSha256) { throw "Unsupported default.xex (SHA-256 $hash). Expected $xexSha256." }

New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
Push-Location $root
try {
    # rexglue logs to stderr; judge by exit code.
    $ErrorActionPreference = 'Continue'
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & $rexglue codegen --ignore-stamp $manifest 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log
    $exitCode = $LASTEXITCODE
    Write-Host ("codegen exit code {0} after {1:N1} s (log: {2})" -f $exitCode, $timer.Elapsed.TotalSeconds, $log)
    if ($exitCode -ne 0) { throw "codegen failed ($exitCode)" }
} finally { Pop-Location; $ErrorActionPreference = 'Stop' }
