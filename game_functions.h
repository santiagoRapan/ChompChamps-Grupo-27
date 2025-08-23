#ifndef GAME_FUNCTIONS_H
#define GAME_FUNCTIONS_H
#include "structs.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

//memoria compartida
int create_shared_memory(const char* name, size_t size);
int destroy_shared_memory(const char* name);
void *attach_shared_memory(int shm_fd, size_t size, bool read_only);
void detach_shared_memory(void* addr, size_t size);
void cleanup_shared_memory(game_state_t* gamestate, game_sync_t* gamesync);
void clear_shm(const char* name);
int connect_to_shared_memory(const char* name, bool read_only);
game_state_t* setup_game_state(int width, int height);
game_sync_t* setup_game_sync();

//semaforos
void initialize_semaphores(game_sync_t* sync, int player_count);
void cleanup_semaphores(game_sync_t* sync, int player_count);

//tablero y movimientos
void initialize_board(game_state_t* state, unsigned int seed);
bool is_valid_position(game_state_t* state, int x, int y);
bool is_cell_free(game_state_t* state, int x, int y);
int get_cell_value(game_state_t* state, int x, int y);
void set_cell_owner(game_state_t* state, int x, int y, int player_id);
void place_players_on_board(game_state_t* state);
void apply_move(game_state_t* game_state,int  player_id, unsigned char move);
int is_valid_move(game_state_t* state, int player_id, unsigned char move);

#endif