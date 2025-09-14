# ===== deteccion de SO =====
UNAME_S := $(shell uname -s)

CC      := gcc
CFLAGS  := -Wall -Wextra -Wconversion -Wshadow -Wformat=2 -g -O0 -Iinclude -MMD -MP -D_POSIX_C_SOURCE=200809L
LDLIBS  := -pthread -lm
ifeq ($(UNAME_S),Linux)          # Docker / Linux
  LDLIBS += -lrt                 # librt en Linux
else                             # macOS (solo conveniencia local)
  CFLAGS += -Wno-deprecated-declarations
endif

LIBS_VIEW := -lncurses

# ===== carpetas =====
SRCDIR := src
INCDIR := include
BINDIR := bin
OBJDIR := obj

# ===== fuentes =====
COMMON_SRCS := $(SRCDIR)/ipc.c
MASTER_SRCS := $(SRCDIR)/master.c
PLAYER_SRCS := $(SRCDIR)/player.c
VIEW_SRCS   := $(SRCDIR)/view.c
TOOL_SRCS   := $(SRCDIR)/shm_tool.c
PLAY_SRCS   := $(SRCDIR)/play.c

COMMON_OBJS := $(COMMON_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
MASTER_OBJS := $(MASTER_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
PLAYER_OBJS := $(PLAYER_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
VIEW_OBJS   := $(VIEW_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TOOL_OBJS   := $(TOOL_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
PLAY_OBJS   := $(PLAY_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

DEPS := $(COMMON_OBJS:.o=.d) $(MASTER_OBJS:.o=.d) $(PLAYER_OBJS:.o=.d) $(VIEW_OBJS:.o=.d) $(TOOL_OBJS:.o=.d) $(PLAY_OBJS:.o=.d)

# ===== targets =====
.PHONY: all build clean distclean docker deps run-demo play

all: build

build: $(BINDIR)/master $(BINDIR)/player $(BINDIR)/view $(BINDIR)/shm_tool
	@echo "✔ build ok"

$(BINDIR)/master: $(COMMON_OBJS) $(MASTER_OBJS) | $(BINDIR)
	$(CC) $^ -o $@ $(LDLIBS)

$(BINDIR)/player: $(COMMON_OBJS) $(PLAYER_OBJS) | $(BINDIR)
	$(CC) $^ -o $@ $(LDLIBS)

$(BINDIR)/view: $(COMMON_OBJS) $(VIEW_OBJS) | $(BINDIR)
	$(CC) $^ -o $@ $(LDLIBS) $(LIBS_VIEW)

$(BINDIR)/shm_tool: $(COMMON_OBJS) $(TOOL_OBJS) | $(BINDIR)
	$(CC) $^ -o $@ $(LDLIBS)

$(BINDIR)/play: $(PLAY_OBJS) | $(BINDIR)
	$(CC) $^ -o $@ $(LIBS_VIEW)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR) $(OBJDIR):
	mkdir -p $@

clean:
	rm -rf $(OBJDIR)
	@echo "✔ clean"

distclean: clean
	rm -rf $(BINDIR)

# instala ncurses (debian/ubuntu). en docker de la catedra ya viene.
deps:
	@echo ">> Instalando dependencias (ncurses)..."
	@apt-get update -y
	@DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends libncurses-dev
	@echo "✔ ncurses instalado"


# ejemplo rapido
run-demo: build
	NPLAYERS=2 STEP_MS=1 $(BINDIR)/master -w 20 -h 20 -v $(BINDIR)/view -p $(BINDIR)/player -p $(BINDIR)/player -d 300 -t 10


# compilar y correr play (antes demo2)
play: $(BINDIR)/play
	$(BINDIR)/play

# correr en docker (imagen multi plataforma de la catedra)
docker:
	docker run --rm -it -v "$$PWD":/work -w /work agodio/itba-so-multi-platform:3.0 bash

-include $(DEPS)
