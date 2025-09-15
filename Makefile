###############################################################################
# Makefile mínimo para TP1 (versión simplificada)
# Comandos usados: make docker | make deps | make clean | make build | make play
# Agregado: make run-catedra  (ejecuta el máster de la cátedra con tu view/player)
###############################################################################

UNAME_S := $(shell uname -s)
CC = gcc
CFLAGS = -Wall -Wextra -Wconversion -Wshadow -Wformat=2 -g -O0 -Iinclude -D_POSIX_C_SOURCE=200809L
LDLIBS = -pthread -lm
ifeq ($(UNAME_S),Linux)
  LDLIBS += -lrt
else
  CFLAGS += -Wno-deprecated-declarations
endif
LIBS_VIEW = -lncurses

SRCDIR = src
BINDIR = bin
OBJDIR = obj

# Fuentes necesarias (sin shm_tool ni extras, y SIN ipc_ro.c)
SRCS = $(SRCDIR)/ipc.c $(SRCDIR)/master.c $(SRCDIR)/player.c $(SRCDIR)/view.c $(SRCDIR)/play.c
OBJS = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

BINARIES = $(BINDIR)/master $(BINDIR)/player $(BINDIR)/view $(BINDIR)/play

.PHONY: build clean deps docker play run-catedra

build: $(BINARIES)
	@echo "✔ build ok"

$(BINDIR)/master: $(OBJDIR)/ipc.o $(OBJDIR)/master.o | $(BINDIR)
	$(CC) $^ -o $@ $(LDLIBS)

$(BINDIR)/player: $(OBJDIR)/ipc.o $(OBJDIR)/player.o | $(BINDIR)
	$(CC) $^ -o $@ $(LDLIBS)

$(BINDIR)/view: $(OBJDIR)/ipc.o $(OBJDIR)/view.o | $(BINDIR)
	$(CC) $^ -o $@ $(LDLIBS) $(LIBS_VIEW)

$(BINDIR)/play: $(OBJDIR)/play.o | $(BINDIR)
	$(CC) $^ -o $@ $(LIBS_VIEW)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR) $(OBJDIR):
	mkdir -p $@

clean:
	rm -rf $(OBJDIR)
	@echo "✔ clean"

deps:
	@echo ">> Instalando dependencias (ncurses)..."
	@apt-get update -y
	@DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends libncurses-dev
	@echo "✔ ncurses instalado"

play: $(BINDIR)/play
	$(BINDIR)/play

docker:
	docker run --rm -it -v "$$PWD":/work -w /work agodio/itba-so-multi-platform:3.0 bash

# =========================
#  MODO MÁSTER CÁTEDRA
# =========================
# Parámetros por defecto (override: make run-catedra W=30 H=20 N=4 D=50 T=120 S=123)
W ?= 20        # ancho
H ?= 20        # alto
D ?= 200       # delay ms
T ?= 10        # timeout s
S ?= 0         # seed
N ?= 2         # jugadores

# Ejecuta el binario de la cátedra "ChompChamps" con tu view y N instancias de tu player.
# Busca ./ChompChamps o ./ChompChamps/ChompChamps en la raíz del repo.
run-catedra: $(BINDIR)/view $(BINDIR)/player
	@# Detectar binario de la cátedra
	@if [ -x ./ChompChamps ]; then M=./ChompChamps; \
	elif [ -x ./ChompChamps/ChompChamps ]; then M=./ChompChamps/ChompChamps; \
	else echo "No encuentro el binario 'ChompChamps'. Copialo en ./ChompChamps o ./ChompChamps/ChompChamps"; exit 1; fi; \
	args=""; for i in $$(seq 1 $(N)); do args="$$args $(BINDIR)/player"; done; \
	echo ">> $$M -w $(W) -h $(H) -d $(D) -t $(T) -s $(S) -v $(BINDIR)/view -p $$args"; \
	$$M -w $(W) -h $(H) -d $(D) -t $(T) -s $(S) -v $(BINDIR)/view -p $$args
