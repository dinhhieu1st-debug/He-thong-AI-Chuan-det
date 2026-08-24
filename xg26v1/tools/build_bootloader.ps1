[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$slt = Join-Path $env:LOCALAPPDATA 'Programs\SiliconLabsTool\slt.exe'

if (-not (Test-Path -LiteralPath $slt)) {
    throw "SLT not found at $slt"
}

$cmakeRoot = (& $slt where cmake).Trim()
$cmake = Join-Path $cmakeRoot 'bin\cmake.exe'

Push-Location $projectRoot
try {
    & $cmake --preset project -S bootloader\cmake_gcc
    if ($LASTEXITCODE -ne 0) { throw "Bootloader configure failed ($LASTEXITCODE)" }

    & $cmake --build bootloader\cmake_gcc\build --config base --target bootloader-storage-internal-single-3200k
    if ($LASTEXITCODE -ne 0) { throw "Bootloader build failed ($LASTEXITCODE)" }

    $hex = Join-Path $projectRoot 'bootloader\cmake_gcc\build\base\bootloader-storage-internal-single-3200k.hex'
    Write-Host "Bootloader ready: $hex"
} finally {
    Pop-Location
}
