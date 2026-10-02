# Review status - 2 October 2026

This experimental source package builds successfully in all three 20-node
configurations. A limited master/one-scanner bench test predates the final
capacity-default adjustment and SD diagnostic guard. Those final changes are
build-verified; their hardware verification remains pending.

## File coverage and privacy

Every line of all 73 candidate files was read, including production code,
headers, test fixtures/stubs, scripts, configuration, comments and documentation.
Follow-up scans checked names, paths, URLs, credentials, encoded content and
control characters. No private credentials, contributor correspondence,
personal computer paths, field captures or outside-contributor identities were
found in this candidate. Sean Clossey's owner/MIT attribution is intentional.
Example coordinates, dates and addresses in tests are fixed synthetic fixtures.

No automatic external upload or analytics endpoint was found. Runtime data is
not confidential by design: read the ESP-NOW, hotspot, serial and CSV limitations
in README. Complete file coverage does not guarantee absence of vulnerabilities.

## Corrections and static checks

Removed stale wired-transport notes and empty tests. Technical comments now
describe current behavior. Restored two meaningful disabled configuration values
that the earlier cleanup had incorrectly treated as comments: the FullGate
SSD1306 selection and the standalone scanner's disabled hardware straps.

All 17 CMake sources and their 35-file production include closure resolve.
All 78 project include references, 80 ordinary API declarations and 45 HG
configuration references checked resolve. External SDK headers were located;
SDK implementation and transitive dependencies are not included in this review
or licensed by this project's MIT file.

## Full builds

On 2 October 2026, the FullGate master, hardware-slot scanner and NVS-ID scanner
each compiled, linked and generated an ESP32-C5 firmware image successfully with
ESP-IDF 5.5.3 after repair of the local Python environment. The starting firmware
references 5.5.1; that SDK version was not re-tested. Generated binaries remain
outside this source package.

The refreshed generated configurations confirm the intended roles and capacity
20 for the master and both scanner variants, disabled straps for NVS, SSD1306
selection for the master, and 8 MB flash. The optional raw SD driver, bench
diagnostics and pin finder are disabled. All three refreshed builds completed
without compiler or CMake warnings.

Active SD mount-failure GPIO diagnostic definitions and calls are now gated by
the disabled-by-default bench option. Normal mounting and recording are unchanged.
All three variants were rebuilt successfully after this correction. Disassembly
of the default master's storage object confirms that the pin-driving diagnostic
functions and calls are absent. A deliberate SD-failure hardware test was not run.

## Two-board bench test and capacity adjustment

The master and one hardware-slot scanner were flashed and tested together on
2 October 2026. Target selftests passed, the scanner read slot 1, ESP-NOW records
arrived, and the master mounted the SD card. A controlled scanner reset produced
one down event and one restart, followed by resumed records without a master
restart. Hotspot connection, web Stop/save, and starting another session worked.
The two files closed with 818 and 298 rows reported saved, with zero reported SD
errors. Reading the actual files from the card remains pending.

The GNSS antenna was added during the test. The first session exercised no-fix
logging; a GNSS fix was observed later. This was a short two-board bench check,
not a field test or a 20-scanner throughput test.

After that test, the remaining 9-node Kconfig default and tally fallback were
changed to 20, and the hardware-slot scanner overlay now explicitly selects 20.
The master and NVS scanner overlays already selected 20. Four strap pins still
encode only slots 1-15; standalone scanners use provisioned NVS IDs. All three
variants were rebuilt and their generated settings checked after this adjustment.
The revised scanner image has not yet been tested on hardware.

## Verification limits

Only the limited hardware checks above have been performed; nothing was published.
Compilation alone does not establish radio or storage reliability.
Ten cross-compiler syntax checks passed without diagnostics; host tests were not
executed. The local page passed JavaScript syntax checking and the provisioning
script passed Python parsing; neither contacted hardware. Actual HG Kconfig
parsing confirmed master/strap-scanner/NVS-scanner roles, ID limits, display
selection and strap/NVS settings. All retained overlay values match the starting
firmware apart from intentional scanner-bus/shared-display-bus removals and the
hardware-slot scanner capacity adjustment described above.
These checks and successful builds do not replace hardware verification.

Other experimental reliability/privacy limitations are documented in README.
The final source changes do not yet have a repeat hardware test, and the actual
SD files have not yet been read back. These limits remain open for community
testing; successful compilation is not a claim of production readiness.
