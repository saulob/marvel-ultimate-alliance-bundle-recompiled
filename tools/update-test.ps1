<#
.SYNOPSIS
  Local, offline test of the launcher's self-update. Nothing is published, uploaded or downloaded from GitHub.

.DESCRIPTION
  Builds a fake newer release (a ZIP of the files in a build folder, a release.json that names it and its SHA-256)
  and a disposable copy of the build to update, both under -Work (default: %TEMP%\mua-update-test). The launcher
  reads the fake release instead of GitHub when MUA_UPDATE_TEST=1 and MUA_UPDATE_TEST_RELEASE_JSON are set, and in
  that mode only accepts the package from 127.0.0.1. The SHA-256 check is the normal one.

  The disposable install differs from the fake release in every bundle file (a marker is appended to its copy), holds
  sentinel files under user_data, game and logs, and is the only folder an update can change.

  1. First test, stops before anything is replaced: -Prepare, -Serve in a second terminal, -LaunchBuild. The banner
     offers the update; UPDATE NOW downloads and verifies it; do not press RESTART TO UPDATE.
  2. Second test, on the disposable copy: -Prepare, -Serve, -Launch, UPDATE NOW, RESTART TO UPDATE, then -Verify.
  3. Rollback: as the second test, but run -HoldFile (third terminal) after the banner says "Update ready" and before
     RESTART TO UPDATE. The install fails on the locked file, rolls back and restarts the previous launcher; then
     -Verify -ExpectRollback.

.EXAMPLE
  .\tools\update-test.ps1 -Prepare
  .\tools\update-test.ps1 -Serve
  .\tools\update-test.ps1 -Launch
  .\tools\update-test.ps1 -Verify
#>
param(
    [switch]$Prepare,
    [switch]$Serve,
    [switch]$Launch,        # start the disposable install's launcher with the test variables
    [switch]$LaunchBuild,   # start the build folder's launcher with the test variables (first test only)
    [switch]$HoldFile,      # lock a game program of the disposable install so the update fails and rolls back
    [switch]$Verify,
    [switch]$ExpectRollback,
    [string]$Source,        # the build folder to take the files from; default: build
    [string]$Work,          # default: %TEMP%\mua-update-test
    [int]$Port = 8765,
    [string]$Version = '9.9.9'
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
if (-not $Source) { $Source = Join-Path $Root 'build' }
if (-not $Work) { $Work = Join-Path $env:TEMP 'mua-update-test' }
$Source = [IO.Path]::GetFullPath($Source)
$Work = [IO.Path]::GetFullPath($Work)
$Site = Join-Path $Work 'site'
$Install = Join-Path $Work 'install'
$Baseline = Join-Path $Work 'baseline.json'
$ReleaseJson = Join-Path $Site 'release.json'
$PackageName = "MarvelUltimateAlliance-v$Version-windows-x64.zip"
$Marker = [Text.Encoding]::ASCII.GetBytes("`n# old-install-marker`n")

$Programs = @('marvel_ultimate_alliance_bundle.exe', 'marvel_ultimate_alliance_mua1.exe', 'marvel_ultimate_alliance_mua1_gold.exe',
    'marvel_ultimate_alliance_mua2.exe', 'trg-updater.exe')
$HeldFile = 'marvel_ultimate_alliance_mua2.exe'

function Get-RuntimeSuffix {
    foreach ($suffix in @('rd', '', 'd')) {
        if ((Test-Path (Join-Path $Source "rexruntime$suffix.dll")) -and (Test-Path (Join-Path $Source "rexgpu-xenos$suffix.dll"))) { return $suffix }
    }
    throw "No rexruntime*.dll and rexgpu-xenos*.dll pair in $Source"
}

function Get-BundleMap {
    $suffix = Get-RuntimeSuffix
    $map = [ordered]@{}
    foreach ($program in $Programs) { $map[$program] = Join-Path $Source $program }
    $map['rexruntime.dll'] = Join-Path $Source "rexruntime$suffix.dll"
    $map['rexgpu-xenos.dll'] = Join-Path $Source "rexgpu-xenos$suffix.dll"
    $map['marvel_ultimate_alliance.toml'] = Join-Path $Source 'marvel_ultimate_alliance.toml'
    foreach ($art in 'header-art-mua1.png', 'header-art-mua1-gold.png', 'header-art-mua2.png') {
        $map["assets/launcher/$art"] = Join-Path $Root "assets\launcher\$art"
    }
    foreach ($license in 'ReXGlue-LICENSE.txt', 'THIRD-PARTY-LICENSES.txt') {
        $map["licenses/$license"] = Join-Path $Root "packaging\licenses\$license"
    }
    foreach ($entry in $map.GetEnumerator()) {
        if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) { throw "Missing $($entry.Value) (build first, or pass -Source)" }
    }
    return $map
}

