###############################################################################
# Makefile mínimo para TP1 (versión simplificada)
# Comandos usados: make docker | make deps | make clean | make build | make play
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

# Fuentes necesarias (sin shm_tool ni extras)
SRCS = $(SRCDIR)/ipc.c $(SRCDIR)/master.c $(SRCDIR)/player.c $(SRCDIR)/view.c $(SRCDIR)/play.c
OBJS = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

BINARIES = $(BINDIR)/master $(BINDIR)/player $(BINDIR)/view $(BINDIR)/play

.PHONY: build clean deps docker play

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
