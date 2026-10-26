<#
.SYNOPSIS
  Builds build\marvel_ultimate_alliance_bundle.exe (the launcher), build\marvel_ultimate_alliance_mua1.exe (Marvel Ultimate
  Alliance), build\marvel_ultimate_alliance_mua1_gold.exe (Gold Edition) and build\marvel_ultimate_alliance_mua2.exe
  (Marvel Ultimate Alliance 2) with Visual Studio's clang++, lld-link, CMake and Ninja. Only -Run launches the
  launcher.

.DESCRIPTION
  One invocation does the whole incremental workflow: it sets up the ReXGlue SDK when it is missing, makes sure the
  patched ReXGlue runtime and GPU plugin in tools\rexglue match patches\rexglue-sdk (rebuilding them with
  build-patched-rexruntime.ps1 when they do not), generates the
  recompiled code of each game when its default.xex, manifest, an include or the rexglue tool changed (the build's
  codegen step decides), reconfigures CMake when the build settings or a generated\<game>\sources.cmake changed, and
  builds the four programs.

.EXAMPLE
  .\tools\build.ps1              # incremental build only
  .\tools\build.ps1 -Run         # build, then launch the game with game\extracted
  .\tools\build.ps1 -Clean       # clean configure and build of build\ (ReXGlue's source and build tree are kept)
  .\tools\build.ps1 -Config Release -BuildDir build-release
#>
param(
    [switch]$Clean,
    [switch]$Run,
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Config = 'RelWithDebInfo',
    [string]$BuildDir,  # relative to the repository root; default: build
    [int]$Jobs = 0,
    [string]$TrgLauncherDir  # a TRG Launcher checkout, used until third_party\trg-launcher is a submodule
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')
$BuildDir = if (-not $BuildDir) { Join-Path $Root 'build' } elseif ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }
$SdkRoot = Join-Path $Root 'tools\rexglue\win-amd64'
$GameData = Join-Path $Root 'game\extracted'
$UserData = Join-Path $BuildDir 'user_data'
$Exe = Join-Path $BuildDir 'marvel_ultimate_alliance_bundle.exe'
$Programs = @('marvel_ultimate_alliance_bundle.exe', 'marvel_ultimate_alliance_mua1.exe', 'marvel_ultimate_alliance_mua1_gold.exe',
    'marvel_ultimate_alliance_mua2.exe')
# One recompiled module per game: the XEX it is generated from, that XEX's SHA-256 and the cache entry CMake records.
$Modules = @(
    @{ Name = 'mua1-gold'; Xex = (Join-Path $Root 'game\extracted\default.xex')
       Sha256 = '06D1D7811E1668A5C16AFEF5102CA59B00684DC1EA6184849418F59F022B86B7'
       Cache = 'MUA_GENERATED_SOURCES_SHA256_MUA1_GOLD'; Sources = (Join-Path $Root 'generated\mua1-gold\sources.cmake') },
    @{ Name = 'mua1'; Xex = (Join-Path $Root 'game\mua1\default.xex')
       Sha256 = '44E5EEC135073224F2ABE09DEAEF765A6D130C81AA575D5C376338F470BEF92A'
       Cache = 'MUA_GENERATED_SOURCES_SHA256_MUA1'; Sources = (Join-Path $Root 'generated\mua1\sources.cmake') },
    @{ Name = 'mua2'; Xex = (Join-Path $Root 'game\mua2\default.xex')
       Sha256 = '2613DC17AF39CEFACB7226855F62281D83B6E991C9D467D9E8EBB0A681AB0440'
       Cache = 'MUA_GENERATED_SOURCES_SHA256_MUA2'; Sources = (Join-Path $Root 'generated\mua2\sources.cmake') }
)
$GameFolders = @('movies', 'sounds', 'z')

$suffix = @{ Debug = 'd'; RelWithDebInfo = 'rd'; Release = '' }[$Config]
$RuntimeDlls = @("rexruntime$suffix.dll", "rexgpu-xenos$suffix.dll")
if ($Config -ne 'Release') { $RuntimeDlls += "TracyClient$suffix.dll" }

