<#
.SYNOPSIS
  Builds and runs the headless tests with Visual Studio's clang++: tests\game_files_tests.cpp (XEX identity parsing,
  SHA-256 and the game-file check) first, then the CMake test groups of the configured build folder (the launcher and
  runtime settings, window, logging and file mapping tests). Nothing opens a window or plays audio; the tests use
  scratch folders only.

.DESCRIPTION
  The CMake groups need the build folder that tools\build.ps1 configures. mua_headless_tests need no ReXGlue runtime;
  mua_runtime_tests run beside the patched runtime in tools\rexglue and are skipped (with a warning) when it does not
  match patches\rexglue-sdk.

.EXAMPLE
  .\tools\test.ps1
  .\tools\test.ps1 -GameDir game\extracted    # also checks that folder against the supported profile and the D: mapping
  .\tools\test.ps1 -SkipCMake                 # only the game file check
#>
param(
    [string]$GameDir,
    [string]$BuildDir,  # relative to the repository root; default: build
    [int]$TimeoutSeconds = 180,
    [switch]$SkipCMake,
    [switch]$SelfTest   # check that a failing test process fails this script
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')
$OutDir = Join-Path $Root 'build\tests'
$Exe = Join-Path $OutDir 'game_files_tests.exe'
$BuildDir = if (-not $BuildDir) { Join-Path $Root 'build' } elseif ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }

$Vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $Vswhere)) { throw 'Visual Studio Installer (vswhere.exe) not found' }
$VsRoot = & $Vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
if (-not $VsRoot) { throw 'Visual Studio with the C++ x64 tools and C++ Clang tools is required' }
$VsDevCmd = Join-Path $VsRoot 'Common7\Tools\VsDevCmd.bat'
$LlvmBin = Join-Path $VsRoot 'VC\Tools\Llvm\x64\bin'
$ClangXX = Join-Path $LlvmBin 'clang++.exe'
$CMake = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$Ninja = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
foreach ($tool in @($VsDevCmd, $ClangXX, $CMake, $Ninja)) {
    if (-not (Test-Path $tool)) { throw "Required tool not found: $tool" }
}

function Invoke-TestExecutable([string]$Path, [string[]]$TestArgs) {
    $name = [IO.Path]::GetFileNameWithoutExtension($Path)
    $log = Join-Path (Split-Path $Path) "$name.out.txt"
    $start = @{ FilePath = $Path; WorkingDirectory = (Split-Path $Path); NoNewWindow = $true; PassThru = $true
                RedirectStandardOutput = $log; RedirectStandardError = "$log.err" }
    if ($TestArgs.Count) { $start.ArgumentList = $TestArgs }
    $process = Start-Process @start
    # Without the handle cached, ExitCode reads as $null after WaitForExit and a failing test passes.
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        throw "$name did not finish within $TimeoutSeconds seconds"
    }
    $process.WaitForExit()
    $output = @(Get-Content -LiteralPath $log)
    Write-Host ("  {0,-36} {1}" -f $name, ($output | Select-Object -Last 1))
    if ($process.ExitCode) {
        $output | Where-Object { $_ -match 'FAIL' } | ForEach-Object { Write-Host "    $_" }
        throw "$name failed ($($process.ExitCode))"
    }
}

if ($SelfTest) {
    New-Item -ItemType Directory -Force $OutDir | Out-Null
    $failing = Join-Path $OutDir 'self_test_failing.cmd'
    Set-Content -LiteralPath $failing -Value '@exit /b 3'
    $failed = $false
    try { Invoke-TestExecutable $failing @() } catch { $failed = $true }
    if (-not $failed) { throw 'A failing test process did not fail the runner' }
    Write-Host 'Self-test passed: a failing test process fails the runner.'
    return
}

