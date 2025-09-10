#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ncurses.h>

#define NUM_OPTIONS 5

int main(void) {
    int values[NUM_OPTIONS] = {20, 20, 100, 2, 100}; // defaults: W,H,S,N,STEP_MS
    const char *labels[NUM_OPTIONS] = {
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
        mvprintw(0, 0, "=== ChompChamps Demo2 ===");
        mvprintw(1, 0, "Use flechas para moverse, Enter para editar, 'q' para salir y 'r' para correr.");

        for (int i = 0; i < NUM_OPTIONS; i++) {
            if (i == highlight) {
                attron(A_REVERSE);
            }
            mvprintw(3 + i, 2, "%s: %d", labels[i], values[i]);
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
            mvprintw(10, 0, "Ingrese nuevo valor para %s: ", labels[highlight]);
            refresh();
            scanw("%d", &values[highlight]);
            noecho();
        } else if (ch == 'r') {
            // Minimal validations
            if (values[0] < 10) values[0] = 10;
            if (values[1] < 10) values[1] = 10;
            if (values[3] < 1) values[3] = 1;
            if (values[3] > 9) values[3] = 9;
            if (values[4] < 0) values[4] = 0;

            // Export env if you still want them (optional)
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", values[3]);
            setenv("NPLAYERS", buf, 1);
            snprintf(buf, sizeof(buf), "%d", values[4]);
            setenv("STEP_MS", buf, 1);

            // Prepare args for master (use flags and repeated -p)
            char wbuf[16], hbuf[16], sbuf[16], stepbuf[16];
            snprintf(wbuf, sizeof(wbuf), "%d", values[0]);
            snprintf(hbuf, sizeof(hbuf), "%d", values[1]);
            snprintf(sbuf, sizeof(sbuf), "%d", values[2]);
            snprintf(stepbuf, sizeof(stepbuf), "%d", values[4]);

            // build argv vector
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

            endwin(); // close ncurses before exec
            execvp("./bin/master", argv);
            perror("execlp master");
            exit(1);
        }
    }

    endwin();
    return 0;
}
