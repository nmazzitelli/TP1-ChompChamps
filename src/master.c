// This is a personal academic project.
// Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

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
#include <sys/select.h> // select(2) para multiplexar pipea de jugadores
#include <stdint.h>
#include "ipc.h"    // SHM: /game_state y /game_sync
#include "rwsem.h"  // RW: semaforos de lectura/escritura
#include <getopt.h>

// util
static int clamp(int v, int lo, int hi){ if (v<lo) return lo; if (v>hi) return hi; return v; }
static void usage(const char* p){
    fprintf(stderr,
        "Uso: %s -w ancho -h alto -v ruta_vista -p jugador1 [jugador2 ...] [-d delay_ms] [-t timeout_s] [-s semilla]\n",
        p);
}

// Reloj mide tiempo entre movimientos validos
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
// Llena todo el tablero con recompensas del 1 al 9 aleatorias
static void board_fill_random(state_t *st){
    int W = st->width, H = st->height;
    for (int y=0; y<H; ++y)
        for (int x=0; x<W; ++x)
            st->board[idx_xy(x,y,W)] = 1 + (rand() % 9);
}


// Distribuye las posiciones iniciales de N jugadores de forma pareja
// y con margen similar al borde
static void distribute_positions(int n, int W, int H, int *px, int *py){
    int R = 1; while (R*R < n) R++;             // filas
    int C = (n + R - 1) / R;                    // columnas
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

// setea la celda inicial como capturada por el jugador (-id) (no suma score)
static void paint_initial_positions(state_t *st, int n, const int *px, const int *py){
    int W = st->width;
    for (int i=0;i<n;++i){
        int id = idx_xy(px[i], py[i], W);
        st->board[id] = -i; // capturada por i (nota: id=0 => celda=0)
        st->players[i].pos_x = (unsigned short)px[i];
        st->players[i].pos_y = (unsigned short)py[i];
    }
}

// Handshake A/B
// Notifica a la vista (A) y espera que temrine de imprimir (B)
// Luego el master aplica el delay si corresponde
static void repaint(sync_t *sy){ sem_post(&sy->A); sem_wait(&sy->B); }

// chequear si el jugador tiene algun movimiento valido
static bool has_valid_move(state_t *st, int i) {
    int x = (int)st->players[i].pos_x;  // pos actual
    int y = (int)st->players[i].pos_y;  // pos actual
    int W = (int)st->width, H = (int)st->height;    // dimensiones tablero
    for (int d = 0; d < 8; d++) {
        int nx = x + DX[d];
        int ny = y + DY[d];
        if (nx >= 0 && nx < W && ny >= 0 && ny < H) {   // dentro de limites
            if (st->board[ny * W + nx] > 0) {
                return true;
            }
        }
    }
    return false;   // no hay movimientos validos
}

// Lanza la vista 
static pid_t launch_view(const char *view_path, unsigned short W, unsigned short H){
    pid_t pid = fork();
    if (pid == 0){
        // hijo vista reemplaza imagen de proceso
        char wbuf[16], hbuf[16];
        snprintf(wbuf, sizeof(wbuf), "%u", (unsigned)W);
        snprintf(hbuf, sizeof(hbuf), "%u", (unsigned)H);
        execlp(view_path, "view", "-w", wbuf, "-h", hbuf, NULL);
        perror("exec view");
        _exit(127);
    }
    return pid;  // padre devuelve el pid de la vista
}

// Crea un pipe por jugador y redirige stdout del jugador al extremo de escritura del pipe
// El master se queda con el extremo de lectura para hacer select(2)
static void launch_players(int n, char *players[], int p_rd[], pid_t pids[], unsigned short W, unsigned short H){
    for (int i = 0; i < n; ++i){
        int pfd[2]; if (pipe(pfd) != 0){ perror("pipe"); p_rd[i]=-1; pids[i]=0; continue; }
        int rd = pfd[0], wr = pfd[1];
        pid_t c = fork();
        if (c == 0){
            // hijo jugador: su stdout es el pipe
            close(rd);
            if (dup2(wr, STDOUT_FILENO) < 0){ perror("dup2"); _exit(127); }
            if (wr != STDOUT_FILENO) close(wr);
            for(int j=0; j<n; j++){ 
                if(p_rd[j]>=0){
                    close(p_rd[j]);
                }
            }
            char idxbuf[16], wbuf[16], hbuf[16];
            snprintf(idxbuf, sizeof(idxbuf), "%d", i);
            snprintf(wbuf,  sizeof(wbuf),  "%u", (unsigned)W);
            snprintf(hbuf,  sizeof(hbuf),  "%u", (unsigned)H);
            //execlp(players[i], "player", "-i", idxbuf, "-w", wbuf, "-h", hbuf, NULL);
            execlp(players[i], "player", wbuf, hbuf, NULL);
            perror("exec player"); _exit(127);
        } else if (c < 0){
            // error en fork: limpiar y marcar invalido
            perror("fork player"); close(rd); close(wr); p_rd[i]=-1; pids[i]=0;
        } else {
            // padre: se queda con extremo de lectura
            pids[i]=c; close(wr); p_rd[i]=rd;
        }
    }
}


// Helpers logica del juego
// Devuelve true si hay al menos 1 celda libre, sirve para detectar bloqueo
static bool any_free_adjacent(const state_t *st, int x0, int y0){
    for (int d=0; d<8; ++d){
        int nx = x0 + DX[d], ny = y0 + DY[d];
        if (!in_bounds(nx,ny,st->width,st->height)) continue;
        if (st->board[idx_xy(nx,ny,st->width)] > 0) return true;
    }
    return false;
}

// Bucle principal
// Atiende 1 solicitud por jugador habilitado antes de pasar al siguiente
// Cada jugador tiene G[i] como compuerta para enviar un movimiento
// Se corta por timeout sin movimientos valiods o por quedarse sin jugadores activos
static void run_round_robin(state_t *st, sync_t *sy,
    int nplayers, int step_ms, int timeout_s,
    int px[], int py[], int p_rd[], pid_t pids[])
{
    bool active_fd[MAX_PLAYERS];    // jugadores con pipe vivo
    for (int i = 0; i < nplayers; ++i)
        active_fd[i] = (p_rd[i] >= 0);

    uint64_t last_valid_ms = now_ms();  // marca del ultimo movimiento valido

    // seed: habilitar 1 solicitud por jugador activo (sin acumular)
    for (int i = 0; i < nplayers; ++i){
        if (active_fd[i]) sem_post(&sy->G[i]);
    }

    while (true) {
        // timeout global
        if (timeout_s > 0 && (now_ms() - last_valid_ms >= (uint64_t)timeout_s * 1000u))
            break;

        // build fd_set con pipes de los jugadores activos
        fd_set rfds; FD_ZERO(&rfds);
        int maxfd = -1;
        int any_active = 0;
        for (int i = 0; i < nplayers; ++i) {
            if (!active_fd[i]) continue;
            FD_SET(p_rd[i], &rfds);
            if (p_rd[i] > maxfd) maxfd = p_rd[i];
            any_active = 1;
        }
        if (!any_active) break;

        // select con timeout step_ms (delay entre impresiones)
        struct timeval tv;
        tv.tv_sec = (time_t)((step_ms > 0) ? (step_ms / 1000) : 0);
        tv.tv_usec = (suseconds_t)((step_ms > 0) ? ((step_ms % 1000) * 1000) : 0);

        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;       // reintentar si señal interrumpio
            perror("select");                   // error grave cerrar todo lo activo
            for (int i = 0; i < nplayers; ++i) {
                if (active_fd[i]) { active_fd[i] = false; if (p_rd[i] >= 0) { close(p_rd[i]); p_rd[i] = -1; } }
            }
            break;
        }

        bool processed[MAX_PLAYERS] = {0};      // quien fue atendido en este ciclo
        bool any_valid_this_cycle = false;      // si hubo algun movimiento valido

        if (ready > 0) {
            // iterar en orden sobre todos los jugadores con datos listos
            for (int i = 0; i < nplayers; ++i) {
                if (!active_fd[i]) continue;
                if (!FD_ISSET(p_rd[i], &rfds)) continue;

                unsigned char dir;              // jugador envia 1 byte con la direccion
                ssize_t r = read(p_rd[i], &dir, 1);
                if (r == 1) {
                    bool moved = false;
                    if (dir <= 7) {
                        int W = st->width, H = st->height;
                        int nx = px[i] + DX[dir];
                        int ny = py[i] + DY[dir];

                        if (in_bounds(nx, ny, W, H)) {
                            int idx_new = idx_xy(nx, ny, W);
                            int val = st->board[idx_new];

                            if (val > 0) {
                                // valid move: sumar reward y capturar celda como -i
                                // seccion critica de escritor, actualiza estado compartido
                                rw_writer_enter(sy);
                                st->players[i].v_moves++;
                                st->players[i].score += (unsigned int)val; // suma recompensa
                                st->board[idx_new] = -i;  // capturada por i
                                // update shm pos
                                st->players[i].pos_x = (unsigned short)nx; // pos en shm
                                st->players[i].pos_y = (unsigned short)ny;
                                rw_writer_exit(sy);

                                // estado local del master
                                px[i] = nx;
                                py[i] = ny;

                                repaint(sy);              // imprime vista A/B
                                last_valid_ms = now_ms(); // reinicia timeout
                                moved = true;
                            } else {
                                // destino no libre
                                rw_writer_enter(sy);
                                st->players[i].inv_moves++;
                                rw_writer_exit(sy);
                                repaint(sy);
                            }
                        } else {
                            // out of bounds
                            rw_writer_enter(sy);
                            st->players[i].inv_moves++;
                            rw_writer_exit(sy);
                            repaint(sy);
                        }
                    } else {
                        // direccion invalida
                        rw_writer_enter(sy);
                        st->players[i].inv_moves++;
                        rw_writer_exit(sy);
                        repaint(sy);
                    }

                    processed[i] = true;               // se consumió su solicitud
                    any_valid_this_cycle |= moved;

                    // re-habilitar SOLO al jugador que ya fue procesado (1 token nuevo)
                    //sem_pos t(&sy->G[i]);
                    if (has_valid_move(st, i)) { // tiene movimientos validos
                        sem_post(&sy->G[i]);
                    } else {
                        // no tiene movimientos validos: marcar bloqueado
                        rw_writer_enter(sy);
                        st->players[i].blocked = true;
                        rw_writer_exit(sy);
                        repaint(sy);

                        // cerraramos su pipe y lo deshabilitamos
                        if (p_rd[i] >= 0) { close(p_rd[i]); p_rd[i] = -1; }
                        active_fd[i] = false;
                    }

                } else if (r == 0) {
                    // eof: jugador cerro -> marcar bloqueado
                    rw_writer_enter(sy);
                    st->players[i].blocked = true;
                    rw_writer_exit(sy);
                    repaint(sy);
                    active_fd[i] = false;
                    if (p_rd[i] >= 0) { close(p_rd[i]); p_rd[i] = -1; }
                } else {
                    if (errno == EINTR) continue;   // retry
                    active_fd[i] = false;           // error de lectura
                    if (p_rd[i] >= 0) { close(p_rd[i]); p_rd[i] = -1; }
                }
            }
        }

        // jugadores habilitados que no se procesaron: si no hay libres adyacentes -> bloquearlos
        for (int i = 0; i < nplayers; ++i) {
            if (!active_fd[i]) continue;
            if (processed[i]) continue;
            if (!any_free_adjacent(st, px[i], py[i])) {
                rw_writer_enter(sy);
                st->players[i].blocked = true;
                rw_writer_exit(sy);
                repaint(sy);
                active_fd[i] = false;
                if (p_rd[i] >= 0) { close(p_rd[i]); p_rd[i] = -1; }
                // if (pids[i] > 0) { kill(pids[i], SIGTERM); }  // Ahora si no deberia de matarlos, y el master espera
            }
        }

        // early stop: si queda solo 1 jugador activo, terminar ya mismo
        {
            int count_active = 0;
            for (int i = 0; i < nplayers; ++i) if (active_fd[i]) count_active++;
            // if (count_active <= 1) break;                 Es por esta linea que se detenia cuando habia un solo jugador
            if (nplayers > 1 && count_active <= 1) break;
        }

        // delay entre impresiones si hubo al menos 1 movimiento valido
        if (any_valid_this_cycle && step_ms > 0) sleep_ms(step_ms);

        // si no queda nadie activo, salir
        bool any = false;
        for (int i = 0; i < nplayers; ++i) if (active_fd[i]) { any = true; break; }
        if (!any) break;
    }

    // señal de fin de juego
    rw_writer_enter(sy);
    st->game_over = true;
    rw_writer_exit(sy);
    for (int i = 0; i < nplayers; ++i) sem_post(&sy->G[i]); // liberar a todos
    repaint(sy);
}

