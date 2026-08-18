# ESP32 Pressure Monitor — agent guidance

## Context to load

- `README.md` for hardware, configuration, build and A2L generation.
- `xcplite/VERSION` for the vendored XCPlite snapshot in use.
- `platformio.ini` and the two extra scripts are the authoritative build
  configuration. There is no CMake build in this project.

## Invariants

- Build with `pio run` from the project root.
- Keep the XCPlite `rtos` configuration: `_FREE_RTOS`,
  `XCPLITE_CONFIGURATION=rtos`, `XCPLIB_CFG_OVERRIDE="xcplib_rtos_cfg.h"`.
- `xcplite/` is gitignored and not part of this repository's history. A fresh
  clone must run `tools/update_xcplite.sh` once before the first build.
- Never edit files under `xcplite/` by hand. They are a snapshot; change them
  upstream and re-run `tools/update_xcplite.sh`.
- Keep `XCPLITE_SOURCES` in `extra_script.py` in sync with the manifest in
  `tools/update_xcplite.sh`.
- `xcp_cals`, `xcp_evts`, `xcp_epk` and `xcp_meta` carry the metadata for offline
  A2L generation. Keep them in flash and retained by the linker. `xcpclient`
  locates `xcp_epk` and `xcp_meta` by ELF section name, but `xcp_cals` and
  `xcp_evts` are merged into `.flash.rodata` and are found only through the
  `__start_*` / `__stop_*` boundary symbols. Dropping those symbols breaks event
  ID derivation and therefore A2L generation.
- ESP-IDF requires `.flash.appdesc` and `.flash.rodata` to be adjacent, and
  `.flash.rodata`, `xcp_epk` and `xcp_meta` must be exactly contiguous so the
  image has a single DROM segment. A gap makes the bootloader map only the last
  segment and the board resets on every boot. `extra_linker_script.py` pads the
  named sections and asserts contiguity after linking. Do not remove either.
- Never commit `src/wlan.h` or credentials in build flags.

## Verification

After changing build flags, source selection or linker behaviour:

1. `pio run`, confirm `firmware.elf` and `firmware.bin` are produced.
2. `readelf -S` — `.flash.appdesc` ends where `.flash.rodata` begins, and
   `xcp_epk` / `xcp_meta` are still named sections.
3. `nm` — `__start_xcp_cals`, `__stop_xcp_cals`, `__start_xcp_evts`,
   `__stop_xcp_evts` are all defined.
4. `esptool image-info firmware.bin` reports exactly one DROM segment.
5. Regenerate the A2L with `xcpclient` and confirm both DAQ events and the
   `parameters` segment appear.
