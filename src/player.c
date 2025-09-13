#include <stdio.h>
#include "structs.h"
#include "game_functions.h"
#include "ipc.h"
#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

static game_state_t* game_state = NULL;
static game_sync_t* game_sync = NULL;
static int id = -1;

static void find_my_id() {
    pid_t my_pid = getpid();
    for (unsigned int i = 0; i < game_state->player_count; i++) {
        if (game_state->players[i].pid == my_pid) {
            id = i;
            return;
        }
    }
    return;
}

void reader_enter() {
    sem_wait(&game_sync->writer_mutex);
    sem_wait(&game_sync->reader_count_mutex);
    
    game_sync->readers_count++;
    if (game_sync->readers_count == 1) {
        sem_wait(&game_sync->state_mutex);
    }
    
    sem_post(&game_sync->reader_count_mutex);
    sem_post(&game_sync->writer_mutex);
}

void reader_exit() {
    sem_wait(&game_sync->reader_count_mutex);
    
    game_sync->readers_count--;
    if (game_sync->readers_count == 0) {
        sem_post(&game_sync->state_mutex);
    }
    
    sem_post(&game_sync->reader_count_mutex);
}

int evaluate_cell(int* board, int x, int y, int width, int height) {
    if (!is_valid_position(x, y, width, height)) {
        return INVALID_POSITION_SCORE; // Posición inválida
    }
     
    if (!is_cell_free(board, x, y, width, height)) {
        return OCCUPIED_CELL_SCORE; // Celda ocupada
    }
    
    int reward = get_cell_value(board, x, y, width, height);
    int score = reward * BASE_REWARD_MULTIPLIER; // Valor base de la recompensa
    
    // Bonificar celdas que nos acercan al centro (más opciones futuras)
    int center_x = game_state->width / 2;
    int center_y = game_state->height / 2;
    int distance_to_center = abs(x - center_x) + abs(y - center_y);
    score += (CENTER_BONUS_MAX - distance_to_center); // Bonificar cercanía al centro
    
    // Contar celdas libres adyacentes (movilidad futura)
    int free_neighbors = 0;
    for (int dir = 0; dir < NUM_DIRECTIONS; dir++) {
        int nx = x + MOVE_DELTAS[dir][0];
        int ny = y + MOVE_DELTAS[dir][1];
        if (is_cell_free(board, nx, ny, width, height)) {
            free_neighbors++;
        }
    }
    return score + free_neighbors * MOBILITY_BONUS;
}

static signed char calculate_move(int* board,int x, int y, bool blocked, int width, int height) {
    int best = -1, best_score = -1;

    for (unsigned char d = 0; d < NUM_DIRECTIONS; d++) {
        if (is_valid_move(board, d, x, y, blocked, width,  height)) {
            int nx = x + MOVE_DELTAS[(int)d][0];
            int ny = y + MOVE_DELTAS[(int)d][1];
            int s = evaluate_cell(board, nx, ny, width, height);
            if (s > best_score) { best_score = s; best = d; }
        }
    }
    return (signed char)best;  // -1 si no encontro
}


int main(int argc, char * argv[]){
    if(argc != 3){
        fprintf(stderr, "Uso: %s <width> <height>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int width = atoi(argv[1]);
    int height = atoi(argv[2]);

    game_state = setup_game_state(width,height);
    game_sync = setup_game_sync();
    if(!game_state || !game_sync){
        fprintf(stderr, "Error al inicializar el estado del juego o la sincronización\n");
        return EXIT_FAILURE;
    }

    find_my_id();
    if(id==-1){ 
        return EXIT_FAILURE;
    }

    bool game_over = false;
    int copy[width*height];
    int copy_x, copy_y, copy_blocked;
    
    do{
        sem_wait(&game_sync->player_turn[id]); //post lo hace master (es su responsabilidad asignar turnos)
        reader_enter();
        game_over = game_state->is_game_over;

        if(game_over){ 
            reader_exit();
            break;
        }
        for(int i = 0; i < width*height; i++){
            copy[i] = game_state->board[i];
        }
        copy_x = game_state->players[id].x;
        copy_y = game_state->players[id].y;
        copy_blocked = game_state->players[id].blocked;
        reader_exit();

        signed char move = calculate_move(copy, copy_x, copy_y, copy_blocked, width, height);
        if(move == -1){
            break;
        }
        write(STDOUT_FILENO, &move, MOVE_DATA_SIZE);
    }while(!game_over);
    return 0;
}
