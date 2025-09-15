// This is a personal academic project.
// Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#define _POSIX_C_SOURCE 200809L
#include "ipc.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

size_t ipc_state_size(unsigned short w, unsigned short h) {
    // sizeof(state_t) incluye el header; sumamos el flexible array board[]
    return sizeof(state_t) + (size_t)w * (size_t)h * sizeof(int);
}

// helpers internos
static int create_or_open(const char *name, bool *created) {
    if (created) *created = false;
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0660);
    if (fd >= 0) { if (created) *created = true; return fd; }
    if (errno != EEXIST) return -1;
    return shm_open(name, O_RDWR, 0660);
}

static void *map_fd(int fd, size_t sz) {
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return (p == MAP_FAILED) ? NULL : p;
}

// /game_state
state_t* ipc_create_and_map_state(unsigned short w, unsigned short h, bool *existed) {
    bool created = false;
    int fd = create_or_open(SHM_STATE, &created);
    if (fd < 0) return NULL;

    size_t sz = ipc_state_size(w, h);
    if (ftruncate(fd, (off_t)sz) != 0) {
        int e = errno; close(fd);
        if (created) shm_unlink(SHM_STATE);
        errno = e; return NULL;
    }
    state_t *st = (state_t*)map_fd(fd, sz);
    if (!st) {
        if (created) shm_unlink(SHM_STATE);
        return NULL;
    }

    if (created) {
        memset(st, 0, sz);
        st->width = w;
        st->height = h;
        st->num_players = 0;
        st->game_over = false;
        // board[] queda en 0, el master luego lo pobla (1 a 9) según seed
    }
    if (existed) *existed = !created;
    return st;
}

state_t* ipc_open_and_map_state(void) {
    // 1) Intentar RW (para cuando el master es el nuestro y permite escritura)
    int fd = shm_open(SHM_STATE, O_RDWR, 0);
    if (fd >= 0) {
        struct stat stbuf;
        if (fstat(fd, &stbuf) != 0) { int e = errno; close(fd); errno = e; return NULL; }
        void *p = mmap(NULL, (size_t)stbuf.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        int e = errno; close(fd); errno = e;
        return (p == MAP_FAILED) ? NULL : (state_t*)p;
    }

    // 2) Si falló por permisos (caso master cátedra)
    if (errno == EACCES || errno == EPERM) {
        fd = shm_open(SHM_STATE, O_RDONLY, 0);
        if (fd < 0) return NULL;

        struct stat stbuf;
        if (fstat(fd, &stbuf) != 0) { int e = errno; close(fd); errno = e; return NULL; }
        void *p = mmap(NULL, (size_t)stbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
        int e = errno; close(fd); errno = e;
        return (p == MAP_FAILED) ? NULL : (state_t*)p;
    }

    // 3) Otros errores
    return NULL;
}


void ipc_unmap_state(state_t *st) {
    if (!st) return;
    // Para desmapear necesitamos el tamaño, lo inferimos del header
    size_t sz = ipc_state_size(st->width, st->height);
    munmap(st, sz);
}

int ipc_unlink_state(void) {
    return shm_unlink(SHM_STATE);
}

// /game_sync
sync_t* ipc_create_and_map_sync(bool *created) {
    bool was_created = false;
    int fd = create_or_open(SHM_SYNC, &was_created);
    if (fd < 0) return NULL;

    if (ftruncate(fd, (off_t)sizeof(sync_t)) != 0) {
        int e = errno; close(fd);
        if (was_created) shm_unlink(SHM_SYNC);
        errno = e; return NULL;
    }

    sync_t *sy = (sync_t*)map_fd(fd, sizeof(sync_t));
    if (!sy) {
        if (was_created) shm_unlink(SHM_SYNC);
        return NULL;
    }

    if (was_created) memset(sy, 0, sizeof(*sy));
    if (created) *created = was_created;
    return sy;
}

sync_t* ipc_open_and_map_sync(void) {
    int fd = shm_open(SHM_SYNC, O_RDWR, 0660);
    if (fd < 0) return NULL;
    return (sync_t*)map_fd(fd, sizeof(sync_t));
}

void ipc_unmap_sync(sync_t *sy) {
    if (sy) munmap(sy, sizeof(*sy));
}

int ipc_unlink_sync(void) {
    return shm_unlink(SHM_SYNC);
}

int ipc_init_sync_semaphores(sync_t *sy) {
    if (!sy) { errno = EINVAL; return -1; }

    // pshared=1 → entre procesos
    if (sem_init(&sy->A, 1, 0) != 0) return -1;  // A: solicitud de impresion
    if (sem_init(&sy->B, 1, 0) != 0) return -1;  // B: impresion completada
    if (sem_init(&sy->C, 1, 1) != 0) return -1;  // C: acceso ordenado
    if (sem_init(&sy->D, 1, 1) != 0) return -1;  // D: bloquea escritores lectores
    if (sem_init(&sy->E, 1, 1) != 0) return -1;  // E: mutex del contador de lectores para proteger F
    sy->F = 0;                                   // F: cantidad de lectores activos
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (sem_init(&sy->G[i], 1, 0) != 0) return -1; // G[i] compuerta por jugador
    }
    return 0;
}