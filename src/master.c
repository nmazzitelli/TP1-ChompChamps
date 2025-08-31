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
#include "ipc.h"

static void writer_enter(sync_t *sy){ sem_wait(&sy->D); }
static void writer_exit (sync_t *sy){ sem_post(&sy->D); }

static int clamp(int v, int lo, int hi){
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void usage(const char* p){
    fprintf(stderr, "Error\n");
}

static int read_env_nplayers(void){
    const char* s = getenv("NPLAYERS");
    if (!s || !*s) return 1; // default: 1 jugador
    char* end = NULL;
    long n = strtol(s, &end, 10);
    if (end && *end != '\0') return 1;
    return clamp((int)n, 1, MAX_PLAYERS);
}

static int read_env_step_ms(void){
    const char* s = getenv("STEP_MS");
    if (!s || !*s) return 500; // por defecto 500 ms entre pasos
    char* end = NULL;
    long ms = strtol(s, &end, 10);
    if (end && *end != '\0') return 500;
    return clamp((int)ms, 0, 5000); // hasta 5s
}

static unsigned long now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec*1000UL + (unsigned long)(ts.tv_nsec/1000000UL);
}

static void sleep_ms(int ms){
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

// Direcciones (0 a 7) 0=arriba, horario
static const int DX[8] = { 0, 1, 1, 1, 0,-1,-1,-1 };
static const int DY[8] = {-1,-1, 0, 1, 1, 1, 0,-1 };

// Codificacion en board[]
// libre: 1 a 9
// cuerpo de jugador i: -(i)
// cabeza de jugador i: -(100+i)
// muerto jugador i:     -(200+i)
#define CELL_BODY(i) (-(i))
#define CELL_HEAD(i) (-(100 + (i)))
#define CELL_DEAD(i) (-(200 + (i)))

// Distribucion de posiciones iniciales
static void distribute_positions(int n, int W, int H, int *posx, int *posy){
    int R = 1;
    while (R*R < n) R++;
    int C = (n + R - 1) / R;

    int stepX = (C > 0) ? (W / (C + 1)) : W;
    int stepY = (R > 0) ? (H / (R + 1)) : H;

    int k = 0;
    for (int r = 0; r < R && k < n; ++r){
        for (int c = 0; c < C && k < n; ++c){
            int x = (c + 1) * stepX;
            int y = (r + 1) * stepY;
            if (x < 0) x = 0;
            if (x >= W) x = W - 1;
            if (y < 0) y = 0;
            if (y >= H) y = H - 1;
            posx[k] = x;
            posy[k] = y;
            ++k;
        }
    }
}

int main(int argc, char **argv){
    if (argc < 3){ usage(argv[0]); return 1; }
    unsigned short W = (unsigned short)atoi(argv[1]);
    unsigned short H = (unsigned short)atoi(argv[2]);
    int seconds_visible = (argc > 3) ? atoi(argv[3]) : 10;

    int nplayers = read_env_nplayers();
    int step_ms  = read_env_step_ms();

    // Crear e inicializar las 2 SHM
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

    // Inicializacion del estado
    srand((unsigned)time(NULL));
    writer_enter(sy);
    st->width = W; st->height = H;
    st->num_players = (unsigned int)nplayers;
    st->game_over = false;

    // Tablero: recompensas 1 a 9
    for (int y=0; y<(int)H; ++y){
        for (int x=0; x<(int)W; ++x){
            st->board[y*W + x] = 1 + (rand() % 9);
        }
    }
    writer_exit(sy);

    // Lanzar vista
    pid_t pid_view = fork();
    if (pid_view == 0){
        execlp("./bin/view", "view", NULL);
        perror("exec view"); _exit(127);
    } else if (pid_view < 0){
        perror("fork view");
        ipc_unmap_sync(sy); ipc_unmap_state(st); return 1;
    }

    // Pipes por jugador + fork jugadores
    int p_rd[MAX_PLAYERS];     // lectura (master)
    int p_wr[MAX_PLAYERS];     // escritura (para dup2 a fd=1 en el hijo)
    pid_t pids[MAX_PLAYERS];   // pids hijos
    memset(pids, 0, sizeof(pids));

    for (int i=0; i<nplayers; ++i){
        int pfd[2];
        if (pipe(pfd) != 0){
            perror("pipe"); p_rd[i]=p_wr[i]=-1; continue;
        }
        p_rd[i] = pfd[0];
        p_wr[i] = pfd[1];

        pid_t c = fork();
        if (c == 0){
            // hijo: conectar write del pipe a fd=1
            close(p_rd[i]);
            if (p_wr[i] != STDOUT_FILENO){
                if (dup2(p_wr[i], STDOUT_FILENO) < 0){ perror("dup2"); _exit(127); }
                close(p_wr[i]);
            }
            for (int k=0; k<i; ++k){ // cerrar restos
                if (p_rd[k] >= 0) close(p_rd[k]);
                if (p_wr[k] >= 0) close(p_wr[k]);
            }
            char idxbuf[16]; snprintf(idxbuf, sizeof(idxbuf), "%d", i);
            execlp("./bin/player", "player", idxbuf, NULL);
            perror("exec player"); _exit(127);
        } else if (c < 0){
            perror("fork player");
            close(p_rd[i]); close(p_wr[i]); p_rd[i]=p_wr[i]=-1;
        } else {
            pids[i] = c;
            close(p_wr[i]); p_wr[i]=-1; // padre se queda con lectura
        }
    }

    // Posiciones iniciales equitativas
    int posx[MAX_PLAYERS], posy[MAX_PLAYERS];
    distribute_positions(nplayers, (int)W, (int)H, posx, posy);

    // Pintar celda inicial como cabeza
    writer_enter(sy);
    for (int i=0; i<nplayers; ++i){
        int idx = posy[i]*W + posx[i];
        st->board[idx] = CELL_HEAD(i);
    }
    writer_exit(sy);

    // Primer render
    sem_post(&sy->A); sem_wait(&sy->B);

    // Round-robin
    unsigned long end_at = now_ms() + (unsigned long)(seconds_visible>0?seconds_visible:10)*1000UL;
    bool active_fd[MAX_PLAYERS]; for (int i=0;i<nplayers;++i) active_fd[i] = (p_rd[i]>=0);

    while (now_ms() < end_at){
        for (int i=0; i<nplayers && now_ms() < end_at; ++i){
            if (!active_fd[i]) continue; // jugador muerto o inactivo

            // habilitar un movimiento de este jugador
            sem_post(&sy->G[i]);

            // esperar su solicitud (hasta STEP_MS)
            unsigned long turn_deadline = now_ms() + (unsigned long)(step_ms > 0 ? step_ms : 0);

            bool processed = false;
            while (now_ms() < turn_deadline){
                fd_set rfds; FD_ZERO(&rfds);
                FD_SET(p_rd[i], &rfds);
                struct timeval tv;
                unsigned long now = now_ms();
                unsigned long remain = (turn_deadline > now) ? (turn_deadline - now) : 0UL;
                tv.tv_sec  = (time_t)(remain / 1000UL);
                tv.tv_usec = (suseconds_t)((remain % 1000UL) * 1000UL);

                int ready = select(p_rd[i] + 1, &rfds, NULL, NULL, &tv);
                if (ready < 0){
                    if (errno == EINTR) continue;
                    perror("select"); active_fd[i] = false; close(p_rd[i]); break;
                }
                if (ready == 0){
                    // se agoto el tiempo del turno sin solicitud -> siguiente jugador
                    break;
                }

                if (FD_ISSET(p_rd[i], &rfds)){
                    unsigned char dir;
                    ssize_t n = read(p_rd[i], &dir, 1);
                    if (n == 1){
                        if (dir <= 7){
                            int nx = posx[i] + DX[dir];
                            int ny = posy[i] + DY[dir];

                            if (nx >= 0 && ny >= 0 && nx < (int)W && ny < (int)H){
                                int idx_new = ny*W + nx;
                                int cell = st->board[idx_new];
                                if (cell > 0){
                                    // solo pisamos celdas libres
                                    int idx_old = posy[i]*W + posx[i];

                                    writer_enter(sy);
                                    // la vieja cabeza pasa a cuerpo
                                    st->board[idx_old] = CELL_BODY(i);
                                    // la nueva celda se vuelve cabeza
                                    st->board[idx_new] = CELL_HEAD(i);
                                    writer_exit(sy);

                                    posx[i] = nx; posy[i] = ny;

                                    // notificar vista
                                    sem_post(&sy->A); sem_wait(&sy->B);
                                    processed = true;
                                }
                                // si no estaba libre, el intento se ignora
                            }
                        }
                        // fin del turno de i (consumimos a lo sumo 1 solicitud)
                        break;
                    } else if (n == 0){
                        // EOF -> jugador i ya no hablara: marcar SOLO la cabeza como muerta
                        writer_enter(sy);
                        bool head_marked = false;
                        for (int y=0; y<(int)H && !head_marked; ++y){
                            for (int x=0; x<(int)W; ++x){
                                int v = st->board[y*W + x];
                                if (v == CELL_HEAD(i)){
                                    st->board[y*W + x] = CELL_DEAD(i);
                                    head_marked = true;
                                    break;
                                }
                            }
                        }
                        writer_exit(sy);
                        sem_post(&sy->A); sem_wait(&sy->B);

                        active_fd[i] = false; close(p_rd[i]); break;
                    } else {
                        if (errno == EINTR) continue;
                        active_fd[i] = false; close(p_rd[i]); break;
                    }
                }
            }

            // Si el jugador sigue activo y NO procesó movimiento, verificar si está atrapado
            if (active_fd[i] && !processed){
                bool any_free = false;
                for (int d=0; d<8 && !any_free; ++d){
                    int nx = posx[i] + DX[d];
                    int ny = posy[i] + DY[d];
                    if (nx < 0 || ny < 0 || nx >= (int)W || ny >= (int)H) continue;
                    int v = st->board[ny*W + nx];
                    if (v > 0){ // hay al menos una celda libre adyacente
                        any_free = true;
                        break;
                    }
                }
                if (!any_free){
                    // Atrapado: marcar TODO su cuerpo y cabeza como muertos (xx) y desactivar
                    writer_enter(sy);
                    for (int y=0; y<(int)H; ++y){
                        for (int x=0; x<(int)W; ++x){
                            int v = st->board[y*W + x];
                            if (v == CELL_BODY(i) || v == CELL_HEAD(i)){
                                st->board[y*W + x] = CELL_DEAD(i);
                            }
                        }
                    }
                    writer_exit(sy);
                    sem_post(&sy->A); sem_wait(&sy->B);
                    active_fd[i] = false;
                    // Cerrar pipe para que el jugador eventualmente reciba EPIPE si intenta escribir
                    if (p_rd[i] >= 0){ close(p_rd[i]); p_rd[i] = -1; }
                    // Opcional: enviar señal para terminar al proceso jugador
                    if (pids[i] > 0){ kill(pids[i], SIGTERM); }
                }
            }

            // ritmo de animacion: si hubo pintada, dormir un poco
            if (processed && step_ms > 0){
                sleep_ms(step_ms);
            }
            // siguiente jugador
        }
        // si todos estan inactivos, salir
        bool any = false; for (int i=0;i<nplayers;++i) if (active_fd[i]) { any=true; break; }
        if (!any) break;
    }

    // Finalizar juego: setear game_over y despertar a todos
    writer_enter(sy);
    st->game_over = true;
    writer_exit(sy);

    for (int i=0; i<nplayers; ++i){ sem_post(&sy->G[i]); }

    // Drain de EOFs por un rato para marcar xx y mostrarlo
    unsigned long drain_until = now_ms() + 1000; // hasta ~1s
    bool any_change = false;
    while (now_ms() < drain_until){
        bool any_fd = false;
        for (int i=0; i<nplayers; ++i){
            if (p_rd[i] < 0) continue;
            any_fd = true;

            fd_set rfds; FD_ZERO(&rfds);
            FD_SET(p_rd[i], &rfds);
            struct timeval tv = {0, 100000}; // 100ms
            int ready = select(p_rd[i] + 1, &rfds, NULL, NULL, &tv);
            if (ready <= 0) continue;

            if (FD_ISSET(p_rd[i], &rfds)){
                unsigned char b;
                ssize_t n = read(p_rd[i], &b, 1);
                if (n == 0){
                    // EOF detectado -> marcar todo el rastro como muerto
                    writer_enter(sy);
                    for (int y=0; y<(int)H; ++y){
                        for (int x=0; x<(int)W; ++x){
                            int v = st->board[y*W + x];
                            if (v == CELL_BODY(i) || v == CELL_HEAD(i)){
                                st->board[y*W + x] = CELL_DEAD(i);
                            }
                        }
                    }
                    writer_exit(sy);
                    any_change = true;
                    close(p_rd[i]); p_rd[i] = -1;
                }
            }
        }
        if (!any_fd) break;
        if (any_change){
            sem_post(&sy->A); sem_wait(&sy->B);
            any_change = false;
        }
    }

    // Un ultimo repaint por las dudas
    sem_post(&sy->A); sem_wait(&sy->B);

    // Cerrar pipes y esperar hijos
    for (int i=0; i<nplayers; ++i){ if (p_rd[i]>=0) close(p_rd[i]); }
    for (int i=0; i<nplayers; ++i){ if (pids[i]>0){ int stc=0; waitpid(pids[i], &stc, 0); } }
    if (pid_view > 0){ int stv=0; waitpid(pid_view, &stv, 0); }

    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}