function Resolve-TrgLauncher {
    if (Test-Path (Join-Path $Root 'third_party\trg-launcher\CMakeLists.txt')) { return '' }
    $candidates = @($TrgLauncherDir, $env:MUA_TRG_LAUNCHER_DIR,
        (Join-Path $Root '..\geometry-wars-retro-evolved-2-recompiled\third_party\trg-launcher')) | Where-Object { $_ }
    foreach ($dir in $candidates) {
        if (Test-Path (Join-Path $dir 'CMakeLists.txt')) { return (Resolve-Path $dir).Path }
    }
    throw 'TRG Launcher not found: add the third_party\trg-launcher submodule or pass -TrgLauncherDir'
}

function Assert-RuntimeReady {
    $problems = @(Test-StagedRuntime $SdkRoot $BuildDir $Config)
    if ($Config -ne 'Debug') { $problems += @(Test-PatchedRuntime $SdkRoot) }
    if (-not $problems.Count) { return }
    Write-Host ''
    Write-Host 'RUNTIME CHECK FAILED' -ForegroundColor Red
    foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
    throw 'The files next to marvel_ultimate_alliance_bundle.exe are not the current ReXGlue runtime and GPU plugin from tools\rexglue'
}

function Assert-GameDataComplete {
    $missing = @($GameFolders | Where-Object { -not (Test-Path (Join-Path $GameData $_) -PathType Container) })
    if ($missing.Count) {
        throw "game\extracted is incomplete (missing: $($missing -join ', ')). Extract the whole Gold Edition disc into it."
    }
}

foreach ($module in $Modules) {
    if (-not (Test-Path $module.Xex)) { throw "Missing $($module.Xex) (install the $($module.Name) disc image first)" }
    if ((Get-FileHash $module.Xex -Algorithm SHA256).Hash -ne $module.Sha256) {
        throw "Unsupported $($module.Xex) (expected SHA-256 $($module.Sha256))"
    }
}
if ($Run) { Assert-GameDataComplete }

& (Join-Path $PSScriptRoot 'setup-sdk.ps1')
if ($Config -eq 'Debug') {
    Write-Host 'ReXGlue patches: not applied to the Debug runtime (stock rexruntimed.dll and rexgpu-xenosd.dll)'
} else {
    Update-PatchedRuntime $SdkRoot $Jobs
}

$Vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $Vswhere)) { throw 'Visual Studio Installer (vswhere.exe) not found' }
$VsRoot = & $Vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
if (-not $VsRoot) { throw 'Visual Studio with the C++ x64 tools and C++ Clang tools is required' }
$VsDevCmd = Join-Path $VsRoot 'Common7\Tools\VsDevCmd.bat'
$LlvmBin = Join-Path $VsRoot 'VC\Tools\Llvm\x64\bin'
$Clang = Join-Path $LlvmBin 'clang.exe'
$ClangXX = Join-Path $LlvmBin 'clang++.exe'
$LldLink = Join-Path $LlvmBin 'lld-link.exe'
$CMake = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$Ninja = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
foreach ($tool in @($VsDevCmd, $Clang, $ClangXX, $LldLink, $CMake, $Ninja)) {
    if (-not (Test-Path $tool)) { throw "Required tool not found: $tool" }
}
$TrgDir = Resolve-TrgLauncher

function Get-CachedValue([string]$name) {
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path $cache)) { return $null }
    $line = Select-String -Path $cache -Pattern "^${name}:[A-Z]+=(.*)$" | Select-Object -First 1
    if ($line) { return $line.Matches[0].Groups[1].Value }
}

function Test-Configured {
    (Test-Path (Join-Path $BuildDir 'build.ninja')) -and
        (Get-CachedValue 'CMAKE_BUILD_TYPE') -eq $Config -and
        (Get-CachedValue 'CMAKE_CXX_COMPILER') -eq ($ClangXX -replace '\\', '/') -and
        (-not $TrgDir -or (Get-CachedValue 'MUA_TRG_LAUNCHER_DIR') -eq ($TrgDir -replace '\\', '/')) -and
        -not ($Modules | Where-Object {
            (Get-CachedValue $_.Cache) -ne $(if (Test-Path $_.Sources) { (Get-FileHash $_.Sources).Hash.ToLower() } else { 'none' })
        })
}

