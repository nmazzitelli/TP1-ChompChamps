// master: organiza shm, vista y jugadores
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include <stdint.h>
#include "ipc.h"
#include "rwsem.h"
#include <getopt.h>

// util
static int clamp(int v, int lo, int hi){ if (v<lo) return lo; if (v>hi) return hi; return v; }
static void usage(const char* p){
    fprintf(stderr,
        "Uso: %s -w ancho -h alto -v ruta_vista -p jugador1 [jugador2 ...] [-d delay_ms] [-t timeout_s] [-s semilla]\n",
        p);
}
static int read_env_nplayers(void){
    const char* s = getenv("NPLAYERS");
    if (!s || !*s) return 1;
    char* end = NULL; long n = strtol(s, &end, 10);
    if (end && *end != '\0') return 1;
    return clamp((int)n, 1, MAX_PLAYERS);
}
static int read_env_step_ms(void){
    const char* s = getenv("STEP_MS");
    if (!s || !*s) return 400;
    char* end = NULL; long ms = strtol(s, &end, 10);
    if (end && *end != '\0') return 400;
    return clamp((int)ms, 0, 5000);
}
static uint64_t now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t sec  = (uint64_t)ts.tv_sec;
    uint64_t nsec = (uint64_t)ts.tv_nsec;
    return sec*1000u + nsec/1000000u;
}
static void sleep_ms(int ms){
    if (ms<=0) return;
    struct timespec ts = { ms/1000, (long)(ms%1000)*1000000L };
    nanosleep(&ts, NULL);
}

// init helpers
static void board_fill_random(state_t *st){
    int W = st->width, H = st->height;
    for (int y=0; y<H; ++y)
        for (int x=0; x<W; ++x)
            st->board[idx_xy(x,y,W)] = 1 + (rand() % 9);
}

static void distribute_positions(int n, int W, int H, int *px, int *py){
    int R = 1; while (R*R < n) R++;
    int C = (n + R - 1) / R;
    int stepX = (C > 0) ? (W / (C + 1)) : W;
    int stepY = (R > 0) ? (H / (R + 1)) : H;
    int k = 0;
    for (int r=0; r<R && k<n; ++r)
        for (int c=0; c<C && k<n; ++c){
            int x = (c + 1) * stepX; if (x<0) x=0; if (x>=W) x=W-1;
            int y = (r + 1) * stepY; if (y<0) y=0; if (y>=H) y=H-1;
            px[k]=x; py[k]=y; ++k;
        }
}

static void paint_initial_heads(state_t *st, int n, const int *px, const int *py){
    int W = st->width;
    for (int i=0;i<n;++i){
        int id = idx_xy(px[i], py[i], W);
        st->board[id] = CELL_HEAD(i);
        st->players[i].pos_x = (unsigned short)px[i];
        st->players[i].pos_y = (unsigned short)py[i];
    }
}

static void repaint(sync_t *sy){ sem_post(&sy->A); sem_wait(&sy->B); }

static pid_t launch_view(const char *view_path){
    pid_t pid = fork();
    if (pid == 0){
        execlp(view_path, view_path, NULL);
        perror("exec view");
        _exit(127);
    }
    return pid;
}

static void launch_players(int n, char *players[], int p_rd[], pid_t pids[]){
    for (int i=0; i<n; ++i){
        int pfd[2]; if (pipe(pfd) != 0){ perror("pipe"); p_rd[i]=-1; pids[i]=0; continue; }
        int rd = pfd[0], wr = pfd[1];
        pid_t c = fork();
        if (c == 0){
            // hijo jugador: su stdout es el pipe
            close(rd);
            if (dup2(wr, STDOUT_FILENO) < 0){ perror("dup2"); _exit(127); }
            if (wr != STDOUT_FILENO) close(wr);
            char idxbuf[16]; snprintf(idxbuf, sizeof(idxbuf), "%d", i);
            execlp(players[i], "player", idxbuf, NULL);
            perror("exec player"); _exit(127);
        } else if (c < 0){
            perror("fork player"); close(rd); close(wr); p_rd[i]=-1; pids[i]=0;
        } else {
            // padre: se queda con extremo de lectura
            pids[i]=c; close(wr); p_rd[i]=rd;
        }
    }
}

// marca cabeza como muerta
static void mark_head_dead(state_t *st, int i){
    int W = st->width, H = st->height;
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x){
        int id = idx_xy(x,y,W);
        if (st->board[id] == CELL_HEAD(i)){ st->board[id] = CELL_DEAD(i); return; }
    }
}
// marca todo el cuerpo del jugador i como muerto
static void mark_all_dead_of(state_t *st, int i){
    int W = st->width, H = st->height;
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x){
        int id = idx_xy(x,y,W);
        int v = st->board[id];
        if (v == CELL_BODY(i) || v == CELL_HEAD(i)) st->board[id] = CELL_DEAD(i);
    }
}
static bool any_free_adjacent(const state_t *st, int x0, int y0){
    for (int d=0; d<8; ++d){
        int nx = x0 + DX[d], ny = y0 + DY[d];
        if (!in_bounds(nx,ny,st->width,st->height)) continue;
        if (st->board[idx_xy(nx,ny,st->width)] > 0) return true;
    }
    return false;
}

