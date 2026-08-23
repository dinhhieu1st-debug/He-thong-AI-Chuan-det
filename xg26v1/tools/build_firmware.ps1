[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$slt = Join-Path $env:LOCALAPPDATA 'Programs\SiliconLabsTool\slt.exe'

if (-not (Test-Path -LiteralPath $slt)) {
    throw "SLT not found at $slt. Install it from https://www.silabs.com/developers/simplicity-studio/silicon-labs-tools"
}

$cmakeRoot = (& $slt where cmake).Trim()
$cmake = Join-Path $cmakeRoot 'bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    throw "SLT CMake not found at $cmake"
}

$env:Path = "$(Split-Path -Parent $slt);$env:Path"
Push-Location $projectRoot
try {
    & $cmake --preset project -S cmake_gcc
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

    & $cmake --build cmake_gcc/build --config base --target smart-iv-monitor
    if ($LASTEXITCODE -ne 0) { throw "Firmware build failed ($LASTEXITCODE)" }

    $hex = Join-Path $projectRoot 'cmake_gcc\build\base\smart-iv-monitor.hex'
    Write-Host "Firmware ready: $hex"
} finally {
    Pop-Location
}
