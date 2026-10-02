#ifndef SEMPHR_H
#define SEMPHR_H

#include <stdlib.h>

#include "FreeRTOS.h"

// one thread in the test. a take on an empty semaphore returns at once rather
// than blocking, which is what a timeout that ran out looks like
typedef struct { int count; int max; } *SemaphoreHandle_t;

static inline SemaphoreHandle_t hg_sem_new(int count, int max)
{
    SemaphoreHandle_t s = malloc(sizeof *s);
    s->count = count;
    s->max = max;
    return s;
}

#define xSemaphoreCreateBinary() hg_sem_new(0, 1)
#define xSemaphoreCreateMutex()  hg_sem_new(1, 1)

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
    (void)wait;
    if (s->count == 0)
        return pdFALSE;
    s->count--;
    return pdTRUE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    if (s->count < s->max)
        s->count++;
    return pdTRUE;
}

#endif
