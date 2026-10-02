#ifndef TASK_H
#define TASK_H

#include "FreeRTOS.h"

extern int64_t hg_test_now_us;

static inline void vTaskDelay(TickType_t ms)
{
    hg_test_now_us += (int64_t)ms * 1000;
}

// the tasks are driven by hand from the test, never started
static inline BaseType_t xTaskCreate(void (*fn)(void *), const char *name,
                                     uint32_t stack, void *arg, int prio,
                                     void *handle)
{
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)handle;
    return pdPASS;
}

#endif