// Resultados
static const char* color_name_for_player(unsigned i){
    static const char *names[8] = {
        "blue", "red", "green", "magenta", "cyan", "yellow", "white", "black"
    };
    return names[i % 8];
}

static void print_results(const state_t *st){
    printf("\n=== Resultados ===\n");
    int winner = -1;

    for (unsigned i = 0; i < st->num_players; ++i){
        const player_t *p = &st->players[i];
        const char *col = color_name_for_player(i);
        printf("Jugador %s (PID %d, color=%s): score=%u, validos=%u, invalidos=%u\n",
               p->name[0] ? p->name : "P?",
               (int)p->player_pid,
               col,
               p->score,
               p->v_moves,
               p->inv_moves);

        if (winner < 0){
            winner = (int)i;
        } else {
            // Criterios de desempate, mas validos, menos invalidos, menor pid
            const player_t *w = &st->players[winner];
            if (p->score > w->score ||
               (p->score == w->score && p->v_moves > w->v_moves) ||
               (p->score == w->score && p->v_moves == w->v_moves && p->inv_moves < w->inv_moves) ||
               (p->score == w->score && p->v_moves == w->v_moves && p->inv_moves == w->inv_moves && p->player_pid < w->player_pid)){
                winner = (int)i;
               }
        }
    }

    if (winner >= 0){
        const player_t *w = &st->players[winner];
        const char *wcol = color_name_for_player((unsigned)winner);
        printf("\nGanador: %s (PID %d, color=%s)\n",
               w->name[0] ? w->name : "P?",
               (int)w->player_pid,
               wcol);
    } else {
        printf("\nGanador: ninguno\n");
    }
}



