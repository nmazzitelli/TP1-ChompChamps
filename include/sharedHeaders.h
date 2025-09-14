// This is a personal academic project.
// Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#ifndef SHAREDHEADERS_H
#define SHAREDHEADERS_H

#include <stdbool.h>
#include <semaphore.h>
#include <sys/types.h>   // pid_t

// constantes compartidas
#define SHM_STATE     "/game_state"
#define SHM_SYNC      "/game_sync"
#define NAME_LEN      16
#define MAX_PLAYERS    9

// direcciones 0 a 7 (arriba y sentido horario)
typedef enum {
    DIR_UP = 0,       // (0,-1)
    DIR_UP_RIGHT,     // (+1,-1)
    DIR_RIGHT,        // (+1, 0)
    DIR_DOWN_RIGHT,   // (+1,+1)
    DIR_DOWN,         // (0,+1)
    DIR_DOWN_LEFT,    // (-1,+1)
    DIR_LEFT,         // (-1, 0)
    DIR_UP_LEFT       // (-1,-1)
} direction_t;

// desplazamientos
static const int DX[8] = { 0, 1, 1, 1, 0,-1,-1,-1 };
static const int DY[8] = {-1,-1, 0, 1, 1, 1, 0,-1 };

static inline int  idx_xy(int x,int y,int W){ return y*W + x; }
static inline bool in_bounds(int x,int y,int W,int H){
    return (0<=x && x<W && 0<=y && y<H);
}

// esquema de celdas segun enunciado:
// libre: 1..9 (recompensa)
// capturada por jugador id: v = -id, con id en [0..8] => valores en [-8..0]
static inline int cell_owner(int v){
    return (v <= 0) ? -v : -1; // -1 => libre
}

// estado de cada jugador
typedef struct {
    char name[NAME_LEN];
    unsigned int score;
    unsigned int inv_moves;
    unsigned int v_moves;
    unsigned short pos_x;
    unsigned short pos_y;
    pid_t player_pid;
    bool  blocked;
} player_t;

// estado global (SHM_STATE)
typedef struct {
    unsigned short width;
    unsigned short height;
    unsigned int   num_players;
    player_t       players[MAX_PLAYERS];
    bool           game_over;
    int            board[]; // flexible array: ints fila-0 a fila-(H-1)
} state_t;

// sincronizacion (SHM_SYNC)
typedef struct {
    sem_t A;                // master -> view: hay cambios
    sem_t B;                // view  -> master: termine
    sem_t C;                // turnstile
    sem_t D;                // mutex escritor del estado
    sem_t E;                // mutex readers
    unsigned int F;         // cantidad de lectores
    sem_t G[MAX_PLAYERS];   // gate por jugador
} sync_t;

#endif // SHAREDHEADERS_H
