#ifndef COMMON_H
#define COMMON_H

#include <semaphore.h>
#include <sys/types.h>
#include <stdint.h>

#define SHM_NAME "/burger_shm"

#define MAX_BANDS 16
#define MAX_ING 6
#define MAX_ORDERS 256
#define MAX_PER_BAND_QUEUE 64

// Ingredientes fijos para simplificar (definidos en common.c)
// 0: pan, 1: tomate, 2: cebolla, 3: lechuga, 4: queso, 5: carne
extern const char *ING_NAMES[MAX_ING];

typedef struct {
    int id;                    // id incremental de orden
    int ing[MAX_ING];          // cantidades requeridas por ingrediente (0/1)
} Order;

typedef struct {
    // Ring buffer simple con semáforos unnamed en memoria compartida
    Order buf[MAX_ORDERS];
    int head; // posición de lectura
    int tail; // posición de escritura
    int count;
    sem_t mutex;   // exclusión para head/tail/count
    sem_t items;   // cuenta de items disponibles
    sem_t spaces;  // espacios libres
} OrderQueue;

typedef struct {
    // Cola por banda
    Order buf[MAX_PER_BAND_QUEUE];
    int head;
    int tail;
    int count;
    sem_t mutex;
    sem_t items;
    sem_t spaces;
} BandQueue;

typedef struct {
    int id;                   // índice de banda [0..n-1]
    int running;              // 1=RUNNING, 0=PAUSED (controlado por controller)
    int processed;            // hamburguesas completadas
    int inv[MAX_ING];         // inventario actual
    pid_t pid;                // PID del proceso worker
    BandQueue q;              // cola de trabajos asignados a esta banda
} BandStatus;

typedef struct {
    int n_bands;              // N
    int shutting_down;        // 1 si se está cerrando
    int next_order_id;        // para ids

    // Cola global de órdenes pendientes (FIFO)
    OrderQueue orders;

    // Estado por banda
    BandStatus bands[MAX_BANDS];

    // Señalización de cambios de inventario (restocker -> manager)
    sem_t inv_update;

    // Última alerta
    char last_alert[128];
} SharedState;

// Utilidades comunes
int can_band_fulfill(const BandStatus *b, const Order *o);
void consume_inventory(BandStatus *b, const Order *o);
void queue_init(OrderQueue *q, int capacity);
void queue_destroy(OrderQueue *q);
int queue_push(OrderQueue *q, const Order *o, int capacity, int block);
int queue_pop(OrderQueue *q, Order *o, int capacity, int block);

void bqueue_init(BandQueue *q, int capacity);
void bqueue_destroy(BandQueue *q);
int bqueue_push(BandQueue *q, const Order *o, int capacity, int block);
int bqueue_pop(BandQueue *q, Order *o, int capacity, int block);

#endif // COMMON_H