$SavedEnv = [Environment]::GetEnvironmentVariables()
try {
    # Keep MSYS2's gcc out of the build.
    $env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch '^C:\\msys64' }) -join ';'
    $env:PATH = "$env:PATH;$(Split-Path $Vswhere)"
    $vsEnv = cmd.exe /c "`"$VsDevCmd`" -arch=x64 -host_arch=x64 -no_logo >nul && set"
    foreach ($line in $vsEnv) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
    $env:PATH = "$LlvmBin;$(Split-Path $CMake);$(Split-Path $Ninja);$env:PATH"

    New-Item -ItemType Directory -Force $OutDir | Out-Null
    $src = Join-Path $Root 'src'
    & $ClangXX -std=c++23 -O1 -Wall -Wextra -I $src (Join-Path $Root 'tests\game_files_tests.cpp') `
        (Join-Path $src 'game_files.cpp') (Join-Path $src 'game_profile.cpp') (Join-Path $src 'xex_identity.cpp') -o $Exe
    if ($LASTEXITCODE) { throw "Test build failed ($LASTEXITCODE)" }

    [string[]]$testArgs = if ($GameDir) { (Resolve-Path $GameDir).Path } else { @() }
    Write-Host 'Game file check:'
    & $Exe @testArgs
    if ($LASTEXITCODE) { throw "Tests failed ($LASTEXITCODE)" }

    if ($SkipCMake) { return }
    if (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) { throw "$BuildDir is not configured: run .\tools\build.ps1 first" }

    $groups = @('mua_headless_tests')
    $runtimeProblems = @(Test-PatchedRuntime (Join-Path $Root 'tools\rexglue\win-amd64'))
    if ($runtimeProblems.Count) {
        Write-Warning "Skipping mua_runtime_tests: tools\rexglue is not the patched runtime ($($runtimeProblems -join '; '))"
    } else {
        $groups += 'mua_runtime_tests'
    }
    foreach ($group in $groups) {
        Write-Host "Building $group..."
        & $Ninja -C $BuildDir $group
        if ($LASTEXITCODE) { throw "Building $group failed ($LASTEXITCODE)" }
    }

    $headless = @('achievements_overlay', 'aspect_ratio','audio_pcm_firewall', 'crash_report', 'diagnostics', 'dpi', 'frame_limiter', 'frame_pacing', 'game_data', 'game_install', 'game_library', 'gpu_perf_patches', 'graphics_adapter', 'guest_map', 'render_scale', 'keyboard', 'launcher_handoff', 'launcher_quit',
                  'lifecycle', 'log_path', 'multisampling', 'relaunch', 'resolve_order', 'runtime_config',
                  'save_diagnostics', 'sdk_fix', 'shader_storage_repair', 'skip_intro_notice', 'sound_stream_ring', 'user_data', 'window_host',
                  'window_placement', 'xaudio_barrier', 'xma_packet_continuity', 'd3d12_async_submission', 'd3d12_sampler_memo',
                  'd3d12_copy_validation') | ForEach-Object { "mua_$($_)_tests" }
    $runtime = @('active_log_tests', 'filesystem_mapping_tests', 'frame_stats_tests', 'log_filter_tests',
                 'physical_heap_stress', 'settings_tests', 'shutdown_order_tests', 'title_thread_shutdown_tests',
                 'vmsum_tests') | ForEach-Object { "mua_$_" }
    Write-Host 'Headless tests:'
    foreach ($name in $headless) { Invoke-TestExecutable (Join-Path $BuildDir "$name.exe") @() }
    if ($groups -contains 'mua_runtime_tests') {
        Write-Host 'Runtime tests (beside the patched runtime, no window):'
        foreach ($name in $runtime) {
            [string[]]$extra = if ($GameDir -and $name -eq 'mua_filesystem_mapping_tests') { (Resolve-Path $GameDir).Path } else { @() }
            Invoke-TestExecutable (Join-Path $BuildDir "$name.exe") $extra
        }
    }
    Write-Host 'All tests passed.'
} finally {
    foreach ($name in @([Environment]::GetEnvironmentVariables().Keys)) {
        if (-not $SavedEnv.Contains($name)) { Remove-Item "Env:$name" }
    }
    foreach ($name in $SavedEnv.Keys) { Set-Item "Env:$name" $SavedEnv[$name] }
}
