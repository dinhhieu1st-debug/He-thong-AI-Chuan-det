param(
    [switch]$CheckOnly,
    [string]$ServerHost
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Workspace = 'E:\SmartIV_MQTT'
$MosquittoExe = 'C:\Program Files\mosquitto\mosquitto.exe'
$MosquittoSub = 'C:\Program Files\mosquitto\mosquitto_sub.exe'
$MosquittoConfig = Join-Path $Workspace 'config\mosquitto.conf'
$Zigbee2MqttScript = Join-Path $Workspace 'start_zigbee2mqtt.ps1'
$Zigbee2MqttConfig = Join-Path $Workspace 'zigbee2mqtt\data\configuration.yaml'
$Gateway = Join-Path $ProjectRoot 'bin\windows\gateway.exe'
$GatewayConfig = Join-Path $ProjectRoot 'config\gateway.conf'
$LogDirectory = Join-Path $Workspace 'logs'

function Test-TcpPort([string]$HostName, [int]$Port) {
    $Client = New-Object System.Net.Sockets.TcpClient
    try {
        $Result = $Client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $Result.AsyncWaitHandle.WaitOne(500)) {
            return $false
        }
        $Client.EndConnect($Result)
        return $true
    }
    catch {
        return $false
    }
    finally {
        $Client.Close()
    }
}

function Get-Zigbee2MqttState {
    try {
        $Output = & $MosquittoSub -h 127.0.0.1 -p 1885 -t 'zigbee2mqtt/bridge/state' -C 1 -W 2 2>&1
        if ($LASTEXITCODE -eq 0) {
            return ($Output -join '')
        }
    }
    catch {
        # A timeout only means Zigbee2MQTT is not online yet.
    }
    return ''
}

function Get-CoordinatorPort([string]$ConfigPath) {
    foreach ($Line in Get-Content -LiteralPath $ConfigPath) {
        if ($Line -match '^\s*port:\s*([^#]+)') {
            return $Matches[1].Trim().Trim([char]39).Trim([char]34)
        }
    }
    return 'khong-xac-dinh'
}

function Stop-ZigbeeServices {
    Write-Host ''
    Write-Host '[STOP] Dang dung Zigbee2MQTT...'

    $ZigbeeProcessIds = @()
    $FrontendListeners = Get-NetTCPConnection -State Listen -LocalPort 8080 -ErrorAction SilentlyContinue
    if ($FrontendListeners) {
        $ZigbeeProcessIds += $FrontendListeners.OwningProcess
    }

    $ZigbeeWrappers = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.CommandLine -and $_.CommandLine.Contains($Zigbee2MqttScript)
    }
    if ($ZigbeeWrappers) {
        $ZigbeeProcessIds += $ZigbeeWrappers.ProcessId
    }

    foreach ($ProcessId in ($ZigbeeProcessIds | Sort-Object -Unique)) {
        if ($ProcessId -and $ProcessId -ne $PID) {
            try {
                # Killing one process tree can also remove another PID in this list.
                & taskkill.exe /PID $ProcessId /T /F 2>&1 | Out-Null
            }
            catch {
                # The service is already stopped, which is the desired state.
            }
        }
    }

    for ($Attempt = 0; $Attempt -lt 10; $Attempt++) {
        if (-not (Test-TcpPort '127.0.0.1' 8080)) { break }
        Start-Sleep -Milliseconds 300
    }

    Write-Host '[STOP] Dang dung Mosquitto port 1885...'
    $BrokerListeners = Get-NetTCPConnection -State Listen -LocalPort 1885 -ErrorAction SilentlyContinue
    foreach ($ProcessId in ($BrokerListeners.OwningProcess | Sort-Object -Unique)) {
        if ($ProcessId) {
            try {
                & 'C:\Program Files\mosquitto\mosquitto_signal.exe' -p $ProcessId shutdown 2>&1 | Out-Null
            }
            catch {
                # The listener may exit between discovery and shutdown.
            }
        }
    }

    for ($Attempt = 0; $Attempt -lt 10; $Attempt++) {
        if (-not (Test-TcpPort '127.0.0.1' 1885)) { break }
        Start-Sleep -Milliseconds 300
    }

    if (Test-TcpPort '127.0.0.1' 1885) {
        $BrokerListeners = Get-NetTCPConnection -State Listen -LocalPort 1885 -ErrorAction SilentlyContinue
        foreach ($ProcessId in ($BrokerListeners.OwningProcess | Sort-Object -Unique)) {
            Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
        }
    }

    Write-Host '[STOP] Zigbee2MQTT va Mosquitto da dung.'
}

