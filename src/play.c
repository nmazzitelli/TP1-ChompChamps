// This is a personal academic project.
// Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ncurses.h>

#define NUM_OPTIONS 5

// rangos como los del master
static const int MIN_W = 10;		// minimo 10x10
static const int MAX_W = 65535;
static const int MIN_H = 10;		// minimo 10x10
static const int MAX_H = 65535;
static const int S_MIN = 0;			// 0 seg = sin timeout
static const int S_MAX = 3600;
static const int N_MIN = 1;			// minimo 1 jugador
static const int N_MAX = 9;       	// master soporta hasta 9 jugadores
static const int STEP_MIN = 0;
static const int STEP_MAX = 5000;

static int clamp_int(int v, int lo, int hi){ if (v < lo) return lo; if (v > hi) return hi; return v; }

int main(void) {
    int values[NUM_OPTIONS] = {20, 20, 120, 2, 50}; // defaults: W,H,S,N,STEP_MS
    const char *base_labels[NUM_OPTIONS] = {
        "Ancho del tablero (W)",
        "Alto del tablero (H)",
        "Tiempo visible (S, seg)",
        "Cantidad de jugadores (N)",
        "Velocidad STEP_MS (ms)"
    };

    int highlight = 0;
    int ch;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    while (1) {
        clear();
        mvprintw(0, 0, "=== ChompChamps ===");
        mvprintw(1, 0, "Use flechas para moverse, Enter para editar, 'q' para salir y 'r' para correr.");

        for (int i = 0; i < NUM_OPTIONS; i++) {
            if (i == highlight) {
                attron(A_REVERSE);
            }

            int lo = 0;
            switch (i) {
                case 0: lo = MIN_W; break;
                case 1: lo = MIN_H; break;
                case 2: lo = S_MIN; break;
                case 3: lo = N_MIN; break;
                case 4: lo = STEP_MIN; break;
            }

            mvprintw(3 + i, 2, "%s minimo: %d valor: %d", base_labels[i], lo, values[i]);

            if (i == highlight) {
                attroff(A_REVERSE);
            }
        }

        ch = getch();
        if (ch == 'q') {
            break;
        } else if (ch == KEY_UP) {
            highlight = (highlight - 1 + NUM_OPTIONS) % NUM_OPTIONS;
        } else if (ch == KEY_DOWN) {
            highlight = (highlight + 1) % NUM_OPTIONS;
        } else if (ch == '\n') {
            echo();
            mvprintw(10, 0, "Ingrese nuevo valor para %s (minimo %d): ",
                     base_labels[highlight],
                     (highlight==0?MIN_W : (highlight==1?MIN_H : (highlight==2?S_MIN : (highlight==3?N_MIN:STEP_MIN))))
                     );
            refresh();
            int v = values[highlight];
            if (scanw("%d", &v) == 1) {
                switch (highlight) {
                    case 0: v = clamp_int(v, MIN_W, MAX_W); break;
                    case 1: v = clamp_int(v, MIN_H, MAX_H); break;
                    case 2: v = clamp_int(v, S_MIN, S_MAX); break;
                    case 3: v = clamp_int(v, N_MIN, N_MAX); break;
                    case 4: v = clamp_int(v, STEP_MIN, STEP_MAX); break;
                }
                values[highlight] = v;
                mvprintw(11, 0, "valor seteado a %d (rango aplicado)", v);
            } else {
                mvprintw(11, 0, "entrada invalida, se mantiene %d", values[highlight]);
            }
            noecho();
            refresh();
            napms(700);
        } else if (ch == 'r') {
            // Minimas validaciones
            if (values[0] < MIN_W) values[0] = MIN_W;
            if (values[0] > MAX_W) values[0] = MAX_W;
            if (values[1] < MIN_H) values[1] = MIN_H;
            if (values[1] > MAX_H) values[1] = MAX_H;
            if (values[2] < S_MIN) values[2] = S_MIN;
            if (values[2] > S_MAX) values[2] = S_MAX;
            if (values[3] < N_MIN) values[3] = N_MIN;
            if (values[3] > N_MAX) values[3] = N_MAX;
            if (values[4] < STEP_MIN) values[4] = STEP_MIN;
            if (values[4] > STEP_MAX) values[4] = STEP_MAX;

            // Exportar variables de entorno para player y view
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", values[3]);
            setenv("NPLAYERS", buf, 1);
            snprintf(buf, sizeof(buf), "%d", values[4]);
            setenv("STEP_MS", buf, 1);

            // Preparar argumentos para master
            char wbuf[16], hbuf[16], sbuf[16], stepbuf[16];
            snprintf(wbuf, sizeof(wbuf), "%d", values[0]);
            snprintf(hbuf, sizeof(hbuf), "%d", values[1]);
            snprintf(sbuf, sizeof(sbuf), "%d", values[2]);
            snprintf(stepbuf, sizeof(stepbuf), "%d", values[4]);

            // construir argv
            const char *player_path = "./bin/player";
            const char *view_path = "./bin/view";
            char *argv[64];
            int ai = 0;
            argv[ai++] = "master";
            argv[ai++] = "-w"; argv[ai++] = wbuf;
            argv[ai++] = "-h"; argv[ai++] = hbuf;
            argv[ai++] = "-v"; argv[ai++] = (char *)view_path;
            for (int i = 0; i < values[3] && ai + 2 < (int)(sizeof(argv)/sizeof(argv[0])); ++i) {
                argv[ai++] = "-p";
                argv[ai++] = (char *)player_path;
            }
            argv[ai++] = "-d"; argv[ai++] = stepbuf;
            argv[ai++] = "-t"; argv[ai++] = sbuf;
            argv[ai] = NULL;

            endwin(); // cerrar ncurses antes de exec
            execvp("./bin/master", argv);
            perror("execlp master");
            exit(1);
        }
    }

    endwin();
    return 0;
}
