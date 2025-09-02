# =========================
# Config detección de SO
# =========================
UNAME_S := $(shell uname -s)

CC      := gcc
CFLAGS  := -Wall -Wextra -g -O0 -Iinclude -MMD -MP
LDLIBS  := -pthread -lm
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

.PHONY: docker deps build all clean demo demo_view run_master destroy_shm check_term

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
	@apt-get update -y >/dev/null && apt-get install -y --no-install-recommends \
	  libncurses5-dev libncursesw5-dev ncurses-term >/dev/null && echo "✔ ncurses instalado"

# =========================
# Helpers de ejecución
# =========================
SAFE_TERM := $(shell sh -c 't="$${TERM:-xterm-256color}"; echo "$$t" | sed s/_/-/g')

W ?= 20
H ?= 20
S ?= 100
NPLAYERS ?= 2
STEP_MS ?= 100

destroy_shm: build
	$(SHMTOOL) destroy || true

check_term:
	@echo ">> TERM del entorno: '$(TERM)'  -> usando normalizado: '$(SAFE_TERM)'"
	@infocmp $(SAFE_TERM) >/dev/null 2>&1 && echo "✔ terminfo OK para $(SAFE_TERM)" || echo "✖ terminfo NO encontrada (instalar ncurses-term)"

demo: build
	@echo ">> Demo: W=$(W) H=$(H) S=$(S)s NPLAYERS=$(NPLAYERS) STEP_MS=$(STEP_MS)ms"
	@echo ">> Usando TERM=$(SAFE_TERM)"
	@env TERM=$(SAFE_TERM) NPLAYERS=$(NPLAYERS) STEP_MS=$(STEP_MS) $(MASTER) $(W) $(H) $(S)

demo_view: demo

run_master: build
	@echo ">> Ejecutando master con: $(ARGS) | NPLAYERS=$(NPLAYERS) STEP_MS=$(STEP_MS)ms"
	@echo ">> Usando TERM=$(SAFE_TERM)"
	@env TERM=$(SAFE_TERM) NPLAYERS=$(NPLAYERS) STEP_MS=$(STEP_MS) $(MASTER) $(ARGS)

# =========================
# Limpieza
# =========================
clean:
	rm -f $(SRC_DIR)/*.o $(SRC_DIR)/*.d $(BIN_DIR)/*
	@echo "✔ clean"

#=========================
# Demo 2 (local, no en Docker)
#=========================

bin/demo2: src/demo2.c
	$(CC) $(CFLAGS) -o bin/demo2 src/demo2.c -lncurses

demo2: bin/demo2 bin/master bin/player bin/view
	./bin/demo2




-include $(SRC_DIR)/*.d