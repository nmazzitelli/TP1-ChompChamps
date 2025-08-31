#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ncurses.h>
#include "ipc.h"

// Lectores (E/F/D)
static void reader_enter(sync_t *sy){
    sem_wait(&sy->E);
    sy->F++;
    if (sy->F == 1) sem_wait(&sy->D); // primer lector bloquea escritores
    sem_post(&sy->E);
}
static void reader_exit(sync_t *sy){
    sem_wait(&sy->E);
    if (sy->F > 0) sy->F--;
    if (sy->F == 0) sem_post(&sy->D); // ultimo lector libera escritores
    sem_post(&sy->E);
}

// ========= Colores =========
// Pares: 1 a 8 colores de jugadores (fondo color, texto por defecto)
//        9: blanco sobre negro (numeros / overlays)
//       10: UI secundaria
static void init_colors(void){
    start_color();
    use_default_colors();

    // Paleta de jugadores (fondos)
    init_pair(1,  -1, COLOR_BLUE);
    init_pair(2,  -1, COLOR_RED);
    init_pair(3,  -1, COLOR_GREEN);
    init_pair(4,  -1, COLOR_MAGENTA);
    init_pair(5,  -1, COLOR_CYAN);
    init_pair(6,  -1, COLOR_YELLOW);
    init_pair(7,  -1, COLOR_WHITE);
    init_pair(8,  -1, COLOR_BLACK);

    // Numeros y overlays
    init_pair(9,  COLOR_WHITE, COLOR_BLACK);

    // UI
    init_pair(10, COLOR_WHITE, -1);
}

// Mapear id de jugador (0..8) a COLOR_PAIR(1..8)
static inline int pair_for_player(int id){
    int p = 1 + (id % 8);
    if (p < 1) p = 1;
    if (p > 8) p = 8;
    return p;
}

// Helpers de decodificacion (coinciden con master)
static inline int is_dead(int v){ return v <= -200; }
static inline int is_head(int v){ return v <= -100 && v > -200; }
static inline int is_body(int v){ return v <= 0 && v > -100; }

static inline int id_from_dead(int v){ return -(200) - v; }
static inline int id_from_head(int v){ return -(100) - v; }
static inline int id_from_body(int v){ return -v; }

// Render del tablero
static void draw_board(WINDOW *win, const state_t *st){
    int W = st->width, H = st->height;
    int cell_w = 2;                     // ancho por celda
    int off_y = 1, off_x = 1;           // margen

    werase(win);
    box(win, 0, 0);

    for (int y=0; y<H; ++y){
        for (int x=0; x<W; ++x){
            int v = st->board[y*W + x];
            int vy = off_y + y;
            int vx = off_x + x*cell_w;

            if (v > 0){
                // celda libre: numero blanco sobre negro
                wattron(win, COLOR_PAIR(9));
                mvwprintw(win, vy, vx, "%d ", v);
                wattroff(win, COLOR_PAIR(9));
            } else if (is_dead(v)){
                // jugador muerto: mantener color y mostrar "xx"
                int id = id_from_dead(v);
                int pair = pair_for_player(id);
                wattron(win, COLOR_PAIR(pair) | A_BOLD);
                mvwprintw(win, vy, vx, "xx");
                wattroff(win, COLOR_PAIR(pair) | A_BOLD);
            } else if (is_head(v)){
                // cabeza: mismo color en negrita y con "@@" para que resalte
                int id = id_from_head(v);
                int pair = pair_for_player(id);
                wattron(win, COLOR_PAIR(pair) | A_BOLD);
                mvwprintw(win, vy, vx, "@@");
                wattroff(win, COLOR_PAIR(pair) | A_BOLD);
            } else {
                // cuerpo (incluye 0 para id=0)
                int id = id_from_body(v);
                int pair = pair_for_player(id);
                wattron(win, COLOR_PAIR(pair));
                mvwprintw(win, vy, vx, "  ");
                wattroff(win, COLOR_PAIR(pair));
            }
        }
    }
    wnoutrefresh(win);
}

// UI general
static void draw_ui(const state_t *st){
    int term_h, term_w; getmaxyx(stdscr, term_h, term_w);
    int W = st->width, H = st->height;

    attron(COLOR_PAIR(10));
    mvprintw(0, 0, "ChompChamps | %dx%d | players=%u | over=%d",
             W, H, st->num_players, st->game_over);
    attroff(COLOR_PAIR(10));

    // Tamano del tablero
    int cell_w = 2;
    int bw = 2 + W*cell_w;
    int bh = 2 + H;

    // Centrar
    int start_y = 2 + (term_h - (bh+2) > 0 ? (term_h - (bh+2))/2 : 0);
    int start_x = (term_w - bw)/2;

    static WINDOW *board_win = NULL;
    static int last_w = 0, last_h = 0;
    if (!board_win || last_w != W || last_h != H){
        if (board_win) delwin(board_win);
        board_win = newwin(bh, bw, start_y, start_x);
        last_w = W; last_h = H;
    } else {
        mvwin(board_win, start_y, start_x);
        wresize(board_win, bh, bw);
    }

    draw_board(board_win, st);
    doupdate();
}

int main(void){
    // Abrir SHMs existentes
    state_t *st = ipc_open_and_map_state();
    if (!st){ perror("view: open state"); return 1; }
    sync_t  *sy = ipc_open_and_map_sync();
    if (!sy){ perror("view: open sync"); ipc_unmap_state(st); return 1; }

    // ncurses init
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, FALSE);
    curs_set(0);
    if (has_colors()) init_colors();

    while (1){
        sem_wait(&sy->A);           // hay cambios
        reader_enter(sy);           // lector
        draw_ui(st);
        int over = st->game_over;
        reader_exit(sy);
        sem_post(&sy->B);           // confirme
        if (over) break;
    }

    endwin();
    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}