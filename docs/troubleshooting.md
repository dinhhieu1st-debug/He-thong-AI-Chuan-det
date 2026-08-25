# Troubleshooting

### `SocketException (10048)` or the HisServer file is locked

```powershell
Get-NetTCPConnection -State Listen -LocalPort 5000,5194 |
  Select-Object LocalPort,OwningProcess
Stop-Process -Id <PID>
```

### Sign-in fails with `Sign-in failed`

- Check that MySQL is running.
- Check that the schema and account migrations have been loaded.
- Run `dotnet user-secrets list` from the correct `HisServer` directory (`software/server/src/HisServer`).
- Restart the HIS Server after MySQL is ready.

### BED-01 shows `OFFLINE`

- Check that G26 has power and has joined the Zigbee network.
- Check that Zigbee2MQTT is receiving fresh payloads.
- Check the gateway and that TCP 5000 is `ESTABLISHED`.
- If using a tunnel, check that `plink` is still running.

### Zigbee2MQTT reports `Failed to init port`

Another process is holding the coordinator's port:

```bash
sudo systemctl stop zigbee2mqtt.service gateway.service 2>/dev/null || true
sudo systemctl restart smart-iv-stack.service
```

### HR/SpO2 shows `--` or the chart has gaps

- Keep the finger steady, avoid pressing too hard, and avoid ambient light.
- Check 3.3 V, GND, SDA on PC07, SCL on PC05, and the pull-up resistors.
- Keep the buzzer wire away from the I2C wires.
- Lifting the finger correctly reports "no signal" — this is expected.
- Placing the finger back needs about two seconds for the filter window to fill with samples.

### The rate-setting or fake-data buttons don't reach G26

This is a two-way command: Server → gateway → Zigbee. Check the gateway
TCP connection, MQTT, that the Zigbee device is online, and that the
converter is the right version. The `enabled` toast only confirms the
server received the request; a change in the `AI test HR/SpO2` field is
what confirms the command actually reached G26.

### An unexpected folder (e.g. `demo1/`) appears at the repo root and `git status` reports it as untracked

This is usually leftover from running `dotnet run` with an old path
(from before `server/` was renamed to `software/server/`). Check that no
`dotnet`/`HisServer` process is still running from that folder
(`ps aux | grep dotnet`), stop it, delete the folder, and keep working from
`software/server/`.
