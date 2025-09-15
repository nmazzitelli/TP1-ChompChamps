// This is a personal academic project.
// Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#pragma once
#include "sharedHeaders.h"

// Readers-writers with turnstile to avoid writer starvation.
// Semaphores in sync_t used:
//   C: Mutex para evitar inanición del máster al acceder al estado
//   D: Mutex para el estado del juego
//   E: Mutex para la siguiente variable
//   F: Cantidad de jugadores leyendo el estado

static inline void rw_reader_enter(sync_t *sy){
    // C: acceso ordenado, si un escritor tomo C (sem_wait), el lector espera
    sem_wait(&sy->C);
    sem_post(&sy->C);

    // E/F: protege y actualiza la cantidad de lectores activos
    sem_wait(&sy->E);
    sy->F++;
    if (sy->F == 1) {
        // Primer lector, toma D para bloquear a los escritores
        sem_wait(&sy->D);
    }
    sem_post(&sy->E);
}

static inline void rw_reader_exit(sync_t *sy){
    sem_wait(&sy->E);
    if (sy->F > 0) sy->F--;
    if (sy->F == 0) {
        // el ultimo lector permite escritores
        sem_post(&sy->D);
    }
    sem_post(&sy->E);
}

// el escritor toma el acceso ordenado (C) para frenar nuevos lectores y despues espera a D
static inline void rw_writer_enter(sync_t *sy){
    // C: bloquea que entren nuevos lectores
    sem_wait(&sy->C);
    // D: espera hasta que no haya lectores activos
    sem_wait(&sy->D);
    // ahora el escritor tiene acceso
}

static inline void rw_writer_exit (sync_t *sy){
    // libera primero D y luego C
    sem_post(&sy->D);
    sem_post(&sy->C);
}