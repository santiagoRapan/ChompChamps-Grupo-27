# -------- config --------
CC       := gcc
CSTD     := -std=c99
WARNS    := -Wall -Wextra -pedantic
INCLUDES := -Iinclude
CFLAGS   := $(CSTD) $(WARNS) $(INCLUDES)
LDFLAGS  :=

# Try to detect ncurses (wide -> regular). Override via: make NCURSES_LIBS="-lncurses"
NCURSES_LIBS ?= $(shell pkg-config --libs ncursesw 2>/dev/null || pkg-config --libs ncurses 2>/dev/null || echo -lncursesw)

# Some systems split terminfo; if you see link errors, you can run:
#   make clean && make NCURSES_LIBS="-lncursesw -ltinfo"
# or set it permanently above.

SRC_DIR  := src
OBJ_DIR  := build
BIN_DIR  := bin

SRC_COMMON := game_functions.c ipc.c
OBJ_COMMON := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRC_COMMON))

# -------- defaults --------
.PHONY: all clean deps shell run run_headless

all: $(BIN_DIR)/master $(BIN_DIR)/player $(BIN_DIR)/view

# -------- binaries --------
$(BIN_DIR)/master: $(OBJ_DIR)/master.o $(OBJ_COMMON) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/player: $(OBJ_DIR)/player.o $(OBJ_COMMON) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/view: $(OBJ_DIR)/view.o $(OBJ_COMMON) | $(BIN_DIR)
	$(CC) $^ -o $@ $(NCURSES_LIBS) $(LDFLAGS)

# -------- objects --------
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# -------- dirs --------
$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

# -------- convenience --------
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Open the course container (run from HOST)
shell:
	docker run -it --rm -v $$PWD:/workspace agodio/itba-so-multi-platform:3.0 bash

# Install deps INSIDE the container
deps:
	apt-get update && apt-get install -y libncurses-dev pkg-config

# Example runs
run: all
	@echo "Running: master + view + 2 players (15x15, delay 300ms, timeout 20s, seed 123)"
	./$(BIN_DIR)/master -w 15 -h 15 -d 300 -t 20 -s 123 -v ./$(BIN_DIR)/view -p ./$(BIN_DIR)/player ./$(BIN_DIR)/player

run_headless: all
	@echo "Running: master + 2 players (no view)"
	./$(BIN_DIR)/master -w 15 -h 15 -d 300 -t 20 -s 123 -p ./$(BIN_DIR)/player ./$(BIN_DIR)/player

# Optional: build only one target
master: $(BIN_DIR)/master
player: $(BIN_DIR)/player
view:   $(BIN_DIR)/view
