// ssd1306 and sh1106 panel driver
// the controller wants a control byte in front of every transfer, 0x00 says
// commands follow and 0x40 says pixel data follows

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "screen.h"
#include "transport.h"

static const char *tag = "screen";

#ifdef CONFIG_HG_SCREEN

#include "font5x7.h"

#define SCREEN_W      128

// Probe both supported display addresses.
static const uint8_t addrs[] = { 0x3c, 0x3d };
static uint8_t addr = 0x3c;
#define SCREEN_PAGES  SCREEN_LINES

// one blank column between characters, so a glyph occupies six
#define GLYPH_W       (FONT_W + 1)


static int ready;
static int started;

// writes that failed in a row. a few means the panel has gone, usually its
// power, and it is looked for again rather than written to forever
static int failures;


// Dedicated software-driven display bus.

#include "driver/gpio.h"
#include "esp_rom_sys.h"

#define SDA CONFIG_HG_SCREEN_SDA_GPIO
#define SCL CONFIG_HG_SCREEN_SCL_GPIO

// Delay after each software-driven line transition; not an exact bus rate.
#define TICK_US 3

// open drain. drive low, or let the pull up take it high
static void line(int pin, int high)
{
    gpio_set_level(pin, high ? 1 : 0);
    esp_rom_delay_us(TICK_US);
}

static void bus_setup(void)
{
    for (int i = 0; i < 2; i++) {
        int pin = i ? SCL : SDA;
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
        gpio_set_level(pin, 1);
    }
}

static void soft_start(void)
{
    line(SDA, 1);
    line(SCL, 1);
    line(SDA, 0);
    line(SCL, 0);
}

static void soft_stop(void)
{
    line(SDA, 0);
    line(SCL, 1);
    line(SDA, 1);
}

// returns 1 if the panel acknowledged
static int soft_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        line(SDA, (b & 0x80) != 0);
        line(SCL, 1);
        line(SCL, 0);
        b <<= 1;
    }

    // release sda and read what the panel does with it
    line(SDA, 1);
    line(SCL, 1);
    int ack = gpio_get_level(SDA) == 0;
    line(SCL, 0);

    return ack;
}

// an ack is sda read low, so a line that sits low or cannot rise makes every
// byte look acknowledged and a missing panel looks present. read both lines
// released, then send an address nothing can sit at. a real bus never
// acknowledges it
#define NOBODY 0x7f

static int bus_is_real(int loud)
{
    // a reset in the middle of a transfer can leave a powered panel holding sda
    // low, waiting for clocks that never came. clock only while it is held and
    // stop as soon as it lets go, then send a stop. a fixed count can land on
    // the panel's next ack and swallow the stop, leaving the line down
    gpio_set_level(SDA, 1);
    line(SCL, 0);
    for (int i = 0; i < 9 && gpio_get_level(SDA) == 0; i++) {
        line(SCL, 1);
        line(SCL, 0);
    }
    soft_stop();

    esp_rom_delay_us(1000);

    int sda = gpio_get_level(SDA);
    int scl = gpio_get_level(SCL);

    soft_start();
    int phantom = soft_byte(NOBODY << 1);
    soft_stop();

    if (sda && scl && !phantom)
        return 1;

    if (loud)
        ESP_LOGW(tag, "panel bus on sda %d scl %d is not usable. released they read %d and %d, and an address nothing uses %s. a 0 means something is holding that line down, an answer with both at 1 means sda is not rising in time",
             SDA, SCL, sda, scl, phantom ? "read as answered" : "did not answer");
    return 0;
}

static esp_err_t panel_write(const uint8_t *buf, size_t len)
{
    soft_start();

    if (!soft_byte(addr << 1)) {
        soft_stop();
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < len; i++) {
        if (!soft_byte(buf[i])) {
            soft_stop();
            return ESP_FAIL;
        }
    }

    soft_stop();
    return ESP_OK;
}


// a page of the panel is one row of text, so the buffer and the layout are the
// same shape and no glyph is ever split across two writes
static uint8_t fb[SCREEN_PAGES][SCREEN_W];

// One dirty bit per display page; only changed pages are transferred.
static uint8_t dirty;

// whichever two wires the panel is actually on, for the log
#define PANEL_SDA CONFIG_HG_SCREEN_SDA_GPIO
#define PANEL_SCL CONFIG_HG_SCREEN_SCL_GPIO

static esp_err_t write_cmds(const uint8_t *cmds, size_t len)
{
    uint8_t buf[24];

    if (len + 1 > sizeof buf)
        return ESP_ERR_INVALID_SIZE;

    buf[0] = 0x00;
    memcpy(buf + 1, cmds, len);

    return panel_write(buf, len + 1);
}

// the panel wakes with its charge pump off and takes every other command
// happily while staying completely dark, so getting the pump command right
// for the controller is the whole game. the two parts share everything
// else in this list
static const uint8_t init[] = {
    0xae,               // display off while we set it up
    0xa8, 0x3f,         // multiplex ratio, 64 rows
    0xd3, 0x00,         // no display offset
    0x40,               // start line 0
    0xa1,               // column 127 on the left, the usual module orientation
    0xc8,               // scan rows backwards, same reason
    0xda, 0x12,         // alternate pin layout, this is a 128x64 not a 128x32
    0x81, 0x7f,         // contrast, middle
    0xa4,               // follow ram rather than force every pixel on
    0xa6,               // not inverted
    0xd5, 0x80,         // clock divide and oscillator
#ifdef CONFIG_HG_SCREEN_SH1106
    0xad, 0x8b,         // dc to dc converter on
#else
    0x20, 0x02,         // page addressing. an ssd1306 defaults to horizontal
                        // and would then wrap the whole screen on one write
    0x8d, 0x14,         // charge pump on
#endif
    0xaf,               // display on
};


