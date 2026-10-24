# Shared by build.ps1, build-patched-rexruntime.ps1 and package-release.ps1: the pinned ReXGlue baseline, the
# runtime, GPU plugin and code generator patches (those for Windows; the Linux ones are for
# build-patched-rexruntime.sh), the check that an SDK folder holds the files built from them, and the check that a
# build folder holds the SDK's files.

$RuntimePatchDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'patches\rexglue-sdk'
$RuntimeStampName = 'rexruntime-patch.txt'
$RuntimeFiles = @('bin/rexruntime.dll', 'bin/rexruntimerd.dll', 'lib/rexruntime.lib', 'lib/rexruntimerd.lib',
                  'bin/rexgpu-xenos.dll', 'bin/rexgpu-xenosrd.dll', 'bin/rexglue.exe')
# How the patched files are built; part of the fingerprint, so changing them invalidates the installed files.
$RuntimePlatform = 'windows'
$RuntimeBuildFlags = '-march=x86-64-v2'
$RuntimeBuildTargets = @('rexruntime', 'rexgpu-xenos')
# The code generator runs beside the Release runtime, so it is built for Release only.
$RuntimeReleaseOnlyTargets = @('rexglue')
$RuntimeBuildConfigs = @('RelWithDebInfo', 'Release')

# Release profile-guided optimization of the GPU plugin (patch 0126): used when this game's profile exists.
function Get-RuntimePgo {
    $profileFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'patches\rexglue-sdk-pgo\rexgpu-xenos.profdata'
    if (Test-Path -LiteralPath $profileFile -PathType Leaf) {
        return @{ Mode = "use $(Get-FileSha256 $profileFile)"; Profile = $profileFile }
    }
    return @{ Mode = 'off'; Profile = '' }
}

function Get-VisualStudioRoot {
    $vsRoot = $null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) { $vsRoot = "$(& $vswhere -latest -products * -property installationPath)".Trim() }
    if (-not $vsRoot) { $vsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community' }
    return $vsRoot
}

# The clang and MSVC toolset versions ReXGlue is built with.
$script:RuntimeCompiler = $null
function Get-RuntimeCompiler {
    if (-not $script:RuntimeCompiler) {
        $vsRoot = Get-VisualStudioRoot
        $clang = "$(& (Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang++.exe') --version | Select-Object -First 1)".Trim()
        $msvc = "$(Get-Content -LiteralPath (Join-Path $vsRoot 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -TotalCount 1)".Trim()
        $script:RuntimeCompiler = "$clang; MSVC $msvc"
    }
    return $script:RuntimeCompiler
}

function Read-RuntimeBaseline {
    $values = @{}
    foreach ($line in Get-Content -LiteralPath (Join-Path $RuntimePatchDir 'baseline.env')) {
        if ($line -match '^\s*([A-Z0-9_]+)=(.*)$') { $values[$Matches[1]] = $Matches[2].Trim() }
    }
    return $values
}

function Get-FileSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# The files the patches changed or added in the ReXGlue source, with their content hash and time. Resetting the source
# and applying the patches again rewrites all of them, and Ninja rebuilds whatever is newer than its object.
function Get-PatchedSourceState([string]$Src) {
    $names = @(& git -C $Src diff --name-only) + @(& git -C $Src ls-files --others --exclude-standard -- src include tests cmake)
    $state = @{}
    foreach ($name in $names) {
        $path = Join-Path $Src $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $state[$name] = @{ Sha256 = Get-FileSha256 $path; Time = (Get-Item -LiteralPath $path).LastWriteTimeUtc }
        }
    }
    return $state
}

# Gives back the time of every file in $State that came out of the patching with the same content; returns how many.
function Restore-UnchangedFileTimes([string]$Src, $State) {
    $kept = 0
    foreach ($name in $State.Keys) {
        $path = Join-Path $Src $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        if ((Get-FileSha256 $path) -ne $State[$name].Sha256) { continue }
        (Get-Item -LiteralPath $path).LastWriteTimeUtc = $State[$name].Time
        $kept++
    }
    return $kept
}

