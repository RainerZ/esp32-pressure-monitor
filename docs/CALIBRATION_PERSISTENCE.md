# Persisting XCP calibration changes — discussion notes

Placeholder for a design discussion we deferred. Nothing here is implemented.

The question: after tuning `parameters` live from CANape, how do we keep those
values across a power cycle?

Facts below were read out of XCPlite at the vendored snapshot (see
`../xcplite/VERSION`) and from the running firmware. Anything not yet verified is
marked **(to verify)**.


## 1. Where we stand today

Calibration changes are **lost on reset**. Not by oversight — persistence is
compiled out:

```c
// xcplite/src/xcplib_rtos_cfg.h
// No persistence (no filesystem on embedded)
#undef OPTION_ENABLE_PERSISTENCE
```

`OPTION_ENABLE_PERSISTENCE` is *on* by default in `xcplib_cfg.h`; the RTOS
override turns it off, on the assumption that an embedded target has no
filesystem. Three things follow from that one `#undef`:

| Derived setting | Effect when persistence is off |
|---|---|
| `XCP_ENABLE_CAL_PERSISTENCE` | Working-page data is never saved |
| `XCP_ENABLE_FREEZE_CAL_PAGE` | The `FREEZE_CAL_PAGE` command does not exist |
| `XCP_MODE_PERSISTENCE` | Passing it to `XcpInit()` logs a warning and is stripped |

You can see the consequence from the host — `xcpclient` reports on connect:

```
XCP FREEZE_SUPPORTED = false
```

`persistence.c` is also not in our vendored source list at all, only
`persistence.h` (which compiles to nothing without the option).

So this is a design decision to make, not a config flag to flip.

### What is actually at stake

Only the calibration **working page** — currently the 36-byte `parameters`
struct. Measurements are not persisted and should not be. The whole calibration
memory pool is `OPTION_CAL_MEM_SIZE` = 4 KB, so the data volume is trivial; the
difficulty is entirely in *where* to put it and *when* to write it.


## 2. What XCPlite already gives us

Upstream `persistence.c` (553 lines, not vendored) implements a binary snapshot
file through plain stdio — `fopen` / `fwrite` / `fread`:

```c
bool XcpBinWrite(const char *epk);              // write current default pages
bool XcpBinLoad(void);                          // load, mark segments preloaded
void XcpBinDelete(void);
bool XcpBinFreezeCalSeg(tXcpCalSegIndex calseg); // freeze one working page
const char *XcpBinGetFilename(void);
```

The file holds a header plus event, calibration-segment and application records,
so event IDs and segment order stay stable across restarts — which matters
because the A2L addresses are absolute in this configuration.

`XcpBinLoad()` is already wired into `XcpInit()`, gated on the
`XCP_MODE_PERSISTENCE` flag:

```c
// xcplite.c, end of XcpInit()
#ifdef OPTION_ENABLE_PERSISTENCE
    if ((mode & XCP_MODE_PERSISTENCE) != 0) {
        if (XcpBinLoad()) { ... }
    }
#endif
```

We currently call `XcpInit(XCP_PROJECT_NAME, XCP_PROJECT_VERSION, XCP_MODE_LOCAL)`
in `src/pressure_monitor.cpp`, so the flag is simply not set.

**The filename carries the EPK**: `<project>_<epk>.bin`, i.e.
`pressure_monitor_V100.bin`. That is a deliberate and important property — bump
`XCP_PROJECT_VERSION` and old calibration data is automatically ignored rather
than being loaded into a struct whose layout has changed. Any scheme we choose
should keep an equivalent guard.

There is also a `XCP_ENABLE_FREEZE_ON_DISCONNECT` option, present but commented
out upstream, which would persist automatically when the XCP client disconnects.


## 3. Options

### A — Mount a filesystem, use upstream `persistence.c` unchanged

ESP-IDF exposes LittleFS / FFat through its VFS layer, and newlib `stdio` is
implemented on top of VFS. So `fopen()` may well work as-is once a filesystem
partition is mounted **(to verify)**.

- *Least new code*: re-enable `OPTION_ENABLE_PERSISTENCE`, add `persistence.c`
  to `XCPLITE_SOURCES` in `extra_script.py` **and** to the manifest in
  `tools/update_xcplite.sh`, pass `XCP_MODE_PERSISTENCE`, mount the filesystem
  before `XcpInit()`.
- *Stays aligned with upstream*, so future XCPlite fixes come along for free.
- Known snag: `XcpBinGetFilename()` returns a bare filename with no directory,
  which VFS needs as an absolute path (`/littlefs/...`). Needs a path prefix, a
  `chdir()`, or a small upstream patch — worth raising with the XCPlite side
  rather than carrying a local diff.
- Costs a partition-table change and pulls in a filesystem.

### B — NVS key/value

ESP-IDF's NVS is a wear-levelled key/value store in its own partition, no
filesystem required, and is the idiomatic ESP32 answer.

- Robust against power loss, designed for exactly this.
- Requires implementing the save/restore ourselves against the calibration
  segment API — but only the *storage*, not the freeze logic.
- Diverges from upstream, so it is our code to maintain.
- Would need its own EPK check, since we lose the filename-based one.

### C — Raw dedicated flash partition

Write the working page to a reserved partition on an explicit freeze.

- Most control, no filesystem, no NVS dependency.
- Most work: erase/write/wear/torn-write handling all become ours.
- Probably only justified if A and B both prove awkward.


## 4. Open questions

1. **Trigger.** Explicit `FREEZE_CAL_PAGE` from CANape (deliberate, matches XCP
   semantics), freeze-on-disconnect (convenient, easy to trigger by accident), or
   an application-level "save" action? Never per measurement cycle — flash wear.
2. **Invalidation.** Keep EPK in the key or filename. Absolute addressing means a
   recompile can move things; loading a stale page would be silently wrong. How
   strict: EPK only, or a layout hash too?
3. **Failure behaviour.** Corrupt or missing data on boot — fall back to the
   reference page silently, or surface it on the display and over XCP?
4. **Scope.** Only `parameters` today, but the mechanism should not assume a
   single segment.
5. **Reference page semantics.** With persistence, "reference page" and
   "power-on values" stop being the same thing. `MQTT_PUBLISH_PERIOD_MS` and the
   other build-flag defaults become the *factory* values, with a persisted layer
   on top. Worth deciding how CANape should present that, and whether we want an
   explicit "restore factory defaults" path (`XcpBinDelete()`).
6. **Interaction with `OPTION_CAL_SEGMENTS_ABS`.** Absolute segment addressing
   plus the calibration bump allocator — confirm a preloaded segment keeps the
   same address **(to verify)**.


## 5. Suggested first step

Before choosing, test option A cheaply: mount LittleFS, add `persistence.c` to
the build, enable the option, pass `XCP_MODE_PERSISTENCE`, and see whether
`XcpBinWrite()` / `XcpBinLoad()` work over VFS with an absolute path. That single
experiment either makes A the obvious answer or rules it out and sends us to B.

Relevant code:

- `src/pressure_monitor.cpp` — the `XcpInit()` call and the `parameters` segment
- `extra_script.py` — `XCPLITE_SOURCES`
- `tools/update_xcplite.sh` — the vendoring manifest
- `xcplite/src/xcplib_rtos_cfg.h` — the `#undef`
- `platformio.ini` — `board_build.partitions` if a partition is needed
