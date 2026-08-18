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
| `pressure` | bar | Calibrated pressure, updated in `slowTask` at task rate |
| `pressure_filtered` | bar | Mean pressure over the last publish interval — the value sent to MQTT |
| `pressure_filtered_samples` | | Number of samples averaged into `pressure_filtered` |
| `pressure_sensor_voltage` | V | Raw sensor voltage on AIN0 |
| `global_counter` | | Free running counter, incremented in `fastTask` |
| `fastTaskOverruns`, `slowTaskOverruns` | | Deadline misses, non-zero when a period is set too aggressively |

Calibration parameters in the `parameters` segment, writable live over XCP:

| Parameter | Unit | Default |
|---|---|---|
| `fast_task_period_ms` | ms | 1 |
| `slow_task_period_ms` | ms | 2 |
| `mqtt_publish_period_ms` | ms | `MQTT_PUBLISH_PERIOD_MS`, default 1000 (clamped to 100 … 3600000) |
| `counter_max` | | 1000 |
| `amplitude` | bar | 1.0 (fallback sine generator only) |
| `sensor_voltage_point1` / `pressure_point1` | V / bar | 0.0 / 0.0 |
| `sensor_voltage_point2` / `pressure_point2` | V / bar | 1.0 / 1.0 |

The two calibration points define a straight line; values between and outside
them are interpolated and extrapolated. If both voltage points are equal, the
calibration is degenerate and `pressure` is set to `NaN`.

Calibration changes are **lost on reset** — this build has no persistence, and
`xcpclient` correspondingly reports `FREEZE_SUPPORTED = false`. Options for
changing that are collected in
[docs/CALIBRATION_PERSISTENCE.md](docs/CALIBRATION_PERSISTENCE.md).

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

XCP and MQTT deliberately carry different things. XCP exposes the **raw**
`pressure` at task rate for real-time analysis; MQTT carries the **mean** over
the publish interval, so a once-per-second subscriber gets a filtered value
rather than an arbitrary instantaneous sample.

`slowTask` accumulates every valid `pressure` sample and, once the interval
elapses, divides by the sample count, stores the result in `pressure_filtered`
and hands it to the publisher task. At the 2 ms default that averages about 500
samples per second, which resolves well below one ADS1115 quantization step: raw
readings jump between discrete LSB values, while the filtered output moves
smoothly in the fifth decimal.

The accumulator is a `double`. A long calibrated interval can gather tens of
thousands of samples, and one software `double` add per 2 ms task cycle is far
below the noise floor of the task timing.

The averaging window and the publish period are the same value, and it is an XCP
**calibration parameter** (`parameters.mqtt_publish_period_ms`), so you can
retune it live from CANape without reflashing. It is clamped to 100 … 3600000 ms.

The power-on value is still a build flag, `MQTT_PUBLISH_PERIOD_MS`: it sets the
calibration *default/reference page*, which is what the node starts with and what
CANape shows as the reference value. XCP changes on the working page override it
until the next reset — this build has no persistence, so a calibrated value is
not retained across a power cycle. A `static_assert` rejects a build flag outside
the clamp range, so the startup value can never silently disagree with the A2L.

The slow task never performs network operations: it formats a JSON snapshot into
a one-element mailbox, and a separate low priority task owns the broker
connection, reconnection, and publishing, always sending the newest queued
value. Nothing is published while every sample in an interval is `NaN`.

Defaults:

- Broker `192.168.0.200:1883` (set in `platformio.ini`)
- Topic `pressure_monitor/measurement`
- Payload `{"pressure":1.086450}`
- Publish period 1000 ms, adjustable over XCP

Override with build flags:

```ini
build_flags =
    -DMQTT_BROKER_HOST=\"192.168.0.10\"
    -DMQTT_BROKER_PORT=1883
    -DMQTT_TOPIC=\"pressure_monitor/measurement\"
    -DMQTT_PAYLOAD_MAX_LENGTH=160
    -DMQTT_PUBLISH_PERIOD_MS=1000
```