// looks for the panel at either address and sets it up. tries is how many
// rounds a hundred ms apart, loud says whether a miss is worth a log line. at
// boot it gets a second and says what it found. after that it is called every
// couple of seconds and stays quiet until the panel turns up
static esp_err_t panel_find(int tries, int loud)
{
    int here = 0;

    if (!bus_is_real(loud))
        return ESP_ERR_NOT_FOUND;

    for (int t = 0; t < tries && !here; t++) {
        for (size_t i = 0; i < sizeof addrs && !here; i++) {
            soft_start();
            here = soft_byte(addrs[i] << 1);
            soft_stop();

            if (here)
                addr = addrs[i];
        }

        if (!here && t + 1 < tries)
            vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!here) {
        if (loud)
            ESP_LOGW(tag, "no panel at 0x3c or 0x3d on sda %d scl %d, will keep looking",
                     PANEL_SDA, PANEL_SCL);
        return ESP_ERR_NOT_FOUND;
    }

    // it has already answered its address, so this normally goes first time.
    // the retries cover a panel that answers before it will take commands
    esp_err_t err = ESP_FAIL;

    for (int t = 0; t < 10; t++) {
        err = write_cmds(init, sizeof init);
        if (err == ESP_OK)
            break;

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (err != ESP_OK) {
        if (loud)
            ESP_LOGE(tag, "panel answered at 0x%02x but would not take its setup, %s",
                     addr, esp_err_to_name(err));
        return err;
    }

    ready = 1;

    // a panel that has just powered up holds whatever was in its memory, which
    // is noise. every page is sent again, which covers the whole screen
    dirty = 0xff;
    screen_flush();

    ESP_LOGI(tag, "panel up at 0x%02x, %d by %d, %d lines of %d",
             addr, SCREEN_W, SCREEN_PAGES * 8, SCREEN_LINES, SCREEN_COLS);
    return ESP_OK;
}

esp_err_t screen_start(void)
{
    bus_setup();

    ESP_LOGI(tag, "panel on a software bus, sda %d scl %d",
             CONFIG_HG_SCREEN_SDA_GPIO, CONFIG_HG_SCREEN_SCL_GPIO);

    started = 1;

    // a panel holds its own reset for a while after power comes up, so give it
    // a second at boot
    return panel_find(10, 1);
}

// a panel with no power at boot, which is what a usb first start looks like,
// or one that lost power since, is looked for again every couple of seconds
// and set up the moment it answers. nothing needs a reset
void screen_retry(void)
{
    static int64_t last_us;

    if (!started || ready)
        return;

    int64_t now = esp_timer_get_time();
    if (now - last_us < 2000000)
        return;

    last_us = now;
    panel_find(1, 0);
}

void screen_clear(void)
{
    memset(fb, 0, sizeof fb);
    dirty = 0xff;
}

void screen_line(int line, const char *text)
{
    if (line < 0 || line >= SCREEN_LINES)
        return;

    // the whole row is drawn every time, blanks included, so a caller never has
    // to clear first and the tail of a longer line before it cannot survive
    uint8_t row[SCREEN_W];
    memset(row, 0, sizeof row);

    int x = 0;

    for (; text != NULL && *text != '\0' && x + FONT_W <= SCREEN_W; text++) {
        unsigned char c = (unsigned char)*text;

        // anything outside the font shows as a question mark rather than
        // reading out of the table
        if (c < FONT_FIRST || c > FONT_LAST)
            c = '?';

        memcpy(row + x, font5x7[c - FONT_FIRST], FONT_W);
        x += GLYPH_W;
    }

    if (memcmp(fb[line], row, SCREEN_W) == 0)
        return;

    memcpy(fb[line], row, SCREEN_W);
    dirty |= (uint8_t)(1u << line);
}

void screen_flush(void)
{
    if (!ready || dirty == 0)
        return;

    uint8_t out[1 + SCREEN_W];
    for (int page = 0; page < SCREEN_PAGES; page++) {
        // Prefix every page transfer with the display-data control byte.
        out[0] = 0x40;

        if ((dirty & (1u << page)) == 0)
            continue;

        // page and column are set by hand every time. an sh1106 has 132
        // columns and shows the middle 128, which is what the offset is for
        uint8_t at[] = {
            (uint8_t)(0xb0 + page),
            (uint8_t)(0x00 | (CONFIG_HG_SCREEN_COL_OFFSET & 0x0f)),
            (uint8_t)(0x10 | (CONFIG_HG_SCREEN_COL_OFFSET >> 4)),
        };

        memcpy(out + 1, fb[page], SCREEN_W);

        if (write_cmds(at, sizeof at) != ESP_OK ||
            panel_write(out, sizeof out) != ESP_OK) {
            if (++failures >= 3) {
                ready = 0;
                failures = 0;
                ESP_LOGW(tag, "panel stopped answering, looking for it again");
            }
            return;
        }

        failures = 0;

        // cleared one at a time. a failure part way through leaves the pages
        // that did not go out still marked, so the next pass picks them up
        dirty &= (uint8_t)~(1u << page);
    }
}

int screen_ready(void)
{
    return ready;
}

#else

esp_err_t screen_start(void)
{
    ESP_LOGI(tag, "not built in");
    return ESP_ERR_NOT_SUPPORTED;
}

void screen_line(int line, const char *text) { (void)line; (void)text; }
void screen_clear(void) { }
void screen_flush(void) { }
void screen_retry(void) { }
int screen_ready(void) { return 0; }

#endif