function Invoke-Configure {
    Write-Host 'Configuring...'
    [string[]]$trg = if ($TrgDir) { "-DMUA_TRG_LAUNCHER_DIR:PATH=$TrgDir" } else { @() }
    & $CMake -S $Root -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" `
        "-DCMAKE_C_COMPILER:FILEPATH=$Clang" "-DCMAKE_CXX_COMPILER:FILEPATH=$ClangXX" `
        "-DCMAKE_LINKER:FILEPATH=$LldLink" "-DCMAKE_MAKE_PROGRAM:FILEPATH=$Ninja" @trg
    if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }
}

[string[]]$parallel = if ($Jobs -gt 0) { '--parallel', "$Jobs" } else { @() }

$SavedEnv = [Environment]::GetEnvironmentVariables()
try {
    # Keep MSYS2's gcc, cmake and ninja out of the build.
    $env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch '^C:\\msys64' }) -join ';'

    Write-Host "Marvel Ultimate Alliance Bundle Recompiled - $Config build"
    # VsDevCmd calls vswhere.exe by bare name; cmd does not resolve it from the current folder.
    $env:PATH = "$env:PATH;$(Split-Path $Vswhere)"
    $vsEnv = cmd.exe /c "`"$VsDevCmd`" -arch=x64 -host_arch=x64 -no_logo >nul && set"
    foreach ($line in $vsEnv) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
    if (-not $env:VCToolsVersion) { throw 'VsDevCmd.bat did not set up the x64 environment' }
    $env:PATH = "$LlvmBin;$(Split-Path $CMake);$(Split-Path $Ninja);$env:PATH"
    Write-Host "Clang++: $(& $ClangXX --version | Select-Object -First 1)"

    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host 'Cleaning build (keeping build\user_data)...'
        Get-ChildItem $BuildDir -Force | Where-Object { $_.Name -ne 'user_data' } | Remove-Item -Recurse -Force
    }

    if (-not (Test-Configured)) { Invoke-Configure }

    Write-Host 'Generating recompiled code if needed...'
    & $CMake --build $BuildDir --target marvel_ultimate_alliance_mua1_gold_codegen marvel_ultimate_alliance_mua1_codegen marvel_ultimate_alliance_mua2_codegen
    if ($LASTEXITCODE) { throw "Codegen failed ($LASTEXITCODE); see generated\codegen.log or run tools\codegen.ps1" }
    if (-not (Test-Configured)) { Invoke-Configure }

    Write-Host 'Building...'
    & $CMake --build $BuildDir @parallel
    if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }
} finally {
    foreach ($name in @([Environment]::GetEnvironmentVariables().Keys)) {
        if (-not $SavedEnv.Contains($name)) { Remove-Item "Env:$name" }
    }
    foreach ($name in $SavedEnv.Keys) { Set-Item "Env:$name" $SavedEnv[$name] }
}

foreach ($file in $Programs + @('marvel_ultimate_alliance.toml') + $RuntimeDlls) {
    if (-not (Test-Path (Join-Path $BuildDir $file))) { throw "Build output is missing $(Join-Path $BuildDir $file)" }
}
Assert-RuntimeReady
Write-Host "BUILD SUCCESSFUL: $Exe"
foreach ($program in $Programs[1..($Programs.Count - 1)]) { Write-Host "                  $(Join-Path $BuildDir $program)" }

if (-not $Run) { return }

$running = Get-Process -Name 'marvel_ultimate_alliance_bundle' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $Exe }
if ($running) {
    Write-Warning "marvel_ultimate_alliance_bundle.exe is already running (PID $($running.Id -join ', ')); not launching another copy."
    return
}

$gameArgs = @("--mua_games_root=`"$(Join-Path $Root 'game')`"", "--user_data_root=`"$UserData`"")
Write-Host 'Launching:'
Write-Host "  $Exe $($gameArgs -join ' ')"
Write-Host "  logs: $(Join-Path $BuildDir 'logs')"
Start-Process -FilePath $Exe -ArgumentList $gameArgs -WorkingDirectory $BuildDir