# Copies only when the content differs, so an installed file keeps its time and nothing built from it is redone.
function Copy-FileIfDifferent([string]$Source, [string]$Destination) {
    if ((Test-Path -LiteralPath $Destination -PathType Leaf) -and (Get-FileSha256 $Source) -eq (Get-FileSha256 $Destination)) {
        return $false
    }
    New-Item -ItemType Directory -Force (Split-Path $Destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    return $true
}

function Copy-TreeIfDifferent([string]$Source, [string]$Destination) {
    $copied = 0
    foreach ($file in Get-ChildItem -LiteralPath $Source -Recurse -File) {
        $relative = $file.FullName.Substring($Source.TrimEnd('\').Length).TrimStart('\')
        if (Copy-FileIfDifferent $file.FullName (Join-Path $Destination $relative)) { $copied++ }
    }
    return $copied
}

function Get-TextSha256([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))) -replace '-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

# NNNN-name.patch applies on every platform, NNNN-name.windows.patch only here and NNNN-name.linux.patch only on
# Linux (tools/build-patched-rexruntime.sh). The numbers are unique, so the order is unambiguous.
$RuntimePatchNamePattern = '^(?<number>[0-9]{4})-[a-z0-9]+(-[a-z0-9]+)*(\.(?<platform>windows|linux))?\.patch$'

# The patches this platform applies, in the order they are applied (by ordinal name).
function Get-RuntimePatchFiles([string]$Platform = $RuntimePlatform) {
    $files = @{}
    $numbers = @{}
    foreach ($file in Get-ChildItem -LiteralPath $RuntimePatchDir -Filter '*.patch' -File) {
        if ($file.Name -notmatch $RuntimePatchNamePattern) { throw "patches\rexglue-sdk\$($file.Name) is not named NNNN-name[.windows|.linux].patch" }
        $number = $Matches['number']
        if ($numbers.ContainsKey($number)) { throw "patches\rexglue-sdk\$($file.Name) and $($numbers[$number]) share the number $number" }
        $numbers[$number] = $file.Name
        if ($Matches['platform'] -and $Matches['platform'] -ne $Platform) { continue }
        $files[$file.Name] = $file
    }
    if (-not $files.Count) { throw 'No patches in patches\rexglue-sdk' }
    $names = [string[]]@($files.Keys)
    [Array]::Sort($names, [StringComparer]::Ordinal)
    return @($names | ForEach-Object { $files[$_] })
}

# One hash for the whole set: every patch's name and hash, in order.
function Get-RuntimePatchSetSha256 {
    return Get-TextSha256 ((Get-RuntimePatchFiles | ForEach-Object { "$($_.Name) $(Get-FileSha256 $_.FullName)" }) -join "`n")
}

# The fingerprint of everything the patched files are built from: the ReXGlue baseline, the platform, the compiler,
# the build settings and the ordered patch set.
function Get-RuntimeInputsSha256 {
    $baseline = Read-RuntimeBaseline
    $lines = @("sdk_version=$($baseline.SDK_VERSION)", "sdk_tag=$($baseline.SDK_TAG)", "sdk_commit=$($baseline.SDK_COMMIT)",
               "platform=$RuntimePlatform", "compiler=$(Get-RuntimeCompiler)",
               "build_flags=$RuntimeBuildFlags", "build_pgo=$((Get-RuntimePgo).Mode)", "build_targets=$($RuntimeBuildTargets -join ',')",
               "release_only_targets=$($RuntimeReleaseOnlyTargets -join ',')",
               "build_configs=$($RuntimeBuildConfigs -join ',')")
    $lines += @(Get-RuntimePatchFiles | ForEach-Object { "patch $($_.Name) $(Get-FileSha256 $_.FullName)" })
    return Get-TextSha256 ($lines -join "`n")
}

function Get-OfficialRuntimeHashes($Baseline) {
    return @{
        'bin/rexruntime.dll'     = $Baseline.OFFICIAL_WIN_RUNTIME_SHA256
        'bin/rexruntimerd.dll'   = $Baseline.OFFICIAL_WIN_RUNTIME_RD_SHA256
        'lib/rexruntime.lib'     = $Baseline.OFFICIAL_WIN_RUNTIME_LIB_SHA256
        'lib/rexruntimerd.lib'   = $Baseline.OFFICIAL_WIN_RUNTIME_RD_LIB_SHA256
        'bin/rexgpu-xenos.dll'   = $Baseline.OFFICIAL_WIN_GPU_SHA256
        'bin/rexgpu-xenosrd.dll' = $Baseline.OFFICIAL_WIN_GPU_RD_SHA256
        'bin/rexglue.exe'        = $Baseline.OFFICIAL_WIN_REXGLUE_SHA256
    }
}

# Records the installed patched files with the fingerprint they were built from.
function Write-RuntimeStamp([string]$SdkDir, [string]$InputsSha256) {
    $baseline = Read-RuntimeBaseline
    $lines = @("sdk_version=$($baseline.SDK_VERSION)", "sdk_commit=$($baseline.SDK_COMMIT)", "inputs_sha256=$InputsSha256",
               "compiler=$(Get-RuntimeCompiler)", "patch_sha256=$(Get-RuntimePatchSetSha256)",
               "patches=$((Get-RuntimePatchFiles | ForEach-Object { $_.Name }) -join ',')")
    foreach ($leaf in $RuntimeFiles) { $lines += "$(Get-FileSha256 (Join-Path $SdkDir $leaf))  $leaf" }
    [IO.File]::WriteAllText((Join-Path $SdkDir $RuntimeStampName), (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
    return $lines
}

function Read-RuntimeStamp([string]$SdkDir) {
    $path = Join-Path $SdkDir $RuntimeStampName
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    $stamp = @{ Values = @{}; Hashes = @{} }
    foreach ($line in Get-Content -LiteralPath $path) {
        if ($line -match '^([a-z_0-9]+)=(.*)$') { $stamp.Values[$Matches[1]] = $Matches[2] }
        elseif ($line -match '^([0-9a-f]{64})  (.+)$') { $stamp.Hashes[$Matches[2]] = $Matches[1] }
    }
    return $stamp
}

# Which inputs differ from the ones the stamp was written for.
function Get-RuntimeInputsChange($StampValues) {
    $old = @(if ($StampValues['patches']) { $StampValues['patches'] -split ',' })
    $current = @(Get-RuntimePatchFiles | ForEach-Object { $_.Name })
    $changes = @()
    $added = @($current | Where-Object { $old -notcontains $_ })
    $removed = @($old | Where-Object { $current -notcontains $_ })
    if ($added.Count) { $changes += "patches added: $($added -join ', ')" }
    if ($removed.Count) { $changes += "patches removed or renamed: $($removed -join ', ')" }
    if ($StampValues['compiler'] -ne (Get-RuntimeCompiler)) { $changes += "compiler changed: $(Get-RuntimeCompiler)" }
    if (-not $changes.Count) {
        $changes += $(if ($StampValues['patch_sha256'] -ne (Get-RuntimePatchSetSha256)) { 'a patch changed' } else { 'ReXGlue baseline or build settings changed' })
    }
    return $changes -join '; '
}

# Returns the problems found; an empty list means $SdkDir holds the project's patched runtime, GPU plugin and code
# generator, built from the current baseline and patches.
function Test-PatchedRuntime([string]$SdkDir) {
    $problems = New-Object System.Collections.Generic.List[string]
    $baseline = Read-RuntimeBaseline
    $stamp = Read-RuntimeStamp $SdkDir
    if (-not $stamp) {
        $problems.Add("$RuntimeStampName is missing in $SdkDir")
        return $problems
    }
    if ($stamp.Values['sdk_commit'] -ne $baseline.SDK_COMMIT) { $problems.Add("runtime was not built from ReXGlue $($baseline.SDK_TAG) ($($baseline.SDK_COMMIT))") }
    if ($stamp.Values['inputs_sha256'] -ne (Get-RuntimeInputsSha256)) { $problems.Add("runtime was built from other inputs than the current ones ($(Get-RuntimeInputsChange $stamp.Values))") }
    $official = Get-OfficialRuntimeHashes $baseline
    foreach ($leaf in $RuntimeFiles) {
        $path = Join-Path $SdkDir $leaf
        if (-not $stamp.Hashes.ContainsKey($leaf)) { $problems.Add("$leaf is not in $RuntimeStampName"); continue }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $problems.Add("missing: $leaf"); continue }
        $hash = Get-FileSha256 $path
        if ($hash -ne $stamp.Hashes[$leaf]) { $problems.Add("$leaf does not match the patched build") }
        if ($hash -eq $official[$leaf]) { $problems.Add("$leaf is the stock ReXGlue release file") }
    }
    return $problems
}

# Rebuilds and installs the patched runtime, GPU plugin and code generator if the installed ones are not current, and
# stops if they still are not afterwards. Only the Release and RelWithDebInfo files are patched.
function Update-PatchedRuntime([string]$SdkDir, [int]$Jobs = 0, [string]$Builder = (Join-Path $PSScriptRoot 'build-patched-rexruntime.ps1')) {
    $problems = Test-PatchedRuntime $SdkDir
    if ($problems.Count) {
        Write-Host 'ReXGlue patches: the installed runtime and GPU plugin are out of date, rebuilding them:'
        foreach ($problem in $problems) { Write-Host "  $problem" }
        $builderArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Builder)
        if ($Jobs -gt 0) { $builderArgs += @('-Jobs', $Jobs) }
        & (Get-Process -Id $PID).Path @builderArgs
        if ($LASTEXITCODE) { throw "Building the patched ReXGlue runtime failed ($LASTEXITCODE)" }
        $problems = Test-PatchedRuntime $SdkDir
        if ($problems.Count) {
            foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
            throw 'The rebuilt ReXGlue runtime and GPU plugin still do not match patches\rexglue-sdk'
        }
    }
    $stamp = Read-RuntimeStamp $SdkDir
    $patchCount = @($stamp.Values['patches'] -split ',').Count
    Write-Host "ReXGlue patches: current ($patchCount patches, fingerprint $($stamp.Values['inputs_sha256'].Substring(0, 12)))"
}

# The runtime and GPU plugin DLLs a configuration loads.
function Get-RuntimeDllNames([string]$Config) {
    $suffix = @{ Debug = 'd'; RelWithDebInfo = 'rd'; Release = '' }[$Config]
    return @("rexruntime$suffix.dll", "rexgpu-xenos$suffix.dll")
}

# Returns the problems found; an empty list means $BuildDir holds the executable and exactly the SDK's DLLs.
function Test-StagedRuntime([string]$SdkDir, [string]$BuildDir, [string]$Config) {
    $problems = New-Object System.Collections.Generic.List[string]
    $exe = Join-Path $BuildDir 'marvel_ultimate_alliance.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { $problems.Add("missing executable: $exe") }
    foreach ($name in Get-RuntimeDllNames $Config) {
        $expected = Join-Path $SdkDir "bin\$name"
        $actual = Join-Path $BuildDir $name
        if (-not (Test-Path -LiteralPath $expected -PathType Leaf)) { $problems.Add("missing in the SDK: $expected"); continue }
        if (-not (Test-Path -LiteralPath $actual -PathType Leaf)) { $problems.Add("$name is not next to the executable`n    expected: $expected`n    actual:   (missing) $actual"); continue }
        $expectedHash = Get-FileSha256 $expected
        $actualHash = Get-FileSha256 $actual
        if ($expectedHash -ne $actualHash) {
            $problems.Add("$name next to the executable is not the installed one`n    expected: $expected ($($expectedHash.Substring(0, 12)))`n    actual:   $actual ($($actualHash.Substring(0, 12)))")
        }
    }
    return $problems
}

# Copies the SDK's DLLs over the ones in $BuildDir that differ.
function Sync-StagedRuntime([string]$SdkDir, [string]$BuildDir, [string]$Config) {
    foreach ($name in Get-RuntimeDllNames $Config) {
        $expected = Join-Path $SdkDir "bin\$name"
        $actual = Join-Path $BuildDir $name
        if (-not (Test-Path -LiteralPath $expected -PathType Leaf)) { continue }
        if ((Test-Path -LiteralPath $actual -PathType Leaf) -and (Get-FileSha256 $expected) -eq (Get-FileSha256 $actual)) { continue }
        try { Copy-Item -LiteralPath $expected -Destination $actual -Force }
        catch { throw "Could not replace $actual with $expected (is the game still running?): $($_.Exception.Message)" }
        Write-Host "Restaged $name from tools\rexglue"
    }
}
