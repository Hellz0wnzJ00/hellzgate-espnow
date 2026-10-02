// run start, stop and naming
// deliberately thin. it holds the state, calls the card underneath and answers
// questions from above. anything cleverer belongs in the layer that asked

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "gnss.h"
#include "session.h"
#include "storage.h"

static const char *tag = "session";

static char name[SESSION_NAME_MAX];
static int running;
static int64_t started_unix;
static int64_t started_us;
static uint32_t records;
static int opened;
static void (*settle_held)(void);

// recursive, because stopping settles held rows and that path comes back
// through code already holding this. made on first use, which is safe only
// because app_main starts the run before the web and screen tasks exist
static SemaphoreHandle_t gate;

void session_hold(void)
{
    if (gate == NULL)
        gate = xSemaphoreCreateRecursiveMutex();

    if (gate != NULL)
        xSemaphoreTakeRecursive(gate, portMAX_DELAY);
}

void session_release(void)
{
    if (gate != NULL)
        xSemaphoreGiveRecursive(gate);
}

esp_err_t session_start(const char *want)
{
    session_hold();

    if (running)
        session_stop();

    memset(name, 0, sizeof name);

    if (want != NULL)
        strncpy(name, want, sizeof name - 1);

    if (name[0] == '\0')
        strcpy(name, "run");

    running = 1;
    records = 0;
    started_us = esp_timer_get_time();

    // zero here means the receiver had no date yet when the run began. the file
    // still gets a unique name, storage falls back to uptime for that
    started_unix = gnss_unix();

    // the file is not opened here. it opens on the first row, by which time the
    // receiver has usually given us a date and the name can carry it
    opened = 0;

    ESP_LOGI(tag, "run %s started", name);

    session_release();
    return ESP_OK;
}

void session_set_start(int64_t boot_unix)
{
    if (boot_unix <= 0 || started_unix > 0)
        return;

    started_unix = boot_unix + started_us / 1000000;
}

void session_ensure_file(void)
{
    session_hold();

    if (!running || opened) {
        session_release();
        return;
    }

    opened = 1;

    // no card is not a reason to refuse a run. the console still carries every
    // row and someone capturing off the port gets the same file
    if (storage_open(name, started_unix) != ESP_OK)
        ESP_LOGW(tag, "run %s is console only, the card is not available", name);

    session_release();
}

void session_on_stop(void (*settle)(void))
{
    settle_held = settle;
}

void session_stop(void)
{
    session_hold();

    if (!running) {
        session_release();
        return;
    }

    // held rows belong to this run, so they go in before the file closes
    if (settle_held != NULL)
        settle_held();

    storage_close();
    running = 0;
    opened = 0;

    ESP_LOGI(tag, "run %s stopped after %lu records",
             name, (unsigned long)records);

    session_release();
}

int session_running(void)
{
    return running;
}

void session_record(void)
{
    if (running)
        records++;
}

void session_state(session_info *out)
{
    // the web task asks for this while the collect task can be starting or
    // stopping a run, and a start memsets the name
    session_hold();

    memset(out, 0, sizeof *out);

    strncpy(out->name, name, sizeof out->name - 1);
    out->running = running;
    out->on_card = running && storage_open_now();
    out->started_unix = started_unix;
    out->records = records;

    if (running)
        out->seconds = (uint32_t)((esp_timer_get_time() - started_us) / 1000000);

    session_release();
}
