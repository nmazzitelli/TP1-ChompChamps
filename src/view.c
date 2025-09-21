// This is a personal academic project.
// Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ncurses.h>
#include <getopt.h>
#include "ipc.h"            // ipc_open_and_map_state/sync
#include "rwsem.h"          // rw_reader_enter/exit

static void ensure_term(void){
    const char *t = getenv("TERM");
    if (!t || !t[0] || strcmp(t, "unknown") == 0){
        // terminal segura por defecto
        setenv("TERM", "xterm", 1);
    }
}

// Inicializa pares de colores, jugadores 1 al 8, tablero 9 al 10
static void init_colors(void){
    start_color();
    use_default_colors();
    init_pair(1,  -1, COLOR_BLUE);
    init_pair(2,  -1, COLOR_RED);
    init_pair(3,  -1, COLOR_GREEN);
    init_pair(4,  -1, COLOR_MAGENTA);
    init_pair(5,  -1, COLOR_CYAN);
    init_pair(6,  -1, COLOR_YELLOW);
    init_pair(7,  -1, COLOR_WHITE);
    init_pair(8,  -1, COLOR_BLACK);
    init_pair(9,  COLOR_WHITE, COLOR_BLACK);    // numeros de celdas libres
    init_pair(10, COLOR_WHITE, -1); // encabezados/ui
}

// Devuelve el par de color para el jugador id (1 a 8)
static inline int pair_for_player(int id){
    int p = 1 + (id % 8);
    if (p < 1) p = 1;
    if (p > 8) p = 8;
    return p;
}

// board
static void draw_board(WINDOW *win, const state_t *st){
    int W = st->width, H = st->height;
    int cell_w = 2, off_y = 1, off_x = 1;   // tamaño/offsets visuales

    werase(win);
    box(win, 0, 0);

    // Dibuja celdas: libres de 1 al 9 con numeros, capturadas con color de jugador
    for (int y=0; y<H; ++y){
        for (int x=0; x<W; ++x){
            int v = st->board[idx_xy(x,y,W)];
            int vy = off_y + y;
            int vx = off_x + x*cell_w;

            if (v > 0){
                wattron(win, COLOR_PAIR(9));
                mvwprintw(win, vy, vx, "%d ", v);
                wattroff(win, COLOR_PAIR(9));
            } else {
                int id = (v <= 0) ? -v : -1;    // dueño de la celda o -1 libre
                int pair = pair_for_player(id);
                wattron(win, COLOR_PAIR(pair));
                mvwprintw(win, vy, vx, "  ");   // bloque solido del color del jugador
                wattroff(win, COLOR_PAIR(pair));
            }
        }
    }

    // heads: dibujar por encima usando pos_x/pos_y de cada jugador
    for (unsigned i=0; i<st->num_players; ++i){
        const player_t *p = &st->players[i];
        int vx = off_x + p->pos_x * cell_w;
        int vy = off_y + p->pos_y;
        int pair = pair_for_player((int)i);

        const char *face = p->blocked ? "xx" : "@@";  // "@@" vivo, "xx" muerto

        wattron(win, COLOR_PAIR(pair) | A_BOLD);
        mvwprintw(win, vy, vx, "%s", face);
        wattroff(win, COLOR_PAIR(pair) | A_BOLD);
    }

    wnoutrefresh(win);
}

// Panel con las stats de los jugadores
static void draw_players(WINDOW *win, const state_t *st){
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " jugadores ");

    int row = 1;
    for (unsigned i=0; i<st->num_players; ++i){
        const player_t *p = &st->players[i];
        int pair = pair_for_player((int)i);

        // Encabezado con nombre e indice
        wattron(win, COLOR_PAIR(pair) | A_BOLD);
        mvwprintw(win, row, 2, "[%u] %s", i, p->name[0] ? p->name : "P?");
        wattroff(win, COLOR_PAIR(pair) | A_BOLD);

        // Score, validos, invalidos, pos, estado
        mvwprintw(win, row, 18, "sc=%u v=%u inv=%u", p->score, p->v_moves, p->inv_moves);
        mvwprintw(win, row+1, 4, "pos=(%u,%u) %s",
                  p->pos_x, p->pos_y,
                  p->blocked ? "blk" : "ok");
        row += 2;
        if (row+1 >= getmaxy(win)) break;   // evita desbordar el panel
    }
    wnoutrefresh(win);
}

