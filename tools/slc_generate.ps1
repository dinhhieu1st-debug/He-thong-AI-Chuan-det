[CmdletBinding()]
param(
    [string]$Board = 'brd2709a'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$slt = Join-Path $env:LOCALAPPDATA 'Programs\SiliconLabsTool\slt.exe'

if (-not (Test-Path -LiteralPath $slt)) {
    throw "SLT not found at $slt"
}

$slcRoot = (& $slt where slc-cli).Trim()
$pythonRoot = (& $slt where python).Trim()
$slc = Join-Path $slcRoot 'slc.bat'
$python = Join-Path $pythonRoot 'python.exe'
$zapDb = Join-Path $env:USERPROFILE '.zap\generate.sqlite'

if (Test-Path -LiteralPath $zapDb) {
    $cleanup = @'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
n = db.execute("DELETE FROM PACKAGE WHERE PATH LIKE '%smart-iv-vitals.xml'").rowcount
db.commit()
print(f"ZAP cache rows removed: {n}")
'@
    & $python -c $cleanup $zapDb
    if ($LASTEXITCODE -ne 0) { throw "Could not clean the ZAP cache" }
}

$oldJavaOptions = $env:_JAVA_OPTIONS
$env:_JAVA_OPTIONS = "-Duser.home=$env:USERPROFILE"
Push-Location $projectRoot
try {
    & $slc generate -p smart-iv-monitor.slcp -d . --with $Board --slconf="$PSScriptRoot\user.slconf" --no-daemon
    if ($LASTEXITCODE -ne 0) { throw "SLC generation failed ($LASTEXITCODE)" }

    $required = @(
        'ZCL_TS_FLAGS_ATTRIBUTE_ID',
        'ZCL_HR_FORECAST_16S_ATTRIBUTE_ID',
        'ZCL_SPO2_FORECAST_16S_ATTRIBUTE_ID',
        'ZCL_HR_TREND_BPM_PER_MIN_ATTRIBUTE_ID',
        'ZCL_TS_ANOMALY_SCORE_X100_ATTRIBUTE_ID',
        'ZCL_DROPS_FORECAST_16S_ATTRIBUTE_ID',
        'ZCL_DROPS_TREND_DPM_PER_MIN_ATTRIBUTE_ID',
        'ZCL_REMAINING_ML_ATTRIBUTE_ID',
        'ZCL_REMAINING_MIN_ATTRIBUTE_ID'
    )
    $zapIds = Get-Content -Raw (Join-Path $projectRoot 'autogen\zap-id.h')
    $missing = @($required | Where-Object { $zapIds -notmatch [regex]::Escape($_) })
    if ($missing.Count -gt 0) {
        throw "Generated zap-id.h is missing custom attributes: $($missing -join ', ')"
    }
    Write-Host 'SLC generation complete; all 9 custom ZCL attributes are present.'
} finally {
    $env:_JAVA_OPTIONS = $oldJavaOptions
    Pop-Location
}