// round-robin
static void run_round_robin(state_t *st, sync_t *sy,
    int nplayers, int step_ms, int timeout_s,
    int px[], int py[], int p_rd[], pid_t pids[])
{
    bool active_fd[MAX_PLAYERS];
    for (int i = 0; i < nplayers; ++i)
        active_fd[i] = (p_rd[i] >= 0);

    // track de la ultima jugada valida
    uint64_t last_valid_ms = now_ms();

    while (true) {
        // cortar si paso el timeout sin jugadas validas
        if (timeout_s > 0 && (now_ms() - last_valid_ms >= (uint64_t)timeout_s * 1000u))
            break;

        for (int i = 0; i < nplayers; ++i) {
            if (!active_fd[i]) continue;

            // habilitar jugador i
            sem_post(&sy->G[i]);

            uint64_t turn_deadline = now_ms() + (uint64_t)(step_ms > 0 ? step_ms : 0);
            bool processed = false;

            while (now_ms() < turn_deadline) {
                fd_set rfds; FD_ZERO(&rfds);
                FD_SET(p_rd[i], &rfds);
                uint64_t now = now_ms();
                uint64_t remain = (turn_deadline > now) ? (turn_deadline - now) : 0u;
                struct timeval tv;
                tv.tv_sec  = (time_t)(remain / 1000u);
                tv.tv_usec = (suseconds_t)((remain % 1000u) * 1000u);

                int ready = select(p_rd[i] + 1, &rfds, NULL, NULL, &tv);
                if (ready < 0) {
                    if (errno == EINTR) continue;
                    perror("select");
                    active_fd[i] = false; close(p_rd[i]);
                    break;
                }
                if (ready == 0) break;

                if (FD_ISSET(p_rd[i], &rfds)) {
                    unsigned char dir; ssize_t n = read(p_rd[i], &dir, 1);
                    if (n == 1) {
                        bool moved = false, invalid = false;

                        if (dir <= 7) {
                            int nx = px[i] + DX[dir], ny = py[i] + DY[dir];
                            if (in_bounds(nx, ny, st->width, st->height)) {
                                int idx_new = idx_xy(nx, ny, st->width);
                                int val = st->board[idx_new];
                                if (val > 0) {
                                    int idx_old = idx_xy(px[i], py[i], st->width);
                                    rw_writer_enter(sy);
                                    st->players[i].v_moves += 1u;
                                    st->players[i].score += (unsigned int)val;
                                    st->board[idx_old] = CELL_BODY(i);
                                    st->board[idx_new] = CELL_HEAD(i);
                                    st->players[i].pos_x = (unsigned short)nx;
                                    st->players[i].pos_y = (unsigned short)ny;
                                    rw_writer_exit(sy);

                                    px[i] = nx; py[i] = ny;
                                    repaint(sy);
                                    moved = true;

                                    // reset inactivity timer
                                    last_valid_ms = now_ms();
                                } else invalid = true;
                            } else invalid = true;
                        } else invalid = true;

                        if (!moved && invalid) {
                            rw_writer_enter(sy);
                            st->players[i].inv_moves += 1u;
                            rw_writer_exit(sy);
                        }

                        // Only mark as processed if it was a valid move
                        processed = moved;
                        break; // one play per turn
                    } else if (n == 0) {
                        // EOF: player left / blocked
                        rw_writer_enter(sy);
                        st->players[i].blocked = true;
                        mark_head_dead(st, i);
                        rw_writer_exit(sy);
                        repaint(sy);
                        active_fd[i] = false; close(p_rd[i]); break;
                    } else {
                        if (errno == EINTR) continue;
                        active_fd[i] = false; close(p_rd[i]); break;
                    }
                }
            }

            // si no jugo y no hay libres, bloquear
            if (active_fd[i] && !processed) {
                if (!any_free_adjacent(st, px[i], py[i])) {
                    rw_writer_enter(sy);
                    st->players[i].blocked = true;
                    mark_all_dead_of(st, i);
                    rw_writer_exit(sy);
                    repaint(sy);
                    active_fd[i] = false;
                    if (p_rd[i] >= 0){ close(p_rd[i]); p_rd[i] = -1; }
                    if (pids[i] > 0){ kill(pids[i], SIGTERM); }
                }
            }

            if (processed && step_ms > 0) sleep_ms(step_ms);
        }

        bool any = false;
        for (int i = 0; i < nplayers; ++i)
            if (active_fd[i]) { any = true; break; }
        if (!any) break;
    }

    // marcar fin de juego
    rw_writer_enter(sy);
    st->game_over = true;
    rw_writer_exit(sy);
    for (int i = 0; i < nplayers; ++i) sem_post(&sy->G[i]);
    repaint(sy);
}

