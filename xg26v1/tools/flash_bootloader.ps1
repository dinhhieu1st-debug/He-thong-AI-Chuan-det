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
$hex = Join-Path $projectRoot 'bootloader\cmake_gcc\build\base\bootloader-storage-internal-single-3200k.hex'

if (-not (Test-Path -LiteralPath $hex)) {
    throw "Bootloader image not found. Run tools\build_bootloader.ps1 first."
}

$arguments = @('flash', $hex)
if ($SerialNo) {
    $arguments += @('--serialno', $SerialNo)
}

& $commander @arguments
if ($LASTEXITCODE -ne 0) { throw "Bootloader flash failed ($LASTEXITCODE)" }