// Layout y dibujado de ui con ncurses
static void draw_ui(const state_t *st){
    int term_h, term_w; getmaxyx(stdscr, term_h, term_w);
    (void)term_h;
    int W = st->width, H = st->height;

    // Barra superior
    attron(COLOR_PAIR(10));
    mvprintw(0, 0, "ChompChamps | %dx%d | players=%u | over=%d",
             W, H, st->num_players, st->game_over);
    attroff(COLOR_PAIR(10));

    // Dimensiones de paneles
    int cell_w = 2;
    int bw = 2 + W*cell_w;  // board width
    int bh = 2 + H;         // board height

    int sw = 42;            // sidebar width
    int sh = (int)(2 + (int)st->num_players * 2 + 1);

    int margin_top = 2;
    int start_y = margin_top;
    int start_x_board = 2;

    int start_x_panel = start_x_board + bw + 2;
    int start_y_panel = start_y;

    // Si no entra lado a lado apila el panel abajo
    bool side_by_side = (start_x_panel + sw <= term_w - 1);
    if (!side_by_side){
        start_x_panel = start_x_board;
        start_y_panel = start_y + bh + 1;
    }

    static WINDOW *board_win = NULL;
    static WINDOW *panel_win = NULL;
    static int last_w = 0, last_h = 0;

    // Crear/redimensionar ventanas solo cuando cambia el tamaño
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

// CLI
static void usage(const char *p){
    fprintf(stderr, "Uso: %s [-w ancho -h alto]  o  %s ancho alto\n", p, p);
}


int main(int argc, char **argv){
    unsigned short W = 0, H = 0;

    // Parseo de parametros y fallback posicional
    int opt;
    while ((opt = getopt(argc, argv, "w:h:")) != -1){
        switch(opt){
            case 'w': W = (unsigned short)strtoul(optarg, NULL, 10); break;
            case 'h': H = (unsigned short)strtoul(optarg, NULL, 10); break;
            default: usage(argv[0]); return 2;
        }
    }

    if ((W == 0 || H == 0) && (optind + 1 < argc)) {
        W = (unsigned short)strtoul(argv[optind],     NULL, 10);
        H = (unsigned short)strtoul(argv[optind + 1], NULL, 10);
    }
    if (W == 0 || H == 0){ usage(argv[0]); return 2; }

    // Conexion a ambas shm
    state_t *st = ipc_open_and_map_state();
    if (!st){ perror("view: open state"); return 1; }
    sync_t  *sy = ipc_open_and_map_sync();
    if (!sy){ perror("view: open sync"); ipc_unmap_state(st); return 1; }

    if (st->width != W || st->height != H){
        fprintf(stderr, "view: advertencia: W/H recibidos (%u,%u) difieren de SHM (%u,%u)\n",
                (unsigned)W,(unsigned)H,(unsigned)st->width,(unsigned)st->height);
    }

    // Inicializacion de ncurses y colores
    ensure_term();
    if (initscr() == NULL){
        fprintf(stderr, "view: no pude inicializar ncurses (TERM=%s)\n", getenv("TERM"));
        ipc_unmap_sync(sy);
        ipc_unmap_state(st);
        return 1;
    }

    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); nodelay(stdscr, FALSE);
    curs_set(0);
    if (has_colors()) init_colors();

    // Bucle A/B
    while (1){
        sem_wait(&sy->A);           // 1. esperar pedido del master
        rw_reader_enter(sy);        // 2. leer estado como lector
        int over = st->game_over;
        draw_ui(st);                // 3. dibujar ui completa
        rw_reader_exit(sy);
        sem_post(&sy->B);           // 4. notificar al master que termine de imprimir
        if (over) break;            // salir si el juego termino
    }

    endwin();
    ipc_unmap_sync(sy);
    ipc_unmap_state(st);
    return 0;
}
