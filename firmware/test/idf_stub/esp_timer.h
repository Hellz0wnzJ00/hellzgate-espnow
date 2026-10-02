#ifndef ESP_TIMER_H
#define ESP_TIMER_H

#include <stdint.h>

// a clock the test moves by hand
extern int64_t hg_test_now_us;

static inline int64_t esp_timer_get_time(void)
{
    return hg_test_now_us;
}

#endif
