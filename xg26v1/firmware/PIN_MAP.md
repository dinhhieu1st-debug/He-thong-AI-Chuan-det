# BRD2709A / EFR32xG26 pin map

This pin map is fixed while the firmware is rebuilt and tested one peripheral
at a time.

| Peripheral | Signal | Pin |
|---|---|---|
| Drop sensor | D0/OUT | PD02 |
| HX711 | DOUT | PC01 |
| HX711 | SCK | PC03 |
| MAX30102 + OLED | I2C SCL | PC05 |
| MAX30102 + OLED | I2C SDA | PC07 |
| Green LED | Output | PA07 |
| Yellow LED | Output, mikroBUS MOSI | PC02 |
| Red LED | Output, mikroBUS INT | PC00 |
| Buzzer | Output, mikroBUS RST | PC06 |
| Tare button | BTN0 | PB00 |
| ESP32-S3 UART | TX (EUSART1), mikroBUS TX | PA04 |
| ESP32-S3 UART | RX (EUSART1), mikroBUS RX | PA05 |

All external modules use 3.3 V logic and share GND with the board.

The ESP32-S3 link is wired crossed: xG26 TX (PA04) -> ESP32 RX, xG26 RX
(PA05) <- ESP32 TX, GND common. UART is 115200 8N1, no flow control, on
EUSART1 - independent from the EUSART0/PB02-PB03 VCOM link used by the CLI.

The integrated application uses all peripherals above. The buzzer driver is
active-high: PC06 HIGH sounds the buzzer and PC06 LOW is silent.
