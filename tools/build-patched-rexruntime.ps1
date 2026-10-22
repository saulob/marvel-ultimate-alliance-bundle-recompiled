<#
.SYNOPSIS
  Builds ReXGlue's rexruntime, Xenos GPU plugin and rexglue code generator from the pinned v0.10.0 source with this
  project's patches and installs them into tools\rexglue\win-amd64, replacing the stock RelWithDebInfo and Release
  files and rexglue.exe.

.DESCRIPTION
  The source is fetched from the official ReXGlue repository (network needed the first time) into -WorkDir,
  checked against the pinned commit in patches\rexglue-sdk\baseline.env, patched with the patches in
  patches\rexglue-sdk that apply to Windows (NNNN-name.patch and NNNN-name.windows.patch, in name order), and built.
  The installed files are recorded with the fingerprint of the baseline, build settings and patches in
  tools\rexglue\win-amd64\rexruntime-patch.txt, which build.ps1 and package-release.ps1 check.
  tools\build.ps1 runs this script by itself when that check fails, so it is only needed by hand for the options
  below. Run tools\codegen.ps1 first so the SDK is in tools\rexglue.
  See docs\rexglue-runtime-patch.md. Nothing is committed, tagged or published.

.EXAMPLE
  .\tools\build-patched-rexruntime.ps1                  # build and install the patched runtime and GPU plugin
  .\tools\build-patched-rexruntime.ps1 -Check -Config Release -Staged build-release   # verify only (the CMake build does)
  .\tools\build-patched-rexruntime.ps1 -StressTest      # ... then run the allocator stress test
  .\tools\build-patched-rexruntime.ps1 -StressTest -SkipBuild   # test whatever runtime is installed
  .\tools\build-patched-rexruntime.ps1 -Restore         # put the stock runtime and GPU plugin back (the next build.ps1 patches them again)
#>
param(
    [string]$WorkDir,     # ReXGlue source and build folder; default: out\rexglue-sdk-src (a short path is safer)
    [switch]$Check,       # only verify tools\rexglue, and with -Staged the DLLs of -Config in that build folder
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Config = 'Release',
    [string]$Staged,
    [switch]$Restore,     # reinstall the stock runtime and GPU plugin from the SDK download and exit
    [switch]$StressTest,  # run tests\physical_heap_stress.cpp against the installed runtime
    [switch]$SkipBuild,   # with -StressTest: do not build the runtime first
    [int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')
$Baseline = Read-RuntimeBaseline
$Sdk = Join-Path $Root 'tools\rexglue\win-amd64'
$SdkZip = Join-Path $Root "tools\rexglue\rexglue-sdk-$($Baseline.SDK_VERSION)-win-amd64.zip"
$Official = Get-OfficialRuntimeHashes $Baseline
$WorkDir = if (-not $WorkDir) { Join-Path $Root 'out\rexglue-sdk-src' } elseif ([IO.Path]::IsPathRooted($WorkDir)) { $WorkDir } else { Join-Path $Root $WorkDir }
$StampPath = Join-Path $Sdk $RuntimeStampName

if (-not (Test-Path (Join-Path $Sdk 'lib\cmake\rexglue'))) { throw 'Missing tools\rexglue (run tools\codegen.ps1 first)' }

if ($Check) {
    # The Debug files stay stock, so only their staged copies are checked.
    $problems = @()
    if ($Config -ne 'Debug') { $problems += @(Test-PatchedRuntime $Sdk) }
    if ($Staged) {
        $stagedDir = if ([IO.Path]::IsPathRooted($Staged)) { $Staged } else { Join-Path $Root $Staged }
        $problems += @(Test-StagedRuntime $Sdk $stagedDir $Config)
    }
    if (-not $problems.Count) { exit 0 }
    Write-Host "ReXGlue: tools\rexglue or the build folder does not match this branch ($Config):"
    foreach ($problem in $problems) { Write-Host "  $problem" }
    Write-Host 'Run .\tools\build.ps1, which rebuilds the runtime and GPU plugin and stages them.'
    exit 1
}

function Invoke-Native([string]$What, [scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE) { throw "$What failed ($LASTEXITCODE)" }
}

function Import-VsEnvironment {
    $vsRoot = Get-VisualStudioRoot
    $vsDevCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
    $llvmBin = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin'
    $cmakeBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
    $ninjaBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
    foreach ($tool in @($vsDevCmd, (Join-Path $llvmBin 'clang++.exe'), (Join-Path $cmakeBin 'cmake.exe'), (Join-Path $ninjaBin 'ninja.exe'))) {
        if (-not (Test-Path $tool)) { throw "Required tool not found: $tool" }
    }
    $env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch '\\msys64\\' }) -join ';'
    $env:PATH = "$env:PATH;$(Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer')"
    foreach ($line in (cmd.exe /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 -no_logo >nul && set")) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
    if (-not $env:VCToolsVersion) { throw 'VsDevCmd.bat did not set up the x64 environment' }
    $env:PATH = "$llvmBin;$cmakeBin;$ninjaBin;$env:PATH"
}

if ($Restore) {
    if (-not (Test-Path $SdkZip)) { throw "Missing $SdkZip (run tools\codegen.ps1)" }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($SdkZip)
    try {
        foreach ($leaf in $RuntimeFiles) {
            $entry = $archive.GetEntry("win-amd64/$leaf")
            if (-not $entry) { throw "$leaf is not in the SDK download" }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $Sdk $leaf), $true)
            if ((Get-FileSha256 (Join-Path $Sdk $leaf)) -ne $Official[$leaf]) { throw "$leaf does not match the stock ReXGlue release" }
        }
    } finally { $archive.Dispose() }
    Remove-Item -LiteralPath $StampPath -ErrorAction SilentlyContinue
    Write-Host 'Stock ReXGlue runtime and GPU plugin restored.'
    return
}

