[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$gui = Join-Path $projectRoot 'host_ai\inference\desktop_gui.py'
python $gui
