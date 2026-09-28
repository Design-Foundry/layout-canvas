# Loads MSVC + Emscripten into the current session:
#     . .\tools\devshell.ps1

$ErrorActionPreference = 'Stop'

$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vs = & "$installer\vswhere.exe" -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio install with the C++ tools (VC.Tools.x86.x64) was found.' }

$env:Path = "$installer;$env:Path"
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

$cmakeBin = "$env:ProgramFiles\CMake\bin"
if (Test-Path $cmakeBin) { $env:Path = "$cmakeBin;$env:Path" }

if (-not $env:EMSDK) { $env:EMSDK = [Environment]::GetEnvironmentVariable('EMSDK', 'User') }
if (-not (Get-Command emcc -ErrorAction SilentlyContinue)) {
    $env:Path = "$env:EMSDK;$env:EMSDK\upstream\emscripten;$env:Path"
}

$msvc = Split-Path $env:VCToolsInstallDir -Leaf
$emcc = (& emcc --version 2>$null | Select-Object -First 1) -replace '^.*\) ', ''
Write-Host "devshell  MSVC $msvc  |  emcc $emcc  |  $(Split-Path $vs -Leaf)" -ForegroundColor DarkGray
