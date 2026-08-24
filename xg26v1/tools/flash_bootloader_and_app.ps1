[CmdletBinding()]
param(
    [string]$SerialNo
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$slt = Join-Path $env:LOCALAPPDATA 'Programs\SiliconLabsTool\slt.exe'

if (-not (Test-Path -LiteralPath $slt)) {
    throw "SLT not found at $slt"
}

$commanderRoot = (& $slt where commander).Trim()
$commander = Join-Path $commanderRoot 'commander.exe'
$bootloaderHex = Join-Path $projectRoot 'bootloader\cmake_gcc\build\base\bootloader-storage-internal-single-3200k.hex'
$applicationHex = Join-Path $projectRoot 'cmake_gcc\build\base\smart-iv-monitor.hex'

foreach ($image in @($bootloaderHex, $applicationHex)) {
    if (-not (Test-Path -LiteralPath $image)) {
        throw "Image not found: $image. Build both projects first."
    }
}

$arguments = @('flash', $bootloaderHex, $applicationHex)
if ($SerialNo) {
    $arguments += @('--serialno', $SerialNo)
}

Write-Host 'Flashing Gecko Bootloader and smart-iv-monitor application...'
& $commander @arguments
if ($LASTEXITCODE -ne 0) { throw "Combined flash failed ($LASTEXITCODE)" }
