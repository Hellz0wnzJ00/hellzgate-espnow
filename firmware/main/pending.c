// observations held until the receiver gives us a date

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "pending.h"
#include "storage.h"

#ifdef CONFIG_HG_GNSS

static const char *tag = "pending";

typedef struct {
    hg_record_t rec;
    int64_t at_us;
    double lat;
    double lon;
    double alt_m;
    double accuracy_m;
    uint8_t have_fix;
} held;

static held *rows;
static uint32_t count;
static uint32_t sent;      // rows from the front already written
static int warned;

static int room(void)
{
    if (rows != NULL)
        return 1;

    // psram, because a busy start can be tens of thousands of rows and there is
    // nowhere near that much internal memory
    rows = heap_caps_malloc(sizeof(held) * CONFIG_HG_PENDING_MAX,
                            MALLOC_CAP_SPIRAM);

    if (rows == NULL) {
        if (!warned) {
            warned = 1;
            ESP_LOGE(tag, "no psram for %d held rows, early rows will go out with no timestamp",
                     CONFIG_HG_PENDING_MAX);
        }
        return 0;
    }

    ESP_LOGI(tag, "holding early rows until the receiver gives a date, room for %d",
             CONFIG_HG_PENDING_MAX);
    return 1;
}

int pending_add(const hg_record_t *r, const csv_fix *fix, int64_t at_us)
{
    if (!room() || count >= CONFIG_HG_PENDING_MAX)
        return -1;

    held *h = &rows[count++];

    h->rec = *r;
    h->at_us = at_us;
    h->have_fix = fix->have_fix ? 1 : 0;
    h->lat = fix->lat;
    h->lon = fix->lon;
    h->alt_m = fix->alt_m;
    h->accuracy_m = fix->accuracy_m;

    return 0;
}

uint32_t pending_count(void)   { return count - sent; }

int pending_gave_up(void)
{
    return esp_timer_get_time() > (int64_t)CONFIG_HG_PENDING_WAIT_S * 1000000;
}

void pending_discard(void)
{
    if (count)
        ESP_LOGW(tag, "dropping %lu rows still waiting for a date, the run ended first",
                 (unsigned long)count);

    count = 0;
    sent = 0;

    if (rows != NULL) {
        heap_caps_free(rows);
        rows = NULL;
    }
}

static void write_one(const held *h, int64_t boot_unix, pending_emit emit)
{
    csv_fix fix;
    memset(&fix, 0, sizeof fix);

    fix.have_fix = h->have_fix;
    fix.lat = h->lat;
    fix.lon = h->lon;
    fix.alt_m = h->alt_m;
    fix.accuracy_m = h->accuracy_m;

    // a zero base means no date ever arrived. the row still goes out, it just
    // goes out without a timestamp rather than with a fake one
    emit(&h->rec, &fix, boot_unix > 0 ? boot_unix + h->at_us / 1000000 : 0);
}

uint32_t pending_flush_some(int64_t boot_unix, pending_emit emit, uint32_t max)
{
    if (sent == count)
        return 0;

    if (sent == 0)
        ESP_LOGI(tag, "writing %lu held rows, timed from %lld",
                 (unsigned long)count, (long long)boot_unix);

    uint32_t n = 0;

    while (sent < count && n < max) {
        write_one(&rows[sent], boot_unix, emit);
        sent++;
        n++;
    }

    // the card syncs on its own schedule through a long backlog rather than
    // everything being held to the end
    storage_tick();

    if (sent < count)
        return n;

    ESP_LOGI(tag, "held rows all written");

    count = 0;
    sent = 0;
    heap_caps_free(rows);
    rows = NULL;
    return n;
}

void pending_flush(int64_t boot_unix, pending_emit emit)
{
    // everything at once, for a run that is ending. a full buffer is tens of
    // thousands of rows and this runs on the web task, so give the cpu up
    // between chunks or the watchdog fires on the idle task
    while (pending_flush_some(boot_unix, emit, 256))
        vTaskDelay(1);
}

#else

int pending_add(const hg_record_t *r, const csv_fix *fix, int64_t at_us)
{
    (void)r; (void)fix; (void)at_us;
    return -1;
}

uint32_t pending_count(void)   { return 0; }

uint32_t pending_flush_some(int64_t boot_unix, pending_emit emit, uint32_t max)
{
    (void)boot_unix; (void)emit; (void)max;
    return 0;
}
int pending_gave_up(void)      { return 1; }

void pending_flush(int64_t boot_unix, pending_emit emit)
{
    (void)boot_unix; (void)emit;
}

void pending_discard(void) { }

#endif
