# =========================
# Config detección de SO
# =========================
UNAME_S := $(shell uname -s)

CC      := gcc
CFLAGS  := -Wall -Wextra -g -O0 -Iinclude -MMD -MP
LDLIBS  := -pthread
ifeq ($(UNAME_S),Linux)          # Docker / Linux (imagen oficial)
  LDLIBS += -lrt                 # librt en Linux
else                             # macOS (solo por conveniencia local)
  CFLAGS += -Wno-deprecated-declarations
endif

# ncurses sólo para la vista
LIBS_VIEW := -lncurses

# =========================
# Rutas y archivos
# =========================
SRC_DIR := src
BIN_DIR := bin

SRCS_COMMON := $(SRC_DIR)/ipc.c
OBJS_COMMON := $(SRCS_COMMON:.c=.o)

MASTER := $(BIN_DIR)/master
VIEW   := $(BIN_DIR)/view
PLAYER := $(BIN_DIR)/player
SHMTOOL:= $(BIN_DIR)/shm_tool

# =========================
# Docker (host)
# =========================
IMAGE := agodio/itba-so-multi-platform:3.0
CONTAINER_NAME := itba-so-tp1

.PHONY: docker deps build all clean demo_view run_master destroy_shm

docker:
	@echo ">> Lanzando contenedor $(CONTAINER_NAME) con imagen $(IMAGE)"
	docker run --rm -it --name $(CONTAINER_NAME) -v "$$(pwd)":/work -w /work $(IMAGE) bash

# =========================
# Build
# =========================
all: build

build: $(BIN_DIR) $(MASTER) $(VIEW) $(PLAYER) $(SHMTOOL)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(MASTER): $(SRC_DIR)/master.c $(OBJS_COMMON) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(VIEW): $(SRC_DIR)/view.c $(OBJS_COMMON) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) $(LIBS_VIEW)

$(PLAYER): $(SRC_DIR)/player.c $(OBJS_COMMON) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(SHMTOOL): $(SRC_DIR)/shm_tool.c $(OBJS_COMMON) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# =========================
# Deps (dentro del contenedor)
# =========================
deps:
	@echo ">> Instalando dependencias (ncurses)..."
	@apt-get update -y && apt-get install -y --no-install-recommends \
	  libncurses5-dev libncursesw5-dev >/dev/null && echo "✔ ncurses instalado"

# =========================
# Demos/Helpers (Acorde a TU master: ./master W H [secs])
# =========================
W ?= 14
H ?= 10
S ?= 8     # segundos que queda visible

# limpia SHMs viejas (muy recomendable antes de probar)
destroy_shm: build
	$(SHMTOOL) destroy || true

# demo para ver la vista que forkea el master
demo_view: build
	@echo ">> Demo vista: W=$(W) H=$(H) S=$(S)s"
	TERM=xterm-256color $(MASTER) $(W) $(H) $(S)

# ejecución manual con args crudos (por compatibilidad)
run_master: build
	@echo ">> Ejecutando master con: $(ARGS)"
	TERM=xterm-256color $(MASTER) $(ARGS)

# =========================
# Limpieza
# =========================
clean:
	rm -f $(SRC_DIR)/*.o $(SRC_DIR)/*.d $(BIN_DIR)/*
	@echo "✔ clean"

-include $(SRC_DIR)/*.d