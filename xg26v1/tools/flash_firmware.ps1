[CmdletBinding()]
param(
    [string]$SerialNo
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$slt = Join-Path $env:LOCALAPPDATA 'Programs\SiliconLabsTool\slt.exe'
$commanderRoot = (& $slt where commander).Trim()
$commander = Join-Path $commanderRoot 'commander.exe'
$hex = Join-Path $projectRoot 'cmake_gcc\build\base\smart-iv-monitor.hex'

if (-not (Test-Path -LiteralPath $hex)) {
    throw "Firmware image not found. Run tools\build_firmware.ps1 first."
}

$arguments = @('flash', $hex)
if ($SerialNo) {
    $arguments += @('--serialno', $SerialNo)
}

& $commander @arguments
if ($LASTEXITCODE -ne 0) { throw "Firmware flash failed ($LASTEXITCODE)" }
