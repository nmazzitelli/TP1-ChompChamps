#pragma once
#include "sharedHeaders.h"

// Readers-writers with turnstile to avoid writer starvation.
// Semaphores in sync_t used:
//   C: turnstile (blocks new readers when a writer is waiting)
//   D: room-empty (writer lock; readers hold it while active)
//   E: mutex protecting F (reader count)
//   F: active reader count

static inline void rw_reader_enter(sync_t *sy){
    // quick pass through turnstile: if a writer has sem_wait'd C, this blocks here
    sem_wait(&sy->C);
    sem_post(&sy->C);

    // increment reader count
    sem_wait(&sy->E);
    sy->F++;
    if (sy->F == 1) {
        // first reader locks writers out
        sem_wait(&sy->D);
    }
    sem_post(&sy->E);
}

static inline void rw_reader_exit(sync_t *sy){
    sem_wait(&sy->E);
    if (sy->F > 0) sy->F--;
    if (sy->F == 0) {
        // last reader allows writers
        sem_post(&sy->D);
    }
    sem_post(&sy->E);
}

// writer obtains turnstile to block new readers, then waits for room to be empty
static inline void rw_writer_enter(sync_t *sy){
    // block turnstile so new readers wait here
    sem_wait(&sy->C);
    // wait until no active readers
    sem_wait(&sy->D);
    // now writer has exclusive access (holds both C and D)
}

static inline void rw_writer_exit (sync_t *sy){
    // release writer lock first, then open the turnstile for waiting readers
    sem_post(&sy->D);
    sem_post(&sy->C);
}