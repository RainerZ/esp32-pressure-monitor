# ESP32 Pressure Monitor

A pressure monitoring node for the ESP32-S3. It reads a pressure sensor through
an ADS1115, applies a two-point calibration, and serves the result two ways:

- **MQTT** — a JSON snapshot published once per second, for logging and dashboards.
- **XCP on Ethernet** — the same signals observable in real time at task rate
  (down to 1 ms) with CANape or `xcpclient`, including live calibration of the
  sensor characteristic while the node keeps running.

That combination is the point of the project: MQTT gives you the slow, cheap,
always-on data stream, while XCP lets you connect at any moment and watch the
raw signal at full rate without changing the firmware or restarting anything.

![Demo Board](ESP32.png)

Derived from the `freertos_esp32_demo` example of
[XCPlite](https://github.com/RainerZ/XCPlite).


## Hardware

- An ESP32-S3 board compatible with `lilygo-t-display-s3`, or an adapted `platformio.ini`.
- An ADS1115 4 channel I2C ADC at its default address `0x48`.
- A 2.4 GHz WLAN. The ESP32 does not connect to 5 GHz-only networks.
- The board and the PC running CANape or `xcpclient` must be on the same network.

ADS1115 wiring for the LilyGo T-Display-S3:

| ADS1115 | LilyGo T-Display-S3 |
|---------|---------------------|
| VDD     | 3.3 V               |
| GND     | GND                 |
| SDA     | GPIO18 / P1         |
| SCL     | GPIO17 / P1         |
| ADDR    | GND                 |

The pressure sensor is read on **AIN0**. The converter runs at gain 1 (a
+/-4.096 V range) and 860 samples/s, so a conversion fits the default 2 ms slow
task period. Never drive an ADS1115 input below GND or above its supply voltage,
whatever the configured range.

If the ADS1115 is not detected at startup, the firmware logs it and falls back to
a generated sine wave, so the XCP and MQTT paths stay testable without hardware.

### Scope pins

Both tasks drive a pin high while running, so scheduler interaction is visible on
a two channel scope. Connect both probe grounds to GND.

- `fastTask`: GPIO2 / IO2, default period 1 ms
- `slowTask`: GPIO1 / IO1, default period 2 ms


## Quick start

```bash
cp src/wlan.h.example src/wlan.h   # then edit in your SSID and password
pio run --target upload
pio device monitor                 # note the IP address the board reports
```

Generate the A2L file from the firmware ELF and connect:

```bash
xcpclient --offline --udp --dest-addr <esp32-ip> --elf .pio/build/lilygo-t-display-s3/firmware.elf --a2l CANape/pressure_monitor.a2l --elf-unit-filter pressure_monitor
```

```bash
xcpclient --udp --dest-addr <esp32-ip> --a2l CANape/pressure_monitor.a2l --mea pressure
```


## Measurement and calibration

Measurements, all observable over XCP:

| Signal | Unit | Description |
|---|---|---|
| `pressure` | bar | Calibrated pressure, updated in `slowTask` |
| `pressure_sensor_voltage` | V | Raw sensor voltage on AIN0 |
| `global_counter` | | Free running counter, incremented in `fastTask` |
| `fastTaskOverruns`, `slowTaskOverruns` | | Deadline misses, non-zero when a period is set too aggressively |

Calibration parameters in the `parameters` segment, writable live over XCP:

| Parameter | Unit | Default |
|---|---|---|
| `fast_task_period_ms` | ms | 1 |
| `slow_task_period_ms` | ms | 2 |
| `counter_max` | | 1000 |
| `amplitude` | bar | 1.0 (fallback sine generator only) |
| `sensor_voltage_point1` / `pressure_point1` | V / bar | 0.0 / 0.0 |
| `sensor_voltage_point2` / `pressure_point2` | V / bar | 1.0 / 1.0 |

The two calibration points define a straight line; values between and outside
them are interpolated and extrapolated. If both voltage points are equal, the
calibration is degenerate and `pressure` is set to `NaN`.

Calibration segments are accessed through a lock that uses atomics only, with no
blocking mutex held, so XCP can rewrite parameters without disturbing task timing.


## Configuration

Everything is set through `build_flags` in [platformio.ini](platformio.ini).

### Hardware options

| Option | Effect |
|---|---|
| `OPTION_DISPLAY` | Status page on the T-Display-S3 LCD (needs LovyanGFX) |
| `OPTION_IO` | Scope trigger pins |
| `OPTION_ANALOG` | ADS1115 input; without it the sine generator is always used |
| `OPTION_MQTT` | MQTT publisher task |

For a board without the LilyGo display, drop `OPTION_DISPLAY` and remove
`lovyan03/LovyanGFX` from `lib_deps`.

### WiFi credentials

Either copy `src/wlan.h.example` to `src/wlan.h` and edit it — `src/wlan.h` is
gitignored, so credentials never reach the repository — or pass them as build
flags, in which case `wlan.h` is not included at all:

```ini
build_flags =
    -DWIFI_SSID=\"your-ssid\"
    -DWIFI_PASSWORD=\"your-password\"
```

### MQTT

The slow task never performs network operations. It formats a JSON snapshot into
a one-element mailbox at most once per second; a separate low priority task owns
the broker connection, reconnection, and publishing, and always sends the newest
queued snapshot. Nothing is published while `pressure` is `NaN`.

Defaults:

- Broker `mqtt.local:1883`
- Topic `pressure_monitor/measurement`
- Payload `{"pressure":1.234567,"voltage":0.987654}`
- Publish period 1000 ms

Override with build flags:

```ini
build_flags =
    -DMQTT_BROKER_HOST=\"192.168.0.10\"
    -DMQTT_BROKER_PORT=1883
    -DMQTT_TOPIC=\"pressure_monitor/measurement\"
    -DMQTT_PUBLISH_PERIOD_MS=1000
    -DMQTT_PAYLOAD_MAX_LENGTH=160
```

For a broker requiring authentication, define both `MQTT_USERNAME` and
`MQTT_PASSWORD`. This project uses unencrypted MQTT; TLS is out of scope.

### XCP

Set in `src/pressure_monitor.cpp`: UDP, port `5555`, project name
`pressure_monitor`, EPK `V100`. TCP is not supported by the XCPlite FreeRTOS
build.


## Project layout

```
platformio.ini            Build configuration, all options live here
extra_script.py           Compiles the vendored XCPlite subset
extra_linker_script.py    ESP-IDF section placement for the XCP metadata
include/                  Module interfaces
src/
  main.cpp                setup() / loop(), startup sequence
  pressure_monitor.cpp    XCP server, tasks, measurements, calibration
  analog.cpp              ADS1115
  display.cpp             T-Display-S3 status page
  wifi_sta.cpp            WiFi scan and connect
  mqtt.cpp                MQTT publisher task
  clock64.c               64 bit microsecond DAQ clock
  wlan.h.example          Template for the gitignored src/wlan.h
xcplite/                  Vendored XCPlite subset, see below
tools/update_xcplite.sh   Refreshes xcplite/ from an XCPlite repository
CANape/                   CANape project and generated A2L
```


## The XCPlite library

`xcplite/` holds a vendored subset of XCPlite: the 7 source files needed for a
32 bit FreeRTOS target plus the headers they require. `extra_script.py` compiles
them into a static library at build time and puts `xcplite/inc` and `xcplite/src`
on the include path.

Vendoring rather than submoduling keeps the repository self-contained and
buildable offline, and keeps library code visible to the debugger.
`xcplite/VERSION` records exactly which upstream commit the snapshot came from.

Refresh it with:

```bash
tools/update_xcplite.sh --repo https://github.com/RainerZ/XCPlite --ref master
```

With no arguments the script reuses the repo and ref recorded in
`xcplite/VERSION`. A local path is a valid `--repo`, which is currently required:
the XCPlite changes this project depends on live on a local branch that has not
been pushed.

The source list is defined twice, in `XCPLITE_SOURCES` in `extra_script.py` and
in the manifest in `tools/update_xcplite.sh`. Keep them in sync.

### Configuration and linker sections

The firmware uses XCPlite's `rtos` configuration:

```ini
-D_FREE_RTOS
-DXCPLITE_CONFIGURATION=rtos
-DXCPLIB_CFG_OVERRIDE=\"xcplib_rtos_cfg.h\"
```

This selects absolute addressing, a 32 bit DAQ queue, a 1 us clock, and disables
on-target A2L generation, persistence and TCP — see `xcplite/src/xcplib_rtos_cfg.h`.

Offline A2L generation needs XCPlite metadata in the `xcp_cals`, `xcp_evts`,
`xcp_epk` and `xcp_meta` sections. ESP-IDF requires `.flash.appdesc` and
`.flash.rodata` to be adjacent, and esptool requires data sharing a flash MMU
page to live in one ELF segment. If the XCPlite input sections are left as linker
orphans they land between those two output sections and `firmware.bin` creation
fails.

`extra_linker_script.py` therefore generates a build-local copy of ESP-IDF's
`sections.ld`: it collects `xcp_cals` and `xcp_evts` at the start of
`.flash.rodata` and defines the `__start_*` / `__stop_*` boundary symbols, and
places `xcp_epk` and `xcp_meta` immediately afterwards as individually named
output sections, because `xcpclient` locates those by ELF section name. The
installed PlatformIO framework files are never modified.

After changing build flags, source selection, or linker behaviour, verify:

```bash
pio run
xtensa-esp32s3-elf-readelf -S .pio/build/lilygo-t-display-s3/firmware.elf | grep -i "xcp\|flash.rodata\|appdesc"
xtensa-esp32s3-elf-nm .pio/build/lilygo-t-display-s3/firmware.elf | grep "__start_xcp\|__stop_xcp"
```

`.flash.appdesc` must end exactly where `.flash.rodata` begins, and `xcp_epk` and
`xcp_meta` must still be named sections.


## Offline A2L generation

The FreeRTOS build has no filesystem and cannot generate or upload an A2L file,
so the A2L is produced from the firmware ELF on the host with `xcpclient` from
the XCPlite repository:

```bash
xcpclient --offline --udp --dest-addr <esp32-ip> \
  --elf .pio/build/lilygo-t-display-s3/firmware.elf \
  --a2l CANape/pressure_monitor.a2l \
  --elf-unit-filter pressure_monitor --log-level=3
```

`--elf-unit-filter pressure_monitor` restricts the A2L to this application
instead of every linked symbol. `--offline --udp --dest-addr` writes the target
address into the A2L; without it the A2L defaults to localhost.
`CANape/XCP_104.aml` must sit next to the generated file, since the A2L includes it.

Expected output: the `fastTask` and `slowTask` events, the `parameters` segment
with its eight members, global and static measurements, supported stack
variables, the `V100` EPK, and the comments and units from the `xcp_meta` section.

Two harmless diagnostics from `xcpclient`:

- `Duplicate instance named 'pressure'` / `'pressure_sensor_voltage'` — these are
  declared in `include/pressure_monitor.h` and defined in
  `src/pressure_monitor.cpp`, so that translation unit's DWARF carries both a
  declaration and a definition. The first registration wins and carries the
  address; the A2L is correct.
- `evaluate_exprloc: ...` errors, and the `slowTask` local `voltage` resolving to
  the same address as `pressure_sensor_voltage` — DWARF location artifacts of the
  optimized build. Both also occur with the upstream example.


## Build and upload

```bash
pio run
pio run --target upload
pio device monitor
```

The serial port is set in `platformio.ini`. Find yours with `pio device list`.
If upload struggles to enter the bootloader, hold BOOT while the upload starts
and release when PlatformIO prints `Connecting...`.

Raise `XCP_LOG_LEVEL` in `src/pressure_monitor.cpp` for more XCP logging
(3 info, 4 XCP commands, 5 debug).


## Network test

Confirm the board reports an IP address in the serial log, then:

```bash
ping <esp32-ip>
```

The XCP server listens on UDP port 5555. A basic connection test:

```bash
xcpclient --udp --dest-addr <esp32-ip>
```

The A2L upload error can be ignored; the FreeRTOS build does not support
on-target A2L generation or upload.


## Licence

MIT. The vendored XCPlite subset in `xcplite/` is MIT, Copyright (c) Vector
Informatik GmbH — see `xcplite/LICENSE`.
