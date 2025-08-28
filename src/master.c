#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include "ipc.h"

// Escritor (mutex D)
static void writer_enter(sync_t *sy){ sem_wait(&sy->D); }
static void writer_exit (sync_t *sy){ sem_post(&sy->D); }

static void usage(){
    fprintf(stderr, "error de argumentos\n");
}

int main(int argc, char **argv){
    if (argc < 3){ usage(argv[0]); return 1; }
    unsigned short W = (unsigned short)atoi(argv[1]);
    unsigned short H = (unsigned short)atoi(argv[2]);
    int seconds_visible = (argc > 3) ? atoi(argv[3]) : 5;   // segundos que se muestra el tablero en esta version

    bool existed_state=false, created_sync=false;

    state_t *st = ipc_create_and_map_state(W, H, &existed_state);
    if (!st){ perror("master: create state"); return 1; }

    sync_t  *sy = ipc_create_and_map_sync(&created_sync);
    if (!sy){ perror("master: create sync"); ipc_unmap_state(st); return 1; }

    if (created_sync){
        if (ipc_init_sync_semaphores(sy) != 0){
            perror("sem_init");
            ipc_unmap_sync(sy); ipc_unmap_state(st); return 1;
        }
    }

    // Inicialización del estado y tablero
    srand((unsigned)time(NULL));

    writer_enter(sy);
    st->width = W; st->height = H;
    st->num_players = 0;
    st->game_over = false;                 // mostramos primero
    for (int y=0; y<H; ++y){
        for (int x=0; x<W; ++x){
            st->board[y*W + x] = rand() % 8; // valores 0 a 7 mapean a pares de color
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

    // Publicar el frame y esperar confirmación de dibujo
    sem_post(&sy->A);   // avisamos que hay algo para imprimir
    sem_wait(&sy->B);   // esperamos confirmacion de que la vista se dibujo

    // Mantener X segundos en pantalla
    if (seconds_visible > 0) sleep((unsigned)seconds_visible);

    // Ahora indicar fin y despedir a la vista (un último A/B)
    writer_enter(sy);
    st->game_over = true;
    writer_exit(sy);
    sem_post(&sy->A);
    sem_wait(&sy->B);

    // Esperar que cierre la vista
    int status = 0;
    if (pid_view > 0) waitpid(pid_view, &status, 0);

    // Limpieza local (no hacemos unlink acá)
    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}