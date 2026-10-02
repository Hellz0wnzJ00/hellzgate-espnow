// where a scanner gets its node id from
//
// Strap builds require a valid hardware slot. Other builds read NVS, then
// fall back to the compiled ID. HG_REQUIRE_ID blocks transmission unless the
// ID is provisioned, strapped, or explicitly trusted by the build settings.

#ifndef SLOTID_H
#define SLOTID_H

#include <stdint.h>

// reads the id and says in the log where it came from. call once at boot
void slotid_init(void);

// Zero-based scanner ID; valid IDs are below the configured cluster size.
uint8_t slotid_get(void);

// 1 for an NVS ID; 0 for hardware straps or the compiled fallback.
int slotid_from_nvs(void);

// True for straps, NVS, or a build ID explicitly trusted in configuration.
// HG_REQUIRE_ID uses this to prevent unprovisioned scanners from enrolling.
int slotid_known(void);

#endif
