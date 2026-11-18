# PowerRAID Embedded Controller Firmware (`firmware/`)

This directory contains the C++ firmware for the onboard **ESP32-S3-WROOM-1** microcontroller on the PowerRAID V2 backplane. Built with [PlatformIO](https://platformio.org/) using the Arduino-ESP32 framework.

---

## Features

- **Interleaved I2C Telemetry:** Decodes Lite-On PS-2122-3H / PS-2751 proprietary registers across 3 PSU slots with safe 80 ms request spacing.
- **Dynamic Active-Count Policy:** Automatically selects optimal active PSU count (1, 2, or 3) to minimize wall AC loss based on calibrated crossover thresholds.
- **Redundancy Management:** Configurable N+1 or N+2 failover with real-time health supervision and fault ride-through.
- **Web API & SSE Telemetry:** REST API endpoints and Server-Sent Events (SSE) live data stream at `/api/v1/monitor/stream`.
- **Embedded Web UI (`pramon`):** Serves the pre-compiled Svelte frontend directly from flash.
- **OTA Updates:** Web-based firmware updating via browser upload or `curl`.
- **WiFi Provisioning:** Simple USB serial console setup (`wifi connect "SSID" "password"`).

---

## Hardware Pinout (PowerRAID V2 Backplane)

| Signal | PSU 1 GPIO | PSU 2 GPIO | PSU 3 GPIO | Description |
| :--- | :---: | :---: | :---: | :--- |
| `PRESENT#` | 9 | 12 | 41 | Card-edge presence detection (active LOW) |
| `PSOK` | 10 | 13 | 42 | Module DC power good status (active HIGH) |
| `PSON#` | 11 | 39 | 38 | Main 12V output enable (active LOW open-drain) |

Shared system signals:
- **`PS_INTERRUPT`**: GPIO 40 (PSU fault alert)
- **`I2C_ENABLE`**: GPIO 21 (TXS0102 level-shifter enable)
- **`SDA / SCL`**: GPIO 47 / 48 (5V I2C bus to server PSUs)
- **Default I2C Addresses:** PSU 1 (`0x58`), PSU 2 (`0x59`), PSU 3 (`0x5A`)

---

## Building from Source

### Prerequisites
- [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/installation/index.html) or VSCode with PlatformIO extension.
- (Optional) [Bun](https://bun.sh/) if modifying the `pramon` web UI frontend (pre-compiled assets are already included in `src/generated/`).

### Compilation
```sh
# Compile firmware image (env: powerraid_firmware)
pio run

# Flash device via USB-C (hold BOOT1 button while connecting)
pio run -t upload

# Open serial console monitor (115200 baud)
pio device monitor
```

---

## Flashing Pre-Compiled Release Binaries

Pre-compiled binary releases (`firmware.bin`, `bootloader.bin`, `partitions.bin`) are available in `archives/firmware-0.8.3.zip` (and `archives/firmware.zip`). Flash via `esptool`:

```sh
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 921600 write_flash \
  0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

---

## Over-The-Air (OTA) Updates

Once running on WiFi, updates can be flashed over the local network without cables:
```sh
curl -u admin:password \
  -F 'firmware=@.pio/build/powerraid_firmware/firmware.bin' \
  http://<powerraid-ip>/api/v1/ota
```

Licensed under Apache-2.0.
