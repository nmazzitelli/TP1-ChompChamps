#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#define TRUE 1
#define MAX_ARG_COUNT 23
#define MIN_BOARD_SIZE 10
#define MAX_PLAYERS 9
#define NAME_LEN 16

typedef struct {
    char name[NAME_LEN]; // Nombre del jugador
    unsigned int score; // Puntaje
    unsigned int inv_moves; // Cantidad de solicitudes de movimientos inválidas realizadas
    unsigned int v_moves; // Cantidad de solicitudes de movimientos válidas realizadas
    unsigned short pos_x, pos_y; // Coordenadas x e y en el tablero
    pid_t player_pid; // Identificador de proceso
    bool blocked; // Indica si el jugador tiene movimientos válidos disponibles
} player_t;

typedef struct{
    unsigned short width, height; // dimensiones
    unsigned int num_players; // cantidad de jugadores
    player_t players[MAX_PLAYERS]; // lista de jugadores
    bool game_over; // si el juego termino o no
    int board[]; // tablero dinamico
} state_t;

typedef struct {
    sem_t print_needed; // Se usa para indicarle a la vista que hay cambios por imprimir
    sem_t print_done; // Se usa para indicarle al master que la vista terminó de imprimir
    sem_t master_utd; // Mutex para evitar inanición del master al acceder al estado
    sem_t game_state_change; // Mutex para el estado del juego
    sem_t sig_var; // Mutex para la siguiente variable
    unsigned int readers; // Cantidad de jugadores leyendo el estado
} sync_t;


//creates shared memory
void * createSHM(char * name, size_t size, mode_t mode){
    int fd;
    fd = shm_open(name, O_RDWR | O_CREAT, mode); 
    if(fd == -1){                                
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    if(strcmp(name, "/game_state") == 0){
        state_t temp_game;
        read(fd, &temp_game, sizeof(state_t));
        size = sizeof(state_t) + temp_game.width * temp_game.height * sizeof(int);
    }

    if(-1 == ftruncate(fd, size)){
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }

    void * p = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if(p == MAP_FAILED){
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    return p;
}

int process_players(char ** argv, int argc, int idx, state_t * game_state){
    int i = idx;
    if(argv[i][0] == '-' || argv[i][0] == ' ' || argv[i][0] == '\n'){
        perror("bad usage of -p [players]");
        exit(EXIT_FAILURE);
    }

    while(argv[i][0] !=  '-' && i < argc){
        //initialize players
        printf("initializing players");
        i++;
    }

    return i; 
}


void arg_handler(int argc, char ** argv, state_t * game_state){
    if(argc > MAX_ARG_COUNT){
        perror("too many arguments");
        exit(EXIT_FAILURE);
    }

    if(argc < 2){
        perror("Error: At least one player must be specified using -p.");
        exit(EXIT_FAILURE);
    }

    int i = 1;
    while(i < argc){
        if(strcmp(argv[i], "-p") == 0){
            i = process_players(argv, argc, i + 1, game_state);
        }
        if(strcmp(argv[i], "-v") == 0){
            
        }
    }
}

int main(int argc, char **argv) {
    // 1) Crear /game_state con sólo el header
    size_t header_size = sizeof(state_t);
    state_t *game_state = createSHM("/game_state", header_size, 0644);

    // setear dimensiones mínimas para la demo
    game_state->width = 10;
    game_state->height = 10;
    game_state->num_players = 0;
    game_state->game_over = false;

    // 2) Redimensionar /game_state al tamaño real y remapear
    int fd_state = shm_open("/game_state", O_RDWR, 0);
    if (fd_state == -1) { perror("shm_open /game_state"); exit(EXIT_FAILURE); }

    size_t full_size = sizeof(state_t) + game_state->width * game_state->height * sizeof(int);
    if (ftruncate(fd_state, (off_t)full_size) == -1) { perror("ftruncate /game_state"); exit(EXIT_FAILURE); }

    // remapear con el tamaño nuevo
    munmap(game_state, header_size);
    game_state = mmap(NULL, full_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_state, 0);
    if (game_state == MAP_FAILED) { perror("mmap resized /game_state"); exit(EXIT_FAILURE); }

    // 3) Inicializar tablero
    int cells = game_state->width * game_state->height;
    for (int i = 0; i < cells; i++) game_state->board[i] = (i % 9); // 0..8

    // 4) Crear /game_sync + semáforos
    sync_t *sync = createSHM("/game_sync", sizeof(sync_t), 0666);
    sem_init(&sync->print_needed, 1, 0);
    sem_init(&sync->print_done,   1, 0);
    sem_init(&sync->master_utd,   1, 1);
    sem_init(&sync->game_state_change, 1, 1);
    sem_init(&sync->sig_var, 1, 1);
    sync->readers = 0;

    // 5) Disparar una impresión y esperar a la vista
    sem_post(&sync->print_needed);
    sem_wait(&sync->print_done);

    // 6) Terminar el juego y avisar a la vista para que salga
    game_state->game_over = true;
    sem_post(&sync->print_needed);


    return 0;
}
