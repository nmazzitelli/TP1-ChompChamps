
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include "sharedHeaders.h"

// ==== Config / nombres de SHM ====
#define SHM_STATE "/game_state"
#define SHM_SYNC  "/game_sync"
#define MIN_BOARD_SIZE 10
#define DEFAULT_WIDTH   12
#define DEFAULT_HEIGHT  12
#define DEFAULT_DELAY  600   // ms entre frames
#define DEFAULT_TIMEOUT 10   // s  de “juego”
#define DEFAULT_SEED     7

// ==== Utilidades de tiempo ====
static uint64_t now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000 + ts.tv_nsec/1000000;
}
static void sleep_ms(unsigned ms){
    struct timespec ts = { ms/1000, (long)(ms%1000)*1000000L };
    nanosleep(&ts, NULL);
}

// ==== SHM genérica ====
static void *create_shm(const char *name, size_t size, mode_t mode){
    int fd = shm_open(name, O_RDWR | O_CREAT, mode);
    if (fd == -1) { perror("shm_open"); exit(EXIT_FAILURE); }
    if (size == 0) { fprintf(stderr,"create_shm(%s): size=0\n", name); exit(EXIT_FAILURE); }
    if (ftruncate(fd, (off_t)size) == -1) { perror("ftruncate"); exit(EXIT_FAILURE); }
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(EXIT_FAILURE); }
    close(fd);
    return p;
}

// ==== Args ====
typedef struct {
    int  W, H;
    unsigned delay_ms;
    unsigned timeout_s;
    unsigned seed;
} opts_t;

static opts_t parse_args(int argc, char **argv){
    opts_t o = { DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_DELAY, DEFAULT_TIMEOUT, DEFAULT_SEED };
    int opt;
    while ((opt = getopt(argc, argv, "w:h:d:t:s:")) != -1){
        switch(opt){
            case 'w': o.W = atoi(optarg); break;
            case 'h': o.H = atoi(optarg); break;
            case 'd': o.delay_ms = (unsigned)atoi(optarg); break;
            case 't': o.timeout_s = (unsigned)atoi(optarg); break;
            case 's': o.seed = (unsigned)atoi(optarg); break;
            default:
                fprintf(stderr, "Uso: %s [-w W] [-h H] [-d ms] [-t s] [-s seed]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (o.W < MIN_BOARD_SIZE || o.H < MIN_BOARD_SIZE){
        fprintf(stderr, "El tablero mínimo es %dx%d.\n", MIN_BOARD_SIZE, MIN_BOARD_SIZE);
        exit(EXIT_FAILURE);
    }
    return o;
}

// ==== Un frame del ping-pong A/B ====
static void frame(sync_t *sync, unsigned delay_ms){
    sem_post(&sync->print_needed);  // A: pedir imprimir
    sem_wait(&sync->print_done);    // B: esperar a que la vista termine
    if (delay_ms) sleep_ms(delay_ms);
}

// ==== Main ====
int main(int argc, char **argv){
    opts_t opt = parse_args(argc, argv);

    // empezar limpio
    shm_unlink(SHM_STATE);
    shm_unlink(SHM_SYNC);

    // 1) Crear /game_state con sólo el header
    size_t header_sz = sizeof(state_t);
    state_t *st = create_shm(SHM_STATE, header_sz, 0644);

    // 2) Inicializar header
    st->width  = (unsigned short)opt.W;
    st->height = (unsigned short)opt.H;
    st->num_players = 0;
    st->game_over   = false;

    // 3) Redimensionar /game_state al tamaño real y remapear
    size_t cells = (size_t)st->width * st->height;
    size_t full_sz = sizeof(state_t) + cells * sizeof(int);

    // reabrir y crecer
    int fd_state = shm_open(SHM_STATE, O_RDWR, 0);
    if (fd_state == -1) { perror("shm_open state(reopen)"); exit(EXIT_FAILURE); }
    if (ftruncate(fd_state, (off_t)full_sz) == -1) { perror("ftruncate state(full)"); exit(EXIT_FAILURE); }

    // remap con tamaño final
    munmap(st, header_sz);
    st = mmap(NULL, full_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd_state, 0);
    if (st == MAP_FAILED) { perror("mmap state(full)"); exit(EXIT_FAILURE); }
    close(fd_state);

    // 4) Inicializar tablero con recompensas 1..9 (negativos = territorio de jugador)
    srand(opt.seed);
    for (size_t i = 0; i < cells; i++){
        st->board[i] = (rand() % 9) + 1;   // 1..9
    }

    // 5) Crear /game_sync y semáforos
    sync_t *sync = create_shm(SHM_SYNC, sizeof(sync_t), 0666);
    sem_init(&sync->print_needed,       1, 0);
    sem_init(&sync->print_done,         1, 0);
    sem_init(&sync->master_utd,         1, 1);
    sem_init(&sync->game_state_change,  1, 1);
    sem_init(&sync->sig_var,            1, 1);
    sync->readers = 0;

    // 6) Loop de frames hasta timeout (solo visualización por ahora)
    uint64_t deadline = now_ms() + (uint64_t)opt.timeout_s*1000;
    while (now_ms() < deadline){
        // (Aquí, más adelante, aplicarías movimientos, puntajes, etc.)
        frame(sync, opt.delay_ms);
    }

    // 7) Cierre limpio: marcar fin y pedir un último frame
    st->game_over = true;
    frame(sync, 0);

    return 0;
}
