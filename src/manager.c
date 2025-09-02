#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "../include/common.h"

static void usage(const char *prog) {
    fprintf(stderr, "Uso: %s -n <bands> [-g] [-s seed] [-i a,b,c,d,e,f]\n", prog);
    fprintf(stderr, "  -n N       Numero de bandas (1..%d)\n", MAX_BANDS);
    fprintf(stderr, "  -g         Generar ordenes aleatorias (por defecto: no genera)\n");
    fprintf(stderr, "  -s seed    Semilla RNG (entero >= 0)\n");
    fprintf(stderr, "  -i lista   Inventario inicial por ingrediente: pan,tomate,cebolla,lechuga,queso,carne\n");
}

static void make_random_order(SharedState *st, Order *o) {
    o->id = __sync_fetch_and_add(&st->next_order_id, 1);
    for (int i = 0; i < MAX_ING; ++i) {
        o->ing[i] = (rand() % 2); // 0 o 1 por simplicidad
    }
    // siempre requiere pan y carne
    o->ing[0] = 1; // pan
    o->ing[5] = 1; // carne
}

static void parse_line_to_order(SharedState *st, const char *line, Order *o) {
    // Formato: 6 enteros 0/1 separados por espacio
    o->id = __sync_fetch_and_add(&st->next_order_id, 1);
    int vals[MAX_ING] = {0};
    int c = 0;
    const char *p = line;
    while (*p && c < MAX_ING) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0' || *p == '\n') break;
        vals[c++] = (*p - '0') ? 1 : 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') ++p;
    }
    for (int i = 0; i < MAX_ING; ++i) o->ing[i] = vals[i];
}

static void spawn_worker(SharedState *st, int i);
// sin restocker automático
// dashboard y controller serán procesos separados

static volatile sig_atomic_t stop_flag = 0;
static void on_sigint(int sig) { (void)sig; stop_flag = 1; }

