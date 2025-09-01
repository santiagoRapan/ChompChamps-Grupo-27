#define _POSIX_C_SOURCE 200809L
#include "game_functions.h"
#include "structs.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>


const int MOVE_DELTAS[8][2] = {
    {0, -1},  // UP
    {1, -1},  // UP_RIGHT
    {1, 0},   // RIGHT
    {1, 1},   // DOWN_RIGHT
    {0, 1},   // DOWN
    {-1, 1},  // DOWN_LEFT
    {-1, 0},  // LEFT
    {-1, -1}  // UP_LEFT
};


void initialize_board(game_state_t* state, unsigned int seed) {
    srand(seed);
    for (int i = 0; i < state->width * state->height; i++) {
        state->board[i] = (rand() % 9) + 1;
    }
}


bool is_valid_position(game_state_t* state, int x, int y) {
    return x >= 0 && x < state->width && y >= 0 && y < state->height;
}

bool is_cell_free(game_state_t* state, int x, int y) {
    if(!is_valid_position(state, x, y)){
        return false;
    }
    int cell_value = state->board[y * state->width + x];
    return cell_value > 0; // Valores positivos = celdas libres
}

int get_cell_value(game_state_t* state, int x, int y) {
    if (!is_valid_position(state, x, y)) {
        return -1;
    }
    return state->board[y * state->width + x];
}

void set_cell_owner(game_state_t* state, int x, int y, int player_id) {
    if (is_valid_position(state, x, y)) {
        state->board[y * state->width + x] = -(player_id + 1);
    }
}

void place_players_on_board(game_state_t* state){
    int positions[][2] = {
        {1, 1},                                    // Jugador 1
        {state->width - 2, 1},                     // Jugador 2
        {1, state->height - 2},                    // Jugador 3
        {state->width - 2, state->height - 2},     // Jugador 4
        {state->width / 2, 1},                     // Jugador 5
        {1, state->height / 2},                    // Jugador 6
        {state->width - 2, state->height / 2},     // Jugador 7
        {state->width / 2, state->height - 2},     // Jugador 8
        {state->width / 2, state->height / 2}      // Jugador 9
    };
    
    for (unsigned int i = 0; i < state->player_count; i++) {
        state->players[i].x = positions[i][0];
        state->players[i].y = positions[i][1];
        
        // Marcar la celda inicial como ocupada (sin recompensa)
        set_cell_owner(state, positions[i][0], positions[i][1], (int)i);
    }
}


void apply_move(game_state_t* game_state,int  player_id, unsigned char move) {//TERMINAR
    int current_x = game_state->players[player_id].x;
    int current_y = game_state->players[player_id].y;
    int new_x = current_x + MOVE_DELTAS[move][0];
    int new_y = current_y + MOVE_DELTAS[move][1];

    int reward = get_cell_value(game_state, new_x, new_y);

    game_state->players[player_id].x = new_x;
    game_state->players[player_id].y = new_y;

    game_state->players[player_id].score += reward;

    set_cell_owner(game_state, new_x, new_y, player_id);
    game_state->players[player_id].valid_moves++;
}

int is_valid_move(game_state_t* state, int player_id, unsigned char move) {
    if (move > 7) {
        return false;
    }
    
    if (player_id < 0 || (unsigned int)player_id >= state->player_count) {
        return false;
    }
    
    if (state->players[player_id].blocked) {
        return false;
    }
    
    int current_x = state->players[player_id].x;
    int current_y = state->players[player_id].y;
    
    int new_x = current_x + MOVE_DELTAS[move][0];
    int new_y = current_y + MOVE_DELTAS[move][1];
    
    return is_cell_free(state, new_x, new_y);
 }

 int determine_winner(game_state_t* state) {
    unsigned int winner = -1;
    unsigned int max_score = 0;
    unsigned int min_valid_moves;
    unsigned int min_invalid_moves;
    
    for (unsigned int i = 0; i < state->player_count; i++) {
        if (state->players[i].score > max_score) {
            max_score = state->players[i].score;
            min_valid_moves = state->players[i].valid_moves;
            min_invalid_moves = state->players[i].invalid_moves;
            winner =(int)i;
        }else if (state->players[i].score == max_score) {
            if (state->players[i].valid_moves < min_valid_moves) {
                min_valid_moves = state->players[i].valid_moves;
                winner = (int)i;
            }else if(state->players[i].valid_moves == min_valid_moves && state->players[i].invalid_moves < min_invalid_moves) {
                min_invalid_moves = state->players[i].invalid_moves;
                winner = (int)i;
            }
        }
    }
    return winner;
}
