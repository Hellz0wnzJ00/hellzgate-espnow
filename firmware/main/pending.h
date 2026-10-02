// observations taken before the receiver has given us a date
//
// the chip has no clock of its own, so early rows used to go out stamped from a
// fixed base and the export carried dates in 2020. they are held here instead,
// and written with their real time once the date arrives. the gap between each
// one and the fix is measured on the monotonic timer, so the times are right
// and not just plausible

#ifndef PENDING_H
#define PENDING_H

#include <stdint.h>

#include "csv.h"
#include "hg_record.h"

// how a held row gets written once its time is known. the fix carries no
// timestamp, the caller formats that from unix_sec
typedef void (*pending_emit)(const hg_record_t *r, const csv_fix *fix,
                             int64_t unix_sec);

// returns 0 if it was held, and non zero if the buffer is full. the caller then
// writes the row rather than lose it
int pending_add(const hg_record_t *r, const csv_fix *fix, int64_t at_us);

// rows held and not yet written
uint32_t pending_count(void);

// true once we have waited long enough that a receiver is clearly not coming.
// after this rows go straight out with the timestamp column empty
int pending_gave_up(void);

// writes up to max held rows, oldest first, timed from when the boot happened.
// returns how many went. the master loop calls this a chunk at a time, so a
// backlog of thousands never holds the collect path up long enough for the
// scanners to overflow
uint32_t pending_flush_some(int64_t boot_unix, pending_emit emit, uint32_t max);

// writes everything held. only for a run that is ending
void pending_flush(int64_t boot_unix, pending_emit emit);

// throws away anything still held. a run that ends while rows are waiting must
// not leave them to turn up in the next one. settle them first
void pending_discard(void);

#endif
