#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "ipc.h"
#include "rwsem.h"

static void usage(const char *p){ fprintf(stderr, "Uso: %s <idx>\n", p); }

int main(int argc, char **argv){
    if (argc < 2){ usage(argv[0]); return 2; }

    char *end = NULL;
    long me_l = strtol(argv[1], &end, 10);
    if (*end != '\0' || me_l < 0 || me_l >= MAX_PLAYERS){ usage(argv[0]); return 2; }
    int me = (int)me_l;

    state_t *st = ipc_open_and_map_state();
    if (!st){ perror("player: open state"); return 1; }
    sync_t  *sy = ipc_open_and_map_sync();
    if (!sy){ perror("player: open sync"); ipc_unmap_state(st); return 1; }

    unsigned seed = (unsigned)time(NULL) ^ ((unsigned)getpid()<<16) ^ (unsigned)me;

    // bucle principal
    while (1){
        // gate: el master habilita 1 solicitud por turno
        if (sem_wait(&sy->G[me]) != 0){
            if (errno == EINTR) continue;
            break;
        }

        // leer bandera de fin
        rw_reader_enter(sy);
        bool over = st->game_over;
        rw_reader_exit(sy);
        if (over) break;

        // elegir direccion 0 a 7 (random simple)
        unsigned char dir = (unsigned char)(rand_r(&seed) % 8);

        // enviar 1 byte por stdout (pipe)
        ssize_t w = write(STDOUT_FILENO, &dir, 1);
        if (w < 0 && errno == EPIPE) break; // master cerro pipe
    }

    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}