function Install-PatchedRuntime {
    $src = $WorkDir
    $srcCmake = $src.Replace('\', '/')
    if (-not (Test-Path (Join-Path $src '.git'))) {
        Write-Host "Fetching ReXGlue $($Baseline.SDK_TAG) from $($Baseline.SDK_REPO) (network) into $src..."
        New-Item -ItemType Directory -Force (Split-Path $src) | Out-Null
        Invoke-Native 'git clone' { git clone --branch $Baseline.SDK_TAG --depth 1 --recurse-submodules --shallow-submodules --jobs 8 $Baseline.SDK_REPO $src }
    }
    $inputsSha256 = Get-RuntimeInputsSha256
    $commit = "$(git -C $src rev-parse HEAD)".Trim()
    if ($commit -ne $Baseline.SDK_COMMIT) { throw "$src is at $commit; expected $($Baseline.SDK_TAG) $($Baseline.SDK_COMMIT) (delete the folder to fetch it again)" }

    # Windows checks out the libmspack symlinks as one-line text files; use copies of their targets.
    $mspack = Join-Path $src 'thirdparty\libmspack'
    foreach ($line in (git -C $mspack ls-files -s)) {
        if ($line -notmatch '^120000 \S+ \d\t(.+)$') { continue }
        $file = Join-Path $mspack $Matches[1]
        $item = Get-Item -LiteralPath $file
        if ($item.LinkType -or $item.Length -gt 260) { continue }
        $target = Join-Path (Split-Path $file) (Get-Content -LiteralPath $file -Raw).Trim()
        if (Test-Path -LiteralPath $target -PathType Leaf) { Copy-Item -LiteralPath $target $file -Force }
    }

    # Another branch may have left other patches in the tree, in any tracked file.
    Invoke-Native 'git checkout' { git -C $src checkout -- . }
    Invoke-Native 'git clean' { git -C $src clean -fdq -- src include tests cmake }
    $patches = Get-RuntimePatchFiles
    foreach ($patch in $patches) {
        & git -C $src apply --check $patch.FullName
        if ($LASTEXITCODE) { throw "patches\rexglue-sdk\$($patch.Name) no longer applies to ReXGlue $($Baseline.SDK_TAG)" }
        Invoke-Native "git apply $($patch.Name)" { git -C $src apply $patch.FullName }
    }

    Import-VsEnvironment
    # -ffile-prefix-map keeps the build folder out of the runtime's log strings.
    $flags = "$RuntimeBuildFlags -ffile-prefix-map=$srcCmake=."
    Push-Location $src
    try {
        Invoke-Native 'ReXGlue configure' { cmake --preset win-amd64 "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" }
        $parallel = @(if ($Jobs -gt 0) { '--parallel'; $Jobs } else { '--parallel' })
        foreach ($config in $RuntimeBuildConfigs) {
            $targets = if ($config -eq 'Release') { $RuntimeBuildTargets + $RuntimeReleaseOnlyTargets } else { $RuntimeBuildTargets }
            Invoke-Native "ReXGlue build ($config)" { cmake --build out/build/win-amd64 --config $config --target $targets @parallel }
        }
    } finally { Pop-Location }

    $built = @{}
    foreach ($leaf in $RuntimeFiles) {
        $config = if ($leaf -match 'rd\.') { 'RelWithDebInfo' } else { 'Release' }
        $path = Join-Path $src "out\win-amd64\$config\$(Split-Path $leaf -Leaf)"
        if (-not (Test-Path $path)) { throw "ReXGlue build output is missing $path" }
        if ((Get-FileSha256 $path) -eq $Official[$leaf]) { throw "$leaf came out identical to the stock release; the patch was not built in" }
        $built[$leaf] = $path
    }
    foreach ($dll in 'bin/rexruntime.dll', 'bin/rexgpu-xenos.dll') {
        $text = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($built[$dll]))
        foreach ($needle in @($src, $srcCmake)) {
            if ($text.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) { throw "$(Split-Path $dll -Leaf) embeds the build folder" }
        }
    }

    Remove-Item -LiteralPath $StampPath -ErrorAction SilentlyContinue
    foreach ($leaf in $RuntimeFiles) { Copy-Item -LiteralPath $built[$leaf] (Join-Path $Sdk $leaf) -Force }
    # tools\rexglue's headers come from the stock release zip and are never reinstalled otherwise; a patch that adds
    # or changes a public header (e.g. 0115's include/rex/audio/thread_priority.h) needs it mirrored here too.
    Copy-Item -LiteralPath (Join-Path $src 'include\rex') (Join-Path $Sdk 'include') -Recurse -Force
    $lines = Write-RuntimeStamp $Sdk $inputsSha256
    $problems = Test-PatchedRuntime $Sdk
    if ($problems.Count) { throw "The installed runtime and GPU plugin do not match the patches: $($problems -join '; ')" }
    Write-Host 'Patched ReXGlue runtime and GPU plugin installed:'
    $lines | ForEach-Object { Write-Host "  $_" }
}

