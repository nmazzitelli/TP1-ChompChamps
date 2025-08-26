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
# Fuentes y binarios
# =========================
SRC  := $(wildcard src/*.c)
BIN  := $(patsubst src/%.c,bin/%,$(SRC))
DEPS := $(BIN:%=%.d)

# =========================
# COMPILACIÓN
# =========================
all: $(BIN)

# Regla patrón general: src/foo.c -> bin/foo
bin/%: src/%.c | bin
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# La vista necesita ncurses además de lo general
bin/view: LDLIBS += $(LIBS_VIEW)

bin:
	mkdir -p bin

clean:
	rm -rf bin

list:
	@echo "Fuentes:  $(SRC)"
	@echo "Binarios: $(BIN)"

-include $(DEPS)

# =========================
# EJECUCIÓN (dentro del contenedor)
# =========================
VIEW   ?= ./bin/view
MASTER ?= ./bin/master
W ?= 12
H ?= 12
D ?= 200
T ?= 10
S ?= 7

# Helper: espera a que exista la SHM que crea el master
wait-shm:
	@for i in $$(seq 1 50); do \
		if [ -e /dev/shm/game_state ]; then exit 0; fi; \
		sleep 0.1; \
	done; \
	echo "ERROR: /dev/shm/game_state no apareció" >&2; exit 1

# Demo/Run: lanzar master primero, esperar SHM, luego view
run-demo: all
	@echo "Lanzando master (bg) y luego view (fg)..."
	$(MASTER) -w $(W) -h $(H) -d $(D) -t $(T) -s $(S) &
	$(MAKE) wait-shm
	$(VIEW)

run: all
	@echo "Run: w=$(W) h=$(H) d=$(D) t=$(T) s=$(S)"
	$(MASTER) -w $(W) -h $(H) -d $(D) -t $(T) -s $(S) &
	$(MAKE) wait-shm
	$(VIEW)

kill:
	@pkill -f 'bin/view'   || true
	@pkill -f 'bin/master' || true

# =========================
# DEPENDENCIAS (instalar dentro del contenedor)
# =========================
deps:
	@set -e; \
	echo ">> Chequeando ncurses..."; \
	have_hdr=false; [ -f /usr/include/ncurses.h ] && have_hdr=true; \
	have_dev=$$(dpkg-query -W -f='$${Status}' libncurses5-dev 2>/dev/null | grep -q "install ok installed" && echo yes || echo no); \
	have_wdev=$$(dpkg-query -W -f='$${Status}' libncursesw5-dev 2>/dev/null | grep -q "install ok installed" && echo yes || echo no); \
	if $$have_hdr && [ "$$have_dev" = "yes" ] && [ "$$have_wdev" = "yes" ]; then \
		echo "✔ ncurses ya está instalado (headers y dev packages)"; \
	else \
		echo ">> Instalando ncurses..."; \
		apt-get update -y >/dev/null; \
		apt-get install -y --no-install-recommends libncurses5-dev libncursesw5-dev >/dev/null; \
		echo "✔ ncurses instalado"; \
	fi

deps-force:
	@apt-get update -y \
	 && apt-get install -y --no-install-recommends libncurses5-dev libncursesw5-dev \
	 && echo "✔ ncurses instalado (forzado)"

# =========================
# DOCKER (imagen de la cátedra, /work)
# =========================
IMAGE      ?= agodio/itba-so-multi-platform:3.0
CONTAINER  ?= chomp
MOUNT_PATH ?= /work

dpull:
	@docker pull $(IMAGE)

docker: dpull
	@docker rm -f $(CONTAINER) >/dev/null 2>&1 || true
	@docker run --rm -it --name $(CONTAINER) \
		-v "$$(pwd)":$(MOUNT_PATH) -w $(MOUNT_PATH) \
		$(IMAGE) bash

dexec:
	@docker exec -it $(CONTAINER) bash

dstop:
	@docker rm -f $(CONTAINER) >/dev/null 2>&1 || true

.PHONY: all clean list run run-demo kill deps deps-force dpull docker dexec dstop wait-shm