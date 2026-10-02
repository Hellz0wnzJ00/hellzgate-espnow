#ifndef QUEUE_H
#define QUEUE_H

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"

typedef struct {
    unsigned char *buf;
    size_t item;
    size_t len, head, tail, used;
} *QueueHandle_t;

static inline QueueHandle_t xQueueCreate(size_t len, size_t item)
{
    QueueHandle_t q = malloc(sizeof *q);
    q->buf = malloc(len * item);
    q->item = item;
    q->len = len;
    q->head = q->tail = q->used = 0;
    return q;
}

static inline BaseType_t xQueueSend(QueueHandle_t q, const void *p, TickType_t wait)
{
    (void)wait;
    if (q->used == q->len)
        return pdFALSE;
    memcpy(q->buf + q->head * q->item, p, q->item);
    q->head = (q->head + 1) % q->len;
    q->used++;
    return pdTRUE;
}

static inline BaseType_t xQueueReceive(QueueHandle_t q, void *p, TickType_t wait)
{
    (void)wait;
    if (q->used == 0)
        return pdFALSE;
    memcpy(p, q->buf + q->tail * q->item, q->item);
    q->tail = (q->tail + 1) % q->len;
    q->used--;
    return pdTRUE;
}

#endif
