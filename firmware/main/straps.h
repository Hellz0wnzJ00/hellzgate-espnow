// slot address straps on a scanner
//
// four pins hold the physical slot number. slot 0 is not valid, so a board that
// is not seated properly reads as an error instead of taking another slot's
// address. the top of the range follows the cluster size, capped at 15 because
// that is all four pins reach.
//
// each pin is read twice, once pulled up and once pulled down. the slot hardware
// ties every address pin hard to 3v3 or ground, so a seated pin gives the same
// level both ways and an open pin follows the pull.

#ifndef STRAPS_H
#define STRAPS_H

#include <stdint.h>

// reads the four pins once. returns the slot, or 0 if it is not valid
uint8_t straps_read_slot(void);

// raw value on the pins, 0 to 15, for logging when the slot is rejected
uint8_t straps_raw(void);

// one bit per address pin that read as open on the last call
uint8_t straps_floating(void);

#endif