int main(int argc, char **argv) {
    int n = 2; int gen = 0; unsigned seed = 0;
    int initial_inv[MAX_ING] = {10,10,10,10,10,10};
    int opt;
    while ((opt = getopt(argc, argv, "n:gs:i:")) != -1) {
        switch (opt) {
            case 'n': {
                char *end = NULL; errno = 0;
                long ln = strtol(optarg, &end, 10);
                if (errno || end == optarg || *end != '\0' || ln < 1 || ln > MAX_BANDS) {
                    fprintf(stderr, "Error: -n debe ser entero en [1..%d]\n", MAX_BANDS);
                    usage(argv[0]); return 1;
                }
                n = (int)ln; break;
            }
            case 'g': gen = 1; break;
            case 's': {
                char *end = NULL; errno = 0;
                unsigned long ul = strtoul(optarg, &end, 10);
                if (errno || end == optarg || *end != '\0') {
                    fprintf(stderr, "Error: -s debe ser entero decimal >=0\n");
                    usage(argv[0]); return 1;
                }
                seed = (unsigned)ul; break;
            }
            case 'i': {
                // permitir separadores coma y/o espacio
                int vals[MAX_ING]; int cnt = 0;
                char tmp[128];
                strncpy(tmp, optarg, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';
                char *tok = strtok(tmp, ", ");
                while (tok && cnt < MAX_ING) {
                    char *end = NULL; errno = 0; long v = strtol(tok, &end, 10);
                    if (errno || end == tok || *end != '\0' || v < 0) {
                        fprintf(stderr, "Error: -i valores enteros >=0. Ej: -i 10,8,5,6,7,9\n");
                        usage(argv[0]); return 1;
                    }
                    vals[cnt++] = (int)v;
                    tok = strtok(NULL, ", ");
                }
                if (cnt != MAX_ING) {
                    fprintf(stderr, "Error: -i requiere %d valores\n", MAX_ING);
                    usage(argv[0]); return 1;
                }
                for (int k=0;k<MAX_ING;++k) initial_inv[k]=vals[k];
                break;
            }
            default: usage(argv[0]); return 1;
        }
    }
    if (n < 1 || n > MAX_BANDS) { usage(argv[0]); return 1; }
    if (seed == 0) seed = (unsigned)getpid();
    srand(seed);

    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(fd, sizeof(SharedState)) != 0) { perror("ftruncate"); return 1; }
    SharedState *st = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (st == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);
    memset(st, 0, sizeof(*st));
    st->n_bands = n;
    st->shutting_down = 0;
    st->next_order_id = 1;
    queue_init(&st->orders, MAX_ORDERS);
    sem_init(&st->inv_update, 1, 0);

    for (int i = 0; i < n; ++i) {
        BandStatus *b = &st->bands[i];
        b->id = i;
        b->running = 1;
        b->processed = 0;
        b->busy = 0;
        // inventario inicial (configurable con -i)
        for (int k = 0; k < MAX_ING; ++k) b->inv[k] = initial_inv[k];
        bqueue_init(&b->q, MAX_PER_BAND_QUEUE);
        sem_init(&b->band_mutex, 1, 1);
        spawn_worker(st, i);
    }

    // inventario manual: no activar restocker automático

    struct sigaction sa = {0};
    sa.sa_handler = on_sigint; sigaction(SIGINT, &sa, NULL);

    // bucle de despacho: lee/genera ordenes y asigna a bandas si pueden
    fprintf(stderr, "Manager iniciado con %d bandas. Use ./dashboard y ./controller en otras terminales. Presione Ctrl+C para salir.\n", n);

    while (!stop_flag) {
        // 1) intake: generar automático solo si -g
    if (gen) {
            Order o;
            make_random_order(st, &o);
            queue_push(&st->orders, &o, MAX_ORDERS, 1);
            usleep(100000); // 100ms
        } else {
            // no intake por stdin; controller u otros procesos encolarán
            usleep(50000);
        }

        // 2) intentar despachar desde cola global a alguna banda
        Order cur;
        int processed_orders = 0;
        while (queue_pop(&st->orders, &cur, MAX_ORDERS, 0) == 0) {
            int assigned = 0;
            
            // Estrategia: buscar la banda con menos carga primero
            int best_band = -1;
            int min_queue = MAX_PER_BAND_QUEUE + 1;
            
            for (int i = 0; i < st->n_bands; ++i) {
                BandStatus *b = &st->bands[i];
                if (!b->running) continue; // banda pausada
                
                // Verificar inventario
                if (!can_band_fulfill_locked(b, &cur)) continue;
                
                // Encontrar banda con menor cola
                sem_wait(&b->q.mutex);
                int queue_size = b->q.count;
                sem_post(&b->q.mutex);
                
                if (queue_size < min_queue) {
                    min_queue = queue_size;
                    best_band = i;
                }
            }
            
            if (best_band >= 0) {
                BandStatus *b = &st->bands[best_band];
                if (bqueue_push(&b->q, &cur, MAX_PER_BAND_QUEUE, 0) == 0) {
                    assigned = 1;
                    processed_orders++;
                    // Limpiar alerta cuando se asigna exitosamente
                    if (processed_orders == 1) {
                        st->last_alert[0] = '\0';
                    }
                }
            }
            
            if (!assigned) {
                // no hay banda con inventario: re-encolar y alertar
                queue_push(&st->orders, &cur, MAX_ORDERS, 1);
                // detectar ingrediente faltante más significativo
                int missingIdx = -1;
                for (int k = 0; k < MAX_ING; ++k) {
                    if (cur.ing[k] == 0) continue;
                    int any = 0;
                    for (int i = 0; i < st->n_bands; ++i) {
                        BandStatus *b = &st->bands[i];
                        sem_wait(&b->band_mutex);
                        int has_ingredient = (b->inv[k] > 0);
                        sem_post(&b->band_mutex);
                        if (has_ingredient) { any = 1; break; }
                    }
                    if (!any) { missingIdx = k; break; }
                }
                if (missingIdx >= 0)
                    snprintf(st->last_alert, sizeof(st->last_alert),
                             "Orden %d bloqueada: falta %s en todas las bandas", cur.id, ING_NAMES[missingIdx]);
                else
                    snprintf(st->last_alert, sizeof(st->last_alert),
                             "Orden %d en espera: bandas ocupadas o sin inventario suficiente", cur.id);
                // notificar a dashboard
                sem_post(&st->inv_update);
                break; // esperar restock o que se liberen bandas
            }
        }
        
        // Si procesamos órdenes, notificar dashboard
        if (processed_orders > 0) {
            sem_post(&st->inv_update);
        }
    }

    // shutdown
    st->shutting_down = 1;
    // despertar a todos
    for (int i = 0; i < st->n_bands; ++i) sem_post(&st->bands[i].q.items);
    sem_post(&st->inv_update);

    // esperar hijos
    while (waitpid(-1, NULL, 0) > 0) {}

    // limpieza
    for (int i = 0; i < st->n_bands; ++i) {
        bqueue_destroy(&st->bands[i].q);
        sem_destroy(&st->bands[i].band_mutex);
    }
    queue_destroy(&st->orders);
    munmap(st, sizeof(SharedState));
    shm_unlink(SHM_NAME);
    return 0;
}

static void worker_loop(SharedState *st, int idx) {
    BandStatus *b = &st->bands[idx];
    while (!st->shutting_down) {
        Order o;
        if (bqueue_pop(&b->q, &o, MAX_PER_BAND_QUEUE, 1) != 0) continue;
        if (st->shutting_down) break;
        // marcar busy
        sem_wait(&b->band_mutex);
        b->busy = 1;
        sem_post(&b->band_mutex);
        // respetar pausa
        while (1) {
            sem_wait(&b->band_mutex);
            int run = b->running;
            sem_post(&b->band_mutex);
            if (st->shutting_down) break;
            if (run) break;
            usleep(100000);
        }
        if (st->shutting_down) break;
        if (!can_band_fulfill_locked(b, &o)) {
            // no alcanza inventario: devolver a global y alertar
            queue_push(&st->orders, &o, MAX_ORDERS, 1);
            snprintf(st->last_alert, sizeof(st->last_alert),
                     "Banda %d sin ingredientes para orden %d", b->id, o.id);
            sem_post(&st->inv_update);
            sem_wait(&b->band_mutex);
            b->busy = 0;
            sem_post(&b->band_mutex);
            continue;
        }
        consume_inventory_locked(b, &o);
        // simular preparación
        usleep(300000); // 300ms
        sem_wait(&b->band_mutex);
        __sync_fetch_and_add(&b->processed, 1);
        b->busy = 0;
        sem_post(&b->band_mutex);
        sem_post(&st->inv_update); // para refrescar dashboard
    }
}

static void spawn_worker(SharedState *st, int i) {
    pid_t pid = fork();
    if (pid == 0) {
        worker_loop(st, i);
        _exit(0);
    } else if (pid > 0) {
        st->bands[i].pid = pid;
    } else {
        perror("fork worker");
    }
}

// (restocker eliminado)

// dashboard/controller externos
