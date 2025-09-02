#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <ctype.h>
#include "../include/common.h"

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' || isspace((unsigned char)s[n-1]))) s[--n] = '\0';
    char *p = s; while (*p && isspace((unsigned char)*p)) ++p; if (p != s) memmove(s, p, strlen(p)+1);
}

static void make_random_order(SharedState *st, Order *o) {
    o->id = __sync_fetch_and_add(&st->next_order_id, 1);
    for (int i = 0; i < MAX_ING; ++i) o->ing[i] = (rand() % 2);
    o->ing[0] = 1; // pan
    o->ing[5] = 1; // carne
}

static void print_help(void) {
    printf("Comandos:\n");
    printf("  p i       -> pausar banda i\n");
    printf("  r i       -> reanudar banda i\n");
    printf("  gen N     -> generar N ordenes aleatorias\n");
    printf("  ord a b c d e f -> orden manual (0/1 por ingrediente)\n");
    printf("  inv b k val -> set inventario (banda b, ingrediente k [0-%d], valor)\n", MAX_ING-1);
    printf("  q         -> salir\n");
}

int main(void) {
    // Abrir shm creada por manager
    int fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); return 1; }
    SharedState *st = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (st == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);

    // Prompts visibles
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Controller listo. Escribe 'help' para ayuda.\n");

    char line[256];
    int last_ids[64]; int last_count = 0;
    while (!st->shutting_down) {
        printf("controller> ");
        if (!fgets(line, sizeof(line), stdin)) break; // EOF/Ctrl+D
        trim(line);
        if (!*line) continue;

        if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            break;
        } else if (strcmp(line, "help") == 0 || strcmp(line, "h") == 0) {
            print_help();
        } else if (line[0] == 'p' && isspace((unsigned char)line[1])) {
            int idx = atoi(&line[2]);
            if (idx >= 0 && idx < st->n_bands) {
                sem_wait(&st->bands[idx].band_mutex);
                st->bands[idx].running = 0;
                sem_post(&st->bands[idx].band_mutex);
                sem_post(&st->inv_update);
                printf("Banda %d pausada\n", idx);
            } else {
                printf("Indice fuera de rango\n");
            }
        } else if (line[0] == 'r' && isspace((unsigned char)line[1])) {
            int idx = atoi(&line[2]);
            if (idx >= 0 && idx < st->n_bands) {
                sem_wait(&st->bands[idx].band_mutex);
                st->bands[idx].running = 1;
                sem_post(&st->bands[idx].band_mutex);
                sem_post(&st->inv_update);
                printf("Banda %d reanudada\n", idx);
            } else {
                printf("Indice fuera de rango\n");
            }
        } else if (strncmp(line, "gen ", 4) == 0) {
            int n = atoi(&line[4]);
            if (n <= 0) { printf("N invalido\n"); continue; }
            for (int i = 0; i < n; ++i) {
                Order o; make_random_order(st, &o);
                if (queue_push(&st->orders, &o, MAX_ORDERS, 1) != 0) {
                    printf("Cola llena; reintenta\n");
                    break;
                }
                if (last_count < (int)(sizeof(last_ids)/sizeof(last_ids[0]))) last_ids[last_count++] = o.id;
            }
            sem_post(&st->inv_update);
            printf("Generadas %d ordenes. IDs:", n);
            for (int i = 0; i < last_count; ++i) printf(" %d", last_ids[i]);
            printf("\n");
            last_count = 0;
        } else if (strncmp(line, "ord ", 4) == 0) {
            int v[MAX_ING] = {0};
            if (sscanf(line+4, "%d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
                Order o = {0};
                o.id = __sync_fetch_and_add(&st->next_order_id, 1);
                for (int i = 0; i < MAX_ING; ++i) o.ing[i] = v[i] ? 1 : 0;
                if (queue_push(&st->orders, &o, MAX_ORDERS, 1) == 0) {
                    sem_post(&st->inv_update);
                    printf("Orden %d encolada\n", o.id);
                } else {
                    printf("No se pudo encolar la orden\n");
                }
            } else {
                printf("Formato: ord a b c d e f (0/1)\n");
            }
    } else if (strncmp(line, "inv ", 4) == 0) {
            int b, k, val;
            if (sscanf(line+4, "%d %d %d", &b, &k, &val) == 3) {
                if (b >=0 && b < st->n_bands && k >=0 && k < MAX_ING) {
            sem_wait(&st->bands[b].band_mutex);
            st->bands[b].inv[k] = val;
            sem_post(&st->bands[b].band_mutex);
                    sem_post(&st->inv_update);
                    printf("Inventario banda %d ingrediente %d -> %d\n", b, k, val);
                } else {
                    printf("Indices fuera de rango\n");
                }
            } else {
                printf("Formato: inv b k val\n");
            }
        } else {
            printf("Comando no reconocido. Escribe 'help'.\n");
        }
    }
    return 0;
}
