[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [uint32]$Version,

    [string]$HeaderString = "ICTU SmartIV XG26 v$Version",

    # Real values for this product, from the project's own ZCL config
    # (config/zcl/smart-iv-vitals.xml, config/zcl/zcl_config.zap: manufacturerCode
    # 0x1049) and config/ota-client-policy-config.h
    # (SL_ZIGBEE_AF_PLUGIN_OTA_CLIENT_POLICY_IMAGE_TYPE_ID). Override only if
    # those change.
    [string]$ManufacturerId = '0x1049',
    [string]$ImageType = '0',

    [string]$Device = 'EFR32MG26B510F3200IM48',
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $projectRoot 'cmake_gcc\build\base' }

$commander = 'C:\Users\khanh\.silabs\slt\installs\archive\Simplicity Commander\commander.exe'
if (-not (Test-Path -LiteralPath $commander)) { throw "Commander not found at $commander" }

$python = 'python'

# 1-3: build firmware, fail fast on any error (build_firmware.ps1 throws on failure).
Write-Host "[1/5] Building firmware..."
& (Join-Path $PSScriptRoot 'build_firmware.ps1')

$appImage = Join-Path $OutDir 'smart-iv-monitor.s37'
if (-not (Test-Path -LiteralPath $appImage)) { throw "Expected build output not found: $appImage" }

$gblOut = Join-Path $OutDir "smart-iv-monitor-v$Version.gbl"
$otaOut = Join-Path $OutDir "smart-iv-monitor-v$Version.ota"

# 4: GBL from the freshly built application image only (no bootloader bundled -
# this is a normal application-cluster OTA, the internal-storage bootloader
# already on the device is not touched).
Write-Host "[2/5] Creating GBL: $gblOut"
& $commander gbl create $gblOut --app $appImage -d $Device
if ($LASTEXITCODE -ne 0) { throw "commander gbl create failed ($LASTEXITCODE)" }

# 5-8: Zigbee OTA wrapper around that GBL.
Write-Host "[3/5] Creating OTA file: $otaOut"
& $commander ota create -o $otaOut `
    --upgrade-image $gblOut `
    --firmware-version $Version `
    --manufacturer-id $ManufacturerId `
    --image-type $ImageType `
    --string $HeaderString
if ($LASTEXITCODE -ne 0) { throw "commander ota create failed ($LASTEXITCODE)" }

# 9-10: self-verify - structural GBL parse must succeed, and the OTA header's
# own totalImageSize must equal the file actually written.
Write-Host "[4/5] Verifying GBL structure via Commander..."
$verifyAppOut = Join-Path $OutDir "verify-v$Version-app.bin"
& $commander gbl parse $gblOut --app $verifyAppOut
if ($LASTEXITCODE -ne 0) { throw "commander gbl parse reported the GBL as invalid ($LASTEXITCODE)" }
Remove-Item -LiteralPath $verifyAppOut -ErrorAction SilentlyContinue

Write-Host "[5/5] Verifying OTA header fields..."
$verifyScript = Join-Path $PSScriptRoot 'verify_ota_header.py'
& $python $verifyScript $otaOut $ManufacturerId $ImageType $Version
if ($LASTEXITCODE -ne 0) { throw "OTA header self-check FAILED - not safe to upload." }

$hash = (Get-FileHash -LiteralPath $otaOut -Algorithm SHA256).Hash
Write-Host ""
Write-Host "OTA file ready: $otaOut"
Write-Host "SHA256: $hash"