function Invoke-StressTest {
    $buildDir = Join-Path $Root 'build-release'
    if (-not (Test-Path (Join-Path $buildDir 'build.ninja'))) { throw 'Run tools\build.ps1 -Config Release -BuildDir build-release first' }
    Import-VsEnvironment
    Invoke-Native 'stress test build' { cmake --build $buildDir --target mua_physical_heap_stress }
    # Run beside the installed runtime, not the copy the build left in $buildDir, which may be stale.
    $stage = Join-Path $buildDir 'physical_heap_stress'
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildDir 'mua_physical_heap_stress.exe') $stage -Force
    Copy-Item -LiteralPath (Join-Path $Sdk 'bin\rexruntime.dll') $stage -Force
    $log = Join-Path $buildDir 'physical_heap_stress.log'
    Push-Location $stage
    try { & .\mua_physical_heap_stress.exe *> $log; $code = $LASTEXITCODE } finally { Pop-Location }
    Get-Content $log | Where-Object { $_ -match '^(view|PASS|FAIL)' } | ForEach-Object { Write-Host $_ }
    if ($code) { throw "Allocator stress test failed ($code): the runtime in tools\rexglue is not the patched one, or the patch is not effective" }
}

if (-not $SkipBuild) { Install-PatchedRuntime }
if ($StressTest) { Invoke-StressTest }
