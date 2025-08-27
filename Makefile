# -------- config --------
CC       := gcc
CSTD     := -std=c99
WARNS    := -Wall -Wextra -pedantic
INCLUDES := -Iinclude
CFLAGS   := $(CSTD) $(WARNS) $(INCLUDES) 
LDFLAGS  :=

NCURSES_LIBS := -lncurses

# -------- dirs --------
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
container:
	docker run -it --rm -v $$PWD:/workspace agodio/itba-so-multi-platform:3.0 bash

# Install deps INSIDE the container
ncurses:
	apt install -y libncurses-dev

# Example runs
run_def:
	@echo "Running: master with default paramameters"
	./$(BIN_DIR)/master -p ./$(BIN_DIR)/player ./$(BIN_DIR)/player

run_view: all
	@echo "Running: master with default paramameters and view"
	./$(BIN_DIR)/master -v ./$(BIN_DIR)/view -p ./$(BIN_DIR)/player ./$(BIN_DIR)/player

MAX_PLAYERS := 9

# -------- runtime params --------
w       ?=
h       ?=
d       ?=
t       ?=
s       ?=
v       ?=
p       ?=

run:
	@# ---- validation: players required (1..9) ----
	@if [ -z "$(strip $(p))" ]; then \
		echo "Error: You must pass players. Example: make run players=\"./bin/player ./bin/player\""; \
		exit 1; \
	fi
	@players_count=$$(echo "$(p)" | wc -w); \
	if [ $$players_count -lt 1 ] || [ $$players_count -gt 9 ]; then \
		echo "Error: players must contain between 1 and 9 paths; got $$players_count"; \
		exit 1; \
	fi
	@# ---- build the argv only for flags you set ----
	@RUN_ARGS=""; \
	if [ -n "$(strip $(w))" ]; then RUN_ARGS="$$RUN_ARGS -w $(w)"; fi; \
	if [ -n "$(strip $(h))" ]; then RUN_ARGS="$$RUN_ARGS -h $(h)"; fi; \
	if [ -n "$(strip $(d))" ]; then RUN_ARGS="$$RUN_ARGS -d $(d)"; fi; \
	if [ -n "$(strip $(t))" ]; then RUN_ARGS="$$RUN_ARGS -t $(t)"; fi; \
	if [ -n "$(strip $(s))" ]; then RUN_ARGS="$$RUN_ARGS -s $(s)"; fi; \
	if [ -n "$(strip $(v))" ]; then RUN_ARGS="$$RUN_ARGS -v $(v)"; fi; \
	RUN_ARGS="$$RUN_ARGS -p $(p)"; \
	echo "Running: master with custom parameters"; \
	./$(BIN_DIR)/master $$RUN_ARGS

# Optional: build only one target
master: $(BIN_DIR)/master
player: $(BIN_DIR)/player
view:   $(BIN_DIR)/view