int main(int argc, char **argv){
    unsigned short W = 0, H = 0;
    int delay = 400;
    int timeout = 10;
    int seed = (int)time(NULL);
    char *view_path = NULL;
    char *players[MAX_PLAYERS];
    int nplayers = 0;

    for (int i = 0; i < MAX_PLAYERS; ++i) players[i] = NULL;

    // Parseo de opciones cortas
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
                if (nplayers < MAX_PLAYERS) players[nplayers++] = optarg;
                while (optind < argc && argv[optind][0] != '-' && nplayers < MAX_PLAYERS){
                    players[nplayers++] = argv[optind++];
                }
                break;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (W == 0 || H == 0 || view_path == NULL || nplayers == 0){
        usage(argv[0]);
        return 1;
    }

    // Semilla y cantidad de jugadores
    int step_ms = delay;
    srand((unsigned)seed);
    int nplayers_cfg = nplayers;

    // Crear y mapear shm de estado, sync e inicializacion de semaforos
    bool existed_state=false, created_sync=false;
    state_t *st = ipc_create_and_map_state(W, H, &existed_state);
    if (!st){ perror("master: create state"); return 1; }
    sync_t  *sy = ipc_create_and_map_sync(&created_sync);
    if (!sy){ perror("master: create sync"); ipc_unmap_state(st); return 1; }
    if (created_sync && ipc_init_sync_semaphores(sy) != 0){
        perror("sem_init"); ipc_unmap_sync(sy); ipc_unmap_state(st); return 1;
    }

    // Inicializacion del estado compartido con exclusion de escritores
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

    // Lanzar vista y jugadores
    pid_t pid_view = launch_view(view_path, W, H);
    if (pid_view < 0){ perror("fork view"); ipc_unmap_sync(sy); ipc_unmap_state(st); return 1; }

    int p_rd[MAX_PLAYERS]; pid_t pids[MAX_PLAYERS]; memset(pids,0,sizeof(pids));
    launch_players(nplayers_cfg, players, p_rd, pids, W, H);

    // Registrar pids/nombres en shm
    rw_writer_enter(sy);
    for (int i=0;i<nplayers_cfg;++i){
        snprintf(st->players[i].name, NAME_LEN, "P%d", i);
        st->players[i].player_pid = pids[i];
    }
    rw_writer_exit(sy);

    // Posiciones iniciales y pintar
    int px[MAX_PLAYERS], py[MAX_PLAYERS];
    distribute_positions(nplayers_cfg, (int)W, (int)H, px, py);
    rw_writer_enter(sy); paint_initial_positions(st, nplayers_cfg, px, py); rw_writer_exit(sy);

    repaint(sy);

    // Loop principal: atenciones round-robin hasta timeout o sin jugadores
    run_round_robin(st, sy, nplayers_cfg, step_ms, timeout, px, py, p_rd, pids);

    // Cierre de pipes de jugadores y espera de todos los hijos
    for (int i = 0; i < nplayers_cfg; ++i) {
        if (p_rd[i] >= 0) { close(p_rd[i]); p_rd[i] = -1; }
    }

    for (int i = 0; i < nplayers_cfg; ++i) {
        if (pids[i] > 0) { int stc = 0; waitpid(pids[i], &stc, 0); }
    }

    if (pid_view > 0) { int stv = 0; waitpid(pid_view, &stv, 0); }

    // Reporte final y limpieza
    print_results(st);

    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}
