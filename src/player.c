// This is a personal academic project.
// Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include "ipc.h"
#include "rwsem.h"

static void usage(const char *p){ fprintf(stderr, "Uso: %s -i idx -w ancho -h alto\n", p); }

int main(int argc, char **argv){
    int me = -1;
    unsigned short W = 0, H = 0;

    int opt;
    while ((opt = getopt(argc, argv, "i:w:h:")) != -1){
        switch(opt){
            case 'i': me = (int)strtol(optarg, NULL, 10); break;
            case 'w': W  = (unsigned short)strtoul(optarg, NULL, 10); break;
            case 'h': H  = (unsigned short)strtoul(optarg, NULL, 10); break;
            default: usage(argv[0]); return 2;
        }
    }
    if (me < 0 || me >= MAX_PLAYERS || W == 0 || H == 0){ usage(argv[0]); return 2; }

    state_t *st = ipc_open_and_map_state();
    if (!st){ perror("player: open state"); return 1; }
    sync_t  *sy = ipc_open_and_map_sync();
    if (!sy){ perror("player: open sync"); ipc_unmap_state(st); return 1; }

    if (st->width != W || st->height != H){
        fprintf(stderr, "player[%d]: advertencia: W/H recibidos (%u,%u) difieren de SHM (%u,%u)\n",
                me, (unsigned)W,(unsigned)H,(unsigned)st->width,(unsigned)st->height);
    }

    unsigned seed = (unsigned)time(NULL) ^ ((unsigned)getpid()<<16) ^ (unsigned)me;

    while (1){
        if (sem_wait(&sy->G[me]) != 0){
            if (errno == EINTR) continue;
            break;
        }

        rw_reader_enter(sy);
        bool over = st->game_over;
        rw_reader_exit(sy);
        if (over) break;

        unsigned char dir = (unsigned char)(rand_r(&seed) % 8);
        ssize_t w = write(STDOUT_FILENO, &dir, 1);
        if (w < 0 && errno == EPIPE) break;
    }

    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}
