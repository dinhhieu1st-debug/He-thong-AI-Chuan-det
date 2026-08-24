# Smart IV OTA

The original v2 application advertises only 256 KiB of OTA storage even though
the bootloader slot is 1,589,248 bytes.  A full application image is therefore
rejected by the device before its first image-block request, which leaves the
web progress bar at 0%.

The library contains a two-stage upgrade for manufacturer `0x1049`, image type
`0`:

1. `smart-iv-ota-bridge-v3.ota` is 220,010 bytes and is offered only to firmware
   v2 and older.  It corrects the OTA storage limit.
2. `smart-iv-monitor-v7.ota` is the full application and is offered only after
   the device reports firmware v3 or newer.

The version restrictions live in adjacent `.policy.json` files.  They are
emitted as `minFileVersion` and `maxFileVersion` in `/api/ota/index.json`, which
Zigbee2MQTT applies against the version reported by each device.  This makes it
safe to keep both images in the library at the same time.

`smart-iv-monitor-v7.ota.pending` is intentional.  The currently running old
server ignores it.  On the next manual server start, the new server promotes it
to `.ota` only after the policy-aware code is loaded.

For a device still on v2, run **Check for update → Update** once to install the
bridge.  After it reboots on v3, run **Check for update → Update** a second time
to install the full v7 application.  Keep the device powered during both steps.
