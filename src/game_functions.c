#define _POSIX_C_SOURCE 200809L
#include "../include/game_functions.h"
#include "../include/structs.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>


const int MOVE_DELTAS[NUM_DIRECTIONS][2] = {
    {0, -1},  // ARRIBA
    {1, -1},  // ARRIBA_DERCHA
    {1, 0},   // DERECHA
    {1, 1},   // ABAJO_DERECHA
    {0, 1},   // ABAJO
    {-1, 1},  // ABAJO_IZQUIERDA
    {-1, 0},  // IZQUIERDA
    {-1, -1}  // ARRIBA_IZQUIERDA
};


void initialize_board(game_state_t* state, unsigned int seed) {
    srand(seed);
    for (int i = 0; i < state->width * state->height; i++) {
        state->board[i] = (rand() % MAX_CELL_VALUE) + MIN_CELL_VALUE;
    }
}

bool is_valid_position(int x, int y, int width, int height) {
    return x >= 0 && x < width && y >= 0 && y < height;
}

bool is_cell_free(int* board, int x, int y, int width, int height) {
    if(!is_valid_position(x, y, width, height)){
        return false;
    }
    int cell_value = board[y * width + x];
    return cell_value > 0; // Valores positivos = celdas libres
}

int get_cell_value(int* board, int x, int y, int width, int height) {
    if (!is_valid_position(x, y, width, height)) {
        return -1;
    }
    return board[y * width + x];
}

bool is_player_blocked(int* board, int x, int y, int width, int height) {
    for (int move = 0; move < 8; move++) {
        int new_x = x + MOVE_DELTAS[move][0];
        int new_y = y + MOVE_DELTAS[move][1];
        if (is_cell_free(board, new_x, new_y, width, height)) {
            return false; // Al menos un movimiento es posible
        }
    }
    return true; // No hay movimientos posibles
}

void set_cell_owner(game_state_t* state, int x, int y, int player_id) {
    if (is_valid_position(x, y, state->width, state->height)) {
        state->board[y * state->width + x] = -(player_id + PLAYER_ID_OFFSET);
    }
}

void place_players_on_board(game_state_t* state){
    int positions[][2] = {
        {PLAYER_POSITION_MARGIN, PLAYER_POSITION_MARGIN},                                    // Jugador 0
        {state->width - PLAYER_POSITION_OFFSET, PLAYER_POSITION_MARGIN},                     // Jugador 1
        {PLAYER_POSITION_MARGIN, state->height - PLAYER_POSITION_OFFSET},                    // Jugador 2
        {state->width - PLAYER_POSITION_OFFSET, state->height - PLAYER_POSITION_OFFSET},     // Jugador 3
        {state->width / 2, PLAYER_POSITION_MARGIN},                     // Jugador 4
        {PLAYER_POSITION_MARGIN, state->height / 2},                    // Jugador 5
        {state->width - PLAYER_POSITION_OFFSET, state->height / 2},     // Jugador 6
        {state->width / 2, state->height - PLAYER_POSITION_OFFSET},     // Jugador 7
        {state->width / 2, state->height / 2}      // Jugador 8
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

    int reward = get_cell_value(game_state->board, new_x, new_y, game_state->width, game_state->height);

    game_state->players[player_id].x = new_x;
    game_state->players[player_id].y = new_y;

    game_state->players[player_id].score += reward;

    set_cell_owner(game_state, new_x, new_y, player_id);
    game_state->players[player_id].valid_moves++;
}

int is_valid_move(int* board, unsigned char move, int x, int y, bool blocked, int width, int height) {
    if (move > MOVE_UP_LEFT) {
        return false;
    }
    
    if (blocked){return false;}

    int current_x = x; 
    int current_y = y;  
    
    int new_x = current_x + MOVE_DELTAS[move][0];
    int new_y = current_y + MOVE_DELTAS[move][1];
    
    return is_cell_free(board, new_x, new_y, width, height);
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
