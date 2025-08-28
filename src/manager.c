#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include "../include/common.h"

static void usage(const char *prog) {
    fprintf(stderr, "Uso: %s -n <bands> [-g] [-s seed]\n", prog);
    fprintf(stderr, "  -n N       Numero de bandas (1..%d)\n", MAX_BANDS);
    fprintf(stderr, "  -g         Generar ordenes aleatorias (lee stdin si no)\n");
    fprintf(stderr, "  -s seed    Semilla RNG\n");
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
static void spawn_restocker(SharedState *st);
// dashboard y controller serán procesos separados

static volatile sig_atomic_t stop_flag = 0;
static void on_sigint(int sig) { (void)sig; stop_flag = 1; }

int main(int argc, char **argv) {
    int n = 2; int gen = 1; unsigned seed = 0;
    int opt;
    while ((opt = getopt(argc, argv, "n:gs:")) != -1) {
        switch (opt) {
            case 'n': n = atoi(optarg); break;
            case 'g': gen = 1; break;
            case 's': seed = (unsigned)atoi(optarg); break;
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
        // inventario inicial
        for (int k = 0; k < MAX_ING; ++k) b->inv[k] = 10; // stock inicial
        bqueue_init(&b->q, MAX_PER_BAND_QUEUE);
        spawn_worker(st, i);
    }

    spawn_restocker(st);

    struct sigaction sa = {0};
    sa.sa_handler = on_sigint; sigaction(SIGINT, &sa, NULL);

    // bucle de despacho: lee/genera ordenes y asigna a bandas si pueden
    fprintf(stderr, "Manager iniciado con %d bandas. Use ./dashboard y ./controller en otras terminales. Presione Ctrl+C para salir.\n", n);

    while (!stop_flag) {
        // 1) intake: generar o leer
        Order o;
        if (gen) {
            make_random_order(st, &o);
            queue_push(&st->orders, &o, MAX_ORDERS, 1);
            usleep(100000); // 100ms
        } else {
            char line[128];
            if (fgets(line, sizeof(line), stdin)) {
                parse_line_to_order(st, line, &o);
                queue_push(&st->orders, &o, MAX_ORDERS, 1);
            } else {
                usleep(100000);
            }
        }

        // 2) intentar despachar desde cola global a alguna banda
        Order cur;
        while (queue_pop(&st->orders, &cur, MAX_ORDERS, 0) == 0) {
            int assigned = 0;
            for (int i = 0; i < st->n_bands; ++i) {
                BandStatus *b = &st->bands[i];
                if (b->running && can_band_fulfill(b, &cur)) {
                    bqueue_push(&b->q, &cur, MAX_PER_BAND_QUEUE, 1);
                    assigned = 1; break;
                }
            }
            if (!assigned) {
                // no hay banda con inventario: re-encolar y alertar
                queue_push(&st->orders, &cur, MAX_ORDERS, 1);
                snprintf(st->last_alert, sizeof(st->last_alert),
                         "Faltan ingredientes para orden %d", cur.id);
                // notificar a dashboard
                sem_post(&st->inv_update);
                break; // esperar restock
            }
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
    for (int i = 0; i < st->n_bands; ++i) bqueue_destroy(&st->bands[i].q);
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
        // respetar pausa
        while (!b->running && !st->shutting_down) usleep(100000);
        if (st->shutting_down) break;
        if (!can_band_fulfill(b, &o)) {
            // no alcanza inventario: devolver a global y alertar
            queue_push(&st->orders, &o, MAX_ORDERS, 1);
            snprintf(st->last_alert, sizeof(st->last_alert),
                     "Banda %d sin ingredientes para orden %d", b->id, o.id);
            sem_post(&st->inv_update);
            continue;
        }
        consume_inventory(b, &o);
        // simular preparación
        usleep(300000); // 300ms
        __sync_fetch_and_add(&b->processed, 1);
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

static void restocker_loop(SharedState *st) {
    // repone inventario aleatoriamente
    while (!st->shutting_down) {
        usleep(500000); // cada 500ms
        for (int i = 0; i < st->n_bands; ++i) {
            BandStatus *b = &st->bands[i];
            for (int k = 0; k < MAX_ING; ++k) {
                int add = (rand() % 3 == 0) ? (1 + rand()%2) : 0; // ocasiones
                b->inv[k] += add;
                if (b->inv[k] > 50) b->inv[k] = 50; // tope
            }
        }
        sem_post(&st->inv_update);
    }
}

static void spawn_restocker(SharedState *st) {
    pid_t pid = fork();
    if (pid == 0) { restocker_loop(st); _exit(0); }
}

// dashboard/controller externos
