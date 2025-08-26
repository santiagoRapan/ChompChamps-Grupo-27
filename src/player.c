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

void find_my_id() {
    pid_t my_pid = getpid();
    for (int i = 0; i < game_state->player_count; i++) {
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

int evaluate_cell(int x, int y) {
    if (!is_valid_position(game_state, x, y)) {
        return -1000; // Posición inválida
    }
    
    if (!is_cell_free(game_state, x, y)) {
        return -1000; // Celda ocupada
    }
    
    int reward = get_cell_value(game_state, x, y);
    int score = reward * 10; // Valor base de la recompensa
    
    // Bonificar celdas que nos acercan al centro (más opciones futuras)
    int center_x = game_state->width / 2;
    int center_y = game_state->height / 2;
    int distance_to_center = abs(x - center_x) + abs(y - center_y);
    score += (20 - distance_to_center); // Bonificar cercanía al centro
    
    // Contar celdas libres adyacentes (movilidad futura)
    int free_neighbors = 0;
    for (int dir = 0; dir < 8; dir++) {
        int nx = x + MOVE_DELTAS[dir][0];
        int ny = y + MOVE_DELTAS[dir][1];
        if (is_cell_free(game_state, nx, ny)) {
            free_neighbors++;
        }
    }
    score += free_neighbors * 5; // Bonificar movilidad
    
    return score;
}

int calculate_move() {
    int my_x = game_state->players[id].x;
    int my_y = game_state->players[id].y;
    
    int best_score = -1;
    unsigned char best_move = -1;
    bool found_valid = false;
    
    // Evaluar todas las direcciones posibles
    for (unsigned char dir = 0; dir < 8; dir++) {
        if (is_valid_move(game_state, id, dir)) {
            int new_x = my_x + MOVE_DELTAS[dir][0];
            int new_y = my_y + MOVE_DELTAS[dir][1];
            
            int score = evaluate_cell(new_x, new_y);
            
            if (!found_valid || score > best_score) {
                best_score = score;
                best_move = dir;
                found_valid = true;
            }
        }
    }
    
    // No hay movimientos válidos
    //! RESOLVER QUE PASA ACA
    
    return best_move;
}

int main(int argc, char * argv[]){
    if(argc != 3){
        fprintf(stderr, "Uso: %s <width> <height>\n", argv[0]);
        return 1;
    }
    int width = atoi(argv[1]);
    int height = atoi(argv[2]);

    game_state = setup_game_state(width,height);
    game_sync = setup_game_sync();
    if(game_state == NULL || game_sync == NULL){
        fprintf(stderr, "Error al inicializar el estado del juego o la sincronización\n");
        return 1;
    }

    find_my_id();
    if(id==-1){ //no se si vale la pena chequear esto, seria muy raro que falle creo
                //REVISAR
        return -1;
    }
    while(!game_state->is_game_over){
        sem_wait(&game_sync->player_turn[id]);
        int move = calculate_move();

        if (move == -1) {
            break; // No hay movimientos válidos, salir del bucle
        } 
        write(STDOUT_FILENO, &move, sizeof(move));
    }

    return 0;
}
