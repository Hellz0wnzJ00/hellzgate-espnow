// experimental raw SD card driver over SPI
// Optional block-driver implementation; the ESP-IDF driver is the default.
// Enable only for diagnostics after checking the documented limitations.
//
// this is the block layer only. it knows nothing about files. fatfs sits on top
// of it through the four disk callbacks and everything above that is unchanged

#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

#include "esp_err.h"

#define SDCARD_SECTOR 512

// brings the bus up, resets the card and takes it into spi mode. safe to call
// twice, the second call is a no op once a card is up
esp_err_t sdcard_init(void);

int sdcard_ready(void);

// Reported capacity in 512-byte sectors; 0 also means capacity is unknown.
uint32_t sdcard_sectors(void);

// "SDSC", "SDHC" or "unknown". for the log and the status page
const char *sdcard_type(void);

esp_err_t sdcard_read(uint32_t sector, uint8_t *out, uint32_t count);
esp_err_t sdcard_write(uint32_t sector, const uint8_t *in, uint32_t count);

// brings the card up and puts a fat filesystem on top of it at mount_point.
// after this, fopen and the rest of the c library work as normal
esp_err_t sdcard_mount(const char *mount_point);

#endif
