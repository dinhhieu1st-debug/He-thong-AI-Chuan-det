# System architecture

## Data flow

```text
MAX30102 + photodiode + HX711
              |
              v
       EFR32xG26 (firmware + AI + OLED + alerts)
              |
              | Zigbee ZCL Attribute
              v
    Zigbee coordinator + Zigbee2MQTT on Raspberry Pi
              |
              | internal MQTT JSON
              v
        TCP gateway on Raspberry Pi
              |
              | TCP 5000 (direct LAN or SSH reverse tunnel)
              v
      HIS Server .NET 8 + MySQL + web UI on 5194
```

## Hardware and G26 pinout

The board in use is the EFR32xG26/BRD2709A. The full reference lives in
[`firmware/PIN_MAP.md`](../firmware/PIN_MAP.md); summary below:

| Block | Signal | G26 pin |
|---|---|---|
| Drop sensor | Digital OUT | PD02 |
| HX711 | DOUT | PC01 |
| HX711 | SCK | PC03 |
| MAX30102 and OLED | SCL | PC05 |
| MAX30102 and OLED | SDA | PC07 |
| Green LED | OUT | PA07 |
| Yellow LED | OUT | PA04 |
| Red LED | OUT | PA05 |
| Active-low buzzer | OUT | PC06 |
| Tare button | INPUT | PB00 |

All modules use 3.3 V logic and share a common GND. Since the OLED and
MAX30102 share the same I2C bus, the team recommends roughly `4.7 kΩ`
pull-up resistors, short wiring, and keeping the I2C lines away from the
buzzer.

## G26 Zigbee protocol

G26 **does not send JSON directly**. The firmware writes data into ZCL
Attributes and reports them over Zigbee:

- Endpoint: `2`
- Cluster ID: `0xFC01`
- Manufacturer Code: `0x1049`
- Model: `SmartIV-Sensor`
- Cluster name: `Smart IV Vitals`

| Attribute | Name | Type | Unit |
|---:|---|---|---|
| `0x0000` | HeartRate | int16s | bpm |
| `0x0001` | Spo2 | int16u | % |
| `0x0002` | FlowRatio | int16u | % |
| `0x0003` | DropRatio | int16s | % |
| `0x0004` | AlarmBitmap | bitmap16 | bit |
| `0x0005` | WeightG | int16u | gram |
| `0x0006` | DropsPerMin | int16u | drops/min |
| `0x0007` | TargetFlowMlH | int16u | ml/h |
| `0x0008` | TargetDropsPerMin | int16u | drops/min |
| `0x000F` | TsFlags | bitmap16 | bit |
| `0x0010` | HrForecast16s | int16u | bpm |
| `0x0011` | Spo2Forecast16s | int16u | % |
| `0x0012` | HrTrendBpmPerMin | int16s | bpm/min |
| `0x0013` | TsAnomalyScoreX100 | int16u | score ×100 |
| `0x0014` | DropsForecast16s | int16u | drops/min |
| `0x0015` | DropsTrendDpmPerMin | int16s | dpm/min |
| `0x0016` | RemainingMl | int16u | ml |
| `0x0017` | RemainingMin | int16u | min |
| `0x0018` | MonitoringActive | int8u | 0/1 |

The Zigbee2MQTT converter turns these attributes into MQTT JSON (see
[`docs/system-integration.md`](system-integration.md) for where the
converter file currently lives, since it is missing from the repo). The
gateway forwards that JSON to the server and relays commands from the
server back to the correct attribute on endpoint 2.

## Operating sequence

1. G26 boots and tares the loadcell for 10 seconds. Do not hang the bag yet.
2. The OLED prompts the user to open the HIS web UI to set the target drip rate.
3. Sensors are still read and streamed to the web so the connection can be checked.
4. Hang the bag, enter the target rate on the web UI, and confirm.
5. The command travels Server → TCP gateway → MQTT → Zigbee2MQTT → ZCL → G26.
6. G26 starts collecting 20 drip-interval samples and 64 vitals samples.
7. Once enough data is collected, AI and alerting are activated.
8. The final alert level is reported back to the server together with its cause.

## HR and SpO2 processing

The sensor is read every `250 ms`. Four reads make up one update cycle, so
the AI receives exactly `1 sample/second`, and 64 samples correspond to
roughly 64 seconds.

To keep HR from jumping around, the firmware currently uses:

- A 12-reading sliding window, requiring at least 8 valid reads.
- A median filter to reject outlier pulses.
- An HR deviation greater than 20 BPM must repeat three times before it is accepted.
- The filtered HR changes by at most 4 BPM per second.
- SpO2 has its own filter window, deviation confirmation, and step-size limit.
- Losing signal for more than 3 seconds returns a "no data" state and clears the old window.
- Values held for display purposes are not counted as new AI samples.
- Fake mode only feeds the AI test branch; the real sensor values are kept separate for comparison.

Do not speed up the 64-sample warm-up by repeating the same value, since that
would make the model misread the timing of its history, trend, and 16-second
forecast.

## Alert logic

### Drip rate

The firmware tracks a dynamic baseline so that a bag naturally slowing down
over time is tolerated. The baseline only follows small changes that stay
within the safe zone; noise spikes, missed drops, and large deviations are
not learned into it.

- Deviation within `±200 ms`: level 1 – normal.
- Deviation over `200 ms` up to `800 ms`: level 2 – attention.
- Deviation over `800 ms`: level 3 – alarm.
- A watchdog detects an excessively long gap with no drop and raises the drip branch to level 3.
- The MLP and LSTM use a 20-drop window to analyze trend; the physical range check remains the primary safeguard.

### Vitals

- The first 60 samples build the HR/SpO2 baseline.
- Collection continues to a full 64-sample history for the vitals AI.
- Deviation under 15% from baseline: level 1.
- Deviation from 15% to under 20%: level 2.
- Deviation of 20% or more: level 3.
- Hard thresholds — HR below 45, HR above 150, or SpO2 below 90 — force level 3.

### Final alert fusion

| Vitals | Drip | Final level |
|---:|---:|---:|
| 1 | 1 | 1 |
| 3 | 3 | 3 |
| Any other combination | | 2 |

See the severity mapping shared between firmware and HIS in
[`docs/system-integration.md`](system-integration.md#alert-contract).

### LED and buzzer

- Level 1: green LED, buzzer off.
- Level 2: yellow LED; buzzer beeps for 0.5 s, then rests for 3 s.
- Level 3: red LED blinking; buzzer toggles every 0.25 s.
- Active-low buzzer: PC06 LOW sounds, HIGH is silent.

## Testing from the web UI

The web UI has three vitals test buttons:

- `Real data`: uses the actual sensor.
- `Fake HR L2`: generates an HR deviation of about 17% from baseline.
- `Fake HR+O2 L3`: generates HR and SpO2 deviations of about 25%.

`Real HR/SpO2` is the actual sensor data. `AI test HR/SpO2` is the data fed
into the AI branch during a test. Switching back to `Real data` restores the
real history in the firmware, so it does not need to relearn from scratch.
