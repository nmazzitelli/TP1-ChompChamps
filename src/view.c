#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ncurses.h>
#include "ipc.h"
#include "rwsem.h"

// init de colores
static void init_colors(void){
    start_color();
    use_default_colors();
    // pares basicos
    init_pair(1,  -1, COLOR_BLUE);
    init_pair(2,  -1, COLOR_RED);
    init_pair(3,  -1, COLOR_GREEN);
    init_pair(4,  -1, COLOR_MAGENTA);
    init_pair(5,  -1, COLOR_CYAN);
    init_pair(6,  -1, COLOR_YELLOW);
    init_pair(7,  -1, COLOR_WHITE);
    init_pair(8,  -1, COLOR_BLACK);
    init_pair(9,  COLOR_WHITE, COLOR_BLACK);
    init_pair(10, COLOR_WHITE, -1);
}

static inline int pair_for_player(int id){
    int p = 1 + (id % 8);
    if (p < 1) p = 1;
    if (p > 8) p = 8;
    return p;
}

// para saber si un jugador tiene alguna celda muerta
static int player_has_dead(const state_t *st, int i){
    int W = st->width, H = st->height;
    for (int y=0; y<H; ++y){
        for (int x=0; x<W; ++x){
            int v = st->board[idx_xy(x,y,W)];
            if (is_dead(v) && id_from_dead(v) == i) return 1;
        }
    }
    return 0;
}

// dibuja tablero
static void draw_board(WINDOW *win, const state_t *st){
    int W = st->width, H = st->height;
    int cell_w = 2, off_y = 1, off_x = 1;

    werase(win);
    box(win, 0, 0);

    for (int y=0; y<H; ++y){
        for (int x=0; x<W; ++x){
            int v = st->board[idx_xy(x,y,W)];
            int vy = off_y + y;
            int vx = off_x + x*cell_w;

            if (v > 0){
                wattron(win, COLOR_PAIR(9));
                mvwprintw(win, vy, vx, "%d ", v);
                wattroff(win, COLOR_PAIR(9));
            } else if (is_dead(v)){
                int id = id_from_dead(v);
                int pair = pair_for_player(id);
                wattron(win, COLOR_PAIR(pair) | A_BOLD);
                mvwprintw(win, vy, vx, "xx");
                wattroff(win, COLOR_PAIR(pair) | A_BOLD);
            } else if (is_head(v)){
                int id = id_from_head(v);
                int pair = pair_for_player(id);
                wattron(win, COLOR_PAIR(pair) | A_BOLD);
                mvwprintw(win, vy, vx, "@@");
                wattroff(win, COLOR_PAIR(pair) | A_BOLD);
            } else {
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

// dibuja panel de jugadores (scoreboard)
static void draw_players(WINDOW *win, const state_t *st){
    werase(win);
    box(win, 0, 0);

    mvwprintw(win, 0, 2, " jugadores ");

    int row = 1;
    for (unsigned i=0; i<st->num_players; ++i){
        const player_t *p = &st->players[i];
        int pair = pair_for_player((int)i);

        wattron(win, COLOR_PAIR(pair) | A_BOLD);
        mvwprintw(win, row, 2, "[%u] %s", i, p->name[0] ? p->name : "P?");
        wattroff(win, COLOR_PAIR(pair) | A_BOLD);

        mvwprintw(win, row, 18, "sc=%u v=%u inv=%u", p->score, p->v_moves, p->inv_moves);
        mvwprintw(win, row+1, 4, "pos=(%u,%u) %s%s",
                  p->pos_x, p->pos_y,
                  p->blocked ? "blk" : "ok",
                  player_has_dead(st, (int)i) ? " dead" : "");
        row += 2;
        if (row+1 >= getmaxy(win)) break;
    }

    wnoutrefresh(win);
}

// ui principal
static void draw_ui(const state_t *st){
    int term_h, term_w; getmaxyx(stdscr, term_h, term_w);
    (void)term_h; // saca el warning
    int W = st->width, H = st->height;

    attron(COLOR_PAIR(10));
    mvprintw(0, 0, "ChompChamps | %dx%d | players=%u | over=%d",
             W, H, st->num_players, st->game_over);
    attroff(COLOR_PAIR(10));

    int cell_w = 2;
    int bw = 2 + W*cell_w;
    int bh = 2 + H;

    int sw = 42;
    int sh = (int)(2 + (int)st->num_players * 2 + 1);

    int margin_top = 2;
    int start_y = margin_top;
    int start_x_board = 2;

    int start_x_panel = start_x_board + bw + 2;
    int start_y_panel = start_y;

    bool side_by_side = (start_x_panel + sw <= term_w - 1);
    if (!side_by_side){
        start_x_panel = start_x_board;
        start_y_panel = start_y + bh + 1;
    }

    static WINDOW *board_win = NULL;
    static WINDOW *panel_win = NULL;
    static int last_w = 0, last_h = 0;

    if (!board_win || last_w != W || last_h != H){
        if (board_win) delwin(board_win);
        board_win = newwin(bh, bw, start_y, start_x_board);
        last_w = W; last_h = H;
    } else {
        mvwin(board_win, start_y, start_x_board);
        wresize(board_win, bh, bw);
    }

    if (!panel_win){
        panel_win = newwin(sh, sw, start_y_panel, start_x_panel);
    } else {
        mvwin(panel_win, start_y_panel, start_x_panel);
        wresize(panel_win, sh, sw);
    }

    draw_board(board_win, st);
    draw_players(panel_win, st);
    doupdate();
}

int main(void){
    state_t *st = ipc_open_and_map_state();
    if (!st){ perror("view: open state"); return 1; }
    sync_t  *sy = ipc_open_and_map_sync();
    if (!sy){ perror("view: open sync"); ipc_unmap_state(st); return 1; }

    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); nodelay(stdscr, FALSE);
    curs_set(0);
    if (has_colors()) init_colors();

    while (1){
        // esperar cambio de estado
        sem_wait(&sy->A);

        // leer y dibujar
        rw_reader_enter(sy);
        int over = st->game_over;
        draw_ui(st);
        rw_reader_exit(sy);

        // notificar fin de impresion
        sem_post(&sy->B);

        if (over) break;
    }

    endwin();
    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}