int main(int argc, char **argv){
    // parse CLI con getopt
    unsigned short W = 0, H = 0;
    int delay = 400;           // ms, default
    int timeout = 10;          // s, default
    int seed = (int)time(NULL); // default
    char *view_path = NULL;
    char *players[MAX_PLAYERS];
    int nplayers = 0;

    for (int i = 0; i < MAX_PLAYERS; ++i) players[i] = NULL;

    int opt;
    const char *optstr = "w:h:d:t:s:v:p:";
    while ((opt = getopt(argc, argv, optstr)) != -1){
        switch (opt){
            case 'w': {
                char *end = NULL;
                long v = strtol(optarg, &end, 10);
                if (end && *end != '\0'){ usage(argv[0]); return 1; }
                W = (unsigned short)clamp((int)v, 1, 65535);
                break;
            }
            case 'h': {
                char *end = NULL;
                long v = strtol(optarg, &end, 10);
                if (end && *end != '\0'){ usage(argv[0]); return 1; }
                H = (unsigned short)clamp((int)v, 1, 65535);
                break;
            }
            case 'd': {
                char *end = NULL;
                long v = strtol(optarg, &end, 10);
                if (end && *end != '\0'){ usage(argv[0]); return 1; }
                delay = clamp((int)v, 0, 5000);
                break;
            }
            case 't': {
                char *end = NULL;
                long v = strtol(optarg, &end, 10);
                if (end && *end != '\0'){ usage(argv[0]); return 1; }
                timeout = (int)clamp((int)v, 0, 3600);
                break;
            }
            case 's': {
                char *end = NULL;
                long v = strtol(optarg, &end, 10);
                if (end && *end != '\0'){ usage(argv[0]); return 1; }
                seed = (int)v;
                break;
            }
            case 'v':
                view_path = optarg;
                break;
            case 'p':
                // first player given as optarg
                if (nplayers < MAX_PLAYERS) players[nplayers++] = optarg;
                // collect subsequent non-option args as additional players
                while (optind < argc && argv[optind][0] != '-' && nplayers < MAX_PLAYERS){
                    players[nplayers++] = argv[optind++];
                }
                break;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    // check required params
    if (W == 0 || H == 0 || view_path == NULL || nplayers == 0){
        usage(argv[0]);
        return 1;
    }

    int step_ms = delay;

    // init vars that were previously taken from env/time
    srand((unsigned)seed);
    int nplayers_cfg = nplayers;

    bool existed_state=false, created_sync=false;
    state_t *st = ipc_create_and_map_state(W, H, &existed_state);
    if (!st){ perror("master: create state"); return 1; }
    sync_t  *sy = ipc_create_and_map_sync(&created_sync);
    if (!sy){ perror("master: create sync"); ipc_unmap_state(st); return 1; }
    if (created_sync && ipc_init_sync_semaphores(sy) != 0){
        perror("sem_init"); ipc_unmap_sync(sy); ipc_unmap_state(st); return 1;
    }

    rw_writer_enter(sy);
    st->width = W; st->height = H;
    st->num_players = (unsigned int)nplayers_cfg;
    st->game_over = false;
    for (unsigned i=0;i<st->num_players;++i){
        st->players[i].score = 0u;
        st->players[i].inv_moves = 0u;
        st->players[i].v_moves = 0u;
        st->players[i].blocked = false;
        st->players[i].player_pid = 0;
        st->players[i].name[0] = '\0';
    }
    board_fill_random(st);
    rw_writer_exit(sy);

    pid_t pid_view = launch_view(view_path);
    if (pid_view < 0){ perror("fork view"); ipc_unmap_sync(sy); ipc_unmap_state(st); return 1; }

    int p_rd[MAX_PLAYERS]; pid_t pids[MAX_PLAYERS]; memset(pids,0,sizeof(pids));
    launch_players(nplayers_cfg, players, p_rd, pids);

    rw_writer_enter(sy);
    for (int i=0;i<nplayers_cfg;++i){
        snprintf(st->players[i].name, NAME_LEN, "P%d", i);
        st->players[i].player_pid = pids[i];
    }
    rw_writer_exit(sy);

    int px[MAX_PLAYERS], py[MAX_PLAYERS];
    distribute_positions(nplayers_cfg, (int)W, (int)H, px, py);
    rw_writer_enter(sy); paint_initial_heads(st, nplayers_cfg, px, py); rw_writer_exit(sy);

    repaint(sy);

    run_round_robin(st, sy, nplayers_cfg, step_ms, timeout, px, py, p_rd, pids);

    for (int i=0;i<nplayers_cfg;++i){ if (p_rd[i]>=0) close(p_rd[i]); }
    for (int i=0;i<nplayers_cfg;++i){ if (pids[i]>0){ int stc=0; waitpid(pids[i], &stc, 0); } }
    if (pid_view>0){ int stv=0; waitpid(pid_view, &stv, 0); }

    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}
