#define _GNU_SOURCE
#include "../include/common.h"
#include <string.h>

const char *ING_NAMES[MAX_ING] = {
    "pan", "tomate", "cebolla", "lechuga", "queso", "carne"
};

int can_band_fulfill(const BandStatus *b, const Order *o) {
    for (int i = 0; i < MAX_ING; ++i) {
        if (o->ing[i] > b->inv[i]) return 0;
    }
    return 1;
}

void consume_inventory(BandStatus *b, const Order *o) {
    for (int i = 0; i < MAX_ING; ++i) {
        b->inv[i] -= o->ing[i];
        if (b->inv[i] < 0) b->inv[i] = 0; // seguridad
    }
}

int can_band_fulfill_locked(BandStatus *b, const Order *o) {
    int ok;
    sem_wait(&b->band_mutex);
    ok = can_band_fulfill(b, o);
    sem_post(&b->band_mutex);
    return ok;
}

void consume_inventory_locked(BandStatus *b, const Order *o) {
    sem_wait(&b->band_mutex);
    consume_inventory(b, o);
    sem_post(&b->band_mutex);
}

static void queue_reset(OrderQueue *q) {
    q->head = q->tail = q->count = 0;
}

void queue_init(OrderQueue *q, int capacity) {
    (void)capacity;
    queue_reset(q);
    sem_init(&q->mutex, 1, 1);
    sem_init(&q->items, 1, 0);
    sem_init(&q->spaces, 1, MAX_ORDERS);
}

void queue_destroy(OrderQueue *q) {
    sem_destroy(&q->mutex);
    sem_destroy(&q->items);
    sem_destroy(&q->spaces);
}

int queue_push(OrderQueue *q, const Order *o, int capacity, int block) {
    if (block) sem_wait(&q->spaces);
    else if (sem_trywait(&q->spaces) != 0) return -1;

    sem_wait(&q->mutex);
    q->buf[q->tail] = *o;
    q->tail = (q->tail + 1) % capacity;
    q->count++;
    sem_post(&q->mutex);
    sem_post(&q->items);
    return 0;
}

int queue_pop(OrderQueue *q, Order *o, int capacity, int block) {
    if (block) sem_wait(&q->items);
    else if (sem_trywait(&q->items) != 0) return -1;

    sem_wait(&q->mutex);
    *o = q->buf[q->head];
    q->head = (q->head + 1) % capacity;
    q->count--;
    sem_post(&q->mutex);
    sem_post(&q->spaces);
    return 0;
}

static void bqueue_reset(BandQueue *q) {
    q->head = q->tail = q->count = 0;
}

void bqueue_init(BandQueue *q, int capacity) {
    (void)capacity;
    bqueue_reset(q);
    sem_init(&q->mutex, 1, 1);
    sem_init(&q->items, 1, 0);
    sem_init(&q->spaces, 1, MAX_PER_BAND_QUEUE);
}

void bqueue_destroy(BandQueue *q) {
    sem_destroy(&q->mutex);
    sem_destroy(&q->items);
    sem_destroy(&q->spaces);
}

int bqueue_push(BandQueue *q, const Order *o, int capacity, int block) {
    if (block) sem_wait(&q->spaces);
    else if (sem_trywait(&q->spaces) != 0) return -1;

    sem_wait(&q->mutex);
    q->buf[q->tail] = *o;
    q->tail = (q->tail + 1) % capacity;
    q->count++;
    sem_post(&q->mutex);
    sem_post(&q->items);
    return 0;
}

int bqueue_pop(BandQueue *q, Order *o, int capacity, int block) {
    if (block) sem_wait(&q->items);
    else if (sem_trywait(&q->items) != 0) return -1;

    sem_wait(&q->mutex);
    *o = q->buf[q->head];
    q->head = (q->head + 1) % capacity;
    q->count--;
    sem_post(&q->mutex);
    sem_post(&q->spaces);
    return 0;
}
