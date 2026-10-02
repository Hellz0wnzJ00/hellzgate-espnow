#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

#include "esp_err.h"

#define GPIO_MODE_INPUT        1
#define GPIO_MODE_OUTPUT       2
#define GPIO_MODE_INPUT_OUTPUT 3
#define GPIO_PULLUP_ONLY       0

// Synthetic input level controlled by the host test.
extern int hg_test_line_level;

static inline esp_err_t gpio_reset_pin(int p) { (void)p; return ESP_OK; }
static inline esp_err_t gpio_set_direction(int p, int m) { (void)p; (void)m; return ESP_OK; }
static inline esp_err_t gpio_set_pull_mode(int p, int m) { (void)p; (void)m; return ESP_OK; }
static inline int gpio_get_level(int p) { (void)p; return hg_test_line_level; }

static inline void esp_rom_delay_us(uint32_t us) { (void)us; }

#endif
