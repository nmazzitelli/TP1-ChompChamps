#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "ipc.h"

// Lectores (E/F/D) para leer estado de forma segura
static void reader_enter(sync_t *sy){
    sem_wait(&sy->E);
    sy->F++;
    if (sy->F == 1) sem_wait(&sy->D); // primer lector bloquea escritores
    sem_post(&sy->E);
}
static void reader_exit(sync_t *sy){
    sem_wait(&sy->E);
    sy->F--;
    if (sy->F == 0) sem_post(&sy->D); // último lector libera escritores
    sem_post(&sy->E);
}

static void usage(const char *p){
    fprintf(stderr, "Uso: %s <idx>\n", p);
}

int main(int argc, char **argv){
    if (argc < 2){ usage(argv[0]); return 2; }

    char *end = NULL;
    long idx_long = strtol(argv[1], &end, 10);
    if (*end != '\0' || idx_long < 0 || idx_long >= MAX_PLAYERS){
        usage(argv[0]); return 2;
    }
    int me = (int)idx_long;

    state_t *st = ipc_open_and_map_state();
    if (!st){ perror("ipc_open_and_map_state"); return 1; }
    sync_t  *sy = ipc_open_and_map_sync();
    if (!sy){ perror("ipc_open_and_map_sync"); ipc_unmap_state(st); return 1; }

    // Semilla distinta por proceso
    unsigned seed = (unsigned)time(NULL) ^ ((unsigned)getpid()<<16) ^ (unsigned)me;

    for (;;){
        if (sem_wait(&sy->G[me]) != 0){
            if (errno == EINTR) continue;
            break;
        }

        // ¿Se terminó el juego?
        reader_enter(sy);
        bool over = st->game_over;
        reader_exit(sy);
        if (over){
            sem_post(&sy->G[me]);
            break;
        }

        // Enviar exactamente 1 dirección (0 a 7) por turno
        unsigned char dir = (unsigned char)(rand_r(&seed) % 8);
        (void)!write(STDOUT_FILENO, &dir, 1);

        // Devolver el "batón" al máster
        sem_post(&sy->G[me]);
    }

    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}
