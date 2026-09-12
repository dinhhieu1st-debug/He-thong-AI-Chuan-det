[CmdletBinding()]
param(
    [ValidateSet('Start', 'Stop', 'Status')]
    [string]$Action = 'Start',
    [string]$PiIp = '192.168.137.52',
    [string]$PiUser = 'iotchallenge',
    [string]$PiPassword = '123',
    [string]$PiHostKey = 'SHA256:LMfiR+UyuVTLJe99kB8dipYuZdAoKXK4RulRsRiEUkw'
)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$serverDir = Join-Path $projectRoot 'server\src\HisServer'
$mysqlExe = 'C:\Program Files\MySQL\MySQL Server 8.4\bin\mysqld.exe'
$mysqlData = 'C:\ProgramData\MySQL\MySQL Server 8.4\Data'
$runtimeDir = Join-Path $projectRoot '.runtime'
$pidFile = Join-Path $runtimeDir 'smart-iv-monitor-pids.json'
$script:monitorPids = @()
$plink = (Get-Command plink.exe -ErrorAction SilentlyContinue).Source
if (-not $plink) { $plink = (Get-Command plink -ErrorAction SilentlyContinue).Source }

function Test-ListenPort([int]$Port) {
    return [bool](Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
}

function Wait-ListenPort([int]$Port, [int]$Seconds = 20) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        if (Test-ListenPort $Port) { return $true }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    return $false
}

function Start-MonitorWindow([string]$Title, [string]$Body, [switch]$Administrator) {
    $script = "`$Host.UI.RawUI.WindowTitle='$Title'; $Body"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))
    $args = @('-NoExit', '-NoProfile', '-EncodedCommand', $encoded)
    if ($Administrator) {
        $process = Start-Process powershell.exe -Verb RunAs -ArgumentList $args -PassThru
    } else {
        $process = Start-Process powershell.exe -ArgumentList $args -PassThru
    }
    $script:monitorPids += $process.Id
    New-Item -ItemType Directory -Path $runtimeDir -Force | Out-Null
    $script:monitorPids | ConvertTo-Json | Set-Content -LiteralPath $pidFile -Encoding ASCII
}

function Invoke-Pi([string]$Command) {
    if (-not $plink) { throw 'PuTTY plink.exe was not found.' }
    & $plink -batch -ssh -pw $PiPassword -hostkey $PiHostKey "$PiUser@$PiIp" $Command
    if ($LASTEXITCODE -ne 0) { throw "Pi command failed ($LASTEXITCODE)." }
}

function Stop-LocalPort([int]$Port) {
    $ids = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty OwningProcess -Unique
    foreach ($processId in $ids) {
        try {
            Stop-Process -Id $processId -Force -ErrorAction Stop
        } catch {
            Start-Process taskkill.exe -Verb RunAs -ArgumentList '/PID', $processId, '/F' -Wait | Out-Null
        }
    }
}