function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

function Get-TreeHashes([string]$Dir) {
    $hashes = [ordered]@{}
    foreach ($file in Get-ChildItem -LiteralPath $Dir -Recurse -File -Force | Sort-Object FullName) {
        $hashes[$file.FullName.Substring($Dir.Length + 1).Replace('\', '/')] = Get-Sha256 $file.FullName
    }
    return $hashes
}

function Set-TestEnvironment {
    $env:MUA_UPDATE_TEST = '1'
    $env:MUA_UPDATE_TEST_RELEASE_JSON = $ReleaseJson
}

function Invoke-Prepare {
    if ($Work.StartsWith($Source, [StringComparison]::OrdinalIgnoreCase) -or $Source.StartsWith($Work, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Work and Source must be separate folders'
    }
    $map = Get-BundleMap
    if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
    New-Item -ItemType Directory $Site, $Install | Out-Null

    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zipPath = Join-Path $Site $PackageName
    $stream = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew)
    try {
        $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($entry in $map.GetEnumerator()) {
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $entry.Value, $entry.Key, [IO.Compression.CompressionLevel]::Optimal)
            }
        } finally { $archive.Dispose() }
    } finally { $stream.Dispose() }
    $zipHash = Get-Sha256 $zipPath
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [IO.File]::WriteAllText((Join-Path $Site "$($PackageName -replace '\.zip$', '.sha256')"), "$zipHash  $PackageName`n", $utf8)

    $base = "http://127.0.0.1:$Port"
    $release = [ordered]@{
        tag_name = "v$Version"; draft = $false; prerelease = $false
        html_url = "https://github.com/saulob/marvel-ultimate-alliance-recompiled/releases/tag/v$Version"
        assets = @([ordered]@{ name = $PackageName; browser_download_url = "$base/$PackageName"
                               size = (Get-Item -LiteralPath $zipPath).Length; digest = "sha256:$zipHash" })
    }
    [IO.File]::WriteAllText($ReleaseJson, ($release | ConvertTo-Json -Depth 5), $utf8)

    # The disposable install: the build's files, each carrying a marker so none equals its fake-release version.
    foreach ($entry in $map.GetEnumerator()) {
        $target = Join-Path $Install $entry.Key
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath $entry.Value $target
        $file = [IO.File]::Open($target, [IO.FileMode]::Append)
        try { $file.Write($Marker, 0, $Marker.Length) } finally { $file.Dispose() }
    }
    foreach ($dll in Get-ChildItem -LiteralPath $Source -File -Filter '*.dll') {
        if (-not (Test-Path -LiteralPath (Join-Path $Install $dll.Name))) { Copy-Item -LiteralPath $dll.FullName $Install }
    }
    $sentinels = @{ 'user_data\sentinel.txt' = 'player data'; 'user_data\mua2\saves\sentinel.sav' = 'a save'
                    'game\sentinel.txt' = 'user-owned game data'; 'logs\sentinel.log' = 'a log' }
    foreach ($sentinel in $sentinels.GetEnumerator()) {
        $path = Join-Path $Install $sentinel.Key
        New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
        [IO.File]::WriteAllText($path, $sentinel.Value, $utf8)
    }
    $state = [ordered]@{ install = Get-TreeHashes $Install; release = Get-TreeHashes $Site; entries = @($map.Keys) }
    [IO.File]::WriteAllText($Baseline, ($state | ConvertTo-Json -Depth 5), $utf8)

    Write-Host ''
    Write-Host "Fake release v$Version prepared under $Work"
    Write-Host "  package:  $zipPath"
    Write-Host "  SHA-256:  $zipHash"
    Write-Host "  install:  $Install  (disposable copy; the build folder is not modified)"
    Write-Host ''
    Write-Host 'Next, in this order:'
    Write-Host '  .\tools\update-test.ps1 -Serve        (second terminal; leave it running)'
    Write-Host '  .\tools\update-test.ps1 -Launch       (disposable install)   or   -LaunchBuild (first test, never press RESTART)'
}

function Invoke-Serve {
    if (-not (Test-Path -LiteralPath $ReleaseJson)) { throw 'Run -Prepare first' }
    $listener = New-Object System.Net.HttpListener
    $listener.Prefixes.Add("http://127.0.0.1:$Port/")
    $listener.Start()
    Write-Host "Serving $Site on http://127.0.0.1:$Port/ (Ctrl+C to stop)"
    try {
        while ($listener.IsListening) {
            $pending = $listener.BeginGetContext($null, $null)
            while (-not $pending.AsyncWaitHandle.WaitOne(500)) { }
            $context = $listener.EndGetContext($pending)
            $name = [Uri]::UnescapeDataString($context.Request.Url.AbsolutePath.TrimStart('/'))
            $file = Join-Path $Site $name
            if ($name -and $name -notmatch '[\\/]' -and (Test-Path -LiteralPath $file -PathType Leaf)) {
                $bytes = [IO.File]::ReadAllBytes($file)
                $context.Response.ContentLength64 = $bytes.Length
                $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
                Write-Host "  200 $name ($($bytes.Length) bytes)"
            } else {
                $context.Response.StatusCode = 404
                Write-Host "  404 $name"
            }
            $context.Response.Close()
        }
    } finally { $listener.Stop() }
}

