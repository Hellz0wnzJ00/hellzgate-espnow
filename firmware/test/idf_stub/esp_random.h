#ifndef ESP_RANDOM_H
#define ESP_RANDOM_H

#include <stdint.h>

// a different number every call, which is all the boot number needs
static inline uint32_t esp_random(void)
{
    static uint32_t x = 0x2545f491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

#endif
