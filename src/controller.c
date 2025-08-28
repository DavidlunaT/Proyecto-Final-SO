#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "../include/common.h"

int main(void) {
    int fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); return 1; }
    SharedState *st = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (st == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);

    fprintf(stderr, "Controller: comandos 'p i', 'r i', 'q' (Ctrl+D para salir)\n");
    char line[64];
    while (!st->shutting_down && fgets(line, sizeof(line), stdin)) {
        if (line[0] == 'q') break;
        if ((line[0] == 'p' || line[0] == 'r')) {
            int idx = atoi(&line[1]);
            if (idx >= 0 && idx < st->n_bands) {
                st->bands[idx].running = (line[0] == 'r');
                sem_post(&st->inv_update);
            }
        }
    }
    return 0;
}
