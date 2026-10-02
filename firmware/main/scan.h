// passive wifi scan on both bands
// fills the locked record and hands each one to a sink, nothing here knows
// about transports

#ifndef SCAN_H
#define SCAN_H

#include <stdint.h>

#include "hg_record.h"

typedef void (*scan_sink)(const hg_record_t *r);

// brings up wifi in station mode and parks on the espnow channel
void radio_init(void);

// who we are and where the records go, does not scan yet
void scan_init(uint8_t node_id, scan_sink sink);

// lets the link interrupt a chunk. scanning got slower once ble started
// sharing the radio, so a fixed chunk size is not a safe way to guarantee the
// node checks in on time. instead the scan asks after every channel whether
// the link needs the radio, and if it does it goes home and hands it over
void scan_set_yield(int (*due)(void), void (*run)(void));

// scans one chunk of channels then comes back to the espnow channel so the
// node can check in. returns 1 on the step that finishes a full sweep.
// Bounded chunks and between-channel yields give transport service a chance
// to run without waiting for the entire dual-band sweep.
int scan_step(void);

// Elapsed time for the last complete sweep, including intervening service.
uint32_t scan_last_sweep_ms(void);

#endif
