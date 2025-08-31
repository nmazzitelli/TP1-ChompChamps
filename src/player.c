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
    if (sy->F == 0) sem_post(&sy->D); // ultimo lector libera escritores
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

    while (1){
        // Esperar a que el master procese el movimiento anterior y nos habilite UNO nuevo
        if (sem_wait(&sy->G[me]) != 0){
            if (errno == EINTR) continue;
            break;
        }

        // ¿Se termino el juego?
        reader_enter(sy);
        bool over = st->game_over;
        reader_exit(sy);
        if (over) break;

        // En la version simple no miramos el tablero: movemos aleatorio 0 a 7
        unsigned char dir = (unsigned char)(rand_r(&seed) % 8);

        // Enviar exactamente 1 dirección (0 a 7) por turno al master por fd=1
        // Si el master cerró el pipe, write fallara con EPIPE y terminamos.
        ssize_t w = write(STDOUT_FILENO, &dir, 1);
        if (w < 0 && errno == EPIPE){
            // Máster ya no nos lee -> bloqueado por diseño del master
            break;
        }

        // Importante: NO hacemos sem_post(G[me]). El master volvera a postear
        // cuando haya procesado nuestro movimiento, habilitando el proximo.
    }

    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}