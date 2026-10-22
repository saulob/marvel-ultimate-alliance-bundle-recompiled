<#
.SYNOPSIS
  Checks a game's default.xex against the supported build and generates its recompiled C++ sources into
  generated\<game>. Without -Game it generates every game.
#>
param(
    [ValidateSet('all', 'mua1', 'mua1-gold', 'mua2')]
    [string]$Game = 'all'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$games = @(
    @{ Name = 'mua1-gold'; Xex = 'game\extracted\default.xex'
       Sha256 = '06D1D7811E1668A5C16AFEF5102CA59B00684DC1EA6184849418F59F022B86B7' },
    @{ Name = 'mua1'; Xex = 'game\mua1\default.xex'
       Sha256 = '44E5EEC135073224F2ABE09DEAEF765A6D130C81AA575D5C376338F470BEF92A' },
    @{ Name = 'mua2'; Xex = 'game\mua2\default.xex'
       Sha256 = '2613DC17AF39CEFACB7226855F62281D83B6E991C9D467D9E8EBB0A681AB0440' }
)

$rexglue = Join-Path $root 'tools\rexglue\win-amd64\bin\rexglue.exe'
if (-not (Test-Path $rexglue)) { throw 'Missing tools\rexglue (run tools\setup-sdk.ps1 first)' }

function Invoke-Codegen($entry) {
    $xex = Join-Path $root $entry.Xex
    $log = Join-Path $root "generated\codegen-$($entry.Name).log"
    $manifest = "$($entry.Name).manifest.toml"
    if (-not (Test-Path $xex)) { throw "Missing $xex (install the $($entry.Name) disc image first)" }
    $hash = (Get-FileHash $xex -Algorithm SHA256).Hash
    if ($hash -ne $entry.Sha256) { throw "Unsupported default.xex (SHA-256 $hash). Expected $($entry.Sha256)." }

    New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
    Push-Location $root
    try {
        # rexglue logs to stderr; judge by exit code.
        $ErrorActionPreference = 'Continue'
        $timer = [Diagnostics.Stopwatch]::StartNew()
        & $rexglue codegen --ignore-stamp $manifest 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log
        $exitCode = $LASTEXITCODE
        Write-Host ("{0}: codegen exit code {1} after {2:N1} s (log: {3})" -f $entry.Name, $exitCode, $timer.Elapsed.TotalSeconds, $log)
        if ($exitCode -ne 0) { throw "codegen failed for $($entry.Name) ($exitCode)" }
    } finally { Pop-Location; $ErrorActionPreference = 'Stop' }
}

foreach ($entry in $games) {
    if ($Game -eq 'all' -or $Game -eq $entry.Name) { Invoke-Codegen $entry }
}
