<#
.SYNOPSIS
  Builds the release update helper, trg-updater.exe, with MSYS2 CLANG64. Local only.

.DESCRIPTION
  Compiles the trg-updater sources listed in CMakeLists.txt (src\updater) with the MSYS2 CLANG64 clang and LLD into a
  static libc++ helper that uses only Windows system DLLs, including the Windows UCRT. The debug information is split
  with llvm-objcopy into trg-updater.debug next to it; the executable keeps only a .gnu_debuglink to it.
  The game itself and the development builds' trg-updater.exe are still built by CMake with Visual Studio's clang.

.EXAMPLE
  .\tools\build-updater.ps1                          # -> build-release\trg-updater-clang64\
  .\tools\build-updater.ps1 -Msys2Root D:\msys64
#>
param(
    [string]$OutDir,  # relative to the repository root; default: build-release\trg-updater-clang64
    [string]$Msys2Root = 'C:\msys64'
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
$OutDir = if (-not $OutDir) { Join-Path $Root 'build-release\trg-updater-clang64' } elseif ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
$Bin = Join-Path $Msys2Root 'clang64\bin'
$Cxx = Join-Path $Bin 'clang++.exe'
$Objcopy = Join-Path $Bin 'llvm-objcopy.exe'
$Objdump = Join-Path $Bin 'llvm-objdump.exe'
foreach ($tool in $Cxx, $Objcopy, $Objdump) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Missing $tool (install MSYS2 and run: pacman -S mingw-w64-clang-x86_64-toolchain)" }
}

$cmake = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw
if ($cmake -notmatch '(?s)add_executable\(trg-updater WIN32\s+([^)]*)\)') { throw 'The trg-updater target was not found in CMakeLists.txt' }
$Sources = @($Matches[1] -split '\s+' | Where-Object { $_ })
$RootForward = $Root.Replace('\', '/')
$CompileFlags = @('-O2', '-g', '-DNDEBUG', '-DMUA_UPDATER_HELPER_BUILD=1', '-std=c++20', "-fmacro-prefix-map=$RootForward/=", '-Wall', '-Wextra', '-I',
    "$RootForward/src")
$LinkFlags = @('-O2', '-g', '-DNDEBUG', '-mwindows', '-static')
$Libraries = @('-lshell32', '-lkernel32', '-luser32', '-lgdi32', '-lwinspool', '-lshell32', '-lole32', '-loleaut32', '-luuid',
    '-lcomdlg32', '-ladvapi32')
# The helper is copied to a temporary folder and run from there, so it may load nothing but Windows itself.
$SystemDlls = @('kernel32.dll', 'shell32.dll', 'user32.dll', 'advapi32.dll', 'ntdll.dll', 'shlwapi.dll', 'ole32.dll', 'bcrypt.dll')

if (Test-Path -LiteralPath $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $OutDir 'obj') | Out-Null
$savedPath = $env:PATH
Push-Location $OutDir
try {
    $env:PATH = "$Bin;$env:PATH"
    Write-Host "Update helper: $((& $Cxx --version | Select-Object -First 1))"
    $objects = foreach ($source in $Sources) {
        $object = 'obj/' + (Split-Path $source -Leaf) + '.obj'
        & $Cxx @CompileFlags -c "$RootForward/$source" -o $object
        if ($LASTEXITCODE) { throw "Compiling $source failed" }
        $object
    }
    & $Cxx @LinkFlags @objects -o obj/trg-updater.exe @Libraries
    if ($LASTEXITCODE) { throw 'Linking trg-updater.exe failed' }
    & $Objcopy --only-keep-debug obj/trg-updater.exe trg-updater.debug
    if ($LASTEXITCODE) { throw 'llvm-objcopy --only-keep-debug failed' }
    & $Objcopy --strip-debug --add-gnu-debuglink=trg-updater.debug obj/trg-updater.exe trg-updater.exe
    if ($LASTEXITCODE) { throw 'llvm-objcopy --strip-debug failed' }

    $imports = @(& $Objdump -p trg-updater.exe | ForEach-Object { if ($_ -match 'DLL Name:\s*(\S+)') { $Matches[1] } })
    if ($LASTEXITCODE -or -not $imports.Count) { throw 'llvm-objdump could not read the imports of trg-updater.exe' }
    $foreign = @($imports | Where-Object { $_.ToLowerInvariant() -notin $SystemDlls -and $_ -notmatch '^api-ms-win-' })
    if ($foreign.Count) { throw "trg-updater.exe imports non-system DLLs: $($foreign -join ', ')" }
} finally {
    $env:PATH = $savedPath
    Pop-Location
}
Write-Host "  $(Join-Path $OutDir 'trg-updater.exe') ($($imports.Count) system DLLs imported)"
Write-Host "  $(Join-Path $OutDir 'trg-updater.debug')"
