// the rows held before the receiver has a date. they go out a chunk at a time
// once it does, new rows join the back while the backlog drains, and every one
// comes out once, in order, with the time it was actually taken

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pending.h"

int hg_test_verbose = 0;
int64_t hg_test_now_us = 0;

static int ticks;
void storage_tick(void) { ticks++; }

static uint32_t out_counter[1000];
static int64_t out_time[1000];
static int nout;

static void emit(const hg_record_t *r, const csv_fix *fix, int64_t unix_sec)
{
    (void)fix;
    out_counter[nout] = r->bssid[0] | (r->bssid[1] << 8);
    out_time[nout] = unix_sec;
    nout++;
}

static uint32_t next;

static void hold_at(int64_t at_us)
{
    hg_record_t r;
    memset(&r, 0, sizeof r);
    r.bssid[0] = (uint8_t)next;
    r.bssid[1] = (uint8_t)(next >> 8);
    next++;

    csv_fix fix;
    memset(&fix, 0, sizeof fix);
    assert(pending_add(&r, &fix, at_us) == 0);
}

int main(void)
{
    // sixty rows before the date, a second apart
    for (int i = 0; i < 60; i++)
        hold_at((int64_t)i * 1000000);
    assert(pending_count() == 60);

    // the date arrives. boot was at unix 1000000, so row i was taken at
    // 1000000 + i. it drains 16 at a time, and new rows keep arriving
    int64_t boot = 1000000;
    uint32_t n = pending_flush_some(boot, emit, 16);
    assert(n == 16);
    assert(nout == 16);
    assert(pending_count() == 44);

    for (int i = 60; i < 70; i++)
        hold_at((int64_t)i * 1000000);
    assert(pending_count() == 54);

    while (pending_flush_some(boot, emit, 16))
        ;

    assert(pending_count() == 0);
    assert(nout == 70);

    // every row once, in the order it was taken, with its own time
    for (int i = 0; i < 70; i++) {
        assert(out_counter[i] == (uint32_t)i);
        assert(out_time[i] == boot + i);
    }
    printf("  drained in chunks, in order, times right          ok\n");

    // the buffer is freed once empty and comes back when needed
    hold_at(0);
    assert(pending_count() == 1);
    pending_flush(boot, emit);
    assert(pending_count() == 0 && nout == 71);

    // a full buffer refuses rather than overwriting, the caller then writes
    // the row itself
    for (int i = 0; i < CONFIG_HG_PENDING_MAX; i++)
        hold_at(0);
    hg_record_t extra;
    memset(&extra, 0, sizeof extra);
    csv_fix fix;
    memset(&fix, 0, sizeof fix);
    assert(pending_add(&extra, &fix, 0) != 0);
    assert(pending_count() == CONFIG_HG_PENDING_MAX);
    printf("  full buffer refuses rather than overwriting       ok\n");

    // no date ever, the wait ran out. they go out with an empty timestamp
    nout = 0;
    pending_flush(0, emit);
    assert(nout == CONFIG_HG_PENDING_MAX);
    for (int i = 0; i < nout; i++)
        assert(out_time[i] == 0);
    printf("  no date, rows go out with no timestamp            ok\n");

    // a run that ends clears everything
    hold_at(0);
    pending_discard();
    assert(pending_count() == 0);
    printf("  discard clears the rows                           ok\n");

    printf("pending tests ok\n");
    return 0;
}
