# ===== Imagen y contenedor =====
IMAGE      ?= agodio/itba-so-multi-platform:3.0
CONTAINER  ?= chomp
MOUNT_PATH ?= /work

# ===== Compilación dentro del contenedor =====
CC      := gcc
CFLAGS  := -Wall -Wextra -g -O0 -Iinclude
LDLIBS  := -pthread -lrt

W ?= 12
H ?= 12

# Abre una shell interactiva dentro del contenedor con tu repo montado.
# Desde esa shell vas a correr: make deps  y  make run
docker:
	@docker rm -f $(CONTAINER) >/dev/null 2>&1 || true
	@docker run --rm -it --name $(CONTAINER) \
		-v "$$(pwd)":$(MOUNT_PATH) -w $(MOUNT_PATH) \
		$(IMAGE) bash

# Instala ncurses (para futuro uso de la vista). Corre esto **dentro del contenedor**.
deps:
	@apt-get update -y >/dev/null && \
	apt-get install -y --no-install-recommends libncurses5-dev libncursesw5-dev >/dev/null && \
	echo "✔ ncurses instalado"

# Compila y prueba SOLO shm_tool (crea y lee las SHM). Corre esto **dentro del contenedor**.
run:
	@mkdir -p bin
	@$(CC) $(CFLAGS) -o bin/shm_tool src/shm_tool.c src/ipc.c $(LDLIBS)
	@./bin/shm_tool init $(W) $(H)
	@./bin/shm_tool open-info
	@echo "✔ Demo OK. Para borrar SHM: make destroy"

# Borra las SHM. Corre esto **dentro del contenedor**.
destroy:
	@./bin/shm_tool destroy || true

clean:
	@rm -rf bin src/*.o src/*.d
	@echo "✔ Clean hecho"

.PHONY: docker deps run destroy clean