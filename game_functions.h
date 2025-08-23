#ifndef GAME_FUNCTIONS_H
#define GAME_FUNCTIONS_H
#include "structs.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

int create_shared_memory(const char* name, size_t size);
int destroy_shared_memory(const char* name);
void initialize_board(game_state_t* state, unsigned int seed);
void initialize_semaphores(game_sync_t* sync, int player_count);
bool is_valid_position(game_state_t* state, int x, int y);
bool is_cell_free(game_state_t* state, int x, int y);
void *attach_shared_memory(int shm_fd, size_t size, bool read_only);
void detach_shared_memory(void* addr, size_t size);
void cleanup_shared_memory(game_state_t* gamestate, game_sync_t* gamesync);
void cleanup_semaphores(game_sync_t* sync, int player_count);
int get_cell_value(game_state_t* state, int x, int y);
void set_cell_owner(game_state_t* state, int x, int y, int player_id);
void place_players_on_board(game_state_t* state);
int connect_to_shared_memory(const char* name, bool read_only);
void* setup_game_state(int height, int width );
void* setup_game_sync();
#endif