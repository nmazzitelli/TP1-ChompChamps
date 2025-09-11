// This is a personal academic project.
// Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#ifndef CHOMP_IPC_H
#define CHOMP_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include "sharedHeaders.h"

// Tamaño real de /game_state según W x H
size_t ipc_state_size(unsigned short width, unsigned short height);

// Crea (o re-crea) /game_state con tamaño W*H y la mapea (RW, MAP_SHARED).
// Si existed==true, la SHM ya existía (y se reusó+truncó).
state_t* ipc_create_and_map_state(unsigned short w, unsigned short h, bool *existed);

// Abre /game_state existente y la mapea (RW, MAP_SHARED).
state_t* ipc_open_and_map_state(void);

// Desmapea /game_state
void ipc_unmap_state(state_t *st);

// Elimina /game_state
int ipc_unlink_state(void);

// ---- /game_sync ----

// Crea /game_sync (tamaño fijo) y la mapea (RW). Inicializa semáforos si *created = true.
sync_t* ipc_create_and_map_sync(bool *created);

// Abre /game_sync existente
sync_t* ipc_open_and_map_sync(void);

// Desmapea /game_sync
void ipc_unmap_sync(sync_t *sy);

// Elimina /game_sync
int ipc_unlink_sync(void);

// Inicializa todos los semáforos de sync_t con pshared=1
int ipc_init_sync_semaphores(sync_t *sy);

// Limpia ambos: shm_unlink de /game_state y /game_sync
static inline void ipc_unlink_all(void) {
    ipc_unlink_state();
    ipc_unlink_sync();
}

#endif // CHOMP_IPC_H