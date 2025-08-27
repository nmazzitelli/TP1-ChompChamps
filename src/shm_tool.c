#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"

static void usage(const char *p) {
    fprintf(stderr,
        "Uso:\n"
        "  %s init <width> <height>\n"
        "  %s open-info\n"
        "  %s destroy\n", p, p, p);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    if (strcmp(argv[1], "destroy") == 0) {
        int e1 = ipc_unlink_state();
        int e2 = ipc_unlink_sync();
        printf("unlink state: %s\n", e1==0?"ok":"err");
        printf("unlink sync : %s\n", e2==0?"ok":"err");
        return (e1==0 && e2==0) ? 0 : 1;
    }

    if (strcmp(argv[1], "init") == 0) {
        if (argc != 4) { usage(argv[0]); return 1; }
        unsigned short w = (unsigned short)strtoul(argv[2], NULL, 10);
        unsigned short h = (unsigned short)strtoul(argv[3], NULL, 10);
        if (w < 10) {
            w = 10; 
        }
        if (h < 10) {
            h = 10;
        }
        bool reused = false;
        state_t *st = ipc_create_and_map_state(w, h, &reused);
        if (!st) { perror("state"); return 1; }

        bool created_sync = false;
        sync_t *sy = ipc_create_and_map_sync(&created_sync);
        if (!sy) { perror("sync"); ipc_unmap_state(st); ipc_unlink_state(); return 1; }

        if (created_sync) {
            if (ipc_init_sync_semaphores(sy) != 0) {
                perror("sem_init"); ipc_unmap_sync(sy); ipc_unmap_state(st);
                ipc_unlink_state(); ipc_unlink_sync(); return 1;
            }
        }

        printf("/game_state %s, %ux%u; /game_sync %s (semaphores %s)\n",
               reused ? "reusada" : "creada",
               st->width, st->height,
               created_sync ? "creada" : "reusada",
               created_sync ? "inicializados" : "existentes");

        ipc_unmap_sync(sy);
        ipc_unmap_state(st);
        return 0;
    }

    if (strcmp(argv[1], "open-info") == 0) {
        state_t *st = ipc_open_and_map_state();
        if (!st) { perror("open state"); return 1; }
        printf("state: %ux%u, players=%u, game_over=%d\n",
               st->width, st->height, st->num_players, st->game_over);
        ipc_unmap_state(st);
        return 0;
    }

    usage(argv[0]);
    return 1;
}