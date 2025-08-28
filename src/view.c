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
    if (sy->F == 1) {
        sem_wait(&sy->D); // primer lector bloquea escritores
    }
    sem_post(&sy->E);
}
static void reader_exit(sync_t *sy){
    sem_wait(&sy->E);
    if (sy->F > 0) {
        sy->F--;
    }
    if (sy->F == 0) {
        sem_post(&sy->D); // ultimo lector libera escritores
    }
    sem_post(&sy->E);
}

// Render del tablero
static void draw_board(WINDOW *win, const state_t *st){
    int W = st->width, H = st->height;  // dim del tablero
    int cell_w = 2;                     // ancho de celda
    int off_y = 1, off_x = 1;           // borde

    werase(win);    // limpia la ventana
    box(win, 0, 0); // dibuja el borde de la ventana

    for (int y=0; y<H; ++y){
        for (int x=0; x<W; ++x){
            int v = st->board[y*W + x];
            int pair = 1;
            if (has_colors()){
                int idx = v % 7; 
                if (idx < 0) {
                    idx = -idx;
                }
                pair = 1 + idx;                  // 1 a 8
                if (pair > 8) {
                    pair = 8;
                }
            }
            wattron(win, COLOR_PAIR(pair));
            int vy = off_y + y;
            int vx = off_x + x*cell_w;
            mvwprintw(win, vy, vx, "  ");
            wattroff(win, COLOR_PAIR(pair));
        }
    }
    wnoutrefresh(win);  // marca la ventana para actualizar
}


// UI general
static void draw_ui(const state_t *st){
    int term_h, term_w; getmaxyx(stdscr, term_h, term_w); // tam actual de terminal
    int W = st->width, H = st->height;

    mvprintw(0, 0, "ChompChamps | %dx%d | players=%u | over=%d",
             W, H, st->num_players, st->game_over);

    // Calcular tamaño del tablero
    int cell_w = 2;
    int bw = 2 + W*cell_w; // box + contenido
    int bh = 2 + H;        // box + contenido

    // Centrar tablero
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

// Inicializacion de colores
static void init_colors(void){

    start_color();
    use_default_colors();
    // Pares 1 a 8
    init_pair(1, -1, COLOR_BLACK);
    init_pair(2, -1, COLOR_BLUE);
    init_pair(3, -1, COLOR_CYAN);
    init_pair(4, -1, COLOR_GREEN);
    init_pair(5, -1, COLOR_MAGENTA);
    init_pair(6, -1, COLOR_RED);
    init_pair(7, -1, COLOR_YELLOW);
    init_pair(8, -1, COLOR_WHITE);
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
    init_colors();

    while (1){
        sem_wait(&sy->A);           // hay cambios
        reader_enter(sy);           // lector
        draw_ui(st);
        int over = st->game_over;
        reader_exit(sy);
        sem_post(&sy->B);           // confirmé
        if (over) break;
    }

    endwin();
    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}