For a broker requiring authentication, define both `MQTT_USERNAME` and
`MQTT_PASSWORD`. This project uses unencrypted MQTT; TLS is out of scope.

### XCP

Set in `src/pressure_monitor.cpp`: UDP, port `5555`, project name
`pressure_monitor`. TCP is not supported by the XCPlite FreeRTOS build.

#### EPK — matching an A2L to the firmware

The EPK is the version string a tool uses to decide whether an A2L still
describes the firmware in front of it. The A2L records both the string and the
address it lives at:

```
EPK "V100-3b357d1d" ADDR_EPK 0x3C0F40FC
```

CANape reads that address from the target and compares. The check is only worth
anything if the EPK changes whenever addresses can have moved, so it is
generated rather than hand-maintained: `extra_script.py` hashes every file under
`src/`, `include/` and `xcplite/` plus `platformio.ini` and the two build
scripts, and writes the first 8 hex digits into `$BUILD_DIR/epk_generated.h`.
`src/epk.cpp` composes the final EPK as `XCP_PROJECT_VERSION "-" <hash>`.

This gives the properties that matter:

| | |
|---|---|
| Source, library or build config changes | EPK changes, A2L must be regenerated |
| Rebuild with nothing changed | EPK identical, no relink, existing A2L stays valid |
| `touch` without an edit | EPK identical |
| Same sources on another machine | Same EPK — the hash is content-based, not a timestamp |

`__DATE__` / `__TIME__` would be the obvious shortcut and is a trap: they only
update when the translation unit containing them is recompiled, so editing any
*other* file moves addresses while the EPK stays put.

`XCP_PROJECT_VERSION` in `include/epk.h` is the human-readable part; bump it for
a release. A `static_assert` enforces the 31 character `XCP_EPK_MAX_LENGTH`, so
an over-long version fails the build rather than being silently truncated.

The EPK is only read from the `xcp_epk` section, so its length varies with the
version string — see [docs/LINKER_SECTIONS.md](docs/LINKER_SECTIONS.md) for why
that section is padded and why the length is safe to change.

`xcpclient` compares the two since XCPlite `7df1965` and warns on a mismatch:

```
[WARN ] EPK mismatch: A2L file CANape/pressure_monitor.a2l has EPK 'V100-05f9c6f8',
        target has EPK 'V100-3b357d1d'. The A2L file is outdated, addresses may be wrong.
```

