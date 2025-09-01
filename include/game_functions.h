#ifndef GAME_FUNCTIONS_H
#define GAME_FUNCTIONS_H
#include "structs.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>


// tablero y movimientos
void initialize_board(game_state_t* state, unsigned int seed);
bool is_valid_position(game_state_t* state, int x, int y);
bool is_cell_free(game_state_t* state, int x, int y);
int get_cell_value(game_state_t* state, int x, int y);
void set_cell_owner(game_state_t* state, int x, int y, int player_id);
void place_players_on_board(game_state_t* state);
void apply_move(game_state_t* game_state,int  player_id, unsigned char move);
int is_valid_move(game_state_t* state, int player_id, unsigned char move);
int determine_winner(game_state_t* state);
#endif