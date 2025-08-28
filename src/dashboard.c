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
        printf("=== Dashboard (separado) ===\n");
        printf("Bandas: %d\n", st->n_bands);
        if (st->last_alert[0]) printf("ALERTA: %s\n", st->last_alert);
        printf("Cola global: %d\n", st->orders.count);
        for (int i = 0; i < st->n_bands; ++i) {
            BandStatus *b = &st->bands[i];
            printf("Banda %d [%s] proc=%d inv:", i, b->running?"RUN":"PAUSE", b->processed);
            for (int k = 0; k < MAX_ING; ++k) printf(" %s=%d", ING_NAMES[k], b->inv[k]);
            printf("\n");
        }
        fflush(stdout);
    }
    return 0;
}