It is a warning, not a hard failure, so you can still connect with a suspect A2L
while iterating. Rebuild the A2L from the current ELF to clear it. An older
`xcpclient` will not report anything — the comparison used to be a `@@@@ TODO`.


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
xcplite/                  Vendored XCPlite subset (22 files), see below
tools/update_xcplite.sh   Refreshes xcplite/ from an XCPlite repository
docs/LINKER_SECTIONS.md   Reference notes on the XCP flash sections
docs/CALIBRATION_PERSISTENCE.md  Deferred design discussion, not implemented
CANape/                   CANape project and generated A2L
```

`docs/LINKER_SECTIONS.md` explains the ESP32 flash layout this project depends
on — what the four XCP sections are for, the two different ways `xcpclient`
finds them, and the two ESP-IDF constraints that make the layout fragile. Read
it before touching `extra_linker_script.py`.


## The XCPlite library

`xcplite/` holds a vendored subset of XCPlite: the 7 source files needed for a
32 bit FreeRTOS target plus the headers they require. `extra_script.py` compiles
them into a static library at build time and puts `xcplite/inc` and `xcplite/src`
on the include path.

The snapshot is committed to this repository, so a fresh clone builds without
fetching anything and without network access. `xcplite/VERSION` records the exact
upstream commit it came from.

The subset is minimal: all 22 files are opened by the compiler on a real build.
It is not a copy of XCPlite — the A2L generator, persistence, shared memory and
the 64-bit queue variants are all disabled by the rtos configuration and absent.

Vendoring rather than submoduling keeps the repository self-contained, avoids a
`clone --recursive` step, and keeps library code visible to the debugger. The
cost is that XCPlite sources live in this repository's history and updates are a
script run rather than a `git pull`.

If `xcplite/` is ever missing, `pio run` stops immediately with
`Vendored XCPlite sources not found at .../xcplite/src. Run tools/update_xcplite.sh`.

Refresh it with:

```bash
tools/update_xcplite.sh                 # reuse the repo and ref in xcplite/VERSION
tools/update_xcplite.sh --ref V2.1.11   # move to another upstream ref
```

With no arguments the script reuses the repo and ref recorded in
`xcplite/VERSION`, so re-running it reproduces the current snapshot. A local path
is also a valid `--repo`, which is useful for testing an upstream change before
it is pushed:

```bash
tools/update_xcplite.sh --repo ~/git/XCPlite-RainerZ --ref V2.1.10
```

The source list is defined twice, in `XCPLITE_SOURCES` in `extra_script.py` and
in the manifest in `tools/update_xcplite.sh`. Keep them in sync.

### Configuration and linker sections

> Detailed background, failure modes and a diagnosis cheat sheet:
> [docs/LINKER_SECTIONS.md](docs/LINKER_SECTIONS.md)


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
output sections. The installed PlatformIO framework files are never modified.

`xcpclient` finds this metadata two different ways, which is why the layout is
asymmetric:

- `xcp_epk` and `xcp_meta` are located **by ELF section name**, so they must
  survive as named output sections.
- `xcp_cals` and `xcp_evts` end up merged into `.flash.rodata` and no longer
  exist as sections with those names. `xcpclient` falls back to the
  `__start_xcp_cals` / `__stop_xcp_cals` and `__start_xcp_evts` /
  `__stop_xcp_evts` boundary symbols instead. Those symbols are therefore not
  optional debug aids — without them event IDs cannot be derived and A2L
  generation fails.

**These four sections must be exactly contiguous.** esptool only merges ELF
sections into a single image segment when no gap separates them. `xcp_epk` is
5 bytes, so without padding it ends unaligned and the `ALIGN(8)` of `xcp_meta`
leaves a 7 byte hole. That splits the flash rodata into two DROM segments, and
the ESP-IDF bootloader then maps only the last one — the tiny `xcp_meta` —
leaving all real rodata unmapped. The firmware crashes and resets on every boot,
printing only:

```
E (209) boot: Image contains multiple DROM segments. Only the last one will be mapped.
```

The script pads each named section with a trailing `. = ALIGN(8);` to prevent
this, and a post-build check parses the ELF and fails the build with an explicit
error if a gap ever reappears. Do not remove either.

After changing build flags, source selection, or linker behaviour, verify:

```bash
pio run
xtensa-esp32s3-elf-readelf -S .pio/build/lilygo-t-display-s3/firmware.elf | grep -i "xcp\|flash.rodata\|appdesc"
xtensa-esp32s3-elf-nm .pio/build/lilygo-t-display-s3/firmware.elf | grep "__start_xcp\|__stop_xcp"
python -m esptool --chip esp32s3 image-info .pio/build/lilygo-t-display-s3/firmware.bin | grep DROM
```

`.flash.appdesc` must end exactly where `.flash.rodata` begins, `xcp_epk` and
`xcp_meta` must still be named sections, and the image must contain exactly
**one** DROM segment.


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

This needs an `xcpclient` new enough to fall back to the `__start_xcp_evts` /
`__stop_xcp_evts` boundary symbols when no section literally named `xcp_evts`
exists in the ELF. That fallback arrived in XCPlite commit `20175a2`, which is on
the `V2.1.10` branch but *after* the commit labelled V2.1.10 — so build
`xcpclient` from the branch head, not from that commit. An older `xcpclient`
cannot derive distinct event IDs from this layout and aborts with
`Duplicate("fastTask")` before reaching calibration and metadata registration.
The snapshot in `xcplite/VERSION` is newer than `20175a2`, so an `xcpclient`
built from the same ref always works.

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
