# Smart IV OTA rescue

Use this once for a device running firmware v2 that reports `ABORT (0x95)`
before receiving the first OTA block. That device does not have a working
internal-storage bootloader, so Zigbee OTA cannot repair it by itself.

1. Connect the target BRD2709A / EFR32MG26 board to this PC through its J-Link
   USB port.
2. Open PowerShell and run:

```powershell
& 'C:\Users\khanh\.silabs\slt\installs\archive\Simplicity Commander\commander.exe' flash `
  'C:\Users\khanh\Desktop\web\server\firmware-rescue\SmartIV-v3-bootloader-rescue.s37' `
  --device EFR32MG26B510F3200IM48
```

The combined image installs both the 1,588 KiB internal-storage bootloader and
the small v3 bridge application. It does not intentionally erase Zigbee/NVM3
tokens. Once v3 rejoins the network, use the HIS Devices page to install v8;
all later upgrades can use Zigbee OTA normally.

`SmartIV-v3-bootloader-rescue.ota` is also provided as a valid Zigbee OTA
container, but it is 450,342 bytes. Firmware v2 can store at most 260,096 bytes
and currently fails before the first block, so that file cannot rescue a v2
device over Zigbee. Do not add it beside the normal v3 bridge in `ota-images`;
use the `.s37` file over J-Link for the one-time recovery.
