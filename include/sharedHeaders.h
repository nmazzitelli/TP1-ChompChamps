#ifndef SHAREDHEADERS_H
#define SHAREDHEADERS_H

#include <stdbool.h>
#include <semaphore.h>
#include <sys/types.h>   // pid_t

// Constantes compartidas
#define NAME_LEN     16
#define MAX_PLAYERS   9

// ---- Tipos compartidos ----
typedef struct {
    char name[NAME_LEN];
    unsigned int score;
    unsigned int inv_moves;
    unsigned int v_moves;
    unsigned short pos_x, pos_y;
    pid_t player_pid;
    bool blocked;
} player_t;

typedef struct {
    unsigned short width, height;      // dimensiones
    unsigned int num_players;          // cantidad de jugadores
    player_t players[MAX_PLAYERS];     // lista de jugadores
    bool game_over;                    // fin del juego
    int board[];                       // flexible array del tablero
} state_t;

typedef struct {
    sem_t print_needed;       // máster -> vista: hay cambios
    sem_t print_done;         // vista  -> máster: terminé de imprimir
    sem_t master_utd;         // para evitar inanición del máster
    sem_t game_state_change;  // mutex del estado del juego
    sem_t sig_var;            // mutex de 'readers'
    unsigned int readers;     // cantidad de lectores
    // Si luego agregan semáforos por jugador, van acá:
    // sem_t player_gate[MAX_PLAYERS];
} sync_t;

#endif // SHAREDHEADERS_H
