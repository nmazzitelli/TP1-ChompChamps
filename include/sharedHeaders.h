#ifndef SHAREDHEADERS_H
#define SHAREDHEADERS_H

#include <stdbool.h>
#include <semaphore.h>
#include <sys/types.h>   // pid_t

// Constantes compartidas

#define SHM_STATE    "/game_state"
#define SHM_SYNC     "/game_sync"
#define NAME_LEN     16
#define MAX_PLAYERS   9

// Direcciones de movimiento
// Códigos 0..7, empezando en arriba y en sentido horario
typedef enum {
    DIR_UP = 0,          // (0,-1)
    DIR_UP_RIGHT = 1,    // (+1,-1)
    DIR_RIGHT = 2,       // (+1, 0)
    DIR_DOWN_RIGHT = 3,  // (+1,+1)
    DIR_DOWN = 4,        // (0,+1)
    DIR_DOWN_LEFT = 5,   // (-1,+1)
    DIR_LEFT = 6,        // (-1, 0)
    DIR_UP_LEFT = 7      // (-1,-1)
} direction_t;

// Estado de cada jugador
typedef struct {
    char name[NAME_LEN];     // Nombre del jugador
    unsigned int score;      // Puntaje
    unsigned int inv_moves;  // Cantidad de movimientos inválidos
    unsigned int v_moves;    // Cantidad de movimientos válidos
    unsigned short pos_x;    // Coordenada x
    unsigned short pos_y;    // Coordenada y
    pid_t player_pid;        // PID del proceso jugador
    bool blocked;            // true si se detectó EOF en su pipe
} player_t;

// Estado global del juego
// Contiene dimensiones, jugadores, flag de fin y el tablero
typedef struct {
    unsigned short width;                 // Ancho
    unsigned short height;                // Alto
    unsigned int   num_players;           // Cantidad de jugadores
    player_t       players[MAX_PLAYERS];  // Lista de jugadores
    bool           game_over;             // Terminado?
    int            board[];               // Tablero: ints fila-0, fila-1, ...
} state_t;

// Sincronización (SHM_SYNC)
typedef struct {
    sem_t A;                // master -> vista: hay cambios
    sem_t B;                // vista  -> master: terminé de imprimir
    sem_t C;                // mutex para evitar inanición del master
    sem_t D;                // mutex para el estado del juego
    sem_t E;                // mutex para variable readers
    unsigned int F;         // cantidad de lectores
    sem_t G[MAX_PLAYERS];   // un semáforo por jugador
} sync_t;

#endif // SHAREDHEADERS_H