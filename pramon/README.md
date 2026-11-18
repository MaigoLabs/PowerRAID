# pramon

Browser control UI for the PowerRAID LiteOn PSU bridge.

## Run

```sh
bun install
bun run dev -- --host 0.0.0.0
```

Open the Vite URL from a browser that can reach the bridge. For the ESP32 Web
API backend, enter the bridge API base URL in the connection card:

```text
http://powerraid-72E0.local/api/v1
http://10.0.1.239/api/v1
```

You can also prefill the field with a query string:

```text
http://localhost:5173/?api=http://10.0.1.239/api/v1
```

The app stores the last successful Web API URL in browser local storage.

## Password Protection

The bridge can be left open or protected with a password. The UI mirrors that
state:

- when a password is set, connecting shows an unlock form instead of the
  dashboard; reads and switches stay disabled until it succeeds
- with "telemetry viewable without a password" enabled, readings load for
  anyone while every switch and setting stays disabled
- on an open bridge, the first visit offers to set a password; skipping it is
  remembered per browser in `pramon.passwordPromptShown`
- the Security card changes the password, removes it, signs out, and toggles
  the telemetry policy

The password never reaches the bridge. The browser derives an SRP verifier with
PBKDF2-HMAC-SHA256 and registers only `v = g^x mod N`, then logs in with SRP-6a
over the RFC 5054 2048-bit group. `crypto.subtle` is unavailable on the plain
HTTP LAN origin the bridge serves, so `src/lib/psu/sha256.ts` implements
SHA-256 and PBKDF2 in JavaScript and both paths are verified against the
firmware and against Python's `hashlib`.

## Backends

`Web API` is the default backend for the ESP32 firmware. It uses typed HTTP
endpoints for status, slot enumeration, probing, register reads and writes,
MCU-side power policy, PSON, WiFi, InfluxDB reporting, cadence, and reset
actions. The live register monitor configures `/api/v1/monitor` and subscribes
to the firmware JSON event stream on `:81/api/v1/monitor/events`, feeding the
same UI update path used by WebSerial.

`WebSerial` remains available for the STM32 proof-of-concept bridge and for raw
serial debugging. Raw `!` commands are intentionally not exposed through the Web
API backend; use the firmware HTTP endpoints for app functionality and OTA for
ESP32 firmware updates.

## Build

```sh
bun run check
bun run build
```
