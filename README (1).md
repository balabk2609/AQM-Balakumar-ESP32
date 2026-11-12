# ESP32 Air-Sensor Firmware with BLE Interface

## Track chosen
**Track B** (no physical board): built and tested in the Wokwi simulator.
Wokwi project: `https://wokwi.com/projects/480451493557285889`

BLE GATT code is fully written but disabled with `#define ENABLE_BLE 0` because Wokwi cannot simulate BLE. Set it to `1` to flash onto a real ESP32.

## Board, sensor and wiring
- Board: ESP32 DevKit (Arduino framework, Wokwi "ESP32" template)
- Sensor: MPU6050 (I2C address `0x68`), temperature read directly from registers with the `Wire` library (no sensor library)
- Humidity: simulated in `readSimulatedHumidity()`

| ESP32 | MPU6050 |
|-------|---------|
| 3V3 | VCC |
| GND | GND |
| GPIO21 | SDA |
| GPIO22 | SCL |

## How to build and run
**Wokwi:** open the project link, press Play, use the Serial Monitor.
**Real board:** open `aqm_firmware.ino` in Arduino IDE (ESP32 board package installed), set `ENABLE_BLE` to `1`, select the board and port, upload.

Serial commands (mirror the BLE characteristics):
- `GET TEMP` prints filtered temperature
- `GET HUM` prints filtered humidity
- `SET INTERVAL <seconds>` changes the sampling interval (1 to 3600 s)

Readings also print automatically every interval (stand-in for BLE notifications).

## How it works
- Samples every 2 s by default (changeable at runtime), using non-blocking `millis()` timing.
- Moving average over the last 5 samples (ring buffer) for both temperature and humidity.
- Temperature: `raw / 340.0 + 36.53` (MPU6050 datasheet formula).

## BLE service (device name `AQM-<FirstName>`)

| Item | UUID | Properties | Format |
|------|------|-----------|--------|
| Service | `6e1a0001-b5a3-f393-e0a9-e50e24dcca9e` | n/a | n/a |
| Temperature | `6e1a0002-b5a3-f393-e0a9-e50e24dcca9e` | Read, Notify | ASCII text, e.g. `24.53` (°C) |
| Humidity | `6e1a0003-b5a3-f393-e0a9-e50e24dcca9e` | Read, Notify | ASCII text, e.g. `55.10` (%) |
| Sampling interval | `6e1a0004-b5a3-f393-e0a9-e50e24dcca9e` | Read, Write | uint8 or uint16 little-endian, seconds |

## How it was tested
- Wokwi simulation: readings print each interval; changing the MPU6050 temperature in the simulator moves the filtered value gradually (moving average).
- Serial commands `GET TEMP`, `GET HUM` and `SET INTERVAL 5` verified in the Serial Monitor (screenshot attached).
- BLE code was **not** tested on hardware.

## Known issues / limitations
- BLE is untested because I had no ESP32 board; it will be tested at the review session.
- Humidity is simulated, not measured.
- The MPU6050 temperature is die temperature, so it can read slightly above ambient.
- Interval values outside 1 to 3600 s are rejected.

## AI usage note
I used Claude to help generate the firmware, the Wokwi wiring steps and this README. I asked it for an ESP32 Arduino sketch meeting the task requirements (MPU6050 over I2C, simulated humidity, 5-sample moving average, BLE GATT service behind a compile switch, Serial command interface). I then ran it in Wokwi, checked the wiring, tested the commands, and reviewed the code so I can explain it. Changes and checks I made: set `DEVICE_NAME` to my own name, kept `ENABLE_BLE` at 0 for Wokwi, wired the MPU6050 to GPIO21/22 myself, tested `GET TEMP`, `GET HUM` and `SET INTERVAL` in the Serial Monitor, and checked that the moving average smooths temperature changes. I reviewed the code so I can explain it, including the BLE section that I could not test without a board.
