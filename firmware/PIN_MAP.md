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
| Yellow LED | Output | PA04 |
| Red LED | Output | PA05 |
| Buzzer | Output, mikroBUS RST | PC06 |
| Tare button | BTN0 | PB00 |

All external modules use 3.3 V logic and share GND with the board.

Current test stage: drop sensor retained and HX711/loadcell under test. All
other application peripherals are inactive until their individual test stage.
