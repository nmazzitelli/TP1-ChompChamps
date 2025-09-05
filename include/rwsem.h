#pragma once
#include "sharedHeaders.h"

// lectores (E/F/D)
static inline void rw_reader_enter(sync_t *sy){
    sem_wait(&sy->E);
    sy->F++;
    if (sy->F == 1) sem_wait(&sy->D); // primer lector bloquea escritor
    sem_post(&sy->E);
}
static inline void rw_reader_exit(sync_t *sy){
    sem_wait(&sy->E);
    if (sy->F > 0) sy->F--;
    if (sy->F == 0) sem_post(&sy->D); // ultimo lector libera escritor
    sem_post(&sy->E);
}

// escritor (D)
static inline void rw_writer_enter(sync_t *sy){ sem_wait(&sy->D); }
static inline void rw_writer_exit (sync_t *sy){ sem_post(&sy->D); }