function Invoke-LaunchProgram([string]$Folder) {
    if (-not (Test-Path -LiteralPath $ReleaseJson)) { throw 'Run -Prepare first' }
    $exe = Join-Path $Folder 'marvel_ultimate_alliance_bundle.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw "Missing $exe" }
    Set-TestEnvironment
    Write-Host "Starting $exe with MUA_UPDATE_TEST=1 and the fake release (the Serve terminal must be running)"
    Start-Process -FilePath $exe -WorkingDirectory $Folder
}

function Invoke-HoldFile {
    $path = Join-Path $Install $HeldFile
    if (-not (Test-Path -LiteralPath $path)) { throw 'Run -Prepare first' }
    $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        Write-Host "$HeldFile is locked. Press RESTART TO UPDATE in the launcher now, wait for the previous launcher"
        Write-Host 'to come back, then press Enter here.'
        [void][Console]::ReadLine()
    } finally { $stream.Dispose() }
}

function Invoke-Verify {
    if (-not (Test-Path -LiteralPath $Baseline)) { throw 'Run -Prepare first' }
    $state = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
    $before = @{}; $state.install.PSObject.Properties | ForEach-Object { $before[$_.Name] = $_.Value }
    $package = @{}
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead((Join-Path $Site $PackageName))
    try {
        foreach ($entry in $archive.Entries) {
            $stream = $entry.Open()
            try { $package[$entry.FullName] = ([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($stream)) -replace '-', '').ToLowerInvariant() }
            finally { $stream.Dispose() }
        }
    } finally { $archive.Dispose() }
    $after = Get-TreeHashes $Install
    $problems = New-Object System.Collections.Generic.List[string]

    foreach ($name in $before.Keys) {
        $isUserFile = $name -match '^(user_data|game|logs|cache)/'
        if ($isUserFile -and $after[$name] -ne $before[$name]) { $problems.Add("user or game file changed: $name") }
    }
    if ($ExpectRollback) {
        foreach ($name in $before.Keys) {
            if (-not $after.Contains($name)) { $problems.Add("missing after rollback: $name") }
            elseif ($after[$name] -ne $before[$name]) { $problems.Add("not restored: $name") }
        }
        foreach ($name in $after.Keys) {
            if (-not $before.ContainsKey($name) -and $name -notmatch '^(user_data|game|logs|cache)/') { $problems.Add("left behind by the failed update: $name") }
        }
    } else {
        foreach ($name in $state.entries) {
            if (-not $after.Contains($name)) { $problems.Add("missing: $name") }
            elseif ($after[$name] -ne $package[$name]) { $problems.Add("not updated: $name") }
        }
        foreach ($name in $before.Keys) {
            if ($package.ContainsKey($name) -or $name -match '^(user_data|game|logs|cache)/') { continue }
            if (-not $after.Contains($name) -or $after[$name] -ne $before[$name]) { $problems.Add("changed but not part of the release: $name") }
        }
    }
    foreach ($name in $after.Keys) {
        if ($name -match '(^|/)\.trg-update-') { $problems.Add("temporary file left behind: $name") }
    }
    foreach ($program in $Programs) {
        if (-not (Test-Path -LiteralPath (Join-Path $Install $program))) { $problems.Add("program missing: $program") }
    }

    $exe = Join-Path $Install 'marvel_ultimate_alliance_bundle.exe'
    $running = @(Get-Process -Name 'marvel_ultimate_alliance_bundle' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
    Write-Host ''
    Write-Host "Bundle launcher from the install folder running: $(if ($running.Count) { 'yes' } else { 'no' })"
    if ($problems.Count) {
        Write-Host 'UPDATE CHECK FAILED' -ForegroundColor Red
        foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
        exit 1
    }
    Write-Host $(if ($ExpectRollback) { 'Rollback verified: every file is as before the update, user and game files untouched.' }
                 else { 'Update verified: every bundle file matches the release, user and game files untouched.' }) -ForegroundColor Green
}

if ($Prepare) { Invoke-Prepare }
elseif ($Serve) { Invoke-Serve }
elseif ($Launch) { Invoke-LaunchProgram $Install }
elseif ($LaunchBuild) { Invoke-LaunchProgram $Source }
elseif ($HoldFile) { Invoke-HoldFile }
elseif ($Verify) { Invoke-Verify }
else { Get-Help $PSCommandPath -Detailed }
