#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <ncurses.h>
#include "sharedHeaders.h"

#define SHM_STATE "/game_state"
#define SHM_SYNC  "/game_sync"
#define TRUE 1
#define FALSE 0

/* init/cleanup */
static void nc_cleanup(void){ endwin(); }
static void nc_init(void){
    const char* term = getenv("TERM");
    if (!term || strcmp(term,"xterm-256color")!=0) setenv("TERM","xterm-256color",1);
    initscr(); cbreak(); noecho(); curs_set(0);
    if (has_colors()){ start_color(); use_default_colors(); }
    atexit(nc_cleanup);
    /* si el terminfo no expone 256 colores, reintentar con xterm-256color */
    if (COLORS < 256){
        endwin();
        setenv("TERM","xterm-256color",1);
        initscr(); cbreak(); noecho(); curs_set(0);
        if (has_colors()){ start_color(); use_default_colors(); }
    }
}

/* colores */
enum { COLOR1=1, COLOR2=256 };  // tablero
static int player_bg[10] = {0,160,33,27,129,220,208,201,45,214}; // colores players

static void nc_init_pairs(void){
    if (!has_colors()) return;
    if (COLORS >= 256){
        init_pair(1, -1, COLOR1);
        init_pair(2, -1, COLOR2);
        for (int id=1; id<=9; ++id) init_pair(10+id, -1, player_bg[id]);
        init_pair(30, 232, COLOR1);
        init_pair(31, 232, COLOR2);
    } else {
        init_pair(1,-1,COLOR_YELLOW); init_pair(2,-1,COLOR_RED);
        for (int id=1; id<=9; ++id) init_pair(10+id,-1,(id%6)+1);
        init_pair(30,COLOR_BLACK,COLOR_YELLOW);
        init_pair(31,COLOR_BLACK,COLOR_RED);
    }
}

/* tamaño de celda según terminal */
static void choose_cell_size(int W,int H,int *cw,int *ch){
    int rows, cols; getmaxyx(stdscr, rows, cols);
    rows = rows>1? rows-1: rows;              // una línea para header
    int cw_auto = cols/(W?W:1); if (cw_auto<2) cw_auto=2; if (cw_auto>8) cw_auto=6;
    int ch_auto = rows/(H?H:1); if (ch_auto<1) ch_auto=1; if (ch_auto>4) ch_auto=3;
    *cw=cw_auto; *ch=ch_auto;
}

/* renderer */
static void render_board_ncurses(int W, int H, const int *cells){
    int cw,ch; choose_cell_size(W,H,&cw,&ch);
    erase();

    // fondo de celdas
    for(int y=0;y<H;y++){
        for(int x=0;x<W;x++){
            int v = cells[y*W+x];
            int dark = (x+y)&1;
            int pair = (v<0)? (10+(-v)) : (dark?2:1);
            if (pair<1 || pair>19) pair = dark?2:1;

            attron(COLOR_PAIR(pair));
            for(int r=0;r<ch;r++){
                move(y*ch+r, x*cw);
                for(int c=0;c<cw;c++) addch(' ');
            }
            attroff(COLOR_PAIR(pair));
        }
    }
    // número de recompensa (1 dígito centrado)
    for(int y=0;y<H;y++){
        for(int x=0;x<W;x++){
            int v = cells[y*W+x];
            if (v>0){
                int dark=(x+y)&1;
                attron(COLOR_PAIR(dark?31:30));
                mvaddch(y*ch+ch/2, x*cw+cw/2, '0'+(v%10));
                attroff(COLOR_PAIR(dark?31:30));
            }
        }
    }

    // HUD de debug (descomentar si querés ver TERM/COLORS)
    // mvprintw(0,0,"COLORS=%d  TERM=%s", COLORS, getenv("TERM"));

    refresh();
}

static int sem_wait_retry(sem_t *s){
    while (sem_wait(s) == -1) {
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

int main() {
    int game_state_fd = shm_open(SHM_STATE, O_RDONLY, 0666);
    if(game_state_fd == -1){
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    // Leer valores básicos para saber el tamaño real:
    state_t temp_game;
    if (read(game_state_fd, &temp_game, sizeof(state_t)) != (ssize_t)sizeof(state_t)) {
        perror("read header");
        exit(EXIT_FAILURE);
    }
    size_t game_size = sizeof(state_t) + (size_t)temp_game.width * (size_t)temp_game.height * sizeof(int);

    // Mapear el estado real:
    state_t *game = mmap(NULL, game_size, PROT_READ, MAP_SHARED, game_state_fd, 0);
    if (game == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    int shm_sync_fd = shm_open(SHM_SYNC, O_RDWR, 0666);
    if (shm_sync_fd == -1) {
        perror("shm_open sync");
        exit(EXIT_FAILURE);
    }

    sync_t *sync = mmap(NULL, sizeof(sync_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_sync_fd, 0);
    if (sync == MAP_FAILED) {
        perror("mmap sync");
        exit(EXIT_FAILURE);
    }

    nc_init();
    nc_init_pairs();

    for (;;) {
        if (sem_wait_retry(&sync->print_needed) == -1) {
            // si falló la espera, tratamos de no colgar al master
            sem_post(&sync->print_done);
            break;
        }
        int *cells = game->board; // o (int*)((char*)game + sizeof(state_t))
        render_board_ncurses(game->width, game->height, cells);
        sem_post(&sync->print_done);
        if (game->game_over) break; // salir después de imprimir el último frame
    }

    // Limpieza básica (el atexit ya hace endwin)
    munmap(game, game_size);
    munmap(sync, sizeof(sync_t));
    close(game_state_fd);
    close(shm_sync_fd);

    return 0;
}