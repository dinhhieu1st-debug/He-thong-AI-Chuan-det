[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [int]$Baud = 115200
)

$ErrorActionPreference = 'Stop'
$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, 'None', 8, 'One')
$serial.Handshake = 'None'
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 250

try {
    $serial.Open()
    Write-Host "Serial monitor opened: $Port @ $Baud baud"
    Write-Host 'Press the RESET button on the board. Press Ctrl+C to close.'
    Write-Host ''

    while ($true) {
        $text = $serial.ReadExisting()
        if ($text.Length -gt 0) {
            [Console]::Write($text)
        }
        Start-Sleep -Milliseconds 20
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}
