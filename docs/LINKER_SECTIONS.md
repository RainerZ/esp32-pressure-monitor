# XCPlite linker sections on ESP32 — reference notes

Background notes for the flash layout this project depends on. Written up
because the failure modes here are silent at build time and only show as a board
that will not boot, or as A2L generation that mysteriously produces nothing.

Everything described here is implemented in
[`../extra_linker_script.py`](../extra_linker_script.py).


## 1. Why there are custom sections at all

XCPlite has to tell the outside world about things that only exist at compile
time: which calibration parameters exist, which DAQ events exist, what version
the firmware is, and what the units and comments of each variable are.

On a desktop target XCPlite can write an A2L file at runtime. On this FreeRTOS
build it cannot — there is no filesystem, and `xcplib_rtos_cfg.h` disables the
A2L generator entirely. So the information is instead **emitted into dedicated
ELF sections at compile time** and read back off the host afterwards.

Four sections carry it:

| Section | Written by | Contains | Consumed by |
|---|---|---|---|
| `xcp_cals` | `CalSegDecl` / `CalSegDeclRef` | Calibration segment descriptors | Firmware at `XcpInit()`, and `xcpclient` |
| `xcp_evts` | `DaqCreateEvent` | DAQ event descriptors | Firmware at `XcpInit()`, and `xcpclient` |
| `xcp_epk` | `XcpCreateEpk` | Version string ("EPK") | `xcpclient` |
| `xcp_meta` | `XCP_COMMENT`, `XCP_UNIT`, `XCP_LIMITS`, `XCP_READ_WRITE` | Per-variable annotations | `xcpclient` |

`xcp_cals` and `xcp_evts` are read **by the firmware itself** during
`XcpInit()`, which is why they must stay in mapped flash rather than being
stripped. `xcp_epk` and `xcp_meta` are only ever read from the ELF on the host.

Keeping all four in flash rather than RAM matters on a microcontroller: they are
descriptors, not live data, so they stay in cached memory-mapped flash and cost
no internal SRAM.


## 2. Two different discovery mechanisms

This is the part that is easy to misread, because the two pairs of sections are
found in completely different ways.

**`xcp_epk` and `xcp_meta` are found by ELF section name.**
`xcpclient` opens the ELF and looks for sections literally called `xcp_epk` and
`xcp_meta`. They must therefore survive linking as *named output sections*.

**`xcp_cals` and `xcp_evts` are not.**
The linker script deliberately merges them into `.flash.rodata` (see §3), so
after linking there is no section named `xcp_evts` at all. Instead the script
defines boundary symbols around the merged content:

```
__start_xcp_cals / __stop_xcp_cals
__start_xcp_evts / __stop_xcp_evts
```

The firmware uses these at `XcpInit()` to walk the descriptor arrays, and
`xcpclient` falls back to them when it cannot find a section by name.

**Consequence:** those four symbols are not debug conveniences. Delete them and
event IDs can no longer be derived, and A2L generation fails with
`Duplicate("fastTask")` — the same event name registering twice because
`xcpclient` could not tell the events apart.

You can see the asymmetry directly:

```console
$ xtensa-esp32s3-elf-readelf -S firmware.elf | grep xcp_
  [16] xcp_epk    PROGBITS  3c0f40fc ...      <- named section, present
  [17] xcp_meta   PROGBITS  3c0f4108 ...      <- named section, present
                                              <- no xcp_cals, no xcp_evts

$ xtensa-esp32s3-elf-nm firmware.elf | grep __start_xcp
3c0c0120 A __start_xcp_cals                   <- found via symbols instead
3c0c0160 A __start_xcp_evts
```

The `xcpclient` fallback to boundary symbols arrived in XCPlite commit
`20175a2`. That commit is on the `V2.1.10` **branch** but lands *after* the
commit labelled "V2.1.10", and the repository has only one tag (`V0.9.2`), so
"use V2.1.10" is not precise enough — build `xcpclient` from the branch head.


## 3. Constraint A — ESP-IDF section adjacency

ESP-IDF requires `.flash.appdesc` and `.flash.rodata` to be **adjacent** in the
final image. The app descriptor is what the bootloader reads to identify the
image.

If custom input sections are left as *linker orphans*, the linker places them
wherever it likes — including between `.flash.appdesc` and `.flash.rodata`. When
that happens, ESP-IDF refuses to produce `firmware.bin` at all.

That is why `extra_linker_script.py` does not simply let the sections fall where
they may. It generates a build-local copy of ESP-IDF's `sections.ld` and places
everything explicitly. The installed PlatformIO framework files are never
modified — the copy lives in the build directory.

Verify:

```bash
xtensa-esp32s3-elf-readelf -S firmware.elf | grep -iE "appdesc|flash.rodata"
```

`.flash.appdesc` must end exactly where `.flash.rodata` begins:
`0x3c0c0020 + 0x100 = 0x3c0c0120`.


## 4. Constraint B — exactly one DROM segment

**This is the one that cost us a bootloop, and it is invisible until you flash.**

