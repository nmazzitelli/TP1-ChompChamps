#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include "ipc.h"

// Escritor (mutex D)
static void writer_enter(sync_t *sy){ sem_wait(&sy->D); }
static void writer_exit (sync_t *sy){ sem_post(&sy->D); }

static int clamp(int v, int lo, int hi){
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void usage(const char* p){
    fprintf(stderr,
        "Uso: %s <W> <H> [segundos_visible=5]\n"
        "Inicializa SHM, rellena el tablero y lanza vista + N jugadores (por env NPLAYERS 1..9).\n", p);
}

static int read_env_nplayers(void){
    const char* s = getenv("NPLAYERS");
    if (!s || !*s) return 1; // default: 1 jugador
    char* end = NULL;
    long n = strtol(s, &end, 10);
    if (end && *end != '\0') return 1;
    return clamp((int)n, 1, MAX_PLAYERS);
}

int main(int argc, char **argv){
    if (argc < 3){ usage(argv[0]); return 1; }
    unsigned short W = (unsigned short)atoi(argv[1]);
    unsigned short H = (unsigned short)atoi(argv[2]);
    int seconds_visible = (argc > 3) ? atoi(argv[3]) : 5;

    int nplayers = read_env_nplayers();

    bool existed_state=false, created_sync=false;

    state_t *st = ipc_create_and_map_state(W, H, &existed_state);
    if (!st){ perror("master: create state"); return 1; }

    sync_t  *sy = ipc_create_and_map_sync(&created_sync);
    if (!sy){
        perror("master: create sync");
        ipc_unmap_state(st);
        return 1;
    }

    if (created_sync){
        if (ipc_init_sync_semaphores(sy) != 0){
            perror("sem_init");
            ipc_unmap_sync(sy); ipc_unmap_state(st);
            return 1;
        }
    }

    // Inicialización del estado y tablero (sin lógicas extra)
    srand((unsigned)time(NULL));
    writer_enter(sy);
    st->width = W; st->height = H;
    st->num_players = (unsigned int)nplayers;
    st->game_over = false;
    for (int y=0; y<(int)H; ++y){
        for (int x=0; x<(int)W; ++x){
            // tablero "dummy" 1 a 9 como recompensas, evitando 0 para que se vea colorido
            st->board[y*W + x] = 1 + (rand() % 9);
        }
    }
    writer_exit(sy);

    // Lanzar vista
    pid_t pid_view = fork();
    if (pid_view == 0){
        execlp("./bin/view", "view", NULL);
        perror("exec view");
        _exit(127);
    } else if (pid_view < 0){
        perror("fork view");
        ipc_unmap_sync(sy); ipc_unmap_state(st);
        return 1;
    }

    // Lanzar jugadores [0 a nplayers-1]
    pid_t pids[MAX_PLAYERS]; memset(pids, 0, sizeof(pids));
    for (int i=0; i<nplayers; ++i){
        pid_t c = fork();
        if (c == 0){
            char idxbuf[16]; snprintf(idxbuf, sizeof(idxbuf), "%d", i);
            execlp("./bin/player", "player", idxbuf, NULL);
            perror("exec player");
            _exit(127);
        } else if (c < 0){
            perror("fork player");
            // seguimos con los que ya lanzamos; al final esperamos lo que haya
        } else {
            pids[i] = c;
        }
    }

    // Primer render (A/B) para que la vista dibuje el estado inicial
    sem_post(&sy->A);
    sem_wait(&sy->B);

    // Handshake mínimo con G[i] (solo batón, sin procesar movimientos todavía)
    // Objetivo: ejercitar G[i] sin agregar lógica de juego.
    // Hacemos unas rondas cortas (2*nplayers) mientras contamos tiempo visible.
    int rounds = 2 * nplayers;
    for (int r = 0; r < rounds; ++r){
        for (int i=0; i<nplayers; ++i){
            // entregar batón
            sem_post(&sy->G[i]);
            // esperar a que el jugador lo devuelva
            sem_wait(&sy->G[i]);
        }
        // opcional: notificar a la vista aunque no cambió el estado (no hace daño)
        sem_post(&sy->A);
        sem_wait(&sy->B);
    }

    // Mantener X segundos visible para que se vea la UI
    if (seconds_visible > 0) sleep((unsigned)seconds_visible);

    // Finalizar juego: marcar over y hacer último A/B
    writer_enter(sy);
    st->game_over = true;
    writer_exit(sy);

    // Despertar a TODOS los jugadores para que vean game_over y salgan
    for (int i=0; i<nplayers; ++i){
        sem_post(&sy->G[i]);
    }

    sem_post(&sy->A);
    sem_wait(&sy->B);

    // Esperar que cierren jugadores y vista
    for (int i=0; i<nplayers; ++i){
        if (pids[i] > 0){
            int stc = 0;
            waitpid(pids[i], &stc, 0);
        }
    }
    int status = 0;
    if (pid_view > 0) waitpid(pid_view, &status, 0);

    // Limpieza local (no hacemos unlink acá)
    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}