foreach ($RequiredFile in @($MosquittoExe, $MosquittoSub, $MosquittoConfig, $Zigbee2MqttScript, $Zigbee2MqttConfig, $GatewayConfig)) {
    if (-not (Test-Path -LiteralPath $RequiredFile -PathType Leaf)) {
        throw "Khong tim thay file: $RequiredFile"
    }
}

$CoordinatorPort = Get-CoordinatorPort $Zigbee2MqttConfig

New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null

if (-not (Test-TcpPort '127.0.0.1' 1885)) {
    Write-Host '[1/3] Dang khoi dong Mosquitto port 1885...'
    Start-Process -FilePath $MosquittoExe `
        -ArgumentList @('-c', $MosquittoConfig, '-v') `
        -WorkingDirectory $Workspace `
        -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $LogDirectory 'gateway-mosquitto-stdout.log') `
        -RedirectStandardError (Join-Path $LogDirectory 'gateway-mosquitto-stderr.log') | Out-Null

    $BrokerReady = $false
    for ($Attempt = 0; $Attempt -lt 30; $Attempt++) {
        Start-Sleep -Milliseconds 500
        if (Test-TcpPort '127.0.0.1' 1885) {
            $BrokerReady = $true
            break
        }
    }
    if (-not $BrokerReady) {
        throw 'Mosquitto khong khoi dong duoc tren port 1885.'
    }
} else {
    Write-Host '[1/3] Mosquitto port 1885 dang chay.'
}

$ZigbeeState = Get-Zigbee2MqttState
if ($ZigbeeState -notmatch '"state"\s*:\s*"online"') {
    Write-Host "[2/3] Dang khoi dong Zigbee2MQTT voi coordinator $CoordinatorPort..."
    Start-Process -FilePath 'powershell.exe' `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Zigbee2MqttScript) `
        -WorkingDirectory $Workspace `
        -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $LogDirectory 'gateway-zigbee2mqtt-stdout.log') `
        -RedirectStandardError (Join-Path $LogDirectory 'gateway-zigbee2mqtt-stderr.log') | Out-Null

    $ZigbeeReady = $false
    for ($Attempt = 0; $Attempt -lt 90; $Attempt++) {
        Start-Sleep -Seconds 1
        $ZigbeeState = Get-Zigbee2MqttState
        if ($ZigbeeState -match '"state"\s*:\s*"online"') {
            $ZigbeeReady = $true
            break
        }
    }
    if (-not $ZigbeeReady) {
        Write-Host '--- Zigbee2MQTT stdout ---'
        Get-Content -Tail 30 (Join-Path $LogDirectory 'gateway-zigbee2mqtt-stdout.log') -ErrorAction SilentlyContinue
        Write-Host '--- Zigbee2MQTT stderr ---'
        Get-Content -Tail 30 (Join-Path $LogDirectory 'gateway-zigbee2mqtt-stderr.log') -ErrorAction SilentlyContinue
        throw "Zigbee2MQTT chua online. Kiem tra $CoordinatorPort co bi Commander chiem dung khong."
    }
} else {
    Write-Host '[2/3] Zigbee2MQTT dang online.'
}

Write-Host "      MQTT bridge state: $ZigbeeState"

if ($CheckOnly) {
    Write-Host '[3/3] Kiem tra hoan tat; khong chay gateway.'
    exit 0
}

if (-not (Test-Path -LiteralPath $Gateway -PathType Leaf)) {
    Write-Host '[3/3] Chua co gateway.exe, dang build...'
    & (Join-Path $PSScriptRoot 'build.cmd')
    if ($LASTEXITCODE -ne 0) {
        throw 'Build gateway.exe that bai.'
    }
}

Write-Host '[3/3] Dang chay gateway Zigbee -> MQTT -> HIS server...'
Write-Host '      Nhan Ctrl+C de dung gateway, Zigbee2MQTT va Mosquitto.'
$env:PATH = "C:\Program Files\mosquitto;$env:PATH"
$env:GATEWAY_CONFIG = $GatewayConfig
$GatewayArguments = @()
if ($ServerHost) {
    $GatewayArguments += @('--server', $ServerHost)
}
$GatewayExitCode = 0
try {
    & $Gateway @GatewayArguments
    $GatewayExitCode = $LASTEXITCODE
}
finally {
    Stop-ZigbeeServices
}
exit $GatewayExitCode
