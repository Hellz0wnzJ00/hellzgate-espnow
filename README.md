# HellzGate ESP-NOW - community beta source

> Some people spoon, we fork. Have fun and be safe! - Hellz

**BETA - experimental source for testing and forks, not a final or production release.**

An early community test build for passive Wi-Fi/BLE observation collection.
Scanner nodes report to a master over ESP-NOW. The FullGate master supports
GNSS, microSD CSV logging, a local status page, a display and fan control.
This is not a production release or the frozen mobile-app integration API.

This source is also intended for forks using XIAO ESP32-C5 boards and custom
hardware. The FullGate configurations are board-specific examples, not universal
pin assignments. See the adaptation guidance below before using them elsewhere.

Owner: **Sean Clossey**. HellzGate is the project name.

## Package

Only source, build configurations and tests are included. No project logs,
field captures, delivery correspondence, PCB files or prebuilt binaries are bundled.
The wired scanner transport is excluded. Display I2C remains because it drives
the screen; it does not carry scanner records. MIT license: see LICENSE.
ESP-IDF and its dependencies retain their separate licenses.

## Build

Use an ESP-IDF environment targeting ESP32-C5. The starting firmware specifies
ESP-IDF 5.5.1. This candidate's master and both scanner configurations compiled
and linked successfully with ESP-IDF 5.5.3 on 2 October 2026. Version 5.5.1 was
not re-tested. A limited master/one-scanner bench test passed before the final
scanner-capacity adjustment and SD diagnostic guard; see VALIDATION.md for
remaining checks. Those final source changes were rebuilt but not reflashed.
Run these commands inside `firmware/`. Use separate build directories/configs.

FullGate master:
```
idf.py -B build_master "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.base;sdkconfig.xiao;sdkconfig.fullgate;sdkconfig.fullgate_espnow" "-DSDKCONFIG=build_master/sdkconfig" build
```

Scanner in a hardware slot with ID straps:
```
idf.py -B build_scanner "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.xiao;sdkconfig.fgnode_espnow" "-DSDKCONFIG=build_scanner/sdkconfig" build
```

Standalone scanner with an NVS ID (slots 1-20):
```
idf.py -B build_nvs "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.xiao;sdkconfig.fgnode_espnow;sdkconfig.fgnode_espnow_nvs" "-DSDKCONFIG=build_nvs/sdkconfig" build
```

All supplied builds select a capacity of 20 scanners; that is a configuration limit, not
proof of a validated 20-scanner field deployment. Every scanner needs a unique
ID. Slots are one-based; internal node IDs are zero-based. The four hardware
strap pins can encode slots 1-15, with zero reserved for an invalid slot; increasing
the cluster capacity does not create more physical slots. The existing backplane
has nine scanner slots. The NVS build accepts slots 1-20; use unused IDs, normally
10-20 when the nine hardware slots are populated. Never duplicate an ID between
a strapped scanner and an NVS scanner.

Provisioning example (generates a file without contacting hardware):
```
python tools/slot_nvs.py 10
```
The tool needs `esp_idf_nvs_partition_gen`. Its optional `--port PORT` writes
the whole NVS partition, replacing its existing contents. Check board, port,
partition layout and ID before use. Provision after flashing the matching image.

Flash only the matching build and verified board/port. For the master:
```
idf.py -B build_master "-DSDKCONFIG=build_master/sdkconfig" -p PORT flash monitor
```
Use the corresponding build directory and SDKCONFIG for each scanner.
The FullGate overlays assume the existing FullGate pin layout and an 8 MB
XIAO ESP32-C5 target. Do not apply these pin assignments to arbitrary boards.
Review configuration values before building. Use a new build directory when
changing overlays; defaults do not replace values in an existing sdkconfig.

## Use

### Adapting a fork to other hardware

The ESP-NOW transport does not require the FullGate PCB. The supplied master
command enables FullGate peripherals, however. For a bare master, use
`sdkconfig.defaults;sdkconfig.xiao` in a fresh build directory, run `menuconfig`,
and select the master role. Leave GNSS, SD, display, fan, pin finder and SD bench
checks disabled unless the corresponding hardware is connected and its pin map
has been verified. Match flash size, partition layout and PSRAM settings to the
actual module. This bare-master configuration has not been hardware-tested here.

A standalone scanner can use the NVS-ID build above without backplane straps.
Assign a unique ID before use; the supplied scanner will not send without one.
Do not use the hardware-slot build on a board without the matching strap wiring.

Without SD enabled, records go to the console rather than a card. Without GNSS,
exports have no GNSS timestamp or position. Forks can adapt the output path, but
must test their own hardware, storage and timing behavior.

### FullGate configuration

The FullGate master starts logging automatically when powered. Hold its configured
button to enable the hotspot. Join `hellzgate` (default password `hellzgate`) and
open `http://192.168.4.1/`. Set a different hotspot password in menuconfig.
Use Stop and allow the save to finish before disconnecting power or removing SD.
Counts are since boot, not per session. GNSS startup may produce empty timestamps
and zero coordinates. Check exported data before uploading it anywhere.

## Known limitations and useful testing

- The device's own hotspot is not filtered from observations yet.
- Radio send success is MAC-layer acknowledgement, not confirmation that records
  reached the master's application queue or SD card. Queue saturation can drop data.
- Receive validation order and delayed send-callback handling need additional tests.
- ESP-NOW enrollment is unauthenticated and unencrypted. Use an isolated test setup;
  matching clusters on the same channel can interfere. The local HTTP controls
  have no separate application authentication.
- Anyone connected to the hotspot can read GPS/session status and operate its
  controls. The public default password is not private. Serial output and SD
  files contain observed SSIDs, device addresses and positions. No automatic
  internet upload or remote analytics is implemented in the packaged code.
- Active SD wiring diagnostics require explicitly enabling `HG_SD_BENCH_CHECKS`.
  Keep it disabled for normal use, including boards with no card attached. The
  bench option drives GPIOs and requires disconnected modules and verified jumpers.
- The optional raw SD driver has unresolved token-buffer/capacity handling;
  keep `HG_SD_OWN_DRIVER` disabled. The supplied builds use ESP-IDF SDSPI.
- Current-build 20-scanner field validation remains outstanding. The host harness
  uses one scanner/master pair and synchronous callbacks.
- Power loss can discard buffered records, particularly before a GNSS fix.
- SSIDs are untrusted input. CSV quoting preserves columns, but does not disable
  spreadsheet formulas or terminal control sequences. Import exports as text.

Useful reports include build configuration, scanner count, uptime, queue/drop
counters, reset/recovery behavior and whether Stop completed. Remove SSIDs,
MAC addresses, coordinates, credentials and personal paths before sharing logs.
Use only equipment and testing environments where you have permission.

## Host tests

With GCC and Make available, from `firmware/test/`:
```
make
```
This runs record, tally, CSV, pending-buffer and ESP-NOW tests, including simulated
lost acknowledgements. It does not test RF hardware, real callback scheduling or
the maximum node count.
