# ===== Imagen y contenedor =====
IMAGE      ?= agodio/itba-so-multi-platform:3.0
CONTAINER  ?= chomp
MOUNT_PATH ?= /work

# ===== Compilación dentro del contenedor =====
CC      := gcc
CFLAGS  := -Wall -Wextra -g -O0 -Iinclude -MMD -MP
LDLIBS  := -pthread -lrt

W ?= 12
H ?= 12

# Abre una shell interactiva dentro del contenedor con tu repo montado.
# Desde esa shell vas a correr: make deps  y  make run  /  make run-demo
docker:
	@docker rm -f $(CONTAINER) >/dev/null 2>&1 || true
	@docker run --rm -it --name $(CONTAINER) \
		-v "$$(pwd)":$(MOUNT_PATH) -w $(MOUNT_PATH) \
		$(IMAGE) bash

# Instala ncurses (para la vista). Corre esto **dentro del contenedor**.
deps:
	@apt-get update -y >/dev/null && \
	apt-get install -y --no-install-recommends libncurses5-dev libncursesw5-dev >/dev/null && \
	echo "✔ ncurses instalado"

# ====== Build de master/view (adentro del contenedor) ======
BIN := bin
SRC := src

SRCS_COMMON := $(SRC)/ipc.c
OBJS_COMMON := $(SRCS_COMMON:.c=.o)

# Generar deps automáticas y compilar includes de tu carpeta
CFLAGS += -MMD -MP -Iinclude

# Asegura que exista bin/
$(BIN):
	@mkdir -p $(BIN)

# Regla genérica para .o en src/
$(SRC)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Ejecutables
$(BIN)/master: $(BIN) $(SRC)/master.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $(SRC)/master.c $(OBJS_COMMON) $(LDLIBS)

$(BIN)/view: $(BIN) $(SRC)/view.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $(SRC)/view.c $(OBJS_COMMON) $(LDLIBS) -lncurses

# Mantengo tu demo de shm_tool, sumo build general y run-demo estático
build: $(BIN)/shm_tool $(BIN)/master $(BIN)/view
	@echo "✔ build ok"

run-demo: build
	@./bin/master $(W) $(H)

# Incluir dependencias generadas por -MMD
-include $(SRC)/*.d