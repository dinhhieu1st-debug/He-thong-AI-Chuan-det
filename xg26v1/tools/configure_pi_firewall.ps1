$ErrorActionPreference = 'Stop'
$subnet = '192.168.137.0/24'
$rules = @(
  @{ Name = 'Smart IV HIS TCP 5000 (Pi hotspot)'; Port = 5000 },
  @{ Name = 'Smart IV HIS Web 5194 (Pi hotspot)'; Port = 5194 }
)

foreach ($rule in $rules) {
  Get-NetFirewallRule -DisplayName $rule.Name -ErrorAction SilentlyContinue |
    Remove-NetFirewallRule
  New-NetFirewallRule -DisplayName $rule.Name -Direction Inbound -Action Allow `
    -Protocol TCP -LocalPort $rule.Port -RemoteAddress $subnet -Profile Any `
    -EdgeTraversalPolicy Allow | Out-Null
}

$rules | ForEach-Object {
  Get-NetFirewallRule -DisplayName $_.Name |
    Select-Object DisplayName, Enabled, Profile, Direction, Action
} | Format-Table -AutoSize