function Stop-System {
    $monitorIds = @()
    if (Test-Path -LiteralPath $pidFile) {
        $monitorIds = @((Get-Content -Raw -LiteralPath $pidFile | ConvertFrom-Json))
    }
    $managedChildren = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object {
            ($_.Name -like 'plink*' -and $_.CommandLine -match [regex]::Escape($PiIp)) -or
            ($_.ProcessId -in @(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
                Where-Object { $_.LocalPort -in 3000, 5000, 3306 } |
                Select-Object -ExpandProperty OwningProcess -Unique))
        }
    $monitorIds += @($managedChildren | Select-Object -ExpandProperty ParentProcessId -Unique)

    Write-Host '[1/3] Stopping Smart IV processes on Pi...'
    try {
        Invoke-Pi "pkill -TERM -x gateway 2>/dev/null || true; pkill -TERM -x node 2>/dev/null || true; pkill -TERM -x mosquitto 2>/dev/null || true; printf '$PiPassword\n' | sudo -S systemctl stop mosquitto 2>/dev/null || true; sleep 2; true"
    } catch {
        Write-Warning "Pi could not be reached: $($_.Exception.Message)"
    }

    Write-Host '[2/3] Stopping HIS Server, MySQL and SSH tunnels...'
    Stop-LocalPort 3000
    Stop-LocalPort 5000
    Stop-LocalPort 3306
    Get-Process plink -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

    Write-Host '[3/3] Closing Smart IV monitor terminals...'
    $monitorIds | Where-Object { $_ -and $_ -ne $PID } | Select-Object -Unique |
        ForEach-Object { Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
    Write-Host 'Smart IV system stopped.' -ForegroundColor Green
}

function Show-Status {
    Write-Host 'Windows services:' -ForegroundColor Cyan
    foreach ($port in 3000, 5000, 3306) {
        $state = if (Test-ListenPort $port) { 'LISTENING' } else { 'STOPPED' }
        Write-Host ("  Port {0}: {1}" -f $port, $state)
    }
    $connection = Get-NetTCPConnection -LocalPort 5000 -State Established -ErrorAction SilentlyContinue
    Write-Host ("  Pi -> HIS: {0}" -f $(if ($connection) { 'CONNECTED' } else { 'DISCONNECTED' }))

    Write-Host 'Pi services:' -ForegroundColor Cyan
    try {
        Invoke-Pi "echo IP:; ip -br -4 addr show wlan0; echo PROCESSES:; pgrep -af '/home/iotchallenge/pi-aarch64/bin/gateway|/home/iotchallenge/pi-aarch64/zigbee2mqtt/index.js|/home/iotchallenge/pi-aarch64/bin/mosquitto' || true; echo PORTS:; ss -ltn | grep -E ':(1885|5000|8080) ' || true"
    } catch {
        Write-Warning $_.Exception.Message
    }
}

function Start-System {
    if (-not (Test-Path -LiteralPath $serverDir)) { throw "HIS Server directory not found: $serverDir" }
    if (-not (Test-Path -LiteralPath $mysqlExe)) { throw "MySQL not found: $mysqlExe" }
    if (-not $plink) { throw 'PuTTY plink.exe was not found.' }

    Write-Host '[0/5] Checking Pi network...' -ForegroundColor Cyan
    if (-not (Test-Connection -ComputerName $PiIp -Count 1 -Quiet -ErrorAction SilentlyContinue)) {
        Start-Process 'ms-settings:network-mobilehotspot'
        throw "Pi $PiIp is unreachable. Turn on Windows Mobile Hotspot and wait until the Pi appears as a connected device, then run this script again."
    }
    Invoke-Pi 'echo PI_SSH_OK'

    Write-Host '[1/5] Starting MySQL...' -ForegroundColor Cyan
    if (-not (Test-ListenPort 3306)) {
        $body = "& '$mysqlExe' --datadir='$mysqlData' --console"
        Start-MonitorWindow 'Smart IV - MySQL' $body -Administrator
        if (-not (Wait-ListenPort 3306 25)) { throw 'MySQL did not open port 3306.' }
    }

    Write-Host '[2/5] Starting HIS Server...' -ForegroundColor Cyan
    if (-not (Test-ListenPort 5000)) {
        $body = "Set-Location -LiteralPath '$serverDir'; dotnet run --launch-profile http"
        Start-MonitorWindow 'Smart IV - HIS Server' $body
        if (-not (Wait-ListenPort 5000 35)) { throw 'HIS Server did not open port 5000.' }
    }

    Write-Host '[3/5] Opening reverse SSH tunnel...' -ForegroundColor Cyan
    $oldTunnel = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like 'plink*' -and $_.CommandLine -match '\-R\s+5000:127\.0\.0\.1:5000' }
    if (-not $oldTunnel) {
        $body = "& '$plink' -batch -N -ssh -pw '$PiPassword' -hostkey '$PiHostKey' -R 5000:127.0.0.1:5000 '$PiUser@$PiIp'"
        Start-MonitorWindow 'Smart IV - SSH Tunnel' $body
        Start-Sleep -Seconds 5
    }
    Invoke-Pi "timeout 5 bash -c '</dev/tcp/127.0.0.1/5000' >/dev/null 2>&1"

    Write-Host '[4/5] Starting Pi gateway stack...' -ForegroundColor Cyan
    $body = "& '$plink' -batch -ssh -pw '$PiPassword' -hostkey '$PiHostKey' '$PiUser@$PiIp' 'cd /home/iotchallenge/pi-aarch64 && bash run.sh 127.0.0.1'"
    Start-MonitorWindow 'Smart IV - Pi Gateway' $body
    Start-Sleep -Seconds 15

    Write-Host '[5/5] Opening dashboards...' -ForegroundColor Cyan
    Start-Process 'http://localhost:3000'
    Start-Process "http://$PiIp`:8080"
    Show-Status
}

switch ($Action) {
    'Start'  { Start-System }
    'Stop'   { Stop-System }
    'Status' { Show-Status }
}
