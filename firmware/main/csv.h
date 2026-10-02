// wigle style csv export
// the escaping is the whole point here, an ssid carrying a comma or a quote
// must never be able to shift the columns

#ifndef CSV_H
#define CSV_H

#include <stddef.h>
#include <stdint.h>

#include "hg_record.h"

// Display/status fallback before GNSS supplies a date. CSV observations use
// gnss_unix() separately and leave unknown timestamps empty.
#define HG_TIME_BASE 1577836800LL   // 2020-01-01 00:00:00 utc

// what the master stamps on a row when there is no fix on the record itself
typedef struct {
    double lat;
    double lon;
    double alt_m;
    double accuracy_m;
    const char *first_seen;  // caller formats the time, we only place it
    int have_fix;
} csv_fix;

// all three follow the snprintf contract, they return the length the output
// needs without the terminator, so call with out_size 0 to size a buffer
int csv_header(char *out, size_t out_size);

// formats a unix time the way wigle wants FirstSeen, YYYY-MM-DD HH:MM:SS
int csv_time(char *out, size_t out_size, int64_t unix_sec);

// Display/status clock: GNSS time when available, otherwise base plus uptime.
// The fallback is not a real date and must not timestamp exported observations.
int64_t csv_now(void);

// Copies a fresh GNSS fix, or clears have_fix when missing/stale.
// csv_row writes zero coordinates when have_fix is false.
void csv_current_fix(csv_fix *out, const char *first_seen);
int csv_row(char *out, size_t out_size, const hg_record_t *r, const csv_fix *fix);
int csv_field(char *out, size_t out_size, const char *in, size_t in_len);

#endif
