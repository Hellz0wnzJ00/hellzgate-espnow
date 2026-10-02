// ssd1306 or sh1106 128x64 over i2c
// Uses a dedicated software-driven display bus at address 0x3c or 0x3d.

#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

#include "esp_err.h"

// eight lines of twenty one characters. the font is five pixels wide with one
// of space, so 21 by 6 is 126 of the 128 columns
#define SCREEN_LINES 8
#define SCREEN_COLS  21

// Initializes the dedicated display bus and panel. If no panel answers,
// screen_retry can detect it later without restarting the master.
esp_err_t screen_start(void);

// looks for a missing panel again every couple of seconds and sets it up when
// it answers. call it from the redraw loop, it does nothing once one is up
void screen_retry(void);

// Updates one buffered text line. screen_flush transfers the dirty pages.
void screen_line(int line, const char *text);

void screen_clear(void);
void screen_flush(void);

int screen_ready(void);

#endif
