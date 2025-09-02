#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include "../include/common.h"

static volatile sig_atomic_t stop_flag = 0;
static void on_sigint(int sig) { (void)sig; stop_flag = 1; }

int main(void) {
    int fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); return 1; }
    SharedState *st = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (st == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);

    struct sigaction sa = {0}; sa.sa_handler = on_sigint; sigaction(SIGINT, &sa, NULL);

    while (!stop_flag && !st->shutting_down) {
        sem_wait(&st->inv_update);
        if (stop_flag || st->shutting_down) break;
        printf("\033[2J\033[H"); // clear
        
        // Información general
        int cola_count = 0;
        sem_wait(&st->orders.mutex);
        cola_count = st->orders.count;
        sem_post(&st->orders.mutex);
        printf("=== BURGER MANAGER DASHBOARD ===\n");
        printf("Bandas: %d | Cola Global: %d órdenes\n", st->n_bands, cola_count);
        
        // Alertas
        if (st->last_alert[0]) {
            printf("🚨 [ALERTA] %s\n", st->last_alert);
        }
        printf("\n");
        
        // Estado detallado por banda
        printf("ESTADO DE BANDAS:\n");
        printf("ID  Estado  Proc  Cola  Inventario (p/t/c/l/q/m)\n");
        printf("--  ------  ----  ----  ------------------------\n");
        
        for (int i = 0; i < st->n_bands; ++i) {
            BandStatus *b = &st->bands[i];
            int running, busy, processed, inv[MAX_ING], band_queue_count;
            
            // Snapshot consistente
            sem_wait(&b->band_mutex);
            running = b->running;
            busy = b->busy;
            processed = b->processed;
            for (int k=0;k<MAX_ING;++k) inv[k]=b->inv[k];
            sem_post(&b->band_mutex);
            
            // Cola de la banda (también necesita lock)
            sem_wait(&b->q.mutex);
            band_queue_count = b->q.count;
            sem_post(&b->q.mutex);
            
            const char *estado = running ? (busy ? "ACTIVA*" : "LISTA ") : "PAUSA ";
            printf("B%d  %s  %4d  %4d  %d/%d/%d/%d/%d/%d\n",
                   i, estado, processed, band_queue_count,
                   inv[0], inv[1], inv[2], inv[3], inv[4], inv[5]);
        }
        
        printf("\nLeyenda: * = procesando orden, p=pan, t=tomate, c=cebolla, l=lechuga, q=queso, m=carne\n");
        printf("Ctrl+C para salir\n");
        fflush(stdout);
    }
    return 0;
}