An ELF has many sections; the flash image has a few *segments*. `esptool`
converts one to the other, and it merges ELF sections into a single image
segment **only when they are contiguous** — no gap.

DROM ("data read-only memory") is the memory-mapped flash region holding
constants. The ESP-IDF bootloader maps **one** DROM segment. If the image
contains more than one, it maps only the last and prints:

```
E (209) boot: Image contains multiple DROM segments. Only the last one will be mapped.
```

### How the gap appeared

`xcp_epk` holds the EPK string — 5 bytes for `"V100"`. `xcp_meta` is declared
`ALIGN(8)`. So:

```
xcp_epk   starts 0x3c0f40ec, 5 bytes  ->  ends 0x3c0f40f1
xcp_meta  ALIGN(8)                    ->  starts 0x3c0f40f8
                                          ^^^^^^ 7 byte hole
```

That 7-byte hole was enough to split DROM in two. The result:

| | Segments | DROM segments |
|---|---|---|
| Broken | 8 | 2 (`0x3c0c0020` and `0x3c0f40f8`) |
| Fixed | 6 | 1 (`0x3c0c0020`) |

The second DROM segment was `xcp_meta` — 440 bytes. The bootloader mapped
*that* and left all ~212 KB of real rodata unmapped, so the firmware crashed the
instant it touched a constant. Reset, repeat, roughly every 12 seconds. The only
visible symptom was a dead display and a USB serial port that kept
disappearing and reappearing.

### The fix

Pad each named section to the alignment of the next one, so no hole can form:

```ld
  xcp_epk : ALIGN(1)
  {
    KEEP(*(xcp_epk))
    . = ALIGN(8);        /* <- pads the section itself to 8 */
  } > default_rodata_seg

  xcp_meta : ALIGN(8)
  {
    KEEP(*(xcp_meta))
    . = ALIGN(8);
  } > default_rodata_seg
```

Setting the location counter *inside* an output section grows that section, so
`xcp_epk` becomes 12 bytes ending at `0x3c0f40f8` — exactly where `xcp_meta`
starts. Contiguous, so esptool merges them.

`KEEP()` matters too: without it, `--gc-sections` discards these sections
because nothing in the C code references them by symbol.

### The guard

Because this is invisible at build time, `extra_linker_script.py` parses the
linked ELF on every build and fails with an explicit error if a gap reappears:

```
*** [firmware.elf] RuntimeError : Gap of 7 bytes between xcp_epk (ends 0x3c0f4101)
and xcp_meta (starts 0x3c0f4108).
esptool would emit separate DROM segments, the ESP-IDF bootloader would map
only the last one, and the firmware would crash and reset on every boot.
```

Do not remove it.


## 5. Diagnosis cheat sheet

| Symptom | Likely cause |
|---|---|
| Board resets every few seconds, display dead, USB serial port appears/disappears | Multiple DROM segments (§4) |
| `E boot: Image contains multiple DROM segments` | Same — a gap between the named sections |
| `firmware.bin` is never produced | Orphan sections split `.flash.appdesc` from `.flash.rodata` (§3) |
| A2L generation aborts with `Duplicate("fastTask")` | `xcpclient` too old for the boundary-symbol fallback (§2), or the `__start_*` / `__stop_*` symbols were dropped |
| A2L has no calibration parameters | `xcp_cals` was garbage-collected — check `KEEP()` |
| A2L has no comments or units | `xcp_meta` missing or not a named section |

To read the boot log while the board is reset-looping, do **not** drive DTR/RTS:
with `ARDUINO_USB_MODE=1` those lines control reset on the ESP32-S3, so a
monitor that asserts them keeps resetting the board and you see nothing. Open the
port with `dtr=False, rts=False`.


## 6. Full verification

```bash
pio run

# 1. appdesc and rodata adjacent, xcp_epk/xcp_meta present and contiguous
xtensa-esp32s3-elf-readelf -S .pio/build/lilygo-t-display-s3/firmware.elf \
  | grep -iE "appdesc|flash.rodata|xcp_"

# 2. boundary symbols defined
xtensa-esp32s3-elf-nm .pio/build/lilygo-t-display-s3/firmware.elf \
  | grep -E "__start_xcp|__stop_xcp"

# 3. exactly ONE DROM segment
python -m esptool --chip esp32s3 image-info \
  .pio/build/lilygo-t-display-s3/firmware.bin | grep DROM

# 4. A2L generation finds both events and the calibration segment
xcpclient --offline --udp --dest-addr <ip> \
  --elf .pio/build/lilygo-t-display-s3/firmware.elf \
  --a2l CANape/pressure_monitor.a2l \
  --elf-unit-filter pressure_monitor --log-level=3
```


## 7. History

- Commit `20175a2` (XCPlite, `V2.1.10` branch) added the named `xcp_epk` /
  `xcp_meta` output sections and the `xcpclient` boundary-symbol fallback. It
  fixed event discovery **and** introduced the DROM gap in the same change.
- The gap was never noticed upstream because the change was not run on hardware
  afterwards.
- Fixed here and upstream by padding the named sections; the upstream example
  `examples/freertos_demo/freertos_esp32_demo` had the identical defect.
