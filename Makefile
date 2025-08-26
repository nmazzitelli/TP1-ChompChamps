# Detecta SO para sumar -lrt solo en Linux
UNAME_S := $(shell uname -s)

CC      := gcc
CFLAGS  := -Wall -Wextra -g -O0 -Iinclude -MMD -MP
LDLIBS  := -pthread
ifeq ($(UNAME_S),Linux)
LDLIBS  += -lrt
endif
LIBS_VIEW = -lncurses

# Fuentes y binarios
SRC  := $(wildcard src/*.c)
BIN  := $(patsubst src/%.c,bin/%,$(SRC))
DEPS := $(BIN:%=%.d)

# --- COMPILACIÓN ---
all: $(BIN)

# regla patrón: src/foo.c -> bin/foo
bin/%: src/%.c | bin
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

bin:
	mkdir -p bin

bin/view: src/view.c
	$(CC) $(CFLAGS) -o $@ $< -pthread -lrt $(LIBS_VIEW)

clean:
	rm -rf bin

list:
	@echo "Fuentes:  $(SRC)"
	@echo "Binarios: $(BIN)"

-include $(DEPS)

# --- EJECUCIÓN ---
VIEW   ?= ./bin/view
PLAYER ?= ./bin/player
MASTER ?= ./bin/master
W ?= 10
H ?= 10
D ?= 200
T ?= 10
S ?= 0

# Lanza view en background y luego master con un jugador (demo)
run-demo: all
	@echo "Lanzando view (bg) y master (fg)..."
	$(VIEW) & \
	$(MASTER) -p $(PLAYER)

# Igual que arriba pero pasando flags reales
run: all
	@echo "Run: w=$(W) h=$(H) d=$(D) t=$(T) s=$(S)"
	$(VIEW) & \
	$(MASTER) -w $(W) -h $(H) -d $(D) -t $(T) -s $(S) -v $(VIEW) -p $(PLAYER)

kill:
	@pkill -f 'bin/view'   || true
	@pkill -f 'bin/master' || true

.PHONY: all clean list run run-demo kill